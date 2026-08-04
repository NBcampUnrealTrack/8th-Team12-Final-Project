// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphPanel.h"
#include "Misc/NotifyHook.h"
#include "IDetailsView.h"
#include "ISceneOutliner.h"
#include "Widgets/SCompoundWidget.h"

class USMPreviewObject;
class FSMStateMachineBlueprintEditor;
class USMBlueprint;
class SSMPreviewModeOutlinerView;
class FObjectPostSaveContext;

enum ESMPreviewModeType
{
	SM_OutlineMode,
	SM_DetailsMode
};

class SSMPreviewModeEditorView : public SCompoundWidget, public FNotifyHook
{
	SLATE_BEGIN_ARGS(SSMPreviewModeEditorView) {}

	SLATE_END_ARGS()

	SSMPreviewModeEditorView();
	virtual ~SSMPreviewModeEditorView() override;
	
	void Construct(const FArguments& InArgs, TSharedPtr<FSMStateMachineBlueprintEditor> InStateMachineEditor, const FName& InTabID);

	/**
	 * Signal to update the current selection based on the preview object.
	 * @param bForce Force update even if the mode is wrong.
	 */
	void UpdateSelection(bool bForce = false);

	/** Checks that there is no simulation running. */
	bool IsSimulationNotRunning() const;
private:
	void OnBlueprintChanged(UBlueprint* InBlueprint);
	void OnPreviewObjectChanged(USMPreviewObject* InPreviewObject);
	void OnPreviewWorldChanged(UWorld* InWorld);
	void OnEditorSelectionChanged(UObject* NewObject);
	void OnBlueprintEditorModeChanged(FName InModeName);
	void OnPackageSaved(const FString& Filename, UPackage* Package, FObjectPostSaveContext ObjectSaveContext);
	void OnActorSelectedToSpawn(TSubclassOf<AActor> ActorClass);
	
	void OnOutlinerSelectionChanged(FSceneOutlinerTreeItemPtr TreeItem, ESelectInfo::Type Type);

	/** Retrieve the display text of the selected context object. */
	FText GetContextText() const;
	
	ECheckBoxState IsChecked(ESMPreviewModeType Mode) const;
	EVisibility IsEditorVisible(ESMPreviewModeType Mode) const;
	const FSlateBrush* GetBorderBrushByMode(ESMPreviewModeType Mode) const;
	void OnCheckedChanged(ECheckBoxState NewType, ESMPreviewModeType Mode);

private:
	TWeakPtr<FSMStateMachineBlueprintEditor> BlueprintEditor;
	TWeakObjectPtr<USMBlueprint> Blueprint;
	TSharedPtr<IDetailsView> PreviewDetailsView;
	TSharedPtr<SSMPreviewModeOutlinerView> OutlinerView;

	FDelegateHandle BlueprintChangedHandle;
	FDelegateHandle BlueprintSavedHandle;
	FDelegateHandle PreviewObjectChangedHandle;
	FDelegateHandle PreviewWorldChangedHandle;
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle BlueprintEditorModeChangedHandle;

	ESMPreviewModeType CurrentMode;
};
