#!/usr/bin/env python3
"""Find a function in a newer build by the set of known functions it calls.

Byte signatures stop transferring when a function is recompiled: one changed
immediate shifts every following instruction, so an exact pattern that spans ten
bytes stops matching even though the function is still there and still does the
same thing. The *callees* are far more stable. If a function in 1.21.44 calls
getLevel, getNameTag and three others, the 1.21.120 build almost certainly has
exactly one function that calls the same five -- and those five addresses are
known in both builds, because their own signatures still resolve.

This turns "find a function whose bytes changed" into a lookup, and it is what
makes the next game update cheap: run the offline signature scan, take the
handful of names that failed, and match each one here.

Usage:
  call_match.py OLD.bin NEW.bin OLD_SCAN.json NEW_SCAN.json SYMBOL [--top 6]

OLD.bin / NEW.bin must be RVA images produced by map_image.py.
"""
from __future__ import annotations

import argparse
import bisect
import json
import re
import struct
import sys

TEXT_LO = 0x1000


def sections(buf: bytes):
    e = struct.unpack_from("<I", buf, 0x3C)[0]
    nsec = struct.unpack_from("<H", buf, e + 6)[0]
    opt = struct.unpack_from("<H", buf, e + 20)[0]
    off = e + 24 + opt
    out = []
    for i in range(nsec):
        raw = buf[off + i * 40: off + i * 40 + 40]
        name = raw[:8].rstrip(b"\x00").decode("latin1")
        vsize, rva, rawsize = struct.unpack_from("<III", raw, 8)
        chars = struct.unpack_from("<I", raw, 36)[0]
        out.append({"name": name, "rva": rva, "vsize": vsize,
                    "rawsize": rawsize, "chars": chars,
                    "exec": bool(chars & 0x20000000)})
    return out


def text_bounds(secs):
    lo = min(s["rva"] for s in secs if s["exec"])
    hi = max(s["rva"] + max(s["vsize"], s["rawsize"]) for s in secs if s["exec"])
    return lo, hi


def function_ranges(buf: bytes, secs):
    """Locate .pdata by shape instead of by name, so this works on any build."""
    text_lo, text_hi = text_bounds(secs)
    best = []
    for s in secs:
        start, size = s["rva"], min(s["vsize"], len(buf) - s["rva"])
        if size < 0x4000 or s["exec"]:
            continue
        ranges = []
        prev = 0
        for i in range(0, size, 12):
            begin, end, _unwind = struct.unpack_from("<III", buf, start + i)
            if not (text_lo <= begin < end <= text_hi) or begin < prev:
                break
            prev = begin
            ranges.append((begin, end))
        if len(ranges) > len(best):
            best = ranges
    if not best:
        raise SystemExit("no .pdata-like section found; is this an RVA image?")
    return best


def contain(ranges, starts, rva):
    i = bisect.bisect_right(starts, rva) - 1
    if i >= 0 and ranges[i][0] <= rva < ranges[i][1]:
        return ranges[i]
    return None


def call_sites(buf: bytes):
    """Every `call rel32` as (site, target)."""
    out = []
    for m in re.finditer(rb"\xE8", buf):
        site = m.start()
        if site + 5 > len(buf):
            break
        rel = struct.unpack_from("<i", buf, site + 1)[0]
        target = site + 5 + rel
        if TEXT_LO <= target < len(buf):
            out.append((site, target))
    return out


def scan_symbols(path: str):
    d = json.load(open(path))
    out = {}
    for e in d.get("signatures", []):
        if e.get("status") in ("ok", "ambiguous") and e.get("rvaInt"):
            out[e["name"]] = e["rvaInt"]
    for e in d.get("offsets", []):
        pass  # fields are structure offsets, not addresses
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old_image")
    ap.add_argument("new_image")
    ap.add_argument("old_scan")
    ap.add_argument("new_scan")
    ap.add_argument("symbol")
    ap.add_argument("--top", type=int, default=6)
    a = ap.parse_args()

    old = open(a.old_image, "rb").read()
    new = open(a.new_image, "rb").read()
    oldsyms = scan_symbols(a.old_scan)
    newsyms = scan_symbols(a.new_scan)

    if a.symbol not in oldsyms:
        raise SystemExit(f"{a.symbol} does not resolve in the old build either")

    ranges_old = function_ranges(old, sections(old))
    starts_old = [r[0] for r in ranges_old]
    ranges_new = function_ranges(new, sections(new))
    starts_new = [r[0] for r in ranges_new]

    target = contain(ranges_old, starts_old, oldsyms[a.symbol])
    if not target:
        raise SystemExit(f"{a.symbol} at {oldsyms[a.symbol]:#x} is not inside a pdata function")

    # Anchors: symbols that resolved in BOTH builds, so a callee can be named on
    # both sides. Everything else is useless for matching.
    anchors_old = {rva: name for name, rva in oldsyms.items() if name in newsyms}
    anchors_new = {newsyms[name]: name for name in anchors_old.values() if newsyms.get(name)}

    sites_old = call_sites(old)
    wanted_old = {}
    for site, tgt in sites_old:
        if target[0] <= site < target[1] and tgt in anchors_old:
            wanted_old[anchors_old[tgt]] = tgt
    if not wanted_old:
        raise SystemExit("target calls no anchored function; cannot match this way")

    # Index the new build: which function holds the call sites of each anchor.
    sites_new = call_sites(new)
    per_func = {}
    for site, tgt in sites_new:
        name = anchors_new.get(tgt)
        if not name:
            continue
        fn = contain(ranges_new, starts_new, site)
        if not fn:
            continue
        per_func.setdefault(fn, set()).add(name)

    scored = []
    wanted = set(wanted_old)
    for fn, names in per_func.items():
        hit = len(names & wanted)
        if not hit:
            continue
        scored.append((hit - len(names - wanted) * 0.5, hit, fn, names))
    scored.sort(key=lambda x: (-x[0], -(x[2][1] - x[2][0])))

    print(f"{a.symbol}: old function {target[0]:#x}..{target[1]:#x} "
          f"({target[1] - target[0]} bytes), {len(wanted)} anchored callees")
    for i, (score, hit, fn, names) in enumerate(scored[:a.top]):
        missing = sorted(wanted - names)
        extra = sorted(names - wanted)
        print(f"  [{i}] {fn[0]:#x}..{fn[1]:#x} ({fn[1] - fn[0]} bytes) "
              f"score {score:.1f} matched {hit}/{len(wanted)}")
        if missing:
            print(f"      missing: {', '.join(missing[:6])}")
        if extra:
            print(f"      extra:   {', '.join(extra[:6])}")
    if not scored:
        print("  no candidate calls any of them")


if __name__ == "__main__":
    sys.exit(main())
