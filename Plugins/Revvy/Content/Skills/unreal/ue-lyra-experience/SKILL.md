---
name: ue-lyra-experience
description: "Use this skill when authoring or extending Lyra-style Experience composition: Experience definitions and manager components, action sets, Pawn data, async-load gating, per-experience GameFeature activation, and modular Pawn composition."
metadata:
  version: 1.1.0
---

# UE Lyra Experience System

Use Lyra's Experience composition system as a modular gameplay-mode pipeline. Verify that
the current project actually derives from Lyra before using Lyra-prefixed classes.

## Context Check

Read `.agents/ue-project-context.md` first. Confirm:

- Engine version is 5.8 (Lyra-based)
- `GameFeatures` and `ModularGameplay` plugins are enabled (they must be)
- Which Game Feature plugins exist under `Plugins/GameFeatures/`
- Whether `ULyraExperienceManagerComponent` lives on `ALyraGameState` or a project-specific subclass
- Existing `ULyraExperienceDefinition` assets under `Content/Experiences/`
- Default experience resolution chain (URL option → DevOptions → command line → world settings)

## Information to Gather

1. New Experience for a new mode, or extending an existing one (shop, combat, hub)?
2. Which Game Feature plugins must be enabled for this Experience?
3. Pawn class needed — reuse the current default Pawn or use a mode-specific Pawn?
4. AbilitySets/InputConfig/TaggedAbilityBindings required on the Pawn?
5. Does this Experience need ActionSets (shared composable bundles) or is the Actions list flat?
6. Replicated or local-only? Experience is replicated by ManagerComponent — clients wait.

---

## Experience Lifecycle

```
ALyraGameMode::InitGame
  └── ResolveExperience (URL option > DevOptions > CmdLine > WorldSettings)

ALyraGameMode::HandleMatchAssignmentIfNotExpectingOne
  └── Server: SetCurrentExperience(AssetId)
      └── ULyraExperienceManagerComponent::ServerSetCurrentExperience
          ├── StartExperienceLoad (FStreamableManager bundle preload)
          ├── OnExperienceLoadComplete       [bundles ready]
          ├── ActivateGameFeaturePlugins     [Plugins enable]
          ├── ExecuteActions                 [UGameFeatureAction::OnGameFeatureActivating]
          └── OnExperienceFullLoadCompleted
              └── OnExperienceLoaded.Broadcast(LoadedExperience)

Clients:
  └── ULyraExperienceManagerComponent::OnRep_CurrentExperience
      └── StartExperienceLoad (SAME full pipeline — including GameFeature plugin
          activation via LoadAndActivateGameFeaturePlugin; there is no net-role
          guard, clients activate the plugins locally too)
```

**Critical**: anything that depends on the Experience MUST register via
`CallOrRegister_OnExperienceLoaded` — never assume sync availability.

---

## ULyraExperienceDefinition

Source: `Source/LyraGame/GameModes/LyraExperienceDefinition.h`

```cpp
UCLASS(BlueprintType, Const)
class ULyraExperienceDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    // GameFeature plugin names (e.g., "ShooterCore") to enable when this
    // Experience activates. Names — NOT plugin paths.
    UPROPERTY(EditDefaultsOnly, Category = "Gameplay")
    TArray<FString> GameFeaturesToEnable;

    // Default Pawn data — PawnClass, AbilitySets, InputConfig, camera mode
    UPROPERTY(EditDefaultsOnly, Category = "Gameplay")
    TObjectPtr<const ULyraPawnData> DefaultPawnData;

    // Flat list of UGameFeatureAction subclasses to execute on activation
    UPROPERTY(EditDefaultsOnly, Instanced, Category = "Actions")
    TArray<TObjectPtr<UGameFeatureAction>> Actions;

    // Composable action bundles — applied AFTER Actions
    UPROPERTY(EditDefaultsOnly, Category = "Gameplay")
    TArray<TObjectPtr<ULyraExperienceActionSet>> ActionSets;
};
```

### Fields explained

