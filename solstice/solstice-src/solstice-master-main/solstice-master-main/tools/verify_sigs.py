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
import mmap
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
            # Map once; scans must not copy the 139 MB code section per pattern.
            self.data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
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

    def exec_spans(self):
        """Every executable section, as (start, end) in image offsets.

        The runtime scans all of them (CodeSection::ranges), and so must this:
        scanning only the largest misses .winlice, a 15 MB executable section the
        packer adds, and reports a handful of signatures as MISSING that the game
        resolves fine.
        """
        spans = []
        for s in self.sections:
            if s["vsize"] == 0:
                continue
            if not (s["characteristics"] & 0x20000000):  # IMAGE_SCN_MEM_EXECUTE
                continue
            end = min(len(self.data), s["rva"] + s["vsize"])
            if end > s["rva"]:
                spans.append((s["rva"], end))
        if not spans:
            spans.append((0, len(self.data)))
        return spans

    def code_span(self):
        """Largest executable section, as (start, end, name)."""
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


def longest_run(pat):
    """Longest stretch of consecutive concrete bytes: (length, offset)."""
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
    return best_len, best_start


def scan(buf, start, end, pat, limit=64):
    """All matches of pat within [start, end).

    Anchored on the longest run of concrete bytes, which lets bytes.find do the
    work in C. The anchor only has to be *two* bytes long: requiring four (as
    this did originally) silently returned "no match" for every pattern without
    such a run, and 78 of the 185 definitions here are like that -- e.g.
    `48 8B ? ? ? ? ? 48 89 ? ? ?` has no run longer than two. A short anchor just
    means more candidates to verify, not wrong results.
    """
    n = len(pat)
    if n == 0 or end - start < n:
        return []

    best_len, best_start = longest_run(pat)
    hits = []

    if best_len >= 2:
        anchor = bytes(pat[best_start:best_start + best_len])
        pos = start
        while True:
            pos = buf.find(anchor, pos, end)
            if pos < 0:
                break
            base = pos - best_start
            if base >= start and base + n <= end:
                if all(pat[j] is None or buf[base + j] == pat[j] for j in range(n)):
                    hits.append(base)
                    if len(hits) >= limit:
                        break
            pos += 1
        return hits

    # Nothing to anchor on (at most one concrete byte, or a lone byte next to
    # wildcards): one wildcard-aware regex pass. Rare enough that the cost of a
    # 139 MB regex search here does not matter.
    parts = [b"." if v is None else re.escape(struct.pack("B", v)) for v in pat]
    rx = re.compile(b"".join(parts), re.DOTALL)
    for m in rx.finditer(buf, start, end):
        hits.append(m.start())
        if len(hits) >= limit:
            break
    return hits


def scan_spans(buf, spans, pat, limit=64):
    """scan() over every executable span, merging the hit lists.

    The runtime searches all executable sections; matching that exactly is what
    keeps the offline verdict and the runtime `Failed to find` count aligned.
    A pattern may legitimately match in more than one span, so the merged list
    is what decides OK / AMBIGUOUS.
    """
    hits = []
    for start, end in spans:
        hits.extend(scan(buf, start, end, pat, limit=limit - len(hits)))
        if len(hits) >= limit:
            break
    return hits


# --------------------------------------------------------------------------
# Extract definitions straight from the Solstice headers
# --------------------------------------------------------------------------
RE_INDEX_FIELD = re.compile(
    r'DEFINE_INDEX_FIELD_TYPED\s*\(\s*([^,]+),\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*OffsetType::(\w+)',
)
RE_INDEX_FIELD2 = re.compile(
    r'DEFINE_INDEX_FIELD\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*OffsetType::(\w+)',
)
RE_SIG = re.compile(
    r'DEFINE_SIG\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*SigType::(\w+)\s*,\s*(-?\d+)\s*\)',
)
RE_FIELD = re.compile(r'^\s*DEFINE_FIELD\s*\(\s*(\w+)\s*,\s*([^)]+)\)', re.M)

def active_cpp(text):
    """Remove comments, preserving quoted strings and line boundaries."""
    tokens = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')
    return tokens.sub(lambda m: m.group() if m.group()[0] in "\"'" else
                      re.sub(r'[^\n]', ' ', m.group()), text)

def read_offset(buf, position, definition):
    ctype = definition.get("read_type", "int").strip()
    types = {"int": (4, True), "uint8_t": (1, False), "int8_t": (1, True),
             "uint16_t": (2, False), "int16_t": (2, True),
             "uint32_t": (4, False), "int32_t": (4, True),
             "uint64_t": (8, False), "int64_t": (8, True)}
    size, signed = types[ctype]
    if position < 0 or position + size > len(buf):
        raise ValueError("offset read outside image")
    raw = int.from_bytes(buf[position:position + size], "little", signed=signed)
    if definition["offset_type"] == "Index":
        # C++ signed integer division truncates towards zero, not -infinity.
        return (-1 if raw < 0 else 1) * (abs(raw) // 8)
    return raw


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

    text = active_cpp(text)
    for m in RE_INDEX_FIELD.finditer(text):
        defs.append({"kind": "offset", "read_type": m.group(1).strip(),
                    "name": m.group(2), "pattern": m.group(3),
                    "disp_off": int(m.group(4)), "offset_type": m.group(5)})
    for m in RE_INDEX_FIELD2.finditer(text):
        defs.append({"kind": "offset", "name": m.group(1), "pattern": m.group(2),
                    "disp_off": int(m.group(3)), "offset_type": m.group(4), "read_type": "int"})

    with open(sig_path, "r", encoding="utf-8", errors="replace") as f:
        sig_text = f.read()
    for m in RE_SIG.finditer(active_cpp(sig_text)):
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
    spans = img.exec_spans()
    start, end, sec_name = img.code_span()

    print("image        : %s" % args.image)
    print("sizeOfImage  : 0x%X" % img.size_of_image)
    print("file size    : 0x%X (%d MB)" % (len(img.data), len(img.data) // (1024 * 1024)))
    print("code section : %s  [0x%X, 0x%X)  %d MB"
          % (sec_name, start, end, (end - start) // (1024 * 1024)))
    print("scan spans   : %d executable section(s), %d MB total"
          % (len(spans), sum(e - s for s, e in spans) // (1024 * 1024)))
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
        hits = scan_spans(img.data, spans, pat)
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
            if rva + 8 <= len(img.data):
                val = read_offset(img.data, rva, d)
                entry["field_offset"] = val
                # Plausibility gate: a nonsense index means the pattern matched
                # the wrong code, even though it "resolved".
                if d["offset_type"] == "Index" and not (2 <= val < 4096):
                    entry["status"] = "SUSPECT"
                    entry["note"] = "implausible vtable index %d" % val
                elif abs(val) > 0x20000:
                    entry["status"] = "SUSPECT"
                    entry["note"] = "implausible field offset 0x%X" % (val & 0xFFFFFFFF)

        results.append(entry)
        counts[entry["status"]] += 1

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
