"""Revvy — UE 5.8 native interop bootstrap.

Registers Revvy's markdown skill packs (Content/Skills/{unreal,shared}/*/SKILL.md +
sibling sub-docs)
with Unreal's native AgentSkill system, so any MCP client attached to Epic's built-in
ModelContextProtocol server discovers them through ToolsetRegistry.AgentSkillToolset
(ListSkills / GetSkills) — in addition to Revvy's own manage_skills tool.

When the same optional engine plugin is present, Revvy also registers a compact
read-only Toolset. It delegates execution to Revvy's native ReadOnly MCP gate and
never exposes Full access through Epic's unauthenticated UE 5.8 server.

Resulting skill IDs: /Revvy/Python/init_unreal_PY.Revvy_<slug> for packs and
/Revvy/Python/init_unreal_PY.Revvy_<slug>__<subdoc> for sub-docs (hyphens -> underscores).
GetSkills requires these FULL paths; short names silently return empty.

Degrades gracefully: when the ToolsetRegistry plugin is disabled, or running in a
commandlet, registration is skipped with a log line. Revvy declares NO hard plugin
dependency — the guards below are the contract.

Registration pattern adapted from VibeUE (MIT, Kevin Buckley / Buckley Builds LLC).
"""

import inspect
from pathlib import Path

import unreal

# Keep references to the dynamically created skill classes alive for the session.
_REVVY_SKILL_CLASSES = []

_SKILLS_DIR = Path(__file__).resolve().parent.parent / "Skills"


def _parse_frontmatter(text):
    """Return (description, body) from optional YAML frontmatter.

    Only the `description:` key is extracted (multi-line continuation lines included).
    The body excludes the frontmatter block.
    """
    if not text.startswith("---"):
        return "", text

    lines = text.split("\n")
    desc_parts = []
    in_desc = False
    end_index = 0
    for i, line in enumerate(lines[1:], start=1):
        if line.strip() == "---":
            end_index = i
            break
        if line.startswith("description:"):
            in_desc = True
            desc_parts.append(line[len("description:"):].strip())
        elif in_desc and (line.startswith("  ") or line.startswith("\t")):
            desc_parts.append(line.strip())
        else:
            in_desc = False

    body = "\n".join(lines[end_index + 1:]) if end_index else text
    return " ".join(p for p in desc_parts if p), body


def _identifier(value):
    """Turn a skill/section name into a valid Python identifier fragment."""
    return "".join(ch if ch.isalnum() else "_" for ch in value)


def _register_revvy_agent_skills():
    # Guard 1: the toolset_registry Python package only exists on sys.path when the
    # ToolsetRegistry plugin is enabled in the host project.
    try:
        from toolset_registry.agent_skill import agent_skill
    except ImportError:
        unreal.log(
            "Revvy: ToolsetRegistry plugin not enabled — skipping native AgentSkill "
            "registration (Revvy's own manage_skills tool is unaffected).")
        return

    # Guard 2: reflection types must be exposed.
    if not hasattr(unreal, "AgentSkill"):
        unreal.log("Revvy: unreal.AgentSkill not exposed — skipping AgentSkill registration.")
        return

    # Guard 3: the registry subsystem needs GEditor (false in commandlets).
    try:
        if hasattr(unreal, "ToolsetRegistry") and not unreal.ToolsetRegistry.is_available():
            unreal.log("Revvy: ToolsetRegistry unavailable (commandlet?) — skipping AgentSkill registration.")
            return
    except Exception:  # noqa: BLE001 — availability probe must never break startup
        pass

    if not _SKILLS_DIR.is_dir():
        unreal.log_warning("Revvy: Skills directory not found at {}".format(_SKILLS_DIR))
        return

    def register_one(class_suffix, description, instructions):
        skill_cls = type("Revvy_" + class_suffix, (unreal.AgentSkill,), {
            "__doc__": description or class_suffix,
            "instructions": instructions,
        })
        skill_cls = agent_skill(skill_cls)
        # agent_skill() populates the CDO from the docstring; set explicitly too so
        # multi-line descriptions are preserved verbatim.
        cdo = skill_cls.get_default_object()
        cdo.set_editor_property("description", inspect.cleandoc(description or class_suffix))
        cdo.set_editor_property("instructions", instructions)
        _REVVY_SKILL_CLASSES.append(skill_cls)

    skill_dirs = []
    for engine_dir in (_SKILLS_DIR / "unreal", _SKILLS_DIR / "shared"):
        if engine_dir.is_dir():
            skill_dirs.extend(sorted(p for p in engine_dir.iterdir() if p.is_dir()))

    count = 0
    for skill_dir in skill_dirs:
        skill_md = skill_dir / "SKILL.md"
        if not skill_md.is_file():
            continue
        try:
            description, body = _parse_frontmatter(skill_md.read_text(encoding="utf-8"))
            register_one(_identifier(skill_dir.name), description, body)
            count += 1

            # Sibling sub-docs (deep reference material) register as their own skills.
            for sub in sorted(skill_dir.glob("*.md")):
                if sub.name.lower() == "skill.md":
                    continue
                sub_desc, sub_body = _parse_frontmatter(sub.read_text(encoding="utf-8"))
                register_one(
                    "{}__{}".format(_identifier(skill_dir.name), _identifier(sub.stem)),
                    "[{} sub-doc] {}".format(skill_dir.name, sub_desc or sub.stem),
                    sub_body)
                count += 1
        except Exception as exc:  # noqa: BLE001 — one bad pack must not break the rest
            unreal.log_warning("Revvy: failed to register skill '{}': {}".format(skill_dir.name, exc))

    unreal.log(
        "Revvy: registered {} skill(s) with Unreal's native AgentSkill system "
        "(IDs: /Revvy/Python/init_unreal_PY.Revvy_*)".format(count))


def _register_revvy_native_toolset():
    """Register the optional read-only UE 5.8 Toolset without a hard dependency."""
    try:
        import revvy_native_toolset
    except Exception as exc:  # noqa: BLE001 — optional interop must not break startup
        unreal.log_warning(
            "Revvy: native Toolset adapter could not be imported: {}".format(exc))
        return

    try:
        result = revvy_native_toolset.register_native_toolset()
    except Exception as exc:  # noqa: BLE001 — registry APIs are Experimental
        unreal.log_warning(
            "Revvy: native Toolset registration failed: {}".format(exc))
        return

    if result.get("success"):
        unreal.log(
            "Revvy: registered optional native read-only Toolset '{}'".format(
                result.get("toolset_class", "RevvyNativeToolset")))
        return

    error = result.get("error") or {}
    code = error.get("code", "unavailable")
    unreal.log(
        "Revvy: native read-only Toolset not registered ({}) — "
        "Revvy MCP remains available".format(code))


_register_revvy_agent_skills()
_register_revvy_native_toolset()
