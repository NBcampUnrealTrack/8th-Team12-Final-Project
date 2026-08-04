// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Blueprints/ISMStateMachineBlueprintEditor.h"

#include "ISMSystemEditorModule.h"

class USMBlueprint;
class USMGraphK2Node_Base;
class USMGraphNode_Base;
class FSMBlueprintEditorToolbar;
class ISMPreviewModeViewportClient;

class FSMStateMachineBlueprintEditor : public ISMStateMachineBlueprintEditor
{
public:
	FSMStateMachineBlueprintEditor();
	virtual ~FSMStateMachineBlueprintEditor() override;

	void InitSMBlueprintEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, USMBlueprint* Blueprint);

	// IToolkit
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FName GetEditingAssetTypeName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FString GetDocumentationLink() const override;
	// ~IToolkit

	// FTickableEditorObject
	virtual void Tick(float DeltaTime) override;
	// ~FTickableEditorObject

	// FBlueprintEditor
	virtual void CreateDefaultCommands() override;
	virtual void RefreshEditors(ERefreshBlueprintEditorReason::Type Reason = ERefreshBlueprintEditorReason::UnknownReason) override;
	virtual void SetCurrentMode(FName NewMode) override;
	virtual void JumpToHyperlink(const UObject* ObjectReference, bool bRequestRename) override;
	virtual void OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled) override;
	virtual FGraphAppearanceInfo GetGraphAppearance(UEdGraph* InGraph) const override;
	virtual void PasteNodesHere(UEdGraph* Graph, const FVector2f& Location) override;
	virtual void DeleteSelectedNodes() override;
	virtual bool IsEditable(UEdGraph* InGraph) const override;
	virtual bool CanStartWatchingPin() const override;
	virtual void OnGraphEditorFocused(const TSharedRef<class SGraphEditor>& InGraphEditor) override;
	virtual FReply OnSpawnGraphNodeByShortcut(FInputChord InChord, const FVector2f& InPosition, UEdGraph* InGraph) override;
	// ~FBlueprintEditor
	
	// ISMStateMachineBlueprintEditor
	virtual USMBlueprint* GetStateMachineBlueprint() const override;
	virtual const TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>& GetSelectedPropertyNode() const override { return SelectedPropertyNode; }
	virtual bool IsSelectedPropertyNodeValid(bool bCheckReadOnlyStatus = true) const override;
	virtual void SelectNodes(const TSet<UEdGraphNode*>& InGraphNodes, bool bZoomToFit = false) override;
	virtual FOnSelectedNodesChanged& OnSelectedNodesChanged() override
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return OnSelectedNodesChangedEvent;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	// ~ISMStateMachineBlueprintEditor
	
	/** The main editor toolbar. */
	TSharedPtr<FSMBlueprintEditorToolbar> GetStateMachineToolbar() const { return StateMachineToolbar; }

	/** Retrieve currently focused graph panel. */
	TSharedPtr<SGraphPanel> GetFocusedGraphPanel() const;
	
	/** True during destructor. */
	bool IsShuttingDown() const { return bShuttingDown; }
	
	void CloseInvalidTabs();
	
	/** Graph nodes selected by the user at the time of a paste operation. */
	const TSet<TWeakObjectPtr<USMGraphNode_Base>>& GetSelectedGraphNodesDuringPaste() const { return SelectedGraphNodesOnPaste; }

	/** Set by property node. This isn't guaranteed to be valid unless used in a selected property command. */
	TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base> SelectedPropertyNode;
	
	/** Set when right clicking on a node. */
	TWeakObjectPtr<UEdGraphNode> SelectedNodeForContext;

	UE_DEPRECATED(5.1, "Do not call directly. Use `OnSelectedNodesChanged()` instead.")
	FOnSelectedNodesChanged OnSelectedNodesChangedEvent;

protected:
	//////////////////////////////////////
	/////// Begin Preview Module
	
	/** Starts previewing the asset. */
	void StartPreviewSimulation();

	/** Verifies a preview simulation can start. */
	bool CanStartPreviewSimulation() const;
	
	/** Terminates simulation */
	void StopPreviewSimulation();

	/** Deletes the selected preview item. */
	void DeletePreviewSelection();

public:
	/** Store a reference to preview client. */
	SMSYSTEMEDITOR_API void SetPreviewClient(const TSharedPtr<ISMPreviewModeViewportClient>& InPreviewClient);

	/** Direct access to the preview client. */
	TWeakPtr<ISMPreviewModeViewportClient> GetPreviewClient() const { return PreviewViewportClient; }

	/** True if the preview setting was enabled when this editor was opened. */
	bool IsPreviewModeAllowed() const { return bPreviewModeAllowed; }
	
	/////// End Preview Module
	//////////////////////////////////////

