#!/usr/bin/env python3
"""3.7 workgroup 1: classify every change between b32d185 and origin/main.

Produces docs/batch3_7/commit-classification.json and
docs/batch3_7/file-change-inventory.json.

This is a STATIC git-archaeology tool. It never builds or runs the game.
"runtimeVerified" is false everywhere unless a batch report explicitly says
otherwise; "targetVersionEvidence" is derived from the diff text/commit subject
and is deliberately conservative (only explicit 1.21.120 references count).
"""
import json
import os
import re
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = "b32d185"
TIP = "origin/main"
PREFIX = "solstice/solstice-src/solstice-master-main/solstice-master-main/"
OUT = os.path.join(ROOT, "docs", "batch3_7")

CATEGORIES = {
    "A": "SDK / Mapping",
    "B": "Object Lifetime / InstanceLocator",
    "C": "MinecraftSim",
    "D": "GUI / OpenGL / D3D",
    "E": "ImGui / ClickGUI",
    "F": "RenderEvent / Dispatcher",
    "G": "ESP / Projection / FrameTransform",
    "H": "Aura / Combat",
    "I": "ExceptionHandler / Diagnostics",
    "J": "Build System",
    "Z": "Documentation / audit data / offline tooling (non-runtime)",
}

# Ordered: first match wins. B/C are split out before the broad SDK bucket A.
RULES = [
    ("C", r"src/SDK/Minecraft/MinecraftSim\.(cpp|hpp)$"),
    ("B", r"src/SDK/Minecraft/(ClientInstance|MinecraftGame|InstanceLocator)\.(cpp|hpp)$"),
    ("D", r"src/Hook/Hooks/RenderHooks/(OpenGLGui|D3DHook)\.cpp$"),
    ("E", r"(ClickGui|HudEditor|imgui|ImGui).*\.(cpp|hpp|h)$"),
    ("F", r"(event_dispatcher\.hpp|src/Features/Events/|batch3_6_2_f_)"),
    ("G", r"(Visual/ESP\.cpp|MathUtils\.(cpp|hpp)|RenderUtils\.(cpp|hpp)|FrameTransform|batch3_6/|batch3_6/projection)"),
    ("H", r"(Modules/Combat/|Modules/Player/|Modules/Movement/|Aura2|batch3_6_2_a/)"),
    ("I", r"(Utils/SysUtils/ExceptionHandler|batch3_6_2_[cde]/)"),
    ("J", r"(CMakeLists\.txt$|GenerateBuildInfo\.cmake$|\.cmake$)"),
    ("A", r"(^src/SDK/|^src/Utils/|^src/Hook/|^src/Solstice\.cpp$|mapping-audit|docs/batch2/|docs/batch3/|tools/(scan_sigs|verify_sigs|test_verify_sigs|mapping_audit|batch2_analyze))"),
]


# All paths produced by git are relative to the repository root (/g/hacker),
# not to the nested project directory. Run every git command from that root so
# pathspecs resolve; ROOT stays the project dir for output files only.
GITROOT = subprocess.check_output(
    ["git", "rev-parse", "--show-toplevel"], cwd=ROOT, text=True).strip()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=GITROOT, text=True,
                                   encoding="utf-8", errors="ignore")


def short(path):
    return path[len(PREFIX):] if path.startswith(PREFIX) else path


def categorize(path):
    s = short(path)
    for code, pat in RULES:
        if re.search(pat, s):
            return code
    return "Z"


def is_runtime_source(path):
    return short(path).endswith((".cpp", ".hpp", ".h"))


def port_recommendation(code, path):
    """Conservative default; overridden per-file by the SDK whitelist review."""
    s = short(path)
    if code in ("D", "E", "F", "G", "H", "I", "J", "Z"):
        return "EXCLUDE"
    if code == "B":
        return "DEFER"          # guards depend on newer MemUtils / state machine
    if code == "C":
        return "PORT_S2"        # 411fa4e is independent, see sdk-port-plan.md
    if code == "A":
        return "DEFER"          # mapping hunks depend on ResolveState state machine
    return "EXCLUDE"


