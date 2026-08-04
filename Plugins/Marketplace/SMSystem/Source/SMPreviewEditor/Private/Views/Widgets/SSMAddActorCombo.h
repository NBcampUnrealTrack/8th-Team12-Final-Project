// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SPositiveActionButton.h"

class USMPreviewObject;
class FSMStateMachineBlueprintEditor;
struct FAssetData;

DECLARE_DELEGATE_OneParam(FOnActorClassSelected, TSubclassOf<AActor> /*ActorClass*/);

/**
 * Custom actor creation combo box.
 */
class SSMAddActorCombo : public SPositiveActionButton
{
public:
	SLATE_BEGIN_ARGS(SSMAddActorCombo) {}
	SLATE_EVENT(FOnActorClassSelected, OnActorSelected)
	SLATE_END_ARGS()

	~SSMAddActorCombo();
	void Construct(const FArguments& InArgs, TSharedPtr<FSMStateMachineBlueprintEditor> InStateMachineEditor);

private:
	/** Creates the combo button menu content. */
	TSharedRef<SWidget> GenerateMenuContent();
	void OnAssetSelected(const FAssetData& InAssetData);
	bool OnShouldFilterAsset(const FAssetData& InAssetData);

private:
	TWeakPtr<FSMStateMachineBlueprintEditor> BlueprintEditor;
	FOnActorClassSelected OnActorClassSelectedEvent;
};
