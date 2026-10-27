#!/usr/bin/env python3
"""Resolve ambiguous signatures by looking at who calls them.

A pattern that matches four places is a pattern problem, not four functions.
What distinguishes the intended match is that the rest of the binary calls it:
already-resolved functions (from verify.json) contain direct `call`/`jmp` to
their callees. Disassembling those 80-odd functions and recording every direct
target gives, for each ambiguous candidate, a count of known callers.

    one candidate with callers      -> that is the method, take it
    no candidate with callers       -> called only through a vtable / not yet
                                       resolved; fall back to layout proximity

Layout proximity is the second signal: the compiler emits the methods of a class
in one block, so among candidates the one nearest the other resolved methods of
the same class is usually right. It is a heuristic and is reported as such.

Usage:
    python tools/xref_triage.py <image> <sections.json> <verify.json> <triage.json>
"""

import argparse
import json
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_64


def calls_from(img, base, rva, length=3072):
    """Direct call/jmp targets in the first `length` bytes at rva, as RVAs."""
    targets = []
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    code = img[rva:rva + length]
    for insn in md.disasm(code, base + rva):
        if insn.mnemonic in ("call", "jmp") and insn.op_str.startswith("0x"):
            try:
                t = int(insn.op_str, 16)
            except ValueError:
                continue
            targets.append(t - base)
        # A function ends at its first ret; past that the bytes are padding or the
        # next function, and linear disassembly starts inventing instructions.
        if insn.mnemonic.startswith("ret"):
            break
    return targets


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("sections")
    ap.add_argument("verify")
    ap.add_argument("triage")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    img = open(args.image, "rb").read()
    base = int(json.load(open(args.sections, encoding="utf-8"))["moduleBase"], 16)
    verify = json.load(open(args.verify, encoding="utf-8"))["results"]
    triage = json.load(open(args.triage, encoding="utf-8"))

    # Only fully-resolved code can be trusted as a caller: a wrong address would
    # produce a wrong call graph, which is worse than no call graph.
    resolved = [r for r in verify if r["status"] == "OK" and r["kind"] == "sig"]

    callers = {}
    for r in resolved:
        for t in calls_from(img, base, int(r["match_rvas"][0], 16)):
            callers.setdefault(t, []).append(r["name"])

    # Layout anchor per class prefix, used only as a tiebreaker.
    anchors = {}
    for r in resolved:
        prefix = r["name"].split("_")[0]
        anchors.setdefault(prefix, []).append(int(r["match_rvas"][0], 16))

    out = []
    counts = {}
    for e in triage:
        cands = []
        for c in e["candidates"]:
            rva = int(c["rva"], 16)
            name_hits = callers.get(rva, [])
            c2 = dict(c)
            c2["callers"] = name_hits[:6]
            c2["caller_count"] = len(name_hits)
            cands.append(c2)

        called = [c for c in cands if c["caller_count"] > 0]
        verdict = "unresolved"
        pick = None
        if len(called) == 1:
            verdict = "xref"
            pick = called[0]
        elif len(called) > 1:
            # Several candidates are called; not enough to decide.
            verdict = "multiple-callers"
        else:
            prefix = e["name"].split("_")[0]
            pool = anchors.get(prefix, [])
            if pool and len(cands) > 1:
                near = min(cands, key=lambda c: min(abs(int(c["rva"], 16) - a) for a in pool))
                verdict = "proximity"
                pick = near

        counts[verdict] = counts.get(verdict, 0) + 1
        out.append(dict(e, verdict=verdict, pick=pick, candidates=cands))

        print("\n%-46s %-6s %-8s" % (e["name"], e["kind"], verdict))
        for c in sorted(cands, key=lambda c: -c["caller_count"]):
            extra = " value=%s" % c.get("value") if "value" in c else ""
            who = ("  by " + ",".join(c["callers"][:2])) if c["callers"] else ""
            mark = " <== pick" if pick is not None and c["rva"] == pick["rva"] else ""
            print("    %-12s callers=%-2d%s%s%s" % (c["rva"], c["caller_count"], extra, who, mark))

    print("\nverdicts: " + ", ".join("%s=%d" % kv for kv in sorted(counts.items())))
    if args.json:
        json.dump(out, open(args.json, "w", encoding="utf-8"), indent=2)
        print("written to %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
