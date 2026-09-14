---
name: ue-common-ui
description: "Use this skill when working with Unreal CommonUI or Lyra-style UI stacks: activatable widgets, stacks and queues, primary layouts, input routing, CommonButton, gamepad-first focus, back actions, and layered modal or menu pushes."
unreal_version: "5.8"
feature_status: production
status_verified: "2026-07-30"
metadata:
  version: 1.0.1
---

# UE CommonUI

Use CommonUI for activation lifecycle, layered stacks, gamepad-first focus, and input-mode
routing. Do not assume a project follows Lyra's UI policy; inspect its plugins, policy,
primary layout, and layer tags first.

## Context Check

Read `.agents/ue-project-context.md` first. Confirm:

- `CommonUI`, `CommonGame`, `CommonUser` plugins enabled (Lyra defaults yes)
- `ULyraUIManagerSubsystem` (or `UCommonUIExtensions`) present
- `UPrimaryGameLayout` subclass and layer names (Game / GameMenu / Menu / Modal)
- Existing `ULyraUIPolicy` / `UGameUIPolicy` assigned in DefaultGame.ini
- Whether `Slate` `RetainerBox` patterns are needed (perf-sensitive HUD)

## Information to Gather

1. New activatable widget (HUD element, modal, full-screen menu) or extending one?
2. Which layer does it belong on? (Game = HUD; GameMenu = pause; Menu = main menu; Modal = blocking dialogs)
3. Input mode needed: GameAndMenu, Menu, or Game?
4. Does it need a focus target on activation? (gamepad navigation)
5. Should back input (B/Esc) deactivate this widget or block?
6. Will it stack on top of another widget (push) or replace (queue)?

---

## UCommonActivatableWidget Lifecycle

Source: `Plugins/Runtime/CommonUI/Source/CommonUI/Public/CommonActivatableWidget.h`

```cpp
UCLASS()
class UMyActivatableWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
protected:
    virtual void NativeOnActivated() override;     // pushed onto stack
    virtual void NativeOnDeactivated() override;   // popped / covered

    // Gamepad focus target
    virtual UWidget* NativeGetDesiredFocusTarget() const override
    {
        return FirstButton;
    }

    // Input mode + mouse-capture rules
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override
    {
        return FUIInputConfig(
            ECommonInputMode::Menu,           // capture all input for menus
            EMouseCaptureMode::NoCapture);    // free cursor
    }

    // Handle B/Esc yourself. Default NativeOnHandleBackAction() calls
    // DeactivateWidget(); return true to handle without deactivating.
    virtual bool NativeOnHandleBackAction() override;

    UPROPERTY(meta=(BindWidget))
    TObjectPtr<UCommonButtonBase> FirstButton;
};
```

### Activation states

| State | When | Tick? | Visible? |
|---|---|---|---|
| Activated | Pushed and on top | Yes | Yes |
| Deactivated (covered) | Below another in stack | Yes (configurable) | Visible behind |
| Deactivated (popped) | Removed from stack | No | No (removed) |

`bAutoActivate = true` (BP default) activates immediately when added to a
parent widget. For stack-driven widgets, set `false` and let the stack drive it.

---

## UCommonActivatableWidgetStack vs Queue

Source: `CommonActivatableWidgetContainer.h`

### Stack

LIFO. Multiple widgets active visually; only top has input focus by default.

```cpp
UCommonActivatableWidgetStack* Stack = ...;
UMyWidget* W = Stack->AddWidget<UMyWidget>(MyWidgetClass,
    [](UMyWidget& Created) { Created.SetTitle("Shop"); });
```

`AddWidget<T>(TSubclassOf<T>, TFunctionRef<void(T&)>)` — templated, called
before the widget's NativeOnActivated. Use the lambda to inject data.

```cpp
Stack->RemoveWidget(*W);          // pop specific
Stack->ClearWidgets();            // pop all
W->DeactivateWidget();            // self-pop (preferred)
```

### Queue

FIFO. Only one widget active at a time; next dequeues after current
deactivates. Use for sequential notifications/dialogs.

---

## UPrimaryGameLayout + ULyraUIManagerSubsystem

Lyra layer model — four named layers, each a `UCommonActivatableWidgetStack`:

| Layer Tag | Purpose | Examples |
|---|---|---|
| `UI.Layer.Game` | HUD, always-on overlays | health bar, ammo, minimap |
| `UI.Layer.GameMenu` | In-game menus that pause | inventory, shop |
| `UI.Layer.Menu` | Top-level menus | main menu, settings |
| `UI.Layer.Modal` | Blocking dialogs | confirm purchase, error |

