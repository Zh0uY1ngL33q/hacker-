#!/usr/bin/env python3
"""3.6.2-F workgroup 3: static per-callback audit for RenderEvent subscribers.

Run AFTER batch3_6_2_f_gen.py. Enriches render-subscriber-inventory.json with:

  * configOverrideMechanism - the startup config path that can overwrite the
    constructor default enabled state (Solstice.cpp -> ConfigManager ->
    ModuleManager::deserialize -> onClientTick -> onEnable).
  * callbackAudit - a KEYWORD-LEVEL static scan of each onRenderEvent body for
    the constructs listed in the batch spec (SDK getters, unverified mappings,
    file/network IO, sleep/wait, mutex, GL, ImGui Begin/End, recursive events,
    entity enumeration, unbounded loops, thread synchronisation).

IMPORTANT: this is a lexical scan, NOT semantic analysis and NOT runtime
evidence. A hit means "this construct appears in the callback source"; it does
NOT mean the code executes, is dangerous, or is the fault. runtimeObserved
stays false everywhere. No danger ranking is inferred from code length or hits.
"""
import os
import re
import json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INV = os.path.join(ROOT, "docs", "batch3_6_2_f", "render-subscriber-inventory.json")

# name -> regex. Deliberately lexical; see module docstring.
AUDIT_PATTERNS = {
    "sdkGetter":          r"ClientInstance::get|getLocalPlayer|getViewMatrix|getFov|LevelRenderer|getCameraPos|Actor::|getLevel\(",
    "unverifiedMapping":  r"OffsetProvider::|SigManager::|\b0x[0-9A-Fa-f]{2,}\b",
    "fileIO":             r"\b(?:std::ifstream|std::ofstream|std::fstream|fopen|CreateFileA?|WriteFile|ReadFile|ofstream|ifstream)\b",
    "networkIO":          r"\b(?:WinHttp|curl|HttpClient|socket|send\(|recv\(|InternetOpen|IRC)\b",
    "sleepWait":          r"\b(?:Sleep|sleep_for|sleep_until|WaitForSingleObject|WaitForMultipleObjects)\b|\.wait\s*\(",
    "mutex":              r"\b(?:std::mutex|recursive_mutex|lock_guard|unique_lock|shared_lock|scoped_lock|EnterCriticalSection)\b",
    "glCalls":            r"\b(?:gl[A-Z]\w*|wgl\w*)\s*\(",
    "imguiBeginEnd":      r"ImGui::(?:Begin|End|BeginChild|EndChild)\b",
    "recursiveEvent":     r"mDispatcher->(?:trigger|listen|deafen)|\.trigger\s*\(",
    "entityEnumeration":  r"\b(?:EntityList|getEntities|forEach|mEntities|getActorList|iterateEntities|mEntityList)\b",
    "unboundedLoop":      r"while\s*\(\s*true\s*\)|for\s*\(\s*;\s*;\s*\)",
    "threadSync":         r"\b(?:std::thread|std::atomic|condition_variable|std::async|CreateThread|std::jthread)\b",
    "nullDerefCandidate": r"ClientInstance::get\(\)->|getLocalPlayer\(\)->|\bget\(\)->",
}

CONFIG_MECHANISM = {
    "startupAutoLoad": "src/Solstice.cpp:454-458",
    "chain": [
        "Solstice.cpp: if (!Prefs->mDefaultConfigName.empty() && ConfigManager::configExists(...)) ConfigManager::loadConfig(name)",
        "ConfigManager::loadConfig -> gFeatureManager->mModuleManager->deserialize(j)  (src/Features/Configs/ConfigManager.cpp:25-38)",
        "ModuleManager::deserialize: mod->mWantedState = enabled  (src/Features/Modules/ModuleManager.cpp:505-515)",
        "ModuleManager::onClientTick: if (mWantedState != mEnabled) setEnabled(...) -> onEnable()  (ModuleManager.cpp:431-435)",
    ],
    "affectsRenderSubscription": True,
    "reason": ("Most RenderEvent listeners are registered inside onEnable(), not the "
               "constructor. The config-determined enabled set therefore decides which "
               "subscribers exist at all on the first frame, and hence the real dispatch order."),
    "runtimeConfigLocation": "%APPDATA%\\Solstice\\Configs\\<DefaultConfigName>.json (on the game machine)",
    "staticStatus": "RUNTIME_CONFIG_NOT_AVAILABLE",
    "detail": ("The runtime config is not in this repository. defaultEnabled in the "
               "inventory is the CONSTRUCTOR value only; whether it was overwritten "
               "must be read from the game machine's config or observed via the "
               "SOLSTICE_RENDER_DIAG log. It is never inferred here."),
}


