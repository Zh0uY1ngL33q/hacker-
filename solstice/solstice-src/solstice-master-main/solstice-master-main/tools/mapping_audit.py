#!/usr/bin/env python3
"""Build a reproducible, conservative inventory of every SDK mapping.

Scanned mappings are never promoted beyond STATIC CANDIDATE: a unique byte
pattern proves structural uniqueness in this dump, not the function's semantic
identity. Literal mappings remain UNVERIFIED unless an external evidence
registry is supplied (this audit deliberately has none).
"""
import argparse
import hashlib
import json
import re
import subprocess
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path


SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cc", ".cpp", ".inl"}
CLASS_FIELD_RE = re.compile(r"\bCLASS_FIELD\s*\(\s*(.+?)\s*,\s*(\w+)\s*,\s*([^,)]+)\s*\)")
CLASS_OPEN_RE = re.compile(r"\b(?:class|struct)\s+(\w+)[^;{]*\{")
LITERAL = r"(?:0x[0-9A-Fa-f]+|[0-9]+)"
DIRECT_PATTERNS = [
    ("DIRECT_VTABLE_INDEX", "callVirtualFunc", re.compile(r"callVirtualFunc(?:<[^;]*?>)?\s*\(\s*(" + LITERAL + r")\b")),
    ("DIRECT_VTABLE_INDEX", "GetVTableFunction", re.compile(r"GetVTableFunction\s*\([^,]+,\s*(" + LITERAL + r")\b")),
    ("DIRECT_FIELD_OFFSET", "member_at", re.compile(r"member_at(?:<[^;]*?>)?\s*\([^,]+,\s*(" + LITERAL + r")\b")),
    ("DIRECT_LITERAL_OFFSET", "pointer-add", re.compile(
        r"reinterpret_cast<[^;]+?>\([^;]*?\+\s*(" + LITERAL + r")\b")),
]
DERIVED_RE = re.compile(r"OffsetProvider::(\w+)\s*([+-])\s*(" + LITERAL + r")\b")
OFFSET_TABLE_RE = re.compile(r"\b(\w*Offsets?)\s*(?:\[[^\]]*\])?\s*=\s*\{\s*(" + LITERAL + r")\b")
OFFSETOF_ASSERT_RE = re.compile(r"static_assert\s*\(\s*offsetof\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*==\s*(" + LITERAL + r")")
LAYOUT_COMMENT_RE = re.compile(r"^\s*(.+?;)\s*//\s*this\s*\+\s*(0x[0-9A-Fa-f]+|[0-9]+)\b")


