// Copyright Recursoft LLC. All Rights Reserved.

#include "SMBlueprintEditor.h"

#include "Commands/SMBlueprintSpawnNodeCommands.h"
#include "Commands/SMEditorCommands.h"
#include "Configuration/SMEditorSettings.h"
#include "Configuration/SMProjectEditorSettings.h"
#include "Construction/ISMEditorConstructionManager.h"
#include "Debug/SMBlueprintDebugController.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOBase.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphK2Node_StateMachineNode.h"
#include "Graph/Nodes/SMGraphNode_AnyStateNode.h"
#include "Graph/Nodes/SMGraphNode_ConduitNode.h"
#include "Graph/Nodes/SMGraphNode_LinkStateNode.h"
#include "Graph/Nodes/SMGraphNode_RerouteNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineParentNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_TransitionEdge.h"
#include "Graph/Schema/SMGraphSchema.h"
#include "Graph/Schema/SMGraphSchemaActions.h"
#include "Graph/SMConduitGraph.h"
#include "Graph/SMGraph.h"
#include "Graph/SMGraphK2.h"
#include "Graph/SMIntermediateGraph.h"
#include "Graph/SMPropertyGraph.h"
#include "Graph/SMStateGraph.h"
#include "Graph/SMTransitionGraph.h"
#include "Properties/PropertyInteraction/SMPropertyInteractionManager.h"
#include "SBlueprintEditorToolbar.h"
#include "SMBlueprintEditorModes.h"
#include "UI/SMBlueprintEditorToolbar.h"
#include "Utilities/SMBlueprintEditorUtils.h"
#include "Utilities/SMEditorAuthentication.h"
#include "Utilities/SMVersionUtils.h"

#include "ISMPreviewEditorModule.h"
#include "ISMPreviewModeViewportClient.h"

#include "Blueprints/SMBlueprint.h"

#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "IContentBrowserSingleton.h"
#include "Kismet2/Breakpoint.h"
#include "Kismet2/DebuggerCommands.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "SGraphPanel.h"
#include "SKismetInspector.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "SMBlueprintEditor"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
// ^ Needed for OnSelectedNodesChangedEvent
FSMStateMachineBlueprintEditor::FSMStateMachineBlueprintEditor() :
	SelectedPropertyNode(nullptr),
	bJumpingToHyperLink(false),
	bShuttingDown(false),
	bPreviewModeAllowed(false)
{
}

FSMStateMachineBlueprintEditor::~FSMStateMachineBlueprintEditor()
{
	bShuttingDown = true;
	if (LoadedBlueprint.IsValid())
	{
		StopPreviewSimulation();
		
		if (OnDebugObjectSetHandle.IsValid())
		{
			LoadedBlueprint->OnSetObjectBeingDebugged().Remove(OnDebugObjectSetHandle);
		}
	}

	OnBlueprintEditorClosed.Broadcast(this);
	
	bShuttingDown = false;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FSMStateMachineBlueprintEditor::InitSMBlueprintEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, USMBlueprint* Blueprint)
{
	TSharedPtr<FSMStateMachineBlueprintEditor> Editor(SharedThis(this));
	
	// Blueprint is already current unless it was saved by a newer version of the plugin.
	FSMVersionUtils::SetToLatestVersion(Blueprint);

	bPreviewModeAllowed = FSMBlueprintEditorUtils::GetProjectEditorSettings()->bEnablePreviewMode;
	
	if (bPreviewModeAllowed)
	{
		// Recreate the preview object only if it already exists. This will clear out the previous undo/redo stack
		// which prevents odd behavior in the event previous history deletes or restores a preview actor.
		Blueprint->RecreatePreviewObject();
	}
	
	LoadedBlueprint = MakeWeakObjectPtr(Blueprint);
	
	if (!Toolbar.IsValid())
	{
		Toolbar = MakeShared<FBlueprintEditorToolbar>(Editor);
	}

	if (!StateMachineToolbar.IsValid())
	{
		StateMachineToolbar = MakeShared<FSMBlueprintEditorToolbar>(Editor);
	}

	// So the play bar matches the level bar.
	GetToolkitCommands()->Append(FPlayWorldCommands::GlobalPlayWorldActions.ToSharedRef());

	CreateDefaultCommands();
	
	// Register default and custom commands.
	BindCommands();

	RegisterMenus();
	
	const TSharedRef<FTabManager::FLayout> DummyLayout = FTabManager::NewLayout("NullLayout")->AddArea(FTabManager::NewPrimaryArea());
	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	InitAssetEditor(Mode, InitToolkitHost, FSMBlueprintEditorModes::SMEditorName, DummyLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, Blueprint, false);

	TArray<UBlueprint*> Blueprints;
	Blueprints.Add(Blueprint);

	CommonInitialization(Blueprints, false);

	const TSharedRef<FApplicationMode> GraphMode = MakeShared<FSMBlueprintEditorGraphMode>(Editor);
	AddApplicationMode(GraphMode->GetModeName(), GraphMode);
	
	if (bPreviewModeAllowed)
	{
		const TSharedRef<FApplicationMode> PreviewMode = MakeShared<FSMBlueprintEditorPreviewMode>(Editor);
		AddApplicationMode(PreviewMode->GetModeName(), PreviewMode);
	}
	
	ExtendMenu();
	ExtendToolbar();
	RegenerateMenusAndToolbars();

	// This does the actual layout generation.
	SetCurrentMode(GraphMode->GetModeName());

	PostLayoutBlueprintEditorInitialization();

	// In case any temporary breakpoints weren't cleaned up already.
	USMBlueprintDebugController::CleanupTemporaryBreakpoints(Blueprint);
	
	OnDebugObjectSetHandle = Blueprint->OnSetObjectBeingDebugged().AddRaw(this, &FSMStateMachineBlueprintEditor::OnDebugObjectSet);
	
	OnBlueprintEditorOpened.Broadcast(this);
}

void FSMStateMachineBlueprintEditor::CreateDefaultCommands()
{
	FBlueprintEditor::CreateDefaultCommands();
	
	FSMBlueprintSpawnNodeCommands::Register();
}

void FSMStateMachineBlueprintEditor::RefreshEditors(ERefreshBlueprintEditorReason::Type Reason)
{
	CloseInvalidTabs();
	FBlueprintEditor::RefreshEditors(Reason);
}

void FSMStateMachineBlueprintEditor::SetCurrentMode(FName NewMode)
{
	if (bJumpingToHyperLink)
	{
		/*
		 * For now all modes support displaying graphs and we don't want to call SetCurrentMode
		 * when jumping because default behavior sets the mode back to blueprint defaults.
		 */
		return;
	}

	if (NewMode != FSMBlueprintEditorModes::SMPreviewMode)
	{
		// Close preview session.
		StopPreviewSimulation();
	}
	
	{
		// Save current selection and restore after a mode change.
		
		FGraphPanelSelectionSet CurrentSelection = GetSelectedNodes();

		FBlueprintEditor::SetCurrentMode(NewMode);

		TSharedPtr<SGraphEditor> FocusedGraphEd = FocusedGraphEdPtr.Pin();
		if (FocusedGraphEd.IsValid())
		{
			for (UObject* Selection : CurrentSelection)
			{
				if (UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(Selection))
				{
					FocusedGraphEd->SetNodeSelection(SelectedNode, true);
				}
			}
		}
	}
}

void FSMStateMachineBlueprintEditor::JumpToHyperlink(const UObject* ObjectReference, bool bRequestRename)
{
	bJumpingToHyperLink = true;
	FBlueprintEditor::JumpToHyperlink(ObjectReference, bRequestRename);
	bJumpingToHyperLink = false;
}

void FSMStateMachineBlueprintEditor::OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled)
{
	StopPreviewSimulation();
	FBlueprintEditor::OnBlueprintChangedImpl(InBlueprint, bIsJustBeingCompiled);

	// Notify any state machines that reference this state machine they need to be recompiled.
	// This is needed for Guid calculation since the owning SM will store all SM reference PathGuids.
	// Note this isn't perfect as nested references with a depth greater than one won't update all owners.
	// In this case the calculation will be performed at run-time and a warning will be logged. Compile time Guid
	// calculation should resolve itself on reload/recompile of owners and during packaging.
	const USMProjectEditorSettings* Settings = FSMBlueprintEditorUtils::GetProjectEditorSettings();
	if (Settings->bCalculateGuidsOnCompile)
	{
		TArray<UBlueprint*> Blueprints;

		FBlueprintEditorUtils::EnsureCachedDependenciesUpToDate(InBlueprint);
		FSMBlueprintEditorUtils::GetDependentBlueprints(InBlueprint, Blueprints);

		for (UBlueprint* Blueprint : Blueprints)
		{
			// Skip if this is a child blueprint since those are already caught by the compile process.
			if (Blueprint->IsA<USMBlueprint>() && !Blueprint->bIsRegeneratingOnLoad &&
				(Blueprint->GeneratedClass && !Blueprint->GeneratedClass->IsChildOf(InBlueprint->GeneratedClass)))
			{
				// First check for a circular dependency where this blueprint is also dependent on its dependent.
				// In that case we just want to continue because UE will handle this. Otherwise the compile fails with little information.
				TArray<UBlueprint*> OtherBlueprints;
				FSMBlueprintEditorUtils::GetDependentBlueprints(Blueprint, OtherBlueprints);
				if (OtherBlueprints.Contains(InBlueprint))
				{
					continue;
				}

				Blueprint->Status = BS_Dirty;
			}
		}
	}
}

