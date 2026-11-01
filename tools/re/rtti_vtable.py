#!/usr/bin/env python3
"""RTTI -> vtable -> slot resolver for the Minecraft.Windows memory image.

Evidence chain, no guessing:
  type descriptor name  (".?AVLevel@@")
    -> TypeDescriptor RVA  (name_rva - 0x10, vftable+spare prefix)
    -> CompleteObjectLocator (COL+0x0C holds the 4-byte TypeDescriptor RVA,
                              COL+0x00 must be signature 1)
    -> vtable               (8 bytes before the first slot holds the 8-byte VA
                             of the COL in a loaded x64 image)
    -> slots                (absolute VAs; RVA = VA - image_base)

Usage:
  rtti_vtable.py IMAGE --class Level --class Actor            # dump slots
  rtti_vtable.py IMAGE --find Level --find BlockLegacy        # name search
  rtti_vtable.py IMAGE --class Level --slots 0-120 --disasm 3
"""
from __future__ import annotations

import argparse
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
except ImportError:
    Cs = None

MEM_EXECUTE = 0x20000000


class Image:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.buf = f.read()
        e = struct.unpack_from("<I", self.buf, 0x3C)[0]
        self.base = struct.unpack_from("<Q", self.buf, e + 0x30)[0]
        nsec = struct.unpack_from("<H", self.buf, e + 6)[0]
        opt = struct.unpack_from("<H", self.buf, e + 20)[0]
        off = e + 24 + opt
        self.sections = []
        for i in range(nsec):
            raw = self.buf[off + i * 40: off + i * 40 + 40]
            vsize, rva, rawsize = struct.unpack_from("<III", raw, 8)
            chars = struct.unpack_from("<I", raw, 36)[0]
            self.sections.append((rva, max(vsize, rawsize), chars))
        self.exec_ranges = [(r, r + s) for r, s, c in self.sections if c & MEM_EXECUTE]

    def u32(self, rva):
        return struct.unpack_from("<I", self.buf, rva)[0]

    def u64(self, rva):
        return struct.unpack_from("<Q", self.buf, rva)[0]

    def in_exec(self, rva):
        return any(a <= rva < b for a, b in self.exec_ranges)


def find_u32(img, value, start=0):
    pat = struct.pack("<I", value)
    out = []
    pos = start
    while True:
        i = img.buf.find(pat, pos)
        if i < 0:
            return out
        out.append(i)
        pos = i + 1


def find_u64(img, value, start=0):
    pat = struct.pack("<Q", value)
    out = []
    pos = start
    while True:
        i = img.buf.find(pat, pos)
        if i < 0:
            return out
        out.append(i)
        pos = i + 1


def build_name_index(img):
    """Every MSVC RTTI type name in the image -> TypeDescriptor RVA."""
    idx = {}
    pos = 0
    while True:
        i = img.buf.find(b".?AV", pos)
        if i < 0:
            break
        pos = i + 1
        end = img.buf.find(b"\x00", i)
        if not (0 < end - i < 400):
            continue
        idx[img.buf[i:end].decode("latin1")] = i - 0x10
    return idx


def type_descriptors(img, needle: str, idx=None):
    """All (name, td_rva) whose mangled name contains `needle`."""
    idx = idx if idx is not None else build_name_index(img)
    return [(n, r) for n, r in idx.items() if needle in n]


def vtables_for_td(img, td_rva):
    """COLs whose pTypeDescriptor == td_rva, then vtables pointing at them."""
    cols = []
    for hit in find_u32(img, td_rva):
        col = hit - 0x0C
        if col < 0:
            continue
        if img.u32(col) != 1:          # signature
            continue
        cols.append(col)
    tables = []
    for col in cols:
        va = img.base + col
        for hit in find_u64(img, va):
            tables.append((col, hit + 8))
    return cols, tables


def read_slots(img, vtable_rva, limit=400):
    slots = []
    for i in range(limit):
        va = img.u64(vtable_rva + i * 8)
        if va < img.base or va - img.base >= len(img.buf):
            break
        rva = va - img.base
        if not img.in_exec(rva):
            break
        slots.append(rva)
    return slots


def disasm(img, rva, n=4):
    if Cs is None:
        return ""
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = False
    code = img.buf[rva:rva + 60]
    out = []
    for ins in md.disasm(code, img.base + rva):
        out.append(f"{ins.mnemonic} {ins.op_str}".strip())
        if len(out) >= n:
            break
    return " ; ".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--class", dest="classes", action="append", default=[])
    ap.add_argument("--find", action="append", default=[])
    ap.add_argument("--slots", default="0-200")
    ap.add_argument("--disasm", type=int, default=3)
    ap.add_argument("--exact", action="store_true", help="name must equal .?AV<X>@@")
    a = ap.parse_args()

    img = Image(a.image)
    print(f"base={hex(img.base)} size={hex(len(img.buf))} exec_sections={len(img.exec_ranges)}")
    name_idx = build_name_index(img)

    if a.find:
        for needle in a.find:
            print(f"\n--- names containing {needle!r}")
            got = [(n, r) for n, r in name_idx.items() if needle.lower() in n.lower()]
            for n, r in sorted(got)[:80]:
                print(f"   {n:74} td={r:#x}")
            print(f"   ({len(got)} total)")

    lo, _, hi = a.slots.partition("-")
    lo, hi = int(lo), int(hi or 200)

    for cls in a.classes:
        print(f"\n===== {cls}")
        tds = type_descriptors(img, cls, name_idx)
        if a.exact:
            tds = [(n, r) for n, r in tds if n == f".?AV{cls}@@"]
        if not tds:
            print("  no type descriptor found")
            continue
        for name, td in tds[:4]:
            cols, tables = vtables_for_td(img, td)
            print(f"  {name}  td={td:#x} cols={[hex(c) for c in cols]} "
                  f"vtables={[hex(t) for _, t in tables]}")
            if not tables:
                continue
            # prefer the table whose -8 slot really points at a COL
            col, vt = tables[0]
            slots = read_slots(img, vt)
            print(f"  vtable rva={vt:#x} slots={len(slots)}")
            for i in range(lo, min(hi, len(slots))):
                print(f"    [{i:3}] {slots[i]:#010x}  {disasm(img, slots[i], a.disasm)}")


if __name__ == "__main__":
    main()
