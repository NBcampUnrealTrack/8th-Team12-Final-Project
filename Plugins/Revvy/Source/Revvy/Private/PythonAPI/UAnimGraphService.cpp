// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UAnimGraphService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyAnimGraph, Log, All);

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "Animation/BlendProfile.h"
#include "Engine/SkeletalMesh.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "Animation/AnimInstance.h"
#include "AnimGraphNode_ControlRig.h"
#include "AnimNode_ControlRig.h"
#include "ControlRig.h"
#include "Engine/MemberReference.h"
#include "UObject/UnrealType.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateGraphSchema.h"
#include "AnimationTransitionGraph.h"
#include "AnimationTransitionSchema.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_Base.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "IAnimationBlueprintEditor.h"
#include "IAnimationBlueprintEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorLibrary.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "GraphEditorActions.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetMathLibrary.h"
#include "AlphaBlend.h"
#include "ScopedTransaction.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

// =============================================================================
// P0 Revvy upgrade — shared mutation scope (transaction + Modify + structural-modified [+ opt compile])
//
// Every AnimBP-mutating UFUNCTION should wrap its body with one of these. Without
// it, edits are not undoable, the package never gets dirty-flagged, and downstream
// tools see stale CDO state because no re-compile is triggered.
//
// Usage:
//   UAnimBlueprint* ABP = LoadAnimBlueprint(Path);
//   if (!ABP) return <error>;
//   FAnimBPMutationScope Scope(TEXT("Connect Anim Nodes"), ABP);
//   ... do work ...
//   Scope.MarkSuccess();   // fires MarkBlueprintAsStructurallyModified on dtor
//   // Optional: Scope.RequestCompile();  // additionally CompileBlueprint at dtor
//   return <success>;
// =============================================================================

namespace
{
	struct FAnimBPMutationScope
	{
		FScopedTransaction Tx;
		UAnimBlueprint* ABP = nullptr;
		UEdGraphNode* Node = nullptr;
		bool bMarkOnExit = false;
		bool bCompileOnExit = false;

		FAnimBPMutationScope(const TCHAR* Desc, UAnimBlueprint* InABP, UEdGraphNode* InNode = nullptr)
			: Tx(FText::FromString(Desc))
			, ABP(InABP)
			, Node(InNode)
		{
			if (ABP) { ABP->Modify(); }
			if (Node) { Node->Modify(); }
		}

		void MarkSuccess() { bMarkOnExit = true; }
		void RequestCompile() { bCompileOnExit = true; }

		~FAnimBPMutationScope()
		{
			if (bMarkOnExit && ABP)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ABP);
				if (bCompileOnExit)
				{
					FKismetEditorUtilities::CompileBlueprint(ABP, EBlueprintCompileOptions::SkipGarbageCollection);
				}
			}
		}
	};

	/**
	 * Locate a UAnimGraphNode_LayeredBoneBlend by NodeGuid string within a named graph.
	 * Returns nullptr on any lookup failure (bad guid parse, missing graph, missing/wrong-type node)
	 * and logs a diagnostic so the caller can just return false.
	 */
	UAnimGraphNode_LayeredBoneBlend* FindLayeredBoneBlendNode(
		UAnimBlueprint* AnimBlueprint,
		const FString& GraphName,
		const FString& NodeId)
	{
		if (!AnimBlueprint)
		{
			return nullptr;
		}

		// Inline graph lookup — UAnimGraphService::FindAnimGraph is private,
		// so replicate its behaviour (case-insensitive name match across every graph).
		UEdGraph* TargetGraph = nullptr;
		{
			TArray<UEdGraph*> AllGraphs;
			AnimBlueprint->GetAllGraphs(AllGraphs);
			for (UEdGraph* G : AllGraphs)
			{
				if (G && G->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
				{
					TargetGraph = G;
					break;
				}
			}
		}
		if (!TargetGraph)
		{
			UE_LOG(LogRevvyAnimGraph, Error, TEXT("FindLayeredBoneBlendNode: Graph '%s' not found"), *GraphName);
			return nullptr;
		}

		FGuid ParsedGuid;
		if (!FGuid::Parse(NodeId, ParsedGuid))
		{
			UE_LOG(LogRevvyAnimGraph, Error, TEXT("FindLayeredBoneBlendNode: Failed to parse NodeId '%s'"), *NodeId);
			return nullptr;
		}

		for (UEdGraphNode* GraphNode : TargetGraph->Nodes)
		{
			if (GraphNode && GraphNode->NodeGuid == ParsedGuid)
			{
				UAnimGraphNode_LayeredBoneBlend* LBB = Cast<UAnimGraphNode_LayeredBoneBlend>(GraphNode);
				if (!LBB)
				{
					UE_LOG(LogRevvyAnimGraph, Error,
						TEXT("FindLayeredBoneBlendNode: Node '%s' is not a LayeredBoneBlend (actual class '%s')"),
						*NodeId, *GraphNode->GetClass()->GetName());
				}
				return LBB;
			}
		}

		UE_LOG(LogRevvyAnimGraph, Error, TEXT("FindLayeredBoneBlendNode: No node with guid '%s' in graph '%s'"),
			*NodeId, *GraphName);
		return nullptr;
	}
}

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

UAnimBlueprint* UAnimGraphService::LoadAnimBlueprint(const FString& AnimBlueprintPath)
{
	if (AnimBlueprintPath.IsEmpty())
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::LoadAnimBlueprint: Path is empty"));
		return nullptr;
	}

	UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AnimBlueprintPath);
	if (!LoadedObject)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::LoadAnimBlueprint: Failed to load: %s"), *AnimBlueprintPath);
		return nullptr;
	}

	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(LoadedObject);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::LoadAnimBlueprint: Not an AnimBlueprint: %s"), *AnimBlueprintPath);
		return nullptr;
	}

	return AnimBlueprint;
}

UEdGraph* UAnimGraphService::FindAnimGraph(UAnimBlueprint* AnimBlueprint, const FString& GraphName)
{
	if (!AnimBlueprint)
	{
		return nullptr;
	}

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	return nullptr;
}

UAnimGraphNode_StateMachine* UAnimGraphService::FindStateMachineNode(UAnimBlueprint* AnimBlueprint, const FString& MachineName)
{
	if (!AnimBlueprint)
	{
		return nullptr;
	}

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachine* StateMachineNode = Cast<UAnimGraphNode_StateMachine>(Node);
			if (StateMachineNode)
			{
				FString NodeTitle = StateMachineNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				if (NodeTitle.Equals(MachineName, ESearchCase::IgnoreCase) ||
					StateMachineNode->EditorStateMachineGraph->GetName().Equals(MachineName, ESearchCase::IgnoreCase))
				{
					return StateMachineNode;
				}
			}
		}
	}

	return nullptr;
}

UAnimStateNodeBase* UAnimGraphService::FindStateNode(UEdGraph* StateMachineGraph, const FString& StateName)
{
	if (!StateMachineGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(Node);
		if (StateNode)
		{
			FString NodeTitle = StateNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (NodeTitle.Equals(StateName, ESearchCase::IgnoreCase) ||
				StateNode->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
			{
				return StateNode;
			}
		}
	}

	return nullptr;
}

UAnimStateTransitionNode* UAnimGraphService::FindTransitionNode(UEdGraph* StateMachineGraph, const FString& SourceState, const FString& DestState)
{
	if (!StateMachineGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
		if (TransitionNode)
		{
			UAnimStateNodeBase* PrevState = TransitionNode->GetPreviousState();
			UAnimStateNodeBase* NextState = TransitionNode->GetNextState();

			if (PrevState && NextState)
			{
				FString PrevName = PrevState->GetStateName();
				FString NextName = NextState->GetStateName();

				if (PrevName.Equals(SourceState, ESearchCase::IgnoreCase) &&
					NextName.Equals(DestState, ESearchCase::IgnoreCase))
				{
					return TransitionNode;
				}
			}
		}
	}

	return nullptr;
}

IAnimationBlueprintEditor* UAnimGraphService::GetAnimBlueprintEditor(UAnimBlueprint* AnimBlueprint)
{
	if (!AnimBlueprint || !GEditor)
	{
		return nullptr;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return nullptr;
	}

	// Try to get existing editor first
	IAssetEditorInstance* ExistingEditor = AssetEditorSubsystem->FindEditorForAsset(AnimBlueprint, false);
	if (!ExistingEditor)
	{
		// Open the editor
		if (!AssetEditorSubsystem->OpenEditorForAsset(AnimBlueprint))
		{
			UE_LOG(LogRevvyAnimGraph, Error, TEXT("UAnimGraphService: Failed to open editor for AnimBlueprint"));
			return nullptr;
		}
		ExistingEditor = AssetEditorSubsystem->FindEditorForAsset(AnimBlueprint, false);
	}

	if (!ExistingEditor)
	{
		return nullptr;
	}

	// Cast to the animation blueprint editor interface
	return static_cast<IAnimationBlueprintEditor*>(ExistingEditor);
}

// ============================================================================
// GRAPH NAVIGATION
// ============================================================================

bool UAnimGraphService::OpenAnimGraph(const FString& AnimBlueprintPath, const FString& GraphName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UEdGraph* Graph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::OpenAnimGraph: Graph '%s' not found"), *GraphName);
		return false;
	}

	IAnimationBlueprintEditor* Editor = GetAnimBlueprintEditor(AnimBlueprint);
	if (!Editor)
	{
		return false;
	}

	// Focus on the graph
	Editor->SetCurrentMode(FName("AnimationBlueprintEditorMode"));

	// Use FocusedGraphEditorChanged or OpenDocument to navigate to the specific graph
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Graph, false);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("UAnimGraphService::OpenAnimGraph: Opened graph '%s'"), *GraphName);
	return true;
}

bool UAnimGraphService::OpenAnimState(const FString& AnimBlueprintPath, const FString& StateMachineName, const FString& StateName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	// Find the state machine
	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::OpenAnimState: State machine '%s' not found"), *StateMachineName);
		return false;
	}

	UEdGraph* StateMachineGraph = StateMachineNode->EditorStateMachineGraph;
	if (!StateMachineGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::OpenAnimState: Could not get state machine graph"));
		return false;
	}

	// Find the state
	UAnimStateNodeBase* StateNode = FindStateNode(StateMachineGraph, StateName);
	if (!StateNode)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::OpenAnimState: State '%s' not found in '%s'"), *StateName, *StateMachineName);
		return false;
	}

	IAnimationBlueprintEditor* Editor = GetAnimBlueprintEditor(AnimBlueprint);
	if (!Editor)
	{
		return false;
	}

	// Focus on the state node - this should open its internal graph
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(StateNode, false);

	// If the state has a bound graph (internal logic), try to open it
	UAnimStateNode* AnimState = Cast<UAnimStateNode>(StateNode);
	if (AnimState && AnimState->BoundGraph)
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(AnimState->BoundGraph, false);
	}

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("UAnimGraphService::OpenAnimState: Opened state '%s' in '%s'"), *StateName, *StateMachineName);
	return true;
}

bool UAnimGraphService::OpenTransition(const FString& AnimBlueprintPath, const FString& StateMachineName, const FString& SourceStateName, const FString& DestStateName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::OpenTransition: State machine '%s' not found"), *StateMachineName);
		return false;
	}

	UEdGraph* StateMachineGraph = StateMachineNode->EditorStateMachineGraph;
	if (!StateMachineGraph)
	{
		return false;
	}

	UAnimStateTransitionNode* TransitionNode = FindTransitionNode(StateMachineGraph, SourceStateName, DestStateName);
	if (!TransitionNode)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::OpenTransition: Transition from '%s' to '%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	IAnimationBlueprintEditor* Editor = GetAnimBlueprintEditor(AnimBlueprint);
	if (!Editor)
	{
		return false;
	}

	// Focus on the transition node
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(TransitionNode, false);

	// Open the transition's bound graph (the transition rule)
	if (TransitionNode->BoundGraph)
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(TransitionNode->BoundGraph, false);
	}

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("UAnimGraphService::OpenTransition: Opened transition '%s' -> '%s'"), *SourceStateName, *DestStateName);
	return true;
}

bool UAnimGraphService::FocusNode(const FString& AnimBlueprintPath, const FString& NodeId)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	FGuid SearchGuid;
	if (!FGuid::Parse(NodeId, SearchGuid))
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::FocusNode: Invalid GUID format: %s"), *NodeId);
		return false;
	}

	// Search all graphs for the node
	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == SearchGuid)
			{
				IAnimationBlueprintEditor* Editor = GetAnimBlueprintEditor(AnimBlueprint);
				if (!Editor)
				{
					return false;
				}

				FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Node, false);
				UE_LOG(LogRevvyAnimGraph, Log, TEXT("UAnimGraphService::FocusNode: Focused on node '%s'"), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				return true;
			}
		}
	}

	UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::FocusNode: Node with GUID '%s' not found"), *NodeId);
	return false;
}

// ============================================================================
// GRAPH INTROSPECTION
// ============================================================================

TArray<FAnimGraphInfo> UAnimGraphService::ListGraphs(const FString& AnimBlueprintPath)
{
	TArray<FAnimGraphInfo> GraphInfos;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return GraphInfos;
	}

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}

		FAnimGraphInfo Info;
		Info.GraphName = Graph->GetName();
		Info.NodeCount = Graph->Nodes.Num();

		// Determine graph type
		if (Cast<UAnimationGraph>(Graph))
		{
			Info.GraphType = TEXT("AnimGraph");
		}
		else if (Cast<UAnimationStateMachineGraph>(Graph))
		{
			Info.GraphType = TEXT("StateMachine");
		}
		else if (Graph->GetName().Contains(TEXT("EventGraph")))
		{
			Info.GraphType = TEXT("EventGraph");
		}
		else
		{
			// Check if this is a state's bound graph
			bool bIsStateGraph = false;
			for (UEdGraph* OuterGraph : Graphs)
			{
				if (!OuterGraph)
				{
					continue;
				}

				for (UEdGraphNode* Node : OuterGraph->Nodes)
				{
					UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
					if (StateNode && StateNode->BoundGraph == Graph)
					{
						Info.GraphType = TEXT("State");
						Info.ParentGraphName = OuterGraph->GetName();
						bIsStateGraph = true;
						break;
					}

					UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node);
					if (TransNode && TransNode->BoundGraph == Graph)
					{
						Info.GraphType = TEXT("Transition");
						Info.ParentGraphName = OuterGraph->GetName();
						bIsStateGraph = true;
						break;
					}
				}

				if (bIsStateGraph)
				{
					break;
				}
			}

			if (!bIsStateGraph)
			{
				Info.GraphType = TEXT("Other");
			}
		}

		GraphInfos.Add(Info);
	}

	return GraphInfos;
}

TArray<FAnimStateMachineInfo> UAnimGraphService::ListStateMachines(const FString& AnimBlueprintPath)
{
	TArray<FAnimStateMachineInfo> Machines;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return Machines;
	}

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_StateMachine* StateMachineNode = Cast<UAnimGraphNode_StateMachine>(Node);
			if (StateMachineNode)
			{
				FAnimStateMachineInfo Info;
				Info.MachineName = StateMachineNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				Info.NodeId = StateMachineNode->NodeGuid.ToString();
				Info.ParentGraphName = Graph->GetName();

				// Count states
				UEdGraph* SMGraph = StateMachineNode->EditorStateMachineGraph;
				if (SMGraph)
				{
					int32 StateCount = 0;
					for (UEdGraphNode* SMNode : SMGraph->Nodes)
					{
						if (Cast<UAnimStateNodeBase>(SMNode) && !Cast<UAnimStateEntryNode>(SMNode))
						{
							StateCount++;
						}
					}
					Info.StateCount = StateCount;
				}

				Machines.Add(Info);
			}
		}
	}

	return Machines;
}

TArray<FAnimStateInfo> UAnimGraphService::ListStatesInMachine(const FString& AnimBlueprintPath, const FString& StateMachineName)
{
	TArray<FAnimStateInfo> States;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return States;
	}

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("UAnimGraphService::ListStatesInMachine: State machine '%s' not found"), *StateMachineName);
		return States;
	}

	UEdGraph* StateMachineGraph = StateMachineNode->EditorStateMachineGraph;
	if (!StateMachineGraph)
	{
		return States;
	}

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		// Skip entry nodes (they don't inherit from UAnimStateNodeBase)
		if (Cast<UAnimStateEntryNode>(Node))
		{
			continue;
		}

		UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(Node);
		if (!StateNode)
		{
			continue;
		}

		FAnimStateInfo Info;
		Info.StateName = StateNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		Info.NodeId = StateNode->NodeGuid.ToString();
		Info.PosX = StateNode->NodePosX;
		Info.PosY = StateNode->NodePosY;

		// Determine state type
		if (Cast<UAnimStateConduitNode>(StateNode))
		{
			Info.StateType = TEXT("Conduit");
		}
		else if (Cast<UAnimStateNode>(StateNode))
		{
			Info.StateType = TEXT("State");
		}
		else
		{
			Info.StateType = StateNode->GetClass()->GetName();
		}

		// Check if this is an end state (no outgoing transitions)
		bool bHasOutgoingTransitions = false;
		for (UEdGraphNode* OtherNode : StateMachineGraph->Nodes)
		{
			UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(OtherNode);
			if (TransNode && TransNode->GetPreviousState() == StateNode)
			{
				bHasOutgoingTransitions = true;
				break;
			}
		}
		Info.bIsEndState = !bHasOutgoingTransitions;

		States.Add(Info);
	}

	return States;
}

