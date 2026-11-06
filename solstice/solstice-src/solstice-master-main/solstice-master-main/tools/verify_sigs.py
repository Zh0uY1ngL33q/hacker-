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

Report scope: every result here is a STATIC PRE-CHECK candidate. `usable` /
`staticallyUsable` do NOT mean the mapping was verified in the live game.
There is a second offline scanner (tools/scan_sigs.py, mirrored from
tools/re/scan_sigs.py) -- both must stay in sync with
src/SDK/OffsetProvider.cpp::plausibleOffset() and CodeSection::ranges().
"""

import argparse
import json
import mmap
import os
import re
import struct
import sys
from collections import Counter

MATCH_LIMIT = 64


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
        try:
            self._parse()
        except Exception:
            self.data.close()
            raise

    def _parse(self):
        d = self.data
        if len(d) < 0x40:
            raise ValueError("truncated DOS header")
        if d[:2] != b"MZ":
            raise ValueError("not an MZ image (headers missing from dump?)")
        e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
        if e_lfanew > len(d) - 24:
            raise ValueError("PE header lies outside the image")
        if d[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
            raise ValueError("bad PE signature -- dump looks truncated")

        coff = e_lfanew + 4
        num_sections = struct.unpack_from("<H", d, coff + 2)[0]
        opt_size = struct.unpack_from("<H", d, coff + 16)[0]
        opt = coff + 20
        if opt_size < 64 or opt > len(d) - opt_size:
            raise ValueError("truncated PE optional header")
        magic = struct.unpack_from("<H", d, opt)[0]
        if magic != 0x20B:
            raise ValueError("expected PE32+ (0x20B), got 0x%X" % magic)

        self.size_of_image = struct.unpack_from("<I", d, opt + 56)[0]
        if self.size_of_image == 0:
            raise ValueError("PE SizeOfImage is zero")

        sec_off = opt + opt_size
        if num_sections > (len(d) - sec_off) // 40:
            raise ValueError("truncated PE section table")
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
        """Executable ranges, exactly as CodeSection::ranges() builds them.

        The runtime keeps (start, end) pairs where
            size = min(VirtualSize, SizeOfImage - RVA)
        for sections with RVA != 0, VirtualSize != 0, IMAGE_SCN_MEM_EXECUTE and
        RVA < SizeOfImage, then stable-sorts by that CLIPPED size (largest
        first). find() returns the first hit in that order, so the offline
        scanner must use the same clipped size for both the span and the sort.
        An image with no executable range yields [] -- the runtime scans
        nothing in that case; it does NOT fall back to the whole file.
        """
        file_end = len(self.data)
        spans = []
        for s in self.sections:
            rva = s["rva"]
            if s["vsize"] == 0 or rva == 0:
                continue
            if not (s["characteristics"] & 0x20000000):  # IMAGE_SCN_MEM_EXECUTE
                continue
            if rva >= self.size_of_image:
                continue
            # Sort by the range the runtime derives from SizeOfImage, not by the
            # bytes that happen to remain in a truncated dump. Otherwise an
            # incomplete file can reorder equal/near-equal PE sections and make
            # "first match wins" disagree with CodeSection::ranges().
            runtime_size = min(s["vsize"], self.size_of_image - rva)
            scan_end = min(rva + runtime_size, file_end)
            if runtime_size > 0 and scan_end > rva:
                spans.append((rva, scan_end, runtime_size))
        # Python's sort is stable, so equal clipped sizes keep PE order just
        # like std::stable_sort in the C++ implementation.
        spans.sort(key=lambda item: -item[2])
        return [(start, end) for start, end, _ in spans]

    def code_span(self):
        """Largest executable range, as (start, end, name), for the report.

        Derived from exec_spans() so the printed range is the same clipped
        range the scanner actually searched.
        """
        spans = self.exec_spans()
        if not spans:
            return (0, 0, "<no executable range>")
        start, end = spans[0]
        name = "<unknown>"
        for s in self.sections:
            if s["rva"] == start:
                name = s["name"]
                break
        return (start, end, name)


# --------------------------------------------------------------------------
# Pattern parsing / scanning
# --------------------------------------------------------------------------
def parse_pattern(text):
    out = []
    for tok in text.split():
        if tok in ("?", "??"):
            out.append(None)
        elif "?" in tok:
            raise ValueError("nibble wildcard unsupported: %s" % tok)
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
    # re.finditer() resumes after the previous *whole* match and therefore
    # misses overlaps. libhat X1 resumes at match+1. Repeated search() preserves
    # the regex engine's fast literal scan while reproducing X1 exactly.
    pos = start
    last = end - n
    while pos <= last:
        m = rx.search(buf, pos, end)
        if m is None:
            break
        hits.append(m.start())
        if len(hits) >= limit:
            break
        pos = m.start() + 1
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

def read_offset(buf, position, definition, limit=None):
    ctype = definition.get("read_type", "int").strip()
    types = {"int": (4, True), "uint8_t": (1, False), "int8_t": (1, True),
             "uint16_t": (2, False), "int16_t": (2, True),
             "uint32_t": (4, False), "int32_t": (4, True),
             "uint64_t": (8, False), "int64_t": (8, True),
             "uintptr_t": (8, False), "intptr_t": (8, True),
             "unsigned": (4, False)}
    if ctype not in types:
        raise ValueError("unsupported typed offset %r" % ctype)
    size, signed = types[ctype]
    end = len(buf) if limit is None else min(len(buf), limit)
    if position < 0 or position + size > end:
        raise ValueError("offset read outside image")
    raw = int.from_bytes(buf[position:position + size], "little", signed=signed)
    if definition["offset_type"] == "Index":
        # A vtable slot displacement is always a multiple of sizeof(void*).
        # Dividing a non-multiple by 8 invents a plausible-looking slot from a
        # wrong byte; the runtime now refuses that mapping, so must this tool.
        if raw % 8 != 0:
            raise ValueError("vtable displacement %d is not 8-byte aligned" % raw)
        # C++ signed integer division truncates towards zero, not -infinity.
        return (-1 if raw < 0 else 1) * (abs(raw) // 8)
    return raw


def evaluate_definition(img, definition, match_limit=MATCH_LIMIT):
    """Evaluate one scanned mapping without ever decoding an ambiguous hit.

    The returned `status` is the same vocabulary as ResolveStatus.  `matches`
    is exact until `match_limit`; `matchCountCapped` says when the real count may
    be larger.  This function is intentionally usable by the synthetic tests so
    they assert the final verdict and rejection reason, not just helper behavior.
    """
    pat = parse_pattern(definition["pattern"])
    hits = scan_spans(img.data, img.exec_spans(), pat, limit=match_limit)
    n = len(hits)
    entry = dict(definition)
    entry["matches"] = n
    entry["matchCountCapped"] = n == match_limit
    entry["match_rvas"] = ["0x%X" % h for h in hits[:8]]
    entry["staticallyUsable"] = False
    entry["usable"] = False

    if n == 0:
        entry["status"] = "MISSING"
        entry["rejectionReason"] = "pattern did not match any executable range"
        return entry
    if n != 1:
        entry["status"] = "AMBIGUOUS"
        entry["rejectionReason"] = (
            "pattern matched at least %d executable locations" % n
            if entry["matchCountCapped"] else
            "pattern matched %d executable locations" % n)
        # Crucial: do not read an operand from an arbitrary first hit.  Runtime
        # scanSig() returns an empty result at this point as well.
        return entry

    hit = hits[0]
    entry["rva"] = "0x%X" % hit
    image_end = min(len(img.data), img.size_of_image)

    if definition["kind"] == "offset":
        operand = hit + definition["disp_off"]
        try:
            value = read_offset(img.data, operand, definition, image_end)
        except (KeyError, ValueError) as exc:
            message = str(exc)
            if "outside image" in message:
                entry["status"] = "OUT_OF_RANGE"
            elif "not 8-byte aligned" in message:
                entry["status"] = "IMPLAUSIBLE"
            else:
                entry["status"] = "INCONSISTENT"
            entry["rejectionReason"] = message
            return entry

        entry["field_offset"] = value
        is_index = definition["offset_type"] == "Index"
        plausible, reason = cpp_plausible_offset(value, is_index)
        if not plausible:
            entry["status"] = "IMPLAUSIBLE"
            entry["rejectionReason"] = reason
            return entry
    else:
        disp_off = definition["disp_off"]
        if definition["sig_type"] == "RefSig":
            operand = hit + disp_off
            if operand < 0 or operand + 4 > image_end:
                entry["status"] = "OUT_OF_RANGE"
                entry["rejectionReason"] = "rel32 operand read outside image"
                return entry
            rel = int.from_bytes(img.data[operand:operand + 4], "little", signed=True)
            target = operand + 4 + rel
        else:
            target = hit + disp_off
        entry["resolved_rva"] = "0x%X" % target
        if target < 0 or target >= image_end:
            entry["status"] = "OUT_OF_RANGE"
            entry["rejectionReason"] = "resolved address lies outside image"
            return entry

    entry["status"] = "OK"
    entry["staticallyUsable"] = True
    entry["usable"] = True
    entry["rejectionReason"] = ""
    return entry


def cpp_plausible_offset(value, is_index):
    """Mirror OffsetProvider::plausibleOffset() in src/SDK/OffsetProvider.cpp.

    Returns (ok, reason). The runtime gate is:

        limit   = 4096 if is_index else 0x8000
        inRange = 0 <= value < limit
        refuse  = (is_index and value == 0)        # callVirtualFunc rejects <= 0
                  or (not is_index and 0 < value < 8)  # MSVC vtable pointer

    A field offset of 0 IS accepted here (this is a static gate over all field
    kinds; readMemberChecked() is polymorphic-object only and rejects < 8).
    The old `abs(val) > 0x20000` rule accepted 0x8000..0x20000 and negative
    values that the runtime marks Implausible -- that divergence is the bug
    this function removes. The index/8 alignment check lives in read_offset().
    """
    limit = 4096 if is_index else 0x8000
    if not (0 <= value < limit):
        return False, "implausible %s %d (runtime accepts 0 <= v < %d)" % (
            "vtable index" if is_index else "field offset", value, limit)
    if is_index and value == 0:
        return False, "vtable index 0 is rejected by callVirtualFunc()"
    if not is_index and 0 < value < 8:
        return False, "field offset %d lies inside the MSVC vtable pointer" % value
    return True, ""


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
    if not spans:
        print("WARNING: no executable range survived the SizeOfImage clip; "
              "every signature below will be MISSING, exactly as the runtime "
              "would scan nothing")
    print("offline report is a STATIC PRE-CHECK; runtime results may differ "
          "(see --json usable_semantics / runtime_differences)")
    print("sections     :")
    for s in img.sections:
        print("   [%-12s] RVA=0x%08X vsize=0x%08X raw=0x%08X flags=0x%08X"
              % (s["name"], s["rva"], s["vsize"], s["raw_size"], s["characteristics"]))
    print()

    defs, hardcoded = load_definitions(args.src)

    results = []
    counts = Counter()

    for d in defs:
        entry = evaluate_definition(img, d)
        results.append(entry)
        counts[entry["status"]] += 1

    print("=" * 78)
    print("%-14s %-10s %-6s %s" % ("KIND", "STATUS", "HITS", "NAME / NOTES"))
    print("=" * 78)
    for e in results:
        note = e.get("rejectionReason", "")
        extra = ""
        if "field_offset" in e:
            extra = "  off=%s" % e["field_offset"]
        print("%-14s %-10s %-6d %s%s%s"
              % (e["kind"], e["status"], e["matches"], e["name"], extra,
                 ("  <- " + note) if note else ""))

    print()
    print("summary: " + ", ".join("%s=%d" % (k, v) for k, v in sorted(counts.items())))
    print("statically usable scanned mappings: %d/%d  "
          "(STATIC PRE-CHECK ONLY - NOT a runtime verification result)" %
          (sum(1 for e in results if e.get("staticallyUsable")), len(results)))
    print("  offline results are CANDIDATES: the runtime additionally skips "
          "uncommitted/non-readable pages, applies a host-module bounds check, "
          "and aborts if decryption times out")

    if hardcoded:
        live = [h for h in hardcoded if not h["disabled"]]
        print()
        print("hardcoded mappings: %d UNVERIFIED (DEFINE_FIELD audit only; "
              "CLASS_FIELD and literal offsets elsewhere are NOT covered)" % len(live))
        print("  %d commented-out declarations ignored" % (len(hardcoded) - len(live)))
        print("  these are pure version snapshots and are NEVER counted as scanned OK")
        for h in hardcoded:
            print("   %-46s = %-10s%s" % (h["name"], h["value"],
                                          "  (disabled)" if h["disabled"] else ""))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"image": args.image, "codeSection": sec_name,
                       "sizeOfImage": img.size_of_image,
                       "scanSpans": [["0x%X" % s, "0x%X" % e] for s, e in spans],
                       "usable_semantics":
                           "STATIC PRE-CHECK ONLY. staticallyUsable (and its "
                           "alias usable) means the pattern resolved uniquely, "
                           "the typed read succeeded and the derived value passes "
                           "OffsetProvider::plausibleOffset(). It is NOT a runtime "
                           "verification result.",
                       "runtime_differences": [
                           "CodeSection::csReadableChunks() skips pages that are "
                           "not committed/readable at runtime; the offline dump "
                           "contains the whole image",
                           "runtime rejects matches != 1 (Ambiguous/Inconsistent) "
                           "and applies a host-module bounds check (OutOfRange)",
                           "runtime aborts before scanning when decryption times out",
                           "DEFINE_FIELD constants are never runtime-verified",
                       ],
                       "hardcoded_mappings":
                           "%d UNVERIFIED (DEFINE_FIELD audit only; CLASS_FIELD "
                           "and literal offsets are NOT covered)" % len(
                               [h for h in hardcoded if not h["disabled"]]),
                       "results": results, "hardcoded": hardcoded}, f, indent=2)
        print("\nreport written to %s" % args.json)

    # Non-zero exit if anything is broken, so this can gate a script.
    return 0 if counts["OK"] == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
