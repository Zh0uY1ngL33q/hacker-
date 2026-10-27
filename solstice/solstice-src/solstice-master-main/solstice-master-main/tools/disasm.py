#!/usr/bin/env python3
"""Disassemble a range of an image dump, annotating RIP-relative operands.

Reading 0x19D9FA4 does not say much; `mov rax, [rip+0x1234]` does. This resolves
every RIP-relative memory operand to `-> 0x<absolute rva>` and, when that RVA
falls inside the image, prints the eight bytes living there as a pointer, so a
global that holds a vtable or an object pointer is visible at a glance.

Usage:
    python tools/disasm.py <image> <start> [end] [--back N]

`--back N` starts N bytes before <start>, useful for finding the prologue.
"""

import argparse
import re
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP


def parse_addr(text):
    return int(text, 16) if text.lower().startswith("0x") else int(text, 10)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("start")
    ap.add_argument("end", nargs="?")
    ap.add_argument("--back", default="0")
    args = ap.parse_args()

    img = open(args.image, "rb").read()
    start = parse_addr(args.start) - parse_addr(args.back)
    end = parse_addr(args.end) if args.end else min(len(img), start + 0x400)
    code = img[start:end]

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    # Function boundaries: MSVC pads with int3, so a standalone run of int3 near
    # the range start is a good hint that the previous function ended there.
    for insn in md.disasm(code, start):
        line = "%08X  %-24s %-8s %s" % (insn.address, insn.bytes.hex(" "),
                                        insn.mnemonic, insn.op_str)

        notes = []
        for op in insn.operands:
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                target = insn.address + insn.size + op.mem.disp
                if 0 <= target < len(img) - 8:
                    ptr = struct.unpack_from("<Q", img, target)[0]
                    notes.append("-> 0x%X [%016X]" % (target, ptr))
                else:
                    notes.append("-> 0x%X" % target)
        if notes:
            line += "      ; " + "; ".join(notes)
        print(line)


if __name__ == "__main__":
    sys.exit(main())
