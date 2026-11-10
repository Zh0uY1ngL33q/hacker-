#!/usr/bin/env python3
"""Static acceptance gate for the SwapBuffers-only isolation build.

This host has no MSVC / ld / ninja, so the DLL cannot be compiled here. Instead
this script expands the C preprocessor conditionals of the four touched
translation units for BOTH variants (isolation ON and OFF) and asserts the five
isolation invariants on the code that would actually be compiled.

READ-ONLY. Exit 0 = all invariants hold; 1 = at least one violation.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(subprocess.run(["git", "rev-parse", "--show-toplevel"],
                           capture_output=True, text=True, check=True).stdout.strip())
PROJ = ROOT / "solstice/solstice-src/solstice-master-main/solstice-master-main"

MACROS_ISOLATION = {
    "SOLSTICE_ISOLATION_SWAP_ONLY": 1,
    "SOLSTICE_SWAP_ONLY_ISOLATION": 1,
    "__DEBUG__": 1,
    "__PRIVATE_BUILD__": 1,
    "__PRIVATE_DEBUG__": 1,
    "SOLSTICE_PLAYUI_SEMANTIC_VERIFIED": 0,
}
MACROS_FULL = dict(MACROS_ISOLATION,
                   SOLSTICE_ISOLATION_SWAP_ONLY=0,
                   SOLSTICE_SWAP_ONLY_ISOLATION=0)

FILES = {
    "Solstice.cpp": PROJ / "src/Solstice.cpp",
    "HookManager.cpp": PROJ / "src/Hook/HookManager.cpp",
    "D3DHook.cpp": PROJ / "src/Hook/Hooks/RenderHooks/D3DHook.cpp",
    "ModuleManager.cpp": PROJ / "src/Features/Modules/ModuleManager.cpp",
    "IsolationPolicy.hpp": PROJ / "src/IsolationPolicy.hpp",
    "CMakeLists.txt": PROJ / "CMakeLists.txt",
}


def eval_expr(expr: str, macros: dict[str, int]) -> bool:
    e = expr.strip()
    e = re.sub(r"defined\s*\(\s*(\w+)\s*\)", lambda m: str(int(bool(macros.get(m.group(1), 0)))), e)
    e = re.sub(r"defined\s+(\w+)", lambda m: str(int(bool(macros.get(m.group(1), 0)))), e)
    if "&&" in e:
        return all(eval_expr(part, macros) for part in e.split("&&"))
    if "||" in e:
        return any(eval_expr(part, macros) for part in e.split("||"))
    neg = False
    while e.startswith("!"):
        neg = not neg
        e = e[1:].strip()
    if e.startswith("(") and e.endswith(")"):
        e = e[1:-1].strip()
    val = bool(macros.get(e, 0)) if re.fullmatch(r"\w+", e) else bool(e and e != "0")
    return (not val) if neg else val


def preprocess(text: str, macros: dict[str, int]) -> tuple[str, list[str]]:
    """Return the active source lines and any unbalanced-directive errors."""
    stack: list[dict] = []
    out: list[str] = []
    errors: list[str] = []
    in_block_comment = False
    for no, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        # ModuleManager.cpp contains a commented-out "/*#ifdef ... #endif*/"
        # block; its #endif must not be parsed as a real directive.
        if in_block_comment:
            if "*/" in s:
                in_block_comment = False
            continue
        if "/*" in s and s.startswith("/*"):
            if "*/" not in s:
                in_block_comment = True
                continue
            s = s.split("/*", 1)[0].strip()
            if not s:
                continue
            line = s
        m = re.match(r"#\s*if(n?def)?\s*(.*)$", s)
        if m:
            kind, expr = m.group(1), m.group(2).strip()
            if kind == "ifdef":
                val = bool(macros.get(expr, 0))
            elif kind == "ifndef":
                val = not bool(macros.get(expr, 0))
            else:
                val = eval_expr(expr, macros)
            parent = stack[-1]["active"] if stack else True
            stack.append({"parent": parent, "taken": val, "active": parent and val})
            continue
        if re.match(r"#\s*elif", s):
            if not stack:
                errors.append(f"line {no}: #elif without #if")
                continue
            fr = stack[-1]
            expr = re.sub(r"^#\s*elif\s*", "", s).strip()
            val = eval_expr(expr, macros)
            fr["active"] = fr["parent"] and (not fr["taken"]) and val
            fr["taken"] = fr["taken"] or val
            continue
        if re.match(r"#\s*else\b", s):
            if not stack:
                errors.append(f"line {no}: #else without #if")
                continue
            fr = stack[-1]
            fr["active"] = fr["parent"] and not fr["taken"]
            fr["taken"] = True
            continue
        if re.match(r"#\s*endif\b", s):
            if not stack:
                errors.append(f"line {no}: #endif without #if")
                continue
            stack.pop()
            continue
        if not stack or stack[-1]["active"]:
            out.append(line)
    if stack:
        errors.append(f"{len(stack)} unclosed #if/#ifdef block(s)")
    return "\n".join(out), errors


def expand(macros: dict[str, int]) -> tuple[dict[str, str], list[str]]:
    active, errs = {}, []
    for name, path in FILES.items():
        if name in ("IsolationPolicy.hpp", "CMakeLists.txt"):
            continue
        src, e = preprocess(path.read_text(encoding="utf-8", errors="replace"), macros)
        active[name] = src
        errs += [f"{name}: {x}" for x in e]
    return active, errs


checks: list[dict] = []


def check(cid: str, title: str, ok: bool, evidence: str) -> None:
    checks.append({"id": cid, "title": title, "result": "PASS" if ok else "FAIL",
                   "evidence": evidence})


def code_only(src: str) -> str:
    """Drop // comments and #include lines so assertions test real statements."""
    lines = []
    for l in src.splitlines():
        l = l.split("//", 1)[0]
        if l.strip().startswith("#include"):
            continue
        lines.append(l)
    return "\n".join(lines)