TArray<FAnimTransitionInfo> UAnimGraphService::GetStateTransitions(const FString& AnimBlueprintPath, const FString& StateMachineName, const FString& StateName)
{
	TArray<FAnimTransitionInfo> Transitions;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return Transitions;
	}

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode)
	{
		return Transitions;
	}

	UEdGraph* StateMachineGraph = StateMachineNode->EditorStateMachineGraph;
	if (!StateMachineGraph)
	{
		return Transitions;
	}

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
		if (!TransitionNode)
		{
			continue;
		}

		UAnimStateNodeBase* PrevState = TransitionNode->GetPreviousState();
		UAnimStateNodeBase* NextState = TransitionNode->GetNextState();

		if (!PrevState || !NextState)
		{
			continue;
		}

		// Filter by state name if provided
		if (!StateName.IsEmpty())
		{
			FString PrevName = PrevState->GetStateName();
			FString NextName = NextState->GetStateName();

			if (!PrevName.Equals(StateName, ESearchCase::IgnoreCase) &&
				!NextName.Equals(StateName, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		FAnimTransitionInfo Info;
		Info.TransitionName = TransitionNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		Info.NodeId = TransitionNode->NodeGuid.ToString();
		Info.SourceState = PrevState->GetStateName();
		Info.DestState = NextState->GetStateName();
		Info.Priority = TransitionNode->PriorityOrder;
		Info.BlendDuration = TransitionNode->CrossfadeDuration;
		Info.bIsAutomatic = TransitionNode->bAutomaticRuleBasedOnSequencePlayerInState;
		DescribeTransitionRule(TransitionNode, Info);

		Transitions.Add(Info);
	}

	return Transitions;
}

bool UAnimGraphService::GetStateMachineInfo(const FString& AnimBlueprintPath, const FString& StateMachineName, FAnimStateMachineInfo& OutInfo)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode)
	{
		return false;
	}

	OutInfo.MachineName = StateMachineNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	OutInfo.NodeId = StateMachineNode->NodeGuid.ToString();

	// Find parent graph
	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->Nodes.Contains(StateMachineNode))
		{
			OutInfo.ParentGraphName = Graph->GetName();
			break;
		}
	}

	// Count states
	UEdGraph* SMGraph = StateMachineNode->EditorStateMachineGraph;
	if (SMGraph)
	{
		int32 StateCount = 0;
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (Cast<UAnimStateNodeBase>(Node) && !Cast<UAnimStateEntryNode>(Node))
			{
				StateCount++;
			}
		}
		OutInfo.StateCount = StateCount;
	}

	return true;
}

bool UAnimGraphService::GetStateInfo(const FString& AnimBlueprintPath, const FString& StateMachineName, const FString& StateName, FAnimStateInfo& OutInfo)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode)
	{
		return false;
	}

	UEdGraph* StateMachineGraph = StateMachineNode->EditorStateMachineGraph;
	if (!StateMachineGraph)
	{
		return false;
	}

	UAnimStateNodeBase* StateNode = FindStateNode(StateMachineGraph, StateName);
	if (!StateNode)
	{
		return false;
	}

	OutInfo.StateName = StateNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	OutInfo.NodeId = StateNode->NodeGuid.ToString();
	OutInfo.PosX = StateNode->NodePosX;
	OutInfo.PosY = StateNode->NodePosY;

	if (Cast<UAnimStateConduitNode>(StateNode))
	{
		OutInfo.StateType = TEXT("Conduit");
	}
	else if (Cast<UAnimStateNode>(StateNode))
	{
		OutInfo.StateType = TEXT("State");
	}
	else
	{
		OutInfo.StateType = StateNode->GetClass()->GetName();
	}

	// Check for outgoing transitions
	bool bHasOutgoing = false;
	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node);
		if (TransNode && TransNode->GetPreviousState() == StateNode)
		{
			bHasOutgoing = true;
			break;
		}
	}
	OutInfo.bIsEndState = !bHasOutgoing;

	return true;
}

// ============================================================================
// ANIMATION ASSET ANALYSIS
// ============================================================================

TArray<FAnimSequenceUsageInfo> UAnimGraphService::GetUsedAnimSequences(const FString& AnimBlueprintPath)
{
	TArray<FAnimSequenceUsageInfo> Sequences;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return Sequences;
	}

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			// Check for sequence player nodes
			UAnimGraphNode_SequencePlayer* SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(Node);
			if (SeqPlayer)
			{
				// Get the sequence from the node
				UAnimSequenceBase* Sequence = SeqPlayer->Node.GetSequence();
				if (Sequence)
				{
					FAnimSequenceUsageInfo Info;
					Info.SequencePath = Sequence->GetPathName();
					Info.SequenceName = Sequence->GetName();
					Info.UsedInGraph = Graph->GetName();
					Info.UsedByNode = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

					// Avoid duplicates
					bool bAlreadyAdded = false;
					for (const FAnimSequenceUsageInfo& Existing : Sequences)
					{
						if (Existing.SequencePath == Info.SequencePath && Existing.UsedInGraph == Info.UsedInGraph)
						{
							bAlreadyAdded = true;
							break;
						}
					}

					if (!bAlreadyAdded)
					{
						Sequences.Add(Info);
					}
				}
			}
		}
	}

	return Sequences;
}

FString UAnimGraphService::GetSkeleton(const FString& AnimBlueprintPath)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	if (AnimBlueprint->TargetSkeleton)
	{
		return AnimBlueprint->TargetSkeleton->GetPathName();
	}

	return FString();
}

FString UAnimGraphService::GetPreviewMesh(const FString& AnimBlueprintPath)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	USkeletalMesh* PreviewMesh = AnimBlueprint->GetPreviewMesh();
	if (PreviewMesh)
	{
		return PreviewMesh->GetPathName();
	}

	return FString();
}

// ============================================================================
// UTILITY
// ============================================================================

bool UAnimGraphService::IsAnimBlueprint(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return false;
	}

	UObject* LoadedObject = UEditorAssetLibrary::LoadAsset(AssetPath);
	return Cast<UAnimBlueprint>(LoadedObject) != nullptr;
}

FString UAnimGraphService::GetParentClass(const FString& AnimBlueprintPath)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	if (AnimBlueprint->ParentClass)
	{
		return AnimBlueprint->ParentClass->GetName();
	}

	return FString();
}

// ============================================================================
// ANIMGRAPH NODE CREATION
// ============================================================================

FString UAnimGraphService::AddStateMachine(const FString& AnimBlueprintPath, const FString& MachineName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddStateMachine: Failed to load AnimBlueprint"));
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add State Machine"), AnimBlueprint);

	// Find the main AnimGraph
	UAnimationGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
		{
			AnimGraph = Cast<UAnimationGraph>(Graph);
			break;
		}
	}

	if (!AnimGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddStateMachine: AnimGraph not found"));
		return FString();
	}

	// Create state machine node - DO NOT set EditorStateMachineGraph before Finalize()
	// PostPlacedNewNode() will create it automatically and asserts that it's null
	FGraphNodeCreator<UAnimGraphNode_StateMachine> NodeCreator(*AnimGraph);
	UAnimGraphNode_StateMachine* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddStateMachine: Failed to create state machine node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;

	// Finalize will call PostPlacedNewNode() which creates EditorStateMachineGraph,
	// entry node, and sets up the schema correctly
	NodeCreator.Finalize();

	// Now rename the graph to the desired name
	if (NewNode->EditorStateMachineGraph)
	{
		TSharedPtr<INameValidatorInterface> NameValidator = FNameValidatorFactory::MakeValidator(NewNode);
		FBlueprintEditorUtils::RenameGraphWithSuggestion(NewNode->EditorStateMachineGraph, NameValidator, MachineName);
	}

	// Mark dirty and compile
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddStateMachine: Created '%s' at (%f, %f)"), *MachineName, PosX, PosY);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::AddSequencePlayer(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& AnimSequencePath, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Sequence Player"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddSequencePlayer: Graph '%s' not found"), *GraphName);
		return FString();
	}

	// Create sequence player node
	FGraphNodeCreator<UAnimGraphNode_SequencePlayer> NodeCreator(*TargetGraph);
	UAnimGraphNode_SequencePlayer* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddSequencePlayer: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;

	// Set animation sequence if provided
	if (!AnimSequencePath.IsEmpty())
	{
		UAnimSequence* Sequence = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimSequencePath));
		if (Sequence)
		{
			NewNode->Node.SetSequence(Sequence);
		}
		else
		{
			UE_LOG(LogRevvyAnimGraph, Warning, TEXT("AddSequencePlayer: Could not load sequence '%s'"), *AnimSequencePath);
		}
	}

	NodeCreator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddSequencePlayer: Created in '%s'"), *GraphName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::AddBlendSpacePlayer(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& BlendSpacePath, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add BlendSpace Player"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddBlendSpacePlayer: Graph '%s' not found"), *GraphName);
		return FString();
	}

	// Create blend space player node
	FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> NodeCreator(*TargetGraph);
	UAnimGraphNode_BlendSpacePlayer* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddBlendSpacePlayer: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;

	// Set blend space if provided
	if (!BlendSpacePath.IsEmpty())
	{
		UBlendSpace* BlendSpace = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendSpacePath));
		if (BlendSpace)
		{
			NewNode->Node.SetBlendSpace(BlendSpace);
		}
		else
		{
			UE_LOG(LogRevvyAnimGraph, Warning, TEXT("AddBlendSpacePlayer: Could not load blend space '%s'"), *BlendSpacePath);
		}
	}

	NodeCreator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddBlendSpacePlayer: Created in '%s'"), *GraphName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::AddAimOffsetPlayer(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& AimOffsetAssetPath, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Aim Offset Player"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddAimOffsetPlayer: Graph '%s' not found"), *GraphName);
		return FString();
	}

	// Validate the asset up-front so we don't leave a half-created node behind on wrong type.
	// A RotationOffset player only makes sense with an AimOffsetBlendSpace (2D) or
	// AimOffsetBlendSpace1D asset — the callers explicitly asked for the aim-offset flavour,
	// not a generic BlendSpace player.
	UBlendSpace* AimOffset = nullptr;
	if (!AimOffsetAssetPath.IsEmpty())
	{
		UObject* Asset = UEditorAssetLibrary::LoadAsset(AimOffsetAssetPath);
		if (!Asset)
		{
			UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddAimOffsetPlayer: Could not load asset '%s'"), *AimOffsetAssetPath);
			return FString();
		}
		AimOffset = Cast<UBlendSpace>(Asset);
		if (!AimOffset || !(AimOffset->IsA<UAimOffsetBlendSpace>() || AimOffset->IsA<UAimOffsetBlendSpace1D>()))
		{
			UE_LOG(LogRevvyAnimGraph, Error,
				TEXT("AddAimOffsetPlayer: Asset '%s' is not a UAimOffsetBlendSpace / UAimOffsetBlendSpace1D"),
				*AimOffsetAssetPath);
			return FString();
		}
	}

	// Create RotationOffsetBlendSpace (Aim Offset) player node
	FGraphNodeCreator<UAnimGraphNode_RotationOffsetBlendSpace> NodeCreator(*TargetGraph);
	UAnimGraphNode_RotationOffsetBlendSpace* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddAimOffsetPlayer: Failed to create node"));
		return FString();
	}

	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;

	if (AimOffset)
	{
		NewNode->Node.SetBlendSpace(AimOffset);
	}

	NodeCreator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddAimOffsetPlayer: Created in '%s' with asset '%s'"),
		*GraphName, *AimOffsetAssetPath);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::AddBlendByBool(const FString& AnimBlueprintPath, const FString& GraphName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Blend By Bool"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddBlendByBool: Graph '%s' not found"), *GraphName);
		return FString();
	}

	FGraphNodeCreator<UAnimGraphNode_BlendListByBool> NodeCreator(*TargetGraph);
	UAnimGraphNode_BlendListByBool* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddBlendByBool: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NodeCreator.Finalize();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddBlendByBool: Created in '%s'"), *GraphName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::AddBlendByInt(const FString& AnimBlueprintPath, const FString& GraphName, int32 NumPoses, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Blend By Int"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddBlendByInt: Graph '%s' not found"), *GraphName);
		return FString();
	}

	FGraphNodeCreator<UAnimGraphNode_BlendListByInt> NodeCreator(*TargetGraph);
	UAnimGraphNode_BlendListByInt* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddBlendByInt: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NodeCreator.Finalize();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddBlendByInt: Created in '%s' with %d poses"), *GraphName, NumPoses);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::AddLayeredBlend(const FString& AnimBlueprintPath, const FString& GraphName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Layered Blend"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddLayeredBlend: Graph '%s' not found"), *GraphName);
		return FString();
	}

	FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> NodeCreator(*TargetGraph);
	UAnimGraphNode_LayeredBoneBlend* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddLayeredBlend: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NodeCreator.Finalize();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddLayeredBlend: Created in '%s'"), *GraphName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

bool UAnimGraphService::SetLayeredBoneBlendPoseCount(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& NodeId, int32 NumBlendPoses)
{
	if (NumBlendPoses < 1)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetLayeredBoneBlendPoseCount: NumBlendPoses must be >= 1 (got %d)"), NumBlendPoses);
		return false;
	}

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UAnimGraphNode_LayeredBoneBlend* Node = FindLayeredBoneBlendNode(AnimBlueprint, GraphName, NodeId);
	if (!Node)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set LayeredBoneBlend Pose Count"), AnimBlueprint, Node);

	const int32 CurrentCount = Node->Node.BlendPoses.Num();
	if (NumBlendPoses > CurrentCount)
	{
		const int32 ToAdd = NumBlendPoses - CurrentCount;
		for (int32 i = 0; i < ToAdd; ++i)
		{
			Node->Node.AddPose();
		}
	}
	else if (NumBlendPoses < CurrentCount)
	{
		for (int32 i = CurrentCount - 1; i >= NumBlendPoses; --i)
		{
			Node->Node.RemovePose(i);
		}
	}

	// Sync editor pins with the new BlendPoses count.
	Node->ReconstructNode();

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetLayeredBoneBlendPoseCount: '%s' %d -> %d"),
		*NodeId, CurrentCount, NumBlendPoses);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetLayeredBoneBlendBranchFilter(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& NodeId, int32 PoseIndex, const FString& RootBoneName, int32 BlendDepth)
{
	if (PoseIndex < 0)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetLayeredBoneBlendBranchFilter: PoseIndex must be >= 0 (got %d)"), PoseIndex);
		return false;
	}

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UAnimGraphNode_LayeredBoneBlend* Node = FindLayeredBoneBlendNode(AnimBlueprint, GraphName, NodeId);
	if (!Node)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set LayeredBoneBlend Branch Filter"), AnimBlueprint, Node);

	// LayerSetup / BlendPoses are kept in lock-step by SyncBlendMasksAndLayers inside AddPose;
	// grow BlendPoses via AddPose so pins stay valid, then reconstruct if we actually grew.
	const int32 CurrentPoses = Node->Node.BlendPoses.Num();
	bool bGrew = false;
	while (Node->Node.BlendPoses.Num() <= PoseIndex)
	{
		Node->Node.AddPose();
		bGrew = true;
	}

	// In BranchFilter mode LayerSetup is sized to BlendPoses; in BlendMask mode it's Reset() empty.
	// Ensure the LayerSetup slot exists either way so this setter is usable independent of BlendMode.
	if (!Node->Node.LayerSetup.IsValidIndex(PoseIndex))
	{
		Node->Node.LayerSetup.SetNum(PoseIndex + 1);
	}

	FInputBlendPose& Layer = Node->Node.LayerSetup[PoseIndex];
	if (Layer.BranchFilters.Num() == 0)
	{
		Layer.BranchFilters.AddDefaulted();
	}
	Layer.BranchFilters[0].BoneName = FName(*RootBoneName);
	Layer.BranchFilters[0].BlendDepth = BlendDepth;

	if (bGrew)
	{
		Node->ReconstructNode();
	}

	UE_LOG(LogRevvyAnimGraph, Log,
		TEXT("SetLayeredBoneBlendBranchFilter: '%s' pose=%d root='%s' depth=%d (poses %d->%d)"),
		*NodeId, PoseIndex, *RootBoneName, BlendDepth, CurrentPoses, Node->Node.BlendPoses.Num());
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetLayeredBoneBlendMode(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& NodeId, const FString& ModeStr)
{
	ELayeredBoneBlendMode NewMode;
	if (ModeStr.Equals(TEXT("BranchFilter"), ESearchCase::IgnoreCase))
	{
		NewMode = ELayeredBoneBlendMode::BranchFilter;
	}
	else if (ModeStr.Equals(TEXT("BlendMask"), ESearchCase::IgnoreCase))
	{
		NewMode = ELayeredBoneBlendMode::BlendMask;
	}
	else
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetLayeredBoneBlendMode: Unknown mode '%s' (expected 'BranchFilter' or 'BlendMask')"), *ModeStr);
		return false;
	}

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UAnimGraphNode_LayeredBoneBlend* Node = FindLayeredBoneBlendNode(AnimBlueprint, GraphName, NodeId);
	if (!Node)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set LayeredBoneBlend Mode"), AnimBlueprint, Node);

	Node->Node.BlendMode = NewMode;

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetLayeredBoneBlendMode: '%s' mode='%s'"), *NodeId, *ModeStr);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetLayeredBoneBlendMask(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& NodeId, int32 PoseIndex, const FString& BlendProfileAssetPath)
{
	if (PoseIndex < 0)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetLayeredBoneBlendMask: PoseIndex must be >= 0 (got %d)"), PoseIndex);
		return false;
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(BlendProfileAssetPath);
	if (!LoadedAsset)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetLayeredBoneBlendMask: Could not load asset '%s'"), *BlendProfileAssetPath);
		return false;
	}
	UBlendProfile* BlendProfile = Cast<UBlendProfile>(LoadedAsset);
	if (!BlendProfile)
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetLayeredBoneBlendMask: Asset '%s' is not a UBlendProfile (actual '%s')"),
			*BlendProfileAssetPath, *LoadedAsset->GetClass()->GetName());
		return false;
	}

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UAnimGraphNode_LayeredBoneBlend* Node = FindLayeredBoneBlendNode(AnimBlueprint, GraphName, NodeId);
	if (!Node)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set LayeredBoneBlend Mask"), AnimBlueprint, Node);

	// Grow BlendPoses so the pose slot logically exists, matching the pattern of
	// SetLayeredBoneBlendBranchFilter — SyncBlendMasksAndLayers keeps BlendMasks in
	// step in BlendMask mode, but we resize BlendMasks directly to stay independent
	// of current BlendMode (mask is only *used* when mode == BlendMask; storing
	// it early is safe and matches editor behaviour).
	bool bGrew = false;
	while (Node->Node.BlendPoses.Num() <= PoseIndex)
	{
		Node->Node.AddPose();
		bGrew = true;
	}

	if (!Node->Node.BlendMasks.IsValidIndex(PoseIndex))
	{
		Node->Node.BlendMasks.SetNum(PoseIndex + 1);
	}
	Node->Node.BlendMasks[PoseIndex] = BlendProfile;

	if (bGrew)
	{
		Node->ReconstructNode();
	}

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetLayeredBoneBlendMask: '%s' pose=%d profile='%s'"),
		*NodeId, PoseIndex, *BlendProfileAssetPath);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetLayeredBoneBlendCurveOption(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& NodeId, const FString& CurveOptionStr)
{
	// NOTE: ECurveBlendOption in UE 5.x does not have a "MaxWeight" enumerator anymore —
	// the engine comment on Override reads: "redirect from MaxWeight old legacy behavior".
	// We accept "MaxWeight" as an alias for Override to match the task's spelling.
	ECurveBlendOption::Type NewOption;
	if (CurveOptionStr.Equals(TEXT("MaxWeight"), ESearchCase::IgnoreCase) ||
		CurveOptionStr.Equals(TEXT("Override"), ESearchCase::IgnoreCase))
	{
		NewOption = ECurveBlendOption::Override;
	}
	else if (CurveOptionStr.Equals(TEXT("NormalizeByWeight"), ESearchCase::IgnoreCase))
	{
		NewOption = ECurveBlendOption::NormalizeByWeight;
	}
	else if (CurveOptionStr.Equals(TEXT("BlendByWeight"), ESearchCase::IgnoreCase))
	{
		NewOption = ECurveBlendOption::BlendByWeight;
	}
	else
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetLayeredBoneBlendCurveOption: Unknown option '%s' (expected 'MaxWeight' | 'Override' | 'NormalizeByWeight' | 'BlendByWeight')"),
			*CurveOptionStr);
		return false;
	}

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UAnimGraphNode_LayeredBoneBlend* Node = FindLayeredBoneBlendNode(AnimBlueprint, GraphName, NodeId);
	if (!Node)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set LayeredBoneBlend Curve Option"), AnimBlueprint, Node);

	Node->Node.CurveBlendOption = NewOption;

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetLayeredBoneBlendCurveOption: '%s' option='%s'"), *NodeId, *CurveOptionStr);
	Scope.MarkSuccess();
	return true;
}

