---
name: ue-gameplay-message-subsystem
description: "Use this skill when implementing decoupled pub/sub with UGameplayMessageSubsystem: GameplayTag-channeled struct messages, listener lifetime, exact versus partial matching, Blueprint async listeners, and choosing between messages, Gameplay Cues, or direct delegates."
metadata:
  version: 1.0.1
---

# UE GameplayMessageSubsystem

You are an expert in `UGameplayMessageSubsystem` — Lyra's decoupled pub/sub pipeline. It replaces tightly coupled delegate spaghetti with `FGameplayTag`-channeled struct messages.

## Context Check

Read `.agents/ue-project-context.md` first. Confirm:

- `GameplayMessageRouter` plugin enabled (Lyra defaults yes)
- `GameplayTags` plugin enabled
- Existing `FGameplayTag` taxonomy (for example `Project.StatusEffect.Burn` or `Ability.Attack.3`)
- Whether messages need replication (they do NOT — this is local-only)

## Information to Gather

1. Publisher and listeners — same module / cross-plugin / cross-Experience?
2. Message struct fields — only what listeners need (no UObject ptrs if unsafe)
3. Channel tag — define under your feature's namespace
4. Match type — exact channel, or fan-in subtree?
5. Listener lifetime — UObject (auto-unregister on GC) or manual `Unregister()`?
6. BP-visible listener needed? Use `UAsyncAction_ListenForGameplayMessage`.

---

## Subsystem API

Source: `Plugins/GameplayMessageRouter/Source/GameplayMessageRuntime/Public/GameFramework/GameplayMessageSubsystem.h`

```cpp
UCLASS()
class UGameplayMessageSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    static UGameplayMessageSubsystem& Get(const UObject* WorldContextObject);

    template <typename FMessageStructType>
    void BroadcastMessage(FGameplayTag Channel, const FMessageStructType& Message);

    template <typename FMessageStructType>
    FGameplayMessageListenerHandle RegisterListener(
        FGameplayTag Channel,
        TFunction<void(FGameplayTag, const FMessageStructType&)>&& Callback,
        EGameplayMessageMatch MatchType = EGameplayMessageMatch::ExactMatch);

    void UnregisterListener(FGameplayMessageListenerHandle Handle);
};
```

Scope: `UGameInstanceSubsystem` — one instance per GameInstance, survives
level travel within the same session, destroyed on PIE end / process exit.

---

## Message Struct Conventions

```cpp
USTRUCT(BlueprintType)
struct FDuckSoulShopPurchaseMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<APlayerState> Purchaser = nullptr;

    UPROPERTY(BlueprintReadOnly)
    FGameplayTag ItemTag;

    UPROPERTY(BlueprintReadOnly)
    int32 Quantity = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Cost = 0;
};
```

Rules:
- `USTRUCT(BlueprintType)` for BP access via async-action node
- `UPROPERTY` everything — needed for BP and to suppress GC of UObject ptrs
  during dispatch
- **No** `NetSerialize`, no replication metadata — messages are local-only
- Prefer `TObjectPtr` over raw `*` for refs to UObjects
- Keep small — C++ listeners get it by const-ref synchronously; the BP async
  node copies it via `GetPayload`

---

## Channel Naming (FGameplayTag)

```
DuckSoul.Shop.Purchase           (specific event)
DuckSoul.Shop.Purchase.Failed
DuckSoul.Shop.Inventory.Changed

DuckSoul.Combat.Damage.Dealt
DuckSoul.Combat.Damage.Received
DuckSoul.Combat.Death

DuckSoul.Quest.Started
DuckSoul.Quest.Updated
DuckSoul.Quest.Completed
```

Convention: `<Project>.<Feature>.<Topic>.<Detail>`. Match-type chooses the
granularity. Register all tags in `Config/DefaultGameplayTags.ini`.

---

## Register Listener Patterns

### Standard listener (paired register/unregister)

