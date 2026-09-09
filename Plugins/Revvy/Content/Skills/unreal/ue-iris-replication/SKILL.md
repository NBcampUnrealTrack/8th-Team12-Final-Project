---
name: ue-iris-replication
description: "Use this skill when evaluating or adopting Iris Replication in UE 5.5+: replication systems and bridges, fragments, filters, prioritizers, push-model dirty marking, legacy DOREPLIFETIME compatibility, net.Iris diagnostics, and incremental migration for opt-in projects."
unreal_version: "5.8"
feature_status: production
status_verified: "2026-07-30"
metadata:
  version: 1.0.1
---

# UE Iris Replication

You are an expert in Iris Replication — UE 5.5+ rewrite of the network replication system, hardened in 5.8. It runs in parallel to the legacy replication path and is opt-in per project.

## Context Check

Read `.agents/ue-project-context.md` first. Confirm:

- UE 5.5+ (5.8 has the production hardening)
- Whether `[/Script/Engine.Engine] +IrisNetDriverConfigs=(...)` opts a
  NetDriver into Iris
- Whether `net.Iris.UseIrisReplication=1` is set in `DefaultEngine.ini`
- Whether the project still uses any `DOREPLIFETIME_WITH_PARAMS` macros
- Existing `Iris` plugin enablement (`IrisCore` / `NetCore` are engine
  modules, not plugins — added as Build.cs dependencies)
- `UseNetworkPredictionExtras` may also be relevant if Mover is involved

## Information to Gather

1. Are you adopting Iris project-wide or per-class opt-in?
2. Push-model only (recommended for new code) or mixed legacy + push?
3. Which classes need custom filters (visibility / interest management)?
4. Are there custom replicator structs (FastArraySerializer, etc.)?
5. Compatibility constraint — does any third-party plugin assume legacy
   path (e.g., GAS replication)?

---

## Iris vs Legacy Replication

| Aspect | Legacy (`NetDriver`, `UNetConnection`) | Iris (`UReplicationSystem`) |
|---|---|---|
| Property tracking | Per-tick poll all `Rep_` properties | Push-model — only marked-dirty props re-checked |
| Subobject replication | Manual `ReplicateSubobjects` | Automatic via Bridge + fragments |
| Filter model | NetCullDistance, dormancy, ad-hoc | `UNetObjectFilter` plug-in system |
| Prioritizer | DormancyHandler, NetUpdateFrequency | `UNetObjectPrioritizer` plug-in system |
| Bandwidth model | Per-actor channel saturation | Per-connection cost-budgeting |
| Profiling | NetTrace (legacy categories) | NetTrace (Iris-specific events + `net.Iris.PrintReplicationInfo`) |
| Code-gen | UHT generates RepNotify wrappers | UHT generates replication fragments from `UPROPERTY(Replicated)` |
| Compatibility | `DOREPLIFETIME` works | Same `DOREPLIFETIME` works (auto-bridged), but loses push-model benefit |

**Coexistence**: Iris and legacy can run in the same world per-actor. Project
default chosen by NetDriver. Opt-in per class via
`bReplicateUsingRegisteredSubObjectList` and bridge registration.

---

## Enable Iris in a Project

`DefaultEngine.ini`:

```ini
[/Script/Engine.Engine]
; There is no dedicated Iris NetDriver class in 5.8 — you opt an existing
; NetDriver definition into Iris. Match by definition, name, or wildcard.
+IrisNetDriverConfigs=(NetDriverDefinition="GameNetDriver",bCanUseIris=true)

[ConsoleVariables]
net.Iris.UseIrisReplication=1

[SystemSettings]
net.Iris.UseIrisReplication=1
```

Command line alternative: `-UseIrisReplication=1`.

`.uproject` plugins (`IrisCore` / `NetCore` are always-built engine modules —
enable the `Iris` plugin, not them):

```json
{ "Name": "Iris", "Enabled": true }
```

Verify at runtime: `LogNet: Using replication system: Iris`.

---

## Push-Model Properties

The big win is push-model. Property changes only re-check the property when
explicitly marked dirty.

### Declare push-model

