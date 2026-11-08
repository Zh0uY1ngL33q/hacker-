#!/usr/bin/env python3
"""Emit mapping-risk-for-render-subscribers.json for 3.6.2-F.

Combines the static RenderEvent subscriber roster with a curated SDK-getter risk
table. runtimeObserved is always false; precise per-consumer verification requires
reading each callback (and the SOLSTICE_RENDER_DIAG run).
"""
import os, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
inv_path = os.path.join(ROOT, "docs", "batch3_6_2_f", "render-subscriber-inventory.json")
out_path = os.path.join(ROOT, "docs", "batch3_6_2_f", "mapping-risk-for-render-subscribers.json")

inv = json.load(open(inv_path, encoding="utf-8"))
subs = inv["inventory"]

# Curated SDK-getter families actually consumed on the GL RenderEvent path.
getter_families = [
    {
        "family": "ClientInstance::get (singleton)",
        "status": "RUNTIME_CACHED",
        "note": "Cached pointer via InstanceLocator; not an OffsetProvider mapping. "
                "Some call sites deref without a null check "
                "(Interface::onRenderEvent:205 dereferences ClientInstance::get()->getLocalPlayer()).",
        "consumers": ["Interface", "Watermark", "Arraylist", "LevelInfo", "Notifications",
                      "SessionInfo", "Keystrokes", "TargetHUD", "ClickGui", "HudEditor",
                      "FullBright", "AutoScale", "UpdateForm", "CustomChat", "Regen",
                      "MotionBlur", "DestroyProgress", "Zoom", "ESP", "Tracers", "Nametags",
                      "ChinaHat", "ItemESP", "JumpCircles", "ItemPhysics", "BoneEsp",
                      "BlockESP", "Aura", "AutoClicker", "InfiniteAura", "InventoryMove",
                      "TargetStrafe", "Jesus", "ChestStealer", "Scaffold", "Nuker",
                      "OreMiner", "TestModule", "AutoPath"],
    },
    {
        "family": "ClientInstance::getLocalPlayer",
        "offset": "ClientInstance_getLocalPlayer",
        "status": "OK_AT_RUNTIME_FOR_INTERFACE",
        "note": "Interface::onRenderEvent executed and returned this run, so the offset "
                "resolved to a usable pointer on the GL path. Not globally guaranteed; "
                "other threads/times may differ.",
        "consumers": ["Interface", "ESP", "Tracers", "Nametags", "ChinaHat", "ItemESP",
                      "Watermark", "Arraylist", "LevelInfo", "TargetHUD", "ClickGui",
                      "SessionInfo", "Keystrokes", "AutoScale", "Aura", "AutoClicker",
                      "InfiniteAura", "InventoryMove", "TargetStrafe", "Jesus",
                      "ChestStealer", "Scaffold", "Nuker", "OreMiner", "AutoPath"],
    },
    {
        "family": "ClientInstance::getViewMatrix / ClientInstance+0x358",
        "status": "HARDCODED_UNVERIFIED",
        "note": "OpenGLGui reads ClientInstance+0x358 directly (hardcoded offset) and only "
                "uses it when it is a true camera basis; otherwise it falls back to a "
                "rotation-derived matrix. This DECOUPLES the ESP-family projection from the "
                "raw view-matrix offset on the GL path, so a bad/unverified matrix offset "
                "does not by itself crash ESP/Nametags/Tracers here.",
        "consumers": ["ESP", "Tracers", "Nametags", "ChinaHat", "ItemESP", "JumpCircles"],
    },
    {
        "family": "ClientInstance::getFov",
        "status": "VERIFY",
        "note": "Used to fill RenderUtils::transform.mFov on the GL path. Verify the fov "
                "offset resolves; the path tolerates a zero fov (uses rotation matrix).",
        "consumers": ["ESP", "Tracers", "Nametags", "ChinaHat", "ItemESP", "JumpCircles",
                      "Watermark", "Arraylist", "LevelInfo", "Keystrokes"],
    },
    {
        "family": "LevelRenderer::getRendererPlayer / getCameraPos",
        "status": "VERIFY",
        "note": "Camera origin for world-to-screen. Null-guarded on the GL path "
                "(OpenGLGui:425-515).",
        "consumers": ["ESP", "Tracers", "Nametags", "ChinaHat", "ItemESP", "JumpCircles"],
    },
    {
        "family": "Actor / Entity enumeration (EntityList)",
        "status": "HIGH_RISK_IF_OFFSET_BAD",
        "note": "ESP, Nametags, Tracers, ChinaHat, ItemESP iterate the entity list. A bad "
                "iteration offset can crash or infinite-loop. This is the highest-risk "
                "mapping dependency for the Visual combat/ESP family on the first frame.",
        "consumers": ["ESP", "Tracers", "Nametags", "ChinaHat", "ItemESP", "ItemPhysics",
                      "BoneEsp"],
    },
    {
        "family": "Block enumeration (BlockSource / Level)",
        "status": "VERIFY",
        "note": "BlockESP iterates blocks; null/offset issues can crash.",
        "consumers": ["BlockESP"],
    },
    {
        "family": "OffsetProvider::ResourcePackManager_composeFullStackBp",
        "status": "MISSING_SIGNATURE_BENIGN",
        "note": "Interface::patchFullStack target; signature unresolved (addr==0) -> safe "
                "early return. Unrelated to the hang (see patchfullstack-callchain.md).",
        "consumers": ["Interface"],
    },
]

