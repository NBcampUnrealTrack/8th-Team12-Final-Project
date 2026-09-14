---
name: unity-editor-automation
display_name: Unity Editor Automation Model
description: How Unity editor automation works for the Revvy Unity bridge — main-thread rules, the verified API anchors (EditorApplication, AssetDatabase, console capture), headless CI entry points, and the version baseline the bridge targets.
keywords:
  - unity
  - unity editor
  - editor scripting
  - assetdatabase
  - headless
  - batchmode
  - bridge
related_skills:
  - unity-overview
  - godot-editor-automation
  - revvy
---

# Unity Editor Automation Model

> **Status: the Unity bridge skeleton is under active development and its seven-tool direct MCP surface is runtime-verified on 6000.3.8f1**
> (`Bridges/Unity/com.revstudio.revvy-unity/`). This skill documents the
> automation model the bridge implements — use it to reason about what Unity
> editor operations are possible and how they must be dispatched. The binding
> contract is `docs/design/multi-engine-bridge-contract.md` (§1-§5, §8.1-8.2).

## Version Baseline (verified 2026-08-01)

- Tested floor: **Unity 6000.3 LTS** (supported to 2027-12).
- Current line: Unity 6.5 (`6000.5.x`). Unity 7 announced for early 2027 with
  no breaking changes from Unity 6 and an explicit "coding agents" focus.
- Bridge reports `Application.unityVersion` as `engine_version`.

## The Main-Thread Rule (most important constraint)

**Every Unity API call must run on the main thread.** A bridge server
necessarily receives requests on a background thread/socket, so all tool work
is marshaled through a main-thread queue:

- `EditorApplication.update` callback or `EditorApplication.delayCall` drains
  a thread-safe queue of pending tool invocations.
- Results are returned to the socket thread via completion handles.
- Both leading Unity MCP servers (CoplayDev unity-mcp, IvanMurzak Unity-MCP)
  converge on exactly this pattern — do not call Unity APIs directly from the
  HTTP listener thread.

## Verified API Anchors (Tier-1 + bounded Tier-2/3 surface)

- `EditorApplication` — lifecycle hooks, playmode state
  (`EditorApplication.isPlaying`, `isCompiling`, `isUpdating`).
- `AssetDatabase` — `FindAssets`, `GUIDToAssetPath`, `ImportAsset`,
  `Refresh`, `MoveAsset`, `DeleteAsset` for `manage_asset`.
- `Application.logMessageReceived` — console capture into a ring buffer for
  `read_logs` (filter by `LogType`).
- `Application.unityVersion`, `Application.productName`,
  `Application.dataPath` — `editor_status` fields; dirty state via
  `EditorSceneManager.GetSceneManagerSetup` / scene `isDirty`.
- Tool registration: attribute-marked C# methods reflected into JSON Schema
  (`[RevvyTool]` mirroring the UE side's UFUNCTION reflection).
- `EditorApplication.EnterPlaymode` / `ExitPlaymode`, `SessionState`, and
  `playModeStateChanged` — asynchronous `manage_play_mode` across domain reloads.
- `SceneManager`, `Transform`, and `Resources.EntityIdToObject` — bounded
  `inspect_scene` traversal and opaque-ID re-rooting on Unity 6000.3+.
- `Undo.RegisterCreatedObjectUndo` / `RecordObject` / `SetTransformParent` /
  `DestroyObjectImmediate` with `Undo.CollapseUndoOperations` — single-step
  undoable `mutate_scene` structural edits; emitted IDs are pruned (not wiped)
  on hierarchy change per contract §4.1.

## Headless / CI Entry Points

- `-batchmode -nographics -executeMethod <Class.Method>` — the CI entry.
- All bridge config must be overridable via env/CLI args in batchmode (port,
  state dir, token) — license activation is the only external dependency.
- Docker path: `unityci/editor` images + game-ci runners for EditMode tests.
- Repository E2E: `Tests/Unity/Run-BridgeE2E.ps1` compiles the package and
  exercises real MCP scene inspection plus start/status/stop transitions.

## Landscape Note

Unity's official **Unity AI** (open beta, subscription) ships its own MCP
server and per-project Skills. Revvy's position: open/BYOK, multi-engine,
shared skill packs, and the existing proxy/CLI/desktop-app stack. Do not
collide with `com.unity.ai.*` package ids.
