#!/usr/bin/env python3
"""Generate 3.6.2-F static RenderEvent subscriber roster + mapping-risk skeletons.

This parses source only; it does NOT run the game. runtimeObserved is always
false and the exact runtime dispatchOrder must be confirmed by the SOLSTICE_RENDER_DIAG
instrumentation described in subscriber-instrumentation.md.
"""
import os, re, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULES_DIR = os.path.join(ROOT, "src", "Features", "Modules")
MGR_CPP = os.path.join(MODULES_DIR, "ModuleManager.cpp")

PRIORITY = {
    "ABSOLUTE_FIRST": 0, "VERY_FIRST": 1, "FIRST": 2, "KINDA_FIRST": 3,
    "NORMAL": 4, "KINDA_LAST": 5, "LAST": 6, "VERY_LAST": 7, "ABSOLUTE_LAST": 8,
}
PRIORITY_NAME = {v: k for k, v in PRIORITY.items()}


def split_top_commas(s):
    out, depth, buf = [], 0, ""
    for ch in s:
        if ch in "<(":
            depth += 1; buf += ch
        elif ch in ">)":
            depth -= 1; buf += ch
        elif ch == "," and depth == 0:
            out.append(buf.strip()); buf = ""
        else:
            buf += ch
    if buf.strip():
        out.append(buf.strip())
    return out


def parse_construction_order():
    order = []
    with open(MGR_CPP, encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = re.search(r"make_shared<(\w+)>\(\)", line)
            if m:
                order.append(m.group(1))
    return order


def find_listens():
    """Return list of dicts: class, file, priority(name), priority_num, func."""
    res = []
    for dirpath, _, files in os.walk(MODULES_DIR):
        for fn in files:
            if not fn.endswith((".cpp", ".hpp")):
                continue
            path = os.path.join(dirpath, fn)
            with open(path, encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
            # enclosing-function tracking (best effort)
            cur_func = None
            for i, line in enumerate(lines):
                fm = re.search(r"^\s*[\w:<>\*&]+\s+(\w+)::(\w+)\s*\(", line)
                if fm:
                    cur_func = (fm.group(1), fm.group(2))
                lm = re.search(r"listen\s*<\s*RenderEvent[^>]*,\s*&(\w+)::onRenderEvent\s*(?:,\s*nes::event_priority::(\w+))?\s*>", line)
                if lm:
                    cls = lm.group(1)
                    pri = lm.group(2) or "NORMAL"
                    func = cur_func[1] if cur_func else "inline/ctor"
                    res.append({
                        "class": cls,
                        "file": os.path.relpath(path, ROOT),
                        "priority": pri,
                        "priority_num": PRIORITY.get(pri, 4),
                        "func": func,
                    })
    return res


def default_enabled(cls):
    # Search both .hpp and .cpp for the constructor and its Module/ModuleBase args.
    for ext in (".hpp", ".cpp"):
        path = None
        for dirpath, _, files in os.walk(MODULES_DIR):
            cand = os.path.join(dirpath, cls + ext)
            if os.path.exists(cand):
                path = cand
                break
        if not path:
            continue
        with open(path, encoding="utf-8", errors="ignore") as f:
            text = f.read()
        # constructor base-init or inline ctor calling ModuleBase(...)
        for m in re.finditer(r"ModuleBase<\s*%s\s*>\s*\(\s*(.*?)\)" % re.escape(cls), text, re.S):
            args = split_top_commas(m.group(1))
            if len(args) >= 5:
                val = args[4].strip().lower()
                if val in ("true", "false"):
                    return val == "true"
        # fallback: Module( "Name", "...", CAT, key, enabled )
        for m in re.finditer(r"\bModule\s*\(\s*(.*?)\)", text, re.S):
            args = split_top_commas(m.group(1))
            if len(args) >= 5 and args[0].startswith('"'):
                val = args[4].strip().lower()
                if val in ("true", "false"):
                    return val == "true"
    return None  # verify


def main():
    order = parse_construction_order()
    order_index = {c: i for i, c in enumerate(order)}
    listens = find_listens()

    # de-dup by class keeping first (constructor) occurrence; note multi-registration
    seen = {}
    for L in listens:
        seen.setdefault(L["class"], L)
    subscribers = []
    for cls, L in seen.items():
        reg_order = order_index.get(cls)
        enabled = default_enabled(cls)
        subscribers.append({
            "name": cls,
            "file": L["file"],
            "callback": "onRenderEvent",
            "priority": L["priority"],
            "priority_num": L["priority_num"],
            "registrationSite": ("onEnable" if L["func"] == "onEnable"
                                 else "onInit" if L["func"] == "onInit"
                                 else "constructor"),
            "constructed": reg_order is not None,
            "registrationOrder": reg_order if reg_order is not None else -1,
            "defaultEnabled": enabled if enabled is not None else "verify",
            "runtimeObserved": False,
        })
    # dispatch order = priority tier, then construction (registration) order
    subscribers.sort(key=lambda s: (s["priority_num"], s["registrationOrder"] if s["registrationOrder"] >= 0 else 10**9))
    for i, s in enumerate(subscribers):
        s["dispatchOrder"] = i

    order_doc = {
        "event": "RenderEvent",
        "note": ("Static dispatch order derived from the EventPriorities enum tiers "
                 "(src/pch.hpp) and ModuleManager::init() construction order. runtimeObserved "
                 "is false; the authoritative per-run sequence is produced by the "
                 "SOLSTICE_RENDER_DIAG instrumentation (subscriber-instrumentation.md)."),
        "priorityTiers": PRIORITY_NAME,
        "subscribers": [
            {
                "name": s["name"],
                "registrationOrder": s["registrationOrder"],
                "dispatchOrder": s["dispatchOrder"],
                "constructed": s["constructed"],
                "enabled": s["defaultEnabled"],
                "callback": s["callback"],
                "runtimeObserved": False,
            } for s in subscribers
        ],
    }
    inv_doc = {
        "event": "RenderEvent",
        "inventory": subscribers,
    }
    out_dir = os.path.join(ROOT, "docs", "batch3_6_2_f")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "render-subscriber-order.json"), "w", encoding="utf-8") as f:
        json.dump(order_doc, f, indent=2, ensure_ascii=False)
    with open(os.path.join(out_dir, "render-subscriber-inventory.json"), "w", encoding="utf-8") as f:
        json.dump(inv_doc, f, indent=2, ensure_ascii=False)
    print("subscribers found:", len(subscribers))
    print("construction order length:", len(order))
    # quick sanity: list priority tiers
    from collections import defaultdict
    tiers = defaultdict(list)
    for s in subscribers:
        tiers[s["priority"]].append(s["name"])
    for p in sorted(tiers, key=lambda k: PRIORITY[k]):
        print(f"  [{p}] ({len(tiers[p])}):", ", ".join(tiers[p]))


if __name__ == "__main__":
    main()
