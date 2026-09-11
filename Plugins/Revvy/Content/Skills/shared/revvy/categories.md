# Revvy Skills — Categorized Learning Map

The runtime-discovered skills are organized into 12 domains. Each domain has a **L1→L4 progression**: load lower levels first when learning a new area; jump straight to L3/L4 when applying patterns to a feature already understood. Use `manage_skills(action='list')` for the authoritative installed count and the loadable section list.

**Load order convention**
- L1 = foundational asset/concept (must understand to do anything)
- L2 = authoring workflows in editor (Blueprint, materials editor, etc.)
- L3 = C++ / runtime architecture (engine-level patterns)
- L4 = advanced / cross-cutting (multi-system integration)

When in doubt: `manage_skills(action='suggest', query='<keyword>')` first, then pick by category here.

---

## 1. Animation

L1 → `animsequence`, `skeleton`
L2 → `animation-editing`, `animation-blueprint`, `animation-montage`, `blender-animation-roundtrip`
L3 → `ue-animation-system` (C++ AnimInstance, thread-safe eval, FAnimInstanceProxy)

**Cross-refs**
- `animation-blueprint` = editor authoring of AnimGraph nodes (Revvy MCP `AnimGraphService`)
- `ue-animation-system` = C++/runtime architecture of UAnimInstance
- `blender-animation-roundtrip` = typed Blender MCP import/inspect/adjust/export workflow with non-destructive copies and QA
- Load BOTH only when the task actually spans editor graph authoring and runtime C++ architecture — e.g. implementing a new character animation: BP for graph wiring, C++ skill for the native update split

**Remaining manual prerequisites**: Motion Matching schema creation/channel configuration
and Anim Layer Interface asset authoring. PoseSearch database authoring and Linked Anim
Layer node insertion are available through Revvy once those prerequisites exist. See
`revvy/SKILL.md` "자동화 경계".

---

## 2. Character & Gameplay Framework

L1 → `gameplay-tags`
L2 → `state-trees`, `enhanced-input`
L3 → `ue-character-movement` (production-safe CMC), `ue-mover-plugin` (5.8 Experimental), `ue-gameplay-framework`, `ue-state-trees`, `ue-input-system`
L4 → `ue-gameplay-abilities` (GAS — depends on all above), `ue-gameplay-message-subsystem` (Lyra pub/sub)

**Cross-refs**
- `state-trees` (editor authoring) ⇄ `ue-state-trees` (runtime architecture)
- `enhanced-input` (editor mapping) ⇄ `ue-input-system` (C++ input stack)
- GAS montage replication: ALSO load `ue-animation-system` + `ue-networking-replication`
- `ue-character-movement` ⇄ `ue-mover-plugin`: inspect the current Pawn and migration
  status before choosing. Do not assume Mover is production-ready or mix both movement
  stacks on one Pawn without an explicit bridge design.
- `ue-gameplay-message-subsystem` = local pub/sub for cross-feature events (shop purchase → HUD + audio + analytics). 새 cross-cutting event 만들 때 delegate 대신 우선 검토.

---

## 3. AI & Navigation

L3 → `ue-ai-navigation`

**Cross-refs**
- AI behaviors authored as State Trees → load `ue-state-trees` together
- Mass AI (large-scale) → `ue-mass-entity` instead

---

## 4. Visuals: Materials, VFX, UV

L1 → `uv-mapping`
L2 → `gpt-sol-image-generation`, `blender-3d-modeling`, `materials`, `effect-generation`, `niagara-emitters`, `niagara-systems`
L3 → `ue-materials-rendering`, `ue-niagara-effects`

**Cross-refs**
- `materials` (editor node graph) ⇄ `ue-materials-rendering` (shader code, custom HLSL, MaterialFunctions)
- `niagara-emitters` + `niagara-systems` (editor assembly) ⇄ `ue-niagara-effects` (C++ module integration, GPU emitters)
- `gpt-sol-image-generation` = capability-gated image creation/editing plus artifact QA; use `screenshots` for capture-only work
- `blender-3d-modeling` = typed Blender MCP mesh/topology/modifier/material/UV workflow; missing tools are a hard stop
- `effect-generation` = Revvy `levy_vfx` builder (Codex concept → MI derivation → assembly guide). **Always load FIRST for effect requests.**

---

## 5. Audio

L2 → `sound-cues`, `metasounds`
L3 → `ue-audio-system` (USoundBase, AttenuationSettings, AudioComponent runtime)

**Cross-refs**
- `metasounds` (procedural audio graphs) for ambient/dynamic SFX
- `sound-cues` for legacy/simple SFX
- `ue-audio-system` adds modulation, sound class hierarchy, runtime mix