FGraphAppearanceInfo FSMStateMachineBlueprintEditor::GetGraphAppearance(UEdGraph* InGraph) const
{
	FGraphAppearanceInfo AppearanceInfo = FBlueprintEditor::GetGraphAppearance(InGraph);

	if (GetStateMachineBlueprint())
	{
		LD::EditorAuth::LDAuthStatus AuthenticationStatus = LD::EditorAuth::GetUserMarketplaceAuthenticationStatus();

		if (AuthenticationStatus == LD::EditorAuth::LDAuthStatus::Succeeded || FSMAuthenticator::Get().IsAuthenticated())
		{
			AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_StateMachine", "LOGIC DRIVER");
		}
		else
		{
			AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_StateMachineInvalid", "LOGIC DRIVER (INVALID LICENSE)");
		}
		if (const UClass* GraphClass = InGraph ? InGraph->GetClass() : nullptr)
		{
			if (GraphClass->IsChildOf<USMPropertyGraph>())
			{
				AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_Property", "PROPERTY");
			}
			else if (GraphClass->IsChildOf<USMIntermediateGraph>())
			{
				AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_Intermediate", "REFERENCE");
			}
			else if (GraphClass->IsChildOf<USMStateGraph>())
			{
				AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_State", "STATE");
			}
			else if (GraphClass->IsChildOf<USMTransitionGraph>())
			{
				AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_Transition", "TRANSITION");
			}
			else if (GraphClass->IsChildOf<USMConduitGraph>())
			{
				AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_Conduit", "CONDUIT");
			}
		}
	}

	return MoveTemp(AppearanceInfo);
}

void FSMStateMachineBlueprintEditor::PasteNodesHere(UEdGraph* Graph, const FVector2f& Location)
{
	FSMBlueprintEditorUtils::FBulkCacheInvalidation CacheInvalidator(GetStateMachineBlueprint());
	FBlueprintEditor::PasteNodesHere(Graph, Location);
}

void FSMStateMachineBlueprintEditor::DeleteSelectedNodes()
{
	FSMBlueprintEditorUtils::FBulkCacheInvalidation CacheInvalidator(GetStateMachineBlueprint());
	FBlueprintEditor::DeleteSelectedNodes();
}

bool FSMStateMachineBlueprintEditor::IsEditable(UEdGraph* InGraph) const
{
	// State machine blueprints should not be editable during PIE. The engine's default
	// InEditingMode() allows editing during PIE for classes not in the disallow list,
	// but state machine graph modifications during PIE should never be considered safe.
	if (IsPlayInEditorActive())
	{
		return false;
	}

	return FBlueprintEditor::IsEditable(InGraph);
}

bool FSMStateMachineBlueprintEditor::CanStartWatchingPin() const
{
	if (const UEdGraphPin* Pin = GetCurrentlySelectedPin())
	{
		if (const USMGraphK2Node_PropertyIOBase* PropertyNode = Cast<USMGraphK2Node_PropertyIOBase>(Pin->GetOwningNode()))
		{
			if (PropertyNode->IsPropertySelectionPin(Pin))
			{
				// We don't want to allow watching property selection pins as they won't change. This should really be schema
				// level control, but the engine has a todo to support that so it has to be checked here.
				return false;
			}
		}
	}
	return FBlueprintEditor::CanStartWatchingPin();
}

void FSMStateMachineBlueprintEditor::OnGraphEditorFocused(const TSharedRef<class SGraphEditor>& InGraphEditor)
{
	// If we are switching to a new graph within this tab then make sure nodes are aware they are unselected.
	// UE will effectively deselect nodes, but will not actually fire the correct deselection events.
	// Output variable interaction may get in a stuck state otherwise.
	// This is reproducible in anim blueprints as well.
	if (bJumpingToHyperLink
		&& !FSlateApplication::Get().GetModifierKeys().IsShiftDown()
		&& !FSlateApplication::Get().GetModifierKeys().IsControlDown())
	{
		for (UObject* PreviouslySelectedNode : PreviouslySelectedNodes)
		{
			if (USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(PreviouslySelectedNode))
			{
				StateNode->OnNodeSelectionChanged(false, false);
			}
		}
	}
	
	ISMStateMachineBlueprintEditor::OnGraphEditorFocused(InGraphEditor);
}

FReply FSMStateMachineBlueprintEditor::OnSpawnGraphNodeByShortcut(FInputChord InChord, const FVector2f& InPosition,
	UEdGraph* InGraph)
{
	// Our state machine hotkey spawners.
	if (InGraph && InGraph->IsA<USMGraph>() && FSMBlueprintSpawnNodeCommands::IsRegistered())
	{
		FScopedTransaction Transaction(LOCTEXT("AddNode", "Add Node"));

		TArray<UEdGraphNode*> OutNodes;
		FVector2f NodeSpawnPos = InPosition;
		FSMBlueprintSpawnNodeCommands::Get().GetGraphActionByChord(InChord, InGraph, NodeSpawnPos, OutNodes);

		TSet<const UEdGraphNode*> NodesToSelect;

		for (UEdGraphNode* CurrentNode : OutNodes)
		{
			NodesToSelect.Add(CurrentNode);
		}

		// Do not change node selection if no actions were performed
		if (OutNodes.Num() > 0)
		{
			InGraph->SelectNodeSet(NodesToSelect, /*bFromUI =*/true);
		}
		else
		{
			Transaction.Cancel();
		}
		
		return FReply::Handled();
	}
	
	// Let UE handle K2 graphs.
	return ISMStateMachineBlueprintEditor::OnSpawnGraphNodeByShortcut(InChord, InPosition, InGraph);
}

USMBlueprint* FSMStateMachineBlueprintEditor::GetStateMachineBlueprint() const
{
	return Cast<USMBlueprint>(LoadedBlueprint.Get());
}

