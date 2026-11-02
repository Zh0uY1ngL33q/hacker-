"""Build a field map for a class from its own vtable.

A getter compiles to three bytes of work: load a member, return it. In a vtable
that shows up as either

    mov rax, [rcx + disp32] ; ret
    mov rcx, [rcx + disp32] ; jmp <real getter>      (tail call, often a cast)

Both forms name a member offset without any guessing, so walking a class's vtable
gives a verified table of (offset -> slot) pairs. That is enough to check the
hand-maintained offsets in OffsetProvider against the binary instead of trusting
a value that was copied forward from an older build.

Usage:
    python tools/re/field_map.py <rva-image> --class ".?AVActor@@" [--all]
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rtti_vtable import Image, build_name_index, vtables_for_td  # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_64  # noqa: E402


def scan_slots(img, vtable):
    """Return {offset: [slot, ...]} for every getter-shaped slot."""
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    base = img.base
    buf = img.buf
    out = {}
    slot = 0
    while True:
        pos = vtable + slot * 8
        if pos + 8 > len(buf):
            break
        fn = struct.unpack_from("<Q", buf, pos)[0]
        if not (base <= fn < base + len(buf)):
            break
        rva = fn - base
        if not (0x1000 <= rva < 0x8b70000):
            break

        code = buf[rva:rva + 32]
        offset = None
        if len(code) >= 8 and code[0] == 0x48 and code[1] == 0x8B:
            modrm = code[2]
            mod = modrm >> 6
            reg = (modrm >> 3) & 7
            rm = modrm & 7
            if mod == 2 and rm == 1 and code[7] == 0xC3:
                offset = struct.unpack_from("<i", code, 3)[0]
            elif mod == 1 and rm == 1 and code[7] == 0xC3:
                offset = struct.unpack_from("<b", code, 3)[0]
            elif mod == 2 and rm == 1 and len(code) >= 14:
                # tail call: 48 8B 81 disp32 E9 rel32
                if code[7] == 0xE9:
                    offset = struct.unpack_from("<i", code, 3)[0]
        if offset is not None and 0 < offset < 0x10000:
            out.setdefault(offset, []).append(slot)
        slot += 1
        if slot > 4000:
            break
    return out, slot


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--class", dest="cls", action="append", required=True)
    args = ap.parse_args()

    img = Image(args.image)
    index = build_name_index(img)
    md = Cs(CS_ARCH_X86, CS_MODE_64)

    for name in args.cls:
        td = index.get(name)
        if not td:
            print(f"{name}: no RTTI")
            continue
        cols, tables = vtables_for_td(img, td)
        vt = tables[0][1] if isinstance(tables[0], tuple) else tables[0]
        offsets, count = scan_slots(img, vt)
        print(f"\n=== {name}  vtable {vt:#x}  slots scanned {count} ===")
        for off in sorted(offsets):
            slots = offsets[off]
            print(f"  +{off:#06x} ({off:5d})  slot {slots[:6]}")


if __name__ == "__main__":
    main()
