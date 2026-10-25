#!/usr/bin/env python3
"""Offline signature scanner for Solstice offset work.

Evaluates every signature declared in src/SDK/OffsetProvider.hpp against a
memory image produced by ModuleDump, without needing the game running.

The image format is the one ModuleDump writes: a PE-shaped file whose byte at
file offset X is the process byte at RVA X. That means every address in a
report is directly comparable with the RVAs in the client's own log.

Why offline: Minecraft.Windows.exe is compressed on disk (LZMA, entropy ~7.98),
so the shipped file contains no code at all. The decrypted image only exists in
memory while the game runs. One dump therefore makes every later analysis pass
free, and lets two game versions be diffed mechanically.

Usage:
    python scan.py IMAGE [--header OffsetProvider.hpp] [--json out.json]
                         [--md out.md] [--diff previous.json] [--verbose]

Exit codes: 0 = all signatures resolved, 1 = one or more failed.
"""

from __future__ import annotations

import argparse
import json
import mmap
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# --------------------------------------------------------------------------
# Header parsing
# --------------------------------------------------------------------------

# Commented-out declarations must be ignored: several signatures in the header
# are disabled with a leading //, and treating them as live would produce
# phantom failures.
_RE_TYPED = re.compile(
    r"DEFINE_INDEX_FIELD_TYPED\s*\(\s*[\w:]+\s*,\s*(\w+)\s*,\s*\"([^\"]+)\"\s*,"
    r"\s*(-?\d+)\s*,\s*OffsetType::(\w+)\s*\)"
)
_RE_PLAIN = re.compile(
    r"DEFINE_INDEX_FIELD(?!_TYPED)\s*\(\s*(\w+)\s*,\s*\"([^\"]+)\"\s*,"
    r"\s*(-?\d+)\s*,\s*OffsetType::(\w+)\s*\)"
)
_RE_CONST = re.compile(r"DEFINE_FIELD\s*\(\s*(\w+)\s*,\s*([^)]+?)\s*\)")


@dataclass
class Sig:
    name: str
    pattern_text: str
    offset: int
    kind: str                     # "Index" or "FieldOffset"
    pattern: list[int | None] = field(default_factory=list)


@dataclass
class ConstField:
    name: str
    value_text: str


def strip_line_comments(src: str) -> str:
    out = []
    for line in src.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("//"):
            continue
        # Drop trailing comments, but only outside string literals.
        if "//" in line:
            in_str = False
            cut = len(line)
            for i, ch in enumerate(line):
                if ch == '"':
                    in_str = not in_str
                elif not in_str and line[i:i + 2] == "//":
                    cut = i
                    break
            line = line[:cut]
        out.append(line)
    return "\n".join(out)


def parse_header(path: Path) -> tuple[list[Sig], list[ConstField]]:
    src = strip_line_comments(path.read_text(encoding="utf-8", errors="replace"))

    sigs: list[Sig] = []
    for m in _RE_TYPED.finditer(src):
        name, pat, off, kind = m.group(1), m.group(2), int(m.group(3)), m.group(4)
        sigs.append(Sig(name, pat, off, kind, parse_pattern(pat)))
    for m in _RE_PLAIN.finditer(src):
        name, pat, off, kind = m.group(1), m.group(2), int(m.group(3)), m.group(4)
        sigs.append(Sig(name, pat, off, kind, parse_pattern(pat)))

    consts = [ConstField(m.group(1), m.group(2)) for m in _RE_CONST.finditer(src)]
    return sigs, consts


def parse_pattern(text: str) -> list[int | None]:
    """'48 8B ? ?' -> [0x48, 0x8B, None, None]. '?' is a full-byte wildcard."""
    out: list[int | None] = []
    for tok in text.split():
        if tok in ("?", "??"):
            out.append(None)
        else:
            out.append(int(tok, 16))
    return out


# --------------------------------------------------------------------------
# Scanning
# --------------------------------------------------------------------------

