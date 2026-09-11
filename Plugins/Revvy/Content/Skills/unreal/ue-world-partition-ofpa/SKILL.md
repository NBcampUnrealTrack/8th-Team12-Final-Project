---
name: ue-world-partition-ofpa
description: "Use this skill when working with World Partition, One-File-Per-Actor, editor/runtime Data Layers, HLOD generation, runtime hash grids, custom streaming sources, Level Instances, WorldPartitionBuilderCommandlet, and wp.* diagnostics."
revvy_classes:
  - WorldPartitionService
metadata:
  version: 1.0.2
---

# UE World Partition + OFPA + Data Layers + HLOD

You are an expert in UE's World Partition system (UE 5.x): streaming grids, One-File-Per-Actor source control, Data Layer runtime gating, HLOD generation pipeline, and Level Instance composition.

## Context Check

Read `.agents/ue-project-context.md` first. Confirm:

- UE 5.8 (5.7+ World Partition has hash-grid changes; 5.8 hardens HLOD)
- Whether the project map(s) are World Partition (`UWorld::IsPartitionedWorld()`)
- OFPA in use (every actor as a separate `.uasset` under `__ExternalActors__/`)
- Data Layer assets present (`UDataLayerAsset`)
- Existing HLOD layer setup
- Streaming source patterns (default = PlayerController; custom component?)

## Information to Gather

1. New WP map or migrating a sublevel-based map?
2. Streaming grid size needed (depends on play area + view distance)?
3. Data Layers — Editor-only (organize), Runtime gated, or both?
4. HLODs needed (open world yes; single combat arena no)?
5. Multi-developer editing the same map? (OFPA conflict resolution becomes critical)
6. Level Instance composition — shop interior as a reusable instance?

---

## World Partition Fundamentals

```
UWorld (World Partition enabled)
  └── UWorldPartition (subsystem on World)
        ├── Runtime Hash Grids (Streaming Grids)
        │     - Each grid: cell size, loading range, HLOD layer
        ├── Streaming Sources (IWorldPartitionStreamingSourceProvider)
        │     - Default: APlayerController implements the provider interface
        │     - Custom: attach a UWorldPartitionStreamingSourceComponent to any actor
        ├── Data Layers (UDataLayerAsset references)
        │     - Editor Layer: organize actors in editor
        │     - Runtime Layer: gate streaming by gameplay state
        └── HLOD Layers (UHLODLayer)
              - Generate proxy meshes for distant cells
```

**Cell**: a bounded volume in the world; all actors inside a cell load/unload
together. **Streaming source**: a moving radius that loads cells in range.

### Enable World Partition

New WP map: editor → File → New Level → "Open World".
Convert sublevel-based map: `WorldPartitionConvertCommandlet`.

```bash
UnrealEditor-Cmd.exe DuckSoul.uproject -run=WorldPartitionConvertCommandlet
    "/Game/Maps/OldMap" -ConversionSuffix
```

Result: original `.umap` + new `OldMap_WP.umap` with WP enabled (`-ConversionSuffix` is a switch; the suffix is `_WP`).

---

## OFPA — One File Per Actor

Without OFPA, an entire `.umap` is one file; merging concurrent edits is nearly
impossible. With OFPA, each actor is a separate `.uasset` under
`Content/__ExternalActors__/<MapName>/<HashFolder>/<ActorGuid>.uasset`.

### Enable OFPA

Per-map: World Settings → World → "Use External Actors" (`ULevel::bUseExternalActors`).
World Partition maps always use external actors; no separate project setting is required.

### Source-control conflict resolution

OFPA conflicts: two devs move the same actor → both have a divergent
`.uasset` for that actor's GUID.

**Resolution strategies**:

1. **Accept theirs / mine** — pick a winner outright in P4/Git.
2. **Merge in editor** — open both versions in editor side-by-side; manually
   reconcile properties. UE has no built-in 3-way merge for assets.
