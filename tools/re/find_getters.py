#!/usr/bin/env python3
"""Find a "returns &member" getter: `lea rax,[rcx+disp32]; ret`.

Why this shape: a C++ member getter that returns a pointer/reference to a field
compiles to exactly these 9 bytes (48 8D 81 dd dd dd dd C3) on MSVC x64 when the
displacement does not fit in a byte. Counting call xrefs (E8 rel32 -> target)
ranks them: a getter used all over the engine has hundreds of callers, a local
helper has one or two.

This does not by itself prove *which* getter is which; it narrows the field to a
handful of candidates whose call sites can then be inspected.
"""
from __future__ import annotations

import argparse
import re
import struct
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from rtti_vtable import Image  # noqa: E402


def find_getters(img, lea=True, mov=False):
    out = []
    for start, end in img.exec_ranges:
        blob = img.buf[start:end]
        if lea:
            for m in re.finditer(rb"\x48\x8d\x81(.{4})\xc3", blob, re.S):
                disp = struct.unpack("<i", m.group(1))[0]
                out.append((start + m.start(), disp, "lea"))
        if mov:
            for m in re.finditer(rb"\x48\x8b\x81(.{4})\xc3", blob, re.S):
                disp = struct.unpack("<i", m.group(1))[0]
                out.append((start + m.start(), disp, "mov"))
    return out


def count_xrefs(img, targets):
    """E8 rel32 -> target, and the tail-call form E9 rel32."""
    counts = {t: 0 for t in targets}
    tset = set(targets)
    for start, end in img.exec_ranges:
        blob = img.buf[start:end]
        for m in re.finditer(rb"[\xe8\xe9]", blob):
            i = m.start()
            if i + 5 > len(blob):
                continue
            rel = struct.unpack_from("<i", blob, i + 1)[0]
            tgt = start + i + 5 + rel
            if tgt in tset:
                counts[tgt] += 1
    return counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--min-xrefs", type=int, default=8)
    ap.add_argument("--limit", type=int, default=40)
    a = ap.parse_args()

    img = Image(a.image)
    getters = find_getters(img, lea=True, mov=True)
    print(f"tiny getters (lea/mov rax,[rcx+disp]; ret): {len(getters)}")

    counts = count_xrefs(img, [g[0] for g in getters])
    rows = [(c, rva, disp, kind) for (rva, disp, kind), c in
            ((g, counts[g[0]]) for g in getters)]
    rows = [r for r in rows if r[0] >= a.min_xrefs]
    rows.sort(reverse=True)

    print(f"with >= {a.min_xrefs} direct call xrefs: {len(rows)}\n")
    print(f"{'xrefs':>6}  {'rva':>10}  {'disp':>8}  kind")
    for c, rva, disp, kind in rows[:a.limit]:
        print(f"{c:6}  {rva:#10x}  {disp:#8x}  {kind}")


if __name__ == "__main__":
    main()