def longest_fixed_run(pattern: list[int | None]) -> tuple[int, int]:
    """Offset and length of the longest run of known bytes.

    Used as a search needle so the inner loop can be bytes.find at C speed
    instead of a Python-level compare over ~146 MB.
    """
    best_start, best_len = 0, 0
    i = 0
    while i < len(pattern):
        if pattern[i] is None:
            i += 1
            continue
        j = i
        while j < len(pattern) and pattern[j] is not None:
            j += 1
        if j - i > best_len:
            best_start, best_len = i, j - i
        i = j
    return best_start, best_len


def verify(data, pos: int, pattern: list[int | None]) -> bool:
    for k, v in enumerate(pattern):
        if v is not None and data[pos + k] != v:
            return False
    return True


def find_all(data, pattern: list[int | None], cap: int = 8) -> list[int]:
    """All match offsets, capped. A cap of 1 answers 'does it exist'."""
    n = len(pattern)
    if n == 0 or len(data) < n:
        return []

    start, length = longest_fixed_run(pattern)
    if length == 0:
        return []  # a signature made only of wildcards is meaningless

    needle = bytes(pattern[start:start + length])
    hits: list[int] = []
    pos = 0
    limit = len(data) - n
    while pos <= limit:
        found = data.find(needle, pos + start)
        if found < 0:
            break
        cand = found - start
        if cand < 0 or cand > limit:
            pos = found + 1
            continue
        if verify(data, cand, pattern):
            hits.append(cand)
            if len(hits) >= cap:
                break
        pos = found + 1
    return hits


# --------------------------------------------------------------------------
# PE helpers (for reporting the section a match lands in)
# --------------------------------------------------------------------------

def read_sections(data) -> list[dict]:
    if data[0:2] != b"MZ":
        return []
    e_lfanew = int.from_bytes(data[0x3C:0x40], "little")
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return []
    nsec = int.from_bytes(data[e_lfanew + 6:e_lfanew + 8], "little")
    opt_size = int.from_bytes(data[e_lfanew + 20:e_lfanew + 22], "little")
    base = e_lfanew + 24 + opt_size
    out = []
    for i in range(nsec):
        o = base + i * 40
        raw_name = bytes(data[o:o + 8]).rstrip(b"\0").decode("ascii", "replace")
        vsize = int.from_bytes(data[o + 8:o + 12], "little")
        rva = int.from_bytes(data[o + 12:o + 16], "little")
        chars = int.from_bytes(data[o + 36:o + 40], "little")
        out.append({
            "index": i,
            "name": raw_name or f"<blank#{i}>",
            "rva": rva,
            "vsize": vsize,
            "executable": bool(chars & 0x20000000),
        })
    return out


