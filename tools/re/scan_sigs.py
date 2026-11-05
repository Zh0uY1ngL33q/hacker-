#!/usr/bin/env python3
"""Offline re-implementation of Solstice's signature resolution.

Reads the ModuleDump memory image (byte at file offset X == process byte at RVA X)
and evaluates every DEFINE_SIG / DEFINE_INDEX_FIELD declared in the headers, using
exactly the same semantics as SigManager::scanSig / OffsetProvider::scanSig:

  * search only sections whose Characteristics include MEM_EXECUTE
  * first match wins
  * DEFINE_SIG(..., SigType::RefSig, off) -> rel32 at match+off, target = match+off+4+rel
  * DEFINE_SIG(..., SigType::Sig, off)     -> match (+off, normally 0)
  * DEFINE_INDEX_FIELD(..., idx_off, Index)     -> int32 at match+idx_off, /8
  * DEFINE_INDEX_FIELD(..., idx_off, FieldOffset)-> int32 at match+idx_off

Report scope: every result is a STATIC PRE-CHECK candidate; `usable` /
`staticallyUsable` are NOT runtime verification results. This file is mirrored
byte-for-byte to <solstice tree>/tools/scan_sigs.py so it ships in the source
ZIP; keep both copies in sync with src/SDK/OffsetProvider.cpp::plausibleOffset()
and CodeSection::ranges().
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys

MEM_EXECUTE = 0x20000000


class Image:
    def __init__(self, path: str):
        with open(path, "rb") as f:
            self.buf = f.read()
        self.base = self._image_base()
        # PE32+ OptionalHeader.SizeOfImage is at opt + 56, opt = e_lfanew + 24.
        # CodeSection::ranges() clips every executable section to it, so the
        # offline scan must clip identically.
        e_lfanew = struct.unpack_from("<I", self.buf, 0x3C)[0]
        self.size_of_image = struct.unpack_from("<I", self.buf, e_lfanew + 24 + 56)[0]
        self.sections = self._sections()

    def _image_base(self) -> int:
        e_lfanew = struct.unpack_from("<I", self.buf, 0x3C)[0]
        return struct.unpack_from("<Q", self.buf, e_lfanew + 0x30)[0]

    def _sections(self):
        e_lfanew = struct.unpack_from("<I", self.buf, 0x3C)[0]
        nsec = struct.unpack_from("<H", self.buf, e_lfanew + 6)[0]
        opt_size = struct.unpack_from("<H", self.buf, e_lfanew + 20)[0]
        off = e_lfanew + 24 + opt_size
        out = []
        for i in range(nsec):
            raw = self.buf[off + i * 40: off + i * 40 + 40]
            name = raw[:8].rstrip(b"\x00")
            vsize, rva, rawsize = struct.unpack_from("<III", raw, 8)
            chars = struct.unpack_from("<I", raw, 36)[0]
            out.append({
                "index": i,
                "name": name.decode("latin1"),
                "rva": rva,
                "vsize": vsize,
                "rawsize": rawsize,
                "chars": chars,
                "exec": bool(chars & MEM_EXECUTE),
            })
        return out

    def exec_ranges(self):
        # Exactly CodeSection::ranges(): keep sections with RVA != 0,
        # VirtualSize != 0, IMAGE_SCN_MEM_EXECUTE and RVA < SizeOfImage; clip
        # size = min(VirtualSize, SizeOfImage - RVA); stable-sort by that
        # CLIPPED size (largest first). Using max(vsize, rawsize) or sorting by
        # the unclipped vsize makes "first match wins" pick a different byte
        # than the in-client resolver. No executable range -> scan nothing.
        image_end = min(len(self.buf), self.size_of_image)
        ranges = []
        for s in self.sections:
            if not s["exec"]:
                continue
            start = s["rva"]
            if start == 0 or s["vsize"] == 0 or start >= image_end:
                continue
            size = min(s["vsize"], image_end - start)
            if size > 0:
                ranges.append((start, start + size, size))
        ranges.sort(key=lambda r: -r[2])
        for start, end, _ in ranges:
            yield start, end

    def read(self, rva: int, n: int) -> bytes:
        return self.buf[rva:rva + n]


def compile_pattern(text: str):
    """'48 8B ? ? ...' -> (bytes, mask) with mask[i] False for wildcards."""
    toks = text.split()
    vals = bytearray()
    mask = bytearray()
    for t in toks:
        if t in ("?", "??"):
            vals.append(0)
            mask.append(0)
        elif t.startswith("?"):          # nibble wildcards not used in this tree
            raise ValueError(f"nibble wildcard unsupported: {t}")
        else:
            vals.append(int(t, 16))
            mask.append(1)
    return bytes(vals), bytes(mask)


_REGEX_CACHE: dict[bytes, "re.Pattern[bytes]"] = {}


def _to_regex(pat: bytes, mask: bytes):
    """Compile a wildcard byte pattern into a bytes regex.

    Python's `re` scans in C and uses a Boyer-Moore style literal prefix when the
    pattern starts with fixed bytes, which every signature in this tree does. The
    naive per-byte Python check over 146 MB of .text was ~3 orders of magnitude
    slower and never finished.
    """
    if not any(m == 0 for m in mask):
        key = bytes(pat)
        rx = _REGEX_CACHE.get(key)
        if rx is None:
            rx = re.compile(re.escape(pat), re.DOTALL)
            _REGEX_CACHE[key] = rx
        return rx
    key = bytes(pat) + b"|" + bytes(mask)
    rx = _REGEX_CACHE.get(key)
    if rx is None:
        out = bytearray()
        for v, m in zip(pat, mask):
            out += re.escape(bytes([v])) if m else b"."
        rx = re.compile(bytes(out), re.DOTALL)
        _REGEX_CACHE[key] = rx
    return rx


def find_all(img: Image, pat, mask, limit: int):
    """Every match RVA (up to limit) across executable sections."""
    rx = _to_regex(pat, mask)
    n = len(pat)
    hits = []
    for start, end in img.exec_ranges():
        blob = img.buf[start:end]
        for m in rx.finditer(blob):
            hits.append(start + m.start())
            if len(hits) >= limit:
                return hits
    return hits


SIG_RE = re.compile(
    r"DEFINE_SIG\(\s*(\w+)\s*,\s*\"([^\"]+)\"\s*,\s*SigType::(\w+)\s*,\s*(-?\w+)\s*\)")
# The two field macros differ in arity and, more importantly, in how wide the
# displacement is read. DEFINE_INDEX_FIELD reads an int; DEFINE_INDEX_FIELD_TYPED
# reads whatever the first argument names, and in this tree every such use is
# uint8_t -- a disp8, one byte. Reading four bytes there invents values like
# 25905216 for BedrockPlatformUWP_mcGame, which the client never computes.
_FIELD_TYPED_RE = re.compile(
    r"DEFINE_INDEX_FIELD_TYPED\(\s*(\w+)\s*,\s*(\w+)\s*,\s*\"([^\"]+)\"\s*,\s*(-?\w+)\s*,\s*OffsetType::(\w+)\s*\)")
_FIELD_PLAIN_RE = re.compile(
    r"DEFINE_INDEX_FIELD\(\s*(\w+)\s*,\s*\"([^\"]+)\"\s*,\s*(-?\w+)\s*,\s*OffsetType::(\w+)\s*\)")

# <format char>, size in bytes. Mirrors what the C++ macro dereferences.
FIELD_WIDTHS = {
    "uint8_t": ("B", 1), "int8_t": ("b", 1),
    "uint16_t": ("H", 2), "int16_t": ("h", 2),
    "uint32_t": ("I", 4), "int32_t": ("i", 4),
    "int": ("i", 4), "unsigned": ("I", 4),
    "uintptr_t": ("Q", 8), "intptr_t": ("q", 8),
}


def cpp_plausible_offset(value, is_index):
    """Mirror OffsetProvider::plausibleOffset() in src/SDK/OffsetProvider.cpp.

    limit = 4096 (Index) or 0x8000 (FieldOffset); accept only 0 <= v < limit,
    reject index 0 (callVirtualFunc rejects <= 0) and field offsets 1..7 (the
    MSVC vtable pointer). A field offset of 0 IS accepted here.
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