bool FSMStateMachineBlueprintEditor::IsSelectedPropertyNodeValid(bool bCheckReadOnlyStatus) const
{
	if (!SelectedPropertyNode.IsValid())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	for (UObject* Node : Nodes)
	{
		if (const USMGraphNode_Base* GraphNode = Cast<USMGraphNode_Base>(Node))
		{
			const FSMGraphProperty_Base* PropertyNode = SelectedPropertyNode->GetPropertyNodeChecked();
			return (!bCheckReadOnlyStatus || !PropertyNode->IsVariableReadOnly()) && GraphNode->GetGraphPropertyNode(PropertyNode->GetGuid()) != nullptr;
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::SelectNodes(const TSet<UEdGraphNode*>& InGraphNodes, bool bZoomToFit)
{
	const TSharedPtr<SGraphEditor> FocusedGraphEd = FocusedGraphEdPtr.Pin();
	if (FocusedGraphEd.IsValid())
	{
		FocusedGraphEd->ClearSelectionSet();

		for (UObject* Selection : InGraphNodes)
		{
			if (UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(Selection))
			{
				FocusedGraphEd->SetNodeSelection(SelectedNode, true);
			}
		}

		if (bZoomToFit)
		{
			FocusedGraphEd->ZoomToFit(true);
		}
	}
}

TSharedPtr<SGraphPanel> FSMStateMachineBlueprintEditor::GetFocusedGraphPanel() const
{
	if (!FocusedGraphEdPtr.IsValid())
	{
		return nullptr;
	}

	const TSharedPtr<SGraphEditor> GraphEditor = FocusedGraphEdPtr.Pin();
	if (!GraphEditor.IsValid() || !GraphEditor->GetGraphPanel())
	{
		return nullptr;
	}

	return StaticCastSharedPtr<SGraphPanel>(GraphEditor->GetGraphPanel()->AsShared().ToSharedPtr());
}

void FSMStateMachineBlueprintEditor::CloseInvalidTabs()
{
	/*
	 * HACK and work around for UE4 crashing on the second time you undo the creation of a graph while the tab is open.
	 *
	 * We check if a graph is problematic and manually close the tab.
	 *
	 * The problem affects animation state machines as well. Isolating the fix here rather than in separate undo events so
	 * if the engine is ever patched we can just remove it here.
	 *
	 * Steps to reproduce:
	 *
	 * 1. Copy and paste a state
	 * 2. Open the state
	 * 3. Edit undo
	 * WORKS!
	 *
	 * 4. Paste the state again
	 * 5. Open the state
	 * 6. Edit undo
	 * CRASH!
	 */

	for (TSharedPtr<SDockTab> Tab : DocumentManager->GetAllDocumentTabs())
	{
		const TSharedRef<SGraphEditor> GraphEditor = StaticCastSharedRef<SGraphEditor>(Tab->GetContent());
		UEdGraph* CurrentGraph = GraphEditor->GetCurrentGraph();
		if (CurrentGraph == nullptr || CurrentGraph->HasAnyFlags(RF_Transient))
		{
			Tab->RequestCloseTab();
			continue;
		}

		if (CurrentGraph->GetSchema() == nullptr)
		{
			// Schema is gone after an undo... can't just close it. Interior methods still look for a schema.
			CurrentGraph->Schema = USMGraphSchema::StaticClass();
			Tab->RequestCloseTab();
			continue;
		}

		// The above close doesn't like to work but the set schema fixes a crash. What we're left is
		// a tab with a null graph. Luckily we can verify the problem by checking the owning node's bound graph.
		if (USMGraphNode_Base* Node = Cast<USMGraphNode_Base>(CurrentGraph->GetOuter()))
		{
			// This can also be null on 4.21 during normal deletion!
			if (Node->GetBoundGraph() == nullptr)
			{
				Tab->RequestCloseTab();
			}
		}
		// State machine definitions also risk crashing if the editor is open when you delete them by node. Found on 4.21.
		else if (USMGraphK2Node_StateMachineNode* RootNode = Cast<USMGraphK2Node_StateMachineNode>(CurrentGraph->GetOuter()))
		{
			if (RootNode->GetStateMachineGraph() == nullptr)
			{
				Tab->RequestCloseTab();
			}
		}
	}
}

void FSMStateMachineBlueprintEditor::PostUndo(bool bSuccess)
{
	FBlueprintEditor::PostUndo(bSuccess);
	FSMBlueprintEditorUtils::FCacheInvalidationArgs Args;
	Args.bAllowIfTransacting = true;
	FSMBlueprintEditorUtils::InvalidateCaches(LoadedBlueprint.Get(), MoveTemp(Args));
	if (!bSuccess)
	{
		return;
	}

	USMBlueprint* Blueprint = GetStateMachineBlueprint();
	check(Blueprint);

	// Collapsed Graphs can be problematic if a state or transition is deleted which contains a nested graph, and then the user undoes that action.
	// The graph will be present in the graph tree, but the node itself will say "Invalid Graph". This happens whether deleting either the state node or graph.
	// It works fine if deleting the entire state machine and undoing it. There is probably a better way of doing this. TODO: Collapsed Graph revamp.
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);

		USMGraphK2* FoundGraph;
		Graphs.FindItemByClass<USMGraphK2>(&FoundGraph);
		ensure(FoundGraph);

		FSMBlueprintEditorUtils::FixUpCollapsedGraphs(FSMBlueprintEditorUtils::GetTopLevelGraph(FoundGraph));
	}

	const ESMEditorConstructionScriptProjectSetting ConstructionProjectSetting = FSMBlueprintEditorUtils::GetProjectEditorSettings()->EditorNodeConstructionScriptSetting;
	if (ConstructionProjectSetting == ESMEditorConstructionScriptProjectSetting::SM_Standard)
	{
		ISMEditorConstructionManager::Get().RunAllConstructionScriptsForBlueprint(Blueprint);
	}
}

void FSMStateMachineBlueprintEditor::PostRedo(bool bSuccess)
{
	FBlueprintEditor::PostRedo(bSuccess);
	FSMBlueprintEditorUtils::FCacheInvalidationArgs Args;
	Args.bAllowIfTransacting = true;
	FSMBlueprintEditorUtils::InvalidateCaches(LoadedBlueprint.Get(), MoveTemp(Args));

	const ESMEditorConstructionScriptProjectSetting ConstructionProjectSetting = FSMBlueprintEditorUtils::GetProjectEditorSettings()->EditorNodeConstructionScriptSetting;
	if (ConstructionProjectSetting == ESMEditorConstructionScriptProjectSetting::SM_Standard)
	{
		ISMEditorConstructionManager::Get().RunAllConstructionScriptsForBlueprint(GetBlueprintObj());
	}
}

FName FSMStateMachineBlueprintEditor::GetToolkitFName() const
{
	return FName("FSMStateMachineBlueprintEditor");
}

FText FSMStateMachineBlueprintEditor::GetBaseToolkitName() const
{
	return LOCTEXT("SMBlueprintEditorAppLabel", "Logic Driver");
}

FText FSMStateMachineBlueprintEditor::GetToolkitName() const
{
	const TArray<UObject *>& CurrentEditingObjects = GetEditingObjects();
	check(CurrentEditingObjects.Num() > 0);

	const UObject* EditingObject = CurrentEditingObjects[0];

	FFormatNamedArguments Args;
	Args.Add(TEXT("ObjectName"), FText::FromString(EditingObject->GetName()));

	return FText::Format(LOCTEXT("FSMStateMachineBlueprintEditor", "{ObjectName}"), Args);
}

FText FSMStateMachineBlueprintEditor::GetToolkitToolTipText() const
{
	const UObject* EditingObject = GetEditingObject();

	check(EditingObject);

	return GetToolTipTextForObject(EditingObject);
}

FName FSMStateMachineBlueprintEditor::GetEditingAssetTypeName() const
{
	// This is only used under File -> Recent menu, and can open any kind of blueprint (in the appropriate editor).
	// This naming keeps it consistent with normal blueprints, and looks nicer.
	return TEXT("Blueprint");
}

FLinearColor FSMStateMachineBlueprintEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

FString FSMStateMachineBlueprintEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("SMBlueprintEditor");
}

FString FSMStateMachineBlueprintEditor::GetDocumentationLink() const
{
	return TEXT("https://logicdriver.com/docs");
}

void FSMStateMachineBlueprintEditor::Tick(float DeltaTime)
{
	FBlueprintEditor::Tick(DeltaTime);
	
	if (GetCurrentMode() == FSMBlueprintEditorModes::SMPreviewMode && PreviewViewportClient.IsValid())
	{
		PreviewViewportClient.Pin()->OnEditorTick(DeltaTime);
	}
}

void FSMStateMachineBlueprintEditor::ExtendMenu()
{
	if (MenuExtender.IsValid())
	{
		RemoveMenuExtender(MenuExtender);
		MenuExtender.Reset();
	}

	MenuExtender = MakeShareable(new FExtender);
	
	MenuExtender->AddMenuExtension(TEXT("ViewZoom"),
		EExtensionHook::After,
		GetToolkitCommands(),        
		FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.BeginSection("TransitionSection", LOCTEXT("ViewTransitionSection", "Transitions"));
			{
				MenuBuilder.AddMenuEntry(FSMEditorCommands::Get().ShowNamedTransitions);
			}
			MenuBuilder.EndSection();
		}));
	
	AddMenuExtender(MenuExtender);

	// Add our extensible menus.
	ISMSystemEditorModule& SMBlueprintEditorModule = FModuleManager::GetModuleChecked<ISMSystemEditorModule>(LOGICDRIVER_EDITOR_MODULE_NAME);
	AddMenuExtender(SMBlueprintEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	if (bPreviewModeAllowed)
	{
		ISMPreviewEditorModule& SMPreviewEditorModule = FModuleManager::LoadModuleChecked<ISMPreviewEditorModule>(LOGICDRIVER_PREVIEW_MODULE_NAME);
		AddMenuExtender(SMPreviewEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	}
	
	if (FSMBlueprintEditorUtils::GetEditorSettings()->bEnableBlueprintMenuExtenders)
	{
		// Add third party menus.
		
		FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
		AddMenuExtender(BlueprintEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	}
}

void FSMStateMachineBlueprintEditor::ExtendToolbar()
{
	// If the ToolbarExtender is valid, remove it before rebuilding it
	if (ToolbarExtender.IsValid())
	{
		RemoveToolbarExtender(ToolbarExtender);
		ToolbarExtender.Reset();
	}

	ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	ISMSystemEditorModule& SMBlueprintEditorModule = FModuleManager::GetModuleChecked<ISMSystemEditorModule>(LOGICDRIVER_EDITOR_MODULE_NAME);
	AddToolbarExtender(SMBlueprintEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	if (FSMBlueprintEditorUtils::GetEditorSettings()->bEnableBlueprintToolbarExtenders)
	{
		FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
		AddToolbarExtender(BlueprintEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	}
}

void FSMStateMachineBlueprintEditor::BindCommands()
{
	const FSMEditorCommands& EditorCommands = FSMEditorCommands::Get();
	const TSharedRef<FUICommandList>& UICommandList = GetToolkitCommands();

	// Preview Commands
	if (bPreviewModeAllowed)
	{
		UICommandList->MapAction(
			EditorCommands.StartSimulateStateMachine,
			FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::StartPreviewSimulation),
			FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanStartPreviewSimulation)
		);
		
		UICommandList->MapAction(
			EditorCommands.StopSimulateStateMachine,
			FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::StopPreviewSimulation),
			FCanExecuteAction()
		);
		
		UICommandList->MapAction(
			FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::DeletePreviewSelection),
			FCanExecuteAction()
		);
	}
	
	UICommandList->MapAction(
		FSMEditorCommands::Get().ShowNamedTransitions,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::OnToggleNamedTransitions),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FSMStateMachineBlueprintEditor::IsToggleNamedTransitionsChecked)
	);
}