| Field | Purpose | DuckSoul example |
|---|---|---|
| `GameFeaturesToEnable` | Plugin names to activate before Actions run | `["DuckSoulCore", "DuckSoulShop"]` |
| `DefaultPawnData` | Pawn class + AbilitySets + Input bindings | `PawnData_Duck_Combat` |
| `Actions` | Direct `UGameFeatureAction` list | `AddComponents`, `AddAbilities` |
| `ActionSets` | Reusable bundles (HUD set, scoring set) | `ActionSet_DuckSoulHUD` |

### Authoring an Experience asset

1. Create `UPrimaryDataAsset` of class `LyraExperienceDefinition` (BP child OK).
2. Place under `Content/Experiences/B_Experience_<Name>.uasset`.
3. Register with AssetManager: add primary asset type `LyraExperienceDefinition`
   in `DefaultGame.ini → [/Script/Engine.AssetManagerSettings]`.
4. Set as default via:
   - URL option: `?Experience=B_Experience_Combat`
   - DevOptions: `ULyraDeveloperSettings::ExperienceOverride`
   - Command line: `-Experience=B_Experience_Combat`
   - World settings: `ALyraWorldSettings::DefaultGameplayExperience`

---

## UExperienceActionSet (Composable Bundles)

Source: `Source/LyraGame/GameModes/LyraExperienceActionSet.h`

```cpp
UCLASS(BlueprintType, NotBlueprintable)
class ULyraExperienceActionSet : public UPrimaryDataAsset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Instanced, Category = "Actions to Perform")
    TArray<TObjectPtr<UGameFeatureAction>> Actions;

    // ActionSets can ALSO pull in GameFeature plugins — merged with the
    // Experience's own GameFeaturesToEnable during load
    UPROPERTY(EditAnywhere, Category = "Feature Dependencies")
    TArray<FString> GameFeaturesToEnable;
};
```

**Composition pattern**: an Experience holds `ActionSets[]` to reuse bundles
across multiple Experiences. E.g., `ActionSet_DuckSoulHUD` is referenced by
both `B_Experience_Combat` and `B_Experience_Shop` — change once, update both.

### Ordering rules

Action execution order is:
1. `Actions[]` of the Experience (in array order)
2. For each `ActionSet` in `ActionSets[]`: that set's `Actions[]` in order

**Implication**: if Action B depends on Action A (e.g., `AddAbilities` needs
`AddComponents` to spawn the AbilitySystemComponent first), keep them in the
same array AND order A before B. Splitting across Experience.Actions and
ActionSet.Actions is fine as long as Experience.Actions runs first.

---

## ULyraPawnData

Source: `Source/LyraGame/Character/LyraPawnData.h`

```cpp
UCLASS(BlueprintType, Const)
class ULyraPawnData : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category = "Pawn")
    TSubclassOf<APawn> PawnClass;

    // Ability sets granted on possession
    UPROPERTY(EditDefaultsOnly, Category = "Abilities")
    TArray<TObjectPtr<ULyraAbilitySet>> AbilitySets;

    // Tag → AbilityClass map for input-driven activation
    UPROPERTY(EditDefaultsOnly, Category = "Abilities")
    TObjectPtr<ULyraAbilityTagRelationshipMapping> TagRelationshipMapping;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<ULyraInputConfig> InputConfig;

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    TSubclassOf<ULyraCameraMode> DefaultCameraMode;
};
```

**No `InputMappings` field on PawnData.** IMCs (Enhanced Input mapping contexts)
live elsewhere: `UGameFeatureAction_AddInputContextMapping::InputMappings`
(`TArray<FInputMappingContextAndPriority>`) applied per-Experience, and
`ULyraHeroComponent::DefaultInputMappings` for the hero pawn's baseline set.
(DuckSoul's `ULyraPawnData` additionally carries a project-specific `Loadout`
field of `TSubclassOf<UDSEquipmentDefinition>`.)

Wired on Pawn possession via `ULyraPawnExtensionComponent`, which implements
`IGameFrameworkInitStateInterface` and drives init through
`CheckDefaultInitialization()` (the pre-5.1 `OnPawnReadyToInitialize` API no
longer exists). The component reads `PawnData` from PlayerState, grants ability
sets, binds input config, sets camera mode.

**Movement note**: Lyra 5.8 still uses the classic CharacterMovementComponent
(`ULyraCharacterMovementComponent : public UCharacterMovementComponent`), NOT the
Mover plugin.

---

