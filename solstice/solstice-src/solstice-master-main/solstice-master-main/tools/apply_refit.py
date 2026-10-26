#!/usr/bin/env python3
"""Apply the verified pattern corrections from refit_sigs.py to the headers.

Only entries that refit_sigs.py marked verified are touched: those resolved to
exactly one match in the dump (or to exactly one match with a plausible value),
so they have been checked offline and do not need another build to validate.

Usage:
    python tools/apply_refit.py <refit.json> [--src .] [--dry-run]
"""

import argparse
import json
import os
import re
import sys


def patch(path, name, old, new, dry):
    # newline="" on both ends: the source files are CRLF, and letting Python
    # translate on read then write would rewrite every line of the file.
    text = open(path, encoding="utf-8", newline="").read()
    if old not in text:
        return "old pattern not found"

    # Restrict the replacement to the definition of `name` so that an identical
    # pattern shared by two signatures cannot be rewritten in the wrong place.
    rx = re.compile(
        r'(DEFINE_(?:INDEX_FIELD(?:_TYPED)?|SIG)\s*\(\s*)'
        r'([^,]*,\s*)?"' + re.escape(old) + r'"'
    )
    count = 0
    out = []
    pos = 0
    for m in re.finditer(r"DEFINE_(?:INDEX_FIELD(?:_TYPED)?|SIG)\s*\(", text):
        pass

    # Simpler and exact: walk macro invocations, and only accept the one whose
    # argument list contains the identifier `name`.
    result = text
    for m in re.finditer(r"DEFINE_(?:INDEX_FIELD(?:_TYPED)?|SIG)\s*\(([^;]*?)\)\s*;?", text, re.S):
        body = m.group(1)
        if old not in body:
            continue
        if not re.search(r"\b" + re.escape(name) + r"\b", body):
            continue
        if body.count('"' + old + '"') != 1:
            return "pattern appears %d times in the macro" % body.count('"' + old + '"')
        start = m.start(1) + body.index('"' + old + '"')
        result = result[:start] + '"' + new + '"' + result[start + len(old) + 2:]
        count += 1
        break

    if count != 1:
        return "macro not found"

    if not dry:
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(result)
    return "ok"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("refit")
    ap.add_argument("--src", default=".")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    entries = json.load(open(args.refit, encoding="utf-8"))
    off = os.path.join(args.src, "src", "SDK", "OffsetProvider.hpp")
    sig = os.path.join(args.src, "src", "SDK", "SigManager.hpp")

    applied = skipped = 0
    for e in entries:
        if not e.get("verified"):
            continue
        path = off if e["kind"] == "offset" else sig
        status = patch(path, e["name"], e["old_pattern"], e["new_pattern"], args.dry_run)
        if status == "ok":
            applied += 1
            print("  %-46s %d diff(s)" % (e["name"], e["diff_count"]))
        else:
            skipped += 1
            print("  %-46s SKIP: %s" % (e["name"], status))

    print("\napplied %d, skipped %d%s"
          % (applied, skipped, " (dry run)" if args.dry_run else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