FString UAnimGraphService::AddSlotNode(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& SlotName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Slot Node"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddSlotNode: Graph '%s' not found"), *GraphName);
		return FString();
	}

	FGraphNodeCreator<UAnimGraphNode_Slot> NodeCreator(*TargetGraph);
	UAnimGraphNode_Slot* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddSlotNode: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NewNode->Node.SlotName = FName(*SlotName);
	NodeCreator.Finalize();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddSlotNode: Created '%s' in '%s'"), *SlotName, *GraphName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::AddSaveCachedPose(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& CacheName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Save Cached Pose"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddSaveCachedPose: Graph '%s' not found"), *GraphName);
		return FString();
	}

	FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> NodeCreator(*TargetGraph);
	UAnimGraphNode_SaveCachedPose* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddSaveCachedPose: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NewNode->CacheName = CacheName;
	NodeCreator.Finalize();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddSaveCachedPose: Created '%s' in '%s'"), *CacheName, *GraphName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::AddUseCachedPose(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& CacheName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Use Cached Pose"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddUseCachedPose: Graph '%s' not found"), *GraphName);
		return FString();
	}

	// First, find the corresponding SaveCachedPose node
	UAnimGraphNode_SaveCachedPose* SaveNode = nullptr;
	TArray<UEdGraph*> AllGraphs;
	AnimBlueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UAnimGraphNode_SaveCachedPose* SaveCached = Cast<UAnimGraphNode_SaveCachedPose>(Node);
			if (SaveCached && SaveCached->CacheName == CacheName)
			{
				SaveNode = SaveCached;
				break;
			}
		}

		if (SaveNode) break;
	}

	if (!SaveNode)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("AddUseCachedPose: SaveCachedPose with name '%s' not found. Creating node anyway."), *CacheName);
	}

	FGraphNodeCreator<UAnimGraphNode_UseCachedPose> NodeCreator(*TargetGraph);
	UAnimGraphNode_UseCachedPose* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddUseCachedPose: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NewNode->SaveCachedPoseNode = SaveNode;
	NodeCreator.Finalize();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddUseCachedPose: Created reference to '%s' in '%s'"), *CacheName, *GraphName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

// ============================================================================
// STATE MACHINE MUTATIONS
// ============================================================================

FString UAnimGraphService::AddState(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& StateName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddState: Failed to load AnimBlueprint"));
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add State"), AnimBlueprint);

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddState: State machine node '%s' not found"), *StateMachineName);
		return FString();
	}
	
	if (!StateMachineNode->EditorStateMachineGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddState: State machine '%s' has no graph (may not be fully initialized)"), *StateMachineName);
		return FString();
	}

	UAnimationStateMachineGraph* SMGraph = StateMachineNode->EditorStateMachineGraph;

	// Create state node - DO NOT set BoundGraph before Finalize()
	// PostPlacedNewNode() will create it automatically and asserts that it's null
	FGraphNodeCreator<UAnimStateNode> NodeCreator(*SMGraph);
	UAnimStateNode* NewState = NodeCreator.CreateNode();
	if (!NewState)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddState: Failed to create state node for '%s'"), *StateName);
		return FString();
	}
	
	NewState->NodePosX = PosX;
	NewState->NodePosY = PosY;

	// Finalize will call PostPlacedNewNode() which creates BoundGraph and sets up schema
	NodeCreator.Finalize();

	// Now rename the graph to the desired name
	if (NewState->BoundGraph)
	{
		TSharedPtr<INameValidatorInterface> NameValidator = FNameValidatorFactory::MakeValidator(NewState);
		FBlueprintEditorUtils::RenameGraphWithSuggestion(NewState->BoundGraph, NameValidator, StateName);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddState: Created '%s' in '%s'"), *StateName, *StateMachineName);
	Scope.MarkSuccess();
	return NewState->NodeGuid.ToString();
}

FString UAnimGraphService::AddConduit(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& ConduitName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddConduit: Failed to load AnimBlueprint"));
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Conduit"), AnimBlueprint);

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddConduit: State machine node '%s' not found"), *StateMachineName);
		return FString();
	}
	
	if (!StateMachineNode->EditorStateMachineGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddConduit: State machine '%s' has no graph"), *StateMachineName);
		return FString();
	}

	UAnimationStateMachineGraph* SMGraph = StateMachineNode->EditorStateMachineGraph;

	// Create conduit node - DO NOT set BoundGraph before Finalize()
	// PostPlacedNewNode() will create it automatically and asserts that it's null
	FGraphNodeCreator<UAnimStateConduitNode> NodeCreator(*SMGraph);
	UAnimStateConduitNode* NewConduit = NodeCreator.CreateNode();
	if (!NewConduit)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddConduit: Failed to create conduit node for '%s'"), *ConduitName);
		return FString();
	}
	
	NewConduit->NodePosX = PosX;
	NewConduit->NodePosY = PosY;

	// Finalize will call PostPlacedNewNode() which creates BoundGraph
	NodeCreator.Finalize();

	// Now rename the graph to the desired name
	if (NewConduit->BoundGraph)
	{
		TSharedPtr<INameValidatorInterface> NameValidator = FNameValidatorFactory::MakeValidator(NewConduit);
		FBlueprintEditorUtils::RenameGraphWithSuggestion(NewConduit->BoundGraph, NameValidator, ConduitName);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddConduit: Created '%s' in '%s'"), *ConduitName, *StateMachineName);
	Scope.MarkSuccess();
	return NewConduit->NodeGuid.ToString();
}

FString UAnimGraphService::AddTransition(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, float BlendDuration)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddTransition: Failed to load AnimBlueprint"));
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Transition"), AnimBlueprint);

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddTransition: State machine node '%s' not found"), *StateMachineName);
		return FString();
	}
	
	if (!StateMachineNode->EditorStateMachineGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddTransition: State machine '%s' has no graph"), *StateMachineName);
		return FString();
	}

	UAnimationStateMachineGraph* SMGraph = StateMachineNode->EditorStateMachineGraph;

	// Find source and destination states
	UAnimStateNodeBase* SourceState = FindStateNode(SMGraph, SourceStateName);
	UAnimStateNodeBase* DestState = FindStateNode(SMGraph, DestStateName);

	if (!SourceState)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddTransition: Source state '%s' not found"), *SourceStateName);
		return FString();
	}

	if (!DestState)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddTransition: Dest state '%s' not found"), *DestStateName);
		return FString();
	}

	// Create transition node - DO NOT set BoundGraph before Finalize()
	// PostPlacedNewNode() will create it via CreateBoundGraph()
	FGraphNodeCreator<UAnimStateTransitionNode> NodeCreator(*SMGraph);
	UAnimStateTransitionNode* Transition = NodeCreator.CreateNode();
	if (!Transition)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddTransition: Failed to create transition node"));
		return FString();
	}

	// Position between states
	Transition->NodePosX = (SourceState->NodePosX + DestState->NodePosX) / 2;
	Transition->NodePosY = (SourceState->NodePosY + DestState->NodePosY) / 2;
	Transition->CrossfadeDuration = BlendDuration;

	// Finalize will call PostPlacedNewNode() which creates BoundGraph
	NodeCreator.Finalize();

	// Connect source -> transition -> dest via pins
	UEdGraphPin* SourceOutPin = SourceState->GetOutputPin();
	UEdGraphPin* TransInPin = Transition->GetInputPin();
	UEdGraphPin* TransOutPin = Transition->GetOutputPin();
	UEdGraphPin* DestInPin = DestState->GetInputPin();

	if (SourceOutPin && TransInPin && TransOutPin && DestInPin)
	{
		SourceOutPin->MakeLinkTo(TransInPin);
		TransOutPin->MakeLinkTo(DestInPin);
	}
	else
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("AddTransition: Failed to connect pins (source_out=%d, trans_in=%d, trans_out=%d, dest_in=%d)"),
			SourceOutPin != nullptr, TransInPin != nullptr, TransOutPin != nullptr, DestInPin != nullptr);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddTransition: Created '%s' -> '%s' with blend %.2fs"),
		*SourceStateName, *DestStateName, BlendDuration);
	Scope.MarkSuccess();
	return Transition->NodeGuid.ToString();
}

bool UAnimGraphService::RemoveState(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& StateName, bool bRemoveTransitions)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Remove State"), AnimBlueprint);

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
	{
		return false;
	}

	UEdGraph* SMGraph = StateMachineNode->EditorStateMachineGraph;
	UAnimStateNodeBase* StateNode = FindStateNode(SMGraph, StateName);

	if (!StateNode)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("RemoveState: State '%s' not found"), *StateName);
		return false;
	}

	// Remove transitions if requested
	if (bRemoveTransitions)
	{
		TArray<UAnimStateTransitionNode*> TransitionsToRemove;
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node);
			if (TransNode)
			{
				if (TransNode->GetPreviousState() == StateNode || TransNode->GetNextState() == StateNode)
				{
					TransitionsToRemove.Add(TransNode);
				}
			}
		}

		for (UAnimStateTransitionNode* TransNode : TransitionsToRemove)
		{
			SMGraph->RemoveNode(TransNode);
		}
	}

	// Remove the state node
	SMGraph->RemoveNode(StateNode);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("RemoveState: Removed '%s' from '%s'"), *StateName, *StateMachineName);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::RemoveTransition(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Remove Transition"), AnimBlueprint);

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
	{
		return false;
	}

	UEdGraph* SMGraph = StateMachineNode->EditorStateMachineGraph;
	UAnimStateTransitionNode* TransitionNode = FindTransitionNode(SMGraph, SourceStateName, DestStateName);

	if (!TransitionNode)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("RemoveTransition: Transition '%s' -> '%s' not found"), 
			*SourceStateName, *DestStateName);
		return false;
	}

	SMGraph->RemoveNode(TransitionNode);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("RemoveTransition: Removed '%s' -> '%s'"), *SourceStateName, *DestStateName);
	Scope.MarkSuccess();
	return true;
}

// ============================================================================
// ANIMGRAPH CONNECTIONS
// ============================================================================

bool UAnimGraphService::ConnectAnimNodes(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Connect Anim Nodes"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectAnimNodes: Graph '%s' not found"), *GraphName);
		return false;
	}

	// Parse GUIDs
	FGuid SourceGuid, TargetGuid;
	if (!FGuid::Parse(SourceNodeId, SourceGuid))
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectAnimNodes: Invalid source GUID"));
		return false;
	}

	if (!FGuid::Parse(TargetNodeId, TargetGuid))
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectAnimNodes: Invalid target GUID"));
		return false;
	}

	// Find nodes
	UEdGraphNode* SourceNode = nullptr;
	UEdGraphNode* TargetNode = nullptr;

	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node)
		{
			if (Node->NodeGuid == SourceGuid) SourceNode = Node;
			if (Node->NodeGuid == TargetGuid) TargetNode = Node;
		}
	}

	if (!SourceNode || !TargetNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectAnimNodes: Nodes not found"));
		return false;
	}

	// Find pins
	UEdGraphPin* SourcePin = nullptr;
	UEdGraphPin* TargetPin = nullptr;

	for (UEdGraphPin* Pin : SourceNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == SourcePinName && Pin->Direction == EGPD_Output)
		{
			SourcePin = Pin;
			break;
		}
	}

	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == TargetPinName && Pin->Direction == EGPD_Input)
		{
			TargetPin = Pin;
			break;
		}
	}

	if (!SourcePin || !TargetPin)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectAnimNodes: Pins not found"));
		return false;
	}

	// Make connection
	SourcePin->MakeLinkTo(TargetPin);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("ConnectAnimNodes: Connected nodes in '%s'"), *GraphName);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::ConnectAnimNodesViaSchema(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& SourceNodeId, const FString& SourcePinName, const FString& TargetNodeId, const FString& TargetPinName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Connect Anim Nodes (Schema)"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectAnimNodesViaSchema: Graph '%s' not found"), *GraphName);
		return false;
	}

	FGuid SourceGuid, TargetGuid;
	if (!FGuid::Parse(SourceNodeId, SourceGuid) || !FGuid::Parse(TargetNodeId, TargetGuid))
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectAnimNodesViaSchema: Invalid GUID"));
		return false;
	}

	UEdGraphNode* SourceNode = nullptr;
	UEdGraphNode* TargetNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node)
		{
			if (Node->NodeGuid == SourceGuid) SourceNode = Node;
			if (Node->NodeGuid == TargetGuid) TargetNode = Node;
		}
	}
	if (!SourceNode || !TargetNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectAnimNodesViaSchema: Nodes not found"));
		return false;
	}

	UEdGraphPin* SourcePin = nullptr;
	UEdGraphPin* TargetPin = nullptr;
	for (UEdGraphPin* Pin : SourceNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == SourcePinName && Pin->Direction == EGPD_Output)
		{
			SourcePin = Pin;
			break;
		}
	}
	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->PinName.ToString() == TargetPinName && Pin->Direction == EGPD_Input)
		{
			TargetPin = Pin;
			break;
		}
	}
	if (!SourcePin || !TargetPin)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectAnimNodesViaSchema: Pins not found ('%s'/'%s')"), *SourcePinName, *TargetPinName);
		return false;
	}

	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	if (!Schema)
	{
		return false;
	}

	// TryCreateConnection auto-inserts Local<->Component converter nodes on pose-space mismatch (editor-faithful).
	const bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);
	if (bConnected)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
		UE_LOG(LogRevvyAnimGraph, Log, TEXT("ConnectAnimNodesViaSchema: Connected '%s'.%s -> %s in '%s'"),
			*SourceNodeId, *SourcePinName, *TargetPinName, *GraphName);
		Scope.MarkSuccess();
	}
	else
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("ConnectAnimNodesViaSchema: Schema rejected connection"));
	}
	return bConnected;
}

bool UAnimGraphService::ConnectToOutputPose(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& SourceNodeId, const FString& SourcePinName)
{
	FString OutputNodeId = GetOutputPoseNodeId(AnimBlueprintPath, GraphName);
	if (OutputNodeId.IsEmpty())
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ConnectToOutputPose: Could not find output pose node"));
		return false;
	}

	return ConnectAnimNodes(AnimBlueprintPath, GraphName, SourceNodeId, SourcePinName, OutputNodeId, TEXT("Result"));
}

