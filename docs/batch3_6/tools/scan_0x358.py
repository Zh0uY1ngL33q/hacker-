"""Batch 3.6 - who owns / writes the matrix that the GL path reads at
ClientInstance+0x358?

Three signatures, all structural:
  ADDR_TAKEN  `lea reg,[base+0x358]`      -> the member is passed somewhere by address
  STORE16     `mov[au]ps [base+0x358],xmm` -> a 16-byte chunk of a matrix is written
  STORE8      `mov [base+0x358], reg`      -> single pointer/qword written

For each hit the enclosing function and the following few instructions are
printed, so a "matrix produced here" claim can be checked instead of assumed.

Offline only.
"""
import bisect
import json
import mmap
import struct
import sys

import capstone
import pefile

IMAGE = r"G:/hacker/unpacked/unpacked_Minecraft.Windows.exe"
OUT = r"G:/hacker/docs/batch3_6/offset-0x358-writers.json"
DISP = 0x358

pe = pefile.PE(IMAGE, fast_load=True)
BASE = pe.OPTIONAL_HEADER.ImageBase

segs = []
for s in pe.sections:
    segs.append((s.VirtualAddress,
                 s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData),
                 s.PointerToRawData, s.SizeOfRawData, s.Characteristics))
segs.sort()
starts = [s[0] for s in segs]
f = open(IMAGE, "rb")
mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)

CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
CS.detail = True
STACK = {"rsp", "rbp"}


def r2o(rva):
    i = bisect.bisect_right(starts, rva) - 1
    if i < 0:
        return None
    va, end, raw, rsize, _ = segs[i]
    if rva >= end or rva - va >= rsize:
        return None
    return raw + (rva - va)


def read(rva, n):
    o = r2o(rva)
    if o is None or o + n > len(mm):
        return None
    return mm[o:o + n]


def enclosing(rva, back=0x600):
    s = max(0, rva - back)
    blob = read(s, rva - s + 8)
    if not blob:
        return rva
    p = blob.rfind(b"\xcc\xcc\xcc\xcc")
    return s + (p + 4) if p >= 0 else s


def scan():
    pat = struct.pack("<i", DISP)
    hits = []
    for a, e, raw, rsize, _ in [(s[0], s[1], s[2], s[3], s[4])
                                for s in segs if s[4] & 0x20000000]:
        i = raw
        end = raw + rsize
        while True:
            i = mm.find(pat, i, end)
            if i < 0:
                break
            rva = a + (i - raw)
            s0 = max(0, rva - 15)
            blob = read(s0, 15 + 24)
            if blob:
                for ins in CS.disasm(blob, BASE + s0):
                    r = ins.address - BASE
                    if r <= rva < r + ins.size:
                        ops = ins.operands
                        if not ops or ops[0].type != capstone.x86.X86_OP_MEM:
                            break
                        m = ops[0].mem
                        if m.disp != DISP:
                            break
                        base = ins.reg_name(m.base)
                        if base in STACK or base in ("rip", ""):
                            break
                        kind = None
                        if ins.mnemonic == "lea":
                            kind = "ADDR_TAKEN"
                        elif ins.mnemonic.startswith("mov") and ops[0].size == 16:
                            kind = "STORE16"
                        elif ins.mnemonic.startswith("mov") and ops[0].size == 8:
                            kind = "STORE8"
                        if kind:
                            hits.append({"rva": r, "kind": kind, "base": base,
                                         "text": "%s %s" % (ins.mnemonic, ins.op_str),
                                         "func": enclosing(r)})
                        break
            i += 1
    return hits


def context(func_rva, span=0x400):
    blob = read(func_rva, span)
    if not blob:
        return []
    out = []
    for ins in CS.disasm(blob, BASE + func_rva):
        note = ""
        if (ins.mnemonic == "call" and ins.operands
                and ins.operands[0].type == capstone.x86.X86_OP_IMM):
            note = "-> %#x" % (ins.operands[0].imm - BASE)
        out.append("%#x  %-46s%s" % (ins.address - BASE,
                                     "%s %s" % (ins.mnemonic, ins.op_str), note))
        if ins.mnemonic == "ret":
            break
    return out


def main():
    hits = scan()
    from collections import Counter
    print("hits at +0x%X: %d" % (DISP, len(hits)))
    print("by kind:", dict(Counter(h["kind"] for h in hits)))
    funcs = {}
    for h in hits:
        funcs.setdefault(h["func"], []).append(h)
    print("distinct enclosing functions:", len(funcs))
    for f_, hs in sorted(funcs.items(), key=lambda kv: -len(kv[1]))[:10]:
        print("  func %#x  %d hit(s)  kinds=%s"
              % (f_, len(hs), sorted({h["kind"] for h in hs})))

    json.dump({"image": IMAGE, "imageBase": hex(BASE), "disp": hex(DISP),
               "hitCount": len(hits),
               "byKind": dict(Counter(h["kind"] for h in hits)),
               "hits": hits,
               "functions": {hex(k): v for k, v in funcs.items()}},
              open(OUT, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print("wrote", OUT)


if __name__ == "__main__":
    sys.exit(main())