```cpp
void UDuckSoulShopUI::NativeOnActivated()
{
    Super::NativeOnActivated();

    UGameplayMessageSubsystem& MsgSub = UGameplayMessageSubsystem::Get(this);

    PurchaseHandle = MsgSub.RegisterListener<FDuckSoulShopPurchaseMessage>(
        FGameplayTag::RequestGameplayTag("DuckSoul.Shop.Purchase"),
        [this](FGameplayTag Channel, const FDuckSoulShopPurchaseMessage& Msg)
        {
            UpdateInventoryDisplay(Msg.ItemTag, Msg.Quantity);
        },
        EGameplayMessageMatch::ExactMatch);
}

void UDuckSoulShopUI::NativeOnDeactivated()
{
    Super::NativeOnDeactivated();
    UGameplayMessageSubsystem::Get(this).UnregisterListener(PurchaseHandle);
}
```

### Partial match (subtree fan-in)

```cpp
// Listen to ALL DuckSoul.Combat.Damage.* events
auto Handle = MsgSub.RegisterListener<FDamageMessage>(
    FGameplayTag::RequestGameplayTag("DuckSoul.Combat.Damage"),
    [this](FGameplayTag Channel, const FDamageMessage& Msg)
    {
        if (Channel.MatchesTag(DealtTag)) { /* dealt */ }
        else if (Channel.MatchesTag(ReceivedTag)) { /* received */ }
    },
    EGameplayMessageMatch::PartialMatch);
```

**ExactMatch**: only the exact `Channel` tag fires the callback.
**PartialMatch**: any tag in the subtree (e.g., `Damage`, `Damage.Dealt`, `Damage.Received`) fires.

PartialMatch costs more — uses tag-hierarchy walk. Prefer ExactMatch for hot paths.

---

## Broadcast Pattern

```cpp
void ADuckSoulShopkeeper::CompletePurchase(APlayerState* Buyer,
                                            FGameplayTag ItemTag,
                                            int32 Qty,
                                            int32 Cost)
{
    FDuckSoulShopPurchaseMessage Msg;
    Msg.Purchaser = Buyer;
    Msg.ItemTag   = ItemTag;
    Msg.Quantity  = Qty;
    Msg.Cost      = Cost;

    UGameplayMessageSubsystem::Get(this).BroadcastMessage(
        FGameplayTag::RequestGameplayTag("DuckSoul.Shop.Purchase"),
        Msg);
}
```

Broadcast is **synchronous** — all listeners' callbacks run on the broadcasting
thread before `BroadcastMessage` returns. Do not do heavy work in a listener;
defer with `AsyncTask(ENamedThreads::GameThread, ...)` or
`GetWorld()->GetTimerManager()`.

---

## FGameplayMessageListenerHandle Lifetime

```cpp
struct FGameplayMessageListenerHandle
{
    bool IsValid() const;
    void Unregister();
};
```

- Only the member-function overload
  `RegisterListener(Channel, Object, &TOwner::Function)` wraps the object in a
  `TWeakObjectPtr` and skips the callback when the object is gone.
- Lambda (`TFunction`) listeners are NEVER auto-pruned — you MUST call
  `UnregisterListener` (or `Handle.Unregister()`)
  in `EndPlay`/`NativeOnDeactivated` to avoid stale dispatch during teardown.
- All listeners are dropped when the subsystem deinitializes (GameInstance
  shutdown); pass any `WorldContextObject` with a route to a world to `Get()`.

---

## BP Integration — UAsyncAction_ListenForGameplayMessage

```cpp
// Blueprint latent node — dedicated K2 node (GameplayMessageNodes module)
UCLASS(BlueprintType, meta=(HasDedicatedAsyncNode))
class UAsyncAction_ListenForGameplayMessage : public UCancellableAsyncAction
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable)
    FAsyncGameplayMessageDelegate OnMessageReceived; // (ProxyObject, ActualChannel)

    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject", BlueprintInternalUseOnly="true"))
    static UAsyncAction_ListenForGameplayMessage* ListenForGameplayMessages(
        UObject* WorldContextObject, FGameplayTag Channel,
        UScriptStruct* PayloadType,
        EGameplayMessageMatch MatchType = EGameplayMessageMatch::ExactMatch);

    // Copy the received payload into a wildcard pin (type must match)
    UFUNCTION(BlueprintCallable, CustomThunk, meta=(CustomStructureParam="OutPayload"))
    bool GetPayload(UPARAM(ref) int32& OutPayload);
};
```

In BP: drag from `ListenForGameplayMessages` node, set Channel +
PayloadType, hook `OnMessageReceived` and call `GetPayload` to copy the
message. Node self-unregisters on `Cancel` or world destruction.

