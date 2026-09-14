---
name: ue-subsystems-taxonomy
description: "Use this skill as a decision guide when choosing between UEngineSubsystem, UGameInstanceSubsystem, UWorldSubsystem, ULocalPlayerSubsystem, and UEditorSubsystem: lifetime matrix, tick ability, replication constraints, PIE bleed prevention, GameInstance-scope vs World-scope pitfalls, and boilerplate cheat-sheet. Load this FIRST whenever the choice of subsystem class is unclear (UI / Messaging / Input / Editor / per-player state)."
metadata:
  version: 1.0.1
---

# UE Subsystems Taxonomy

You are an expert in UE's `USubsystem` hierarchy. Subsystems are auto-instantiated singleton-like services bound to a specific lifetime scope. Picking the wrong scope is the #1 source of state-leak bugs.

## Context Check

Read `.agents/ue-project-context.md` first. Confirm:

- The state you want to store — what lifecycle does it match?
- Whether you need editor-only behavior (`UEditorSubsystem`)
- Whether the state must survive PIE end (don't — that's the bug to avoid)
- Whether you need replication (subsystems don't replicate — pick a different class)

## Information to Gather

1. How long should the state live? — engine boot to exit, game session, world, per-player, editor session
2. Should it tick? — only `UWorldSubsystem` and `UTickableWorldSubsystem` tick reliably
3. Does it need to replicate? — never use a subsystem; use `AGameState`/`APlayerState`
4. Editor-only or runtime?
5. Multiple instances ever (split-screen)? — only `ULocalPlayerSubsystem` is per-instance

---

## Lifetime Matrix

```
Process boot                                                    Process exit
   │                                                                  │
   │  UEngineSubsystem ────────────────────────────────────────────►  │
   │     │                                                            │
   │     │  UEditorSubsystem (editor builds only) ──────────────►     │
   │     │                                                            │
   │   GI start                                              GI end   │
   │     │  UGameInstanceSubsystem ─────────────────────────►         │
   │     │                                                            │
   │     │   World A load                  World A unload  World B... │
   │     │     │ UWorldSubsystem ───────►    │                        │
   │     │     │                             │ (new World subsystem)  │
   │     │     │   LP1 join         LP1 leave│                        │
   │     │     │     │ ULocalPlayerSubsystem │                        │
   │     │     │     │ (one per LocalPlayer) │                        │
```

### Lifetime / capability summary

| Subsystem | Lifetime | Tick | Replicates | Instances | Editor? |
|---|---|---|---|---|---|
| `UEngineSubsystem` | Engine boot → exit | No native tick | No | One per process | Yes (always live) |
| `UEditorSubsystem` | Editor open → close | No | No | One per editor | Editor only |
| `UGameInstanceSubsystem` | GI start → end (survives map travel) | No | No | One per GameInstance | Yes (PIE has GI) |
| `UWorldSubsystem` | World load → unload | No (but `UTickableWorldSubsystem` does) | No | One per world | Yes (each PIE world) |
| `UTickableWorldSubsystem` | Same as World | **Yes** (`Tick(DeltaTime)`) | No | Same | Same |
| `ULocalPlayerSubsystem` | LocalPlayer create → destroy | No | No | **One per LocalPlayer** (split-screen multi) | Yes |

---

## Decision Tree

```
Does it need to TICK every frame?
├─ Yes → UTickableWorldSubsystem
└─ No
   ├─ Per-player state (split-screen, Enhanced Input contexts, per-controller UI)?
   │  └─ ULocalPlayerSubsystem
   ├─ World-scoped (level streaming, AOI, world-time)?
   │  └─ UWorldSubsystem
   ├─ Survives map travel within a game session (HUD layout, save data, audio mix)?
   │  └─ UGameInstanceSubsystem
   ├─ Editor-only (asset registry callbacks, custom toolbar)?
   │  └─ UEditorSubsystem
   └─ Engine-process-global (cross-PIE singletons, file I/O service)?
      └─ UEngineSubsystem  (rarely the right answer — usually GI is enough)
```

**Tie-breakers**:
- "I might need it later in editor and runtime" → GameInstance (covers both PIE and runtime).
- "I want it per-PIE-session not per-world" → GameInstance (PIE creates one GI per session).
- "I want clean restart on PIE-end" → GameInstance or World, never Engine.

---

## Replication Constraint

**Subsystems never replicate.** They are local-only services.

If you need replicated state:
- Game-mode logic → `AGameMode` (server-only)
- Game-session state → `AGameState` (replicated)
- Per-player state → `APlayerState` (replicated)
- Per-pawn state → `UActorComponent` on the Pawn (replicated)

Common mistake: putting "current shop balance" on `UGameInstanceSubsystem` —
clients can't see the server's value. Move to `APlayerState`.

---

## Tick Ability

Only `UWorldSubsystem` derivatives can tick reliably.

```cpp
UCLASS()
class UDuckSoulAOIWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override
    { RETURN_QUICK_DECLARE_CYCLE_STAT(UDuckSoulAOIWorldSubsystem, STATGROUP_Tickables); }

    // Optional — gate ticking
    virtual bool IsTickable() const override
    { return GetWorld() && GetWorld()->IsGameWorld(); }
};
```

For non-tick subsystems that need periodic work, use `FTimerManager` from a
World source (`GetWorld()->GetTimerManager()`).

---

## Boilerplate Cheat-Sheets

### UGameInstanceSubsystem

```cpp
UCLASS()
class UDuckSoulMessagingSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override
    {
        Super::Initialize(Collection);
        // Collection.InitializeDependency(UOtherSubsystem::StaticClass());
    }
    virtual void Deinitialize() override
    {
        Super::Deinitialize();
    }
};

// Access:
auto* Sub = UGameInstance::GetSubsystem<UDuckSoulMessagingSubsystem>(GetGameInstance());
auto* Sub2 = WorldContextObject->GetWorld()->GetGameInstance()
    ->GetSubsystem<UDuckSoulMessagingSubsystem>();
```

### UWorldSubsystem

```cpp
UCLASS()
class UDuckSoulWorldClock : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Decide if this subsystem should be created for a given world type
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override
    { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }
};

// Access:
auto* Sub = GetWorld()->GetSubsystem<UDuckSoulWorldClock>();
```

### ULocalPlayerSubsystem

```cpp
UCLASS()
class UDuckSoulPlayerHUDSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
};

// Access:
auto* Sub = LocalPlayer->GetSubsystem<UDuckSoulPlayerHUDSubsystem>();
auto* Sub2 = ULocalPlayer::GetSubsystem<UDuckSoulPlayerHUDSubsystem>(
    PlayerController->GetLocalPlayer());
```

### UEditorSubsystem

```cpp
UCLASS()
class UDuckSoulEditorToolsSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
};

// Access (editor only):
auto* Sub = GEditor->GetEditorSubsystem<UDuckSoulEditorToolsSubsystem>();
```

`UEditorSubsystem` depends on `GEditor`. Wrap in `#if WITH_EDITOR` in shared
modules.

### UEngineSubsystem

```cpp
UCLASS()
class UDuckSoulFileServiceSubsystem : public UEngineSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
};

// Access:
auto* Sub = GEngine->GetEngineSubsystem<UDuckSoulFileServiceSubsystem>();
```

Rarely needed. Most "engine-level" state can live on GameInstance.

---

## Case Studies

### UGameplayMessageSubsystem — UGameInstanceSubsystem

**Why**: messages fan-out across multiple worlds (PIE) but within one game
session. Listeners outlive single-world swaps (level travel). No tick needed.

If it were on `UWorldSubsystem`, level travel would lose all listeners
mid-session. If on `UEngineSubsystem`, PIE sessions would leak listeners
across PIE restarts within the same editor run.

### ULyraUIManagerSubsystem — UGameInstanceSubsystem

**Why**: UI layout (PrimaryGameLayout) persists across level loads within a
game session. Per-LocalPlayer layouts live underneath but the manager is
GameInstance-scope so it can route Push to the right player's layout.

### UEnhancedInputLocalPlayerSubsystem — ULocalPlayerSubsystem

**Why**: input mapping contexts are per-controller. Split-screen player 1 vs
player 2 have completely independent IMC stacks. `ULocalPlayerSubsystem` is
the only choice that respects multiple instances.

### Asset preloader — UEngineSubsystem (rare)

**Why**: when the goal is to keep an object alive across editor PIE
start/stop and across actual game runs in shipping builds. Even then,
prefer `UGameInstanceSubsystem` unless you've profiled the load cost.

---

## Anti-Patterns

| Anti-pattern | Why it breaks | Replace with |
|---|---|---|
| World state on `UGameInstanceSubsystem` | Lingers across worlds — PIE bleed: "the value from level 1 still here in level 2" | `UWorldSubsystem` |
| Per-player state on `UGameInstanceSubsystem` | Breaks split-screen (one shared value for two players) | `ULocalPlayerSubsystem` |
| Replicated state on any subsystem | Subsystems don't replicate — only local PC sees changes | `AGameState`/`APlayerState`/`UActorComponent` |
| Tick on `UGameInstanceSubsystem` | No native tick hook; you have to bind `FTSTicker::GetCoreTicker()` manually | `UTickableWorldSubsystem` |
| Editor work in runtime subsystem | `WITH_EDITOR` guards everywhere; symbol bloat | `UEditorSubsystem` for editor; pair with runtime sub if both needed |
| Save data on `UWorldSubsystem` | Lost on level travel | `UGameInstanceSubsystem` or `USaveGame` |
| Audio mix state on `UWorldSubsystem` | Mix resets each level — abrupt music change | `UGameInstanceSubsystem` |
| Singleton service on `UEngineSubsystem` when GI is enough | Survives PIE end → state from previous PIE leaks | `UGameInstanceSubsystem` |

---

## Initialize Dependency Order

```cpp
virtual void Initialize(FSubsystemCollectionBase& Collection) override
{
    // Force ordering: my subsystem after the dependency is up
    Collection.InitializeDependency(UDependencySubsystem::StaticClass());
    Super::Initialize(Collection);
    // Now safe to fetch the dependency
    auto* Dep = GetGameInstance()->GetSubsystem<UDependencySubsystem>();
}
```

Without `InitializeDependency`, subsystem init order is not guaranteed.

---

## ShouldCreateSubsystem (Conditional Activation)

```cpp
virtual bool ShouldCreateSubsystem(UObject* Outer) const override
{
    // Don't create on dedicated server, or only in shipping, etc.
    // (Outer's World is usually not set yet here — don't call GetWorld())
    if (IsRunningDedicatedServer())
        return false;
    return Super::ShouldCreateSubsystem(Outer);
}
```

Returning `false` prevents instantiation entirely.

---

## Debugging

### Inspect alive subsystems

```cpp
TArray<UGameInstanceSubsystem*> Subs =
    GetGameInstance()->GetSubsystemArrayCopy<UGameInstanceSubsystem>();
for (UGameInstanceSubsystem* S : Subs) UE_LOG(LogTemp, Log, TEXT("%s"), *S->GetName());
```

### Watch initialization

```ini
[Core.Log]
LogSubsystemCollection=VeryVerbose
```

`Verbose` logs collection initialization; `VeryVerbose` adds per-subsystem
creation/dependency decisions.

---

## Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| Subsystem missing in PIE | A custom `DoesSupportWorldType` override excluded PIE (default supports Game/PIE/Editor) | Include PIE in your `DoesSupportWorldType` override |
| Static cache leak across PIE runs | `static TMap<>` inside subsystem method | Use UPROPERTY member, not static |
| Partially-initialized dependency | Two subsystems call `InitializeDependency` on each other — the second call returns the first subsystem before its Initialize finished | Break the cycle; only one direction allowed |
| Cannot find subsystem from BP | Not marked `BlueprintType` | Add `UCLASS(BlueprintType)` |
| Subsystem ticked too early | `UTickableWorldSubsystem::IsTickable` returned true before world ready | Gate on `GetWorld()->bMatchStarted` or similar |
| Engine subsystem reads from GEditor in -game builds | `GEditor == nullptr` outside editor | Use `UEditorSubsystem` instead; never mix |
| Multiple instances in split-screen of GameInstance sub | Misunderstood scope | Use LocalPlayer subsystem for per-player |

---

## Related Skills

- `ue-gameplay-message-subsystem` — concrete `UGameInstanceSubsystem` example.
- `ue-common-ui` — `ULyraUIManagerSubsystem` is `UGameInstanceSubsystem`.
- `ue-input-system` — `UEnhancedInputLocalPlayerSubsystem` is the canonical
  `ULocalPlayerSubsystem`.
- `ue-editor-tools` — `UEditorSubsystem` patterns for toolbar / asset hooks.
- `ue-gameplay-framework` — when subsystems are NOT the right answer
  (replicated state goes on GameState/PlayerState).