---

## 6. World, Environment, Streaming

L1 → `level-actors`, `terrain-data`
L2 → `map-blockout`, `landscape`, `landscape-auto-material`, `landscape-materials`, `foliage`, `pcg`
L3 → `ue-world-level-streaming` (legacy sublevels + soft refs), `ue-world-partition-ofpa` (WP / OFPA / Data Layers / HLOD — 5.8), `ue-procedural-generation`

**Cross-refs**
- `landscape-materials` ⇄ `landscape-auto-material` ⇄ `materials` (auto-material is a landscape-specific material pattern)
- `pcg` (editor PCG graph authoring) ⇄ `ue-procedural-generation` (runtime spawning, level-streaming integration)
- Non-WP map / legacy sublevels → `ue-world-level-streaming`. WP map (DuckSoul main island) → `ue-world-partition-ofpa`. 둘 다 필요한 경우 거의 없음.
- `ue-world-partition-ofpa` covers OFPA source-control conflicts, Runtime Data Layer gating, HLOD generation (Nanite HLOD 5.8), Level Instance vs deprecated sublevels.

---

## 7. Data & Assets

L1 → `enum-struct`
L2 → `data-assets`, `data-tables`, `asset-management`
L3 → `ue-data-assets-tables`, `ue-serialization-savegames`

**Cross-refs**
- `data-assets` ⇄ `ue-data-assets-tables` (BP-facing assets vs C++ UDataAsset patterns + DataRegistry)
- SaveGame requires `ue-serialization-savegames` + understanding of UPROPERTY serialization flags
- DataTable rows reference `enum-struct` definitions

---

## 8. Code Foundations (C++ / Blueprint)

L2 → `blueprints`, `blueprint-graphs`
L3 → `ue-cpp-foundations`, `ue-actor-component-architecture`
L4 → `ue-module-build-system` (Build.cs, Module dependencies, MinimalAPI gotcha — relevant after 5.8 upgrade)

**Cross-refs**
- `blueprints` = BP author workflow; `blueprint-graphs` = graph node manipulation via Revvy
- `ue-cpp-foundations` = UCLASS/UFUNCTION/UPROPERTY semantics, reflection, GC
- `ue-actor-component-architecture` = composition patterns, replication-friendly component design
- `ue-module-build-system` = critical for adding new modules / understanding why MinimalAPI breaks linking

---

## 9. UI

L2 → `umg-widgets`
L3 → `ue-ui-umg-slate` (Slate primitives, retainer boxes), `ue-common-ui` (Activatable Widget Stack, Lyra UI 기반)

**Cross-refs**
- DuckSoul (Lyra-based) UI는 CommonUI 필수. 로드 순서: `umg-widgets` → `ue-ui-umg-slate` → `ue-common-ui`.
- `ue-common-ui` covers UCommonActivatableWidget lifecycle, PrimaryGameLayout layers, FUIInputConfig, back-action routing — patterns absent from UMG/Slate-only skill.
- Shop / pause menu / modal dialogs → `ue-common-ui`. Pure HUD primitive widget → `ue-ui-umg-slate`.

---

## 10. Performance & Scale

L3 → `ue-async-threading`, `ue-physics-collision`
L4 → `ue-mass-entity` (large-scale ECS — load only when handling 100+ entities)

**Cross-refs**
- `ue-async-threading` = TaskGraph, async loading, ParallelFor — pairs with `ue-animation-system` (worker thread eval) and `ue-niagara-effects` (GPU emit + CPU prep)
- `ue-mass-entity` replaces traditional Actor approach for crowd/horde scenarios

---

## 11. Networking & Cinematics

L3 → `ue-networking-replication` (legacy DOREPLIFETIME), `ue-iris-replication` (5.5+ hardened in 5.8), `ue-online-subsystem-eos`, `ue-sequencer-cinematics`

**Cross-refs**
- `ue-networking-replication` = legacy DOREPLIFETIME, RPC types, NetRole. **Load FIRST** for any networking task.
- `ue-iris-replication` = modern push-model replication. 신규 멀티플레이 feature 검토 시 Iris 우선. Mover 와 독립적이며 GAS는 5.4+ Iris-compatible.
- `ue-online-subsystem-eos` = sessions, parties, voice, achievements, leaderboards via EOS. Lyra/DuckSoul `LyraGameEOS.target` 이 evidence — 멀티플레이 hookup 시 필수.
- `ue-sequencer-cinematics` = LevelSequence runtime + scriptable cinematic playback.

---

## 12. Editor, Testing, Project Meta