def sources_for(entry):
    """All header/implementation siblings of the module, regardless of which
    file the listen<> call was found in (they are often different files)."""
    p = os.path.join(ROOT, entry["file"])
    stem = os.path.splitext(p)[0]
    out = []
    for cand in (p, stem + ".cpp", stem + ".hpp", stem + ".h"):
        if os.path.isfile(cand) and cand not in out:
            out.append(cand)
    return out


def match_brace(text, start):
    """Index of the '}' matching the '{' at `start`, skipping comments and
    string/char literals so braces inside them cannot unbalance the count."""
    depth, i, n = 0, start, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            if j < 0:
                return -1
            i = j + 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            if j < 0:
                return -1
            i = j + 2
            continue
        if c in ('"', "'"):
            q, i = c, i + 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


# Definition only: `void [Class::]onRenderEvent(args) {`. Declarations end in
# ';' and pointer-to-member references end in '>' or ',' so neither matches.
DEF_RE = re.compile(r"\b(?:void|auto)\s+(?:\w+::)?onRenderEvent\s*\([^;{}]*\)\s*\{")


def extract_bodies(text, func="onRenderEvent"):
    bodies = []
    for m in DEF_RE.finditer(text):
        brace = text.rfind("{", m.start(), m.end())
        if brace < 0:
            continue
        end = match_brace(text, brace)
        if end > brace:
            bodies.append(text[brace:end + 1])
    return "\n".join(bodies)


def audit(entry):
    paths = sources_for(entry)
    # Scan each file independently: concatenating files lets a brace in one file
    # pair with one in the next and silently swallow the whole translation unit.
    body = "\n".join(
        extract_bodies(open(p, encoding="utf-8", errors="ignore").read())
        for p in paths
    )
    if not body:
        return {
            "sourceFilesScanned": [os.path.relpath(p, ROOT) for p in paths],
            "bodyFound": False,
            "note": "onRenderEvent body could not be isolated lexically; manual read required.",
        }
    hits = {k: bool(re.search(pat, body)) for k, pat in AUDIT_PATTERNS.items()}
    return {
        "sourceFilesScanned": [os.path.relpath(p, ROOT) for p in paths],
        "bodyFound": True,
        "bodyChars": len(body),
        "constructsPresent": hits,
        "note": ("Lexical keyword scan only. A hit is NOT a runtime observation and "
                 "NOT a danger verdict; no ranking is derived from hit count or body length. "
                 "Null-pointer / object-lifetime safety requires manual semantic review."),
    }


def main():
    with open(INV, encoding="utf-8") as f:
        doc = json.load(f)

    doc["configOverrideMechanism"] = CONFIG_MECHANISM
    doc["auditMethod"] = {
        "type": "STATIC_LEXICAL_SCAN",
        "tool": "tools/batch3_6_2_f_audit.py",
        "generatedFrom": "tools/batch3_6_2_f_gen.py",
        "runtimeEvidence": False,
    }
    for entry in doc["inventory"]:
        entry["configOverrideApplies"] = True
        entry["runtimeConfigObserved"] = False
        entry["callbackAudit"] = audit(entry)
        entry["runtimeObserved"] = False

    with open(INV, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2, ensure_ascii=False)

    found = sum(1 for e in doc["inventory"] if e["callbackAudit"].get("bodyFound"))
    print(f"audited {len(doc['inventory'])} subscribers; bodies isolated: {found}")
    # aggregate, purely descriptive (no ranking)
    from collections import Counter
    agg = Counter()
    for e in doc["inventory"]:
        for k, v in e["callbackAudit"].get("constructsPresent", {}).items():
            if v:
                agg[k] += 1
    print("construct presence (module counts, descriptive only):")
    for k, v in agg.most_common():
        print(f"  {k:<22} {v}")


if __name__ == "__main__":
    main()
