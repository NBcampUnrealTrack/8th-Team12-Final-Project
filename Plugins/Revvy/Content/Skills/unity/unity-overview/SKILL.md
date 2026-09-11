---
name: unity-overview
display_name: Unity Support Overview
description: Current state of Revvy's Unity support — the editor bridge in Bridges/Unity, its verified Tier-1 and bounded Tier-2 tools, per-engine skill isolation, and the remaining roadmap.
keywords:
  - unity
  - unity editor
  - unity bridge
  - cross-engine
  - engine support
  - roadmap
related_skills:
  - unity-editor-automation
  - godot-overview
  - revvy
  - lore-vcs
  - blender-3d-modeling
  - blender-animation-roundtrip
  - gpt-sol-image-generation
---

# Unity Support Overview

> **Honest status: a Unity editor bridge skeleton exists**
> (`Bridges/Unity/com.revstudio.revvy-unity`, UPM package). It hosts the MCP
> server contract on the project-scoped `ue_port` in `<project>/.revvy/revvy-proxy.json`
> (`127.0.0.1:8088` only before project allocation) with four Tier-1 tools — `editor_status`,
> `manage_asset`, `read_logs`, `execute_script` — plus verified Tier-2
> `manage_play_mode` and read-only `inspect_scene`, and verified Tier-3/4/5
> `mutate_scene` (create/instantiate-prefab/rename/reparent/delete plus typed
> create: camera/lights, single-step undoable), and read-only
> `capture_viewport` (viewport PNG with explicit headless refusal; Tier-10 adds
> an optional `inline` base64 image block, 1 MB cap), and Tier-7
> `simulate_input` (discrete keyboard/mouse delivery in play mode via the
> optional Input System backend; `input_backend_unavailable` without it), and
> Tier-8 `manage_properties` (curated property list/get/set — transform, light,
> camera — with one-call-one-undo and prefab overrides recorded), and Tier-9
> play-mode `pause`/`resume`/`step` (`manage_play_mode` extension, `frames`
> 1-60, input queued while paused lands on exactly the stepped frame), and
> Tier-11 `manage_prefab` (`list_overrides`/`revert`/`apply` of prefab-instance
> overrides — pristine instances report none, revert is one-call-one-undo,
> `apply` rewrites the source asset and is declared non-undoable), and Tier-15
> `override_created` (per-write override reporting on `manage_properties set` —
> computed from the same predicate `manage_prefab` uses, so an instance root's
> position/rotation report false while its scale reports true; `prefab_override`
> unchanged). It writes
> `revvy-proxy.json` + `tools-manifest.json` for the proxy. Package compilation
> and live proxy pairing E2E (health, tools/list merge, `editor_status`
> round-trip through revvy-proxy) pass on Unity 6000.3.8f1. Component
> attach/detach is not implemented.

## Current State

- The MCP connection to the Unity Editor comes from the UPM bridge package in
  `Bridges/Unity/`; install it into a Unity 6000.3+ project via git URL or
  `file:` path (see the package README).
- Editor mutations go through the bridge's Tier-1 tools, never through
  `execute_python_code` — Revvy's Python services drive the Unreal editor only.
- `manage_play_mode` supports bounded asynchronous `status`/`start`/`stop`.
  `inspect_scene` supports bounded read-only hierarchy traversal, pagination,
  and opaque-ID re-rooting while the editor is not playing.
- The `unity/` skill directory isolates Unity-specific skills from Unreal
  content: `unity-overview` (this skill) and `unity-editor-automation`
  (bridge tool usage).
- `unity.status` in `revvy-capabilities.json` tracks the maturity level; check
  it before promising Unity work.

## What Works Today

Engine-agnostic skills live under `Content/Skills/shared/` and remain fully
usable while working on a Unity project:

- `revvy` — core automation contract, tool discovery, and skill routing.
- `lore-vcs` — Lore version-control checkpoints and recovery points before
  destructive work.
- `blender-3d-modeling` / `blender-animation-roundtrip` — Blender mesh, UV,
  material, armature, and FBX round-trip pipelines that feed any engine.
- `gpt-sol-image-generation` — image/texture generation and editing, including
  the `gpt-sol-image-generation/qa-checklist` gate.

## Skill Isolation Layout

Skills are grouped by engine under `Content/Skills/<engine>/<slug>/`:

- `unreal/` — Unreal Engine skill packs (the full current catalogue).
- `shared/` — engine-agnostic packs, always available regardless of engine.
- `unity/`, `godot/` — per-engine packs (overview + editor-automation skills
  for each bridge).

A bare skill name resolves against the active engine's directory first, then
`shared/`. An explicit engine qualifier such as `unreal/blueprints` or
`shared/revvy` is also accepted, and the `<skill>/<section>` sub-doc grammar is
unchanged.

## Roadmap

The Tier-1 surface plus `manage_play_mode`, read-only `inspect_scene`, and
`mutate_scene` are implemented and verified (package compile, direct MCP E2E,
live proxy pairing, real-editor mutation E2E).
Next steps: component/property operations, prefab override
editing, and simulated input. Do not claim or improvise those missing write
surfaces; use only the six registered bridge tools and engine-agnostic pipelines.
