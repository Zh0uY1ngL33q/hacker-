#!/usr/bin/env python3
"""Audit byte patches against the dumped image.

A byte patch is only safe when
  1. it starts on an instruction boundary,
  2. it ends on an instruction boundary (it must not cut an instruction in half),
  3. every register the removed instructions *defined* is dead afterwards, or is
     redefined before being read.

(1) and (2) are mechanical. (3) is a heuristic on the following window, which is
enough to catch the failure that actually bit this project: an 8-byte "zero xmm0"
patch that also removed `movaps xmm1, xmm9`, whose only reader sits nine bytes
later in the same basic block.

Table entries mirror the DEFINE_*_PATCH_FUNC call sites; keep them in sync.
"""
from __future__ import annotations

import argparse
import json
import re
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from rtti_vtable import Image  # noqa: E402

# (label, signature name, byte delta, patch length, replacement hex, source)
# replacement "nop" means the call site NOP-fills the range.
PATCHES = [
    ("NoSwitchAnimation", "ItemInHandRenderer_render_bytepatch", 0x0, 4, "0F 57 C0 90",
     "Animations.cpp DEFINE_PATCH_FUNC(patchNoSwitchAnimation ...)"),
    ("DefaultSwing", "ItemInHandRenderer_renderItem_bytepatch", 0x0, 8, "nop",
     "Animations.cpp DEFINE_NOP_PATCH_FUNC(patchDefaultSwing, ..., 0x8)"),
    ("DefaultSwing2", "ItemInHandRenderer_renderItem_bytepatch", 0xB, 8, "nop",
     "Animations.cpp DEFINE_NOP_PATCH_FUNC(patchDefaultSwing2, ...+11, 0x8)"),
    ("FluxSwing", "FluxSwing", 0x5, 5, "nop",
     "Animations.cpp DEFINE_NOP_PATCH_FUNC(patchFluxSwing, ..., 0x5)"),
]

# read-modify-write: the destination is also an input, so NOPing the preceding
# definition leaves a live value behind.
RMW = re.compile(r"^(add|sub|mul|div|and|or|xor|inc|dec|shl|shr|sar|"
                 r"addss|subss|mulss|divss|xorps|pxor|andps|orps|comiss|ucomiss)$")

WRITE_MNEMONICS = re.compile(r"^(mov|movss|movaps|movups|movdqa|movdqu|xorps|pxor|mulss|addss|subss|lea|movzx|movsx|set|inc|dec|add|sub)")


def regs_of(op_str: str):
    return set(re.findall(r"\b(xmm\d+|xmm1[0-5]|r[a-z0-9]+|e[a-z]{2})\b", op_str))


def dest_reg(ins):
    if not WRITE_MNEMONICS.match(ins.mnemonic):
        return None
    first = ins.op_str.split(",")[0].strip()
    m = re.fullmatch(r"(xmm\d+|r[a-z0-9]+|e[a-z]{2})", first)
    return m.group(1) if m else None


def audit(img, md, label, site, size, replacement, source):
    code = img.buf[site:site + 48]
    covered, addr = [], 0
    for ins in md.disasm(code, img.base + site):
        covered.append((ins.address - img.base, ins.size, ins.mnemonic, ins.op_str))
        addr += ins.size
        if addr >= size:
            break
    total = sum(c[1] for c in covered)

    verdict = "ALIGNED"
    if not covered or covered[0][0] != site:
        verdict = "NO_DECODE"
    elif total > size:
        verdict = "CUTS_INSTRUCTION"

    removed_defs = set()
    for _, _, mn, ops in covered:
        d = dest_reg(type("I", (), {"mnemonic": mn, "op_str": ops})())
        if d:
            removed_defs.add(d)

    replaced_defs = set()
    if replacement != "nop":
        rep = bytes.fromhex(replacement)
        for ins in md.disasm(rep, img.base + site):
            d = dest_reg(ins)
            if d:
                replaced_defs.add(d)

    live = {}
    if verdict == "ALIGNED":
        for reg in removed_defs - replaced_defs:
            for ins in md.disasm(img.buf[site + size:site + size + 200], img.base + site + size):
                if ins.mnemonic == "ret" or ins.mnemonic == "jmp":
                    break
                reads = regs_of(ins.op_str)
                d = dest_reg(ins)
                rmw = bool(RMW.match(ins.mnemonic))
                if reg in reads and (reg != d or rmw):
                    live[reg] = (ins.address - img.base, ins.mnemonic, ins.op_str)
                    break
                if d == reg:
                    break

    print(f"\n=== {label}  site={site:#x} size={size}  -> {verdict}")
    print(f"    source: {source}")
    print(f"    original bytes: {img.buf[site:site + size].hex(' ')}")
    print(f"    replacement   : {replacement}")
    for rva, n, mn, ops in covered:
        mark = "" if rva + n <= site + size else "   <-- runs past the patch"
        print(f"      +{rva - site:#04x} ({n}B) {mn} {ops}{mark}")
    if removed_defs:
        print(f"    defined by removed code: {', '.join(sorted(removed_defs))}"
              f" | also defined by replacement: {', '.join(sorted(replaced_defs)) or '-'}")
    if live:
        for reg, use in live.items():
            print(f"    !! {reg} is still read at {use[0]:#x} ({use[1]} {use[2]})"
                  f" and nothing in the patch defines it")
    return verdict, live


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--baseline", required=True, help="scan_sigs.py --json output")
    a = ap.parse_args()

    img = Image(a.image)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    sigs = {r["name"]: r for r in json.load(open(a.baseline))["signatures"]}

    misaligned = livedef = unresolved = 0
    for label, signame, delta, size, replacement, source in PATCHES:
        entry = sigs.get(signame)
        if not entry or entry["status"] == "failed" or not entry.get("rva"):
            print(f"\n=== {label}: signature {signame} unresolved -> patch is a no-op")
            unresolved += 1
            continue
        site = int(entry["rva"], 16) + delta
        v, live = audit(img, md, label, site, size, replacement, source)
        if v != "ALIGNED":
            misaligned += 1
        if live:
            livedef += 1
    print(f"\nsummary: {misaligned} misaligned, {livedef} with a live register removed, "
          f"{unresolved} unresolved signature(s)")
    return 1 if (misaligned or livedef or unresolved) else 0


if __name__ == "__main__":
    sys.exit(main())
