---
name: revvy
description: Operate and extend Unreal Engine through Revvy's MCP tools and Python services. Use for Revvy tool discovery, safe editor automation, skill selection, external proxy diagnostics, Unreal asset or actor changes, and deciding whether a task belongs in editor automation or runtime C++.
compatibility: Requires Revvy MCP server
revvy_classes:
  - BenchmarkService
  - BuildInfoService
  - EditorTransactionService
  - ObjectReflectionService
  - PerformanceService
  - SubsystemQueryService
---

# Revvy Core Workflow

Revvy exposes its own skills system via MCP. Use it instead of this file for all tasks.

> **UE 5.8 native interop**: when the host project enables Epic's `ToolsetRegistry` plugin,
> every Revvy skill also registers with Unreal's native AgentSkill system at editor start
> (`ToolsetRegistry.AgentSkillToolset` → `ListSkills`/`GetSkills`, IDs
> `/Revvy/Python/init_unreal_PY.Revvy_<slug>` — full paths required; short names return
> empty). Clients on Epic's built-in MCP server (port 8000) see the same skills without
> connecting to Revvy's server. `manage_skills` remains the primary path.
>
> The optional native Toolset is intentionally narrower than an authenticated
> Revvy Safe session. Its C++ allow-list exposes only Python reflection discovery
> (`discover_python_class`, `discover_python_function`,
> `discover_python_module`, and `list_python_subsystems`). It excludes
> `read_logs`, every open-world tool, every mutation, and every Full-access
> action. The catalogue comes from the current Editor process rather than the
> shared proxy manifest.
>
> UE 5.8 `FGlobalSandbox` is C++-only. Revvy reports Sandbox status/changes only
> when a compatible user-local bridge is present; it does not claim a protected
> write session otherwise. Sandbox mutation is never registered with the
> unauthenticated native Toolset and requires a one-time approval bound to the
> exact operation and file list.

## 🤝 Automation Contract

The goal is **completion of the requested editor task, not instructions for the user to perform it**. For every request:

1. **Discover** current project/editor state and live capabilities (`manage_skills(action='list')`, `discover_python_class`, `read_logs`).
2. **Remove recoverable blockers yourself** — filesystem, process, port, log, and MCP channels — before asking the user anything.
3. **Preflight**: perform a read-only inspection of the target state before any mutation (asset exists? current property value? graph shape?).
4. **Act**, splitting multi-object changes into verifiable steps; stop dependent writes on the first failure.
5. **Readback proof**: after every mutation, perform an **independent read that proves the requested state** (re-query the property, re-list the asset, re-read the graph). A tool returning `success` is not proof.
6. **Report** actions performed, verified resulting state, and unresolved risks. Never replace an available action with setup directions or UI click instructions for the user.

**Truth-source ordering** — when sources disagree, trust in this order:
1. Live introspection results (`discover_python_class/_function/_module`, `manage_skills` list, current tool results)
2. Current editor state, logs, project files
3. UE source / Epic documentation
4. Skill content (capability baseline only — may be stale; never invent an API a live discovery call does not expose)

**Blocker exhaustion**: before declaring something blocked, record which channels (filesystem / process / port / log / editor / MCP) were attempted with evidence each cannot proceed. One failed call is not exhaustion — retry or read back first. Ask the user for at most **one minimal blocker action**.

**Destructive ops**: bulk deletes / overwrites / restarts need an explicit recovery point (save, source-control state) or user authorization first. Editor tool calls are transaction-wrapped (Ctrl+Z), but Python-driven multi-asset edits are not atomic — verify each step.

## Select Scope

- Editor asset or level authoring: load the domain skill such as `blueprints`, `materials`, `level-actors`, or `state-trees`.
- Runtime C++ architecture: load the matching `ue-*` skill.
- End-to-end work spanning both: load both only when each scope is required.
- Terminal, IDE, or editor-restart work: load `revvy-external-control`.
- Unknown domain: call `manage_skills(action="suggest", query="<task>")`; do not rely on a hard-coded skill count or catalogue.

## 🧭 Route by functional area — the **NOT for** column is the disambiguator

When two areas seem to fit, the one that *excludes* your task is telling you where to go instead.