iso, iso_errs = expand(MACROS_ISOLATION)
full, full_errs = expand(MACROS_FULL)

check("PP-01", "preprocessor conditionals are balanced in both variants",
      not iso_errs and not full_errs, f"isolation={iso_errs} full={full_errs}")

cm = FILES["CMakeLists.txt"].read_text(encoding="utf-8", errors="replace")
check("BUILD-01", "isolation option exists and defaults ON",
      bool(re.search(r'option\(SOLSTICE_SWAP_ONLY_ISOLATION[\s\S]{0,220}?\bON\)', cm))
      and "add_compile_definitions(SOLSTICE_SWAP_ONLY_ISOLATION)" in cm,
      "CMakeLists.txt: option(... ON) + add_compile_definitions")

pol = FILES["IsolationPolicy.hpp"].read_text(encoding="utf-8", errors="replace")
check("BUILD-02", "single centralized switch header defines the policy",
      "SOLSTICE_ISOLATION_SWAP_ONLY 1" in pol and "SOLSTICE_ISOLATION_SWAP_ONLY 0" in pol
      and all("#include <IsolationPolicy.hpp>" in FILES[f].read_text(encoding="utf-8", errors="replace")
              for f in ("Solstice.cpp", "HookManager.cpp", "D3DHook.cpp", "ModuleManager.cpp")),
      "IsolationPolicy.hpp included by all four touched translation units")

hm = iso["HookManager.cpp"]
check("ISO-01", "only D3DHook is installed in the isolation build",
      "ADD_HOOK(D3DHook);" in hm
      and not any(f"ADD_HOOK({h});" in hm for h in
                  ("KeyHook", "PacketReceiveHook", "PacketSendHook", "BaseTickHook",
                   "SetupAndRenderHook", "RakPeerHooks", "AnimationHooks",
                   "RenderItemInHandHook", "NametagRenderHook")),
      "HookManager::init(false) retains one ADD_HOOK; PacketReceiveHook never constructed")
check("ISO-02", "219 packet-handler detours cannot be created",
      "ADD_HOOK(PacketReceiveHook);" not in hm and "PacketReceiveHook::init" not in hm
      and "0x136" not in hm,
      "HookManager never constructs PacketReceiveHook, so its 0x136-id loop "
      "(the 219 handler detours) is unreachable")
check("ISO-03", "dynamic BaseTick hook is suspended",
      "initLp skipped: dynamic Actor::baseTick hook suspended" in hm
      and "ADD_HOOK(BaseTickHook);" not in hm,
      "HookManager::init(true) returns immediately")
check("ISO-04", "unified MH_EnableHook(MH_ALL_HOOKS) is preserved",
      "MH_EnableHook(MH_ALL_HOOKS)" in hm and "mDetour->enable()" not in hm,
      "single enable after the loop; no early per-detour enable")

dh = iso["D3DHook.cpp"]
check("ISO-05", "both SwapBuffers detours are still created",
      "installGlSwapHooks();" in dh and "OpenGL gdi32!SwapBuffers" in dh
      and "OpenGL opengl32!wglSwapBuffers" in dh,
      "D3DHook::installGlSwapHooks() creates both native detours")