L1 → `viewport`, `screenshots`
L2 → `engine-settings`, `project-settings`, `pie-testing`, `lore-vcs`
L3 → `ue-editor-tools`, `ue-testing-debugging`, `ue-automation-functional-tests` (Functional Test + Spec + Gauntlet), `ue-game-features`, `ue-lyra-experience` (Lyra Experience deep)
L4 → `ue-project-context`, `revvy`, `ue-build-cook-package` (UAT/BuildGraph/cooker), `ue-subsystems-taxonomy`

**Cross-refs**
- `engine-settings` + `project-settings` ⇄ `ue-editor-tools` (programmatic editor automation via EditorUtilityWidget)
- `pie-testing` (PIE workflow) ⇄ `ue-testing-debugging` (debug/diagnostics) ⇄ `ue-automation-functional-tests` (build-gating automation). 4/30 milestone 직결.
- `ue-game-features` (GFP 기초) → `ue-lyra-experience` (Experience 컴포지션) 순서. Experience 작업할 때 GFP 모르면 async-load deadlock.
- `ue-project-context` = always load FIRST on a new task — captures project conventions, plugin list, engine version
- `ue-build-cook-package` = 4/30 milestone 빌드 산출물 직결. UAT BuildCookRun + BuildGraph + GFP packaging + EOS encryption key.
- `ue-subsystems-taxonomy` = UI/Message/Input/Editor 어느 subsystem class를 골라야 할지 막힐 때 항상 먼저. UEngineSubsystem vs UGameInstanceSubsystem vs UWorldSubsystem vs ULocalPlayerSubsystem vs UEditorSubsystem 결정.
- `lore-vcs` = Lore(Epic 오픈소스 VCS) 체크포인트/커밋/싱크. 파괴적 대량 작업 전 recovery point 생성 (자동화 계약 요구사항). git 저장소면 shell git 사용 — Lore 아님.

---

## Recommended Discovery Flow

For any new task, follow this 4-step protocol:

1. **`manage_skills(action='load', skill_name='ue-project-context')`** — get project conventions/constraints
2. **`manage_skills(action='suggest', query='<your_domain_keyword>')`** — let Revvy narrow candidates
3. **Pick category above; load L1 → L<n>** in order until you have enough to act
4. **`discover_python_class('unreal.<ClassName>', method_filter='<keyword>')`** for live API surface (skill content can be stale — engine is truth)

## Cross-Reference Quick-Lookup (editor-skill ↔ engine-skill pairs)

| Domain workflow | Editor (Revvy-native) | Engine (quodsoler ue-*) |
|---|---|---|
| Animation | `animation-blueprint`, `animation-editing` | `ue-animation-system` |
| Blender animation round trip | `blender-animation-roundtrip` | typed Blender MCP capability |
| State machines | `state-trees` | `ue-state-trees` |
| Input | `enhanced-input` | `ue-input-system` |
| Materials | `materials`, `landscape-materials` | `ue-materials-rendering` |
| Image generation / editing | `gpt-sol-image-generation` | native image capability or `generate_vfx_texture` |
| Blender mesh / material / UV | `blender-3d-modeling` | typed Blender MCP capability |
| VFX | `niagara-*`, `effect-generation` | `ue-niagara-effects` |
| Audio | `metasounds`, `sound-cues` | `ue-audio-system` |
| Data | `data-assets`, `data-tables` | `ue-data-assets-tables` |
| UI (primitives) | `umg-widgets` | `ue-ui-umg-slate` |
| UI (CommonUI / Activatable) | `umg-widgets` | `ue-common-ui` |
| Procedural | `pcg`, `foliage`, `landscape` | `ue-procedural-generation` |
| World/Level (legacy) | `level-actors`, `map-blockout` | `ue-world-level-streaming` |
| World Partition | `level-actors` | `ue-world-partition-ofpa` |
| Testing (PIE) | `pie-testing` | `ue-testing-debugging` |
| Functional tests | `pie-testing` | `ue-automation-functional-tests` |
| Editor automation | `engine-settings`, `project-settings`, `viewport` | `ue-editor-tools` |
| Lyra Experience | (편집기 GUI 없음) | `ue-lyra-experience` |
| Messaging (pub/sub) | (편집기 GUI 없음) | `ue-gameplay-message-subsystem` |
| Mover (5.8) | (편집기 GUI 없음) | `ue-mover-plugin` |
| Iris replication | (편집기 GUI 없음) | `ue-iris-replication` |
| EOS / online services | `engine-settings` | `ue-online-subsystem-eos` |
| Packaging / cook | `project-settings` | `ue-build-cook-package` |
| Subsystem class 선택 | — | `ue-subsystems-taxonomy` |

**Rule**: when implementing a feature, load editor skill for "how to author" + engine skill for "how the runtime behaves". Skipping either causes guess-spiraling.
