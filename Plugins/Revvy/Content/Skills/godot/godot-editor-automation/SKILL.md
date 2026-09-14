---
name: godot-editor-automation
display_name: Godot Editor Automation Model
description: How Godot editor automation works for the Revvy Godot bridge — EditorPlugin/EditorInterface surface, verified API anchors, headless CI entry points, GDScript constraints, and the version baseline the bridge targets.
keywords:
  - godot
  - godot editor
  - editorplugin
  - editorinterface
  - headless
  - gdscript
  - bridge
related_skills:
  - godot-overview
  - unity-editor-automation
  - revvy
---

# Godot Editor Automation Model

> **Status: the Godot bridge is under active development and its eleven-tool direct surface is runtime-verified on 4.6.1 (worker-reported on 4.7.1)**
> (`Bridges/Godot/addons/revvy/`). This skill documents the automation model
> the bridge implements. The shared binding contract starts at
> `docs/design/multi-engine-bridge-contract.md`; tier-specific contracts under
> `docs/design/` define the later tool surfaces.

## Version Baseline (verified 2026-08-01)

- API baseline/target: **Godot 4.5**. Directly verified: **4.6.1**.
  Worker-reported: **4.7.1** (2026-07-13 research snapshot), with 4.8 in
  development at that snapshot. A real 4.5 editor CI leg is not wired yet.
- Bridge reports `Engine.get_version_info().string` (e.g. `4.7.1.stable`) as
  `engine_version`.

## Bridge Vehicle: GDScript EditorPlugin

- Standard `addons/revvy/` + `plugin.cfg` EditorPlugin — dependency-free,
  hot-reloadable, AssetLib-distributable. **No GDExtension, no C#** (C# needs
  .NET editor builds = smaller install base).
- GDScript has networking built in: `TCPServer`, `WebSocketPeer`,
  `HTTPRequest` — enough to embed the loopback MCP endpoint with zero deps.
- Lifecycle: `_enter_tree` starts the server + writes the runtime config,
  `_exit_tree` stops and cleans up.

## Verified API Anchors (current eleven-tool surface)

- `EditorInterface` (static access since Godot 4.2) — selection, filesystem,
  inspector, open scenes, play/stop: the primary tool dispatch surface.
- `EditorFileSystem` (`EditorInterface.get_resource_filesystem`) — scan,
  refresh, reimport for `manage_asset`.
- `Engine.get_version_info()`, `ProjectSettings.globalize_path("res://")`,
  `EditorInterface.is_playing_scene()` — `editor_status` fields.
- Scene ops go through `EditorInterface.get_edited_scene_root()` + SceneTree
  on the main thread (Godot scene ops are single-threaded anyway — no
  marshaling queue needed, unlike Unity).
- `EditorInterface.play_current_scene()` / `stop_playing_scene()` provide the
  deferred `manage_play_mode` transition surface.
- Edited-scene `Node` traversal and `instance_from_id()` provide bounded
  `inspect_scene` pagination and opaque-ID re-rooting.
- `EditorInterface.get_editor_undo_redo()` (`EditorUndoRedoManager`) — one
  undo action per `mutate_scene` call (`add_do_method`/`add_undo_method` +
  `add_do_reference`; deleted nodes are never freed so undo restores them),
  with `EditorInterface.mark_scene_as_unsaved()` after real edits.
- `EditorInterface.get_editor_viewport_3d()` + `ViewportTexture.get_image()` —
  viewport PNG capture, with explicit refusal when the display driver is
  headless and optional bounded inline base64 transport.
- `Input.parse_input_event()` + `Input.warp_mouse()` — discrete keyboard and
  mouse delivery to a running game; the bridge does not invent editor-level
  pause or frame-step support that Godot does not expose.
- Curated property setters use `EditorUndoRedoManager`, and child writes inside
  an instanced scene enable the owning editable instance before persisting the
  local override.
- `PackedScene.instantiate()`, `PackedScene.pack()`, `ResourceSaver.save()`,
  and `ResourceLoader.CACHE_MODE_REPLACE` back override inspection,
  single-action revert, and explicitly non-undoable apply.

## Headless / CI Entry Points

- `godot --headless --path <proj> --editor` — editor without display; the
  bridge must load and serve here from day one.
- `--import` catches GDScript parse regressions in CI; `--quit-after` bounds
  runs; gate serving behind `REVVY_ALLOW_HEADLESS=1`.
- Recommended CI matrix (not yet wired into this repository's workflow):
  chickensoft-games/setup-godot at the engine floor + research-current line
  (4.5 / 4.7). No editor license secret is required.

## GDScript Constraints

- Godot **4.x API only** — no 3.x calls (`PoolByteArray` → `PackedByteArray`,
  `instance()` → `instantiate()`, etc.).
- Typed GDScript throughout (`var x: Type`, `-> Type` returns) — it catches
  errors at parse time, which is the only compile check a plugin gets.

## Landscape Note

Godot's current contribution policy discourages AI-assisted contributions and
prohibits contributions generated entirely by AI. Contributors remain
responsible for understanding, testing, and disclosing assistance. This policy
governs contributions to Godot itself, not third-party addons such as Revvy:
https://contributing.godotengine.org/en/latest/pull_requests/pull_request_guidelines.html