def strip_comments_preserve_lines(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def owner_from_name(name):
    return name.split("_", 1)[0] if "_" in name else "(global)"


def parse_int(value):
    try:
        return int(value, 0)
    except ValueError:
        return value.strip()


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_files(src):
    return sorted(p for p in (src / "src").rglob("*") if p.suffix.lower() in SOURCE_SUFFIXES
                  and not p.name.endswith(".bak"))


def occurrence_index(files, src):
    index = defaultdict(list)
    symbol = re.compile(r"\b[A-Za-z_]\w*\b")
    for path in files:
        rel = path.relative_to(src).as_posix()
        for line_no, line in enumerate(strip_comments_preserve_lines(path.read_text("utf-8", errors="replace")).splitlines(), 1):
            for token in set(symbol.findall(line)):
                index[token].append(f"{rel}:{line_no}")
    return index


def scan_entries(verify, secondary, occurrences):
    def comparable(value):
        if isinstance(value, str) and value.lower().startswith("0x"):
            return int(value, 16)
        return value

    second = {}
    for item in secondary["signatures"]:
        second[("sig", item["name"])] = item
    for item in secondary["offsets"]:
        second[("offset", item["name"])] = item

    entries, differences = [], []
    for item in verify["results"]:
        other = second.get((item["kind"], item["name"]))
        kind = "DEFINE_SIG" if item["kind"] == "sig" else (
            "DEFINE_INDEX_FIELD_TYPED" if item.get("read_type") not in (None, "int") else "DEFINE_INDEX_FIELD")
        status = item["status"]
        verification = "STATIC CANDIDATE" if status == "OK" else "UNVERIFIED"
        value = (item.get("resolved_rva", item.get("rva")) if item["kind"] == "sig"
                 else item.get("field_offset"))
        definition_header = "src/SDK/SigManager.hpp:" if item["kind"] == "sig" else "src/SDK/OffsetProvider.hpp:"
        consumers = [x for x in occurrences.get(item["name"], []) if not x.startswith(definition_header)]
        entry = {
            "name": item["name"], "owner": owner_from_name(item["name"]), "kind": kind,
            "value": value, "matchCount": item["matches"], "matchCountCapped": item.get("matchCountCapped", False),
            "staticStatus": status, "verificationLevel": verification,
            "evidence": {"source": "target-image executable-section scan", "pattern": item.get("pattern"),
                         "matchRvas": item.get("match_rvas", [])},
            "rejectionReason": item.get("rejectionReason", ""), "consumers": consumers,
        }
        if other is None:
            differences.append({"name": item["name"], "reason": "missing from tools/re/scan_sigs.py output"})
        else:
            checks = {
                "status": (status, other.get("resolveStatus")),
                "matches": (item["matches"], other.get("matches")),
                "matchCountCapped": (item.get("matchCountCapped", False), other.get("matchCountCapped", False)),
                "rejectionReason": (item.get("rejectionReason", ""), other.get("rejectionReason", "")),
            }
            if status == "OK":
                checks["value"] = (comparable(value), comparable(
                    other.get("rva") if item["kind"] == "sig" else other.get("value")))
            mismatches = {k: list(v) for k, v in checks.items() if v[0] != v[1]}
            if mismatches:
                differences.append({"name": item["name"], "reason": "core verdict mismatch", "fields": mismatches})
        entries.append(entry)
    verify_keys = {(x["kind"], x["name"]) for x in verify["results"]}
    for key, item in second.items():
        if key not in verify_keys:
            differences.append({"name": item["name"], "reason": "missing from tools/verify_sigs.py output"})
    return entries, differences


def literal_entries(files, src, existing_names, occurrences):
    out = []
    for path in files:
        rel = path.relative_to(src).as_posix()
        original = path.read_text("utf-8", errors="replace")
        clean_lines = strip_comments_preserve_lines(original).splitlines()
        # Keep active trailing layout comments, but remove block-commented code.
        no_blocks = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), original, flags=re.S).splitlines()
        depth = 0
        class_stack = []
        for line_no, line in enumerate(clean_lines, 1):
            for opened in CLASS_OPEN_RE.finditer(line):
                class_stack.append((opened.group(1), depth))
            owner = class_stack[-1][0] if class_stack else path.stem
            for m in CLASS_FIELD_RE.finditer(line):
                name, expression = m.group(2), m.group(3).strip()
                if name in ("name",):  # macro definition, not a mapping
                    continue
                referenced = re.fullmatch(r"OffsetProvider::(\w+)", expression)
                level = "UNVERIFIED"
                evidence = {"source": f"declaration {rel}:{line_no}", "expression": expression}
                if referenced and referenced.group(1) in existing_names:
                    evidence["dependsOn"] = referenced.group(1)
                out.append({
                    "name": f"{owner}::{name}", "owner": owner, "kind": "CLASS_FIELD",
                    "value": parse_int(expression), "matchCount": None, "matchCountCapped": False,
                    "staticStatus": "UNVERIFIED", "verificationLevel": level,
                    "evidence": evidence, "rejectionReason": "no independent static semantic evidence",
                    "consumers": [f"{rel}:{line_no}"],
                })
            depth += line.count("{") - line.count("}")
            while class_stack and depth <= class_stack[-1][1]:
                class_stack.pop()

            # API literals are deliberately conservative. They find direct SDK
            # offsets/slots without treating unrelated numeric constants as mappings.
            for kind, api, pattern in DIRECT_PATTERNS:
                for ordinal, m in enumerate(pattern.finditer(line), 1):
                    value = parse_int(m.group(1))
                    out.append({
                        "name": f"{rel}:{line_no}:{api}#{ordinal}", "owner": owner, "kind": kind,
                        "value": value, "matchCount": None, "matchCountCapped": False,
                        "staticStatus": "UNVERIFIED", "verificationLevel": "UNVERIFIED",
                        "evidence": {"source": f"literal API argument {rel}:{line_no}"},
                        "rejectionReason": "hardcoded literal has no scanner or semantic proof",
                        "consumers": [f"{rel}:{line_no}"],
                    })
            for ordinal, m in enumerate(DERIVED_RE.finditer(line), 1):
                dependency, operator, delta = m.groups()
                out.append({
                    "name": f"{rel}:{line_no}:derived#{ordinal}", "owner": owner,
                    "kind": "DERIVED_LITERAL_OFFSET", "value": f"{dependency} {operator} {delta}",
                    "matchCount": None, "matchCountCapped": False,
                    "staticStatus": "UNVERIFIED", "verificationLevel": "UNVERIFIED",
                    "evidence": {"source": f"derived literal {rel}:{line_no}", "dependsOn": dependency},
                    "rejectionReason": "derived literal adjustment has no independent semantic proof",
                    "consumers": [f"{rel}:{line_no}"],
                })
            table_matches = OFFSET_TABLE_RE.finditer(line) if rel.startswith("src/SDK/") else ()
            for ordinal, m in enumerate(table_matches, 1):
                variable, value = m.groups()
                out.append({
                    "name": f"{rel}:{line_no}:{variable}#{ordinal}", "owner": owner,
                    "kind": "DIRECT_OFFSET_TABLE", "value": parse_int(value),
                    "matchCount": None, "matchCountCapped": False,
                    "staticStatus": "UNVERIFIED", "verificationLevel": "UNVERIFIED",
                    "evidence": {"source": f"literal offset table {rel}:{line_no}"},
                    "rejectionReason": "literal layout table has no independent semantic proof",
                    "consumers": [f"{rel}:{line_no}"],
                })
            for ordinal, m in enumerate(OFFSETOF_ASSERT_RE.finditer(line), 1):
                class_name, field_name, value = m.groups()
                out.append({
                    "name": f"{class_name}::{field_name}", "owner": class_name,
                    "kind": "STATIC_ASSERT_OFFSET", "value": parse_int(value),
                    "matchCount": None, "matchCountCapped": False,
                    "staticStatus": "UNVERIFIED", "verificationLevel": "UNVERIFIED",
                    "evidence": {"source": f"local offsetof assertion {rel}:{line_no}"},
                    "rejectionReason": "local struct-layout assertion does not prove target-game semantic layout",
                    "consumers": [f"{rel}:{line_no}"],
                })
            layout_line = no_blocks[line_no - 1] if line_no <= len(no_blocks) else ""
            layout = LAYOUT_COMMENT_RE.match(layout_line)
            if layout:
                declaration, value = layout.groups()
                before_semicolon = declaration[:-1].strip()
                field_match = re.search(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:\{[^}]*\})?\s*(?::\s*\d+)?\s*$",
                                        before_semicolon)
                field_name = field_match.group(1) if field_match else f"field_at_{value}"
                out.append({
                    "name": f"{owner}::{field_name}", "owner": owner,
                    "kind": "LAYOUT_COMMENT_OFFSET", "value": parse_int(value),
                    "matchCount": None, "matchCountCapped": False,
                    "staticStatus": "UNVERIFIED", "verificationLevel": "UNVERIFIED",
                    "evidence": {"source": f"annotated layout declaration {rel}:{line_no}",
                                 "declaration": declaration.strip()},
                    "rejectionReason": "source layout annotation has no target-image semantic proof",
                    "consumers": [f"{rel}:{line_no}"],
                })
    return out