bool UAnimGraphService::DisconnectAnimNode(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& NodeId, const FString& PinName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Disconnect Anim Node"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		return false;
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeId, NodeGuid))
	{
		return false;
	}

	// Find node
	UEdGraphNode* Node = nullptr;
	for (UEdGraphNode* GraphNode : TargetGraph->Nodes)
	{
		if (GraphNode && GraphNode->NodeGuid == NodeGuid)
		{
			Node = GraphNode;
			break;
		}
	}

	if (!Node)
	{
		return false;
	}

	// Find and disconnect pin
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinName)
		{
			Pin->BreakAllPinLinks();
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
			Scope.MarkSuccess();
			return true;
		}
	}

	return false;
}

FString UAnimGraphService::GetOutputPoseNodeId(const FString& AnimBlueprintPath, const FString& GraphName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		return FString();
	}

	// Look for output pose nodes (Root node in AnimGraph, StateResult in state graphs)
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Cast<UAnimGraphNode_Root>(Node) || Cast<UAnimGraphNode_StateResult>(Node))
		{
			return Node->NodeGuid.ToString();
		}
	}

	return FString();
}

// ============================================================================
// ANIMATION ASSET ASSIGNMENT
// ============================================================================

bool UAnimGraphService::SetSequencePlayerAsset(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& NodeId, const FString& AnimSequencePath)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Sequence Player Asset"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		return false;
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeId, NodeGuid))
	{
		return false;
	}

	// Find node
	UAnimGraphNode_SequencePlayer* SeqPlayer = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node && Node->NodeGuid == NodeGuid)
		{
			SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(Node);
			break;
		}
	}

	if (!SeqPlayer)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetSequencePlayerAsset: Node is not a sequence player"));
		return false;
	}

	// Load sequence
	UAnimSequence* Sequence = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimSequencePath));
	if (!Sequence)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetSequencePlayerAsset: Could not load sequence '%s'"), *AnimSequencePath);
		return false;
	}

	SeqPlayer->Node.SetSequence(Sequence);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetSequencePlayerAsset: Set sequence to '%s'"), *AnimSequencePath);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetBlendSpaceAsset(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& NodeId, const FString& BlendSpacePath)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set BlendSpace Asset"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		return false;
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeId, NodeGuid))
	{
		return false;
	}

	// Find node
	UAnimGraphNode_BlendSpacePlayer* BSPlayer = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node && Node->NodeGuid == NodeGuid)
		{
			BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(Node);
			break;
		}
	}

	if (!BSPlayer)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetBlendSpaceAsset: Node is not a blend space player"));
		return false;
	}

	// Load blend space
	UBlendSpace* BlendSpace = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendSpacePath));
	if (!BlendSpace)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetBlendSpaceAsset: Could not load blend space '%s'"), *BlendSpacePath);
		return false;
	}

	BSPlayer->Node.SetBlendSpace(BlendSpace);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetBlendSpaceAsset: Set blend space to '%s'"), *BlendSpacePath);
	Scope.MarkSuccess();
	return true;
}

FString UAnimGraphService::GetNodeAnimationAsset(const FString& AnimBlueprintPath, const FString& GraphName, 
	const FString& NodeId)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		return FString();
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeId, NodeGuid))
	{
		return FString();
	}

	// Find node
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node && Node->NodeGuid == NodeGuid)
		{
			// Try sequence player
			if (UAnimGraphNode_SequencePlayer* SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				UAnimSequenceBase* Sequence = SeqPlayer->Node.GetSequence();
				if (Sequence)
				{
					return Sequence->GetPathName();
				}
			}

			// Try blend space player
			if (UAnimGraphNode_BlendSpacePlayer* BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
			{
				UBlendSpace* BlendSpace = BSPlayer->Node.GetBlendSpace();
				if (BlendSpace)
				{
					return BlendSpace->GetPathName();
				}
			}

			break;
		}
	}

	return FString();
}

// ============================================================================
// ADVANCED ANIMATION NODES
// ============================================================================

FString UAnimGraphService::AddTwoBoneIKNode(const FString& AnimBlueprintPath, const FString& GraphName,
	float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Two Bone IK Node"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddTwoBoneIKNode: Graph '%s' not found"), *GraphName);
		return FString();
	}

	FGraphNodeCreator<UAnimGraphNode_TwoBoneIK> NodeCreator(*TargetGraph);
	UAnimGraphNode_TwoBoneIK* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddTwoBoneIKNode: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NodeCreator.Finalize();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddTwoBoneIKNode: Created in '%s'"), *GraphName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::AddModifyBoneNode(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& BoneName, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Modify Bone Node"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddModifyBoneNode: Graph '%s' not found"), *GraphName);
		return FString();
	}

	FGraphNodeCreator<UAnimGraphNode_ModifyBone> NodeCreator(*TargetGraph);
	UAnimGraphNode_ModifyBone* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddModifyBoneNode: Failed to create node"));
		return FString();
	}
	
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;

	if (!BoneName.IsEmpty())
	{
		NewNode->Node.BoneToModify.BoneName = FName(*BoneName);
	}

	NodeCreator.Finalize();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddModifyBoneNode: Created in '%s' for bone '%s'"), *GraphName, *BoneName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

FString UAnimGraphService::BindAnimNodeProperty(const FString& AnimBlueprintPath, const FString& GraphName,
	const FString& NodeGuid, const FString& PinName, const FString& MemberVariableName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return TEXT("ERR: anim blueprint not found");
	}

	FAnimBPMutationScope Scope(TEXT("Bind Anim Node Property"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		return FString::Printf(TEXT("ERR: graph '%s' not found"), *GraphName);
	}

	FGuid Guid;
	if (!FGuid::Parse(NodeGuid, Guid))
	{
		return TEXT("ERR: invalid node GUID");
	}

	UAnimGraphNode_Base* Target = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node))
		{
			if (AnimNode->NodeGuid == Guid)
			{
				Target = AnimNode;
				break;
			}
		}
	}
	if (!Target)
	{
		return TEXT("ERR: node not found");
	}

	const FName PinFName(*PinName);

	// Resolve the optional-pin index + the underlying FProperty for the pin (handles hidden pins too).
	FName BindingName = NAME_None;
	FProperty* PinProperty = nullptr;
	int32 OptionalPinIndex = INDEX_NONE;
	// GetPinBindingInfo / EnsureBindingsArePresent are protected on UAnimGraphNode_Base;
	// expose them via a using-accessor (no instances created — pure access cast).
	struct FAnimGraphNodeBindingAccessor : public UAnimGraphNode_Base
	{
		using UAnimGraphNode_Base::GetPinBindingInfo;
		using UAnimGraphNode_Base::EnsureBindingsArePresent;
	};
	static_cast<FAnimGraphNodeBindingAccessor*>(Target)->GetPinBindingInfo(PinFName, BindingName, PinProperty, OptionalPinIndex);
	if (BindingName == NAME_None)
	{
		BindingName = PinFName;
	}
	// Fallback: look the property up directly on the anim node struct.
	if (!PinProperty)
	{
		if (UScriptStruct* NodeStruct = Target->GetFNodeType())
		{
			PinProperty = FindFProperty<FProperty>(NodeStruct, PinFName);
		}
	}

	Target->Modify();

	// Expose the optional pin so the binding has a pin to represent it.
	if (OptionalPinIndex != INDEX_NONE)
	{
		Target->SetPinVisibility(true, OptionalPinIndex);
	}

	// A bound pin cannot also have a literal link.
	if (UEdGraphPin* Pin = Target->FindPin(PinFName))
	{
		Pin->BreakAllPinLinks();
	}

	// Ensure the binding object exists, then read it back as a UObject* via reflection so we don't need
	// to include the concrete (private) UAnimGraphNodeBinding_Base type.
	static_cast<FAnimGraphNodeBindingAccessor*>(Target)->EnsureBindingsArePresent();
	UObject* Binding = nullptr;
	if (FObjectPropertyBase* BindingProp = CastField<FObjectPropertyBase>(
			UAnimGraphNode_Base::StaticClass()->FindPropertyByName(TEXT("Binding"))))
	{
		Binding = BindingProp->GetObjectPropertyValue_InContainer(Target);
	}
	if (!Binding)
	{
		return TEXT("ERR: node has no binding object");
	}
	Binding->Modify();

	// Build the property binding (member-variable property access, the editor "fast path").
	FAnimGraphNodePropertyBinding NewBinding;
	NewBinding.PropertyName = BindingName;
	NewBinding.PropertyPath.Reset();
	NewBinding.PropertyPath.Add(MemberVariableName);
	NewBinding.PathAsText = FText::FromString(MemberVariableName);
	NewBinding.Type = EAnimGraphNodePropertyBindingType::Property;
	NewBinding.bIsBound = true;
	if (PinProperty)
	{
		FEdGraphPinType PinType;
		if (GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(PinProperty, PinType))
		{
			NewBinding.PinType = PinType;
			NewBinding.PromotedPinType = PinType;
		}
	}

	// The concrete binding class (UAnimGraphNodeBinding_Base) lives in a private header, so reach its
	// PropertyBindings TMap<FName, FAnimGraphNodePropertyBinding> through reflection instead of including it.
	FMapProperty* MapProp = CastField<FMapProperty>(Binding->GetClass()->FindPropertyByName(TEXT("PropertyBindings")));
	if (!MapProp)
	{
		return TEXT("ERR: PropertyBindings map not found on binding object");
	}
	void* MapContainer = MapProp->ContainerPtrToValuePtr<void>(Binding);
	FScriptMapHelper MapHelper(MapProp, MapContainer);
	// Overwrite any existing binding for this pin, then add ours.
	MapHelper.RemovePair(&BindingName);
	MapHelper.AddPair(&BindingName, &NewBinding);

	Target->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBlueprint);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("BindAnimNodeProperty: '%s'.%s -> %s"),
		*GraphName, *BindingName.ToString(), *MemberVariableName);
	Scope.MarkSuccess();
	return BindingName.ToString();
}

FString UAnimGraphService::AddConvertSpaceNode(const FString& AnimBlueprintPath, const FString& GraphName,
	bool bToComponentSpace, float PosX, float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Convert Space Node"), AnimBlueprint);

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddConvertSpaceNode: Graph '%s' not found"), *GraphName);
		return FString();
	}

	UAnimGraphNode_Base* NewNode = nullptr;
	if (bToComponentSpace)
	{
		FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> NodeCreator(*TargetGraph);
		UAnimGraphNode_LocalToComponentSpace* N = NodeCreator.CreateNode();
		if (N) { N->NodePosX = PosX; N->NodePosY = PosY; }
		NodeCreator.Finalize();
		NewNode = N;
	}
	else
	{
		FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> NodeCreator(*TargetGraph);
		UAnimGraphNode_ComponentToLocalSpace* N = NodeCreator.CreateNode();
		if (N) { N->NodePosX = PosX; N->NodePosY = PosY; }
		NodeCreator.Finalize();
		NewNode = N;
	}

	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddConvertSpaceNode: Failed to create node"));
		return FString();
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddConvertSpaceNode: Created (%s) in '%s'"),
		bToComponentSpace ? TEXT("LocalToComponent") : TEXT("ComponentToLocal"), *GraphName);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

// ============================================================================
// FILE-LOCAL HELPERS (transition rule authoring)
// ============================================================================

namespace
{
	/** Get the single value output pin of a pure variable-get node. */
	UEdGraphPin* GetVariableValuePin(UK2Node_VariableGet* VarNode)
	{
		if (!VarNode)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : VarNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinName != UEdGraphSchema_K2::PN_Self)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** Map a comparison keyword to a KismetMathLibrary function name. Empty string = unknown op. */
	FString ComparisonToFunctionName(const FString& Op)
	{
		const FString S = Op.ToLower();
		if (S == TEXT("greater") || S == TEXT(">")) return TEXT("Greater_DoubleDouble");
		if (S == TEXT("less") || S == TEXT("<")) return TEXT("Less_DoubleDouble");
		if (S == TEXT("greater_equal") || S == TEXT(">=") || S == TEXT("gequal")) return TEXT("GreaterEqual_DoubleDouble");
		if (S == TEXT("less_equal") || S == TEXT("<=") || S == TEXT("lequal")) return TEXT("LessEqual_DoubleDouble");
		if (S == TEXT("equal") || S == TEXT("==")) return TEXT("EqualEqual_DoubleDouble");
		if (S == TEXT("not_equal") || S == TEXT("!=")) return TEXT("NotEqual_DoubleDouble");
		return FString();
	}

	/** Map a KismetMathLibrary comparison function name back to an operator symbol (for introspection). */
	FString FunctionNameToSymbol(const FString& FnName)
	{
		if (FnName == TEXT("Greater_DoubleDouble")) return TEXT(">");
		if (FnName == TEXT("Less_DoubleDouble")) return TEXT("<");
		if (FnName == TEXT("GreaterEqual_DoubleDouble")) return TEXT(">=");
		if (FnName == TEXT("LessEqual_DoubleDouble")) return TEXT("<=");
		if (FnName == TEXT("EqualEqual_DoubleDouble")) return TEXT("==");
		if (FnName == TEXT("NotEqual_DoubleDouble")) return TEXT("!=");
		return FnName;
	}

	/** Parse an alpha-blend keyword into EAlphaBlendOption (defaults to Linear). */
	EAlphaBlendOption ParseBlendOption(const FString& In)
	{
		const FString S = In.ToLower();
		if (S == TEXT("cubic")) return EAlphaBlendOption::Cubic;
		if (S == TEXT("hermitecubic") || S == TEXT("hermite")) return EAlphaBlendOption::HermiteCubic;
		if (S == TEXT("sinusoidal") || S == TEXT("sin")) return EAlphaBlendOption::Sinusoidal;
		if (S == TEXT("quadraticinout") || S == TEXT("quadratic")) return EAlphaBlendOption::QuadraticInOut;
		if (S == TEXT("cubicinout")) return EAlphaBlendOption::CubicInOut;
		if (S == TEXT("expinout") || S == TEXT("exp")) return EAlphaBlendOption::ExpInOut;
		if (S == TEXT("circularin")) return EAlphaBlendOption::CircularIn;
		if (S == TEXT("circularout")) return EAlphaBlendOption::CircularOut;
		if (S == TEXT("circularinout")) return EAlphaBlendOption::CircularInOut;
		return EAlphaBlendOption::Linear;
	}
}

// ============================================================================
// PRIVATE HELPERS (state machine authoring)
// ============================================================================

UAnimStateTransitionNode* UAnimGraphService::ResolveTransition(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, UAnimBlueprint*& OutBlueprint)
{
	OutBlueprint = nullptr;
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return nullptr;
	}
	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
	{
		return nullptr;
	}
	UAnimStateTransitionNode* Transition = FindTransitionNode(StateMachineNode->EditorStateMachineGraph, SourceStateName, DestStateName);
	if (!Transition)
	{
		return nullptr;
	}
	OutBlueprint = AnimBlueprint;
	return Transition;
}

UAnimGraphNode_TransitionResult* UAnimGraphService::GetTransitionResultNode(UAnimStateTransitionNode* Transition)
{
	if (!Transition)
	{
		return nullptr;
	}
	UAnimationTransitionGraph* TransitionGraph = Cast<UAnimationTransitionGraph>(Transition->BoundGraph);
	if (!TransitionGraph)
	{
		return nullptr;
	}
	return TransitionGraph->GetResultNode();
}

void UAnimGraphService::ResetTransitionRule(UAnimStateTransitionNode* Transition)
{
	if (!Transition)
	{
		return;
	}

	Transition->bAutomaticRuleBasedOnSequencePlayerInState = false;

	UAnimationTransitionGraph* TransitionGraph = Cast<UAnimationTransitionGraph>(Transition->BoundGraph);
	if (!TransitionGraph)
	{
		return;
	}

	UAnimGraphNode_TransitionResult* ResultNode = TransitionGraph->GetResultNode();
	if (ResultNode)
	{
		if (UEdGraphPin* CanEnterPin = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input))
		{
			CanEnterPin->BreakAllPinLinks();
			CanEnterPin->DefaultValue = TEXT("false");
		}
	}

	// Remove every rule-logic node, leaving only the result node intact.
	TArray<UEdGraphNode*> NodesToRemove;
	for (UEdGraphNode* Node : TransitionGraph->Nodes)
	{
		if (Node && Node != ResultNode)
		{
			NodesToRemove.Add(Node);
		}
	}
	for (UEdGraphNode* Node : NodesToRemove)
	{
		TransitionGraph->RemoveNode(Node);
	}
}

