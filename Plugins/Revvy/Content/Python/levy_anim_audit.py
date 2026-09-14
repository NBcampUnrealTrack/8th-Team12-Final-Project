"""
levy_anim_audit.py — read-only AnimBlueprint / Montage validation audit (DRAFT).

Run INSIDE the Unreal Editor (Revvy/Levy python). READ-ONLY: never modifies assets.
Reports per AnimBlueprint: AnimGraph node inventory, Montage Slot names, MotionMatching
database references, Linked Anim Layer interfaces, state machines — and flags likely
duplicate ABP variants (drift risk).

Why a draft: AnimGraph introspection uses get_graph_nodes_of_class (read-only) — the
`unreal` reflection surface here is finicky and version-sensitive, so EVERY operation is
wrapped in try/except and the report records what succeeded vs. failed. Validate in-editor;
adjust class/property names against this engine build if a section reports errors.

Engine: UE 5.7 (team version — do NOT assume 5.8 APIs).
Usage:  import levy_anim_audit; levy_anim_audit.run()
        levy_anim_audit.run(paths=["/Game/Character/Player/ABP_Doctor"])  # specific
"""
import unreal, json, re, os


# AnimGraph node classes to scan. Missing classes in this engine build are skipped.
def _node_classes():
    names = [
        "AnimGraphNode_Slot",
        "AnimGraphNode_StateMachine",
        "AnimGraphNode_MotionMatching",
        "AnimGraphNode_LinkedAnimLayer",
        "AnimGraphNode_LinkedAnimGraph",
        "AnimGraphNode_SequencePlayer",
        "AnimGraphNode_BlendSpacePlayer",
    ]
    out = {}
    for n in names:
        cls = getattr(unreal, n, None)
        if cls is not None:
            out[n] = cls
    return out


def _list_anim_blueprints():
    """Return ([object_path,...], error_or_None) for all AnimBlueprints under /Game."""
    try:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        assets = None
        # Preferred: ARFilter (class_paths in 5.x; fall back to class_names).
        try:
            f = unreal.ARFilter(
                class_names=["AnimBlueprint"],
                recursive_paths=True,
                package_paths=["/Game"],
            )
            assets = ar.get_assets(f)
        except Exception:
            assets = ar.get_assets_by_class("AnimBlueprint", True)
        paths = []
        for a in assets:
            try:
                paths.append(str(a.package_name) + "." + str(a.asset_name))
            except Exception:
                pass
        return paths, None
    except Exception as e:
        return [], "AssetRegistry error: " + str(e)[:140]


def _audit_abp(obj_path):
    rec = {
        "abp": obj_path,
        "nodes": {},
        "slots": [],
        "mm_databases": [],
        "layer_interfaces": [],
        "errors": [],
    }
    classes = _node_classes()

    # Locate the AnimGraph object: "<obj_path>:AnimGraph" (proven path in .revvy_introspect.py)
    graph = None
    for gname in ("AnimGraph",):
        try:
            g = unreal.load_object(None, obj_path + ":" + gname)
            if g and hasattr(g, "get_graph_nodes_of_class"):
                graph = g
                break
        except Exception:
            pass
    if graph is None:
        rec["errors"].append("AnimGraph not found / no get_graph_nodes_of_class")
        return rec

    for cname, cls in classes.items():
        try:
            nodes = graph.get_graph_nodes_of_class(cls)
            rec["nodes"][cname] = len(nodes)
            for nd in nodes:
                try:
                    ns = nd.get_editor_property("node")
                except Exception:
                    ns = None
                if ns is None:
                    continue
                if cname == "AnimGraphNode_Slot":
                    try:
                        rec["slots"].append(str(ns.get_editor_property("slot_name")))
                    except Exception:
                        pass
                elif cname == "AnimGraphNode_MotionMatching":
                    try:
                        db = ns.get_editor_property("database")
                        if db:
                            rec["mm_databases"].append(str(db.get_name()))
                    except Exception:
                        pass
                elif cname == "AnimGraphNode_LinkedAnimLayer":
                    try:
                        itf = ns.get_editor_property("interface")
                        if itf:
                            rec["layer_interfaces"].append(str(itf.get_name()))
                    except Exception:
                        pass
        except Exception as e:
            rec["errors"].append(cname + ": " + str(e)[:60])
    return rec


def _variant_drift(paths):
    """Group ABP names by a base (trailing digits / _New / vN stripped) to flag duplicates."""
    groups = {}
    for p in paths:
        nm = p.rsplit(".", 1)[-1]
        base = re.sub(r"(_New|_new|\d+|[._-]?v?\d+([._]\d+)*)$", "", nm)
        groups.setdefault(base, []).append(nm)
    return {k: v for k, v in groups.items() if len(v) > 1}


def run(paths=None, write=True):
    out = {"abps": [], "drift": {}, "summary": {}}
    if paths is None:
        paths, err = _list_anim_blueprints()
        if err:
            out["summary"]["list_error"] = err
    out["summary"]["abp_count"] = len(paths)

    for p in paths:
        try:
            out["abps"].append(_audit_abp(p))
        except Exception as e:
            out["abps"].append({"abp": p, "errors": ["audit failed: " + str(e)[:80]]})

    out["drift"] = _variant_drift(paths)

    # Roll-up flags worth a human glance.
    flags = []
    for r in out["abps"]:
        if r.get("nodes", {}).get("AnimGraphNode_Slot", 0) == 0 and not r.get("errors"):
            flags.append("no Slot node: " + r["abp"])
    if out["drift"]:
        flags.append("variant groups: " + ", ".join(out["drift"].keys()))
    out["summary"]["flags"] = flags

    txt = json.dumps(out, indent=1, ensure_ascii=False)
    print(txt)

    if write:
        try:
            fp = os.path.join(unreal.Paths.project_saved_dir(), "levy_anim_audit.json")
            with open(fp, "w", encoding="utf-8") as f:
                f.write(txt)
            print("[levy_anim_audit] wrote " + fp)
        except Exception as e:
            print("[levy_anim_audit] write skipped: " + str(e)[:80])
    return out


if __name__ == "__main__":
    run()
