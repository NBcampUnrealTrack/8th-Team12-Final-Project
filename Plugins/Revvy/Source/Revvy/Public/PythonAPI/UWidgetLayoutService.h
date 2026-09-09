// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UWidgetLayoutService.generated.h"

class UUserWidget;

/**
 * Runtime widget layout service exposed directly to Python for spawning
 * UMG UUserWidget instances onto a player controller's viewport and
 * driving their runtime layout state (visibility, layout prepass).
 *
 * Distinct from:
 *   - UWidgetService (asset-authoring: Widget Blueprint hierarchy, MVVM
 *                     bindings, animations, off-screen preview, PIE-scoped
 *                     spawn via FPIEWidgetHandle). This service focuses on
 *                     the *runtime viewport layer* — actor-generic label-
 *                     keyed registry, arbitrary UUserWidget subclasses,
 *                     visibility / layout prepass, live enumeration.
 *
 * Mirrors the UNiagaraComponentService / UCameraShakeService pattern:
 * all UFUNCTIONs are static and BlueprintCallable so they are reachable
 * from Python as `unreal.WidgetLayoutService.<snake_case_name>(...)`.
 *
 * Runtime state service — no FScopedTransaction (widgets are transient
 * viewport state, not persisted asset / actor state).
 *
 * State model:
 *   - Static TMap<FString, TWeakObjectPtr<UUserWidget>> registry keyed by
 *     a generated FGuid label returned from CreateUserWidget.
 *   - Weak refs are pruned on access; callers may treat missing entries
 *     as widgets that were destroyed externally.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.WidgetLayoutService
 *
 *   label = svc.create_user_widget(
 *       "/Game/UI/WBP_HUD.WBP_HUD_C", "PC0")
 *   svc.set_widget_visibility(label, "HitTestInvisible")
 *   svc.force_layout_prepass(label)
 *   active = json.loads(svc.list_active_widgets())
 *   svc.remove_user_widget(label)
 */
UCLASS(BlueprintType)
class REVVY_API UWidgetLayoutService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Create
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Instantiate a UUserWidget subclass owned by the target player
	 * controller and add it to the viewport. The widget is tracked in
	 * an internal registry keyed by a freshly generated FGuid label so
	 * subsequent layout / visibility / removal calls can reference it.
	 *
	 * @param WidgetClassPath                  Full class path to a
	 *                                         UUserWidget subclass
	 *                                         (e.g. "/Game/UI/WBP_HUD.WBP_HUD_C").
	 *                                         Blueprint generated classes
	 *                                         automatically get "_C" appended
	 *                                         if missing.
	 * @param PlayerControllerActorNameOrPath  Actor label / name / path of
	 *                                         the target APlayerController.
	 *                                         Empty falls back to the first
	 *                                         PC in the world.
	 * @return Generated FGuid label used to reference the widget, or empty
	 *         string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WidgetLayout|Create")
	static FString CreateUserWidget(
		const FString& WidgetClassPath,
		const FString& PlayerControllerActorNameOrPath);

	// ═══════════════════════════════════════════════════════════════════
	// Remove
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Remove a previously created widget from the viewport and drop the
	 * registry entry.
	 *
	 * @param WidgetLabel  Label returned from CreateUserWidget.
	 * @return True if the widget was resolved and removed (or already gone
	 *         from the viewport but still in the registry). False if the
	 *         label is unknown.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WidgetLayout|Remove")
	static bool RemoveUserWidget(const FString& WidgetLabel);

	// ═══════════════════════════════════════════════════════════════════
	// Layout
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set the ESlateVisibility on a tracked widget.
	 *
	 * @param WidgetLabel     Label returned from CreateUserWidget.
	 * @param VisibilityStr   One of "Visible" | "Collapsed" | "Hidden" |
	 *                        "HitTestInvisible" | "SelfHitTestInvisible"
	 *                        (case-insensitive).
	 * @return True on success. False if the label is unknown or the
	 *         visibility string is unrecognised.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WidgetLayout|Layout")
	static bool SetWidgetVisibility(
		const FString& WidgetLabel,
		const FString& VisibilityStr);

	/**
	 * Force a Slate layout prepass on a tracked widget so layout-dependent
	 * queries (DesiredSize, cached geometry) reflect the latest state
	 * without waiting for the next tick.
	 *
	 * @param WidgetLabel  Label returned from CreateUserWidget.
	 * @return True on success. False if the label is unknown.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WidgetLayout|Layout")
	static bool ForceLayoutPrepass(const FString& WidgetLabel);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Enumerate all currently tracked widgets, pruning any stale weak
	 * references (widgets that were destroyed / GC'd externally).
	 *
	 * @return JSON array of objects:
	 *         [{"label": "...", "widget_class": "...", "is_in_viewport": bool}, ...]
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WidgetLayout|Query")
	static FString ListActiveWidgets();

private:
	/** Registry keyed by generated FGuid label. */
	static TMap<FString, TWeakObjectPtr<UUserWidget>> Widgets;
};
