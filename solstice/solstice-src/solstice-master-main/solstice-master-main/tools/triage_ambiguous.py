#!/usr/bin/env python3
"""Triage signatures that resolve to more than one place, or to a wrong place.

Two discriminators, both computed against the dump, neither needing a rebuild:

  * value gate (offset table only): the code at the match must read a plausible
    vtable index / member offset. A lookalike instruction sequence essentially
    never does.
  * vtable reference test (any pattern): a virtual method's address appears as a
    pointer in a data section. The dump is a virtual image, so those pointers are
    module_base + rva and can be searched for directly. A candidate referenced
    from data is the method that something actually calls; a candidate that is
    referenced from nowhere is usually a static helper that happens to look
    similar.

Usage:
    python tools/triage_ambiguous.py <image> <verify.json> [--sections sections.json] [--json out.json]
"""

import argparse
import json
import struct
import sys


def load(path):
    with open(path, "rb") as f:
        return f.read()


def sections_from_manifest(path):
    m = json.load(open(path, encoding="utf-8"))
    base = int(m["moduleBase"], 16)
    secs = []
    for s in m["sections"]:
        rva = int(s["rva"], 16)
        size = int(s["virtualSize"], 16)
        chars = int(s["characteristics"], 16)
        secs.append({"rva": rva, "size": size, "chars": chars,
                     "name": s["name"].strip() or "<blank>",
                     "exec": bool(chars & 0x20000000),
                     "read": bool(chars & 0x40000000)})
    return base, secs


def data_regions(img, secs):
    """Ranges a vtable / object pointer can live in: readable, not executable."""
    out = []
    for s in secs:
        if s["exec"] or not s["read"]:
            continue
        end = min(len(img), s["rva"] + s["size"])
        if end > s["rva"]:
            out.append((s["rva"], end))
    return out


def count_ptr_refs(img, regions, value):
    needle = struct.pack("<Q", value)
    total = 0
    first = []
    for start, end in regions:
        pos = start
        while True:
            pos = img.find(needle, pos, end)
            if pos < 0:
                break
            total += 1
            if len(first) < 4:
                first.append(pos)
            pos += 1
    return total, first


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("verify")
    ap.add_argument("--sections", required=True)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    img = load(args.image)
    base, secs = sections_from_manifest(args.sections)
    regions = data_regions(img, secs)
    results = json.load(open(args.verify, encoding="utf-8"))["results"]

    print("module base 0x%X, %d data region(s) totalling %.1f MB"
          % (base, len(regions), sum(e - s for s, e in regions) / 1048576.0))

    out = []
    for r in results:
        if r["status"] not in ("AMBIGUOUS", "SUSPECT"):
            continue

        # verify_sigs truncates the match list; re-scan-free approach: use what it
        # kept, plus values it already computed for the chosen one.
        hits = [int(h, 16) for h in r.get("match_rvas", [])]
        cands = []
        for h in hits:
            c = {"rva": "0x%X" % h}
            if r["kind"] == "offset":
                at = h + r["disp_off"]
                if at + 4 <= len(img):
                    raw = struct.unpack_from("<i", img, at)[0]
                    val = raw // 8 if r["offset_type"] == "Index" else raw
                    c["value"] = val
                    c["plausible"] = (0 <= val <= 4096) if r["offset_type"] == "Index" \
                                     else abs(val) <= 0x20000
            n, refs = count_ptr_refs(img, regions, base + h)
            c["data_refs"] = n
            c["ref_sites"] = ["0x%X" % x for x in refs]
            cands.append(c)

        good_value = [c for c in cands if c.get("plausible")]
        referenced = [c for c in cands if c["data_refs"] > 0]

        verdict = "unresolved"
        if r["kind"] == "offset" and len(good_value) == 1:
            verdict = "value-gate"
        elif r["kind"] == "sig" and len(referenced) == 1:
            verdict = "vtable-ref"
        elif len(referenced) >= 1 and len(good_value) == 1 and len(referenced) == 1:
            verdict = "both"
        elif r["kind"] == "offset" and len(good_value) == 0:
            verdict = "no plausible value"

        out.append({"name": r["name"], "kind": r["kind"], "status": r["status"],
                    "matches": r["matches"], "verdict": verdict, "candidates": cands})

        print("\n%-46s %-8s %-5s -> %s" % (r["name"], r["status"], r["matches"], verdict))
        for c in cands:
            extra = ""
            if "value" in c:
                extra = " value=%-8s%s" % (c["value"], " OK" if c["plausible"] else " BAD")
            print("    %-12s refs=%-3d%s" % (c["rva"], c["data_refs"], extra))

    summary = {}
    for e in out:
        summary[e["verdict"]] = summary.get(e["verdict"], 0) + 1
    print("\nverdicts: " + ", ".join("%s=%d" % kv for kv in sorted(summary.items())))

    if args.json:
        json.dump(out, open(args.json, "w", encoding="utf-8"), indent=2)
        print("written to %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
