#!/usr/bin/env python3
"""Parse x64 PE .pdata/.xdata into exact [begin,end) function boundaries.

Leaf functions (no stack frame) are intentionally absent from .pdata; this
module also discovers them from `ret`-terminated runs inside executable
sections so that tiny getters (e.g. `mov rax,[rcx+0x118]; ret`) are not lost.
"""
import struct
from pathlib import Path

class PEImage:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        assert d[pe:pe+4] == b'PE\0\0', 'not a PE'
        coff = pe + 4
        self.nsec = struct.unpack_from('<H', d, coff+2)[0]
        optsz = struct.unpack_from('<H', d, coff+16)[0]
        opt = coff + 20
        self.magic = struct.unpack_from('<H', d, opt)[0]
        self.image_base = struct.unpack_from('<Q', d, opt+24)[0] if self.magic == 0x20b \
            else struct.unpack_from('<I', d, opt+28)[0]
        self.size_of_image = struct.unpack_from('<I', d, opt+56)[0]
        dd = opt + (112 if self.magic == 0x20b else 96)
        self.exception_rva, self.exception_size = struct.unpack_from('<II', d, dd+3*8)
        self.sections = []
        for i in range(self.nsec):
            o = opt + optsz + i*40
            name = d[o:o+8].rstrip(b'\0').decode('latin1', 'replace')
            vs, va, rs, rp = struct.unpack_from('<IIII', d, o+8)
            ch = struct.unpack_from('<I', d, o+36)[0]
            self.sections.append({'name':name, 'vsize':vs, 'rva':va, 'rawsize':rs,
                                  'rawptr':rp, 'chars':ch})
        self.functions = self._parse_pdata()

    def rva2off(self, rva):
        for s in self.sections:
            span = max(s['vsize'], s['rawsize'])
            if s['rva'] <= rva < s['rva'] + span:
                off = s['rawptr'] + (rva - s['rva'])
                if off < len(self.data):
                    return off
        return None

    def read(self, rva, n):
        o = self.rva2off(rva)
        return None if o is None else self.data[o:o+n]

    def _parse_pdata(self):
        if not self.exception_rva or not self.exception_size:
            return []
        o = self.rva2off(self.exception_rva)
        if o is None: return []
        out = []
        for i in range(self.exception_size // 12):
            b, e, u = struct.unpack_from('<III', self.data, o + i*12)
            if b == 0 or e <= b or e > self.size_of_image:
                continue
            out.append({'begin':b, 'end':e, 'unwind':u, 'kind':'pdata'})
        out.sort(key=lambda f: f['begin'])
        return out

    def function_at(self, rva):
        lo, hi = 0, len(self.functions)-1
        while lo <= hi:
            mid = (lo+hi)//2
            f = self.functions[mid]
            if f['begin'] <= rva < f['end']:
                return f
            if rva < f['begin']: hi = mid-1
            else: lo = mid+1
        return self._leaf_at(rva)

    def _leaf_at(self, rva):
        """Tiny leaf getter: scan backward to a ret boundary, forward to ret."""
        o = self.rva2off(rva)
        if o is None: return None
        d = self.data
        start = o
        while start > max(0, o-64) and d[start-1] != 0xC3:
            start -= 1
        end = o
        while end < min(len(d), o+64) and d[end] != 0xC3:
            end += 1
        if end >= len(d) or d[end] != 0xC3: return None
        # translate file offsets back to RVA using the containing section
        for s in self.sections:
            if s['rawptr'] <= start < s['rawptr'] + s['rawsize']:
                b = s['rva'] + (start - s['rawptr'])
                e = s['rva'] + (end + 1 - s['rawptr'])
                if b <= rva < e and e-b <= 64:
                    return {'begin':b, 'end':e, 'unwind':0, 'kind':'leaf'}
        return None

    def body(self, f):
        return self.read(f['begin'], f['end']-f['begin'])

if __name__ == '__main__':
    import sys, json
    img = PEImage(sys.argv[1])
    print(json.dumps({'imageBase':hex(img.image_base), 'sizeOfImage':hex(img.size_of_image),
                      'pdata_functions':len(img.functions),
                      'exception_dir':[hex(img.exception_rva),hex(img.exception_size)]}, indent=2))
