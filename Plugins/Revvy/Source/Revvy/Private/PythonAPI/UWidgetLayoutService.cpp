// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UWidgetLayoutService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyWidgetLayout, Log, All);

// Engine
#include "GameFramework/PlayerController.h"

// UMG runtime
#include "Blueprint/UserWidget.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Static state
// =================================================================

TMap<FString, TWeakObjectPtr<UUserWidget>> UWidgetLayoutService::Widgets;

// =================================================================
// Local helpers (private to this translation unit)
// =================================================================

namespace
{
	/** Parse a string into an ESlateVisibility. Returns false if unrecognised. */
	bool ParseSlateVisibility(const FString& Str, ESlateVisibility& OutVis)
	{
		const FString Lower = Str.ToLower();
		if (Lower == TEXT("visible"))                    { OutVis = ESlateVisibility::Visible;                 return true; }
		if (Lower == TEXT("collapsed"))                  { OutVis = ESlateVisibility::Collapsed;               return true; }
		if (Lower == TEXT("hidden"))                     { OutVis = ESlateVisibility::Hidden;                  return true; }
		if (Lower == TEXT("hittestinvisible"))           { OutVis = ESlateVisibility::HitTestInvisible;        return true; }
		if (Lower == TEXT("selfhittestinvisible"))       { OutVis = ESlateVisibility::SelfHitTestInvisible;    return true; }
		return false;
	}

	/** Serialize a JSON array to a compact FString. */
	FString WidgetLayoutJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}
}

// =================================================================
// Create
// =================================================================

FString UWidgetLayoutService::CreateUserWidget(
	const FString& WidgetClassPath,
	const FString& PlayerControllerActorNameOrPath)
{
	TSubclassOf<UUserWidget> WidgetClass = URevvyCommonHelpers::LoadClassWithFallback(WidgetClassPath, UUserWidget::StaticClass());
	if (!WidgetClass)
	{
		UE_LOG(LogRevvyWidgetLayout, Warning,
			TEXT("CreateUserWidget: failed to load UUserWidget class '%s'"), *WidgetClassPath);
		return FString();
	}

	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyWidgetLayout, Warning,
			TEXT("CreateUserWidget: PlayerController '%s' not found (need active PIE?)"),
			*PlayerControllerActorNameOrPath);
		return FString();
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(PC, WidgetClass);
	if (!Widget)
	{
		UE_LOG(LogRevvyWidgetLayout, Warning,
			TEXT("CreateUserWidget: CreateWidget returned null for class '%s'"),
			*WidgetClassPath);
		return FString();
	}

	Widget->AddToViewport();

	// Generate a unique label and store the weak ref.
	const FString Label = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Widgets.Add(Label, TWeakObjectPtr<UUserWidget>(Widget));

	UE_LOG(LogRevvyWidgetLayout, Log,
		TEXT("CreateUserWidget: spawned '%s' on PC '%s' -> label '%s'"),
		*WidgetClassPath, *PC->GetName(), *Label);

	return Label;
}

// =================================================================
// Remove
// =================================================================

bool UWidgetLayoutService::RemoveUserWidget(const FString& WidgetLabel)
{
	TWeakObjectPtr<UUserWidget>* Entry = Widgets.Find(WidgetLabel);
	if (!Entry)
	{
		UE_LOG(LogRevvyWidgetLayout, Warning,
			TEXT("RemoveUserWidget: unknown label '%s'"), *WidgetLabel);
		return false;
	}

	if (UUserWidget* Widget = Entry->Get())
	{
		Widget->RemoveFromParent();
	}
	else
	{
		UE_LOG(LogRevvyWidgetLayout, Log,
			TEXT("RemoveUserWidget: widget for label '%s' was already GC'd; pruning entry"),
			*WidgetLabel);
	}

	Widgets.Remove(WidgetLabel);
	return true;
}

// =================================================================
// Layout
// =================================================================

bool UWidgetLayoutService::SetWidgetVisibility(
	const FString& WidgetLabel,
	const FString& VisibilityStr)
{
	TWeakObjectPtr<UUserWidget>* Entry = Widgets.Find(WidgetLabel);
	if (!Entry)
	{
		UE_LOG(LogRevvyWidgetLayout, Warning,
			TEXT("SetWidgetVisibility: unknown label '%s'"), *WidgetLabel);
		return false;
	}

	UUserWidget* Widget = Entry->Get();
	if (!Widget)
	{
		UE_LOG(LogRevvyWidgetLayout, Warning,
			TEXT("SetWidgetVisibility: widget for label '%s' was GC'd; pruning entry"),
			*WidgetLabel);
		Widgets.Remove(WidgetLabel);
		return false;
	}

	ESlateVisibility Vis;
	if (!ParseSlateVisibility(VisibilityStr, Vis))
	{
		UE_LOG(LogRevvyWidgetLayout, Warning,
			TEXT("SetWidgetVisibility: unrecognised visibility '%s' ")
			TEXT("(expected Visible|Collapsed|Hidden|HitTestInvisible|SelfHitTestInvisible)"),
			*VisibilityStr);
		return false;
	}

	Widget->SetVisibility(Vis);
	return true;
}

bool UWidgetLayoutService::ForceLayoutPrepass(const FString& WidgetLabel)
{
	TWeakObjectPtr<UUserWidget>* Entry = Widgets.Find(WidgetLabel);
	if (!Entry)
	{
		UE_LOG(LogRevvyWidgetLayout, Warning,
			TEXT("ForceLayoutPrepass: unknown label '%s'"), *WidgetLabel);
		return false;
	}

	UUserWidget* Widget = Entry->Get();
	if (!Widget)
	{
		UE_LOG(LogRevvyWidgetLayout, Warning,
			TEXT("ForceLayoutPrepass: widget for label '%s' was GC'd; pruning entry"),
			*WidgetLabel);
		Widgets.Remove(WidgetLabel);
		return false;
	}

	Widget->ForceLayoutPrepass();
	return true;
}

// =================================================================
// Query
// =================================================================

FString UWidgetLayoutService::ListActiveWidgets()
{
	// Prune stale weak refs and build a JSON snapshot in one pass.
	TArray<FString> StaleLabels;
	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Reserve(Widgets.Num());

	for (const TPair<FString, TWeakObjectPtr<UUserWidget>>& Pair : Widgets)
	{
		UUserWidget* Widget = Pair.Value.Get();
		if (!Widget)
		{
			StaleLabels.Add(Pair.Key);
			continue;
		}

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("label"), Pair.Key);

		FString ClassName;
		if (UClass* Cls = Widget->GetClass())
		{
			ClassName = Cls->GetPathName();
		}
		Obj->SetStringField(TEXT("widget_class"), ClassName);

		Obj->SetBoolField(TEXT("is_in_viewport"), Widget->IsInViewport());

		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	for (const FString& Stale : StaleLabels)
	{
		Widgets.Remove(Stale);
	}

	return WidgetLayoutJsonArrayToString(Arr);
}
