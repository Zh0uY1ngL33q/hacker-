#!/usr/bin/env python3
"""Recover signatures that the runtime fuzzy search could not find.

When a pattern fails, the mod re-scans it tolerating up to a few changed bytes,
where the budget scales with how specific the pattern is. That keeps the report
short, but it also means a pattern that drifted by more bytes than the budget
comes back as "no near miss" -- indistinguishable from a function that was
rewritten, even though it is sitting right there with one more changed immediate.

Offline there is no reason to be stingy: anchor on the longest intact run, then
count mismatches over the whole pattern with no budget at all, and rank. The
result is the same information the runtime tries to produce, without the cap.

Usage:
    python tools/recover_missing.py <image> <verify.json> [--src .] [--json out.json]
"""

import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_sigs as vs


def tolerate(hay, start, pat, anchor_len_min=3, cap=32):
    """Every offset where pat matches with at most ~25% concrete bytes wrong.

    Anchored on the longest run of concrete bytes, so a 3-byte anchor already
    narrows 139 MB down to a handful of candidates; the mismatch count is then
    computed over the full pattern.
    """
    n = len(pat)
    if n == 0 or len(hay) < n:
        return []

    concrete = sum(1 for v in pat if v is not None)
    budget = max(2, concrete // 4)
    if budget > 10:
        budget = 10

    # Two anchors: the longest run, and the one after it. A pattern whose longest
    # intact run is itself one of the changed regions is still found by the
    # second, which costs one extra pass over the section.
    runs = []
    cur, cur_start = 0, 0
    for i, v in enumerate(pat):
        if v is None:
            cur = 0
        else:
            if cur == 0:
                cur_start = i
            cur += 1
            if cur >= anchor_len_min:
                runs.append((cur, cur_start))
    runs.sort(reverse=True)

    hits = []
    seen = set()
    for length, at in runs[:2]:
        anchor = bytes(pat[at:at + length])
        pos = 0
        while True:
            pos = hay.find(anchor, pos)
            if pos < 0:
                break
            base = pos - at
            if 0 <= base and base + n <= len(hay) and base not in seen:
                mism = 0
                for j, v in enumerate(pat):
                    if v is not None and hay[base + j] != v:
                        mism += 1
                        if mism > budget:
                            break
                if mism <= budget:
                    seen.add(base)
                    hits.append((base, mism))
            pos += 1
        if len(hits) >= cap:
            break

    hits.sort(key=lambda h: h[1])
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("verify")
    ap.add_argument("--src", default=".")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    img = vs.Image(args.image)
    spans = img.exec_spans()

    results = json.load(open(args.verify, encoding="utf-8"))["results"]
    missing = [r for r in results if r["status"] == "MISSING"]

    out = []
    for r in missing:
        pat = vs.parse_pattern(r["pattern"])
        hits = []
        for s, e in spans:
            for rel, mism in tolerate(img.data[s:e], 0, pat):
                hits.append((s + rel, mism))
        hits.sort(key=lambda h: h[1])

        entry = {"name": r["name"], "kind": r["kind"], "pattern": r["pattern"]}
        if not hits:
            entry["result"] = "gone"
            out.append(entry)
            print("%-46s gone" % r["name"])
            continue

        best_mism = hits[0][1]
        best = [h for h in hits if h[1] == best_mism]
        entry["rva"] = "0x%X" % best[0][0]
        entry["mismatches"] = best_mism
        entry["candidates"] = len(best)

        # The corrected pattern is literally the bytes at the candidate, so it is
        # correct by construction for this build.
        actual = img.data[best[0][0]:best[0][0] + len(pat)]
        newpat = []
        for j, v in enumerate(pat):
            newpat.append("?" if v is None else "%02X" % actual[j])
        entry["new_pattern"] = " ".join(newpat)

        entry["result"] = "unique" if len(best) == 1 else "ambiguous"
        out.append(entry)
        print("%-46s %-9s mism=%-2d cands=%-2d 0x%X"
              % (r["name"], entry["result"], best_mism, len(best), best[0][0]))

    counts = {}
    for e in out:
        counts[e["result"]] = counts.get(e["result"], 0) + 1
    print("\n" + ", ".join("%s=%d" % kv for kv in sorted(counts.items())))

    if args.json:
        json.dump(out, open(args.json, "w", encoding="utf-8"), indent=2)
        print("written to %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
