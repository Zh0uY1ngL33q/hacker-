#!/usr/bin/env python3
"""Resolve a signature to a vtable slot instead of to a code address.

A function's *entry* changes when the compiler reallocates registers, but its
vtable slot does not: the slot index is fixed by the class declaration. So when a
byte pattern fails at the entry point it often still matches *inside* the same
function, and the containing vtable slot identifies it exactly.

For every failed signature this:
  1. guesses the class from the signature name prefix,
  2. finds that class's vtables through RTTI,
  3. searches the first N bytes of each slot for the pattern,
  4. reports slots that contain it - exactly one slot means resolved.
"""
from __future__ import annotations

import argparse
import json
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
import scan_sigs  # noqa: E402
from rtti_vtable import Image, build_name_index, vtables_for_td, read_slots  # noqa: E402


def find_in(img, start, length, pat, mask):
    blob = img.buf[start:start + length]
    n = len(pat)
    for i in range(0, len(blob) - n + 1):
        ok = True
        for k in range(n):
            if mask[k] and blob[i + k] != pat[k]:
                ok = False
                break
        if ok:
            return i
    return -1


def slots_for(img, idx, cls):
    td = idx.get(f".?AV{cls}@@")
    if td is None:
        return None
    cols, tables = vtables_for_td(img, td)
    if not tables:
        return None
    # primary vtable = the one with the most slots
    best = max(tables, key=lambda t: len(read_slots(img, t[1])))
    return best[1], read_slots(img, best[1], 800)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--sig-header", action="append", default=[])
    ap.add_argument("--window", type=int, default=0x120)
    ap.add_argument("--class", dest="classes", action="append", default=[],
                    help="extra class guesses (name prefix is used automatically)")
    a = ap.parse_args()

    img = Image(a.image)
    idx = build_name_index(img)
    patterns = {e["name"]: e["pattern"] for e in scan_sigs.parse_headers(a.sig_header)[0]}
    data = json.load(open(a.baseline))

    failed = [r["name"] for r in data["signatures"] if r["status"] == "failed"]
    failed += [r["name"] for r in data["signatures"] if r["status"] == "ambiguous"]

    guesses = list(a.classes)
    for name in failed:
        head = name.split("_")[0]
        if head and head not in guesses:
            guesses.append(head)

    resolved = 0
    for cls in guesses:
        got = slots_for(img, idx, cls)
        if not got:
            continue
        vt, slots = got
        print(f"\n=== {cls}   vtable {vt:#x}  slots {len(slots)}")
        for name in failed:
            if not any(name.startswith(cls + "_") or name == cls for _ in [0]):
                continue
            pat_s = patterns.get(name)
            if not pat_s:
                continue
            pat, mask = scan_sigs.compile_pattern(pat_s)
            hits = []
            for i, rva in enumerate(slots):
                off = find_in(img, rva, a.window, pat, mask)
                if off >= 0:
                    hits.append((i, rva, off))
            if len(hits) == 1:
                i, rva, off = hits[0]
                print(f"  [OK  ] {name:44} slot {i:3}  fn {rva:#x}  pattern at +{off:#x}")
                resolved += 1
            elif hits:
                print(f"  [amb ] {name:44} slots " +
                      ", ".join(f"{h[0]}(+{h[2]:#x})" for h in hits[:6]))
            else:
                print(f"  [miss] {name:44} not inside any slot of {cls}")

    print(f"\nuniquely resolved by vtable slot: {resolved}")


if __name__ == "__main__":
    main()