3. **Per-actor lock** — Perforce checkout the `__ExternalActors__/.../<Guid>.uasset`
   file BEFORE editing. P4V auto-locks; Git has no equivalent — use
   coordination.

### Coordination conventions

- Each team member owns a "region" of the map (large grid quadrant).
- Use `World Partition Editor` window's "Show Only My Cells" filter.
- Daily sync; never let conflict count exceed ~10 actors.

### Adopting on existing maps

`ConvertLevelsToExternalActorsCommandlet` — splits monolithic `.umap` into
OFPA files. Irreversible without losing edit history.

---

## Runtime Hash Grids (Streaming Grids)

Source: `Engine/Source/Runtime/Engine/Public/WorldPartition/RuntimeHashSet/`

WP assigns each actor to a cell based on the actor's bounds. Cells form a
grid; the grid's cell size is configured per-WorldPartition.

### Cell size tuning

| Map scale | Cell size | Loading range |
|---|---|---|
| Small (1km²) — DuckSoul combat arena | 4096 cm (40m) | 8192 cm |
| Medium (4km²) — DuckSoul main island | 25600 cm (256m) | 51200 cm |
| Large (16km²+) — Open world | 51200+ cm | 102400+ cm |

Smaller cells: more cells, finer streaming, higher cell-count overhead.
Larger cells: chunkier streaming pops, lower overhead.

### Multiple grids (advanced)

A single WP can have multiple grids (different cell sizes per layer):
high-detail vegetation grid (small cells) + static-mesh grid (large cells).
Configure via World Settings → World Partition → Runtime Hash.

---

## Data Layers

Source: `WorldPartition/DataLayer/DataLayerAsset.h`

Two flavors:

| Type | Authoring | Runtime behavior |
|---|---|---|
| Editor Layer | Organize actors in editor UI | Always loaded; no runtime effect |
| Runtime Layer | Same UI | Conditionally loaded; can gate by gameplay state |

### Create a Runtime Data Layer

1. Content Browser → New → Data Layer Asset → `DL_Runtime_DayPhase`.
2. Set Type = `Runtime`.
3. Assign actors: in Outliner, drag actors into the Data Layer in the Data Layers panel.

### Runtime activation

```cpp
UDataLayerManager* DLMgr = UDataLayerManager::GetDataLayerManager(GetWorld());
DLMgr->SetDataLayerRuntimeState(
    DataLayerAsset, EDataLayerRuntimeState::Activated);

// or by instance name
const UDataLayerInstance* DL = DLMgr->GetDataLayerInstanceFromName(FName("DL_Runtime_DayPhase"));
DLMgr->SetDataLayerInstanceRuntimeState(DL, EDataLayerRuntimeState::Loaded);
```

States: `Unloaded` → `Loaded` (in memory, not visible) → `Activated` (visible).

### DuckSoul recipe — gate combat arena Data Layer

```cpp
void ADuckSoulGameMode::HandleShopHubToCombat()
{
    UDataLayerManager* DLMgr = UDataLayerManager::GetDataLayerManager(GetWorld());
    DLMgr->SetDataLayerRuntimeState(
        DL_ShopHub, EDataLayerRuntimeState::Unloaded);
    DLMgr->SetDataLayerRuntimeState(
        DL_CombatArena, EDataLayerRuntimeState::Activated);
}
```

Combine with Lyra Experience swap: layer state change synchronized with
Experience load.

---

## HLOD (Hierarchical LOD)

Distant cells use baked proxy meshes (HLOD) instead of full geometry.

### UE 5.7 → 5.8 changes

- 5.7: HLOD uses simplified static mesh proxies; Nanite-aware
- 5.8: Nanite HLOD generation hardened — proxy meshes themselves can be
  Nanite-rendered, removing the LOD switch pop