```cpp
UCLASS()
class ADuckSoulItem : public AActor
{
    GENERATED_BODY()
public:
    UPROPERTY(ReplicatedUsing=OnRep_Quantity)
    int32 Quantity = 0;

    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutProps) const override
    {
        Super::GetLifetimeReplicatedProps(OutProps);

        // Push-model variant of DOREPLIFETIME
        FDoRepLifetimeParams Params;
        Params.bIsPushBased = true;
        DOREPLIFETIME_WITH_PARAMS_FAST(ADuckSoulItem, Quantity, Params);
    }

    UFUNCTION() void OnRep_Quantity();

    void SetQuantity(int32 NewQuantity)
    {
        if (Quantity != NewQuantity)
        {
            Quantity = NewQuantity;
            MARK_PROPERTY_DIRTY_FROM_NAME(ADuckSoulItem, Quantity, this);
        }
    }
};
```

`bIsPushBased=true` opts the property out of polling. You MUST call
`MARK_PROPERTY_DIRTY_FROM_NAME(ClassName, PropName, Owner)` after every
assignment — otherwise replication never sends the change.

### Why push-model

Legacy: every tick, NetDriver compares last-sent value vs current for every
replicated property on every replicated actor. O(props × actors × tick rate).

Push-model: only marked-dirty properties are compared. For a project with
hundreds of replicated actors and rarely-changing properties (inventory, stats),
the bandwidth/CPU saving is significant.

---

## UObjectReplicationBridge + Fragments

Iris uses Replication Fragments — typed adapters that explain how to
serialize a property group. Code-gen produces them from `UPROPERTY(Replicated)`.

```cpp
// UHT auto-generates fragments for UPROPERTY(Replicated). To register
// fragments manually, override UObject::RegisterReplicationFragments
// (called when the object is added to the ReplicationSystem):
void ADuckSoulItem::RegisterReplicationFragments(
    UE::Net::FFragmentRegistrationContext& Context,
    UE::Net::EFragmentRegistrationFlags RegistrationFlags)
{
    UE::Net::FReplicationFragmentUtil::CreateAndRegisterFragmentsForObject(
        this, Context, RegistrationFlags);
}
```

For custom-serialized properties (`FastArraySerializer`-style), implement
`UE::Net::FReplicationFragment` directly. See
`Iris/ReplicationSystem/ReplicationFragmentUtil.h` (IrisCore module).

---

## Filters (UNetObjectFilter)

Filters decide which connections see which replicated objects. Plug-in
architecture — register, configure, attach.

### Built-in filters

| Filter | Class | Purpose |
|---|---|---|
| Connection | `UNetObjectConnectionFilter` | Per-connection allow/deny |
| Spatial (grid) | `UNetObjectGridFilter` | Distance/AOI grid culling |
| Spatial (world loc) | `UNetObjectGridWorldLocFilter` | Grid culling keyed on world location |
| Parent-relevancy grid | `UNetObjectParentRelevancyGridFilter` | Grid culling following parent relevancy |

Owner-only visibility is handled by lifetime conditions (`COND_OwnerOnly`),
which Iris maps automatically — there is no dedicated owner filter class.

### Attach a filter

```cpp
UReplicationSystem* RepSystem =
    UE::Net::FReplicationSystemUtil::GetReplicationSystem(this);
// FNetObjectFilterHandle from RepSystem->GetFilterHandle(FName("MyGridFilter"))
RepSystem->SetFilter(RefHandle, GridFilterHandle, /*FilterConfigProfile=*/NAME_None);
```

The legacy `NetCullDistance` becomes a configured `UNetObjectGridFilter`.

### Custom filter

Subclass `UNetObjectFilter`, override `PreFilter`, `Filter`, `PostFilter`.
Use for interest management driven by gameplay (e.g., team-based visibility).

---

## Prioritizers (UNetObjectPrioritizer)

Replace dormancy. Each replicated object has a priority per tick per
connection. High-priority objects send first; budget-constrained ones defer.

```cpp
// The built-in distance-based spatial prioritizer is referenced by handle.
UReplicationSystem* RepSystem =
    UE::Net::FReplicationSystemUtil::GetReplicationSystem(this);
RepSystem->SetPrioritizer(RefHandle, DefaultSpatialNetObjectPrioritizerHandle);

// Or a fixed priority (Actor-level helper):
UE::Net::FReplicationSystemUtil::SetStaticPriority(this, 1.0f);
```

Prioritizers subclass `UNetObjectPrioritizer` and are registered via
`UNetObjectPrioritizerDefinitions`. [UNVERIFIED: exact DefaultEngine.ini config
section name for per-prioritizer tuning.]

---

## Compatibility Shims

`DOREPLIFETIME(ClassName, PropName)` still works under Iris — the system
auto-bridges to a polled fragment. You lose push-model benefits but old code
keeps replicating.

GAS (`UAbilitySystemComponent`) has Iris-compatibility code added in 5.4+;
abilities replicate correctly under Iris in 5.8.