UAnimGraphNode_Base* UAnimGraphService::FindStateAssetPlayer(UAnimStateNodeBase* StateNode)
{
	if (!StateNode)
	{
		return nullptr;
	}
	UEdGraph* StateGraph = StateNode->GetBoundGraph();
	if (!StateGraph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : StateGraph->Nodes)
	{
		if (UAnimGraphNode_SequencePlayer* SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(Node))
		{
			return SeqPlayer;
		}
	}
	for (UEdGraphNode* Node : StateGraph->Nodes)
	{
		if (UAnimGraphNode_BlendSpacePlayer* BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
		{
			return BSPlayer;
		}
	}
	return nullptr;
}

void UAnimGraphService::DescribeTransitionRule(UAnimStateTransitionNode* Transition, FAnimTransitionInfo& OutInfo)
{
	OutInfo.RuleType = TEXT("None");
	OutInfo.RuleVariable = TEXT("");
	OutInfo.RuleSummary = TEXT("inert (never fires)");
	OutInfo.bHasRule = false;

	if (!Transition)
	{
		return;
	}

	if (Transition->bAutomaticRuleBasedOnSequencePlayerInState)
	{
		OutInfo.RuleType = TEXT("Automatic");
		OutInfo.RuleSummary = TEXT("auto (asset player time remaining)");
		OutInfo.bHasRule = true;
		return;
	}

	UAnimGraphNode_TransitionResult* ResultNode = GetTransitionResultNode(Transition);
	if (!ResultNode)
	{
		return;
	}
	UEdGraphPin* CanEnterPin = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input);
	if (!CanEnterPin)
	{
		return;
	}

	if (CanEnterPin->LinkedTo.Num() == 0)
	{
		if (CanEnterPin->DefaultValue.ToLower() == TEXT("true"))
		{
			OutInfo.RuleType = TEXT("Custom");
			OutInfo.RuleSummary = TEXT("always true");
			OutInfo.bHasRule = true;
		}
		return;
	}

	UEdGraphPin* SourcePin = CanEnterPin->LinkedTo[0];
	UEdGraphNode* SourceNode = SourcePin ? SourcePin->GetOwningNodeUnchecked() : nullptr;

	if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(SourceNode))
	{
		OutInfo.RuleType = TEXT("Bool");
		OutInfo.RuleVariable = VarGet->GetVarName().ToString();
		OutInfo.RuleSummary = FString::Printf(TEXT("%s == true"), *OutInfo.RuleVariable);
		OutInfo.bHasRule = true;
		return;
	}

	if (UK2Node_CallFunction* CallFn = Cast<UK2Node_CallFunction>(SourceNode))
	{
		const FString FnName = CallFn->FunctionReference.GetMemberName().ToString();

		if (FnName == TEXT("Not_PreBool"))
		{
			FString VarName;
			if (UEdGraphPin* APin = CallFn->FindPin(TEXT("A"), EGPD_Input))
			{
				if (APin->LinkedTo.Num() > 0)
				{
					if (UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(APin->LinkedTo[0]->GetOwningNodeUnchecked()))
					{
						VarName = VG->GetVarName().ToString();
					}
				}
			}
			OutInfo.RuleType = TEXT("Bool");
			OutInfo.RuleVariable = VarName;
			OutInfo.RuleSummary = VarName.IsEmpty() ? TEXT("NOT (bool)") : FString::Printf(TEXT("%s == false"), *VarName);
			OutInfo.bHasRule = true;
			return;
		}

		// Treat as a numeric comparison.
		FString VarName;
		if (UEdGraphPin* APin = CallFn->FindPin(TEXT("A"), EGPD_Input))
		{
			if (APin->LinkedTo.Num() > 0)
			{
				if (UK2Node_VariableGet* VG = Cast<UK2Node_VariableGet>(APin->LinkedTo[0]->GetOwningNodeUnchecked()))
				{
					VarName = VG->GetVarName().ToString();
				}
			}
		}
		FString BVal;
		if (UEdGraphPin* BPin = CallFn->FindPin(TEXT("B"), EGPD_Input))
		{
			BVal = BPin->DefaultValue;
		}
		OutInfo.RuleType = TEXT("Comparison");
		OutInfo.RuleVariable = VarName;
		OutInfo.RuleSummary = FString::Printf(TEXT("%s %s %s"), *VarName, *FunctionNameToSymbol(FnName), *BVal);
		OutInfo.bHasRule = true;
		return;
	}

	OutInfo.RuleType = TEXT("Custom");
	OutInfo.RuleSummary = TEXT("custom rule graph");
	OutInfo.bHasRule = true;
}

// ============================================================================
// ENTRY STATE / TRANSITION SETTINGS
// ============================================================================

bool UAnimGraphService::SetEntryState(const FString& AnimBlueprintPath, const FString& StateMachineName, const FString& StateName)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Entry State"), AnimBlueprint);

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetEntryState: State machine '%s' not found"), *StateMachineName);
		return false;
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(StateMachineNode->EditorStateMachineGraph);
	if (!SMGraph || !SMGraph->EntryNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetEntryState: State machine '%s' has no entry node"), *StateMachineName);
		return false;
	}

	UAnimStateNodeBase* TargetState = FindStateNode(SMGraph, StateName);
	if (!TargetState)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetEntryState: State '%s' not found"), *StateName);
		return false;
	}

	UEdGraphPin* EntryOutPin = SMGraph->EntryNode->GetOutputPin();
	UEdGraphPin* StateInPin = TargetState->GetInputPin();
	if (!EntryOutPin || !StateInPin)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetEntryState: Could not resolve entry/state pins"));
		return false;
	}

	EntryOutPin->BreakAllPinLinks();
	EntryOutPin->MakeLinkTo(StateInPin);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetEntryState: '%s' is now the entry state of '%s'"), *StateName, *StateMachineName);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetTransitionPriority(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, int32 Priority)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionPriority: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition Priority"), AnimBlueprint, Transition);

	Transition->PriorityOrder = Priority;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionPriority: '%s'->'%s' priority = %d"), *SourceStateName, *DestStateName, Priority);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetTransitionBlend(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, float BlendDuration, const FString& BlendMode)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionBlend: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition Blend"), AnimBlueprint, Transition);

	Transition->CrossfadeDuration = FMath::Max(0.0f, BlendDuration);
	Transition->BlendMode = ParseBlendOption(BlendMode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionBlend: '%s'->'%s' blend %.2fs (%s)"), *SourceStateName, *DestStateName, BlendDuration, *BlendMode);
	Scope.MarkSuccess();
	return true;
}

// ============================================================================
// TRANSITION RULES
// ============================================================================

bool UAnimGraphService::SetTransitionRuleFromBool(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, const FString& BoolVariableName, bool bInvert)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleFromBool: transition '%s'->'%s' not found in '%s'"), *SourceStateName, *DestStateName, *StateMachineName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition Rule (Bool)"), AnimBlueprint, Transition);

	UClass* VarClass = AnimBlueprint->SkeletonGeneratedClass ? AnimBlueprint->SkeletonGeneratedClass : AnimBlueprint->GeneratedClass;
	FProperty* Prop = VarClass ? VarClass->FindPropertyByName(FName(*BoolVariableName)) : nullptr;
	if (!Prop || !Prop->IsA<FBoolProperty>())
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleFromBool: bool variable '%s' not found on AnimBP (add it and compile first)"), *BoolVariableName);
		return false;
	}

	ResetTransitionRule(Transition);

	UAnimGraphNode_TransitionResult* ResultNode = GetTransitionResultNode(Transition);
	UEdGraph* RuleGraph = Transition->BoundGraph;
	if (!ResultNode || !RuleGraph)
	{
		return false;
	}
	UEdGraphPin* CanEnterPin = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input);
	if (!CanEnterPin)
	{
		return false;
	}

	FGraphNodeCreator<UK2Node_VariableGet> VarCreator(*RuleGraph);
	UK2Node_VariableGet* VarNode = VarCreator.CreateNode();
	VarNode->VariableReference.SetSelfMember(FName(*BoolVariableName));
	VarNode->NodePosX = ResultNode->NodePosX - (bInvert ? 500 : 280);
	VarNode->NodePosY = ResultNode->NodePosY;
	VarCreator.Finalize();

	UEdGraphPin* VarOutPin = GetVariableValuePin(VarNode);
	if (!VarOutPin)
	{
		RuleGraph->RemoveNode(VarNode);
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleFromBool: failed to resolve value pin for '%s'"), *BoolVariableName);
		return false;
	}

	if (bInvert)
	{
		UFunction* NotFn = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(TEXT("Not_PreBool")));
		if (!NotFn)
		{
			RuleGraph->RemoveNode(VarNode);
			return false;
		}
		FGraphNodeCreator<UK2Node_CallFunction> FnCreator(*RuleGraph);
		UK2Node_CallFunction* NotNode = FnCreator.CreateNode();
		NotNode->SetFromFunction(NotFn);
		NotNode->NodePosX = ResultNode->NodePosX - 250;
		NotNode->NodePosY = ResultNode->NodePosY;
		FnCreator.Finalize();

		UEdGraphPin* APin = NotNode->FindPin(TEXT("A"), EGPD_Input);
		UEdGraphPin* RetPin = NotNode->GetReturnValuePin();
		if (!APin || !RetPin)
		{
			return false;
		}
		VarOutPin->MakeLinkTo(APin);
		RetPin->MakeLinkTo(CanEnterPin);
	}
	else
	{
		VarOutPin->MakeLinkTo(CanEnterPin);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionRuleFromBool: '%s'->'%s' driven by %s%s"),
		*SourceStateName, *DestStateName, bInvert ? TEXT("NOT ") : TEXT(""), *BoolVariableName);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetTransitionRuleComparison(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, const FString& FloatVariableName, const FString& Comparison, float Threshold)
{
	const FString FnName = ComparisonToFunctionName(Comparison);
	if (FnName.IsEmpty())
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleComparison: unknown comparison '%s' (use greater/less/greater_equal/less_equal/equal/not_equal)"), *Comparison);
		return false;
	}

	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleComparison: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition Rule (Comparison)"), AnimBlueprint, Transition);

	UClass* VarClass = AnimBlueprint->SkeletonGeneratedClass ? AnimBlueprint->SkeletonGeneratedClass : AnimBlueprint->GeneratedClass;
	FProperty* Prop = VarClass ? VarClass->FindPropertyByName(FName(*FloatVariableName)) : nullptr;
	if (!Prop || !Prop->IsA<FNumericProperty>())
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleComparison: numeric variable '%s' not found on AnimBP (add it and compile first)"), *FloatVariableName);
		return false;
	}

	UFunction* CmpFn = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(*FnName));
	if (!CmpFn)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleComparison: math function '%s' not found"), *FnName);
		return false;
	}

	ResetTransitionRule(Transition);

	UAnimGraphNode_TransitionResult* ResultNode = GetTransitionResultNode(Transition);
	UEdGraph* RuleGraph = Transition->BoundGraph;
	if (!ResultNode || !RuleGraph)
	{
		return false;
	}
	UEdGraphPin* CanEnterPin = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input);
	if (!CanEnterPin)
	{
		return false;
	}

	FGraphNodeCreator<UK2Node_VariableGet> VarCreator(*RuleGraph);
	UK2Node_VariableGet* VarNode = VarCreator.CreateNode();
	VarNode->VariableReference.SetSelfMember(FName(*FloatVariableName));
	VarNode->NodePosX = ResultNode->NodePosX - 550;
	VarNode->NodePosY = ResultNode->NodePosY;
	VarCreator.Finalize();

	UEdGraphPin* VarOutPin = GetVariableValuePin(VarNode);
	if (!VarOutPin)
	{
		RuleGraph->RemoveNode(VarNode);
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleComparison: failed to resolve value pin for '%s'"), *FloatVariableName);
		return false;
	}

	FGraphNodeCreator<UK2Node_CallFunction> FnCreator(*RuleGraph);
	UK2Node_CallFunction* CmpNode = FnCreator.CreateNode();
	CmpNode->SetFromFunction(CmpFn);
	CmpNode->NodePosX = ResultNode->NodePosX - 280;
	CmpNode->NodePosY = ResultNode->NodePosY;
	FnCreator.Finalize();

	UEdGraphPin* APin = CmpNode->FindPin(TEXT("A"), EGPD_Input);
	UEdGraphPin* BPin = CmpNode->FindPin(TEXT("B"), EGPD_Input);
	UEdGraphPin* RetPin = CmpNode->GetReturnValuePin();
	if (!APin || !BPin || !RetPin)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleComparison: comparison node pins missing"));
		return false;
	}

	VarOutPin->MakeLinkTo(APin);
	BPin->DefaultValue = FString::SanitizeFloat(Threshold);
	RetPin->MakeLinkTo(CanEnterPin);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionRuleComparison: '%s'->'%s' rule = %s %s %.3f"),
		*SourceStateName, *DestStateName, *FloatVariableName, *FunctionNameToSymbol(FnName), Threshold);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetTransitionRuleAutomatic(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, float TriggerTime)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionRuleAutomatic: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition Rule (Automatic)"), AnimBlueprint, Transition);

	ResetTransitionRule(Transition);
	Transition->bAutomaticRuleBasedOnSequencePlayerInState = true;
	Transition->AutomaticRuleTriggerTime = TriggerTime;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionRuleAutomatic: '%s'->'%s' auto (trigger %.3f)"), *SourceStateName, *DestStateName, TriggerTime);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::ClearTransitionRule(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("ClearTransitionRule: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Clear Transition Rule"), AnimBlueprint, Transition);

	ResetTransitionRule(Transition);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("ClearTransitionRule: cleared '%s'->'%s'"), *SourceStateName, *DestStateName);
	Scope.MarkSuccess();
	return true;
}

// ============================================================================
// ADVANCED TRANSITION SEMANTICS
// (per-bone blend profile, notify events, and transition logic type)
// ============================================================================

namespace
{
	/** Map EAlphaBlendOption back to the canonical string used by SetTransitionBlend. */
	FString BlendOptionToString(EAlphaBlendOption InOption)
	{
		switch (InOption)
		{
		case EAlphaBlendOption::Linear:         return TEXT("Linear");
		case EAlphaBlendOption::Cubic:          return TEXT("Cubic");
		case EAlphaBlendOption::HermiteCubic:   return TEXT("HermiteCubic");
		case EAlphaBlendOption::Sinusoidal:     return TEXT("Sinusoidal");
		case EAlphaBlendOption::QuadraticInOut: return TEXT("QuadraticInOut");
		case EAlphaBlendOption::CubicInOut:     return TEXT("CubicInOut");
		case EAlphaBlendOption::ExpInOut:       return TEXT("ExpInOut");
		case EAlphaBlendOption::CircularIn:     return TEXT("CircularIn");
		case EAlphaBlendOption::CircularOut:    return TEXT("CircularOut");
		case EAlphaBlendOption::CircularInOut:  return TEXT("CircularInOut");
		default:                                return TEXT("Linear");
		}
	}

	/** Map ETransitionLogicType to the canonical string used by GetTransitionAdvancedInfo. */
	FString LogicTypeToString(ETransitionLogicType::Type InLogic)
	{
		switch (InLogic)
		{
		case ETransitionLogicType::TLT_StandardBlend:   return TEXT("StandardBlend");
		case ETransitionLogicType::TLT_Inertialization: return TEXT("Inertialization");
		case ETransitionLogicType::TLT_Custom:          return TEXT("Custom");
		default:                                        return TEXT("StandardBlend");
		}
	}
}

bool UAnimGraphService::SetTransitionBlendProfile(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, const FString& BlendProfileAssetPath)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionBlendProfile: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition Blend Profile"), AnimBlueprint, Transition);

	UBlendProfile* LoadedProfile = LoadObject<UBlendProfile>(nullptr, *BlendProfileAssetPath);
	if (!LoadedProfile)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionBlendProfile: failed to load UBlendProfile at '%s'"), *BlendProfileAssetPath);
		return false;
	}

	// UE 5.8: Transition->BlendProfile is BlendProfile_DEPRECATED; the live field is
	// FBlendProfileInterfaceWrapper BlendProfileWrapper. Use its SetSkeletonBlendProfile
	// helper so bIsSkeletonBlendProfile stays coherent and the wrapper's cached ptr updates.
	Transition->BlendProfileWrapper.SetSkeletonBlendProfile(LoadedProfile);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionBlendProfile: '%s'->'%s' profile = '%s'"),
		*SourceStateName, *DestStateName, *BlendProfileAssetPath);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetTransitionStartEvent(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, const FString& NotifyName)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionStartEvent: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition Start Event"), AnimBlueprint, Transition);

	Transition->TransitionStart.NotifyName = NotifyName.IsEmpty() ? NAME_None : FName(*NotifyName);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionStartEvent: '%s'->'%s' notify = '%s'"),
		*SourceStateName, *DestStateName, *NotifyName);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetTransitionEndEvent(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, const FString& NotifyName)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionEndEvent: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition End Event"), AnimBlueprint, Transition);

	Transition->TransitionEnd.NotifyName = NotifyName.IsEmpty() ? NAME_None : FName(*NotifyName);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionEndEvent: '%s'->'%s' notify = '%s'"),
		*SourceStateName, *DestStateName, *NotifyName);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetTransitionInterruptionEvent(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, const FString& NotifyName)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionInterruptionEvent: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition Interruption Event"), AnimBlueprint, Transition);

	Transition->TransitionInterrupt.NotifyName = NotifyName.IsEmpty() ? NAME_None : FName(*NotifyName);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionInterruptionEvent: '%s'->'%s' notify = '%s'"),
		*SourceStateName, *DestStateName, *NotifyName);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetTransitionLogicType(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName, const FString& LogicTypeStr)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionLogicType: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Transition Logic Type"), AnimBlueprint, Transition);

	const FString Normalized = LogicTypeStr.TrimStartAndEnd();
	ETransitionLogicType::Type NewLogic;
	if (Normalized.Equals(TEXT("StandardBlend"), ESearchCase::IgnoreCase))
	{
		NewLogic = ETransitionLogicType::TLT_StandardBlend;
	}
	else if (Normalized.Equals(TEXT("Inertialization"), ESearchCase::IgnoreCase))
	{
		NewLogic = ETransitionLogicType::TLT_Inertialization;
	}
	else if (Normalized.Equals(TEXT("Custom"), ESearchCase::IgnoreCase))
	{
		// Custom logic requires a hand-authored custom transition subgraph. Refuse rather
		// than silently produce a broken transition — caller must build the graph manually.
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("SetTransitionLogicType: 'Custom' rejected — requires custom transition subgraph. Use StandardBlend or Inertialization."));
		return false;
	}
	else
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetTransitionLogicType: unknown logic type '%s' (use StandardBlend or Inertialization)"), *LogicTypeStr);
		return false;
	}

	Transition->LogicType = NewLogic;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetTransitionLogicType: '%s'->'%s' logic = %s"),
		*SourceStateName, *DestStateName, *Normalized);
	Scope.MarkSuccess();
	return true;
}