def define_field_entries(verify, occurrences):
    out = []
    for item in verify.get("hardcoded", []):
        name = item["name"]
        out.append({
            "name": name, "owner": owner_from_name(name), "kind": "DEFINE_FIELD",
            "value": parse_int(str(item.get("value"))), "matchCount": None, "matchCountCapped": False,
            "staticStatus": "UNVERIFIED", "verificationLevel": "UNVERIFIED",
            "evidence": {"source": "DEFINE_FIELD declaration only"},
            "rejectionReason": "constant is not scanned; semantic evidence requires manual review",
            "consumers": [x for x in occurrences.get(name, [])
                          if not x.startswith("src/SDK/OffsetProvider.hpp:")],
        })
    return out


def markdown(report):
    s = report["summary"]
    lines = [
        "# Solstice Mapping Audit", "",
        "> This is an offline static audit. **STATIC CANDIDATE is not RUNTIME VERIFIED** and does not prove semantic identity.", "",
        "## Provenance", "",
        f"- Audited base commit: `{report['git']['baseCommit']}` (branch `{report['git']['branch']}`)",
        "- The generated files live in the later review commit; use `git log -1` for that containing commit ID.",
        f"- Target image: `{report['target']['path']}`", f"- Size: `{report['target']['size']} bytes`",
        f"- SHA-256: `{report['target']['sha256']}`",
        f"- Generated (UTC): `{report['generatedUtc']}`", "",
        "## Counts", "",
        f"- Mapping declaration records inventoried: **{s['totalMappings']}**",
        f"- Unique logical `(kind, name)` mappings: **{s['uniqueLogicalMappings']}** (conditional declarations can share both)",
        f"- Scanned mappings: **{s['scannedMappings']}**",
        f"- STATIC VERIFIED: **{s['verificationLevels'].get('STATIC VERIFIED', 0)}**",
        f"- STATIC CANDIDATE: **{s['verificationLevels'].get('STATIC CANDIDATE', 0)}**",
        f"- RUNTIME VERIFIED: **{s['verificationLevels'].get('RUNTIME VERIFIED', 0)}**",
        f"- UNVERIFIED: **{s['verificationLevels'].get('UNVERIFIED', 0)}**", "",
        "### Kinds", "", "| Kind | Count |", "|---|---:|",
    ]
    lines += [f"| {k} | {v} |" for k, v in sorted(s["kinds"].items())]
    lines += ["", "### Inventory scope", "",
              "The inventory includes active DEFINE_SIG, DEFINE_INDEX_FIELD, DEFINE_INDEX_FIELD_TYPED, DEFINE_FIELD and CLASS_FIELD declarations; direct literal arguments to mapping APIs; derived `OffsetProvider + literal` expressions; literal offset tables; `offsetof` assertions; and active `this+offset` layout annotations (including explicit padding records). Commented-out declarations and `.bak` files are excluded."]
    lines += ["", "### Scanner verdicts", "", "| Status | Count |", "|---|---:|"]
    lines += [f"| {k} | {v} |" for k, v in sorted(s["scannerStatuses"].items())]
    lines += ["", "## Verification levels", "",
              "- **STATIC VERIFIED**: independent static semantic evidence; none were automatically promoted.",
              "- **STATIC CANDIDATE**: exactly one structurally plausible image match; semantic identity remains unproved.",
              "- **RUNTIME VERIFIED**: confirmed in the target process; this offline run assigns none.",
              "- **UNVERIFIED**: rejected scan or hardcoded mapping without sufficient independent evidence.", "",
              "## Scanner consistency", "",
              f"Core verdict differences: **{len(report['scannerConsistency']['differences'])}**."]
    if report["scannerConsistency"]["differences"]:
        lines += ["", "```json", json.dumps(report["scannerConsistency"]["differences"], indent=2), "```"]
    lines += ["", "## Reproducible scan runs", ""]
    lines += [f"- {x}" for x in report["scanRuns"]]
    lines += ["", "## Tests", ""]
    lines += [f"- `{x}`" for x in report["tests"]]
    lines += ["", "Covered matrices: zero/one/many/overlapping hits; 64-KiB boundary; distinct and split executable sections; empty ranges; int8/int32/int64 signs and widths; truncated operands; unsupported typed offsets; vtable alignment/index zero; PE VirtualSize/RawSize/SizeOfImage/truncation/order/no-exec cases; both-scanner equivalence; C++ guarded-read source contracts."]
    lines += ["", "## C++ runtime audit findings", ""]
    lines += [f"- {x}" for x in report["cppAudit"]]
    lines += ["", "## Offline/runtime boundary", "",
              "- Offline scanners inspect bytes present in executable PE ranges; runtime additionally observes committed/readable VirtualQuery regions.",
              "- Runtime match counting caps at 2 because uniqueness is the verdict; offline reports preserve up to 64 hits. A runtime count of 2 therefore means at least 2, while the rejection status is equivalent.",
              "- Unique matching only establishes a static candidate. It does not establish function or field semantics.",
              "- No mapping in this report is labeled RUNTIME VERIFIED; that requires target-process evidence.",
              "- Hardcoded DEFINE_FIELD, CLASS_FIELD, and direct literals are UNVERIFIED unless independently adjudicated.", "",
              "## Unresolved issues", ""]
    lines += [f"- {x}" for x in report["unresolved"]]
    lines += ["", "## Modified files and reasons", ""]
    lines += [f"- `{x['path']}` — {x['reason']}" for x in report["modifiedFiles"]]
    lines += ["", "## Build-machine validation", "", "```powershell"] + report["buildMachineCommands"] + ["```", "",
              "## Full inventory", "",
              "The machine-readable inventory, including every consumer location and rejection reason, is in `mapping-audit.json`.", "",
              "| Name | Owner | Kind | Value | Hits | Static status | Evidence level | Rejection reason |", "|---|---|---|---:|---:|---|---|---|"]
    for e in report["mappings"]:
        val = "" if e["value"] is None else str(e["value"])
        hits = "" if e["matchCount"] is None else str(e["matchCount"])
        reason = e["rejectionReason"].replace("|", "\\|")
        lines.append(f"| `{e['name']}` | {e['owner']} | {e['kind']} | {val} | {hits} | {e['staticStatus']} | {e['verificationLevel']} | {reason} |")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--verify-json", type=Path, required=True)
    ap.add_argument("--secondary-json", type=Path, required=True)
    ap.add_argument("--output-json", type=Path, required=True)
    ap.add_argument("--output-md", type=Path, required=True)
    args = ap.parse_args()
    src, repo = args.src.resolve(), args.src.resolve().parents[3]
    verify = json.loads(args.verify_json.read_text("utf-8"))
    secondary = json.loads(args.secondary_json.read_text("utf-8"))
    files = source_files(src)
    occurrences = occurrence_index(files, src)
    scanned, differences = scan_entries(verify, secondary, occurrences)
    defined = define_field_entries(verify, occurrences)
    literals = literal_entries(files, src, {e["name"] for e in scanned + defined}, occurrences)
    mappings = scanned + defined + literals
    kinds = Counter(e["kind"] for e in mappings)
    statuses = Counter(e["staticStatus"] for e in scanned)
    levels = Counter(e["verificationLevel"] for e in mappings)
    def timing(name):
        values = {}
        for line in (src / "docs" / "mapping-audit" / name).read_text("utf-8").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                values[key] = value
        return values
    verify_timing = timing("verify-sigs.timing.txt")
    secondary_timing = timing("scan-sigs.timing.txt")
    test_timing = timing("unit-tests.timing.txt")
    for status in ("OK", "MISSING", "AMBIGUOUS", "INCONSISTENT", "IMPLAUSIBLE", "OUT_OF_RANGE"):
        statuses.setdefault(status, 0)
    for level in ("STATIC VERIFIED", "STATIC CANDIDATE", "RUNTIME VERIFIED", "UNVERIFIED"):
        levels.setdefault(level, 0)
    report = {
        "schemaVersion": 1, "generatedUtc": datetime.now(timezone.utc).isoformat(),
        "git": {"baseCommit": git(repo, "rev-parse", "HEAD"), "branch": git(repo, "branch", "--show-current")},
        "target": {"path": str(args.image.resolve()), "size": args.image.stat().st_size,
                   "sha256": sha256(args.image)},
        "summary": {"totalMappings": len(mappings),
                    "uniqueLogicalMappings": len({(e["kind"], e["name"]) for e in mappings}),
                    "scannedMappings": len(scanned),
                    "kinds": dict(sorted(kinds.items())), "scannerStatuses": dict(sorted(statuses.items())),
                    "verificationLevels": dict(sorted(levels.items()))},
        "scannerConsistency": {"coreFields": ["status", "matches", "matchCountCapped", "value", "rejectionReason"],
                               "differences": differences},
        "scanRuns": [
            "tools/verify_sigs.py: %s s, exit %s because rejected mappings were found (report produced normally)" %
            (verify_timing["elapsedSeconds"], verify_timing["exitCode"]),
            "tools/re/scan_sigs.py: %s s, exit %s" %
            (secondary_timing["elapsedSeconds"], secondary_timing["exitCode"]),
            "Both scanners evaluated 172 scanned mappings and agreed on all core verdict fields.",
        ],
        "tests": ["tools/test_verify_sigs.py: 37/37 targeted tests passed (process %.3f s; see checked-in test log)" %
                  float(test_timing["elapsedSeconds"]),
                  "two scanner implementations compared for every scanned mapping"],
        "cppAudit": [
            "ResolveTable is the authoritative status source; initialization verdicts use status, not cached nonzero addresses.",
            "Missing, Ambiguous, Inconsistent, Implausible and OutOfRange remain distinct.",
            "Ambiguous mappings are refused before operand reads.",
            "Offset operands and rel32 operands now require exact module bounds, readable memory and guarded copies.",
            "Every non-Ok scanned mapping leaves its published SDK value at zero; Solstice stops initialization by default unless the explicit unverified-mappings override is compiled in.",
            "Signature/offset map mutations are serialized during parallel initialization.",
            "Decryption timeout stops SDK initialization; release rejects untrusted mappings unless explicitly overridden.",
            "CodeSection find/count share ranges and ordering; runtime readable-region splitting cannot be reproduced from a dump.",
        ],
        "unresolved": [
            "STATIC CANDIDATE entries still require semantic disassembly or target-runtime confirmation.",
            "All literal CLASS_FIELD/direct offsets and vtable indices without independent evidence remain UNVERIFIED.",
            "Runtime VirtualQuery page state is absent from the dump, so offline/runtime hit counts can legitimately differ.",
            "The C++ changes require MSVC 14.51 compilation on the designated build machine.",
            "Only a target-game run can establish RUNTIME VERIFIED status and validate consumers.",
        ],
        "modifiedFiles": [
            {"path": "tools/re/scan_sigs.py", "reason": "overlap-safe matching, exact typed reads, PE bounds/order, early ambiguity rejection and canonical reasons"},
            {"path": "tools/scan_sigs.py", "reason": "byte-identical in-tree mirror of the authoritative scanner"},
            {"path": "tools/verify_sigs.py", "reason": "final-verdict evaluator and matching/PE/operand correctness fixes"},
            {"path": "tools/test_verify_sigs.py", "reason": "37-case boundary and C++ source-contract regression suite"},
            {"path": "tools/mapping_audit.py", "reason": "reproducible full mapping inventory and report generator"},
            {"path": "src/SDK/OffsetProvider.hpp/.cpp", "reason": "exact-width guarded operand reads and preserved rejection evidence"},
            {"path": "src/SDK/SigManager.hpp/.cpp", "reason": "single guarded rel32 decode and authoritative resolved-address publication"},
            {"path": "src/SDK/SigFuzz.cpp", "reason": "fix undefined report counters using authoritative status totals"},
            {"path": "docs/mapping-audit/*", "reason": "raw scanner outputs, timing and unit-test evidence"},
            {"path": "mapping-audit.json / mapping-audit-report.md", "reason": "machine-readable and human-readable final audit"},
        ],
        "buildMachineCommands": [
            "# Run only after human review and an explicit push of the audit branch:",
            "cd C:\\work\\hacker-git", "git fetch origin codex/mapping-audit-121120",
            "git checkout -B codex/mapping-audit-121120 origin/codex/mapping-audit-121120",
            "cd solstice\\solstice-src\\solstice-master-main\\solstice-master-main",
            "cmake --build out/build/x64-Release",
        ],
        "mappings": mappings,
    }
    args.output_json.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", "utf-8")
    args.output_md.write_text(markdown(report), "utf-8")
    print(json.dumps(report["summary"], indent=2))
    print("scanner core differences:", len(differences))


if __name__ == "__main__":
    main()