void FSMStateMachineBlueprintEditor::OnDebugObjectSet(UObject* Object)
{
	if (!LoadedBlueprint.IsValid())
	{
		return;
	}

	ResetBlueprintDebugStates();
}

void FSMStateMachineBlueprintEditor::ResetBlueprintDebugStates()
{
	// Locate all sm nodes and reset the debug state to clear up visual ghosting from previous runs.

	TArray<USMGraphNode_Base*> GraphNodes;
	FSMBlueprintEditorUtils::GetAllNodesOfClassNested<USMGraphNode_Base>(LoadedBlueprint.Get(), GraphNodes);

	for (USMGraphNode_Base* GraphNode : GraphNodes)
	{
		GraphNode->ResetDebugState();
	}
}

void FSMStateMachineBlueprintEditor::OnNodeSingleClicked(UObject* Object)
{
	if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Object))
	{
		// HACK: To select a property OnMouseButtonUp, we have to use SGraphEditor::FOnNodeSingleClicked.
		// The graph panel handles mouse events after OnMouseButtonDown to initiate node dragging and doesn't
		// ever call OnMouseButtonUp on the slate widget, meaning we can't hook the event under our SGraphNode or
		// SGraphProperty. If we steal focus OnMouseButtonDown that would work but prevent SGraphPanel logic such
		// as dragging. If we just put our selection logic under OnMouseButtonDown, that also works but is slightly
		// inconsistent behavior. It's somewhat annoying as dragging a state by its property (which is common) would
		// select the property and display the connectors each time.
		
		if (USMGraphK2Node_PropertyNode_Base* PropertyNode = StateNode->GetPropertyNodeUnderMouse())
		{
			StateNode->GetPropertyInteractionManager().TogglePropertySelection(PropertyNode, GetFocusedGraphPanel().ToWeakPtr());
		}
		else
		{
			StateNode->GetPropertyInteractionManager().DeselectAllProperties();
		}
	}
}

void FSMStateMachineBlueprintEditor::SetupGraphEditorEvents(UEdGraph* InGraph,
                                                            SGraphEditor::FGraphEditorEvents& InEvents)
{
	ISMStateMachineBlueprintEditor::SetupGraphEditorEvents(InGraph, InEvents);
	InEvents.OnNodeSingleClicked = SGraphEditor::FOnNodeSingleClicked::CreateSP(this, &FSMStateMachineBlueprintEditor::OnNodeSingleClicked);
}

void FSMStateMachineBlueprintEditor::OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated)
{
	if (!NewlyActivated.IsValid())
	{
		const TArray<UObject*> ObjArray;
		Inspector->ShowDetailsForObjects(ObjArray);
	}
	else
	{
		FBlueprintEditor::OnActiveTabChanged(PreviouslyActive, NewlyActivated);
	}
}

void FSMStateMachineBlueprintEditor::OnSelectedNodesChangedImpl(const TSet<UObject*>& NewSelection)
{
	FBlueprintEditor::OnSelectedNodesChangedImpl(NewSelection);

	if (SelectedStateMachineNode.IsValid())
	{
		SelectedStateMachineNode.Reset();
	}

	// if we only have one node selected, let it know
	if (NewSelection.Num() == 1)
	{
		USMGraphK2Node_Base* NewSelectedStateMachineGraphNode = Cast<USMGraphK2Node_Base>(*NewSelection.CreateConstIterator());
		if (NewSelectedStateMachineGraphNode != nullptr)
		{
			SelectedStateMachineNode = NewSelectedStateMachineGraphNode;
		}
	}

	{
		// Nodes that are now selected (present in NewSelection but not in PreviouslySelectedNodes)
		TSet<UObject*> NewlySelectedNodes;
		for (UObject* Node : NewSelection)
		{
			if (!PreviouslySelectedNodes.Contains(Node))
			{
				NewlySelectedNodes.Add(Node);
			}
		}

		// Nodes that are now deselected (present in PreviouslySelectedNodes but not in NewSelection)
		TSet<UObject*> NewlyDeselectedNodes;
		for (UObject* Node : PreviouslySelectedNodes)
		{
			if (!NewSelection.Contains(Node))
			{
				NewlyDeselectedNodes.Add(Node);
			}
		}

		for (UObject* Node : NewlySelectedNodes)
		{
			if (USMGraphNode_Base* NewNode = Cast<USMGraphNode_Base>(Node))
			{
				NewNode->OnNodeSelectionChanged(true);
			}
		}

		for (UObject* Node : NewlyDeselectedNodes)
		{
			if (USMGraphNode_Base* NewNode = Cast<USMGraphNode_Base>(Node))
			{
				NewNode->OnNodeSelectionChanged(false);
			}
		}
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	OnSelectedNodesChangedEvent.Broadcast(SharedThis(this), NewSelection);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	PreviouslySelectedNodes = NewSelection;
}

void FSMStateMachineBlueprintEditor::OnCreateGraphEditorCommands(TSharedPtr<FUICommandList> GraphEditorCommandsList)
{
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().GoToGraph,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::GoToGraph),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanGoToGraph));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().GoToNodeBlueprint,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::GoToNodeBlueprint),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanGoToNodeBlueprint));
	
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().CreateSelfTransition,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CreateSingleNodeTransition),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanCreateSingleNodeTransition));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().CutAndMergeStates,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CutCombineStates),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanCutOrCopyCombineStates));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().CopyAndMergeStates,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CopyCombineStates),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanCutOrCopyCombineStates));
	
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().CollapseToStateMachine,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CollapseNodesToStateMachine),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanCollapseNodesToStateMachine));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ConvertToStateMachineReference,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ConvertStateMachineToReference),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanConvertStateMachineToReference));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ChangeStateMachineReference,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ChangeStateMachineReference),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanChangeStateMachineReference));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().JumpToStateMachineReference,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::JumpToStateMachineReference),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanJumpToStateMachineReference));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().EnableIntermediateGraph,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::EnableIntermediateGraph),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanEnableIntermediateGraph));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().DisableIntermediateGraph,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::DisableIntermediateGraph),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanDisableIntermediateGraph));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ReplaceWithStateMachine,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ReplaceWithStateMachine),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanReplaceWithStateMachine));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ReplaceWithStateMachineReference,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ReplaceWithStateMachineReference),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanReplaceWithStateMachineReference));
	
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ReplaceWithStateMachineParent,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ReplaceWithStateMachineParent),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanReplaceWithStateMachineParent));
	
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ReplaceWithState,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ReplaceWithState),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanReplaceWithState));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ReplaceWithConduit,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ReplaceWithConduit),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanReplaceWithConduit));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().GoToPropertyBlueprint,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::GoToPropertyBlueprint),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanGoToPropertyBlueprint));
	
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().GoToPropertyGraph,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::GoToPropertyGraph),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanGoToPropertyGraph));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().GoToStateStackBlueprint,
	FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::GoToStateStackBlueprint),
	FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanGoToStateStackBlueprint));
	
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().GoToTransitionStackBlueprint,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::GoToTransitionStackBlueprint),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanGoToTransitionStackBlueprint));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ConvertPropertyToGraphEdit,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ToggleGraphPropertyEdit),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanToggleGraphPropertyEdit));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().RevertPropertyToNodeEdit,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ToggleGraphPropertyEdit),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanToggleGraphPropertyEdit));
	
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ResetGraphProperty,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ClearGraphProperty),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanClearGraphProperty));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().AddBreakpoint,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::AddBreakpoint),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanAddBreakpoint),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FSMStateMachineBlueprintEditor::CanAddBreakpoint));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().RemoveBreakpoint,
	FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::RemoveBreakpoint),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanRemoveBreakpoint),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FSMStateMachineBlueprintEditor::CanRemoveBreakpoint));
	
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ToggleBreakpoint,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ToggleBreakpoint),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanToggleBreakpoint),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FSMStateMachineBlueprintEditor::CanToggleBreakpoint));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().EnableBreakpoint,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::EnableBreakpoint),
		FCanExecuteAction::CreateSP( this, &FSMStateMachineBlueprintEditor::CanEnableBreakpoint),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FSMStateMachineBlueprintEditor::CanEnableBreakpoint));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().DisableBreakpoint,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::DisableBreakpoint),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanDisableBreakpoint),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateSP(this, &FSMStateMachineBlueprintEditor::CanDisableBreakpoint));

	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().ActivateState,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::ToggleStateActive),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanToggleStateActive),
		FGetActionCheckState::CreateSP(this, &FSMStateMachineBlueprintEditor::IsStateActive));
	
	GraphEditorCommandsList->MapAction(FSMEditorCommands::Get().TakeTransition,
		FExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::TakeTransition),
		FCanExecuteAction::CreateSP(this, &FSMStateMachineBlueprintEditor::CanTakeTransition));
	
	OnCreateGraphEditorCommandsEvent.Broadcast(this, GraphEditorCommandsList);
}