```cpp
// Push a widget to a layer
UPrimaryGameLayout* Layout =
    UPrimaryGameLayout::GetPrimaryGameLayoutForPrimaryPlayer(GetWorld());

UMyShopWidget* Shop = Layout->PushWidgetToLayerStack<UMyShopWidget>(
    FGameplayTag::RequestGameplayTag("UI.Layer.GameMenu"),
    MyShopWidgetClass,
    [InitData](UMyShopWidget& W) { W.Init(InitData); });

// Async push for soft class refs
TSharedRef<FStreamableHandle> Handle =
    Layout->PushWidgetToLayerStackAsync<UMyShopWidget>(
        ShopLayerTag, /*bSuspendInputUntilComplete*/ true,
        SoftWidgetClass, [](UMyShopWidget& W) { /* init */ });
```

`PushWidgetToLayerStackAsync` is the right entry for any Experience-driven
push, because the widget class may live in a GameFeature plugin loaded
on-demand.

---

## UCommonInputSubsystem (Input Method Detection)

Source: `Plugins/Runtime/CommonUI/Source/CommonInput/Public/CommonInputSubsystem.h`

```cpp
auto* InputSub = UCommonInputSubsystem::Get(GetOwningLocalPlayer());
ECommonInputType Current = InputSub->GetCurrentInputType();
// MouseAndKeyboard | Gamepad | Touch

InputSub->OnInputMethodChangedNative.AddUObject(
    this, &UMyHUD::HandleInputMethodChanged);

void UMyHUD::HandleInputMethodChanged(ECommonInputType NewType)
{
    bShowButtonHints = (NewType == ECommonInputType::Gamepad);
}
```

Widgets respond to input swaps for context-sensitive button glyphs. Pair with
`UCommonInputActionDataBase` for action-name → glyph-icon lookup.

---

## FUIInputConfig (Capture Rules)

```cpp
struct FUIInputConfig
{
    ECommonInputMode InputMode;          // Game | Menu | All (GameAndMenu)
    EMouseCaptureMode MouseCaptureMode;  // NoCapture | CapturePermanently | CapturePermanently_IncludingInitialMouseDown
    bool bHideCursorDuringViewportCapture;
};
```

| InputMode | Game input? | UI input? | Use case |
|---|---|---|---|
| `Game` | yes | no | gameplay (HUD only) |
| `Menu` | no | yes | full menu, pause |
| `All` (GameAndMenu) | yes | yes | overlays that don't pause (loot HUD with shoot still active) |

The TOP activatable widget on the stack determines the config; CommonUI
routes via `UCommonUIActionRouterBase::ApplyUIInputConfig`. Widgets that
return `TOptional<>()` inherit the next-below widget's config.

---

## UCommonButtonBase + Action Routing

```cpp
// In a widget:
UPROPERTY(meta=(BindWidget))
TObjectPtr<UCommonButtonBase> ConfirmButton;

void UMyWidget::NativeOnActivated()
{
    Super::NativeOnActivated();
    ConfirmButton->OnClicked().AddUObject(this, &UMyWidget::HandleConfirm);

    // Bind back-action — real API is RegisterUIActionBinding + FBindUIActionArgs.
    // FBindUIActionArgs accepts a FDataTableRowHandle, UInputAction*, or FUIActionTag.
    RegisterUIActionBinding(FBindUIActionArgs(
        BackActionData,                 // FDataTableRowHandle (legacy) or UInputAction*
        /*bShouldDisplayInActionBar*/ true,
        FSimpleDelegate::CreateUObject(this, &UMyWidget::HandleBack)));
}
```

Back-action routes through `UCommonUIActionRouterBase`. By default,
`UCommonActivatableWidget::NativeOnHandleBackAction` calls `DeactivateWidget()` —
return `true` to handle without deactivating (e.g., focus a sub-menu).

### Hold-to-confirm

`UCommonButtonBase` supports hold buttons via `bRequiresHold` (enable) plus
`HoldTime` / `HoldData` (`TSubclassOf<UCommonUIHoldData>`); read the required
duration with `GetRequiredHoldTime()`. There is no `OnButtonHoldProgress`
delegate — track progress by overriding the virtual
`NativeOnHoldProgress(float DeltaTime)` (and `NativeOnHoldProgressRollback`),
and drive your own progress widget from it.

