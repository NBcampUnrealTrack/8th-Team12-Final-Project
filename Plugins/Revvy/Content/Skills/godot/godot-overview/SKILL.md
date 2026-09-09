---
name: godot-overview
display_name: Godot Support Overview
description: Current state of Revvy's Godot support — the dependency-free eleven-tool editor bridge, verified Tier-1 through Tier-11 and Tier-15 behavior, per-engine skill isolation, and the remaining roadmap.
keywords:
  - godot
  - godot editor
  - godot bridge
  - cross-engine
  - engine support
  - roadmap
related_skills:
  - godot-editor-automation
  - unity-overview
  - revvy
  - lore-vcs
  - blender-3d-modeling
  - blender-animation-roundtrip
  - gpt-sol-image-generation
---

# Godot Support Overview

> **Honest status: a dependency-free, eleven-tool Godot editor bridge is implemented** at
> `Bridges/Godot/addons/revvy/`. It exposes Tier-1 `editor_status`,
> `manage_asset`, `read_logs`, and constrained `execute_script`, plus verified
> Tier-2 `manage_play_mode` and read-only `inspect_scene`, and verified Tier-3/4/5
> `mutate_scene` (create/instantiate-packed-scene/rename/reparent/delete plus
> typed create: camera/lights, single-step undoable), and read-only
> `capture_viewport` (viewport PNG with explicit headless refusal; Tier-10 adds
> an optional `inline` base64 image block, 1 MB cap), and Tier-7
> `simulate_input` (discrete keyboard/mouse delivery in play mode;
> `REVVY_INPUT_BACKEND=none` kill switch), and Tier-8 `manage_properties`
> (curated property list/get/set — transform, light, camera — with
> one-call-one-undo and instanced-scene local overrides), and Tier-9's extended
> `manage_play_mode` descriptor (`pause`/`resume`/`step` answered with explicit
> `pause_unsupported` — the played scene is a separate process and 4.5 exposes
> no scriptable pause/step; `status` advertises `capabilities` up front), and
> Tier-11 `manage_prefab` (`list_overrides`/`revert`/`apply` of instanced-scene
> overrides — reference-instance diff enumeration, one-call-one-undo revert,
> pack+save `apply` declared non-undoable, automatic editable-instance enabling
> on child writes), and Tier-15 `override_created` (per-write override
> reporting on `manage_properties set` — an instanced-scene root transform
> reports true here, the documented divergence from Unity, because Godot stores
> it as a genuine difference; `prefab_override` unchanged).
> Protocol smoke and live proxy pairing E2E pass, along with all registered
> tool legs listed in `Content/Skills/revvy-capabilities.json` — including
> play/inspection/mutation, viewport capture, simulated input, curated
> properties, pause/step refusal, inline images, and packed-scene overrides —
> on Godot 4.6.1 (worker-reported pass on 4.7.1). General resource mutation
> remains.

## Current State

- The addon hosts a loopback MCP endpoint and writes `revvy-proxy.json` plus
  `tools-manifest.json` for discovery.
- The registered direct surface is exactly eleven tools: `editor_status`,
  `manage_asset`, `read_logs`, `execute_script`, `manage_play_mode`,
  `inspect_scene`, `mutate_scene`, `capture_viewport`, `simulate_input`,
  `manage_properties`, and `manage_prefab`.
- `manage_play_mode` supports bounded asynchronous `status`/`start`/`stop`.
  `inspect_scene` supports bounded read-only hierarchy traversal, pagination,
  and opaque-ID re-rooting while the editor is not playing.
- The `godot/` skill directory contains this overview and
  `godot-editor-automation`, isolated from Unreal content.
- Do not attempt Godot editor mutations through `execute_python_code` — Revvy's
  Python services drive the Unreal editor only.

## What Works Today

Engine-agnostic skills live under `Content/Skills/shared/` and remain fully
usable while working on a Godot project:

- `revvy` — core automation contract, tool discovery, and skill routing.
- `lore-vcs` — Lore version-control checkpoints and recovery points before
  destructive work.
- `blender-3d-modeling` / `blender-animation-roundtrip` — Blender mesh, UV,
  material, armature, and FBX/glTF round-trip pipelines that feed any engine.
- `gpt-sol-image-generation` — image/texture generation and editing, including
  the `gpt-sol-image-generation/qa-checklist` gate.

## Skill Isolation Layout

Skills are grouped by engine under `Content/Skills/<engine>/<slug>/`:

- `unreal/` — Unreal Engine skill packs (the full current catalogue).
- `shared/` — engine-agnostic packs, always available regardless of engine.
- `unity/`, `godot/` — per-engine overview and editor-automation skills.

A bare skill name resolves against the active engine's directory first, then
`shared/`. An explicit engine qualifier such as `unreal/blueprints` or
`shared/revvy` is also accepted, and the `<skill>/<section>` sub-doc grammar is
unchanged.

## Roadmap

The eleven-tool bridge surface is implemented and verified (protocol smoke,
real-editor E2E tests, and live proxy pairing on 4.6.1; worker-reported on
4.7.1). Remaining expansion areas are general `.tres`/`.res` resource
inspection and mutation beyond the current asset/prefab operations, project
build/export, first-class test execution, game-process diagnostics, and richer
progress/event streaming. The opaque-ID resolver also needs an issued-ID
registry and a rejection gate for live scene IDs the bridge never returned.
Pause and frame-step remain explicitly unsupported until a runtime/debugger
bridge exists. Do not claim or improvise missing surfaces; use only registered
tools and engine-agnostic pipelines until each slice lands.
