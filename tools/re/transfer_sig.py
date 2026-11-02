#!/usr/bin/env python3
"""Transfer a resolved 1.21.44 function to 1.21.120 by body fingerprint.

Why this works where a 20-byte signature failed: the signature is one fixed byte
pattern for all builds, but a *body* taken from the old build is 96 bytes of
real code. Register allocation changes a handful of those bytes; the rest stay
put. Matching 96 bytes with a mismatch budget is statistically meaningful in a
93 MB .text, whereas matching a 10-byte anchor is not.

Input: scan JSON for 1.21.44 (rva per signature) + the 1.21.120 image.
Output: for each symbol, the best 1.21.120 candidate and the byte delta.
"""
from __future__ import annotations

import argparse
import json
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from capstone import Cs, CS_ARCH_X86, CS_MODE_64  # noqa: E402
from rtti_vtable import Image  # noqa: E402


def in_exec(img, rva):
    return any(a <= rva < b for a, b in img.exec_ranges)


def longest_fixed_run(values):
    best_start = best_len = 0
    i = 0
    while i < len(values):
        j = i
        while j < len(values) and values[j]:
            j += 1
        if j - i > best_len:
            best_start, best_len = i, j - i
        i = max(j, i + 1)
    return best_start, best_len


def build_template(img44, rva, length):
    """Body bytes from the old build, with every build-specific field masked.

    What changes between two builds of the same function:
      * rel32 call/jump targets       -> immediates
      * rip-relative data references  -> displacements (.rdata addresses move)
      * stack displacements           -> stable, but masking them is harmless
    What stays: opcodes and ModRM bytes, which is what identifies the function.

    Masking has to come from real instruction boundaries, not a byte scan: an
    0xE8 inside a displacement would otherwise mask four unrelated bytes.
    """
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    body = img44.buf[rva:rva + length]
    fixed = bytearray([1] * len(body))
    consumed = 0
    for ins in md.disasm(body, rva):
        if consumed + ins.size > len(body):
            break
        for off, size in ((getattr(ins, "disp_offset", 0), getattr(ins, "disp_size", 0)),
                          (getattr(ins, "imm_offset", 0), getattr(ins, "imm_size", 0))):
            if size and off:
                start = consumed + off
                fixed[start:start + size] = b"\x00" * size
        consumed += ins.size
    return bytes(body[:consumed]), bytes(fixed[:consumed])


def search(img, tmpl, fixed, budget, limit=6):
    a_start, a_len = longest_fixed_run(fixed)
    if a_len < 6:
        return [], a_start, a_len
    anchor = tmpl[a_start:a_start + a_len]
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
            if s < 0 or s + len(tmpl) > len(blob):
                continue
            mism = 0
            for k in range(len(tmpl)):
                if fixed[k] and blob[s + k] != tmpl[k]:
                    mism += 1
                    if mism > budget:
                        break
            if mism <= budget:
                hits.append((mism, sec_start + s))
                if len(hits) >= limit:
                    return hits, a_start, a_len
    return hits, a_start, a_len


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old-json", required=True, help="scan JSON for 1.21.44")
    ap.add_argument("--new-json", required=True, help="scan JSON for 1.21.120")
    ap.add_argument("--old-image", required=True)
    ap.add_argument("--new-image", required=True)
    ap.add_argument("--length", type=int, default=96)
    ap.add_argument("--budget", type=int, default=8)
    ap.add_argument("--only-failed-on-new", action="store_true", default=True)
    a = ap.parse_args()

    old = Image(a.old_image)
    new = Image(a.new_image)
    oj = json.load(open(a.old_json))
    nj = json.load(open(a.new_json))

    old_sigs = {r["name"]: r for r in oj["signatures"]}
    new_sigs = {r["name"]: r for r in nj["signatures"]}

    targets = [n for n, r in new_sigs.items()
               if r["status"] in ("failed", "ambiguous")
               and n in old_sigs and old_sigs[n].get("rvaInt")]

    print(f"candidates to transfer: {len(targets)}  template={a.length}B budget={a.budget}\n")
    ok = 0
    for name in sorted(targets):
        rva44 = old_sigs[name].get("rvaInt") or 0
        if not in_exec(old, rva44):
            rva44 = old_sigs[name].get("hitInt") or int(old_sigs[name]["hit"], 16) if old_sigs[name].get("hit") else 0
        if not in_exec(old, rva44):
            print(f"[skip] {name:44} 1.21.44 target {rva44:#x} is not code")
            continue
        tmpl, fixed = build_template(old, rva44, a.length)
        hits, a_start, a_len = search(new, tmpl, fixed, a.budget)
        if not hits:
            print(f"[miss] {name:44} 1.21.44 {rva44:#x}  no match within {a.budget}")
            continue
        hits.sort()
        best_m, best_rva = hits[0]
        uniq = sum(1 for h in hits if h[0] == best_m)
        verdict = "OK  " if (best_m <= 3 and uniq == 1) else "chk "
        if verdict == "OK  ":
            ok += 1
        print(f"[{verdict}] {name:44} 1.21.44 {rva44:#x} -> 1.21.120 {best_rva:#x}"
              f"  ({best_m} bytes differ, {len(hits)} candidate(s))")

    print(f"\nconfident transfers: {ok}/{len(targets)}")


if __name__ == "__main__":
    main()
