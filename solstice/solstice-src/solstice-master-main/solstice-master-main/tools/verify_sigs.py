#!/usr/bin/env python3
"""
Offline signature/offset validator for Solstice.

Consumes the runtime image dump produced by ModuleDump (SOLSTICE_DUMP=1) and
reports, per signature:

    OK        - resolved exactly once
    AMBIGUOUS - resolved N>1 times  (dangerous: first match wins at runtime)
    MISSING   - no match at all

Because the dump is a *virtual* image (file offset == RVA), no section mapping
is needed; we only parse the PE headers to find the code section bounds.

Usage:
    python verify_sigs.py <dump.image> [--src <solstice src dir>] [--json out.json]

The runtime image is WinLicense packed and only decrypted in memory, so this
tool must be pointed at the dump, never at the on-disk exe.
"""

import argparse
import json
import os
import re
import struct
import sys
from collections import Counter


# --------------------------------------------------------------------------
# PE header parsing (virtual-image aware)
# --------------------------------------------------------------------------
class Image:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        self.path = path
        self.size_of_image = 0
        self.sections = []
        self._parse()

    def _parse(self):
        d = self.data
        if d[:2] != b"MZ":
            raise ValueError("not an MZ image (headers missing from dump?)")
        e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
        if d[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
            raise ValueError("bad PE signature -- dump looks truncated")

        coff = e_lfanew + 4
        num_sections = struct.unpack_from("<H", d, coff + 2)[0]
        opt_size = struct.unpack_from("<H", d, coff + 16)[0]
        opt = coff + 20
        magic = struct.unpack_from("<H", d, opt)[0]
        if magic != 0x20B:
            raise ValueError("expected PE32+ (0x20B), got 0x%X" % magic)

        self.size_of_image = struct.unpack_from("<I", d, opt + 56)[0]

        sec_off = opt + opt_size
        for i in range(num_sections):
            off = sec_off + i * 40
            raw_name = d[off:off + 8]
            vsize, rva, raw_size = struct.unpack_from("<III", d, off + 8)
            chars = struct.unpack_from("<I", d, off + 36)[0]
            name = raw_name.split(b"\x00")[0].decode("utf-8", "replace")
            self.sections.append({
                "index": i, "name": name or ("<blank#%d>" % i),
                "rva": rva, "vsize": vsize, "raw_size": raw_size,
                "characteristics": chars,
            })

    def code_span(self):
        """Span used for scanning: the largest executable section.

        Mirrors what the game actually keeps decrypted, and avoids scanning
        data/zero-filled regions where patterns match spuriously.
        """
        best = None
        for s in self.sections:
            if s["vsize"] == 0:
                continue
            if not (s["characteristics"] & 0x20000000):  # IMAGE_SCN_MEM_EXECUTE
                continue
            if best is None or s["vsize"] > best["vsize"]:
                best = s
        if best is None:
            best = max(self.sections, key=lambda s: s["vsize"])
        return (best["rva"], best["rva"] + best["vsize"], best["name"])


# --------------------------------------------------------------------------
# Pattern parsing / scanning
# --------------------------------------------------------------------------
def parse_pattern(text):
    out = []
    for tok in text.split():
        if tok == "?":
            out.append(None)
        elif "?" in tok:
            hi = int(tok[0], 16) & 0xF
            lo = int(tok[1], 16) & 0xF
            out.append((hi << 4) | lo)
        else:
            out.append(int(tok, 16))
    return out


def scan(buf, start, end, pat, limit=64):
    """All matches of pat within [start, end). Anchored for speed."""
    n = len(pat)
    if n == 0 or end - start < n:
        return []

    best_len, best_start = 0, 0
    cur, cur_start = 0, 0
    for i, v in enumerate(pat):
        if v is None:
            cur = 0
        else:
            if cur == 0:
                cur_start = i
            cur += 1
            if cur > best_len:
                best_len, best_start = cur, cur_start
    if best_len < 4:
        return []

    anchor = bytes(pat[best_start:best_start + best_len])
    hay = buf[start:end]

    hits, pos = [], 0
    while True:
        pos = hay.find(anchor, pos)
        if pos < 0:
            break
        base = pos - best_start
        if base >= 0 and base + n <= len(hay):
            if all(pat[j] is None or hay[base + j] == pat[j] for j in range(n)):
                hits.append(start + base)
                if len(hits) >= limit:
                    break
        pos += 1
    return hits


# --------------------------------------------------------------------------
# Extract definitions straight from the Solstice headers
# --------------------------------------------------------------------------
RE_INDEX_FIELD = re.compile(
    r'DEFINE_INDEX_FIELD_TYPED\s*\(\s*[^,]+,\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*OffsetType::(\w+)',
)
RE_INDEX_FIELD2 = re.compile(
    r'DEFINE_INDEX_FIELD\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*OffsetType::(\w+)',
)
RE_SIG = re.compile(
    r'DEFINE_SIG\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*SigType::(\w+)\s*,\s*(-?\d+)\s*\)',
)
RE_FIELD = re.compile(r'^\s*DEFINE_FIELD\s*\(\s*(\w+)\s*,\s*([^)]+)\)', re.M)


def load_definitions(src_dir):
    defs = []

    off_path = os.path.join(src_dir, "src", "SDK", "OffsetProvider.hpp")
    sig_path = os.path.join(src_dir, "src", "SDK", "SigManager.hpp")

    with open(off_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    commented = set()
    for line in text.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("//"):
            m = RE_FIELD.match(stripped[2:])
            if m:
                commented.add(m.group(1))

    for m in RE_INDEX_FIELD.finditer(text):
        defs.append({"kind": "offset", "name": m.group(1), "pattern": m.group(2),
                    "disp_off": int(m.group(3)), "offset_type": m.group(4)})
    for m in RE_INDEX_FIELD2.finditer(text):
        defs.append({"kind": "offset", "name": m.group(1), "pattern": m.group(2),
                    "disp_off": int(m.group(3)), "offset_type": m.group(4)})

    with open(sig_path, "r", encoding="utf-8", errors="replace") as f:
        sig_text = f.read()
    for m in RE_SIG.finditer(sig_text):
        defs.append({"kind": "sig", "name": m.group(1), "pattern": m.group(2),
                     "sig_type": m.group(3), "disp_off": int(m.group(4))})

    hardcoded = []
    for m in RE_FIELD.finditer(text):
        name, val = m.group(1), m.group(2).strip()
        hardcoded.append({"name": name, "value": val, "disabled": name in commented})

    return defs, hardcoded


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="dump produced by ModuleDump")
    ap.add_argument("--src", default=os.getcwd(), help="solstice source root")
    ap.add_argument("--json", default=None, help="write report as JSON")
    args = ap.parse_args()

    img = Image(args.image)
    start, end, sec_name = img.code_span()

    print("image        : %s" % args.image)
    print("sizeOfImage  : 0x%X" % img.size_of_image)
    print("file size    : 0x%X (%d MB)" % (len(img.data), len(img.data) // (1024 * 1024)))
    print("code section : %s  [0x%X, 0x%X)  %d MB"
          % (sec_name, start, end, (end - start) // (1024 * 1024)))
    print("sections     :")
    for s in img.sections:
        print("   [%-12s] RVA=0x%08X vsize=0x%08X raw=0x%08X flags=0x%08X"
              % (s["name"], s["rva"], s["vsize"], s["raw_size"], s["characteristics"]))
    print()

    defs, hardcoded = load_definitions(args.src)

    results = []
    counts = Counter()

    for d in defs:
        pat = parse_pattern(d["pattern"])
        hits = scan(img.data, start, end, pat)
        n = len(hits)
        if n == 0:
            status = "MISSING"
        elif n == 1:
            status = "OK"
        else:
            status = "AMBIGUOUS"

        entry = dict(d)
        entry["matches"] = n
        entry["status"] = status
        entry["match_rvas"] = ["0x%X" % h for h in hits[:8]]

        # Reproduce what the runtime does with the match, so we can see the
        # offset value the game would actually get.
        if d["kind"] == "offset" and n >= 1:
            rva = hits[0] + d["disp_off"]
            entry["rva"] = "0x%X" % hits[0]
            if rva + 4 <= len(img.data):
                raw = struct.unpack_from("<i", img.data, rva)[0]
                val = raw // 8 if d["offset_type"] == "Index" else raw
                entry["field_offset"] = val
                # Plausibility gate: a nonsense index means the pattern matched
                # the wrong code, even though it "resolved".
                if d["offset_type"] == "Index" and not (0 <= val <= 4096):
                    entry["status"] = "SUSPECT"
                    entry["note"] = "implausible vtable index %d" % val
                elif abs(val) > 0x20000:
                    entry["status"] = "SUSPECT"
                    entry["note"] = "implausible field offset 0x%X" % (val & 0xFFFFFFFF)

        results.append(entry)
        counts[status] += 1

    print("=" * 78)
    print("%-14s %-10s %-6s %s" % ("KIND", "STATUS", "HITS", "NAME / NOTES"))
    print("=" * 78)
    for e in results:
        note = e.get("note", "")
        extra = ""
        if "field_offset" in e:
            extra = "  off=%s" % e["field_offset"]
        print("%-14s %-10s %-6d %s%s%s"
              % (e["kind"], e["status"], e["matches"], e["name"], extra,
                 ("  <- " + note) if note else ""))

    print()
    print("summary: " + ", ".join("%s=%d" % (k, v) for k, v in sorted(counts.items())))

    if hardcoded:
        live = [h for h in hardcoded if not h["disabled"]]
        print()
        print("hardcoded DEFINE_FIELD with no signature (%d live, %d commented out):"
              % (len(live), len(hardcoded) - len(live)))
        print("  these are pure version snapshots and are the most fragile part of the build")
        for h in hardcoded:
            print("   %-46s = %-10s%s" % (h["name"], h["value"],
                                          "  (disabled)" if h["disabled"] else ""))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"image": args.image, "codeSection": sec_name,
                       "results": results, "hardcoded": hardcoded}, f, indent=2)
        print("\nreport written to %s" % args.json)

    # Non-zero exit if anything is broken, so this can gate a script.
    return 0 if counts["OK"] == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