FString UAnimGraphService::GetTransitionAdvancedInfo(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SourceStateName, const FString& DestStateName)
{
	UAnimBlueprint* AnimBlueprint = nullptr;
	UAnimStateTransitionNode* Transition = ResolveTransition(AnimBlueprintPath, StateMachineName, SourceStateName, DestStateName, AnimBlueprint);
	if (!Transition || !AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("GetTransitionAdvancedInfo: transition '%s'->'%s' not found"), *SourceStateName, *DestStateName);
		return FString();
	}

	// Read-only: no FAnimBPMutationScope, no Modify(), no structural-modified mark.

	FString BlendProfilePath;
	if (const UBlendProfile* ExistingProfile = Transition->BlendProfileWrapper.GetBlendProfile())
	{
		BlendProfilePath = ExistingProfile->GetPathName();
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("blend_profile"),              BlendProfilePath);
	Root->SetStringField(TEXT("transition_start_event"),     Transition->TransitionStart.NotifyName.IsNone()     ? FString() : Transition->TransitionStart.NotifyName.ToString());
	Root->SetStringField(TEXT("transition_end_event"),       Transition->TransitionEnd.NotifyName.IsNone()       ? FString() : Transition->TransitionEnd.NotifyName.ToString());
	Root->SetStringField(TEXT("transition_interrupt_event"), Transition->TransitionInterrupt.NotifyName.IsNone() ? FString() : Transition->TransitionInterrupt.NotifyName.ToString());
	Root->SetStringField(TEXT("logic_type"),                 LogicTypeToString(Transition->LogicType.GetValue()));
	Root->SetNumberField(TEXT("priority"),                   Transition->PriorityOrder);
	Root->SetNumberField(TEXT("blend_duration"),             Transition->CrossfadeDuration);
	Root->SetStringField(TEXT("blend_mode"),                 BlendOptionToString(Transition->BlendMode));
	Root->SetNumberField(TEXT("crossfade_duration"),         Transition->CrossfadeDuration);
	Root->SetStringField(TEXT("shared_rules_guid"),          Transition->SharedRulesGuid.IsValid()     ? Transition->SharedRulesGuid.ToString()     : FString());
	Root->SetStringField(TEXT("shared_crossfade_guid"),      Transition->SharedCrossfadeGuid.IsValid() ? Transition->SharedCrossfadeGuid.ToString() : FString());

	FString OutJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root, Writer);
	return OutJson;
}

// ============================================================================
// HIGH-LEVEL STATE AUTHORING
// ============================================================================

FString UAnimGraphService::SetStateAnimation(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& StateName, const FString& AnimSequencePath, bool bLoop, float PlayRate)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Set State Animation"), AnimBlueprint);

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetStateAnimation: state machine '%s' not found"), *StateMachineName);
		return FString();
	}

	UAnimStateNode* StateNode = Cast<UAnimStateNode>(FindStateNode(StateMachineNode->EditorStateMachineGraph, StateName));
	if (!StateNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetStateAnimation: state '%s' not found (or is a conduit)"), *StateName);
		return FString();
	}

	UEdGraph* StateGraph = StateNode->GetBoundGraph();
	if (!StateGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetStateAnimation: state '%s' has no graph"), *StateName);
		return FString();
	}

	UAnimSequence* Sequence = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimSequencePath));
	if (!Sequence)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetStateAnimation: could not load sequence '%s'"), *AnimSequencePath);
		return FString();
	}

	UAnimGraphNode_SequencePlayer* SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(FindStateAssetPlayer(StateNode));
	if (!SeqPlayer)
	{
		FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(*StateGraph);
		SeqPlayer = Creator.CreateNode();
		SeqPlayer->NodePosX = -350;
		SeqPlayer->NodePosY = 0;
		Creator.Finalize();
	}

	SeqPlayer->Node.SetSequence(Sequence);
	SeqPlayer->Node.SetLoopAnimation(bLoop);
	SeqPlayer->Node.SetPlayRate(PlayRate);

	// Wire the player's pose output to the state's Output Pose (result) node.
	UAnimGraphNode_StateResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : StateGraph->Nodes)
	{
		if (UAnimGraphNode_StateResult* Result = Cast<UAnimGraphNode_StateResult>(Node))
		{
			ResultNode = Result;
			break;
		}
	}
	if (ResultNode)
	{
		UEdGraphPin* OutPin = nullptr;
		for (UEdGraphPin* Pin : SeqPlayer->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				OutPin = Pin;
				break;
			}
		}
		UEdGraphPin* InPin = nullptr;
		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input)
			{
				InPin = Pin;
				break;
			}
		}
		if (OutPin && InPin && !OutPin->LinkedTo.Contains(InPin))
		{
			InPin->BreakAllPinLinks();
			OutPin->MakeLinkTo(InPin);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	UE_LOG(LogRevvyAnimGraph, Log, TEXT("SetStateAnimation: '%s' in '%s' -> %s (loop=%d, rate=%.2f)"),
		*StateName, *StateMachineName, *AnimSequencePath, bLoop ? 1 : 0, PlayRate);
	Scope.MarkSuccess();
	return SeqPlayer->NodeGuid.ToString();
}

FString UAnimGraphService::BuildStateMachine(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& SpecJson, float PosX, float PosY)
{
	TArray<FString> Errors;
	int32 StatesCreated = 0;
	int32 TransitionsCreated = 0;

	auto MakeReport = [&](bool bSuccess) -> FString
	{
		TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetBoolField(TEXT("success"), bSuccess);
		Report->SetStringField(TEXT("machine"), StateMachineName);
		Report->SetNumberField(TEXT("states_created"), StatesCreated);
		Report->SetNumberField(TEXT("transitions_created"), TransitionsCreated);
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : Errors)
		{
			ErrArr.Add(MakeShared<FJsonValueString>(E));
		}
		Report->SetArrayField(TEXT("errors"), ErrArr);
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Report, Writer);
		return Out;
	};

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		Errors.Add(TEXT("Failed to load AnimBlueprint"));
		return MakeReport(false);
	}

	FAnimBPMutationScope Scope(TEXT("Build State Machine"), AnimBlueprint);
	Scope.RequestCompile();

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Errors.Add(TEXT("Invalid JSON spec"));
		return MakeReport(false);
	}

	if (!FindStateMachineNode(AnimBlueprint, StateMachineName))
	{
		const FString NewMachineId = AddStateMachine(AnimBlueprintPath, StateMachineName, PosX, PosY);
		if (NewMachineId.IsEmpty())
		{
			Errors.Add(FString::Printf(TEXT("Failed to create state machine '%s'"), *StateMachineName));
			return MakeReport(false);
		}
	}

	FString FirstStateName;

	// --- States ---
	const TArray<TSharedPtr<FJsonValue>>* StatesArr = nullptr;
	if (Root->TryGetArrayField(TEXT("states"), StatesArr) && StatesArr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *StatesArr)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				continue;
			}
			TSharedPtr<FJsonObject> StateObj = Value->AsObject();
			if (!StateObj.IsValid())
			{
				continue;
			}

			FString SName;
			if (!StateObj->TryGetStringField(TEXT("name"), SName) || SName.IsEmpty())
			{
				Errors.Add(TEXT("State entry missing 'name'"));
				continue;
			}
			if (FirstStateName.IsEmpty())
			{
				FirstStateName = SName;
			}

			float SX = PosX;
			float SY = PosY;
			const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
			if (StateObj->TryGetArrayField(TEXT("pos"), PosArr) && PosArr && PosArr->Num() >= 2)
			{
				SX = (float)(*PosArr)[0]->AsNumber();
				SY = (float)(*PosArr)[1]->AsNumber();
			}

			UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
			UEdGraph* SMGraph = SMNode ? SMNode->EditorStateMachineGraph : nullptr;
			if (!SMGraph || !FindStateNode(SMGraph, SName))
			{
				const FString StateId = AddState(AnimBlueprintPath, StateMachineName, SName, SX, SY);
				if (StateId.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("Failed to add state '%s'"), *SName));
					continue;
				}
				StatesCreated++;
			}

			FString Anim;
			if (StateObj->TryGetStringField(TEXT("animation"), Anim) && !Anim.IsEmpty())
			{
				bool bLoop = true;
				StateObj->TryGetBoolField(TEXT("loop"), bLoop);
				double Rate = 1.0;
				StateObj->TryGetNumberField(TEXT("play_rate"), Rate);
				if (SetStateAnimation(AnimBlueprintPath, StateMachineName, SName, Anim, bLoop, (float)Rate).IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("Failed to set animation '%s' on state '%s'"), *Anim, *SName));
				}
			}
		}
	}

	// --- Transitions ---
	const TArray<TSharedPtr<FJsonValue>>* TransArr = nullptr;
	if (Root->TryGetArrayField(TEXT("transitions"), TransArr) && TransArr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *TransArr)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				continue;
			}
			TSharedPtr<FJsonObject> TransObj = Value->AsObject();
			if (!TransObj.IsValid())
			{
				continue;
			}

			FString From;
			FString To;
			if (!TransObj->TryGetStringField(TEXT("from"), From) || !TransObj->TryGetStringField(TEXT("to"), To))
			{
				Errors.Add(TEXT("Transition entry missing 'from'/'to'"));
				continue;
			}

			double Blend = 0.2;
			TransObj->TryGetNumberField(TEXT("blend"), Blend);

			UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
			UEdGraph* SMGraph = SMNode ? SMNode->EditorStateMachineGraph : nullptr;
			if (!SMGraph || !FindTransitionNode(SMGraph, From, To))
			{
				const FString TransId = AddTransition(AnimBlueprintPath, StateMachineName, From, To, (float)Blend);
				if (TransId.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("Failed to add transition '%s'->'%s'"), *From, *To));
					continue;
				}
				TransitionsCreated++;
			}
			else
			{
				SetTransitionBlend(AnimBlueprintPath, StateMachineName, From, To, (float)Blend, TEXT("Linear"));
			}

			double PriorityD = 0.0;
			if (TransObj->TryGetNumberField(TEXT("priority"), PriorityD))
			{
				SetTransitionPriority(AnimBlueprintPath, StateMachineName, From, To, (int32)PriorityD);
			}

			const TSharedPtr<FJsonObject>* RuleObjPtr = nullptr;
			if (TransObj->TryGetObjectField(TEXT("rule"), RuleObjPtr) && RuleObjPtr && RuleObjPtr->IsValid())
			{
				const TSharedPtr<FJsonObject> RuleObj = *RuleObjPtr;
				FString RType;
				RuleObj->TryGetStringField(TEXT("type"), RType);
				RType = RType.ToLower();

				if (RType == TEXT("bool"))
				{
					FString Var;
					bool bInv = false;
					RuleObj->TryGetStringField(TEXT("variable"), Var);
					RuleObj->TryGetBoolField(TEXT("invert"), bInv);
					if (!SetTransitionRuleFromBool(AnimBlueprintPath, StateMachineName, From, To, Var, bInv))
					{
						Errors.Add(FString::Printf(TEXT("Failed bool rule on '%s'->'%s' (variable '%s')"), *From, *To, *Var));
					}
				}
				else if (RType == TEXT("comparison"))
				{
					FString Var;
					FString Op;
					double Val = 0.0;
					RuleObj->TryGetStringField(TEXT("variable"), Var);
					RuleObj->TryGetStringField(TEXT("op"), Op);
					RuleObj->TryGetNumberField(TEXT("value"), Val);
					if (!SetTransitionRuleComparison(AnimBlueprintPath, StateMachineName, From, To, Var, Op, (float)Val))
					{
						Errors.Add(FString::Printf(TEXT("Failed comparison rule on '%s'->'%s'"), *From, *To));
					}
				}
				else if (RType == TEXT("automatic"))
				{
					double Trig = -1.0;
					RuleObj->TryGetNumberField(TEXT("trigger_time"), Trig);
					SetTransitionRuleAutomatic(AnimBlueprintPath, StateMachineName, From, To, (float)Trig);
				}
				else if (RType == TEXT("always"))
				{
					UAnimBlueprint* RuleBP = nullptr;
					UAnimStateTransitionNode* RuleTrans = ResolveTransition(AnimBlueprintPath, StateMachineName, From, To, RuleBP);
					if (RuleTrans)
					{
						ResetTransitionRule(RuleTrans);
						if (UAnimGraphNode_TransitionResult* RN = GetTransitionResultNode(RuleTrans))
						{
							if (UEdGraphPin* CEP = RN->FindPin(TEXT("bCanEnterTransition"), EGPD_Input))
							{
								CEP->DefaultValue = TEXT("true");
							}
						}
						if (RuleBP)
						{
							FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(RuleBP);
						}
					}
				}
			}
		}
	}

	// --- Entry ---
	FString Entry;
	if (!Root->TryGetStringField(TEXT("entry"), Entry) || Entry.IsEmpty())
	{
		Entry = FirstStateName;
	}
	if (!Entry.IsEmpty())
	{
		if (!SetEntryState(AnimBlueprintPath, StateMachineName, Entry))
		{
			Errors.Add(FString::Printf(TEXT("Failed to set entry state '%s'"), *Entry));
		}
	}

	Scope.MarkSuccess();
	return MakeReport(Errors.Num() == 0);
}

FAnimStateMachineValidationResult UAnimGraphService::ValidateStateMachine(const FString& AnimBlueprintPath, const FString& StateMachineName)
{
	FAnimStateMachineValidationResult Result;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		Result.Errors.Add(TEXT("Failed to load AnimBlueprint"));
		return Result;
	}

	UAnimGraphNode_StateMachine* StateMachineNode = FindStateMachineNode(AnimBlueprint, StateMachineName);
	if (!StateMachineNode || !StateMachineNode->EditorStateMachineGraph)
	{
		Result.Errors.Add(FString::Printf(TEXT("State machine '%s' not found"), *StateMachineName));
		return Result;
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(StateMachineNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		Result.Errors.Add(TEXT("State machine graph is invalid"));
		return Result;
	}

	TArray<UAnimStateNode*> States;
	TArray<UAnimStateTransitionNode*> Transitions;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{
			States.Add(State);
		}
		else if (UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node))
		{
			Transitions.Add(Transition);
		}
	}
	Result.StateCount = States.Num();
	Result.TransitionCount = Transitions.Num();

	// Entry state
	UAnimStateNodeBase* EntryTarget = nullptr;
	if (SMGraph->EntryNode)
	{
		if (UEdGraphPin* EntryOut = SMGraph->EntryNode->GetOutputPin())
		{
			if (EntryOut->LinkedTo.Num() > 0 && EntryOut->LinkedTo[0])
			{
				EntryTarget = Cast<UAnimStateNodeBase>(EntryOut->LinkedTo[0]->GetOwningNodeUnchecked());
			}
		}
	}
	if (!EntryTarget)
	{
		Result.Errors.Add(TEXT("No entry state set (state machine has no default state, will not run)"));
	}

	// States with no animation/pose
	for (UAnimStateNode* State : States)
	{
		bool bHasPose = false;
		if (UEdGraph* StateGraph = State->GetBoundGraph())
		{
			for (UEdGraphNode* Node : StateGraph->Nodes)
			{
				if (UAnimGraphNode_StateResult* ResultNode = Cast<UAnimGraphNode_StateResult>(Node))
				{
					for (UEdGraphPin* Pin : ResultNode->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() > 0)
						{
							bHasPose = true;
							break;
						}
					}
				}
			}
		}
		if (!bHasPose)
		{
			Result.Warnings.Add(FString::Printf(TEXT("State '%s' has no animation connected to its Output Pose"), *State->GetStateName()));
		}
	}

	// Inert transitions
	for (UAnimStateTransitionNode* Transition : Transitions)
	{
		FAnimTransitionInfo Info;
		DescribeTransitionRule(Transition, Info);
		if (!Info.bHasRule)
		{
			UAnimStateNodeBase* Prev = Transition->GetPreviousState();
			UAnimStateNodeBase* Next = Transition->GetNextState();
			Result.Errors.Add(FString::Printf(TEXT("Transition '%s'->'%s' is inert (no rule) and will never fire"),
				Prev ? *Prev->GetStateName() : TEXT("?"), Next ? *Next->GetStateName() : TEXT("?")));
		}
	}

	// Reachability from entry
	if (EntryTarget)
	{
		TSet<UAnimStateNodeBase*> Reachable;
		TArray<UAnimStateNodeBase*> Stack;
		Reachable.Add(EntryTarget);
		Stack.Add(EntryTarget);
		while (Stack.Num() > 0)
		{
			UAnimStateNodeBase* Current = Stack.Pop();
			for (UAnimStateTransitionNode* Transition : Transitions)
			{
				if (Transition->GetPreviousState() == Current)
				{
					UAnimStateNodeBase* Next = Transition->GetNextState();
					if (Next && !Reachable.Contains(Next))
					{
						Reachable.Add(Next);
						Stack.Add(Next);
					}
				}
			}
		}
		for (UAnimStateNode* State : States)
		{
			if (!Reachable.Contains(State))
			{
				Result.Warnings.Add(FString::Printf(TEXT("State '%s' is unreachable from the entry state"), *State->GetStateName()));
			}
		}
	}

	Result.bIsValid = (Result.Errors.Num() == 0);
	return Result;
}

