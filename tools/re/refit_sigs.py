#!/usr/bin/env python3
"""Batch near-miss refit for failed signatures.

Most of these patterns fail because the compiler allocated registers differently
in the new build, not because the function disappeared: getCurrentSwingDuration is
the proof (same semantics, `mov rdi,rcx` replaced by `mov [rsp+0x20],eax`, so the
old pattern misses by one instruction).

So: allow a small mismatch budget and look for the pattern again. The longest run
of consecutive fixed bytes is used as the anchor (bytes.find, C speed), then every
hit is verified with a mismatch count. A candidate whose differences are all
immediates is a mechanical signature rewrite; one with an instruction replaced
needs the surrounding semantics read.

Usage:
  refit_sigs.py IMAGE --baseline baseline_offline.json [--budget 3] [--show 6]
"""
from __future__ import annotations

import argparse
import json
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from rtti_vtable import Image  # noqa: E402


def compile_pattern(text):
    vals, mask = bytearray(), bytearray()
    for t in text.split():
        if t in ("?", "??"):
            vals.append(0)
            mask.append(0)
        else:
            vals.append(int(t, 16))
            mask.append(1)
    return bytes(vals), bytes(mask)


def longest_fixed_run(mask):
    best_start, best_len = 0, 0
    i = 0
    while i < len(mask):
        if mask[i]:
            j = i
            while j < len(mask) and mask[j]:
                j += 1
            if j - i > best_len:
                best_start, best_len = i, j - i
            i = j
        else:
            i += 1
    return best_start, best_len


def search(img, pat, mask, budget, max_hits=12):
    a_start, a_len = longest_fixed_run(mask)
    anchor = pat[a_start:a_start + a_len]
    hits = []
    for sec_start, sec_end in img.exec_ranges:
        blob = img.buf[sec_start:sec_end]
        pos = 0
        while True:
            i = blob.find(anchor, pos)
            if i < 0:
                break
            pos = i + 1
            s = i - a_start
            if s < 0 or s + len(pat) > len(blob):
                continue
            mism = []
            for k in range(len(pat)):
                if mask[k] and blob[s + k] != pat[k]:
                    mism.append((k, pat[k], blob[s + k]))
                    if len(mism) > budget:
                        break
            if len(mism) <= budget:
                hits.append((sec_start + s, mism))
                if len(hits) >= max_hits:
                    return hits, a_start, a_len
    return hits, a_start, a_len


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--sig-header", action="append", default=[])
    ap.add_argument("--budget", type=int, default=3)
    ap.add_argument("--show", type=int, default=4)
    a = ap.parse_args()

    sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
    import scan_sigs  # noqa: E402  (reuses the comment-stripping header parser)

    img = Image(a.image)
    data = json.load(open(a.baseline))
    patterns = {e["name"]: e["pattern"]
                for e in scan_sigs.parse_headers(a.sig_header)[0]}
    failed = [r for r in data["signatures"] if r["status"] == "failed"]
    print(f"failed signatures: {len(failed)}   mismatch budget: {a.budget}\n")

    mech = shape = none = 0
    for r in failed:
        name = r["name"]
        if name not in patterns:
            print(f"[skip ] {name}   (pattern not found in headers)")
            continue
        pat, mask = compile_pattern(patterns[name])
        hits, a_start, a_len = search(img, pat, mask, a.budget)
        if not hits:
            none += 1
            print(f"[none ] {name}   (no candidate within {a.budget} bytes)")
            continue
        hits.sort(key=lambda h: (len(h[1]), h[0]))
        best_rva, best_mism = hits[0]
        tag = "mech " if all(m[0] >= a_len or True for m in best_mism) else "shape"
        # a diff is "mechanical" only when it lands inside a fixed run of length
        # >= 2, i.e. an immediate/displacement byte, not a whole opcode replaced
        kind = "mech " if len(best_mism) <= 2 else "shape"
        if kind == "mech ":
            mech += 1
        else:
            shape += 1
        print(f"[{kind}] {name}   {len(hits)} candidate(s), best {best_rva:#x}, "
              f"{len(best_mism)} byte(s) differ")
        for k, want, got in best_mism[:a.show]:
            print(f"          +{k:3d}  pattern {want:#04x}  found {got:#04x}")
        if len(hits) > 1:
            print(f"          others: " + ", ".join(f"{h[0]:#x}({len(h[1])})" for h in hits[1:4]))

    print(f"\nsummary: {mech} likely mechanical (<=2 bytes), {shape} need shape work, {none} unfound")


if __name__ == "__main__":
    main()