void FSMStateMachineBlueprintEditor::OnCreateComment()
{
	if (const TSharedPtr<SGraphEditor> GraphEditor = FocusedGraphEdPtr.Pin())
	{
		if (UEdGraph* Graph = GraphEditor->GetCurrentGraph())
		{
			if (const UEdGraphSchema* Schema = Graph->GetSchema())
			{
				if (Schema->IsA(UEdGraphSchema_K2::StaticClass()))
				{
					return ISMStateMachineBlueprintEditor::OnCreateComment();
				}
			}
			
			FSMGraphSchemaAction_NewComment CommentAction;
			CommentAction.PerformAction(Graph, nullptr, GraphEditor->GetPasteLocation2f());
		}
	}
}

void FSMStateMachineBlueprintEditor::CopySelectedNodes()
{
	FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	TSet<UEdGraphNode*> SelectedNodesSet;

	bool bSelectionChanged = false;
	for (UObject* Object : SelectedNodes)
	{
		if (UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Object))
		{
			SelectedNodesSet.Add(GraphNode);
		}

		if (const USMGraphNode_RerouteNode* ThisRerouteNode = Cast<USMGraphNode_RerouteNode>(Object))
		{
			// Reroutes prevent individual transitions from being selected by default. Temporarily
			// select all transitions and reroutes to ensure the entire transition reroute chain is pasted.

			TArray<USMGraphNode_TransitionEdge*> ReroutedTransitions;
			TArray<USMGraphNode_RerouteNode*> RerouteNodes;
			ThisRerouteNode->GetAllReroutedTransitions(ReroutedTransitions, RerouteNodes);

			for (USMGraphNode_TransitionEdge* Transition : ReroutedTransitions)
			{
				if (!SelectedNodes.Contains(Transition))
				{
					AddToSelection(Transition);
					bSelectionChanged = true;
				}
			}

			for (USMGraphNode_RerouteNode* RerouteNode : RerouteNodes)
			{
				if (!SelectedNodes.Contains(RerouteNode))
				{
					AddToSelection(RerouteNode);
					bSelectionChanged = true;
				}
			}
		}
	}

	FBlueprintEditor::CopySelectedNodes();

	if (bSelectionChanged)
	{
		// Nodes were added only to make sure they can be pasted, reset the selection back to the original.
		SelectNodes(SelectedNodesSet);
	}
}

void FSMStateMachineBlueprintEditor::PasteNodes()
{
	FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (UObject* Object : SelectedNodes)
	{
		if (USMGraphNode_Base* Node = Cast<USMGraphNode_Base>(Object))
		{
			SelectedGraphNodesOnPaste.Add(Node);
		}
	}
	
	FBlueprintEditor::PasteNodes();

	SelectedGraphNodesOnPaste.Reset();
}

void FSMStateMachineBlueprintEditor::OnToggleNamedTransitions()
{
	USMEditorSettings* EditorSettings = FSMBlueprintEditorUtils::GetMutableEditorSettings();
	EditorSettings->bDisplayNamedTransitionsTemporarily =
		!EditorSettings->bDisplayNamedTransitionsTemporarily;
}

bool FSMStateMachineBlueprintEditor::IsToggleNamedTransitionsChecked() const
{
	return FSMBlueprintEditorUtils::GetEditorSettings()->bDisplayNamedTransitionsTemporarily;
}

void FSMStateMachineBlueprintEditor::StartPreviewSimulation()
{
	if (!bPreviewModeAllowed)
	{
		return;
	}
	ISMPreviewEditorModule& PreviewModule = FModuleManager::LoadModuleChecked<ISMPreviewEditorModule>(LOGICDRIVER_PREVIEW_MODULE_NAME);
	PreviewModule.StartPreviewSimulation(GetStateMachineBlueprint());
	RegenerateMenusAndToolbars();
}

bool FSMStateMachineBlueprintEditor::CanStartPreviewSimulation() const
{
	ISMPreviewEditorModule& PreviewModule = FModuleManager::LoadModuleChecked<ISMPreviewEditorModule>(LOGICDRIVER_PREVIEW_MODULE_NAME);
	return PreviewModule.CanStartPreviewSimulation(GetStateMachineBlueprint());
}

void FSMStateMachineBlueprintEditor::StopPreviewSimulation()
{
	if (!bPreviewModeAllowed)
	{
		return;
	}

	if (USMBlueprint* Blueprint = GetStateMachineBlueprint())
	{
		ISMPreviewEditorModule& PreviewModule = FModuleManager::LoadModuleChecked<ISMPreviewEditorModule>(LOGICDRIVER_PREVIEW_MODULE_NAME);
		PreviewModule.StopPreviewSimulation(Blueprint);
	}
}

void FSMStateMachineBlueprintEditor::DeletePreviewSelection()
{
	ISMPreviewEditorModule& PreviewModule = FModuleManager::LoadModuleChecked<ISMPreviewEditorModule>(LOGICDRIVER_PREVIEW_MODULE_NAME);
	PreviewModule.DeleteSelection(SharedThis(this));
}

void FSMStateMachineBlueprintEditor::SetPreviewClient(const TSharedPtr<ISMPreviewModeViewportClient>& InPreviewClient)
{
	PreviewViewportClient = InPreviewClient;
}

void FSMStateMachineBlueprintEditor::CreateSingleNodeTransition()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Node))
		{
			// This is a hack since we only want the context menu to be the way to self transition.
			StateNode->bCanTransitionToSelf = true;
			StateNode->GetSchema()->TryCreateConnection(StateNode->GetOutputPin(), StateNode->GetInputPin());
			StateNode->bCanTransitionToSelf = false;
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanCreateSingleNodeTransition() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Node))
		{
			// Skip if already has self transition or it's an Any State Node.
			if (StateNode->HasTransitionFromNode(StateNode) || StateNode->IsA<USMGraphNode_AnyStateNode>() ||
				StateNode->IsA<USMGraphNode_LinkStateNode>())
			{
				continue;
			}

			return true;
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::CollapseNodesToStateMachine()
{
	const TSet<UObject*> Nodes = GetSelectedNodes();

	FSMBlueprintEditorUtils::CollapseNodesAndCreateStateMachine(Nodes);
}

bool FSMStateMachineBlueprintEditor::CanCollapseNodesToStateMachine() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Node))
		{
			if (StateNode->IsA<USMGraphNode_RerouteNode>())
			{
				continue;
			}

			FSMStateMachineNodePlacementValidator Validator;
			return FSMBlueprintEditorUtils::CanStateMachineBePlacedInGraph(StateNode->GetOwningStateMachineGraph(), Validator);
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::CutCombineStates()
{
	FSMBlueprintEditorUtils::CombineStates(SelectedNodeForContext.Get(), GetSelectedNodes(), true);
	// Clear selection down the right clicked node. When the nodes are removed slate may not update the selection set otherwise and can cause a crash.
	ClearSelectionStateFor(SelectionState_Graph);
	AddToSelection(SelectedNodeForContext.Get());
}

void FSMStateMachineBlueprintEditor::CopyCombineStates()
{
	FSMBlueprintEditorUtils::CombineStates(SelectedNodeForContext.Get(), GetSelectedNodes(), false);
}

bool FSMStateMachineBlueprintEditor::CanCutOrCopyCombineStates() const
{
	if (!InEditingMode())
	{
		return false;
	}

	if (!Cast<USMGraphNode_StateNode>(SelectedNodeForContext.Get()))
	{
		return false;
	}

	int32 MergeCount = 0;
	TSet<UObject*> Nodes = GetSelectedNodes();
	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateNode* StateNode = Cast<USMGraphNode_StateNode>(Node))
		{
			if (StateNode != SelectedNodeForContext.Get() && StateNode->IsUsingDefaultNodeClass() && StateNode->GetAllNodeStackTemplates().Num() == 0)
			{
				continue;
			}
			
			if (++MergeCount > 1)
			{
				break;
			}
		}
	}

	return MergeCount > 1;
}