## ULyraExperienceManagerComponent

Lives on `ALyraGameState`. Replicated. Drives the lifecycle.

```cpp
// API surface used outside the manager
void ULyraExperienceManagerComponent::CallOrRegister_OnExperienceLoaded(
    FOnLyraExperienceLoaded::FDelegate&& Delegate);

// Ordering variants — fired before/after the normal-priority delegates:
void CallOrRegister_OnExperienceLoaded_HighPriority(FOnLyraExperienceLoaded::FDelegate&& Delegate);
void CallOrRegister_OnExperienceLoaded_LowPriority(FOnLyraExperienceLoaded::FDelegate&& Delegate);

bool ULyraExperienceManagerComponent::IsExperienceLoaded() const;

const ULyraExperienceDefinition*
    ULyraExperienceManagerComponent::GetCurrentExperienceChecked() const;
```

### Wait-for-experience pattern (clients and late-joining components)

```cpp
void UMyComponent::BeginPlay()
{
    Super::BeginPlay();

    AGameStateBase* GS = GetWorld()->GetGameState();
    if (!GS) { /* listen for GameState set later */ return; }

    auto* ExpMgr = GS->FindComponentByClass<ULyraExperienceManagerComponent>();
    if (!ensure(ExpMgr)) return;

    ExpMgr->CallOrRegister_OnExperienceLoaded(
        FOnLyraExperienceLoaded::FDelegate::CreateUObject(
            this, &UMyComponent::OnExperienceLoaded));
}

void UMyComponent::OnExperienceLoaded(const ULyraExperienceDefinition* Exp)
{
    // Safe to read Exp->DefaultPawnData, query ability sets, spawn UI, etc.
}
```

**Never** poll `IsExperienceLoaded()` in Tick. Always register a callback.

---

## Async Loading (FStreamableManager + Bundles)

`StartExperienceLoad()` builds a `BundlesToLoad` list (no ini keys involved):

- Always: `FLyraBundles::Equipped`
- By net mode: `UGameFeaturesSubsystemSettings::LoadStateClient` when
  `GIsEditor || NetMode != NM_DedicatedServer`, and
  `UGameFeaturesSubsystemSettings::LoadStateServer` when
  `GIsEditor || NetMode != NM_Client`

The manager then calls `UAssetManager::ChangeBundleStateForPrimaryAssets` with
that bundle list for the Experience asset and all referenced ActionSets, and
preloads soft references those actions declare in `AddAdditionalAssetBundleData`.

**Why bundles?** Cooked builds only stage assets reachable via primary asset
bundle declarations. Soft references from `Actions[]` need to be added to the
Experience's bundle data or they will not be staged → runtime missing-asset.

---

## UGameFeatureAction Subclasses (Built-in)

Used inside Experience `Actions[]` or ActionSet `Actions[]`:

Note: Lyra's action classes have NO `Lyra` prefix — they are plain
`UGameFeatureAction_*` classes living in `Source/LyraGame/GameFeatures/`.

| Action | Purpose | Typical use |
|---|---|---|
| `UGameFeatureAction_AddComponents` | Add UActorComponent to Pawn/PlayerState/GameState (engine GameFeatures plugin) | AbilitySystemComponent on Pawn |
| `UGameFeatureAction_AddAbilities` | Grant `ULyraAbilitySet` to actor | Grant default abilities to Pawn |
| `UGameFeatureAction_AddInputBinding` | Add `ULyraInputConfig` mapping | Combat input config on Player |
| `UGameFeatureAction_AddInputContextMapping` | Add IMC to LocalPlayer subsystem | Default IMC |
| `UGameFeatureAction_AddWidgets` | Push CommonUI widgets to PrimaryGameLayout | HUD widget |
| `UGameFeatureAction_AddGameplayCuePath` | Register extra gameplay cue asset paths | Plugin-local GC notifies |
| `UGameFeatureAction_SplitscreenConfig` | Toggle splitscreen for the mode | Disable splitscreen in menus |
| `UGameFeatureAction_WorldActionBase` | Base for per-world activation | Custom subclass |

---

## DuckSoul Recipe — Shop Hub vs Combat Arena

### B_Experience_ShopHub