def main():
    os.makedirs(OUT, exist_ok=True)

    # ---- commits -----------------------------------------------------------
    raw = git("log", "--reverse", "--format=%H%x1f%h%x1f%ad%x1f%s",
              "--date=iso", f"{BASE}..{TIP}").strip().split("\n")
    commits = []
    for line in raw:
        if not line.strip():
            continue
        full, abbr, date, subject = line.split("\x1f")
        files = git("show", "--format=", "--name-only", full).strip().split("\n")
        files = [f for f in files if f.strip()]
        cats = sorted({categorize(f) for f in files})
        src = [f for f in files if is_runtime_source(f)]
        commits.append({
            "commit": full,
            "short": abbr,
            "date": date,
            "subject": subject,
            "fileCount": len(files),
            "files": [short(f) for f in files],
            "categories": cats,
            "categoryNames": [CATEGORIES[c] for c in cats],
            "mixedCommit": len(cats) > 1,
            "runtimeSourceFiles": [short(f) for f in src],
            "hasRuntimeSource": bool(src),
            "recommendation": ("SELECT_HUNKS_ONLY" if len(cats) > 1 else
                               "REVIEW_AS_UNIT" if src else "DOCS_ONLY"),
            "note": ("Mixed SDK + non-SDK commit: only reviewed relevant hunks may "
                     "be ported. Never cherry-pick this commit wholesale."
                     if len(cats) > 1 else
                     "Documentation/audit-only commit; no runtime code." if not src else
                     "Single-category commit; still review the file range before porting."),
        })

    # ---- files -------------------------------------------------------------
    ns = git("diff", "--name-status", f"{BASE}..{TIP}").strip().split("\n")
    numstat = {}
    for line in git("diff", "--numstat", f"{BASE}..{TIP}").strip().split("\n"):
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) == 3 and parts[0].isdigit() and parts[1].isdigit():
            numstat[parts[2]] = (int(parts[0]), int(parts[1]))

    inventory = []
    for line in ns:
        if not line.strip():
            continue
        status, path = line.split("\t", 1)
        code = categorize(path)
        a, d = numstat.get(path, (0, 0))
        src_commits = git("log", "--format=%h", f"{BASE}..{TIP}", "--", path).strip().split("\n")
        src_commits = [c for c in src_commits if c.strip()]
        diff_full = None
        diff_cmd = f"git diff {BASE}..{TIP} -- {short(path)}"
        if is_runtime_source(path) or short(path) in ("CMakeLists.txt", "GenerateBuildInfo.cmake"):
            diff_full = git("diff", f"{BASE}..{TIP}", "--", path)
        entry = {
            "file": short(path),
            "repoPath": path,
            "status": {"A": "ADDED", "M": "MODIFIED", "D": "DELETED"}.get(status, status),
            "category": code,
            "categoryName": CATEGORIES[code],
            "linesAdded": a,
            "linesDeleted": d,
            "sourceCommits": src_commits,
            "isSdk": code in ("A", "B", "C"),
            "isRuntimeSource": is_runtime_source(path),
            "targetVersionEvidence": "SEE_DIFF",   # refined in sdk-port-plan.md
            "runtimeVerified": False,
            "recommendedPort": port_recommendation(code, path),
            "dependsOn": [],
            "regressionRisk": ("HIGH - excluded by 3.7 policy"
                               if port_recommendation(code, path) == "EXCLUDE" else
                               "REVIEW_REQUIRED"),
            "diff": diff_full,
            "diffCommand": diff_cmd,
        }
        inventory.append(entry)

    commit_doc = {
        "batch": "3.7",
        "base": BASE,
        "tip": TIP,
        "tipCommit": git("rev-parse", TIP).strip(),
        "baseCommit": git("rev-parse", BASE).strip(),
        "commitCount": len(commits),
        "categories": CATEGORIES,
        "method": "git log/diff static archaeology; no build or runtime execution",
        "commits": commits,
    }
    file_doc = {
        "batch": "3.7",
        "base": BASE,
        "tip": TIP,
        "fileCount": len(inventory),
        "categories": CATEGORIES,
        "statusValues": {"ADDED": "file exists only at tip",
                         "MODIFIED": "content differs",
                         "DELETED": "file exists only at base"},
        "diffCoverage": ("Full unified diff embedded for runtime source (.cpp/.hpp/.h) "
                         "and build files; null for docs/audit data (use diffCommand)"),
        "files": inventory,
    }
    for name, doc in (("commit-classification.json", commit_doc),
                      ("file-change-inventory.json", file_doc)):
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=2, ensure_ascii=False)

    print(f"commits: {len(commits)}  files: {len(inventory)}")
    from collections import Counter
    print("file categories:", dict(Counter(e["category"] for e in inventory)))
    print("commit categories (mixed):", sum(1 for c in commits if c["mixedCommit"]))
    print("runtime source files:", sum(1 for e in inventory if e["isRuntimeSource"]))
    print("embedded full diffs:", sum(1 for e in inventory if e["diff"]))


if __name__ == "__main__":
    main()