void FSMStateMachineBlueprintEditor::ConvertStateMachineToReference()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	USMGraphNode_StateMachineStateNode* StateMachineNode = nullptr;
	if (Nodes.Num() > 0)
	{
		StateMachineNode = CastChecked<USMGraphNode_StateMachineStateNode>(*Nodes.CreateConstIterator());
	}

	FSMBlueprintEditorUtils::ConvertStateMachineToReference(StateMachineNode);
}

bool FSMStateMachineBlueprintEditor::CanConvertStateMachineToReference() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	for (UObject* Node : Nodes)
	{
		if (Node->IsA<USMGraphNode_StateMachineParentNode>())
		{
			continue;
		}
		
		if (USMGraphNode_StateMachineStateNode* StateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
		{
			if (!StateNode->IsStateMachineReference())
			{
				return FSMBlueprintEditorUtils::CanStateMachineBeConvertedToReference(StateNode->GetOwningStateMachineGraph());
			}
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::ChangeStateMachineReference()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FOpenAssetDialogConfig SelectAssetConfig;
	SelectAssetConfig.DialogTitleOverride = LOCTEXT("ChooseStateMachinePath", "Choose a state machine");
	SelectAssetConfig.bAllowMultipleSelection = false;
	SelectAssetConfig.AssetClassNames.Add(USMBlueprint::StaticClass()->GetClassPathName());

	// Set the path to the current folder.
	if (UBlueprint* Blueprint = GetBlueprintObj())
	{
		UObject* AssetOuter = Blueprint->GetOuter();
		UPackage* AssetPackage = AssetOuter->GetOutermost();

		// Remove the file name and go directly to the folder.
		FString AssetPath = AssetPackage->GetName();
		const int LastSlashPos = AssetPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		SelectAssetConfig.DefaultPath = AssetPath.Left(LastSlashPos);
	}

	TArray<FAssetData> AssetData = ContentBrowserModule.Get().CreateModalOpenAssetDialog(SelectAssetConfig);
	if (AssetData.Num() == 1)
	{
		if (USMBlueprint *ReferencedBlueprint = Cast<USMBlueprint>(AssetData[0].GetAsset()))
		{
			if (!ReferencedBlueprint->HasAnyFlags(RF_Transient) && IsValid(ReferencedBlueprint))
			{
				TSet<UObject*> Nodes = GetSelectedNodes();
				for (UObject* Node : Nodes)
				{
					if (USMGraphNode_StateMachineStateNode* StateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
					{
						FScopedTransaction Transaction(TEXT(""), NSLOCTEXT("UnrealEd", "ChangeStateMachineReference", "Change State Machine Reference"), StateNode);
						StateNode->Modify();
						if (!StateNode->ReferenceStateMachine(ReferencedBlueprint))
						{
							Transaction.Cancel();
							return;
						}

						UBlueprint* Blueprint = FSMBlueprintEditorUtils::FindBlueprintForNodeChecked(StateNode);
						FSMBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					}
				}
			}
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanChangeStateMachineReference() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateMachineStateNode* StateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
		{
			return StateNode->IsStateMachineReference();
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::JumpToStateMachineReference()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateMachineStateNode* StateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
		{
			StateNode->JumpToReference();
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanJumpToStateMachineReference() const
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateMachineStateNode* StateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
		{
			return StateNode->IsStateMachineReference();
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::EnableIntermediateGraph()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateMachineStateNode* StateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
		{
			StateNode->SetUseIntermediateGraph(true);
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanEnableIntermediateGraph() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateMachineStateNode* StateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
		{
			return !StateNode->ShouldUseIntermediateGraph();
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::DisableIntermediateGraph()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateMachineStateNode* StateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
		{
			StateNode->SetUseIntermediateGraph(false);
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanDisableIntermediateGraph() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateMachineStateNode* StateNode = Cast<USMGraphNode_StateMachineStateNode>(Node))
		{
			return StateNode->ShouldUseIntermediateGraph();
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::ReplaceWithStateMachine()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_StateMachineStateNode>(Cast<USMGraphNode_Base>(Node));
	}
}

bool FSMStateMachineBlueprintEditor::CanReplaceWithStateMachine() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	bool bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent;
	USMGraphSchema::CanReplaceNodeWith(Cast<UEdGraphNode>(*Nodes.CreateConstIterator()), bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent);

	return bCanAddStateMachine;
}

void FSMStateMachineBlueprintEditor::ReplaceWithStateMachineReference()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		if (USMGraphNode_StateMachineStateNode* StateMachineRefNode = FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_StateMachineStateNode>(Cast<USMGraphNode_Base>(Node), true))
		{
			StateMachineRefNode->ReferenceStateMachine(nullptr);
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanReplaceWithStateMachineReference() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	bool bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent;
	USMGraphSchema::CanReplaceNodeWith(Cast<UEdGraphNode>(*Nodes.CreateConstIterator()), bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent);

	return bCanAddStateMachineRef;
}

void FSMStateMachineBlueprintEditor::ReplaceWithStateMachineParent()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_StateMachineParentNode>(Cast<USMGraphNode_Base>(Node));
	}
}

bool FSMStateMachineBlueprintEditor::CanReplaceWithStateMachineParent() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	bool bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent;
	USMGraphSchema::CanReplaceNodeWith(Cast<UEdGraphNode>(*Nodes.CreateConstIterator()), bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent);

	return bCanAddStateMachineParent;
}

void FSMStateMachineBlueprintEditor::ReplaceWithState()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_StateNode>(Cast<USMGraphNode_Base>(Node));
	}
}

bool FSMStateMachineBlueprintEditor::CanReplaceWithState() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	bool bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent;
	USMGraphSchema::CanReplaceNodeWith(Cast<UEdGraphNode>(*Nodes.CreateConstIterator()), bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent);

	return bCanAddState;
}

void FSMStateMachineBlueprintEditor::ReplaceWithConduit()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	for (UObject* Node : Nodes)
	{
		FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_ConduitNode>(Cast<USMGraphNode_Base>(Node));
	}
}

bool FSMStateMachineBlueprintEditor::CanReplaceWithConduit() const
{
	if (!InEditingMode())
	{
		return false;
	}

	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	bool bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent;
	USMGraphSchema::CanReplaceNodeWith(Cast<UEdGraphNode>(*Nodes.CreateConstIterator()), bCanAddStateMachine, bCanAddStateMachineRef, bCanAddState, bCanAddConduit, bCanAddStateMachineParent);

	return bCanAddConduit;
}

void FSMStateMachineBlueprintEditor::GoToGraph()
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return;
	}

	for (UObject* Node : Nodes)
	{
		if (const USMGraphNode_Base* GraphNode = Cast<USMGraphNode_Base>(Node))
		{
			GraphNode->GoToLocalGraph();
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanGoToGraph() const
{
	TSet<UObject*> Nodes = GetSelectedNodes();

	if (Nodes.Num() != 1)
	{
		return false;
	}

	for (UObject* Node : Nodes)
	{
		if (const USMGraphNode_Base* GraphNode = Cast<USMGraphNode_Base>(Node))
		{
			if (!GraphNode->CanGoToLocalGraph())
			{
				continue;
			}

			return true;
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::GoToNodeBlueprint()
{
	if (USMGraphNode_Base* Node = Cast<USMGraphNode_Base>(GetSingleSelectedNode()))
	{
		FSMBlueprintEditorUtils::GoToNodeBlueprint(Node);
	}
}

bool FSMStateMachineBlueprintEditor::CanGoToNodeBlueprint() const
{
	if (USMGraphNode_Base* Node = Cast<USMGraphNode_Base>(GetSingleSelectedNode()))
	{
		if (UClass* Class = Node->GetNodeClass())
		{
			if (UBlueprint::GetBlueprintFromClass(Class))
			{
				return true;
			}
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::GoToPropertyBlueprint()
{
	if (USMGraphNode_StateNode* Node = Cast<USMGraphNode_StateNode>(SelectedPropertyNode->GetOwningGraphNode()))
	{
		if (UBlueprint* PropertyBlueprint = SelectedPropertyNode->GetTemplateBlueprint())
		{
			if (USMNodeInstance* Template = SelectedPropertyNode->GetOwningTemplate())
			{
				if (const FSMNode_Base* DebugNode = FSMBlueprintEditorUtils::GetDebugNode(Node))
				{
					const int32 TemplateIndex = Node->GetIndexOfTemplate(Template->GetTemplateGuid());
					if (USMNodeInstance* NodeStackInstance = DebugNode->GetNodeInStack(TemplateIndex))
					{
						PropertyBlueprint->SetObjectBeingDebugged(NodeStackInstance);
					}
					else if (USMNodeInstance* NodeInstance = DebugNode->GetNodeInstance())
					{
						if (UBlueprint* NodeBlueprint = UBlueprint::GetBlueprintFromClass(NodeInstance->GetClass()))
						{
							// Node template not found, verify this is the node blueprint instead.
							if (NodeBlueprint == PropertyBlueprint)
							{
								PropertyBlueprint->SetObjectBeingDebugged(NodeInstance);
							}
						}
					}
				}
			}
			
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(PropertyBlueprint);
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanGoToPropertyBlueprint() const
{
	return IsSelectedPropertyNodeValid() && SelectedPropertyNode->GetTemplateBlueprint() != nullptr;
}

void FSMStateMachineBlueprintEditor::GoToStateStackBlueprint()
{
	if (const USMGraphNode_StateNode* Node = Cast<USMGraphNode_StateNode>(GetSingleSelectedNode()))
	{
		if (const USMNodeInstance* Template = Node->GetHoveredStackTemplate())
		{
			if (UBlueprint* NodeBlueprint = UBlueprint::GetBlueprintFromClass(Template->GetClass()))
			{
				if (const FSMNode_Base* DebugNode = FSMBlueprintEditorUtils::GetDebugNode(Node))
				{
					const TArray<FStateStackContainer>& AllStackInstances = Node->GetAllNodeStackTemplates();

					// Find exact index.
					int32 StackIndex = INDEX_NONE;
					for (int32 Idx = 0; Idx < AllStackInstances.Num(); ++Idx)
					{
						if (AllStackInstances[Idx].NodeStackInstanceTemplate == Template)
						{
							StackIndex = Idx;
							break;
						}
					}

					if (ensure(StackIndex >= 0))
					{
						if (USMNodeInstance* NodeInstance = DebugNode->GetNodeInStack(StackIndex))
						{
							NodeBlueprint->SetObjectBeingDebugged(NodeInstance);
						}
					}
				}

				FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NodeBlueprint);
			}
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanGoToStateStackBlueprint() const
{
	return true;
}

void FSMStateMachineBlueprintEditor::GoToTransitionStackBlueprint()
{
	if (const USMGraphNode_TransitionEdge* Node = Cast<USMGraphNode_TransitionEdge>(GetSingleSelectedNode()))
	{
		if (const USMNodeInstance* Template = Node->GetHoveredStackTemplate())
		{
			if (UBlueprint* NodeBlueprint = UBlueprint::GetBlueprintFromClass(Template->GetClass()))
			{
				if (const FSMNode_Base* DebugNode = FSMBlueprintEditorUtils::GetDebugNode(Node))
				{
					const TArray<FTransitionStackContainer>& AllStackInstances = Node->GetAllNodeStackTemplates();

					// Find exact index.
					int32 StackIndex = INDEX_NONE;
					for (int32 Idx = 0; Idx < AllStackInstances.Num(); ++Idx)
					{
						if (AllStackInstances[Idx].NodeStackInstanceTemplate == Template)
						{
							StackIndex = Idx;
							break;
						}
					}

					if (ensure(StackIndex >= 0))
					{
						if (USMNodeInstance* NodeInstance = DebugNode->GetNodeInStack(StackIndex))
						{
							NodeBlueprint->SetObjectBeingDebugged(NodeInstance);
						}
					}
				}

				FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NodeBlueprint);
			}
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanGoToTransitionStackBlueprint() const
{
	// Command is added in only if hovered.
	return true;
}

void FSMStateMachineBlueprintEditor::GoToPropertyGraph()
{
	SelectedPropertyNode->JumpToPropertyGraph();
}

bool FSMStateMachineBlueprintEditor::CanGoToPropertyGraph() const
{
	return IsSelectedPropertyNodeValid(false);
}

void FSMStateMachineBlueprintEditor::ClearGraphProperty()
{
	SelectedPropertyNode->ResetProperty();
}

bool FSMStateMachineBlueprintEditor::CanClearGraphProperty() const
{
	if (!InEditingMode())
	{
		return false;
	}

	return IsSelectedPropertyNodeValid();
}

void FSMStateMachineBlueprintEditor::ToggleGraphPropertyEdit()
{
	SelectedPropertyNode->GetPropertyGraph()->ToggleGraphPropertyEdit();
}

bool FSMStateMachineBlueprintEditor::CanToggleGraphPropertyEdit() const
{
	if (!InEditingMode())
	{
		return false;
	}

	if (IsSelectedPropertyNodeValid())
	{
		return SelectedPropertyNode->GetPropertyNodeChecked()->AllowToggleGraphEdit();
	}

	return false;
}

void FSMStateMachineBlueprintEditor::AddBreakpoint()
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		if (SelectedNode->CanPlaceBreakpoints() && SelectedNode->GetBreakpoint() == nullptr)
		{
			SelectedNode->CreateBreakpoint();
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanAddBreakpoint() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (const USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		{
			if (SelectedNode->CanPlaceBreakpoints() && SelectedNode->GetBreakpoint() == nullptr)
			{
				return true;
			}
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::RemoveBreakpoint()
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		{
			SelectedNode->RemoveBreakpoint();
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanRemoveBreakpoint() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (const USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		{
			if (SelectedNode->GetBreakpoint() != nullptr)
			{
				return true;
			}
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::ToggleBreakpoint()
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		{
			if (SelectedNode->CanPlaceBreakpoints())
			{
				if (SelectedNode->GetBreakpoint() == nullptr)
				{
					SelectedNode->CreateBreakpoint();
				}
				else
				{
					SelectedNode->RemoveBreakpoint();
				}
			}
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanToggleBreakpoint() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (const USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		{
			if (SelectedNode->CanPlaceBreakpoints())
			{
				return true;
			}
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::EnableBreakpoint()
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		{
			if (SelectedNode->HasTemporaryBreakpoint())
			{
				// Just recreate with a user provided breakpoint.
				SelectedNode->RemoveBreakpoint();
				SelectedNode->CreateBreakpoint();
			}
			else
			{
				SelectedNode->SetBreakpointEnabled(true);
			}
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanEnableBreakpoint() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (const USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		{
			if (const FBlueprintBreakpoint* ExistingBreakpoint = SelectedNode->GetBreakpoint())
			{
				if (!ExistingBreakpoint->IsEnabledByUser())
				{
					return true;
				}
			}
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::DisableBreakpoint()
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		{
			SelectedNode->SetBreakpointEnabled(false);
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanDisableBreakpoint() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (const USMGraphNode_StateNodeBase* SelectedNode = Cast<USMGraphNode_StateNodeBase>(*NodeIt))
		{
			if (const FBlueprintBreakpoint* ExistingBreakpoint = SelectedNode->GetBreakpoint())
			{
				if (ExistingBreakpoint->IsEnabledByUser())
				{
					return true;
				}
			}
		}
	}

	return false;
}

void FSMStateMachineBlueprintEditor::ToggleStateActive()
{
	FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	
	for (UObject* Object : SelectedNodes)
	{
		if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Object))
		{
			if (FSMNode_Base* Node = StateNode->GetDebugNode())
			{
				// May not be instantiated yet, have to go through the node instance for debug options.
				if (USMStateInstance_Base* StateInstance = Cast<USMStateInstance_Base>(Node->GetOrCreateNodeInstance()))
				{
					if (const USMInstance* DebugInstance = StateInstance->GetStateMachineInstance())
					{
						if (DebugInstance->IsActive())
						{
							StateInstance->SetActive(!StateInstance->IsActive(), true);
						}
					}
				}
			}
		}
	}
}

bool FSMStateMachineBlueprintEditor::CanToggleStateActive() const
{
	if (FKismetDebugUtilities::GetCurrentInstruction())
	{
		// Can't reliably arbitrarily change active states in breakpoint mode.
		return false;
	}
	
	if (LoadedBlueprint.IsValid() && LoadedBlueprint->GetObjectBeingDebugged())
	{
		if (const USMInstance* DebugInstance = Cast<USMInstance>(LoadedBlueprint->GetObjectBeingDebugged()))
		{
			return DebugInstance->IsActive();
		}
	}
	
	return false;
}

ECheckBoxState FSMStateMachineBlueprintEditor::IsStateActive() const
{
	FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	TOptional<ECheckBoxState> CheckBoxState;
	
	for (UObject* Object : SelectedNodes)
	{
		if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Object))
		{
			const bool bIsActive = StateNode->IsDebugNodeActive();

			if (!CheckBoxState.IsSet())
			{
				CheckBoxState = bIsActive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				continue;
			}
			
			switch (CheckBoxState.GetValue())
			{
			case ECheckBoxState::Checked:
				CheckBoxState = bIsActive ? ECheckBoxState::Checked : ECheckBoxState::Undetermined;
				break;
			case ECheckBoxState::Unchecked:
				CheckBoxState = bIsActive ? ECheckBoxState::Undetermined : ECheckBoxState::Unchecked;
				break;
			case ECheckBoxState::Undetermined:
				break;
			}
		}
	}
	
	return CheckBoxState.Get(ECheckBoxState::Unchecked);
}

void FSMStateMachineBlueprintEditor::TakeTransition()
{
	FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	
	const USMGraphNode_StateNodeBase* SharedFromState = nullptr;
	const USMGraphNode_StateNodeBase* SharedToState = nullptr;
	USMInstance* DebugInstance = nullptr;
	TArray<USMGraphNode_TransitionEdge*> SelectedTransitions;
	for (UObject* Object : SelectedNodes)
	{
		if (USMGraphNode_TransitionEdge* Transition = FSMBlueprintEditorUtils::GetPrimaryTransitionFromTransitionOrReroute(Cast<USMGraphNode_Base>(Object)))
		{
			if (const USMGraphNode_StateNodeBase* FromState = Transition->GetFromState())
			{
				if (const USMGraphNode_StateNodeBase* ToState = Transition->GetToState())
				{
					if (const FSMNode_Base* TransitionDebugNode = Transition->GetDebugNode())
					{
						if ((SharedFromState == nullptr || SharedFromState == FromState || FromState->IsA<USMGraphNode_ConduitNode>())
							&& (SharedToState == nullptr || SharedToState == ToState || ToState->IsA<USMGraphNode_ConduitNode>()))
						{
							bool bValidTransitionChain = true;
							for (USMGraphNode_TransitionEdge* SelectedTransition : SelectedTransitions)
							{
								bValidTransitionChain = FSMBlueprintEditorUtils::AreTransitionsConnectedByConduit(SelectedTransition, Transition);
								if (!bValidTransitionChain)
								{
									break;
								}
							}
							if (!bValidTransitionChain)
							{
								continue;
							}
							
							if (!FromState->IsA<USMGraphNode_ConduitNode>())
							{
								SharedFromState = FromState;
							}
							
							if (!ToState->IsA<USMGraphNode_ConduitNode>())
							{
								SharedToState = ToState;
							}
							
							SelectedTransitions.Add(Transition);
							ensure(DebugInstance == nullptr || DebugInstance == TransitionDebugNode->GetOwningInstance());
							DebugInstance = TransitionDebugNode->GetOwningInstance();
						}
						else
						{
							// Can't make transition chain.
							return;
						}
					}
				}
			}
		}
	}
	
	if (DebugInstance && SelectedTransitions.Num() > 0)
	{
		TArray<USMTransitionInstance*> TransitionInstances;
		for (const USMGraphNode_TransitionEdge* Transition : SelectedTransitions)
		{
			if (FSMNode_Base* TransitionDebugNode = Transition->GetDebugNode())
			{
				USMTransitionInstance* TransitionInstance = CastChecked<USMTransitionInstance>(TransitionDebugNode->GetOrCreateNodeInstance());
				TransitionInstances.Add(TransitionInstance);
			}
		}

		DebugInstance->TakeTransitionChain(TransitionInstances);
	}
}

bool FSMStateMachineBlueprintEditor::CanTakeTransition() const
{
	if (FKismetDebugUtilities::GetCurrentInstruction())
	{
		// Can't reliably switch states when break pointed during execution.
		return false;
	}
	
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	
	for (UObject* Object : SelectedNodes)
	{
		if (const USMGraphNode_TransitionEdge* Transition =
			FSMBlueprintEditorUtils::GetPrimaryTransitionFromTransitionOrReroute(Cast<USMGraphNode_Base>(Object)))
		{
			if (const USMGraphNode_StateNodeBase* FromState = Transition->GetFromState())
			{
				if (const FSMNode_Base* Node = FromState->GetDebugNode())
				{
					return Node->IsActive();
				}
				if (const USMGraphNode_AnyStateNode* AnyState = Cast<USMGraphNode_AnyStateNode>(FromState))
				{
					// We should be able to determine if the current state excludes this Any State, but determining
					// this from the editor at this point isn't reliable, especially if we're running parallel states.
					// Since this is for debugging, it's not a big deal if Any State transitions are always allowed.
					return true;
				}
			}
		}
	}
	
	return false;
}

FSMNodeBlueprintEditor::FSMNodeBlueprintEditor()
{
}

FSMNodeBlueprintEditor::~FSMNodeBlueprintEditor()
{
}

FText FSMNodeBlueprintEditor::GetBaseToolkitName() const
{
	return LOCTEXT("SMNodeBlueprintEditorAppLabel", "Logic Driver");
}

FName FSMNodeBlueprintEditor::GetEditingAssetTypeName() const
{
	// This is only used under File -> Recent menu, and can open any kind of blueprint (in the appropriate editor).
	// This naming keeps it consistent with normal blueprints, and looks nicer.
	return TEXT("Blueprint");
}

FString FSMNodeBlueprintEditor::GetDocumentationLink() const
{
	return TEXT("https://logicdriver.com/docs");
}

FGraphAppearanceInfo FSMNodeBlueprintEditor::GetGraphAppearance(UEdGraph* InGraph) const
{
	FGraphAppearanceInfo AppearanceInfo = FBlueprintEditor::GetGraphAppearance(InGraph);

	if (Cast<USMNodeBlueprint>(GetBlueprintObj()))
	{
		LD::EditorAuth::LDAuthStatus AuthenticationStatus = LD::EditorAuth::GetUserMarketplaceAuthenticationStatus();

		if (AuthenticationStatus == LD::EditorAuth::LDAuthStatus::Succeeded || FSMAuthenticator::Get().IsAuthenticated())
		{
			AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_NodeBlueprint", "NODE BLUEPRINT");
		}
		else
		{
			AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_NodeBlueprintInvalid", "LOGIC DRIVER (INVALID LICENSE)");
		}
	}

	return MoveTemp(AppearanceInfo);
}

void FSMNodeBlueprintEditor::InitNodeBlueprintEditor(const EToolkitMode::Type Mode,
                                                     const TSharedPtr<IToolkitHost>& InitToolkitHost, const TArray<UBlueprint*>& InBlueprints,
                                                     bool bShouldOpenInDefaultsMode)
{
	InitBlueprintEditor(Mode, InitToolkitHost, InBlueprints, bShouldOpenInDefaultsMode);
}

void FSMNodeBlueprintEditor::OnBlueprintChangedImpl(UBlueprint* InBlueprint, bool bIsJustBeingCompiled)
{
	FBlueprintEditor::OnBlueprintChangedImpl(InBlueprint, bIsJustBeingCompiled);

	TArray<UBlueprint*> Blueprints;
	FSMBlueprintEditorUtils::GetDependentBlueprints(InBlueprint, Blueprints);

	for (UBlueprint* Blueprint : Blueprints)
	{
		if (Blueprint->IsA<USMBlueprint>() && !Blueprint->bIsRegeneratingOnLoad)
		{
			// First check for a circular dependency where this blueprint is also dependent on its dependent.
			// In that case we just want to continue because UE will handle this. Otherwise the compile fails with little information.
			TArray<UBlueprint*> OtherBlueprints;
			FSMBlueprintEditorUtils::GetDependentBlueprints(Blueprint, OtherBlueprints);
			if (OtherBlueprints.Contains(InBlueprint))
			{
				continue;
			}

			// TODO: The first call to ensure cached dependencies may fix a rare crash involving a REINST template class of this class.
			FSMBlueprintEditorUtils::EnsureCachedDependenciesUpToDate(Blueprint);
			if (bIsJustBeingCompiled)
			{
				/* If this is part of a compile (compile button pressed) then attempt a full compile of the state machine. This will
				 * also refresh graph properties exposed on the node. */
				FSMBlueprintEditorUtils::ConditionallyCompileBlueprint(Blueprint, true, true);
			}
			else
			{
				// A change not caused by a compile. Mark the state machine dirty so it can be recompiled later.
				// Don't just change compile state, mark the asset dirty so it can save structural or property
				// changes. Especially important if a parent node calls this.
				FSMBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
