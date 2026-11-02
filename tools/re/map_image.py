#!/usr/bin/env python3
"""Rewrite a PE file so that file offset == RVA.

The reverse-engineering helpers here (rtti_vtable, vtable_slot_match, scan_sigs)
address bytes by RVA, which only works on a memory dump. A file on disk lays its
sections out at PointerToRawData, so every lookup lands somewhere else -- RTTI
name scans still find the strings, but the pointers to them never match, and the
vtable walk returns zero tables with no error.

This maps each section to its VirtualAddress into a sparse image of SizeOfImage
and writes that out, so the same tools work on a plain exe.
"""
from __future__ import annotations

import argparse
import struct


def sections(buf: bytes):
    e = struct.unpack_from("<I", buf, 0x3C)[0]
    nsec = struct.unpack_from("<H", buf, e + 6)[0]
    opt_size = struct.unpack_from("<H", buf, e + 20)[0]
    opt = e + 24
    magic = struct.unpack_from("<H", buf, opt)[0]
    image_base_off = opt + (0x18 if magic == 0x20B else 0x1C)
    base = struct.unpack_from("<Q" if magic == 0x20B else "<I", buf, image_base_off)[0]
    size_of_image = struct.unpack_from("<I", buf, opt + 0x38)[0]
    off = e + 24 + opt_size
    out = []
    for i in range(nsec):
        o = off + i * 40
        name = buf[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", buf, o + 8)
        out.append((name, va, vsize, rawptr, rawsize))
    return base, size_of_image, out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("output")
    a = ap.parse_args()

    with open(a.image, "rb") as f:
        buf = f.read()
    base, size_of_image, secs = sections(buf)
    img = bytearray(size_of_image)
    # Keep the headers: every consumer re-reads the section table from offset 0,
    # and without them a mapped image looks like a zero-filled file.
    img[0:0x1000] = buf[0:0x1000]
    for name, va, vsize, rawptr, rawsize in secs:
        n = min(rawsize, vsize, size_of_image - va, len(buf) - rawptr)
        if n > 0:
            img[va:va + n] = buf[rawptr:rawptr + n]
        print(f"{name:10} va={va:#x} size={vsize:#x} copied={max(n, 0):#x}")
    with open(a.output, "wb") as f:
        f.write(img)
    print(f"base={base:#x} SizeOfImage={size_of_image:#x} -> {a.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
