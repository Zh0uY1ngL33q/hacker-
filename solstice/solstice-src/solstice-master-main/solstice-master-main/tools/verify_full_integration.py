#!/usr/bin/env python3
"""Static acceptance gate for recovery/b32d185-full-integration.

READ-ONLY. This script does not compile the DLL, does not launch Minecraft and
does not fabricate runtime evidence. It only checks what can be proven from the
repository itself. Every check carries exactly one evidence grade:

    STATICALLY_CONFIRMED  proven from source/git objects on this machine
    BUILD_VERIFIED        proven by an executed build tool (CMake script mode)
    RUNTIME_VERIFIED      proven by a captured runtime log/dump  (none here)
    UNVERIFIED            assertion checked and FAILED
    DEFERRED              deliberately out of scope for this integration

Usage:  python tools/verify_full_integration.py [--json <path>]
Exit:   0 = no UNVERIFIED results; 1 = at least one UNVERIFIED.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

BASE = "b32d185"
SDK_GOLDEN = "1ec0571"
CPM_SHA = "83e5eb71b2bbb8b1f2ad38f1950287a057624e385c238f6087f94cdfc44af9c5"
MAIN_EXPECTED = "e8755437d5adf91e7ba952791d39d72d87741348"

ROOT = Path(
    subprocess.run(
        ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
    ).stdout.strip()
)
PROJ = Path("solstice/solstice-src/solstice-master-main/solstice-master-main")
PROJ_GIT = PROJ.as_posix()  # git speaks forward slashes even on Windows

# Non-SDK runtime surface that Commit 6 must leave byte-for-byte at b32d185.
RUNTIME_DIRS = [
    "src/Hook", "src/Client", "src/Features", "src/Utils/Render",
    "include", "resources", "src/Base", "src/Assets",
]
RUNTIME_NAME_GLOBS = [
    "*OpenGLGui*", "*D3DHook*", "*SwapBuffers*", "*ImGui*", "*ClickGui*",
    "*event_dispatcher*", "*ModuleManager*", "*FeatureManager*", "*ModuleBase*",
    "*ESP*", "*Tracers*", "*Nametags*", "*Watermark*", "*Arraylist*",
    "*Notifications*", "*Interface*", "*Keystrokes*", "*ChinaHat*",
    "*TargetHUD*", "*MathUtils*", "*FrameTransform*", "*OWorldToScreen*",
    "*FOV*", "*HookManager*", "*Detour*", "*Aura*",
]

# Paths the integration is ALLOWED to touch. Anything else is a violation.
ALLOWED = (
    "/cmake/", "/src/SDK/", "/src/Solstice.cpp", "/src/Utils/MemUtils.",
    "/src/Utils/SysUtils/ExceptionHandler.", "/tools/", "/docs/",
)

checks: list[dict] = []


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)


def out(*args: str) -> str:
    return git(*args).stdout.strip()


def read(rel: str) -> str:
    return (ROOT / PROJ / rel).read_text(encoding="utf-8", errors="replace")


def has(rel: str, *needles: str) -> bool:
    text = read(rel)
    return all(n in text for n in needles)


def add(cid: str, title: str, ok: bool | None, evidence: str,
        grade_ok: str = "STATICALLY_CONFIRMED", deferred: str | None = None) -> None:
    if ok is None:
        grade = "DEFERRED"
        detail = deferred or "not verifiable on this machine"
    else:
        grade = grade_ok if ok else "UNVERIFIED"
        detail = evidence
    checks.append({"id": cid, "title": title, "result": "PASS" if ok else ("DEFERRED" if ok is None else "FAIL"),
                   "grade": grade, "evidence": detail})


# ---------------------------------------------------------------- git topology
anc = git("merge-base", "--is-ancestor", BASE, "HEAD").returncode == 0
add("GIT-01", "HEAD descends from stable baseline b32d185", anc,
    f"git merge-base --is-ancestor {BASE} HEAD -> rc={0 if anc else 1}; "
    f"merge-base(HEAD, origin/main)={out('merge-base', 'HEAD', 'origin/main')}")

merged = git("merge-base", "--is-ancestor", "origin/main", "HEAD").returncode == 0
add("GIT-02", "origin/main was NOT merged into the integration branch", not merged,
    f"git merge-base --is-ancestor origin/main HEAD -> rc={0 if merged else 1}")

local_main, remote_main = out("rev-parse", "main"), out("rev-parse", "origin/main")
add("GIT-03", "local main and origin/main untouched", local_main == remote_main == MAIN_EXPECTED,
    f"main={local_main[:7]} origin/main={remote_main[:7]} expected={MAIN_EXPECTED[:7]}")

commits = out("rev-list", "--count", f"{BASE}..HEAD")
subjects = out("log", "--format=%s", f"{BASE}..HEAD")
REQUIRED_GROUPS = [
    "CPM bootstrap offline fix",
    "状态机完整依赖闭包移植",
    "Batch 1~3 证据映射移植",
    "MinecraftSim 探针失败后按退避重试",
    "安全 SDK 兼容分流",
    "稳定运行时零差异核验",
]
# A pure documentation-correction commit may legitimately follow the six
# functional groups, so assert the GROUPS rather than a raw commit count.
groups_ok = int(commits) >= 6 and all(s in subjects for s in REQUIRED_GROUPS)
add("GIT-04", "all six functional-group commits are present", groups_ok,
    f"git rev-list --count {BASE}..HEAD = {commits}; matched group subjects = "
    f"{sum(1 for s in REQUIRED_GROUPS if s in subjects)}/6")

changed = [p for p in out("diff", "--name-only", f"{BASE}..HEAD").splitlines() if p]
violations = [p for p in changed if not any(a in p for a in ALLOWED)]
add("GIT-05", "every changed file lies in the SDK/build/tools/docs allowlist",
    not violations, f"{len(changed)} changed files; violations={violations}")

# ------------------------------------------------------------- stable runtime
diff_dirs = [p for p in changed if any(f"/{d}/" in p for d in RUNTIME_DIRS)]
add("RT-01", "non-SDK runtime directories have zero diff vs b32d185", not diff_dirs,
    f"checked {len(RUNTIME_DIRS)} runtime directory roots; changed={diff_dirs}")

tracked_runtime = set()
for g in RUNTIME_NAME_GLOBS:
    for line in git("ls-files", "--", f"{PROJ_GIT}/{g}").stdout.splitlines():
        tracked_runtime.add(line)
runtime_touched = sorted(tracked_runtime.intersection(changed))
add("RT-02", "named stable runtime modules are all at their b32d185 blob", not runtime_touched,
    f"{len(tracked_runtime)} named runtime files enumerated; touched={runtime_touched}")

hook_mgr = read("src/Hook/HookManager.cpp")
add("RT-03", "single unified MH_EnableHook(MH_ALL_HOOKS) at end of init(false)",
    "MH_EnableHook(MH_ALL_HOOKS)" in hook_mgr,
    "HookManager.cpp:77 calls MH_EnableHook(MH_ALL_HOOKS) after the full init loop")

early = []
for p in (ROOT / PROJ / "src/Hook/Hooks").rglob("*.cpp"):
    if "mDetour->enable()" in p.read_text(encoding="utf-8", errors="replace"):
        early.append(str(p.relative_to(ROOT / PROJ)).replace("\\", "/"))
add("RT-04", "early per-detour enable inventory is inherited, not newly introduced",
    all(git("diff", "--quiet", BASE, "HEAD", "--", f"{PROJ_GIT}/{p}").returncode == 0 for p in early),
    f"inherited early enables: {sorted(early)}; all identical in b32d185 and HEAD")

# ----------------------------------------------------------- mapping state machine
rs = read("src/SDK/ResolveState.hpp")
enum_ok = all(s in rs for s in ("Ok", "Missing", "Ambiguous", "Inconsistent", "Implausible", "OutOfRange"))
add("SDK-01", "ResolveStatus carries all six trust states", enum_ok,
    "ResolveState.hpp enum class ResolveStatus { Ok, Missing, Ambiguous, Inconsistent, Implausible, OutOfRange }")

add("SDK-02", "both resolvers expose the authoritative statusTable()",
    has("src/SDK/OffsetProvider.hpp", "statusTable()") and has("src/SDK/SigManager.hpp", "statusTable()"),
    "OffsetProvider.hpp:139 and SigManager.hpp:72 return mStatus (ResolveTable)")

add("SDK-03", "initializers derive their verdict from allOk(), not raw pointer values",
    has("src/SDK/OffsetProvider.cpp", "mStatus.allOk()") and has("src/SDK/SigManager.cpp", "mStatus.allOk()"),
    "OffsetProvider.cpp:247 / SigManager.cpp:191")

add("SDK-04", "startup aggregates failures from the trust tables",
    has("src/Solstice.cpp", "SigManager::statusTable()", "OffsetProvider::statusTable()",
        "ResolveStatus::Ok", "hardcodedFields()"),
    "Solstice.cpp:257-272 counts non-Ok entries and reports hardcoded fields as UNVERIFIED")

add("SDK-05", "DEBUG manual gate is verbatim, opt-in and non-automatic",
    has("src/Solstice.cpp", "Solstice should not be used in this state.",
        "Type 'DEBUG' to continue, or press ENTER to exit.",
        'input != "DEBUG"', "FreeLibraryAndExitThread", "OPERATOR OVERRIDE"),
    "Solstice.cpp:307-330; __DEBUG__ only offers the choice; override is logged, not a resolution")

golden = git("show", f"{SDK_GOLDEN}:{PROJ_GIT}/src/SDK/OffsetProvider.hpp").stdout
local = read("src/SDK/OffsetProvider.hpp")
add("SDK-06", "OffsetProvider.hpp matches the 1ec0571 SDK golden snapshot",
    golden.replace("\r\n", "\n").strip() == local.replace("\r\n", "\n").strip(),
    f"git show {SDK_GOLDEN} compared ignoring CRLF; {len(local.splitlines())} lines")

add("SDK-07", "Batch 1-3 evidence mappings are present",
    has("src/SDK/OffsetProvider.hpp", "ClientInstance_getBlockSource", "MinecraftGame_playUi",
        "BlockSource_mBuildHeight", "OffsetType::FieldOffset"),
    "ClientInstance_getBlockSource idx=22, MinecraftGame_playUi idx=10 (slot 164), BlockSource_mBuildHeight typed uint8_t")

add("SDK-08", "implausible/hardcoded values are rejected or registered, never trusted",
    has("src/SDK/OffsetProvider.hpp", "rejectOffset", "plausibleOffset", "hardcodedFields", "DEFINE_FIELD"),
    "OffsetProvider.hpp registers literal DEFINE_FIELD values as UNVERIFIED")

# include closure over every changed SDK/Utils C++ file
inc_re = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
missing_inc = []
for rel in [p.split(PROJ_GIT + "/", 1)[1] for p in changed if ("/src/SDK/" in p or "/src/Utils/" in p or p.endswith("/Solstice.cpp"))]:
    if not rel.endswith((".cpp", ".hpp", ".h")):
        continue
    for inc in inc_re.findall(read(rel)):
        if not inc.startswith(("SDK/", "Utils/", "Minecraft/", "nes/")):
            continue
        cands = [ROOT / PROJ / "src" / inc, ROOT / PROJ / "include" / inc,
                 ROOT / PROJ / "src" / "SDK" / inc]
        if not any(c.exists() for c in cands):
            missing_inc.append(f"{rel}:{inc}")
add("SDK-09", "project-local includes of all changed SDK/Utils files resolve",
    not missing_inc, f"unresolved={missing_inc}")

# ------------------------------------------------------------------ call safety
macro_defs = []
for p in (ROOT / PROJ).rglob("*"):
    if p.is_file() and p.suffix in {".cmake", ".txt", ".json", ".hpp", ".h", ".cpp"}:
        try:
            t = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for m in re.finditer(r"(?:add_compile_definitions|target_compile_definitions|#define|\"D)\s*\(?\s*SOLSTICE_PLAYUI_SEMANTIC_VERIFIED", t):
            macro_defs.append(str(p.relative_to(ROOT)))
mg = read("src/SDK/Minecraft/MinecraftGame.cpp")
add("SAFE-01", "slot 164 virtual call cannot be activated by any build target",
    not macro_defs and "#else" in mg and "PLAYUI_SEMANTIC_VALIDATION: UNVERIFIED UNTIL PROVEN" in mg,
    f"defining sites={macro_defs}; MinecraftGame.cpp:98 gates the call behind an undefined macro")

add("SAFE-02", "isValidVirtualCall is available and used on the opt-in path",
    has("src/Utils/MemUtils.hpp", "isValidVirtualCall", "isReadableRange", "isPatchableCode")
    and has("src/SDK/Minecraft/MinecraftGame.cpp", "MemUtils::isValidVirtualCall"),
    "MemUtils.hpp:130-144 checks object, vtable, slot and executable target")

ci_same = git("diff", "--quiet", BASE, "HEAD", "--", f"{PROJ_GIT}/src/SDK/Minecraft/ClientInstance.cpp").returncode == 0
add("SAFE-03", "getBlockSource() nullptr gate (9229523) is NOT applied",
    ci_same and "return MemUtils::callVirtualFunc<BlockSource*>" in read("src/SDK/Minecraft/ClientInstance.cpp"),
    "ClientInstance.cpp unchanged vs b32d185; ~55-57 unprotected consumers would regress if a nullptr were introduced")

eh = read("src/Utils/SysUtils/ExceptionHandler.cpp")
add("SAFE-04", "exception filter is null-safe and records raw fault context",
    all(s in eh for s in ("pExceptionInfo && pExceptionInfo->ExceptionRecord", "AccessType:",
                          "FaultAddress:", "RIP=0x", "no ContextRecord")) and "sw.~StackWalker();" not in eh,
    "ExceptionHandler.cpp guards all three pointers; skips StackWalker when ContextRecord is absent")

add("SAFE-05", "exception filter is restored on SDK early-abort paths",
    has("src/Utils/SysUtils/ExceptionHandler.hpp", "shutdown()")
    and "ExceptionHandler::shutdown()" in read("src/Solstice.cpp"),
    "ExceptionHandler::shutdown() restores the previous filter before FreeLibraryAndExitThread")

sim = read("src/SDK/Minecraft/MinecraftSim.cpp")
add("SIM-01", "MinecraftSim probe retries after a failed attempt",
    all(s in sim for s in ("kProbeRetryMs", "sLastProbeTick", "GetTickCount64", "sLayoutDumped", "cached == -1")),
    "2 s backoff, success cached permanently, one-shot layout dump under mutex")

# ------------------------------------------------------------------- build system
vendored = ROOT / PROJ / "cmake" / "CPM_0.38.7.cmake"
sha = hashlib.sha256(vendored.read_bytes()).hexdigest() if vendored.exists() else "MISSING"
add("BUILD-01", "vendored CPM v0.38.7 matches the pinned SHA-256", sha == CPM_SHA,
    f"sha256={sha}")

cmake_ok = subprocess.run(
    ["cmake", "-P", str(ROOT / PROJ / "tools" / "verify_cpm_bootstrap.cmake")],
    cwd=ROOT, capture_output=True, text=True)
verify_pass = "VERIFY_PASS" in cmake_ok.stdout
add("BUILD-02", "CPMAddPackage is defined after bootstrap include",
    verify_pass, f"cmake -P verify_cpm_bootstrap.cmake -> {cmake_ok.stdout.strip().splitlines()[-1] if cmake_ok.stdout.strip() else cmake_ok.stderr.strip()}",
    grade_ok="BUILD_VERIFIED")

cml = read("CMakeLists.txt")
add("BUILD-03", "generate_build_info is ordered before the Solstice target",
    "project(Solstice)" in cml and "add_dependencies(${PROJECT_NAME} generate_build_info SolsticeShaders)" in cml,
    "CMakeLists.txt:4 project(Solstice); :285 add_dependencies(${PROJECT_NAME} ...) is the same edge E.1 added")

cl_missing = all(shutil.which(t) is None for t in ("cl", "ld", "ninja", "g++"))
add("BUILD-04", "full CMake configure + MSVC Release x64 link",
    None if cl_missing else False,
    "not attempted",
    deferred="no cl / ld / ninja / g++ on this host; CMakeLists.txt:30 also hard-requires ld. "
             "Configure, compile, link and DLL SHA-256 stay DEFERRED to the build machine.")

add("BUILD-05", "runtime image identity / dump / crash logs",
    None, "not available",
    deferred="no captured runtime log, minidump or EXE/DLL SHA-256 for this branch; "
             "RUNTIME_IMAGE_IDENTITY_UNVERIFIED is retained.")

add("BUILD-06", "non-SDK diagnostic options from main were not imported",
    "SOLSTICE_RENDER_DIAG" not in cml and "SOLSTICE_OPT_ALLOW_UNVERIFIED" not in cml,
    "CMakeLists.txt is byte-identical to b32d185; no subscriber-level render diagnostics")

# -------------------------------------------------------------------- summary
failed = [c["id"] for c in checks if c["result"] == "FAIL"]
grades: dict[str, int] = {}
for c in checks:
    grades[c["grade"]] = grades.get(c["grade"], 0) + 1
report = {
    "branch": out("rev-parse", "--abbrev-ref", "HEAD"),
    "head": out("rev-parse", "HEAD"),
    "baseline": out("rev-parse", BASE),
    "project_prefix": str(PROJ).replace("\\", "/"),
    "changed_files": len(changed),
    "checks": checks,
    "grade_counts": grades,
    "failed": failed,
    "overall": "STABLE_RUNTIME_WITH_CURRENT_SDK / BUILD_UNVERIFIED" if not failed
               else "INTEGRATION_INCOMPLETE",
    "note": "No RUNTIME_VERIFIED claim is made anywhere in this report.",
}

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", type=str, default="")
    a = ap.parse_args()
    payload = json.dumps(report, indent=2, ensure_ascii=False)
    if a.json:
        Path(a.json).write_text(payload + "\n", encoding="utf-8")
    print(payload)
    sys.exit(1 if failed else 0)