# Best-effort getter families per subscriber (by module behaviour on the GL path).
def families_for(name):
    esp_family = ["Actor / Entity enumeration (EntityList)",
                  "ClientInstance::getViewMatrix / ClientInstance+0x358",
                  "ClientInstance::getFov",
                  "LevelRenderer::getRendererPlayer / getCameraPos",
                  "ClientInstance::getLocalPlayer", "ClientInstance::get (singleton)"]
    gui_family = ["ClientInstance::get (singleton)", "ClientInstance::getLocalPlayer"]
    if name in ("ESP", "Tracers", "Nametags", "ChinaHat", "ItemESP", "JumpCircles",
                "ItemPhysics", "BoneEsp"):
        return esp_family
    if name in ("BlockESP",):
        return ["Block enumeration (BlockSource / Level)", "ClientInstance::get (singleton)",
                "ClientInstance::getLocalPlayer"]
    if name in ("Watermark", "Arraylist", "LevelInfo", "Notifications", "SessionInfo",
                "Keystrokes", "TargetHUD", "ClickGui", "HudEditor", "FullBright",
                "AutoScale", "UpdateForm", "CustomChat", "Regen", "MotionBlur",
                "DestroyProgress", "Zoom", "Interface"):
        return gui_family
    # Combat / Movement / Player modules: typically read local player + level for
    # overlays/aim assists; treat as local-player + singleton.
    return ["ClientInstance::get (singleton)", "ClientInstance::getLocalPlayer"]

risk_subs = []
for s in subs:
    risk_subs.append({
        "name": s["name"],
        "priority": s["priority"],
        "dispatchOrder": s["dispatchOrder"],
        "registrationSite": s["registrationSite"],
        "defaultEnabled": s["defaultEnabled"],
        "getterFamilies": families_for(s["name"]),
        "runtimeObserved": False,
    })

doc = {
    "event": "RenderEvent",
    "scope": ("Only mappings consumed by subscribers actually executed on the GL first "
              "frame. 73 mappings are unusable overall; this file isolates which are "
              "touched by RenderEvent subscribers and whether they are GL-path-decoupled."),
    "unusableMappingSummary": {
        "offsets": {"OK": 26, "MISSING": 24, "AMBIGUOUS": 6, "IMPLAUSIBLE": 1, "total": 57},
        "signatures": {"OK": 73, "MISSING": 20, "AMBIGUOUS": 21, "OUT_OF_RANGE": 1, "total": 115},
        "combinedUnusable": 73,
        "trustworthiness": "NOT_TRUSTWORTHY",
    },
    "rules": [
        "Status vocabulary: OK / MISSING / AMBIGUOUS / IMPLAUSIBLE / OUT_OF_RANGE / HARDCODED_UNVERIFIED.",
        "Do NOT assume a module crashes just because a mapping is untrusted; confirm the "
        "specific consumer actually reads that mapping on the GL path.",
        "GL path reconstructs the view matrix from camera rotation + camera pos, so the "
        "ESP-family projection is decoupled from the raw view-matrix offset; a bad matrix "
        "offset alone does not crash those modules here.",
        "Entity-list enumeration remains the highest-risk mapping dependency for ESP-family "
        "subscribers on the first frame.",
        "runtimeObserved is false; the SOLSTICE_RENDER_DIAG run + per-callback source read "
        "resolve per-consumer status.",
    ],
    "getterFamilies": getter_families,
    "subscribers": risk_subs,
}

with open(out_path, "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2, ensure_ascii=False)
print("wrote", out_path, "with", len(risk_subs), "subscribers and", len(getter_families), "getter families")