---

## Patterns

### Combat damage → HUD + audio + analytics

Single publish, three independent listeners:

```cpp
// Publisher (damage system):
MsgSub.BroadcastMessage(DamageDealtTag, DamageMsg);

// Listener 1 — HUD floating damage number:
HUDHandle = MsgSub.RegisterListener<FDamageMsg>(DamageDealtTag,
    [this](auto, const auto& M) { SpawnDamageNumber(M); });

// Listener 2 — audio cue:
AudioHandle = MsgSub.RegisterListener<FDamageMsg>(DamageDealtTag,
    [this](auto, const auto& M) { PlayDamageSFX(M.DamageType); });

// Listener 3 — analytics aggregator:
StatsHandle = MsgSub.RegisterListener<FDamageMsg>(DamageDealtTag,
    [this](auto, const auto& M) { Accumulate(M.Damage); });
```

The publisher knows nothing about HUD, audio, or analytics. Adding a fourth
listener (e.g., screen-shake) requires zero changes to the publisher.

### Experience-scoped listener (installed via Action)

Implement a `UGameFeatureAction_AddGameplayMessageListener` subclass that
registers on Experience activate and unregisters on deactivate. This binds
listener lifetime to Experience, not GameInstance — clean swap-on-mode-change.

---

## Comparison: GameplayMessage vs Alternatives

| Pattern | Replicated? | Coupling | Best for |
|---|---|---|---|
| `UGameplayMessageSubsystem` | No (local-only) | None (tag channel) | Cross-system events, HUD/UI/analytics fan-out |
| GameplayCues (`UGameplayCueManager`) | Yes (replicated via GAS) | None (tag-driven) | Cosmetic VFX/SFX synced across clients |
| Direct `DECLARE_MULTICAST_DELEGATE` | No | Tight (publisher type known) | Component-internal events |
| Replicated `MARK_PROPERTY_DIRTY` / RPC | Yes | Tight (rep struct) | Authoritative state sync |

**Rule**: use `UGameplayMessageSubsystem` for local fan-out where listeners
don't already hold a ref to the publisher. Use GameplayCues (`UGameplayCueManager`) when
the event must be cosmetic-synced to remote clients. Use replicated RPCs
when the event affects authoritative gameplay state.

---

## Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| Listener fires after object destroyed → crash | Forgot to unregister; lambda captures dangling `this` | Always pair `RegisterListener` with `Unregister` in EndPlay/NativeOnDeactivated |
| Cross-PIE leakage | Listener registered on subsystem persists past PIE end | GameInstance is per-PIE — fine. But static caches inside callbacks DO leak |
| Heavy work blocks broadcaster | Listener does heavy compute synchronously | Defer: `AsyncTask(ENamedThreads::GameThread, ...)` or a timer |
| Partial match catches too much | Channel tag too short | Use deeper channel tags: `Combat.Damage.Dealt.Critical` not `Combat.Damage` |
| Message struct has raw `UObject*` | Field GC'd between broadcast and async-deferred listener | Use `TObjectPtr` or `TWeakObjectPtr` and null-check |
| No listeners but broadcast still costs | Tag hash + map lookup per broadcast | Hot paths: cache `bHasAnyListeners` if needed (premature, usually fine) |
| BP listener double-fires | `UAsyncAction_ListenForGameplayMessage` activated twice on same channel | Hook only ONE node per channel per object; cancel previous before re-listen |

---

## Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new[] {
    "Core", "CoreUObject", "Engine",
    "GameplayTags", "GameplayMessageRuntime"
});
```

`.uproject` `Plugins` entry:

```json
{ "Name": "GameplayMessageRouter", "Enabled": true }
```

---

## Related Skills

- `ue-gameplay-abilities` — GAS GameplayEvents go through ASC, not this
  subsystem. Use this for cross-cutting non-ability events.
- `gameplay-tags` — channel naming + AssetManager registration.
- `ue-lyra-experience` — install listeners via Experience Actions for
  mode-scoped pub/sub.
- `ue-common-ui` — HUD listeners usually live on activatable widgets;
  unregister in `NativeOnDeactivated`.
- `ue-subsystems-taxonomy` — `UGameInstanceSubsystem` lifetime overview.