def strip_comments(src: str) -> str:
    """Remove // and /* */ comments before parsing.

    Without this the commented-out declarations (ClientInstance_getScreenName,
    the glm_* block, JSON_toStyledString, ...) are counted as live signatures and
    the failure list stops matching the in-client report.
    """
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"':
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == '"':
                    break
                j += 1
            out.append(src[i:j + 1])
            i = j + 1
            continue
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def parse_headers(paths):
    sigs, fields = [], []
    for p in paths:
        src = strip_comments(open(p, encoding="utf-8", errors="replace").read())
        for m in SIG_RE.finditer(src):
            sigs.append({"name": m.group(1), "pattern": m.group(2),
                          "type": m.group(3), "offset": m.group(4)})
        for m in _FIELD_TYPED_RE.finditer(src):
            fields.append({"name": m.group(2), "pattern": m.group(3),
                           "idx": m.group(4), "kind": m.group(5),
                           "ctype": m.group(1)})
        for m in _FIELD_PLAIN_RE.finditer(src):
            fields.append({"name": m.group(1), "pattern": m.group(2),
                           "idx": m.group(3), "kind": m.group(4),
                           "ctype": "int"})
    return sigs, fields


def eval_sig(img: Image, entry):
    pat, mask = compile_pattern(entry["pattern"])
    hits = find_all(img, pat, mask, 4)
    if not hits:
        return {"name": entry["name"], "status": "failed", "matches": 0, "rva": None}
    off = int(entry["offset"], 0)
    hit = hits[0]
    if entry["type"] == "RefSig":
        site = hit + off
        rel = struct.unpack_from("<i", img.buf, site)[0]
        target = site + 4 + rel
    else:
        target = hit + off
    # Runtime now REFUSES ambiguous mappings (value stays 0), so an offline
    # "ambiguous" entry is not a usable resolution either. STATIC PRE-CHECK ONLY.
    static_ok = len(hits) == 1
    return {"name": entry["name"],
            "status": "ambiguous" if len(hits) > 1 else "ok",
            "staticallyUsable": static_ok,
            "usable": static_ok,
            "matches": len(hits),
            "rva": hex(target),
            "rvaInt": target,
            "hit": hex(hit)}


