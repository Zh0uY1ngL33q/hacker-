#!/usr/bin/env python3
"""Disassemble every RIP-relative reference to one global, with context.

Given a global's RVA, find every instruction that addresses it (by inverting the
RIP-relative displacement, no disassembly needed to locate them) and disassemble
forward from each. Registers that were loaded from the global are tracked, so a
following `[rax+0x1b0]` is marked as a member access on that object -- which is
how a struct offset is recovered when the class has no RTTI to anchor on.

Usage:
    python tools/globref.py <image> <global-rva> [--count N] [--len BYTES]
"""

import argparse
import re
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_REG, X86_REG_RIP


def parse_addr(text):
    return int(text, 16) if text.lower().startswith("0x") else int(text, 10)


# mov/lea reg, [rip+disp32]: REX (0x48/0x4C) + opcode + modrm 0x05/0x0D/... .
# Searching for these short prefixes is thousands of times faster than trying
# every byte offset in a 226 MB image.
RIP_PREFIXES = []
for rex in (0x48, 0x4C):
    for opcode in (0x8B, 0x8D, 0x89):          # mov r64, lea r64, mov [rip], r64
        for modrm in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
            RIP_PREFIXES.append(bytes((rex, opcode, modrm)))


def refs_to(img, target):
    """Instruction starts whose RIP-relative displacement resolves to target."""
    out = []
    for prefix in RIP_PREFIXES:
        pos = 0
        while True:
            pos = img.find(prefix, pos)
            if pos < 0 or pos + 7 > len(img):
                break
            d = struct.unpack_from("<i", img, pos + 3)[0]
            if pos + 7 + d == target:
                out.append(pos)
            pos += 1
    return sorted(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("global_rva")
    ap.add_argument("--len", default="0x30", help="bytes to disassemble per site")
    args = ap.parse_args()

    img = open(args.image, "rb").read()
    target = parse_addr(args.global_rva)
    length = parse_addr(args.len)
    sites = refs_to(img, target)

    print("global 0x%X referenced from %d site(s)\n" % (target, len(sites)))
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    for start in sites:
        print("--- 0x%X ---" % start)
        holding = set()
        for insn in md.disasm(img[start:start + length], start):
            mark = ""
            # A register written by an instruction that read the global now holds it.
            if any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in insn.operands):
                if insn.operands and insn.operands[0].type == X86_OP_REG:
                    mark = "   ; holds 0x%X" % target
                    holding.add(insn.reg_name(insn.operands[0].reg))
            for op in insn.operands:
                if op.type == X86_OP_MEM and insn.reg_name(op.mem.base) in holding and op.mem.disp:
                    mark += "   ; member +0x%X of 0x%X" % (op.mem.disp & 0xFFFFFFFF, target)
            print("  %08X  %-22s %-8s %s%s" % (insn.address, insn.bytes.hex(" "),
                                               insn.mnemonic, insn.op_str, mark))
            if insn.mnemonic.startswith("ret") or insn.mnemonic == "jmp":
                break
        print()


if __name__ == "__main__":
    sys.exit(main())