Plugins that override `NetDriver` (custom transports) may not work with Iris
NetDriver — verify with vendor.

---

## Debugging

### Console commands

```
Net.Iris.PrintReplicatedObjects
Net.Iris.PrintRelevantObjects
Net.Iris.PrintNetCullDistances
Net.Iris.PrintPushBasedStatuses
```

### NetTrace (Unreal Insights)

```
Trace.Start net,iris
```

Iris-specific events: `IrisReplicate`, `IrisFilter`, `IrisPrioritize`,
`IrisPropertyDirty`. Compare bandwidth pre/post-push-model conversion.

### Logging

```ini
[Core.Log]
LogIris=Verbose
LogIrisNetCull=Verbose
LogIrisFiltering=Log
```

---

## Migration Recipe

### Phase 1 — Enable, keep legacy semantics

1. Add plugins + NetDriverDefinitions config.
2. Set `net.Iris.UseIrisReplication=1`.
3. Boot PIE listen-server, smoke-test: shop, combat, character movement.
4. `LogNet` should report Iris usage.

### Phase 2 — Convert hot properties to push-model

1. Identify high-rate replicated properties (`Net.Iris.PrintPushBasedStatuses`).
2. Replace `DOREPLIFETIME(Class, Prop)` with
   `DOREPLIFETIME_WITH_PARAMS_FAST(Class, Prop, Params)` and `bIsPushBased`.
3. Add `MARK_PROPERTY_DIRTY_FROM_NAME` at every assignment site.
4. Compile, test in PIE — missing MARK means the property silently stops
   replicating. Use `net.PackageMap.DebugObject` to confirm.

### Phase 3 — Add filters / prioritizers for scale

1. Replace ad-hoc `NetCullDistance` with `UNetObjectGridFilter`.
2. Owner-only replication (inventory, ammo) → keep `COND_OwnerOnly`; Iris maps lifetime conditions automatically.
3. Crowd-scenarios (NPC swarms) → spatial prioritizer with budget cap.

### Phase 4 — Measure

Compare bandwidth via NetTrace; commit when reduction confirmed.

---

## Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| Push-model property never updates remote | Missing `MARK_PROPERTY_DIRTY_FROM_NAME` | Add at every write site; build a setter wrapper |
| `MARK_PROPERTY_DIRTY` on non-push property | Mixing legacy `DOREPLIFETIME` with `MARK_*` | Either fully convert or stop calling MARK |
| Replicated array push-model partially works | Arrays need element-level dirty tracking (FastArraySerializer pattern) | Use `FFastArraySerializer` or accept full-array push |
| Filter never fires | Filter attached before world init | Attach in `BeginPlay` or after `BeginReplication` |
| RPC ordering changed | Iris uses different RPC pipeline | Don't rely on undefined inter-RPC ordering; add sequence numbers for protocols that need it |
| `AbilitySystemComponent` replication breaks | Old GAS version without Iris compat | Update to UE 5.4+ engine GAS |
| Custom NetDriver subclass crashes | NetDriver expects legacy hooks | Opt the NetDriver into Iris via `+IrisNetDriverConfigs` instead of subclassing; verify custom transports support Iris |
| Listen-server "ghost" actor on client | Bridge not registered before BeginReplication | Register class fragments in `StartupModule` |
| Migration breaks RepNotify on uint8 enum | Enum repnotify quirk in Iris early versions | Use `int32` underlying type or update engine |

---

## Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new[] {
    "Core", "CoreUObject", "Engine",
    "NetCore", "IrisCore"   // engine modules, provide the Iris types
});
```

`.uproject` (enable the `Iris` plugin; `IrisCore`/`NetCore` are engine
modules, not plugins):

```json
{ "Name": "Iris", "Enabled": true }
```

---

## Related Skills

- `ue-networking-replication` — legacy replication semantics (DOREPLIFETIME,
  RPC types, NetRole). **Load FIRST** — Iris extends those concepts.
- `ue-gameplay-abilities` — GAS replication. Iris-compatible from 5.4 but
  verify ASC behavior in PIE.
- `ue-async-threading` — push-model often interacts with async-task
  dirty-marking; thread-safety matters when `MARK_*` is called off game
  thread (it must not — call on game thread or queue).
- `ue-online-subsystem-eos` — EOS sits below NetDriver; Iris vs legacy is
  orthogonal to EOS transport.
- `ue-mover-plugin` — Mover prediction is independent of Iris; both can
  coexist on the same Pawn.