```
GameFeaturesToEnable: ["DuckSoulCore", "DuckSoulShop"]
DefaultPawnData:      PawnData_Duck_Browse  (no combat abilities)
Actions:
  - AddComponents:    InventoryComponent → Pawn
  - AddWidgets:       W_ShopHUD pushed to GameMenu layer
ActionSets:
  - ActionSet_DuckSoulCommonHUD
  - ActionSet_DuckSoulMessagingListeners
```

### B_Experience_CombatArena

```
GameFeaturesToEnable: ["DuckSoulCore", "DuckSoulCombat"]
DefaultPawnData:      PawnData_Duck_Combat (dual-sword AbilitySet)
Actions:
  - AddComponents:    LyraHealthComponent → Pawn
  - AddAbilities:     AbilitySet_DuckCombat
  - AddInputBinding:  InputConfig_Combat
  - AddWidgets:       W_CombatHUD pushed to Game layer
ActionSets:
  - ActionSet_DuckSoulCommonHUD
```

Both share `ActionSet_DuckSoulCommonHUD` — change pause menu once, both modes update.

---

## Debugging

### Log category

```
Log LogLyraExperience Verbose
```

`LogLyraExperience` (declared in `LyraLogChannels.h`) traces the whole load
pipeline — `StartExperienceLoad(CurrentExperience = ...)`, plugin activation,
and full-load completion. This is the primary debugging tool; there is no
`showdebug LyraExperience` page and no `Lyra.Experience.*` console commands.

### Forcing an Experience

- URL option: `open MyMap?Experience=B_Experience_Combat`
  (parsed via `UGameplayStatics::ParseOption(OptionsString, "Experience")`)
- Command line: `-Experience=B_Experience_Combat`

### Gameplay tag inspection

```
ShowDebug AbilitySystem
DumpGameplayTags
```

To confirm an `AddAbilities` action actually granted abilities, dump the
target ASC's GameplayCueContainer + ActivatableAbilities.

---

## Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| Experience never loads | `BuiltInInitialFeatureState != Active` for referenced GFP, or GFP not in `Plugins/GameFeatures/` discovery path | Set `Registered` minimum + verify discovery; `GameFeaturesSubsystem::LoadAndActivateGameFeaturePlugin` returns failure reason |
| Client Pawn has no abilities | Code ran before `OnExperienceLoaded` on client | Always register via `CallOrRegister_OnExperienceLoaded` |
| AddAbilities crashes on dedicated server | ASC component not yet added — order issue | Put `AddComponents` BEFORE `AddAbilities` in same array |
| Soft asset is null in cooked build | Soft reference not in primary asset bundle | Override `AddAdditionalAssetBundleData` on the Action subclass |
| Two Experiences leak components on swap | Action's `OnGameFeatureDeactivating` not symmetric | Pair every Add with explicit Remove; test by swapping experiences via console |
| Pawn possession race | Pawn spawned before Experience loaded, no abilities granted | Use `ULyraPawnExtensionComponent::CheckDefaultInitialization` — it waits for both Pawn ready AND Experience loaded |
| Async-load deadlock | GFP plugin disabled in `.uproject` but referenced in `GameFeaturesToEnable` | Enable plugin in `.uproject` or remove from list |

---

## Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new[] {
    "Core", "CoreUObject", "Engine",
    "GameFeatures", "ModularGameplay",
    "GameplayTags", "GameplayAbilities", "GameplayTasks"
});
// Lyra game module dependency only if extending ULyraExperienceDefinition itself
PrivateDependencyModuleNames.Add("LyraGame");
```

---

## Related Skills

- `ue-game-features` — UGameFeatureData, UGameFeatureAction base, plugin
  lifecycle. **Load FIRST** when working on Experiences.
- `ue-gameplay-framework` — GameMode/GameState/PlayerState; the Experience
  Manager lives on GameState.
- `ue-gameplay-abilities` — ULyraAbilitySet structure, ASC component flow.
- `ue-gameplay-message-subsystem` — Experience-scoped pub/sub; listeners
  installed via Experience Actions.
- `ue-common-ui` — `AddWidgets` action pushes Activatable widgets to
  PrimaryGameLayout.
- `ue-subsystems-taxonomy` — when to put Experience-derived state on a
  GameInstanceSubsystem vs the WorldSubsystem.