| Functional area | Use for — **NOT** for… | Load skill(s) |
|---|---|---|
| **Scene & actors** | place / move / arrange / tag actors in a level — NOT gameplay logic (→ Blueprints), NOT terrain/foliage (→ World), NOT attaching Niagara components (→ VFX) | `level-actors` |
| **Blueprints & gameplay logic** | BP classes & graphs, Enhanced Input, gameplay tags — NOT AI behavior (→ `state-trees`), NOT AnimBP graphs (→ Animation), NOT C++ source authoring (→ `ue-cpp-foundations`) | `blueprints`, `blueprint-graphs`, `enhanced-input`, `gameplay-tags` |
| **Materials & shading** | materials, instances, nodes, HLSL — NOT Niagara particle materials (→ VFX), NOT landscape auto-material (→ World) | `materials` |
| **VFX** | Niagara systems/emitters, effect builds — NOT the mesh/material itself (→ Materials), NOT one-off screenshots (→ `screenshots`) | `effect-generation` FIRST, then `niagara-emitters`/`niagara-systems` |
| **World & terrain** | landscape, foliage, PCG, map blockout, heightmaps — NOT single-actor placement (→ Scene), NOT runtime streaming C++ (→ `ue-world-partition-ofpa`) | `landscape`, `foliage`, `pcg`, `map-blockout`, `terrain-data` |
| **Animation** | AnimBP graphs, montages, sequences, skeletons — NOT runtime C++ AnimInstance (→ `ue-animation-system`), NOT Blender-side editing (→ `blender-animation-roundtrip`) | `animation-blueprint`, `animation-montage`, `animsequence` |
| **UI (UMG)** | widget blueprints, layout, MVVM — NOT the gameplay behind the UI (→ Blueprints), NOT CommonUI/Lyra stacks (→ `ue-common-ui`) | `umg-widgets` |
| **Audio** | MetaSound/SoundCue authoring — NOT triggering from gameplay (→ Blueprints), NOT anim-notify wiring (→ Animation) | `metasounds`, `sound-cues` |
| **Data** | enums/structs, data tables/assets — NOT save games (→ `ue-serialization-savegames`) | `enum-struct`, `data-tables`, `data-assets` |
| **Diagnostics, testing & run** | PIE start/stop/query, logs, screenshots — NOT fixing the logic a bug points to (→ its authoring area) | `pie-testing`, `screenshots`, `viewport` |
| **Project meta** | project/engine settings, conventions — always pair with `ue-project-context` on a new project | `project-settings`, `engine-settings`, `ue-project-context` |
| **Version control** | Lore checkpoints/commit/sync, recovery points before destructive work — NOT git repos (shell git), NOT single-asset undo (→ editor transactions) | `lore-vcs` |

## ⭐ Start here — categorized learning map

Before listing, load the **taxonomy** to see the runtime-discovered skills grouped by domain (Animation / Gameplay / Visuals / World / Data / UI / Networking / Performance / Editor) with **L1→L4 progression order** and editor↔engine cross-reference pairs:

```
manage_skills(action="load", skill_name="revvy/categories")
```

Use this FIRST on any unfamiliar domain — it tells you which skills to chain (e.g. `state-trees` + `ue-state-trees`, `animation-blueprint` + `ue-animation-system`) for both authoring and runtime understanding.

## Discover available skills

```
manage_skills(action="list")
```

## Load a skill before working in a domain

Always load the relevant skill **before writing any code**. The skill contains exact API patterns and gotchas — without it you will guess wrong property names and spiral into discovery loops.

| Task | Load this skill |
|---|---|
| Blueprints | `blueprints` |
| PCG / procedural generation / scatter | `pcg` |
| Materials | `materials` |
| State Trees | `state-trees` |
| Play / test / run / PIE | `pie-testing` |
| Create or edit an image / GPT Sol | `gpt-sol-image-generation` and `gpt-sol-image-generation/qa-checklist` |
| Blender mesh / topology / modifier / material / UV | `blender-3d-modeling` |
| Blender armature / action / keyframe / animation FBX | `blender-animation-roundtrip` |

```
manage_skills(action="load", skill_name="pcg")
manage_skills(action="load", skill_name="blueprints")
manage_skills(action="load", skill_name="materials")
```

The loaded skill returns:
- `content` — workflows, gotchas, and property formats for the domain
- `revvy_classes` / `unreal_classes` — class names to feed into `discover_python_class('unreal.<ClassName>', method_filter='<keyword>')` to get live method signatures
- `COMMON_MISTAKES` — extracted "common mistakes" section (when the skill has one)
- `available_sections` — sibling sub-docs you can load via `skill_name="<skill>/<section>"` for deeper reference material

Always call `discover_python_class` on the classes in `revvy_classes` before writing code — never guess method names from the skill content alone. **Batch the discovery into ONE call** instead of one call per class:

```
# ONE call covers all classes and topics ('|' ORs the filter keywords):
discover_python_class(
    class_name="unreal.MaterialService, unreal.WidgetService, unreal.MaterialNodeService",
    method_filter="create|delete|compile|property|color")
# WRONG — separate calls per class waste round-trips
```

## Required Sequence

1. Query current state. Never guess actor names, asset paths, class names, target names, or property values.
2. Load the smallest relevant skill index. Load a listed sub-document only when it covers the current step.
3. Call `discover_python_class` for the exact service method signature before writing Python.
4. Re-query targets and counts immediately before mutation.
5. Execute one logical operation.
6. Verify through a read-only query, graph inspection, compile result, log, or screenshot.

## Tool Boundaries

- Use `manage_asset` for asset search, existence checks, open, save, move, duplicate, and delete.
- Use `manage_skills` for documentation; it remains available through the standalone proxy while Unreal is offline.
- Use `execute_python_code` only for confirmed `unreal.*` APIs and Revvy service methods.
- Use `read_logs` for logs instead of Python file access.
- Use editor subsystems in current UE versions; do not assume deprecated `EditorLevelLibrary` methods exist.
- A reply that opens with `[revvy] This result is N characters...` was too large to return whole. It is a real result, not an error: the rest is held under the printed handle. Read on with `revvy_result_page {"handle": "...", "offset": N}` until a reply says it is the end. Do not re-run the original tool to get the missing part — it was already computed, and re-running costs the call again and can return different data.

## Safety

Ask for explicit confirmation before:

- deleting or overwriting an asset or map;
- changing persistent project settings;
- bypassing reference checks;
- clearing collections, bindings, states, nodes, foliage, or components;
- changing 100 or more actors or instances.

Do not replace a missing setter with delete-and-recreate. If the exact in-place API cannot be discovered, leave current data unchanged and report the capability gap.

Transactions are method-specific. Confirm transaction support from the live signature or service documentation. For a manual transaction, always close it on success and cancel it on failure.

## 자동화 경계 — scriptable vs editor-GUI-only (UE 5.8, verified 2026-07-30)

Some Unreal authoring is genuinely **not exposed to scripting**. Confirm the live Revvy
surface before promising it. Data behind private/editor-only fields may still be reachable
through a dedicated Revvy C++ service, so `WITH_EDITORONLY_DATA` alone is not proof that a
workflow is GUI-only.

**Animation Blueprints** — most authoring is scriptable via `AnimGraphService`: state
machines/states/conduits/transitions(+rules), common player/blend/slot/cached-pose nodes,
`AddLinkedAnimLayer` when the AnimBP already implements the interface, state animation
assignment, validation, and compile. **Not currently exposed:** Motion Matching node
insertion, Anim Layer Interface asset authoring, and arbitrary node types outside the
service's AddX surface.

**Motion Matching (PoseSearch)** — `MotionMatchingService` can create a database from an
existing schema, add animation assets, synchronously build its index, and query build
statistics. Creating or editing the PoseSearch Schema itself is not currently exposed.
Runtime DB swaps remain available through
`UMotionMatchingAnimNodeLibrary.SetDatabasesToSearch` and Chooser.

**Anim Layers** — Revvy can add a Linked Anim Layer graph node after the AnimBP implements
the relevant interface. Interface asset authoring is still an editor/manual prerequisite.
Runtime linking remains available through `UAnimInstance::LinkAnimClassLayers`.

Read-only audit helpers (no editor mutation): `Content/Python/levy_anim_audit.py`, `Content/Python/levy_animinstance_bindings.py`.

Project-specific names found in examples (for example DuckSoul, LyraGameEOS, or Xociety)
are examples only. Never infer that those modules, assets, tags, or paths exist in the
current project; verify project context and live assets first.

## Restart Semantics

The proxy is persistent, but Unreal mutations require a connected editor. When a call reports the editor offline:

1. Do not queue or automatically replay the mutation.
2. Wait until `revvy_status` reports the editor connected.
3. Re-query current state because the previous call may have partially completed.
4. Retry only if verification shows the requested change is still absent.

## In-Editor AI Provider

Revvy Settings can switch the embedded chat between `Claude Code` and `Codex GPT`.
Both providers launch a local CLI and reuse its current login; never request, copy,
or persist an OpenAI API key for Codex. Read Codex models from the current account's
cache instead of guessing preview model IDs. Provider changes reset the selected model
and local CLI session so Claude and GPT model names cannot cross transports.

## Evidence

A transport-level success is not completion evidence. For graph edits, inspect nodes and connections and compile. For actors and assets, read back identity and properties. Report the verified path or actor plus the relevant result.
