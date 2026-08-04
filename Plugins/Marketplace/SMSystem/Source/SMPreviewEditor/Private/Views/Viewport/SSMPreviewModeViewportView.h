// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SEditorViewport.h"

class FSMStateMachineBlueprintEditor;
class FSMAdvancedPreviewScene;
class FSMPreviewModeViewportClient;

/**
 * Slate widget which renders our view client.
 */
class SSMPreviewModeViewportView : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
{
public:
	SLATE_BEGIN_ARGS(SSMPreviewModeViewportView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FSMStateMachineBlueprintEditor> InStateMachineEditor);
	~SSMPreviewModeViewportView();

	// ICommonEditorViewportToolbarInfoProvider
	virtual TSharedRef<class SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override;
	// ~ICommonEditorViewportToolbarInfoProvider
	
	TSharedPtr<FSMAdvancedPreviewScene> GetAdvancedPreviewScene() const { return AdvancedPreviewScene; }
	
protected:
	// SEditorViewport
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
	virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
	virtual void BindCommands() override;
	// ~SEditorViewport

	/** If the contained viewport client should be visible. */
	bool IsViewportClientVisible() const;

protected:
	/** Preview Scene - uses advanced preview settings */
	TSharedPtr<FSMAdvancedPreviewScene> AdvancedPreviewScene;
	
	/** Level viewport client */
	TSharedPtr<FSMPreviewModeViewportClient> SystemViewportClient;

	/** Owning blueprint editor. */
	TWeakPtr<FSMStateMachineBlueprintEditor> BlueprintEditorPtr;
};