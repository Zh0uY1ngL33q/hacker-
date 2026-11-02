#!/usr/bin/env python3
"""Convert an on-disk PE into the RVA-indexed image layout ModuleDump writes.

ModuleDump images are indexed by RVA, so file offset == RVA. A normal PE is not:
each section's raw data sits at PointerToRawData. Flattening the file to
SizeOfImage bytes with every section copied to its RVA makes the same scanners
work on both artifacts without a second code path.

Sections whose raw size is zero (uninitialised .bss-like data) stay zero-filled,
which is what the loader does anyway.
"""
from __future__ import annotations

import argparse
import struct
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pe")
    ap.add_argument("out")
    a = ap.parse_args()

    with open(a.pe, "rb") as f:
        data = f.read()

    e = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e:e + 4] != b"PE\0\0":
        print("not a PE")
        return 2
    nsec = struct.unpack_from("<H", data, e + 6)[0]
    opt_size = struct.unpack_from("<H", data, e + 20)[0]
    image_size = struct.unpack_from("<I", data, e + 24 + 56)[0]     # SizeOfImage
    hdr_size = struct.unpack_from("<I", data, e + 24 + 60)[0]       # SizeOfHeaders
    sec_off = e + 24 + opt_size

    img = bytearray(image_size)
    # The headers must land at RVA 0, otherwise the flattened file has no section
    # table and every scanner sees zero sections (all signatures "fail").
    if hdr_size:
        img[0:hdr_size] = data[0:hdr_size]
    print(f"SizeOfImage {image_size:#x}, SizeOfHeaders {hdr_size:#x}, sections {nsec}")
    for i in range(nsec):
        h = data[sec_off + i * 40: sec_off + i * 40 + 40]
        name = h[:8].rstrip(b"\0").decode("latin1")
        vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", h, 8)
        n = min(rawsize, vsize if vsize else rawsize)
        if n and rva + n <= image_size:
            img[rva:rva + n] = data[rawptr:rawptr + n]
        print(f"  {name:10} rva={rva:#010x} vsize={vsize:#09x} raw={rawsize:#09x} copied={n:#09x}")

    with open(a.out, "wb") as f:
        f.write(img)
    print(f"wrote {a.out} ({len(img):#x} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