def eval_field(img: Image, entry):
    pat, mask = compile_pattern(entry["pattern"])
    hits = find_all(img, pat, mask, 4)
    if not hits:
        return {"name": entry["name"], "status": "failed", "matches": 0,
                "staticallyUsable": False, "usable": False, "value": None}
    idx = int(entry["idx"], 0)
    # Read with the width the C++ macro dereferences (a uint8_t disp8 must not
    # be read as four bytes, which invents values the client never computes).
    ctype = entry.get("ctype", "int")
    fmt = FIELD_WIDTHS.get(ctype, ("i",))[0]
    width = struct.calcsize("<" + fmt)
    if hits[0] + idx + width > len(img.buf):
        return {"name": entry["name"], "status": "suspect", "matches": len(hits),
                "staticallyUsable": False, "usable": False, "value": None,
                "note": "typed offset read outside image"}
    raw = struct.unpack_from("<" + fmt, img.buf, hits[0] + idx)[0]
    is_index = entry["kind"] == "Index"
    # A vtable slot displacement is always a multiple of sizeof(void*); the C++
    # macro refuses a non-aligned one before dividing by 8.
    aligned = (not is_index) or (raw % 8 == 0)
    # C++ signed integer division truncates towards zero; Python // floors.
    if is_index and aligned:
        val = (abs(raw) // 8) * (-1 if raw < 0 else 1)
    else:
        val = int(raw)
    if not aligned:
        plausible, reason = False, "vtable displacement %d is not 8-byte aligned" % raw
    else:
        plausible, reason = cpp_plausible_offset(val, is_index)

    status = "ambiguous" if len(hits) > 1 else ("ok" if plausible else "suspect")
    static_ok = len(hits) == 1 and aligned and plausible
    return {"name": entry["name"],
            "status": status,
            "staticallyUsable": static_ok,  # STATIC PRE-CHECK ONLY
            "usable": static_ok,            # legacy alias
            "matches": len(hits),
            "value": val,
            "raw": raw,
            "aligned": aligned,
            "note": reason,
            "hit": hex(hits[0])}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--sig-header", action="append", default=[])
    ap.add_argument("--field-header", action="append", default=[])
    ap.add_argument("--json")
    ap.add_argument("--show", choices=["failed", "ambiguous", "ok", "all"], default="failed")
    a = ap.parse_args()

    img = Image(a.image)
    spans = list(img.exec_ranges())
    print(f"image={a.image} base={hex(img.base)} size={hex(len(img.buf))} "
          f"sizeOfImage={hex(img.size_of_image)}")
    print(f"scan spans: {len(spans)} executable range(s); "
          f"STATIC PRE-CHECK ONLY, NOT a runtime verification result")
    for s in img.sections:
        print(f"  [{s['index']:2}] {s['name']!r:12} rva={s['rva']:#x} "
              f"vsize={s['vsize']:#x} exec={s['exec']}")

    sigs, fields = parse_headers(a.sig_header)
    _, fields2 = parse_headers(a.field_header)
    fields += fields2

    rs = [eval_sig(img, e) for e in sigs]
    rf = [eval_field(img, e) for e in fields]

    for label, rows in (("SIGNATURES", rs), ("OFFSETS", rf)):
        nfail = sum(1 for r in rows if r["status"] in ("failed", "suspect"))
        namb = sum(1 for r in rows if r["status"] == "ambiguous")
        nusable = sum(1 for r in rows if r.get("staticallyUsable"))
        print(f"\n=== {label}: total={len(rows)} failed/suspect={nfail} "
              f"ambiguous={namb} staticallyUsable={nusable}")
        for r in rows:
            if a.show == "all" or r["status"] == a.show:
                extra = r.get("value", r.get("rva"))
                print(f"  {r['status']:9} {r['name']:52} matches={r['matches']} -> {extra}")

    if a.json:
        json.dump({"base": hex(img.base),
                   "sizeOfImage": img.size_of_image,
                   "scanSpans": [["0x%X" % s, "0x%X" % e] for s, e in spans],
                   "usable_semantics":
                       "STATIC PRE-CHECK ONLY. staticallyUsable (alias usable) "
                       "means unique match + typed read OK + value passes "
                       "OffsetProvider::plausibleOffset(). NOT a runtime result.",
                   "runtime_differences": [
                       "runtime skips uncommitted/non-readable pages "
                       "(csReadableChunks); the offline dump contains the whole image",
                       "runtime applies a host-module bounds check and rejects "
                       "matches != 1 (Ambiguous/Inconsistent)",
                       "runtime aborts if decryption times out",
                       "DEFINE_FIELD constants are never runtime-verified",
                   ],
                   "signatures": rs, "offsets": rf},
                  open(a.json, "w"), indent=1)
        print(f"\nwrote {a.json}")


if __name__ == "__main__":
    main()