protected:
	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// ~FEditorUndoClient

	/** Extend menu */
	void ExtendMenu();

	/** Extend toolbar */
	void ExtendToolbar();

	void BindCommands();

	/** When a debug object was set for the blueprint being edited. */
	void OnDebugObjectSet(UObject* Object);

	/** Find all nodes for the blueprint and reset their debug state. */
	void ResetBlueprintDebugStates();

	/** Callback when a node is clicked on. */
	void OnNodeSingleClicked(UObject* Object);
	
	/** FBlueprintEditor interface */
	virtual void SetupGraphEditorEvents(UEdGraph* InGraph, SGraphEditor::FGraphEditorEvents& InEvents) override;
	virtual void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated) override;
	virtual void OnSelectedNodesChangedImpl(const TSet<UObject*>& NewSelection) override;
	virtual void OnCreateGraphEditorCommands(TSharedPtr<FUICommandList> GraphEditorCommandsList) override;
	virtual void OnCreateComment() override;
	virtual void CopySelectedNodes() override;
	virtual void PasteNodes() override;
	/** ~FBlueprintEditor interface */
	
	void OnToggleNamedTransitions();
	bool IsToggleNamedTransitionsChecked() const;
	
	/** A self transition for the same state. */
	void CreateSingleNodeTransition();
	bool CanCreateSingleNodeTransition() const;

	void CollapseNodesToStateMachine();
	bool CanCollapseNodesToStateMachine() const;

	void CutCombineStates();
	void CopyCombineStates();
	bool CanCutOrCopyCombineStates() const;
	
	void ConvertStateMachineToReference();
	bool CanConvertStateMachineToReference() const;

	void ChangeStateMachineReference();
	bool CanChangeStateMachineReference() const;

	void JumpToStateMachineReference();
	bool CanJumpToStateMachineReference() const;

	void EnableIntermediateGraph();
	bool CanEnableIntermediateGraph() const;

	void DisableIntermediateGraph();
	bool CanDisableIntermediateGraph() const;

	void ReplaceWithStateMachine();
	bool CanReplaceWithStateMachine() const;

	void ReplaceWithStateMachineReference();
	bool CanReplaceWithStateMachineReference() const;

	void ReplaceWithStateMachineParent();
	bool CanReplaceWithStateMachineParent() const;
	
	void ReplaceWithState();
	bool CanReplaceWithState() const;

	void ReplaceWithConduit();
	bool CanReplaceWithConduit() const;

	void GoToGraph();
	bool CanGoToGraph() const;

	void GoToNodeBlueprint();
	bool CanGoToNodeBlueprint() const;
	
	void GoToPropertyBlueprint();
	bool CanGoToPropertyBlueprint() const;

	void GoToStateStackBlueprint();
	bool CanGoToStateStackBlueprint() const;
	
	void GoToTransitionStackBlueprint();
	bool CanGoToTransitionStackBlueprint() const;
	
	void GoToPropertyGraph();
	bool CanGoToPropertyGraph() const;

	void ClearGraphProperty();
	bool CanClearGraphProperty() const;

	void ToggleGraphPropertyEdit();
	bool CanToggleGraphPropertyEdit() const;

	void AddBreakpoint();
	bool CanAddBreakpoint() const;
	
	void RemoveBreakpoint();
	bool CanRemoveBreakpoint() const;
	
	void ToggleBreakpoint();
	bool CanToggleBreakpoint() const;
	
	void EnableBreakpoint();
	bool CanEnableBreakpoint() const;
	
	void DisableBreakpoint();
	bool CanDisableBreakpoint() const;

	void ToggleStateActive();
	bool CanToggleStateActive() const;
	ECheckBoxState IsStateActive() const;
	
	void TakeTransition();
	bool CanTakeTransition() const;

private:
	/** The extender to pass to the level editor to extend its window menu */
	TSharedPtr<FExtender> MenuExtender;

	/** Toolbar extender */
	TSharedPtr<FExtender> ToolbarExtender;

	/** The command list for this editor */
	TSharedPtr<FUICommandList> GraphEditorCommands;

	/** Custom toolbar used for switching modes */
	TSharedPtr<FSMBlueprintEditorToolbar> StateMachineToolbar;

	/** Selected state machine graph node */
	TWeakObjectPtr<USMGraphK2Node_Base> SelectedStateMachineNode;

	/** The currently loaded blueprint. */
	TWeakObjectPtr<UBlueprint> LoadedBlueprint;

	/** When the user sets a debug object. */
	FDelegateHandle OnDebugObjectSetHandle;

	/** Preview world viewport. */
	TWeakPtr<ISMPreviewModeViewportClient> PreviewViewportClient;

	/** Graph nodes selected only at the time of a paste operation. */
	TSet<TWeakObjectPtr<USMGraphNode_Base>> SelectedGraphNodesOnPaste;

	/** Nodes last selected. */
	TSet<UObject*> PreviouslySelectedNodes;
	
	/** True during hyper link jump! */
	bool bJumpingToHyperLink;

	/** Called from destructor. */
	bool bShuttingDown;

	/** If preview mode has been enabled from settings for this editor. */
	bool bPreviewModeAllowed;
};


class FSMNodeBlueprintEditor : public FBlueprintEditor
{
public:
	FSMNodeBlueprintEditor();
	virtual ~FSMNodeBlueprintEditor() override;

	// IToolkit
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetEditingAssetTypeName() const override;
	virtual FString GetDocumentationLink() const override;
	virtual FGraphAppearanceInfo GetGraphAppearance(UEdGraph* InGraph) const override;
	// ~IToolkit

	void InitNodeBlueprintEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost,
		const TArray<UBlueprint*>& InBlueprints, bool bShouldOpenInDefaultsMode);

protected:
	// FBlueprintEditor
	virtual void OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled) override;
	// ~FBlueprintEditor
};
