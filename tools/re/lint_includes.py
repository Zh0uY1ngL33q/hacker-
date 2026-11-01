#!/usr/bin/env python3
"""Compiler-free include resolution check.

This is NOT a compile. It answers one narrow question: does every include that
*refers to a file in this tree* land on a path CMakeLists.txt actually exposes?

Judgement rule, chosen so the result means something:

  * resolved under one of the include_directories() entries  -> OK
  * resolved next to the including file                     -> OK
  * not found, but a file with the same basename exists elsewhere in the tree
    -> REPORT ("exists but not on the include path" = certain build break)
  * not found and no such basename anywhere in the tree      -> external / SDK
    header, ignored (Windows.h, <algorithm>, ...)

A report here is a guaranteed compile failure; silence proves nothing about the
code being valid.
"""
from __future__ import annotations

import argparse
import os
import re
import sys

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
DIRECTIVE_RE = re.compile(r"^include_directories\(([^)]*)\)", re.M)
TARGET_INC_RE = re.compile(r"target_include_directories\(\s*\w+\s+(?:PUBLIC|PRIVATE|INTERFACE|SYSTEM)"
                           r"((?:\s+(?:PUBLIC|PRIVATE|INTERFACE|SYSTEM))?\s+[^)]*)\)", re.M)


def _dirs_from_tokens(base, tokens):
    out = []
    for token in tokens.split():
        token = token.strip('"')
        if token in ("PUBLIC", "PRIVATE", "INTERFACE", "SYSTEM"):
            continue
        # The vendored subprojects publish their include dir as
        # $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>, so unwrap the
        # generator expression first, then resolve the variable, then skip
        # anything still expression-shaped rather than guessing at it.
        if token.startswith("$<"):
            inner = token.split(":", 1)[1] if ":" in token else ""
            token = inner[:-1] if inner.endswith(">") else inner
        token = token.replace("${CMAKE_CURRENT_LIST_DIR}", base)
        token = token.replace("${CMAKE_CURRENT_SOURCE_DIR}", base)
        if "$" in token:
            continue
        d = os.path.normpath(os.path.join(base, token))
        if os.path.isdir(d):
            out.append(d)
    return out


def include_dirs(root, cmakelists):
    """Directories the build actually exposes.

    Two sources, because the tree uses both:
      * include_directories() in the top-level CMakeLists
      * target_include_directories(... PUBLIC/INTERFACE ...) inside the vendored
        subprojects added with add_subdirectory(include/{Kiero,libhat,nes,...}).
        Without the second source every `#include "kiero.hpp"` looks broken,
        which it is not: include/Kiero/CMakeLists.txt publishes include/ itself.
    """
    dirs = []
    text = open(cmakelists, encoding="utf-8", errors="replace").read()
    for m in DIRECTIVE_RE.finditer(text):
        dirs.extend(_dirs_from_tokens(root, m.group(1)))

    for base, _, files in os.walk(root):
        if "CMakeLists.txt" not in files:
            continue
        sub = os.path.join(base, "CMakeLists.txt")
        try:
            subtext = open(sub, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for m in TARGET_INC_RE.finditer(subtext):
            dirs.extend(_dirs_from_tokens(base, m.group(1)))
    # de-duplicate, keep order
    seen, out = set(), []
    for d in dirs:
        if d not in seen:
            seen.add(d)
            out.append(d)
    return out


def walk(root, exts=(".cpp", ".hpp", ".h")):
    for base, _, files in os.walk(root):
        for f in files:
            if f.endswith(exts):
                yield os.path.join(base, f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    a = ap.parse_args()

    cl = os.path.join(a.root, "CMakeLists.txt")
    if not os.path.isfile(cl):
        print(f"no CMakeLists.txt under {a.root}")
        return 2
    # Absolute root, otherwise a substituted ${CMAKE_CURRENT_LIST_DIR} becomes a
    # relative path that gets appended to itself by os.path.join and every
    # vendored include dir silently disappears.
    a.root = os.path.abspath(a.root)
    cl = os.path.join(a.root, "CMakeLists.txt")
    dirs = include_dirs(a.root, cl)
    print(f"include dirs exposed by CMakeLists ({len(dirs)}):")
    for d in dirs:
        print("  ", os.path.relpath(d, a.root).replace("\\", "/"))

    # basename index of every header in the tree, for the "exists but not
    # reachable" case
    by_name = {}
    for p in walk(os.path.join(a.root, "src")):
        by_name.setdefault(os.path.basename(p), []).append(p)
    for d in dirs:
        for p in walk(d):
            by_name.setdefault(os.path.basename(p), []).append(p)

    resolved = 0
    missing_local = {}
    external = set()
    for path in walk(os.path.join(a.root, "src")):
        text = open(path, encoding="utf-8", errors="replace").read()
        here = os.path.dirname(path)
        for inc in INCLUDE_RE.findall(text):
            norm = inc.replace("\\", "/")
            if any(os.path.isfile(os.path.join(d, norm)) for d in dirs) or \
               os.path.isfile(os.path.join(here, norm)):
                resolved += 1
                continue
            base = os.path.basename(norm)
            if base in by_name:
                missing_local.setdefault(norm, []).append(os.path.relpath(path, a.root))
            else:
                external.add(norm)

    print(f"\nresolved in project: {resolved}")
    print(f"external/SDK headers (ignored): {len(external)}")
    print(f"LOCAL HEADERS NOT ON THE INCLUDE PATH: {len(missing_local)}")
    for inc, users in sorted(missing_local.items()):
        print(f"  !! {inc}   (file exists in tree)")
        for u in users[:5]:
            print(f"       included by {u}")
    return 1 if missing_local else 0


if __name__ == "__main__":
    sys.exit(main())