- 5.8: Instanced Static Mesh HLOD reduces draw calls for vegetation
- 5.8: Nanite fallback-mesh support aids Nanite HLOD compatibility [UNVERIFIED: no `bUseNaniteFallbackMesh` HLOD option found in UE 5.8 source]

### Generate HLODs

```bash
UnrealEditor-Cmd.exe DuckSoul.uproject -run=WorldPartitionBuilderCommandlet
    -Builder=WorldPartitionHLODsBuilder
    /Game/Maps/Main
```

Or in editor: Build → Build HLODs.

HLOD assets land under `Content/__ExternalActors__/<Map>/HLOD/`.

### HLOD layer configuration

`UHLODLayer` asset defines: proxy mesh material, screen size threshold,
generation method (Mesh Merge / Instancing / None).

Assign per-actor: Details → World Partition → HLOD Layer.

---

## Streaming Source Customization

Default: every possessed APawn has an implicit streaming source.

### Custom streaming source

```cpp
UCLASS()
class UDuckSoulCameraStreamingSource : public UWorldPartitionStreamingSourceComponent
{
    GENERATED_BODY()
public:
    UDuckSoulCameraStreamingSource(const FObjectInitializer& OI);
};

UDuckSoulCameraStreamingSource::UDuckSoulCameraStreamingSource(const FObjectInitializer& OI)
    : Super(OI)
{
    // EnableStreamingSource()/DisableStreamingSource() toggle bStreamingSourceEnabled at runtime.
    Priority = EStreamingSourcePriority::High;
    // Add an FStreamingSourceShape to the Shapes array for a custom radius;
    // if empty, the source falls back to a sphere with the grid's loading range.
}
```

Attach to camera Actor to stream cells along the camera view rather than the
Pawn position — useful for top-down RTS or cinematic preview.

### Multiple sources

Each LocalPlayer's Pawn can be a source. Co-op uses multiple sources; cells
load if ANY source overlaps.

---

## Level Instance vs Deprecated Sublevels

UE 5.8 deprecates legacy `ULevelStreaming` sublevels for World Partition maps.
**Level Instance** replaces them.

### Level Instance

`ULevelInstance` actor: a reference to another `.umap` instantiated at a
transform. Contents are baked into the parent map at cook (one cook unit).

```cpp
// Spawn at runtime — rare; usually placed in editor
ULevelInstanceSubsystem* LISub = UWorld::GetSubsystem<ULevelInstanceSubsystem>(GetWorld());
FLevelInstanceID ID = LISub->RegisterLevelInstance(LevelInstanceAsset);
```

### Use cases

| Use case | Pattern |
|---|---|
| Reusable shop interior | Make `LI_ShopInterior.umap` → place in main map at each shop location |
| Procgen variant rooms | Pool of LIs swapped at runtime |
| Multi-developer concurrent editing | Each owns a Level Instance asset; edits in isolation |

DuckSoul recipe: convert prototype Shop level to a Level Instance, instantiate
in MainMap at the shop door, gate visibility via Data Layer.

### Migration from sublevels

UE 5.8 still loads legacy sublevels in non-WP maps. In WP maps: not supported.
Convert a sublevel's actors into a Level Instance via the editor
(select actors → **Create Level Instance From Selection**, backed by
`ULevelInstanceSubsystem`) — there is no batch sublevel→Level Instance commandlet.

---

## WorldPartitionBuilderCommandlet

Build various derived data from CLI:

```bash
UnrealEditor-Cmd.exe DuckSoul.uproject -run=WorldPartitionBuilderCommandlet
    -Builder=<BuilderName> /Game/Maps/<MapName>
```

| Builder | Purpose |
|---|---|
| `WorldPartitionHLODsBuilder` | Generate HLOD proxy meshes |
| `WorldPartitionNavigationDataBuilder` | Bake navmesh per cell |
| `WorldPartitionMiniMapBuilder` | Generate 2D minimap texture |
| `WorldPartitionRenameDuplicateBuilder` | Bulk rename actors |

