#!/usr/bin/env python3
"""Rewrite an over-fitted signature into an opcode skeleton.

Diagnosis this implements: a pattern like

    40 53 48 81 EC D0 00 00 00 0F 29 BC 24 A0 00 00 00 ...

encodes the stack frame size (D0 00 00 00) and a movaps slot (A0 00 00 00).
Those change when the function gains a local, so the pattern dies on every other
build - measured: GameRenderer::bobHurt exists in 1.21.44 at 0xa76e10 with frame
E0 / slot C0, i.e. the same function, +0x10 of frame.

Masking every immediate and displacement leaves opcode+ModRM bytes, which is what
actually identifies a function. The risk is uniqueness, so match counts are
reported and only single matches are offered as a rewrite.

Output is a ready-to-paste pattern string for SigManager.hpp.
"""
from __future__ import annotations

import argparse
import json
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
import scan_sigs  # noqa: E402
from rtti_vtable import Image  # noqa: E402


def skeletonize(pat: bytes, mask: bytes):
    """Mask every immediate/displacement field, keep opcode and ModRM bytes."""
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    new = bytearray(mask)
    consumed = 0
    for ins in md.disasm(bytes(pat), 0):
        if consumed + ins.size > len(pat):
            break
        for off, size in ((getattr(ins, "disp_offset", 0), getattr(ins, "disp_size", 0)),
                          (getattr(ins, "imm_offset", 0), getattr(ins, "imm_size", 0))):
            if size and off:
                new[consumed + off:consumed + off + size] = b"\x00" * size
        consumed += ins.size
    return new


def to_pattern(pat: bytes, mask: bytes) -> str:
    return " ".join(f"{b:02X}" if m else "?" for b, m in zip(pat, mask))


def find_all(img, pat: bytes, mask: bytes, limit: int):
    """Same semantics as scan_sigs.find_all, against rtti_vtable.Image.

    rtti_vtable.Image exposes exec_ranges as a list, scan_sigs.Image as a method;
    rather than couple the two, search here directly.
    """
    rx = scan_sigs._to_regex(pat, mask)
    hits = []
    for start, end in img.exec_ranges:
        blob = img.buf[start:end]
        for m in rx.finditer(blob):
            hits.append(start + m.start())
            if len(hits) >= limit:
                return hits
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--sig-header", action="append", required=True)
    ap.add_argument("--status", default="failed", choices=["failed", "ambiguous", "both"])
    a = ap.parse_args()

    img = Image(a.image)
    patterns = {e["name"]: e["pattern"] for e in scan_sigs.parse_headers(a.sig_header)[0]}
    data = json.load(open(a.baseline))
    want = {"failed"} if a.status == "failed" else \
           {"ambiguous"} if a.status == "ambiguous" else {"failed", "ambiguous"}

    targets = [r["name"] for r in data["signatures"] if r["status"] in want]

    fixed = unique = multi = none = 0
    recipes = []
    for name in targets:
        if name not in patterns:
            continue
        pat, mask = scan_sigs.compile_pattern(patterns[name])
        sk = skeletonize(pat, mask)
        hits = find_all(img, pat, sk, 6)
        fixed += 1
        if not hits:
            none += 1
            print(f"[none ] {name:44} skeleton not found")
            continue
        if len(hits) == 1:
            unique += 1
            print(f"[uniq ] {name:44} {hits[0]:#x}   ({sum(sk)} / {len(sk)} bytes fixed)")
            recipes.append((name, to_pattern(pat, sk), hits[0]))
        else:
            multi += 1
            print(f"[multi] {name:44} {len(hits)} hits: " + ", ".join(f"{h:#x}" for h in hits[:4]))

    print(f"\nskeleton rewrite: {unique} unique, {multi} multi, {none} not found "
          f"(of {fixed} attempted)")

    if recipes:
        out = "Dump/skeleton_recipes.txt"
        with open(out, "w", encoding="utf-8") as f:
            for name, pat_s, rva in recipes:
                f.write(f"# {name}  -> RVA {rva:#x}\n{pat_s}\n\n")
        print(f"patterns written to {out}")


if __name__ == "__main__":
    main()