// =============================================================================
// P0 Revvy upgrade — PIE-less AnimBP verification
// =============================================================================

FAnimGraphOutputConnectionInfo UAnimGraphService::GetAnimGraphOutputConnection(const FString& AnimBlueprintPath)
{
	FAnimGraphOutputConnectionInfo Info;

	UAnimBlueprint* ABP = LoadAnimBlueprint(AnimBlueprintPath);
	if (!ABP)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("GetAnimGraphOutputConnection: failed to load %s"), *AnimBlueprintPath);
		return Info;
	}

	UEdGraph* MainGraph = FindAnimGraph(ABP, TEXT("AnimGraph"));
	if (!MainGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("GetAnimGraphOutputConnection: AnimGraph not found in %s"), *AnimBlueprintPath);
		return Info;
	}

	UAnimGraphNode_Root* Root = nullptr;
	for (UEdGraphNode* N : MainGraph->Nodes)
	{
		if ((Root = Cast<UAnimGraphNode_Root>(N)) != nullptr)
		{
			break;
		}
	}
	if (!Root)
	{
		return Info;
	}

	// Output Pose is the Root node's incoming Result pin link.
	for (UEdGraphPin* Pin : Root->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() > 0)
		{
			UEdGraphPin* SourcePin = Pin->LinkedTo[0];
			if (SourcePin && SourcePin->GetOwningNode())
			{
				Info.bIsDriven = true;
				Info.SourceNodeId = SourcePin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::Digits);
				Info.SourceNodeClass = SourcePin->GetOwningNode()->GetClass()->GetName();
				Info.SourcePin = SourcePin->PinName.ToString();
			}
			break;
		}
	}

	return Info;
}

TArray<FAnimLinkedLayerInfo> UAnimGraphService::GetLinkedAnimLayers(const FString& AnimBlueprintPath)
{
	TArray<FAnimLinkedLayerInfo> Result;

	UAnimBlueprint* ABP = LoadAnimBlueprint(AnimBlueprintPath);
	if (!ABP)
	{
		UE_LOG(LogRevvyAnimGraph, Warning, TEXT("GetLinkedAnimLayers: failed to load %s"), *AnimBlueprintPath);
		return Result;
	}

	// Build a quick lookup of (LayerName → InterfaceGraphGuid) so we can verify
	// each LinkedAnimLayer's InterfaceGuid actually maps to one of the implemented
	// interface's layer graphs. A mismatch means the layer will silently A-pose.
	TMap<FName, FGuid> LayerNameToGuid;
	TMap<FName, UClass*> LayerNameToInterfaceClass;
	for (const FBPInterfaceDescription& Desc : ABP->ImplementedInterfaces)
	{
		for (UEdGraph* IfaceGraph : Desc.Graphs)
		{
			if (IfaceGraph)
			{
				LayerNameToGuid.Add(IfaceGraph->GetFName(), IfaceGraph->InterfaceGuid);
				LayerNameToInterfaceClass.Add(IfaceGraph->GetFName(), Desc.Interface);
			}
		}
	}

	// Walk EVERY UEdGraph attached to the blueprint (function graphs + interface
	// layer graphs + anim graph + state machine subgraphs) and collect every
	// UAnimGraphNode_LinkedAnimLayer node.
	TArray<UEdGraph*> AllGraphs;
	ABP->GetAllGraphs(AllGraphs);

	for (UEdGraph* G : AllGraphs)
	{
		if (!G) continue;
		TArray<UAnimGraphNode_LinkedAnimLayer*> LinkedNodes;
		G->GetNodesOfClass<UAnimGraphNode_LinkedAnimLayer>(LinkedNodes);

		for (UAnimGraphNode_LinkedAnimLayer* N : LinkedNodes)
		{
			if (!N) continue;
			FAnimLinkedLayerInfo Entry;
			Entry.NodeId = N->NodeGuid.ToString(EGuidFormats::Digits);
			Entry.Layer = N->Node.Layer.ToString();
			Entry.InterfacePath = N->Node.Interface.Get() ? N->Node.Interface->GetPathName() : TEXT("");
			Entry.InstanceClass = N->Node.InstanceClass.Get() ? N->Node.InstanceClass->GetPathName() : TEXT("");

			// Did the InterfaceGuid resolve to a real ImplementedInterfaces graph?
			if (const FGuid* Found = LayerNameToGuid.Find(N->Node.Layer))
			{
				Entry.bGuidResolved = (N->InterfaceGuid.IsValid() && N->InterfaceGuid == *Found);
			}
			else
			{
				Entry.bGuidResolved = false;
			}

			// FunctionReference (parent class) is protected; read via reflection.
			Entry.bFunctionReferenceInSync = false;
			if (FStructProperty* FuncRefProp = FindFProperty<FStructProperty>(N->GetClass(), TEXT("FunctionReference")))
			{
				const FMemberReference* MR = FuncRefProp->ContainerPtrToValuePtr<FMemberReference>(N);
				if (MR)
				{
					Entry.bFunctionReferenceInSync = (MR->GetMemberName() == N->Node.Layer);
				}
			}

			Result.Add(Entry);
		}
	}

	return Result;
}

// =============================================================================
// P0 Revvy upgrade — LinkedAnimLayer recipe + reflection setter
// =============================================================================

namespace
{
	/** Locate the inner FAnimNode_* struct hosted by a UAnimGraphNode_*. */
	static bool ResolveInnerAnimNode(UAnimGraphNode_Base* GraphNode, UScriptStruct*& OutStruct, void*& OutAddr)
	{
		OutStruct = nullptr;
		OutAddr = nullptr;
		if (!GraphNode) return false;
		for (TFieldIterator<FStructProperty> It(GraphNode->GetClass()); It; ++It)
		{
			FStructProperty* SP = *It;
			if (!SP || !SP->Struct) continue;
			// Match a struct whose UPROPERTY name is "Node" (convention used by every
			// UAnimGraphNode_* shipped by Epic). Avoids matching FName/FGuid/etc. members.
			if (SP->GetFName() == FName(TEXT("Node")))
			{
				OutStruct = SP->Struct;
				OutAddr = SP->ContainerPtrToValuePtr<void>(GraphNode);
				return true;
			}
		}
		return false;
	}

	/** Find a node by GUID across every UEdGraph attached to the blueprint. */
	static UEdGraphNode* AnimGraphFindNodeByGuid(UAnimBlueprint* ABP, const FString& NodeId)
	{
		if (!ABP) return nullptr;
		FGuid TargetGuid;
		if (!FGuid::Parse(NodeId, TargetGuid))
		{
			// Allow EGuidFormats::Digits (32 hex chars no dashes) — Parse only accepts dashed.
			FString Dashed = NodeId;
			if (Dashed.Len() == 32)
			{
				Dashed = FString::Printf(TEXT("%s-%s-%s-%s-%s"),
					*NodeId.Mid(0, 8), *NodeId.Mid(8, 4), *NodeId.Mid(12, 4), *NodeId.Mid(16, 4), *NodeId.Mid(20, 12));
				if (!FGuid::Parse(Dashed, TargetGuid)) return nullptr;
			}
			else
			{
				return nullptr;
			}
		}
		TArray<UEdGraph*> AllGraphs;
		ABP->GetAllGraphs(AllGraphs);
		for (UEdGraph* G : AllGraphs)
		{
			if (!G) continue;
			for (UEdGraphNode* N : G->Nodes)
			{
				if (N && N->NodeGuid == TargetGuid) return N;
			}
		}
		return nullptr;
	}
}

FString UAnimGraphService::AddLinkedAnimLayer(
	const FString& AnimBlueprintPath,
	const FString& GraphName,
	const FString& LayerName,
	float PosX,
	float PosY)
{
	UAnimBlueprint* ABP = LoadAnimBlueprint(AnimBlueprintPath);
	if (!ABP)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddLinkedAnimLayer: failed to load %s"), *AnimBlueprintPath);
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Linked Anim Layer"), ABP);
	Scope.RequestCompile();

	UEdGraph* Graph = FindAnimGraph(ABP, GraphName);
	if (!Graph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddLinkedAnimLayer: graph '%s' not found in %s"), *GraphName, *AnimBlueprintPath);
		return FString();
	}

	// Resolve Interface from ImplementedInterfaces by matching the layer name to one of
	// the interface graph names. If unmatched, fail loudly with a list of available layers
	// so the agent can correct the call (rather than silently spawning a broken self-layer).
	const FName LayerFName(*LayerName);
	UClass* InterfaceClass = nullptr;
	FGuid InterfaceGuid;
	TArray<FString> AvailableLayers;
	for (FBPInterfaceDescription& Desc : ABP->ImplementedInterfaces)
	{
		for (UEdGraph* IfaceGraph : Desc.Graphs)
		{
			if (IfaceGraph)
			{
				AvailableLayers.Add(IfaceGraph->GetFName().ToString());
				if (IfaceGraph->GetFName() == LayerFName)
				{
					InterfaceClass = Desc.Interface;
					InterfaceGuid = IfaceGraph->InterfaceGuid;
				}
			}
		}
	}
	if (!InterfaceClass)
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("AddLinkedAnimLayer: layer '%s' not found in any implemented interface on %s. Available layers: [%s]"),
			*LayerName, *AnimBlueprintPath, *FString::Join(AvailableLayers, TEXT(", ")));
		return FString();
	}

	Graph->Modify();

	UAnimGraphNode_LinkedAnimLayer* N = NewObject<UAnimGraphNode_LinkedAnimLayer>(Graph);
	Graph->AddNode(N, /*bFromUI=*/false, /*bSelectNewNode=*/false);
	N->CreateNewGuid();
	N->PostPlacedNewNode();
	N->AllocateDefaultPins();
	N->NodePosX = static_cast<int32>(PosX);
	N->NodePosY = static_cast<int32>(PosY);

	// Replicate UAnimGraphNode_LinkedAnimLayer::SetupFromLayerId body (which is protected)
	// using only public surface + reflection for FunctionReference.
	N->Node.Interface = InterfaceClass;
	N->InterfaceGuid = InterfaceGuid;
	N->Node.InstanceClass = nullptr; // Layer overrides go in via separate setter when desired.
	N->Node.Layer = LayerFName;

	if (FStructProperty* FuncRefProp = FindFProperty<FStructProperty>(N->GetClass(), TEXT("FunctionReference")))
	{
		FMemberReference* MR = FuncRefProp->ContainerPtrToValuePtr<FMemberReference>(N);
		if (MR)
		{
			FGuid FuncGuid;
			FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(
				FBlueprintEditorUtils::GetMostUpToDateClass(InterfaceClass), LayerFName, FuncGuid);
			MR->SetExternalMember(LayerFName, InterfaceClass, FuncGuid);
		}
	}

	N->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ABP);

	UE_LOG(LogRevvyAnimGraph, Log, TEXT("AddLinkedAnimLayer: spawned %s in %s/%s"), *LayerName, *AnimBlueprintPath, *GraphName);
	Scope.MarkSuccess();
	return N->NodeGuid.ToString(EGuidFormats::Digits);
}

bool UAnimGraphService::SetAnimGraphNodeProperty(
	const FString& AnimBlueprintPath,
	const FString& NodeId,
	const FString& PropertyName,
	const FString& ValueText)
{
	UAnimBlueprint* ABP = LoadAnimBlueprint(AnimBlueprintPath);
	if (!ABP)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetAnimGraphNodeProperty: failed to load %s"), *AnimBlueprintPath);
		return false;
	}

	UEdGraphNode* RawNode = AnimGraphFindNodeByGuid(ABP, NodeId);
	UAnimGraphNode_Base* GraphNode = Cast<UAnimGraphNode_Base>(RawNode);
	if (!GraphNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetAnimGraphNodeProperty: node '%s' not found or not an AnimGraphNode in %s"), *NodeId, *AnimBlueprintPath);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Anim Graph Node Property"), ABP, GraphNode);
	Scope.RequestCompile();

	UScriptStruct* InnerStruct = nullptr;
	void* InnerAddr = nullptr;
	if (!ResolveInnerAnimNode(GraphNode, InnerStruct, InnerAddr))
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("SetAnimGraphNodeProperty: could not resolve inner anim node struct for %s"), *NodeId);
		return false;
	}

	FProperty* Prop = InnerStruct->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetAnimGraphNodeProperty: property '%s' not found on %s (inner struct: %s)"),
			*PropertyName, *NodeId, *InnerStruct->GetName());
		return false;
	}

	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(InnerAddr);
	const TCHAR* ImportResult = Prop->ImportText_Direct(*ValueText, PropAddr, GraphNode, PPF_None);
	if (!ImportResult)
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetAnimGraphNodeProperty: ImportText_Direct failed for '%s' = '%s' on %s"),
			*PropertyName, *ValueText, *NodeId);
		return false;
	}

	GraphNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ABP);
	UE_LOG(LogRevvyAnimGraph, Log,
		TEXT("SetAnimGraphNodeProperty: set %s.%s = %s on %s"),
		*InnerStruct->GetName(), *PropertyName, *ValueText, *NodeId);
	Scope.MarkSuccess();
	return true;
}

// =============================================================================
// P0 Revvy upgrade — Control Rig AnimGraph node authoring
//
// FAnimNode_ControlRig private-member access uses reflection over the
// UAnimGraphNode_ControlRig::Node UPROPERTY (5.8 friend list only covers
// UAnimGraphNode_ControlRig and UAnimNodeControlRigLibrary — UAnimGraphService
// is neither). Class assignment goes through the FAnimNode_ControlRig public
// helper SetControlRigClass which populates FControlRigAssetStrongReference
// correctly for 5.8 (ControlRigClass is deprecated).
// =============================================================================

namespace
{
	/**
	 * Resolve a UControlRig-derived UClass from either a full class path
	 * ("/Game/Rigs/CR.CR_C") or a UBlueprint asset path ("/Game/Rigs/CR")
	 * — in the latter case we retry with a "_C" suffix, since Blueprint
	 * generated classes are stored on the Blueprint asset object.
	 */
	static UClass* LoadControlRigClass(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty()) return nullptr;
		UClass* LoadedClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!LoadedClass && !ClassPath.EndsWith(TEXT("_C")))
		{
			const FString Retry = ClassPath + TEXT("_C");
			LoadedClass = LoadObject<UClass>(nullptr, *Retry);
		}
		return LoadedClass;
	}
}

FString UAnimGraphService::AddControlRigNode(
	const FString& AnimBlueprintPath,
	const FString& GraphName,
	const FString& ControlRigClassPath,
	float PosX,
	float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	FAnimBPMutationScope Scope(TEXT("Add Control Rig Node"), AnimBlueprint);
	Scope.RequestCompile();

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddControlRigNode: Graph '%s' not found"), *GraphName);
		return FString();
	}

	// Validate class up-front so a malformed path does not leave a half-created node behind.
	UClass* LoadedClass = nullptr;
	if (!ControlRigClassPath.IsEmpty())
	{
		LoadedClass = LoadControlRigClass(ControlRigClassPath);
		if (!LoadedClass)
		{
			UE_LOG(LogRevvyAnimGraph, Error,
				TEXT("AddControlRigNode: Could not load class '%s'"), *ControlRigClassPath);
			return FString();
		}
		if (!LoadedClass->IsChildOf(UControlRig::StaticClass()))
		{
			UE_LOG(LogRevvyAnimGraph, Error,
				TEXT("AddControlRigNode: Class '%s' is not a UControlRig-derived class"),
				*ControlRigClassPath);
			return FString();
		}
	}

	FGraphNodeCreator<UAnimGraphNode_ControlRig> NodeCreator(*TargetGraph);
	UAnimGraphNode_ControlRig* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Error, TEXT("AddControlRigNode: Failed to create node"));
		return FString();
	}

	NewNode->NodePosX = static_cast<int32>(PosX);
	NewNode->NodePosY = static_cast<int32>(PosY);

	if (LoadedClass)
	{
		// 5.8 public helper — internally fills FControlRigAssetStrongReference
		// (the deprecated ControlRigClass member is left alone).
		NewNode->Node.SetControlRigClass(TSubclassOf<UControlRig>(LoadedClass));
	}

	NodeCreator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log,
		TEXT("AddControlRigNode: Created in '%s' with class '%s'"),
		*GraphName, *ControlRigClassPath);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

