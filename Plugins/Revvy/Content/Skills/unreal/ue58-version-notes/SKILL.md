---
name: ue58-version-notes
display_name: UE 5.8 Version Notes & API Knowledge Gaps
description: Version-pinned UE 5.8 API reference — breaking changes, deprecations,
  and new surfaces vs what LLM training data knows. ALWAYS consult before writing
  C++ against engine APIs or recommending editor workflows. Use when generating
  engine-facing C++/Python, upgrading code, or when an API call fails to compile.
keywords:
  - "5.8"
  - version
  - deprecated
  - breaking change
  - api
  - migration
related_skills:
  - ue-cpp-foundations
  - ue-module-build-system
---

# UE 5.8 Version Notes (verified 2026-07, against installed engine source)

> ⚠️ **Knowledge-gap warning**: do not assume a model knows the UE 5.8 API surface.
> APIs recalled from older versions may be renamed, deprecated, or gone. When compiling against
> the engine: this doc first, then `discover_python_class` / engine headers — never
> memory. Live engine version check: `unreal.SystemLibrary.get_engine_version()`.

## Version timeline

| Version | Date | Status |
|---|---|---|
| 5.6 | 2025-06 | old |
| 5.7 | 2025-11-13 | previous |
| **5.8** | **2026-06-23** | **current — last planned UE5 major at announcement** |
| UE6 | not officially dated here | in development; do not invent migration or Blueprint-deprecation timelines |

## Compile-verified 5.8 breaking changes / deprecations

These broke or warned in THIS plugin's 5.8 build — treat as certain:

| Old (pre-5.8 habit) | 5.8 reality |
|---|---|
| `StructUtils` plugin/module dependency | **Gone.** `FInstancedStruct`/`FPropertyBag` live in CoreUObject; includes stay `StructUtils/...` paths. Remove the plugin+module dep |
| `UMovieScene::GetBindings()` (non-const) | Deprecated — use const access (`const UMovieScene*` then `GetBindings()`) |
| `REN_ForceNoResetLoaders` flag on `Rename()` | Deprecated — Rename no longer calls ResetLoaders; drop the flag. For intentional linker mismatch use `REN_AllowPackageLinkerMismatch` |
| `ULandscapeLayerInfoObject::LayerName` direct access | Being privatized — use `GetLayerName()` |
| `FCoreDelegates::OnPostEngineInit` | Deprecated — `FCoreDelegates::GetOnPostEngineInit()` |
| `FImageUtils::ExportRenderTarget2DAsPNG` | Deprecated — `FImageUtils::GetRenderTargetImage` + ImageWrapper PNG encode |
| `Engine/UserDefinedStruct.h` | Moved: `StructUtils/UserDefinedStruct.h` |
| XAudio2 audio backend | Deprecated; WASAPI default on Windows. Revert: `[Audio] AudioMixerModuleName=AudioMixerXAudio2` |
| `EditorLevelLibrary` / EditorScriptingUtilities | Long-deprecated, still the #1 LLM hallucination: use subsystems — `UnrealEditorSubsystem` (world/camera), `LevelEditorSubsystem` (level save/load), `EditorActorSubsystem` (select/spawn/destroy), `StaticMeshEditorSubsystem` |
| JSON iteration: `Pair.Key` as FString | 5.8 JSON object keys are `UE::FSharedString` — deref `*Pair.Key` where FString expected |

## New 5.8 surfaces LLMs don't know

- **`ModelContextProtocol` plugin** (Experimental, in-engine MCP server, port 8000, HTTP+SSE, loopback-only, no auth). Console: `ModelContextProtocol.StartServer/StopServer/RefreshTools/GenerateClientConfig <Client>`. Tool Search mode default: `tools/list` = 3 meta-tools (`list_toolsets`/`describe_toolset`/`call_tool`).
- **`ToolsetRegistry` plugin**: C++ `UToolsetDefinition` + static `UFUNCTION(meta=(AICallable))`, explicit `UToolsetRegistry::RegisterToolsetClass()`. Python: `unreal.ToolsetDefinition` subclass + `@toolset_registry.tool_call` + `@staticmethod` (decorator order matters; stdlib type hints — `list[str]` not `unreal.Array`). AgentSkills: `unreal.AgentSkill` subclass + `@agent_skill`; discovery = class derivation, ID = class path name.
- **Mover remains Experimental in 5.8**: `UMoverComponent`, NavWalking + Detour Crowd,
  ChaosMover trajectory prediction, and sim-driven montages are available, but APIs and
  data formats may change. Treat CMC as the production-safe default until the project has
  completed its own migration and network validation.
- **Sandboxes are Experimental**: use `FGlobalSandbox` to isolate file changes and
  selectively persist/discard them; keep transactions and authorization for side effects
  outside the sandboxed mount points.
- **Semantic Search is Experimental**: asset thumbnails/metadata may be sent to the
  configured provider and API keys are stored in editor per-user config, so require an
  explicit privacy/cost decision before indexing.
- **Production-ready promotions**: MegaLights, Audio Insights, Dataflow (Chaos Cloth), Live Link Hub, Iris replication, Movie Render Graph. PCG production-ready since 5.7 (+5.8: manual edits survive regeneration).
- **Experimental**: Mesh Terrain (3D 지형, cave/overhang), Lumen Lite, MetaHuman Crowd, Control Rig Dynamics, Substrate NPR/toon.
- Python embedded = **3.11.8**.

## Synchronous viewport capture (LLM blind spot)

Common wrong belief: "editor viewport capture needs a render tick → impossible from Python".
Reality (Epic's own MCP does this): `GCurrentLevelEditingViewportClient` → set pose →
`Viewport->Draw()` (synchronous) → `GetViewportScreenShot()` (blocking readback) → restore.
Revvy: `unreal.ScreenshotService.capture_from_pose(...)`.

## Rules of engagement

1. Compile error on an engine symbol → check this table BEFORE retrying variations.
2. Suggesting an editor-automation API → verify with `discover_python_class` (live truth).
3. New engine feature question → this doc + `deep_research`, not memory.
4. When 5.9/UE6 lands: regenerate this pack; date-stamp stays in the heading.
