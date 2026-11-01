#!/usr/bin/env python3
"""Map every unresolved symbol to the source files and features that use it.

The feature-compatibility table has to come from the tree, not from memory: a
module is only "compatible" if the symbols it actually references resolve.
"""
from __future__ import annotations

import argparse
import json
import os
import re

SRC = r"G:\hacker\solstice\solstice-src\solstice-master-main\solstice-master-main\src"


def walk():
    for root, _, files in os.walk(SRC):
        for f in files:
            if f.endswith((".cpp", ".hpp", ".h")):
                yield os.path.join(root, f)


def feature_of(path):
    rel = os.path.relpath(path, SRC).replace("\\", "/")
    parts = rel.split("/")
    if len(parts) >= 3 and parts[0] == "Features" and parts[1] == "Modules":
        return f"{parts[1]}/{parts[2]}/{os.path.splitext(parts[-1])[0]}"
    if len(parts) >= 2:
        return "/".join(parts[:2])
    return rel


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--only-bad", action="store_true")
    a = ap.parse_args()

    data = json.load(open(a.baseline))
    status = {}
    for key, label in (("signatures", "sig"), ("offsets", "off")):
        for r in data[key]:
            status[r["name"]] = (label, r["status"], r.get("matches"))

    users = {}
    pat = re.compile(r"(?:SigManager|OffsetProvider)::([A-Za-z_0-9]+)")
    for path in walk():
        try:
            text = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for name in set(pat.findall(text)):
            users.setdefault(name, set()).add(path)

    rows = []
    for name, (kind, st, matches) in sorted(status.items()):
        if a.only_bad and st == "ok":
            continue
        files = sorted(users.get(name, ()))
        feats = sorted({feature_of(f) for f in files})
        rows.append((st, kind, name, matches, feats, files))

    order = {"failed": 0, "ambiguous": 1, "ok": 2}
    rows.sort(key=lambda r: (order.get(r[0], 3), r[2]))
    for st, kind, name, matches, feats, files in rows:
        print(f"{st:9} {kind:3} {name:48} m={matches}  features={', '.join(feats) or '(unused)'}")


if __name__ == "__main__":
    main()