bool UAnimGraphService::SetControlRigNodeClass(
	const FString& AnimBlueprintPath,
	const FString& GraphName,
	const FString& NodeId,
	const FString& ControlRigClassPath)
{
	// GraphName is accepted for API symmetry; NodeGuid is globally unique across
	// the blueprint so the search is scoped at the whole-ABP level here.
	(void)GraphName;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UEdGraphNode* RawNode = AnimGraphFindNodeByGuid(AnimBlueprint, NodeId);
	UAnimGraphNode_ControlRig* Node = Cast<UAnimGraphNode_ControlRig>(RawNode);
	if (!Node)
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetControlRigNodeClass: node '%s' not found or not a Control Rig node in %s"),
			*NodeId, *AnimBlueprintPath);
		return false;
	}

	UClass* LoadedClass = LoadControlRigClass(ControlRigClassPath);
	if (!LoadedClass || !LoadedClass->IsChildOf(UControlRig::StaticClass()))
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetControlRigNodeClass: '%s' is not a valid UControlRig-derived class path"),
			*ControlRigClassPath);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Control Rig Node Class"), AnimBlueprint, Node);
	Scope.RequestCompile();

	Node->Node.SetControlRigClass(TSubclassOf<UControlRig>(LoadedClass));
	Node->ReconstructNode();

	UE_LOG(LogRevvyAnimGraph, Log,
		TEXT("SetControlRigNodeClass: node '%s' -> '%s'"),
		*NodeId, *ControlRigClassPath);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetControlRigNodeAlpha(
	const FString& AnimBlueprintPath,
	const FString& GraphName,
	const FString& NodeId,
	float Alpha)
{
	(void)GraphName;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return false;
	}

	UEdGraphNode* RawNode = AnimGraphFindNodeByGuid(AnimBlueprint, NodeId);
	UAnimGraphNode_ControlRig* Node = Cast<UAnimGraphNode_ControlRig>(RawNode);
	if (!Node)
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetControlRigNodeAlpha: node '%s' not found or not a Control Rig node in %s"),
			*NodeId, *AnimBlueprintPath);
		return false;
	}

	UScriptStruct* InnerStruct = nullptr;
	void* InnerAddr = nullptr;
	if (!ResolveInnerAnimNode(Node, InnerStruct, InnerAddr))
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetControlRigNodeAlpha: could not resolve inner FAnimNode_ControlRig struct"));
		return false;
	}

	FFloatProperty* AlphaProp = FindFProperty<FFloatProperty>(InnerStruct, TEXT("Alpha"));
	if (!AlphaProp)
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("SetControlRigNodeAlpha: 'Alpha' property not found on FAnimNode_ControlRig"));
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Control Rig Node Alpha"), AnimBlueprint, Node);

	const float Clamped = FMath::Clamp(Alpha, 0.0f, 1.0f);
	AlphaProp->SetPropertyValue_InContainer(InnerAddr, Clamped);

	UE_LOG(LogRevvyAnimGraph, Log,
		TEXT("SetControlRigNodeAlpha: node '%s' -> %.3f"), *NodeId, Clamped);
	Scope.MarkSuccess();
	return true;
}

FString UAnimGraphService::GetControlRigNodeInfo(
	const FString& AnimBlueprintPath,
	const FString& GraphName,
	const FString& NodeId)
{
	(void)GraphName;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		return FString();
	}

	UEdGraphNode* RawNode = AnimGraphFindNodeByGuid(AnimBlueprint, NodeId);
	UAnimGraphNode_ControlRig* Node = Cast<UAnimGraphNode_ControlRig>(RawNode);
	if (!Node)
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("GetControlRigNodeInfo: node '%s' not found or not a Control Rig node in %s"),
			*NodeId, *AnimBlueprintPath);
		return FString();
	}

	UScriptStruct* InnerStruct = nullptr;
	void* InnerAddr = nullptr;
	if (!ResolveInnerAnimNode(Node, InnerStruct, InnerAddr))
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("GetControlRigNodeInfo: could not resolve inner FAnimNode_ControlRig struct"));
		return FString();
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	// Class path via the public accessor — no reflection required.
	const FControlRigAssetStrongReference AssetRef = Node->Node.GetControlRigAssetReference();
	if (UClass* BPClass = AssetRef.GetBlueprintClass())
	{
		Root->SetStringField(TEXT("control_rig_class"), BPClass->GetPathName());
	}
	else
	{
		Root->SetStringField(TEXT("control_rig_class"), FString());
	}

	// Alpha (private → reflection).
	if (FFloatProperty* AlphaProp = FindFProperty<FFloatProperty>(InnerStruct, TEXT("Alpha")))
	{
		Root->SetNumberField(TEXT("alpha"), AlphaProp->GetPropertyValue_InContainer(InnerAddr));
	}

	// AlphaInputType (private enum → reflection, export as symbol name).
	if (FEnumProperty* EnumProp = FindFProperty<FEnumProperty>(InnerStruct, TEXT("AlphaInputType")))
	{
		void* EnumAddr = EnumProp->ContainerPtrToValuePtr<void>(InnerAddr);
		const int64 Value = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumAddr);
		if (UEnum* EnumDef = EnumProp->GetEnum())
		{
			Root->SetStringField(TEXT("alpha_input_type"), EnumDef->GetNameStringByValue(Value));
		}
		else
		{
			Root->SetStringField(TEXT("alpha_input_type"), FString::FromInt(static_cast<int32>(Value)));
		}
	}

	// InputMapping / OutputMapping — both are TMap<FName, FName>, private → reflection.
	auto SerializeMapping = [InnerStruct, InnerAddr](const TCHAR* PropName) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		FMapProperty* MapProp = FindFProperty<FMapProperty>(InnerStruct, PropName);
		if (!MapProp) return Result;
		void* MapAddr = MapProp->ContainerPtrToValuePtr<void>(InnerAddr);
		FScriptMapHelper Helper(MapProp, MapAddr);
		FNameProperty* KeyProp = CastField<FNameProperty>(MapProp->KeyProp);
		FNameProperty* ValProp = CastField<FNameProperty>(MapProp->ValueProp);
		if (!KeyProp || !ValProp) return Result;
		for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
		{
			if (!Helper.IsValidIndex(i)) continue;
			const FName Key = KeyProp->GetPropertyValue(Helper.GetKeyPtr(i));
			const FName Val = ValProp->GetPropertyValue(Helper.GetValuePtr(i));
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("rig"),    Key.ToString());
			Entry->SetStringField(TEXT("source"), Val.ToString());
			Result.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Result;
	};

	Root->SetArrayField(TEXT("input_variables"),  SerializeMapping(TEXT("InputMapping")));
	Root->SetArrayField(TEXT("output_variables"), SerializeMapping(TEXT("OutputMapping")));

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

// =============================================================================
// P0 Revvy upgrade — LinkedAnimGraph (non-interface) authoring
//
// UAnimGraphNode_LinkedAnimGraph is the Tag+InstanceClass base of
// UAnimGraphNode_LinkedAnimLayer. Layer support is already covered by
// AddLinkedAnimLayer (which fills InterfaceGuid + FunctionReference); these
// helpers cover the simpler variant that just binds a UAnimInstance-derived
// class and, optionally, a runtime lookup tag.
//
// Class loading mirrors AddControlRigNode: LoadObject<UClass> with a "_C"
// suffix fallback so callers can pass either a Blueprint asset path
// ("/Game/ABP_Foo") or a full class path ("/Game/ABP_Foo.ABP_Foo_C").
// =============================================================================

namespace
{
	/**
	 * Resolve a UAnimInstance-derived UClass from either a full class path
	 * or a UBlueprint asset path (retried with "_C" suffix).
	 */
	static UClass* LoadAnimInstanceClass(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty()) return nullptr;
		UClass* LoadedClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!LoadedClass && !ClassPath.EndsWith(TEXT("_C")))
		{
			const FString Retry = ClassPath + TEXT("_C");
			LoadedClass = LoadObject<UClass>(nullptr, *Retry);
		}
		return LoadedClass;
	}
}

FString UAnimGraphService::AddLinkedAnimGraphNode(
	const FString& AnimBlueprintPath,
	const FString& GraphName,
	const FString& InstanceClassPath,
	const FString& Tag,
	float PosX,
	float PosY)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("AddLinkedAnimGraphNode: failed to load %s"), *AnimBlueprintPath);
		return FString();
	}

	UEdGraph* TargetGraph = FindAnimGraph(AnimBlueprint, GraphName);
	if (!TargetGraph)
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("AddLinkedAnimGraphNode: graph '%s' not found in %s"),
			*GraphName, *AnimBlueprintPath);
		return FString();
	}

	// Validate class up-front so a malformed path does not leave a half-created node behind.
	UClass* LoadedClass = nullptr;
	if (!InstanceClassPath.IsEmpty())
	{
		LoadedClass = LoadAnimInstanceClass(InstanceClassPath);
		if (!LoadedClass)
		{
			UE_LOG(LogRevvyAnimGraph, Warning,
				TEXT("AddLinkedAnimGraphNode: could not load class '%s'"),
				*InstanceClassPath);
			return FString();
		}
		if (!LoadedClass->IsChildOf(UAnimInstance::StaticClass()))
		{
			UE_LOG(LogRevvyAnimGraph, Warning,
				TEXT("AddLinkedAnimGraphNode: '%s' is not a UAnimInstance-derived class"),
				*InstanceClassPath);
			return FString();
		}
	}

	FAnimBPMutationScope Scope(TEXT("Add Linked Anim Graph Node"), AnimBlueprint);
	Scope.RequestCompile();

	FGraphNodeCreator<UAnimGraphNode_LinkedAnimGraph> NodeCreator(*TargetGraph);
	UAnimGraphNode_LinkedAnimGraph* NewNode = NodeCreator.CreateNode();
	if (!NewNode)
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("AddLinkedAnimGraphNode: failed to create node in '%s'"), *GraphName);
		return FString();
	}

	NewNode->NodePosX = static_cast<int32>(PosX);
	NewNode->NodePosY = static_cast<int32>(PosY);

	if (LoadedClass)
	{
		NewNode->Node.InstanceClass = LoadedClass;
	}
	if (!Tag.IsEmpty())
	{
		// 5.8: the linked-node Tag moved off FAnimNode_LinkedAnimGraph (now
		// Tag_DEPRECATED) onto UAnimGraphNode_Base; set via the public SetTag().
		NewNode->SetTag(FName(*Tag));
	}

	NodeCreator.Finalize();

	UE_LOG(LogRevvyAnimGraph, Log,
		TEXT("AddLinkedAnimGraphNode: created in '%s' class='%s' tag='%s'"),
		*GraphName, *InstanceClassPath, *Tag);
	Scope.MarkSuccess();
	return NewNode->NodeGuid.ToString();
}

bool UAnimGraphService::SetLinkedAnimGraphInstance(
	const FString& AnimBlueprintPath,
	const FString& GraphName,
	const FString& NodeId,
	const FString& InstanceClassPath)
{
	// GraphName is accepted for API symmetry; NodeGuid is globally unique across
	// the blueprint so the search is scoped at the whole-ABP level here.
	(void)GraphName;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("SetLinkedAnimGraphInstance: failed to load %s"), *AnimBlueprintPath);
		return false;
	}

	UEdGraphNode* RawNode = AnimGraphFindNodeByGuid(AnimBlueprint, NodeId);
	UAnimGraphNode_LinkedAnimGraph* Node = Cast<UAnimGraphNode_LinkedAnimGraph>(RawNode);
	if (!Node)
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("SetLinkedAnimGraphInstance: node '%s' not found or not a LinkedAnimGraph node in %s"),
			*NodeId, *AnimBlueprintPath);
		return false;
	}

	UClass* LoadedClass = LoadAnimInstanceClass(InstanceClassPath);
	if (!LoadedClass || !LoadedClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("SetLinkedAnimGraphInstance: '%s' is not a valid UAnimInstance-derived class path"),
			*InstanceClassPath);
		return false;
	}

	FAnimBPMutationScope Scope(TEXT("Set Linked Anim Graph Instance"), AnimBlueprint, Node);
	Scope.RequestCompile();

	Node->Node.InstanceClass = LoadedClass;
	Node->ReconstructNode();

	UE_LOG(LogRevvyAnimGraph, Log,
		TEXT("SetLinkedAnimGraphInstance: node '%s' -> '%s'"),
		*NodeId, *InstanceClassPath);
	Scope.MarkSuccess();
	return true;
}

bool UAnimGraphService::SetLinkedAnimGraphTag(
	const FString& AnimBlueprintPath,
	const FString& GraphName,
	const FString& NodeId,
	const FString& Tag)
{
	(void)GraphName;

	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("SetLinkedAnimGraphTag: failed to load %s"), *AnimBlueprintPath);
		return false;
	}

	UEdGraphNode* RawNode = AnimGraphFindNodeByGuid(AnimBlueprint, NodeId);
	UAnimGraphNode_LinkedAnimGraph* Node = Cast<UAnimGraphNode_LinkedAnimGraph>(RawNode);
	if (!Node)
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("SetLinkedAnimGraphTag: node '%s' not found or not a LinkedAnimGraph node in %s"),
			*NodeId, *AnimBlueprintPath);
		return false;
	}

	// Tag is a runtime-only lookup key — no RequestCompile.
	FAnimBPMutationScope Scope(TEXT("Set Linked Anim Graph Tag"), AnimBlueprint, Node);

	// 5.8: Tag is an editor-node property on UAnimGraphNode_Base, not the runtime
	// FAnimNode_LinkedAnimGraph. Use the public SetTag() accessor.
	Node->SetTag(Tag.IsEmpty() ? NAME_None : FName(*Tag));

	UE_LOG(LogRevvyAnimGraph, Log,
		TEXT("SetLinkedAnimGraphTag: node '%s' -> '%s'"),
		*NodeId, *Tag);
	Scope.MarkSuccess();
	return true;
}

// =============================================================================
// P0 Revvy upgrade — AnimBlueprint structural ops (reparent + introspection)
//
// Reparent is a structural class change (not a graph mutation) so it uses a
// direct FScopedTransaction instead of FAnimBPMutationScope. We guard the
// operation on IsChildOf(UAnimInstance::StaticClass()) so callers cannot
// point an AnimBlueprint at a non-AnimInstance parent (which would silently
// corrupt the ABP).
// =============================================================================

bool UAnimGraphService::ReparentAnimBlueprint(
	const FString& AnimBlueprintPath,
	const FString& NewParentClassPath)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("ReparentAnimBlueprint: failed to load %s"), *AnimBlueprintPath);
		return false;
	}

	UClass* NewParentClass = LoadAnimInstanceClass(NewParentClassPath);
	if (!NewParentClass)
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("ReparentAnimBlueprint: could not load class '%s'"),
			*NewParentClassPath);
		return false;
	}
	if (!NewParentClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		UE_LOG(LogRevvyAnimGraph, Error,
			TEXT("ReparentAnimBlueprint: '%s' is not a UAnimInstance-derived class — refusing to reparent %s"),
			*NewParentClassPath, *AnimBlueprintPath);
		return false;
	}

	// Direct FScopedTransaction — reparent is a structural class change, not a
	// graph mutation, so FAnimBPMutationScope's structural-modified marker is
	// the wrong dtor hook. ReparentBlueprint + CompileBlueprint below handle
	// dirty-flagging and CDO regeneration themselves.
	FScopedTransaction Tx(FText::FromString(TEXT("Reparent Anim Blueprint")));
	AnimBlueprint->Modify();

	// 5.8: ReparentBlueprint is exposed on UBlueprintEditorLibrary, not
	// FBlueprintEditorUtils. Same (UBlueprint*, UClass*) signature.
	UBlueprintEditorLibrary::ReparentBlueprint(AnimBlueprint, NewParentClass);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);

	UE_LOG(LogRevvyAnimGraph, Log,
		TEXT("ReparentAnimBlueprint: %s -> parent '%s'"),
		*AnimBlueprintPath, *NewParentClassPath);
	return true;
}

FString UAnimGraphService::GetAnimBPReparentInfo(const FString& AnimBlueprintPath)
{
	UAnimBlueprint* AnimBlueprint = LoadAnimBlueprint(AnimBlueprintPath);
	if (!AnimBlueprint)
	{
		UE_LOG(LogRevvyAnimGraph, Warning,
			TEXT("GetAnimBPReparentInfo: failed to load %s"), *AnimBlueprintPath);
		return FString();
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	UClass* ParentClass = AnimBlueprint->ParentClass;
	Root->SetStringField(TEXT("current_parent_class"),
		ParentClass ? ParentClass->GetPathName() : FString());
	Root->SetBoolField(TEXT("is_child_of_anim_instance"),
		ParentClass != nullptr && ParentClass->IsChildOf(UAnimInstance::StaticClass()));

	Root->SetNumberField(TEXT("variable_count"), AnimBlueprint->NewVariables.Num());

	const int32 GraphCount = AnimBlueprint->UbergraphPages.Num() + AnimBlueprint->FunctionGraphs.Num();
	Root->SetNumberField(TEXT("graph_count"), GraphCount);

	// State machine count — scope to the main AnimGraph as documented on the header.
	int32 StateMachineCount = 0;
	UEdGraph* MainAnimGraph = nullptr;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
		{
			MainAnimGraph = Graph;
			break;
		}
	}
	if (MainAnimGraph)
	{
		for (UEdGraphNode* N : MainAnimGraph->Nodes)
		{
			if (Cast<UAnimGraphNode_StateMachine>(N))
			{
				++StateMachineCount;
			}
		}
	}
	Root->SetNumberField(TEXT("state_machine_count"), StateMachineCount);

	// Warnings — surface the standard reparent gotchas so the caller can decide
	// whether to proceed without needing to know Blueprint internals.
	TArray<TSharedPtr<FJsonValue>> Warnings;
	Warnings.Add(MakeShared<FJsonValueString>(FString(
		TEXT("reparenting will preserve variables but may unhook inherited event handlers"))));
	if (AnimBlueprint->NewVariables.Num() > 0)
	{
		Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
			TEXT("%d local variables will migrate; conflicts with new parent's variables must be resolved manually"),
			AnimBlueprint->NewVariables.Num())));
	}
	if (StateMachineCount > 0)
	{
		Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
			TEXT("%d state machines in the main AnimGraph — review parent class linkage after reparent"),
			StateMachineCount)));
	}
	if (ParentClass != nullptr && !ParentClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		Warnings.Add(MakeShared<FJsonValueString>(FString(
			TEXT("current parent is not a UAnimInstance descendant — AnimBP is in an unusual state"))));
	}
	Root->SetArrayField(TEXT("warnings"), Warnings);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