check("ISO-06", "SwapBuffers callbacks call the original and return, no render",
      "OpenGLGui::render(dc)" not in dh
      and "return gdiSwapDetour->getOriginal<&onGdiSwapBuffers>()(dc);" in dh
      and "return wglSwapDetour->getOriginal<&onWglSwapBuffers>()(dc);" in dh,
      "onGdiSwapBuffers / onWglSwapBuffers are one-line passthroughs")
check("ISO-07", "RenderEvent is not dispatched anywhere in the active render path",
      "GL HUD dispatch" not in dh and "trigger(holder)" not in dh
      and "make_holder<RenderEvent>" not in dh,
      "the whole D3D present body is compiled out and both swap callbacks are passthroughs")
check("ISO-08", "kiero D3D12/D3D11 fallback RVA bindings are suspended",
      "kiero::init" not in dh and "kiero::bind(140" not in dh
      and "kiero::unbind(8)" not in dh and "Failed to initialize kiero" not in dh,
      "s_init/s_shutdown skip all kiero init, bind, unbind and shutdown calls")
check("ISO-09", "ClickGUI/HUD shutdown state is untouched",
      "OpenGLGui::shutdown();" not in dh,
      "s_shutdown does not tear down a GUI that was never initialized")

mm = iso["ModuleManager.cpp"]
init = mm.split("void ModuleManager::init()", 1)[-1].split("void ModuleManager::shutdown()", 1)[0]
body = code_only(init)
first_reg = body.find("mModules.emplace_back")
early_return = body.find("return;")
check("ISO-10", "no visual modules (ClickGUI/HUD/Visual) are constructed",
      "registers 0 modules" in body and early_return != -1
      and (first_reg == -1 or early_return < first_reg),
      f"ModuleManager::init() returns at offset {early_return} before the first "
      f"module registration at offset {first_reg}")

so = iso["Solstice.cpp"]
soc = code_only(so)
check("ISO-11", "no manual console waits in the isolation build",
      "std::getline(std::cin" not in soc and "Press ENTER to continue" not in soc
      and '[isolation]' in soc,
      "DEBUG gate and second ENTER wait skipped; state still logged")
check("ISO-12", "no external update-check / IRC / persisted-config side effects in the loop",
      "OAuthUtils::getLatestCommitHash()" not in soc
      and "ConfigManager::loadConfig" not in soc
      and "getModule<IRC>()" not in soc
      and "gFeatureManager->mModuleManager->onClientTick()" not in soc,
      "shutdownThread uses the minimal END-polling loop")
check("ISO-13", "signature bytepatch and playUi SDK calls are suspended",
      "patchInHandSlot(false)" not in soc and 'playUi("beacon.activate"' not in soc
      and 'playUi("beacon.deactivate"' not in soc,
      "no RVA-derived bytepatch, no MinecraftGame virtual call")
check("ISO-14", "persisted EnforceDebugging cannot spin the isolation build",
      "Prefs->mEnforceDebugging" not in soc and "IsDebuggerPresent()" not in soc
      and "Please attach a debugger" not in soc,
      "the debugger-wait MessageBox loop is compiled out in this variant")

fh = full["HookManager.cpp"]
fd = full["D3DHook.cpp"]
fm = full["ModuleManager.cpp"]
fs = full["Solstice.cpp"]
check("AB-01", "turning the switch OFF restores the complete client",
      all(x in fh for x in ("ADD_HOOK(PacketReceiveHook);", "ADD_HOOK(BaseTickHook);",
                            "ADD_HOOK(KeyHook);"))
      and "OpenGLGui::render(dc)" in fd and "kiero::init" in fd
      and "make_shared<HudEditor>" in fm and "std::getline(std::cin" in fs
      and "OAuthUtils::getLatestCommitHash()" in fs,
      "same source tree, -DSOLSTICE_SWAP_ONLY_ISOLATION=OFF yields the full baseline")

failed = [c["id"] for c in checks if c["result"] == "FAIL"]
report = {
    "variant": "SOLSTICE_SWAP_ONLY_ISOLATION=ON",
    "checks": checks,
    "failed": failed,
    "overall": "SWAP_ONLY_ISOLATION_STATICALLY_CONFIRMED" if not failed else "ISOLATION_INCOMPLETE",
    "build_verified": False,
    "note": "No compiler is available on this host; this is source-level verification only.",
}

if __name__ == "__main__":
    print(json.dumps(report, indent=2, ensure_ascii=False))
    sys.exit(1 if failed else 0)