Integrate into CI: each builder is reproducible without editor UI.

---

## Debugging

### Console commands

```
wp.Runtime.ToggleDrawStreamingSources
wp.Runtime.ToggleDrawRuntimeHash2D
wp.Runtime.ToggleDrawRuntimeHash3D
wp.Runtime.DumpStreamingSources
wp.Runtime.DumpDataLayers
wp.Runtime.ToggleDrawDataLayers
```

These are toggles (no `1`/`0` argument). HLOD visualization has no `wp.HLOD.*`
console command — toggle it in the World Partition editor ("Show HLODs").

`wp.Runtime.ToggleDrawRuntimeHash2D` shows cell boundaries colored by state:
green = loaded, yellow = loading, red = unloaded but in range.

### Editor windows

- **World Partition Editor** (Window → World Partition → World Partition Editor) —
  see cells, filter by layer, force load/unload
- **Data Layers** panel — view all data layer assignments
- **Outliner > World Partition** filter — only show actors in current view

### Log categories

```ini
[Core.Log]
LogWorldPartition=Verbose
LogHLODBuilder=Log
```

---

## Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| OFPA conflict every commit | Multiple devs editing same actor | Region-ownership convention + P4 checkout |
| Actor not streaming in | Wrong grid assignment / outside grid bounds | Verify actor bounds; force load via WP Editor |
| Runtime Data Layer ignored | Used Editor Layer instead | Recreate as Runtime Data Layer |
| HLODs flicker / pop | Threshold mismatch with grid loading range | Match HLOD screen-size to cell streaming range |
| Build → HLOD takes hours | Per-actor proxy generation; not cached | Use shared DDC; incremental rebuild for changed actors only |
| Level Instance edits not visible | Forgot to save the LI source `.umap` | Save the LI map AND parent map |
| Nav not loading | Per-cell navmesh not built | Run `WorldPartitionNavigationDataBuilder` |
| Streaming source not registering | `bStreamingSourceEnabled=false` | Set true; check Priority |
| Cooked build missing actors | Data Layer state not set on cook | `WorldPartitionResaveActorsBuilder` to bake state |
| Map open is slow | Editor loads ALL OFPA actors on open | Use "Load Region" — load only visible region in editor |

---

## DuckSoul Recipe — Combat Arena as Level Instance + Data Layer Gating

1. `LI_CombatArena.umap` — author the arena in isolation.
2. Place `ULevelInstance` actor in `Main.umap` at intended spawn transform.
3. Assign to `DL_Runtime_CombatArena` (Runtime Data Layer).
4. Initial state in WorldSettings: `DL_Runtime_CombatArena → Unloaded`.
5. When `OnExperienceLoaded(B_Experience_CombatArena)`:
   ```cpp
   DLSub->SetDataLayerInstanceRuntimeState(
       DL_Runtime_CombatArena, EDataLayerRuntimeState::Activated);
   ```
6. Player enters door → walks into the streaming range → cells with arena
   actors load.
7. On Experience swap back to ShopHub: deactivate the arena Data Layer.

---

## Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new[] {
    "Core", "CoreUObject", "Engine"
});
// World Partition is engine-built, no extra plugin required.
// For Level Instance subsystem access:
PrivateDependencyModuleNames.Add("LevelInstance");
```

---

## Related Skills

- `ue-world-level-streaming` — legacy sublevel streaming + soft refs. Load
  FIRST for non-WP maps; this skill covers the WP path.
- `ue-procedural-generation` — PCG works inside WP maps but interacts with
  streaming sources for partition-driven generation.
- `ue-lyra-experience` — Experience swap commonly toggles Data Layers.
- `ue-build-cook-package` — `MapsToCook` includes WP maps; cooker bakes
  cell-level data.
- `ue-ai-navigation` — per-cell navmesh built via WP builder commandlet.
