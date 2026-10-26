#!/usr/bin/env python3
"""Turn a sigreport.json near miss into a verified signature.

sigreport.json says, for each pattern that no longer resolves, where it probably
moved to and which concrete bytes there no longer match. That is enough to draft
a corrected pattern -- but a draft is not a signature: it has to resolve exactly
once, and (for the offset table) the value read at the match has to be a
plausible vtable index / member offset. Both can be checked against the dump
offline, which is why this exists: a pattern that is only verified by rebuilding
and re-injecting costs a full round trip per mistake.

Usage:
    python tools/refit_sigs.py <dump.image> <sigreport.json> [--src .] [--json out.json]
"""

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import verify_sigs as vs


def apply_diffs(pattern, diffs):
    """Return the pattern with each reported byte replaced by the byte found."""
    tokens = pattern.split()
    for d in diffs:
        # `at` indexes the pattern *including* the '?' placeholders, which is the
        # same indexing as the runtime uses, so it maps 1:1 onto the token list.
        tokens[d["at"]] = "%02X" % (d["to"] & 0xFF)
    return " ".join(tokens)


def read_value(img, rva, disp_off, offset_type):
    at = rva + disp_off
    if at + 4 > len(img.data):
        return None
    raw = struct.unpack_from("<i", img.data, at)[0]
    return raw // 8 if offset_type == "Index" else raw


def plausible(value, offset_type):
    if value is None:
        return False
    if offset_type == "Index":
        return 0 <= value <= 4096
    return abs(value) <= 0x20000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("report")
    ap.add_argument("--src", default=".")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    img = vs.Image(args.image)
    start, end, _ = img.code_span()

    defs, _hardcoded = vs.load_definitions(args.src)
    by_name = {d["name"]: d for d in defs}

    report = json.load(open(args.report, encoding="utf-8"))
    near = {e["name"]: e for e in report.get("nearMisses", [])}

    failed = list(report["offsets"]["failed"]) + list(report["signatures"]["failed"])

    out = []
    for name in failed:
        d = by_name.get(name)
        miss = near.get(name, {})
        entry = {"name": name, "kind": d["kind"] if d else "?", "verified": False}

        if not d:
            entry["note"] = "not found in source"
            out.append(entry)
            continue

        if not miss.get("found"):
            entry["note"] = "no near miss; needs disassembly"
            out.append(entry)
            continue

        new_pattern = apply_diffs(d["pattern"], miss.get("diffs", []))
        hits = vs.scan(img.data, start, end, vs.parse_pattern(new_pattern))

        entry["old_pattern"] = d["pattern"]
        entry["new_pattern"] = new_pattern
        entry["diff_count"] = len(miss.get("diffs", []))
        entry["hits"] = len(hits)
        entry["hit_rvas"] = ["0x%X" % h for h in hits[:6]]
        entry["near_rva"] = miss.get("rva")

        if d["kind"] == "offset":
            values = [read_value(img, h, d["disp_off"], d["offset_type"]) for h in hits]
            entry["values"] = values
            good = [i for i, v in enumerate(values) if plausible(v, d["offset_type"])]
            entry["plausible_hits"] = good
            # Exactly one hit, or several hits of which exactly one reads a
            # plausible value: the value gate is a strong discriminator, because a
            # wrong match almost never lands on a small index / member offset.
            if len(hits) == 1 and good:
                entry["verified"] = True
                entry["chosen_rva"] = "0x%X" % hits[0]
                entry["value"] = values[0]
            elif len(good) == 1:
                entry["verified"] = True
                entry["chosen_rva"] = "0x%X" % hits[good[0]]
                entry["value"] = values[good[0]]
                entry["note"] = "disambiguated by value (%d matches)" % len(hits)
            else:
                entry["note"] = "ambiguous or implausible after fix"
        else:
            if len(hits) == 1:
                entry["verified"] = True
                entry["chosen_rva"] = "0x%X" % hits[0]
            else:
                entry["note"] = "ambiguous after fix"

        out.append(entry)

    ok = [e for e in out if e["verified"]]
    print("failed at runtime : %d" % len(out))
    print("verified offline  : %d" % len(ok))
    print("needs work        : %d" % (len(out) - len(ok)))
    print()
    print("%-46s %-6s %-5s %-6s %s" % ("NAME", "KIND", "DIFF", "HITS", "RESULT"))
    for e in out:
        if e["verified"]:
            extra = ("value=%s" % e.get("value")) if e["kind"] == "offset" else ""
            if e.get("note"):
                extra += " " + e["note"]
        else:
            extra = e.get("note", "")
        print("%-46s %-6s %-5s %-6s %s"
              % (e["name"], e["kind"], e.get("diff_count", "-"), e.get("hits", "-"), extra))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=2)
        print("\nwritten to %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
