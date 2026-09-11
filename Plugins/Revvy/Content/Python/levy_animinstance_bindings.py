"""
levy_animinstance_bindings.py — AnimInstance -> ABP binding checklist (static, offline).

Parses C++ AnimInstance headers and lists every Blueprint-exposed UPROPERTY (the variables
an Animation Blueprint must read/bind). Pure text parsing — NO `unreal` import — so it runs
outside the editor (CI / quick check). Read-only.

Why: each UAnimInstance subclass computes state in C++ (NativeUpdateAnimation), but the ABP
side must wire those variables/curves by hand per skeleton. Drift across ABP variants is a
known risk (backlog #5). This emits the authoritative per-class checklist to diff against.

Usage:
    python levy_animinstance_bindings.py                      # auto-discover under Source/Oblivio
    python levy_animinstance_bindings.py path/to/Foo.h ...    # explicit headers
    import levy_animinstance_bindings as b; b.run()           # returns dict
"""
import os
import re
import sys
import json

UPROP_RE = re.compile(r"UPROPERTY\s*\((?P<spec>[^)]*)\)", re.IGNORECASE)
# Member decl on a following line: <Type> <Name> [= default] ;
MEMBER_RE = re.compile(
    r"^\s*(?P<type>[A-Za-z_][\w:\*&<>,\s]*?)\s+(?P<name>[A-Za-z_]\w*)\s*(?:=\s*(?P<default>[^;]+?))?\s*;"
)
CATEGORY_RE = re.compile(r"Category\s*=\s*\"([^\"]*)\"")


def _classify(spec):
    s = spec.lower()
    read = "blueprintreadonly" in s or "blueprintreadwrite" in s
    write = "blueprintreadwrite" in s
    editable = "editanywhere" in s or "editdefaultsonly" in s or "editinstanceonly" in s
    thread_safe = "blueprintthreadsafe" in s
    if read:
        access = "ABP read/write" if write else "ABP read"
    elif editable:
        access = "config (editor)"
    else:
        access = "internal"
    return access, read, thread_safe


def parse_header(path):
    cls = {"file": path, "class": None, "props": []}
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except Exception as e:
        cls["error"] = str(e)[:120]
        return cls

    # best-effort class name (first UCLASS-decorated class ... : public UAnimInstance-ish)
    for ln in lines:
        m = re.match(r"\s*class\s+\w*_API\s+(U\w+)", ln) or re.match(r"\s*class\s+(U\w+)\s*:", ln)
        if m:
            cls["class"] = m.group(1)
            break

    i = 0
    n = len(lines)
    while i < n:
        um = UPROP_RE.search(lines[i])
        if not um:
            i += 1
            continue
        spec = um.group("spec")
        cat_m = CATEGORY_RE.search(spec)
        category = cat_m.group(1) if cat_m else ""
        # find the member decl on the next few non-empty, non-comment lines
        j = i + 1
        member = None
        while j < n and j <= i + 4:
            stripped = lines[j].strip()
            if not stripped or stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*"):
                j += 1
                continue
            mm = MEMBER_RE.match(lines[j])
            if mm:
                member = mm
            break
        if member:
            access, is_read, thread_safe = _classify(spec)
            cls["props"].append({
                "name": member.group("name"),
                "type": re.sub(r"\s+", " ", member.group("type")).strip(),
                "category": category,
                "access": access,
                "thread_safe": thread_safe,
                "default": (member.group("default") or "").strip(),
            })
        i = j + 1 if member else i + 1
    return cls


def _discover(root):
    found = []
    for base, _dirs, files in os.walk(root):
        for fn in files:
            if fn.lower().endswith(".h") and "animinstance" in fn.lower():
                found.append(os.path.join(base, fn))
    return sorted(found)


def run(paths=None, source_root=None):
    if not paths:
        root = source_root or os.path.join(os.getcwd(), "Source")
        if not os.path.isdir(root):
            # fall back to a couple levels up (script may live deep in the plugin)
            root = os.getcwd()
        paths = _discover(root)
    out = {"classes": [], "summary": {}}
    for p in paths:
        out["classes"].append(parse_header(p))
    # checklist = Blueprint-readable props only (what the ABP must bind)
    total_bind = 0
    for c in out["classes"]:
        binds = [p for p in c.get("props", []) if p["access"].startswith("ABP")]
        c["bind_count"] = len(binds)
        total_bind += len(binds)
    out["summary"] = {
        "header_count": len(paths),
        "total_abp_bindings": total_bind,
    }
    return out


def _print_report(out):
    for c in out["classes"]:
        print("\n=== {} ({}) ===".format(c.get("class") or "?", os.path.basename(c["file"])))
        if c.get("error"):
            print("  ERROR:", c["error"])
            continue
        by_cat = {}
        for p in c["props"]:
            if not p["access"].startswith("ABP"):
                continue
            by_cat.setdefault(p["category"] or "(none)", []).append(p)
        if not by_cat:
            print("  (no Blueprint-exposed properties found)")
        for cat, props in sorted(by_cat.items()):
            print("  [{}]".format(cat))
            for p in props:
                ts = " (thread-safe)" if p["thread_safe"] else ""
                dv = " = {}".format(p["default"]) if p["default"] else ""
                print("    - {} {}{}{}".format(p["type"], p["name"], dv, ts))
    print("\n--- summary:", json.dumps(out["summary"]), "---")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    result = run(paths=args or None)
    _print_report(result)
