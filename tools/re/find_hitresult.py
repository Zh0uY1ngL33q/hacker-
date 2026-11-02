#!/usr/bin/env python3
"""Identify Level::getHitResult by looking at what its callers do.

Step 1  collect every `lea/mov rax,[rcx+disp32]; ret` (a member getter).
Step 2  one pass over .text building target -> call sites, but only for those
        getter addresses, so memory stays small (the previous attempt stored
        every call target and was killed for taking minutes).
Step 3  score each candidate by whether the code right after a call reads the
        HitResult layout the client already assumes:
            +0x18 HitType, +0x20 mBlockPos, +0x2C mPos, +0x38 mEntity
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
import time

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from rtti_vtable import Image  # noqa: E402

FIELDS = (0x18, 0x1C, 0x20, 0x2C, 0x38)
WEIGHT = {0x18: 4, 0x1C: 1, 0x20: 4, 0x2C: 2, 0x38: 2}


def collect_getters(img):
    out = {}
    for start, end in img.exec_ranges:
        blob = img.buf[start:end]
        for pat, kind in ((rb"\x48\x8d\x81(.{4})\xc3", "lea"), (rb"\x48\x8b\x81(.{4})\xc3", "mov")):
            for m in re.finditer(pat, blob, re.S):
                out[start + m.start()] = (struct.unpack("<i", m.group(1))[0], kind)
    return out


def call_sites(img, wanted, cap=120):
    """One pass; only remember targets that are in `wanted`."""
    found = {t: [] for t in wanted}
    t0 = time.time()
    for start, end in img.exec_ranges:
        blob = img.buf[start:end]
        n = len(blob)
        for i, b in enumerate(blob):
            if b != 0xE8 and b != 0xE9:
                continue
            if i + 5 > n:
                continue
            rel = struct.unpack_from("<i", blob, i + 1)[0]
            tgt = start + i + 5 + rel
            if tgt in found and len(found[tgt]) < cap:
                found[tgt].append((start + i, b == 0xE8))
        print(f"  scanned {start:#x}..{end:#x}  ({time.time()-t0:.0f}s)", flush=True)
    return found


def score(img, md, rva, sites):
    """Only dword accesses count.

    `cmp qword ptr [rax+0x18], 0xf` is std::string's SSO length check and was
    drowning the signal; HitType is a uint32 compared against 0..3, and mBlockPos
    is three ints at +0x20, so require the 4-byte form.
    """
    hits = {f: 0 for f in FIELDS}
    examples = []
    for site, is_call in sites:
        start = site + 5
        for ins in md.disasm(img.buf[start:start + 56], start):
            txt = ins.op_str
            if ins.mnemonic == "cmp" and "dword ptr [rax + 0x18]" in txt:
                m = re.search(r", (0x[0-9a-f]+|\d+)$", txt)
                if m:
                    imm = int(m.group(1), 0)
                    if imm <= 3:
                        hits[0x18] += 1
                        if len(examples) < 3:
                            examples.append((site, f"{ins.mnemonic} {txt}"))
            elif "dword ptr [rax + %#x]" % 0x20 in txt and ins.mnemonic in ("mov", "movsxd", "lea", "cmp"):
                hits[0x20] += 1
                if len(examples) < 3:
                    examples.append((site, f"{ins.mnemonic} {txt}"))
            elif "dword ptr [rax + %#x]" % 0x2C in txt:
                hits[0x2C] += 1
            if ins.mnemonic == "ret":
                break
    total = sum(WEIGHT[f] * hits[f] for f in FIELDS)
    return total, hits, examples


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--min-xrefs", type=int, default=4)
    ap.add_argument("--top", type=int, default=8)
    a = ap.parse_args()

    img = Image(a.image)
    md = Cs(CS_ARCH_X86, CS_MODE_64)

    g = collect_getters(img)
    print(f"getters: {len(g)}", flush=True)
    print("building call index (one pass)...", flush=True)
    sites = call_sites(img, set(g.keys()))

    rows = []
    for rva, (disp, kind) in g.items():
        s = sites.get(rva) or []
        if len(s) < a.min_xrefs:
            continue
        total, hits, ex = score(img, md, rva, s)
        if total <= 0:
            continue
        rows.append((total, rva, disp, kind, len(s), hits, ex))
    rows.sort(reverse=True)

    print(f"\n{'score':>6} {'xrefs':>6} {'rva':>10} {'disp':>8} kind   +18/+1c/+20/+2c/+38")
    for total, rva, disp, kind, nx, hits, ex in rows[:a.top]:
        h = "/".join(str(hits[f]) for f in FIELDS)
        print(f"{total:6} {nx:6} {rva:#10x} {disp:#8x} {kind:5}  {h}")
        for site, txt in ex:
            print(f"        call@{site:#x} -> {txt}")


if __name__ == "__main__":
    main()
