"""Enumerate every hit of an ambiguous offset signature and show the value each
one would produce.

scan_sigs keeps the first hit and reports the match count, which is enough to know
that a pattern is not unique but not enough to choose. Here all hits are shown
with the value each would give, so agreement between several hits is visible
evidence and a single odd value stands out instead of being adopted silently.

Usage:
    python tools/re/ambig_offsets.py _cmp/img120.bin \
        --field-header <OffsetProvider.hpp> [--only NAME ...]
"""

import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scan_sigs  # noqa: E402


def hits_of(img, entry, limit=64):
    pattern, mask = scan_sigs.compile_pattern(entry["pattern"])
    rx = re.compile(
        b"".join(re.escape(bytes([v])) if m else b"." for v, m in zip(pattern, mask)),
        re.S,
    )
    out = []
    for match in rx.finditer(img.buf):
        out.append(match.start())
        if len(out) >= limit:
            break
    return out


def value_at(img, entry, hit):
    """Reproduce what the initializer macro would read."""
    off = int(entry["idx"], 0)
    ctype = entry.get("ctype", "int")
    fmt = scan_sigs.FIELD_WIDTHS.get(ctype, ("i",))[0]
    raw = struct.unpack_from("<" + fmt, img.buf, hit + off)[0]
    is_index = entry["kind"] == "Index"
    return raw // 8 if is_index else raw, raw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--field-header", required=True, action="append")
    ap.add_argument("--only", nargs="*")
    args = ap.parse_args()

    _sigs, fields = scan_sigs.parse_headers(args.field_header)
    img = scan_sigs.Image(args.image)

    wanted = [f for f in fields if f["kind"] in ("Index", "FieldOffset")]
    if args.only:
        wanted = [f for f in wanted if f["name"] in set(args.only)]

    for entry in wanted:
        found = hits_of(img, entry)
        if len(found) < 2:
            continue
        values = []
        for hit in found:
            try:
                values.append((hit,) + value_at(img, entry, hit))
            except Exception:
                continue
        distinct = sorted({v[1] for v in values})
        print(f"\n{entry['name']}  ({entry['kind']}, offset +{entry['idx']}, "
              f"{len(found)} hits, {len(distinct)} distinct values)")
        for hit, value, raw in values[:12]:
            print(f"    hit {hit:#09x}  raw={raw:#010x}  value={value}")
        if len(distinct) == 1 and len(values) > 1:
            print(f"    -> every hit agrees on {distinct[0]}")


if __name__ == "__main__":
    main()
