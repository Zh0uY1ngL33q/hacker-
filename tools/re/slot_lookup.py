#!/usr/bin/env python3
"""Resolve a failed signature by finding it inside a known class's vtable slot.

rtti_vtable.vtables_for_td returns every 8-byte occurrence of the COL pointer,
including coincidences, and vtable_slot_match then keeps the table with the most
slots -- which is zero when the first coincidence wins. This walks every
candidate properly: a real vtable starts with executable pointers, so the walk is
self-validating.

Usage:
  slot_lookup.py <mapped-image> --sig-header SigManager.hpp \
      --pair ItemRenderer=ItemRenderer_render --pair Mob=Mob_getCurrentSwingDuration

The image must be RVA-addressed; build it with map_image.py.
"""
from __future__ import annotations

import argparse
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scan_sigs  # noqa: E402


class Img:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.buf = f.read()
        e = struct.unpack_from("<I", self.buf, 0x3C)[0]
        opt = e + 24
        magic = struct.unpack_from("<H", self.buf, opt)[0]
        self.base = struct.unpack_from("<Q" if magic == 0x20B else "<I", self.buf, opt + (0x18 if magic == 0x20B else 0x1C))[0]
        nsec = struct.unpack_from("<H", self.buf, e + 6)[0]
        opt_size = struct.unpack_from("<H", self.buf, e + 20)[0]
        secs = []
        off = opt + opt_size
        for i in range(nsec):
            o = off + i * 40
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", self.buf, o + 8)
            chars = struct.unpack_from("<I", self.buf, o + 36)[0]
            secs.append((va, vsize, chars))
        self.exec = [(va, va + vsize) for va, vsize, ch in secs if ch & 0x20000000]

    def u32(self, rva):
        return struct.unpack_from("<I", self.buf, rva)[0]

    def u64(self, rva):
        return struct.unpack_from("<Q", self.buf, rva)[0]

    def in_exec(self, rva):
        return any(a <= rva < b for a, b in self.exec)

    def find_all(self, needle, limit=64):
        out, i = [], self.buf.find(needle)
        while i >= 0 and len(out) < limit:
            out.append(i)
            i = self.buf.find(needle, i + 1)
        return out


def find_in(img, start, length, pat, mask):
    blob = img.buf[start:start + length]
    n = len(pat)
    for i in range(len(blob) - n + 1):
        for k in range(n):
            if mask[k] and blob[i + k] != pat[k]:
                break
        else:
            return i
    return -1


def vtables(img, cls):
    td_name = b".?AV" + cls.encode() + b"@@"
    name_at = img.buf.find(td_name)
    if name_at < 0:
        return []
    td = name_at - 16
    out = []
    for hit in img.find_all(struct.pack("<I", td)):
        col = hit - 12
        if col < 0 or img.u32(col) != 1:
            continue
        for vptr in img.find_all(struct.pack("<Q", img.base + col)):
            vt = vptr + 8
            slots = []
            for i in range(1200):
                va = img.u64(vt + i * 8)
                rva = va - img.base if va >= img.base else va
                if not (0 <= rva < len(img.buf)) or not img.in_exec(rva):
                    break
                slots.append(rva)
            if len(slots) >= 2:
                out.append((col, vt, slots))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--sig-header", action="append", required=True)
    ap.add_argument("--pair", action="append", required=True, help="Class=Signature_name")
    ap.add_argument("--window", type=int, default=0x200)
    a = ap.parse_args()

    img = Img(a.image)
    patterns = {e["name"]: e["pattern"] for e in scan_sigs.parse_headers(a.sig_header)[0]}
    for pair in a.pair:
        cls, sig = pair.split("=", 1)
        pat_s = patterns.get(sig)
        if not pat_s:
            print(f"{sig:44} no pattern in header")
            continue
        pat, mask = scan_sigs.compile_pattern(pat_s)
        tables = vtables(img, cls)
        if not tables:
            print(f"{sig:44} class {cls}: no vtable")
            continue
        hits = []
        for col, vt, slots in tables:
            for i, rva in enumerate(slots):
                off = find_in(img, rva, a.window, pat, mask)
                if off >= 0:
                    hits.append((len(slots), i, rva, off, vt))
        if len(hits) == 1:
            n, i, rva, off, vt = hits[0]
            print(f"{sig:44} OK  slot {i:3} fn {rva:#x} (pattern +{off:#x}) vtable {vt:#x} slots {n}")
        elif hits:
            print(f"{sig:44} AMB " + ", ".join(f"{h[1]}@{h[2]:#x}" for h in hits[:5]))
        else:
            print(f"{sig:44} MISS in {sum(len(t[2]) for t in tables)} slots of {cls}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