---

## DuckSoul Recipe — Push Shop Modal with Focus Restoration

```cpp
void UDuckSoulHUDComponent::OpenShop(const FShopInitData& InitData)
{
    auto* Layout = UPrimaryGameLayout::GetPrimaryGameLayoutForPrimaryPlayer(GetWorld());
    if (!Layout) return;

    // Remember currently focused widget for restoration
    PreviousFocus = FSlateApplication::Get().GetUserFocusedWidget(0);

    FGameplayTag ModalLayer = FGameplayTag::RequestGameplayTag("UI.Layer.Modal");

    Layout->PushWidgetToLayerStackAsync<UDuckSoulShopWidget>(
        ModalLayer, /*bSuspendInputUntilComplete*/ true,
        ShopWidgetClass,
        [InitData, this](UDuckSoulShopWidget& W)
        {
            W.Init(InitData);
            W.OnClosed.AddUObject(this, &UDuckSoulHUDComponent::HandleShopClosed);
        });
}

void UDuckSoulHUDComponent::HandleShopClosed()
{
    // Stack already popped — restore previous focus
    if (PreviousFocus.IsValid())
        FSlateApplication::Get().SetUserFocus(0, PreviousFocus.Pin());
}
```

---

## Localization (CommonTextBlock)

Use `UCommonTextBlock` instead of `UTextBlock` — it pulls style from
`UCommonTextStyle` (font, color, shadow, scrollable behavior) and works with
CommonUI's localization fallbacks.

```cpp
UPROPERTY(meta=(BindWidget))
TObjectPtr<UCommonTextBlock> TitleText;

TitleText->SetText(NSLOCTEXT("DuckSoulShop", "Title", "Item Shop"));
```

Style assets live in `Content/UI/Styles/`. Lyra has `LyraTextStyle_*` —
DuckSoul should subclass these for theme overrides.

---

## Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| Focus lost after pop | Previous widget didn't `SetIsFocusable(true)` or no `GetDesiredFocusTarget` | Override `NativeGetDesiredFocusTarget`; ensure focusable buttons exist |
| Input config not applying | Widget returns `TOptional<>()` and parent doesn't define it either | Set explicit `GetDesiredInputConfig` on top-of-stack widget |
| Pure Slate widget under CommonUI | `SWidget` constructed inside CommonUI parent without `SCommonButton` wrapper | Use `UCommon*` UMG widgets, not raw Slate, inside activatable widgets |
| Gamepad navigation skips a button | `bIsFocusable = false` or button not in nav-chain | Set focusable; check `Navigation` properties in UMG designer |
| Widget receives input while underneath modal | Modal returned `Game` input mode | Modals must return `Menu` mode |
| Pause menu doesn't pause | UWorld not paused — CommonUI doesn't auto-pause | Call `UGameplayStatics::SetGamePaused(this, true)` in NativeOnActivated |
| Stack memory leak after level travel | Stack holds references; world subsystem destroyed | UI lives on `UGameInstanceSubsystem` — `ResetLayout` on travel |
| `PushWidgetToLayerStackAsync` never resolves | Soft class ref points to GFP plugin not loaded | Activate the GameFeature before push or use Experience-bound push |

---

## Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new[] {
    "Core", "CoreUObject", "Engine",
    "UMG", "Slate", "SlateCore",
    "CommonUI", "CommonInput",
    "CommonGame", "GameplayTags"
});
PrivateDependencyModuleNames.AddRange(new[] {
    "InputCore", "EnhancedInput"
});
```

In `.uplugin` or `.uproject` plugins list:

```json
{ "Name": "CommonUI", "Enabled": true },
{ "Name": "CommonInput", "Enabled": true },
{ "Name": "CommonGame", "Enabled": true }
```

---

## Related Skills

- `ue-ui-umg-slate` — UMG base widgets, Slate primitives, retainer boxes.
  Load FIRST for designer-level widget composition.
- `ue-input-system` — Enhanced Input + `ULyraInputConfig`; CommonUI consumes
  these via `AddInputBinding` Experience action.
- `ue-lyra-experience` — `UGameFeatureAction_AddWidgets` is the standard push
  mechanism in Lyra; the Experience determines which widgets exist.
- `ue-subsystems-taxonomy` — `ULyraUIManagerSubsystem` is a
  `UGameInstanceSubsystem`; understand the lifetime.