def section_of(sections: list[dict], rva: int) -> str:
    for s in sections:
        if s["rva"] <= rva < s["rva"] + max(s["vsize"], 1):
            return s["name"]
    return "?"


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=Path)
    ap.add_argument("--header", type=Path,
                    default=Path("src/SDK/OffsetProvider.hpp"))
    ap.add_argument("--json", type=Path)
    ap.add_argument("--md", type=Path)
    ap.add_argument("--diff", type=Path, help="previous JSON report to compare against")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not args.image.exists():
        print(f"error: image not found: {args.image}", file=sys.stderr)
        return 2
    if not args.header.exists():
        print(f"error: header not found: {args.header}", file=sys.stderr)
        return 2

    sigs, consts = parse_header(args.header)
    print(f"signatures: {len(sigs)}   fixed fields: {len(consts)}")

    sections: list[dict] = []
    results = []

    with args.image.open("rb") as fh:
        size = args.image.stat().st_size
        data = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            sections = read_sections(data)
            exec_sections = [s for s in sections if s["executable"]]
            print(f"image: {size / (1024 * 1024):.1f} MB, "
                  f"{len(sections)} sections ({len(exec_sections)} executable)")
            for s in exec_sections:
                print(f"  exec {s['name']:<12} RVA=0x{s['rva']:08X} "
                      f"size=0x{s['vsize']:X}")

            for sig in sigs:
                # cap 8 is enough to distinguish unique from ambiguous.
                hits = find_all(data, sig.pattern, cap=8)

                entry = {
                    "name": sig.name,
                    "kind": sig.kind,
                    "offset": sig.offset,
                    "pattern": sig.pattern_text,
                    "count": len(hits),
                    "rva": None,
                    "section": None,
                    "value": None,
                    "status": "",
                }

                if not hits:
                    entry["status"] = "NOT_FOUND"
                else:
                    rva = hits[0]
                    entry["rva"] = rva
                    entry["section"] = section_of(sections, rva)
                    entry["status"] = "AMBIGUOUS" if len(hits) > 1 else "OK"

                    # Resolve the field the signature is meant to locate: read
                    # the disp32 at rva+offset, and for a vtable index divide by
                    # sizeof(void*), matching DEFINE_INDEX_FIELD's semantics.
                    at = rva + sig.offset
                    if 0 <= at <= len(data) - 4:
                        raw = int.from_bytes(data[at:at + 4], "little", signed=True)
                        entry["value"] = (raw // 8) if sig.kind == "Index" else raw
                        entry["raw"] = raw

                results.append(entry)
        finally:
            data.close()

    ok = [r for r in results if r["status"] == "OK"]
    amb = [r for r in results if r["status"] == "AMBIGUOUS"]
    miss = [r for r in results if r["status"] == "NOT_FOUND"]

    print()
    print(f"resolved   : {len(ok)}")
    print(f"ambiguous  : {len(amb)}")
    print(f"not found  : {len(miss)}")

    if amb:
        print("\nambiguous signatures (a hit here may be the wrong function):")
        for r in amb:
            print(f"  {r['name']:<48} {r['count']} matches, first in {r['section']}")

    if miss:
        print("\nnot found:")
        for r in miss:
            print(f"  {r['name']}")

    if args.verbose:
        print("\nall results:")
        for r in results:
            val = "-" if r["value"] is None else hex(r["value"] & 0xFFFFFFFF)
            rva = "-" if r["rva"] is None else f"0x{r['rva']:X}"
            print(f"  {r['status']:<10} {r['name']:<48} rva={rva:<12} value={val}")

    if args.diff:
        prev = json.loads(args.diff.read_text(encoding="utf-8"))
        old = {r["name"]: r for r in prev.get("results", [])}
        print(f"\ndiff against {args.diff}:")
        for r in results:
            p = old.get(r["name"])
            if not p:
                print(f"  NEW      {r['name']}")
            elif p.get("value") != r["value"]:
                print(f"  CHANGED  {r['name']}: {p.get('value')} -> {r['value']}")
            elif p.get("status") != r["status"]:
                print(f"  STATUS   {r['name']}: {p.get('status')} -> {r['status']}")

    report = {
        "image": str(args.image),
        "imageSize": size,
        "header": str(args.header),
        "sections": sections,
        "fixedFields": [{"name": c.name, "value": c.value_text} for c in consts],
        "results": results,
    }

    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"\nwrote {args.json}")

    if args.md:
        lines = [
            f"# Signature report: `{args.image.name}`",
            "",
            f"- image size: {size / (1024 * 1024):.1f} MB",
            f"- signatures: {len(results)} "
            f"(resolved {len(ok)}, ambiguous {len(amb)}, missing {len(miss)})",
            "",
            "| status | signature | RVA | section | matches | value |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
        for r in results:
            rva = "-" if r["rva"] is None else f"0x{r['rva']:X}"
            val = "-" if r["value"] is None else f"0x{r['value'] & 0xFFFFFFFF:X}"
            lines.append(f"| {r['status']} | `{r['name']}` | {rva} | "
                         f"{r['section'] or '-'} | {r['count']} | {val} |")
        args.md.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"wrote {args.md}")

    return 0 if not miss and not amb else 1


if __name__ == "__main__":
    sys.exit(main())
