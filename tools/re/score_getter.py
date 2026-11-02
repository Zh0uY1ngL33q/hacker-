#!/usr/bin/env python3
"""Disambiguate a getter by what its callers do with the returned pointer.

A generic getter is just `lea rax,[rcx+D]; ret`. To decide whether a candidate is
`Level::getHitResult`, look at the code that immediately follows each call: a
HitResult caller reads HitType at +0x18 or the block position at +0x20 (layout
taken from the client's own SDK/Minecraft/World/HitResult.hpp).

Counting those hits per candidate turns "one of 82 getters" into "this one, with
N call sites that treat the result as a HitResult".
"""
from __future__ import annotations

import argparse
import re
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from rtti_vtable import Image  # noqa: E402

FIELD_HINTS = {
    0x18: "HitType",
    0x1C: "mFacing",
    0x20: "mBlockPos",
    0x2C: "mPos",
    0x38: "mEntity",
}


def getters(img):
    out = []
    for start, end in img.exec_ranges:
        blob = img.buf[start:end]
        for pat, kind in ((rb"\x48\x8d\x81(.{4})\xc3", "lea"), (rb"\x48\x8b\x81(.{4})\xc3", "mov")):
            for m in re.finditer(pat, blob, re.S):
                out.append((start + m.start(), struct.unpack("<i", m.group(1))[0], kind))
    return out


def xrefs(img, target):
    sites = []
    for start, end in img.exec_ranges:
        blob = img.buf[start:end]
        for m in re.finditer(rb"[\xe8\xe9]", blob):
            i = m.start()
            if i + 5 > len(blob):
                continue
            rel = struct.unpack_from("<i", blob, i + 1)[0]
            if start + i + 5 + rel == target:
                sites.append((start + i, blob[i] == 0xE8))
    return sites


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--disp", type=lambda s: int(s, 0), action="append", default=[],
                    help="only these displacements (default: all)")
    ap.add_argument("--min-hits", type=int, default=1)
    ap.add_argument("--top", type=int, default=15)
    a = ap.parse_args()

    img = Image(a.image)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    cands = getters(img)
    if a.disp:
        want = set(a.disp)
        cands = [c for c in cands if c[1] in want]
    print(f"candidates: {len(cands)}")

    rows = []
    for rva, disp, kind in cands:
        sites = xrefs(img, rva)
        if not sites:
            continue
        hits = {off: 0 for off in FIELD_HINTS}
        examples = []
        for site, _ in sites[:400]:
            code = img.buf[site + 5:site + 5 + 64]
            for ins in md.disasm(code, site + 5):
                for off in FIELD_HINTS:
                    if re.search(r"\[rax \+ %#x\]" % off, ins.op_str):
                        hits[off] += 1
                        if len(examples) < 3:
                            examples.append((site, f"{ins.mnemonic} {ins.op_str}"))
            # only the first few instructions after the call matter
        score = hits[0x18] * 3 + hits[0x20] * 3 + hits[0x2C] + hits[0x38]
        if score >= a.min_hits:
            rows.append((score, rva, disp, kind, len(sites), hits, examples))

    rows.sort(reverse=True)
    print(f"{'score':>5} {'xrefs':>6} {'rva':>10} {'disp':>7} kind   hits(+18/+1c/+20/+2c/+38)")
    for score, rva, disp, kind, nx, hits, ex in rows[:a.top]:
        h = "/".join(str(hits[o]) for o in (0x18, 0x1C, 0x20, 0x2C, 0x38))
        print(f"{score:5} {nx:6} {rva:#10x} {disp:#7x} {kind:5}  {h}")
        for site, txt in ex:
            print(f"        call@{site:#x} -> {txt}")


if __name__ == "__main__":
    main()
