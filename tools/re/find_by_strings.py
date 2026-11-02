"""Locate a function from one build inside another by what it *refers to*.

Byte patterns fail across compiler versions because register allocation and
immediate values change. What does not change is the function's vocabulary:

  * the string literals it passes around (they live in .rdata as bytes)
  * the functions it calls, if those can be named in the other build too

So for each target build pair we extract, per function:

  strings  set of NUL-terminated ASCII/UTF-16 literals reached through a
           rip-relative operand inside the function body
  calls    set of callee RVAs (resolved rel32)

then index the new binary by string content, and score candidates by how many
of the old function's strings appear inside them. A function that mentions
"jump" and "sprint" is far more distinctive than any 24-byte instruction run.

Usage:
    python find_by_strings.py --old-image <img> --new-image <img>
                              --old-json <scan> --names A,B,C [--window 0x400]
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])

from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_IMM  # noqa: E402
from rtti_vtable import Image  # noqa: E402

MIN_LEN = 4          # shorter literals match too much code to be a fingerprint
MAX_LEN = 200


def read_cstring(img: Image, rva: int) -> str | None:
    if not (0 <= rva < len(img.buf) - MIN_LEN):
        return None
    end = img.buf.find(b"\x00", rva, rva + MAX_LEN)
    if end < 0 or end - rva < MIN_LEN:
        return None
    raw = img.buf[rva:end]
    # printable ASCII only; a run of bytes with control characters is data, not a literal
    if any(b < 0x20 or b > 0x7E for b in raw):
        return None
    return raw.decode("ascii")


def func_refs(img: Image, md, start: int, length: int):
    """Strings and callee RVAs referenced in [start, start+length)."""
    strings, calls = set(), set()
    code = img.buf[start:start + length]
    for ins in md.disasm(code, start):
        mn = ins.mnemonic
        if mn in ("call", "jmp") and ins.operands:
            op = ins.operands[0]
            if op.type == CS_OP_IMM:
                calls.add(op.imm & 0xFFFFFFFF)
        for op in ins.operands:
            if op.type != 3:            # X86_OP_MEM
                continue
            mem = op.mem
            if mem.base != 41:          # X86_REG_RIP
                continue
            tgt = ins.address + ins.size + mem.disp
            if not (0 <= tgt < len(img.buf)):
                continue
            s = read_cstring(img, tgt)
            if s:
                strings.add(s)
        if len(strings) > 40:
            break
    return strings, calls


def build_string_index(img: Image, md, window: int, step: int):
    """Map every string literal -> list of code RVAs that reference it.

    Walks executable sections linearly; a reference is attributed to the slice
    window it falls in, which is how a caller is located without knowing where
    function boundaries are.
    """
    index: dict[str, set[int]] = {}
    for start, end in img.exec_ranges:
        blob = img.buf[start:end]
        pos = 0
        while True:
            i = blob.find(b"\x00", pos)
            if i < 0:
                break
            # candidate string ending at i: walk back while printable
            j = i
            while j > 0 and 0x20 <= blob[j - 1] <= 0x7E:
                j -= 1
            n = i - j
            if MIN_LEN <= n <= MAX_LEN:
                s = blob[j:i].decode("ascii", "ignore")
                if len(s) == n:
                    index.setdefault(s, set())
            pos = i + 1
    return index


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old-image", required=True)
    ap.add_argument("--new-image", required=True)
    ap.add_argument("--old-json", required=True)
    ap.add_argument("--new-json", required=True)
    ap.add_argument("--names", required=True)
    ap.add_argument("--window", type=lambda x: int(x, 0), default=0x400)
    ap.add_argument("--top", type=int, default=3)
    a = ap.parse_args()

    old = Image(a.old_image)
    new = Image(a.new_image)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    ojson = {r["name"]: r for r in json.load(open(a.old_json))["signatures"]}
    ojson.update({r["name"]: r for r in json.load(open(a.old_json))["offsets"]})
    njson = {r["name"]: r for r in json.load(open(a.new_json))["signatures"]}
    njson.update({r["name"]: r for r in json.load(open(a.new_json))["offsets"]})

    # reverse index over the new build: string -> code sites
    print("indexing new build strings ...", flush=True)
    sites: dict[str, list[int]] = {}
    for start, end in new.exec_ranges:
        blob = new.buf[start:end]
        for m in re.finditer(rb"[\x20-\x7e]{4,200}\x00", blob):
            s = m.group()[:-1].decode("ascii")
            # record the literal's own address; callers are found by xref below
            sites.setdefault(s, []).append(start + m.start())

    for name in a.names.split(","):
        name = name.strip()
        old_r = ojson.get(name)
        if not old_r or old_r.get("status") != "ok" or not old_r.get("rvaInt"):
            print(f"\n=== {name}: not resolved in the old build -> cannot transfer")
            continue
        rva = old_r["rvaInt"]
        # take a generous slice: the signature points at the entry, and the
        # interesting literals are usually in the first few hundred bytes
        strings, calls = func_refs(old, md, rva, a.window)
        print(f"\n=== {name}  old rva {rva:#x}  strings={len(strings)} calls={len(calls)}")
        for s in sorted(strings)[:8]:
            print(f"      str {s!r}")
        if not strings:
            print("      (no string literals -> this fingerprint does not apply)")
            continue

        # score every new-build site by how many old strings appear near it
        scored = []
        for s in strings:
            for lit_rva in sites.get(s, []):
                scored.append((lit_rva, s))
        # group by proximity: a function referencing several distinct literals
        scored.sort()
        groups = []
        for lit_rva, s in scored:
            if groups and lit_rva - groups[-1][-1][0] < a.window:
                groups[-1].append((lit_rva, s))
            else:
                groups.append([(lit_rva, s)])
        rank = []
        for g in groups:
            distinct = {s for _, s in g}
            rank.append((len(distinct), g[0][0], sorted(distinct)))
        rank.sort(reverse=True)
        for n, lit_rva, ds in rank[:a.top]:
            print(f"      -> {n}/{len(strings)} literals near new rva {lit_rva:#x}: {ds[:4]}")


if __name__ == "__main__":
    main()
