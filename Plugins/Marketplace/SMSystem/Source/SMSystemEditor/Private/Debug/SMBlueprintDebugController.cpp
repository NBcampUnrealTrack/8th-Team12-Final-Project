// Copyright Recursoft LLC. All Rights Reserved.

#include "Debug/SMBlueprintDebugController.h"

#include "Graph/Nodes/FunctionNodes/SMGraphK2Node_FunctionNodes_TransitionInstance.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_GraphPropertyNode.h"
#include "Graph/Nodes/RootNodes/SMGraphK2Node_DebugEntryNode.h"
#include "Graph/Nodes/RootNodes/SMGraphK2Node_StateEntryNode.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/SMGraph.h"
#include "Graph/SMStateGraph.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Blueprints/SMBlueprint.h"
#include "Blueprints/SMBlueprintDebugEditorBridge.h"
#include "SMInstance.h"

#include "Blueprint/BlueprintExceptionInfo.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/Breakpoint.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "LevelEditor.h"
#include "Misc/ScopeExit.h"
#include "UnrealEdGlobals.h"

TStrongObjectPtr<USMBlueprintDebugController> USMBlueprintDebugController::BlueprintDebugController;

USMBlueprintDebugController* USMBlueprintDebugController::GetInstance()
{
	if (!BlueprintDebugController.IsValid())
	{
		BlueprintDebugController = TStrongObjectPtr<USMBlueprintDebugController>(NewObject<USMBlueprintDebugController>());
	}
	return BlueprintDebugController.Get();
}

void USMBlueprintDebugController::Shutdown()
{
	BlueprintDebugController.Reset();
}

bool USMBlueprintDebugController::NotifyNewCommandStarting(LD::Editor::Debug::EDebugCommandType InCommandType)
{
	CleanupTemporaryBreakpoints();
	
	if (InCommandType != LD::Editor::Debug::EDebugCommandType::Default && IsLogicDriverReadyToDebug(InCommandType))
	{
		CurrentDebugCommandType = InCommandType;
		
		bCommandJustExecuted = true;
		RecordBreakpointStateNode(FindClosestStateNodeInStack());
		
		MostRecentNodeOnCommandStart = FKismetDebugUtilities::GetCurrentInstruction();
	
		if (InCommandType == LD::Editor::Debug::EDebugCommandType::StepOut)
		{
			ON_SCOPE_EXIT
			{
				bCommandJustExecuted = false;
			};
			
			const EResult ShouldUEHandleCommand = ShouldUEHandleStepOut();
			if (ShouldUEHandleCommand == EResult::Yes)
			{
				return true;
			}
			RequestStepOut();
			ResumeSession();
			return ShouldUEHandleCommand == EResult::Unknown;
		}
		else if (InCommandType == LD::Editor::Debug::EDebugCommandType::StepOver)
		{
			const EResult ShouldUEHandleCommand = ShouldUEHandleStepOver();
			if (ShouldUEHandleCommand == EResult::Yes)
			{
				bCommandJustExecuted = false;
				return true;
			}
			
			// If stepping over on our state, if there's nowhere to step over, step out instead.
			if (const USMGraphNode_StateNodeBase* ThisState = FindClosestStateNodeInStack())
			{
				// Do we want to handle more cases of stepping out instead? This only works for end states.
				if (ThisState->IsEndState())
				{
					RequestStepOut();
				}
				else
				{
					RequestStepOver();
				}
				
				bCommandJustExecuted = false;
				
				ResumeSession();
				return ShouldUEHandleCommand == EResult::Unknown;
			}
		}
	}
	
	return true;
}

void USMBlueprintDebugController::ResetCommand()
{
	CurrentDebugCommandType = LD::Editor::Debug::EDebugCommandType::Default;
}

void USMBlueprintDebugController::CancelAndCleanup()
{
	CleanupTemporaryBreakpoints();
	RecordBreakpointStateNode(nullptr);
	ResetCommand();
	MostRecentNodeOnCommandStart.Reset();
}

bool USMBlueprintDebugController::IsLogicDriverReadyToDebug(LD::Editor::Debug::EDebugCommandType InCommandType) const
{
	// If we're at 0, then we aren't executing any of our blueprint execution points.
	if (GetDebugStackDepth() > 0)
	{
		// Stepping Into - We don't want to handle anything if it's not one of our state nodes already.
		if (InCommandType == LD::Editor::Debug::EDebugCommandType::StepInto)
		{
			return FindClosestStateNodeInStack(true) != nullptr;
		}
		
		// Others - We must be in the stack to consider attempting to handle.
		return IsLogicDriverInStack();
	}
	
	return false;
}

bool USMBlueprintDebugController::IsLogicDriverInStack()
{
	const TArrayView<const FFrame* const> ScriptStack = FBlueprintContextTracker::Get().GetCurrentScriptStack();
	for (int32 Idx = ScriptStack.Num() - 1; Idx >= 0; --Idx)
	{
		const FFrame* StackFrame = ScriptStack[Idx];
		if (StackFrame->Object && StackFrame->Object->IsA<USMInstance>())
		{
			return true;
		}
	}
	
	return false;
}

bool USMBlueprintDebugController::IsBreakpointTemporary(const FBlueprintBreakpoint& InBreakpoint)
{
	return InBreakpoint.IsEnabled() && !InBreakpoint.IsEnabledByUser();
}

void USMBlueprintDebugController::CleanupTemporaryBreakpoints(const USMBlueprint* Blueprint)
{
	check(Blueprint);
	FKismetDebugUtilities::RemoveBreakpointsByPredicate(Blueprint, [](const FBlueprintBreakpoint& Breakpoint)
	{
		// Don't call any of the GraphNode Breakpoint methods, as that can modify the size of the bp array and throw
		// an exception during this predicate evaluation.
		return Breakpoint.GetLocation() 
			&& Breakpoint.GetLocation()->IsA<USMGraphNode_Base>()
			&& IsBreakpointTemporary(Breakpoint);
	});
}

void USMBlueprintDebugController::CleanupTemporaryBreakpoints(const USMInstance* Instance)
{
	check(Instance);
	if (const USMBlueprint* Blueprint = Cast<USMBlueprint>(UBlueprint::GetBlueprintFromClass(Instance->GetClass())))
	{
		CleanupTemporaryBreakpoints(Blueprint);
	}
}

void USMBlueprintDebugController::ResumeSession()
{
	// See DebuggerCommands.cpp static void LeaveDebuggingMode()
	
	GUnrealEd->SetPIEWorldsPaused(false);

	// Determine whether or not we are resuming play.
	const bool bIsResumingPlay = !FKismetDebugUtilities::IsSingleStepping() && !GEditor->ShouldEndPlayMap();

	if (FSlateApplication::Get().InKismetDebuggingMode() && bIsResumingPlay)
	{
		// Focus the game view port when resuming from debugging.
		FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor").FocusPIEViewport();
	}

	// Tell the application to stop ticking in this stack frame. The parameter controls whether or not to recapture the mouse to the game viewport.
	FSlateApplication::Get().LeaveDebuggingMode(!bIsResumingPlay);
}

USMBlueprintDebugController::USMBlueprintDebugController()
{
	if (!IsTemplate())
	{
		FSMBlueprintDebugEditorBridge::Get().OnDebugScopeStartEvent.BindUObject(this, &USMBlueprintDebugController::OnDebugScopeStart);
		FSMBlueprintDebugEditorBridge::Get().OnDebugScopeEndEvent.BindUObject(this, &USMBlueprintDebugController::OnDebugScopeEnd);
	}
}

USMBlueprintDebugController::~USMBlueprintDebugController()
{
	if (!IsTemplate())
	{
		FSMBlueprintDebugEditorBridge::Get().OnDebugScopeStartEvent.Unbind();
		FSMBlueprintDebugEditorBridge::Get().OnDebugScopeEndEvent.Unbind();
		UnbindKismetDebugDelegates();
		CancelAndCleanup();
	}
}

void USMBlueprintDebugController::RequestStepInto(const FBlueprintContextTracker& BlueprintContext,
	const UObject* ContextObject, const UFunction* ContextFunction)
{
	if (const USMGraphNode_StateNodeBase* StateNode = GetBestStateNodeFromFunction(ContextObject, ContextFunction))
	{
		RecordBreakpointStateNode(StateNode);
	}
	
	FKismetDebugUtilities::RequestSingleStepIn();
}

void USMBlueprintDebugController::RequestStepOver()
{
	if (const USMGraphNode_StateNodeBase* StateNode = GetMostRecentStateNode())
	{
		ApplyTemporaryBreakpointsToOutputStates(StateNode);
	}
}

void USMBlueprintDebugController::RequestStepOut()
{
	const USMGraphNode_StateNodeBase* StateNode = GetMostRecentStateNode();
	if (StateNode && MostRecentNodeOnCommandStart.IsValid())
	{
		if (StateNode->GetGraph() == MostRecentNodeOnCommandStart.Get()->GetGraph())
		{
			StateNode = Cast<USMGraphNode_StateNodeBase>(FSMBlueprintEditorUtils::FindTopLevelOwningNode(StateNode->GetGraph()));
		}
		
		if (StateNode)
		{
			RecordBreakpointStateNode(StateNode);
			ApplyTemporaryBreakpointsToOutputStates(StateNode);
		}
	}
}

void USMBlueprintDebugController::BindKismetDebugDelegates()
{
	OnScriptExecutionDelegateHandle = FBlueprintContextTracker::OnEnterScriptContext.AddUObject(this, &USMBlueprintDebugController::OnEnterScriptExecution);
	OnScriptExceptionHandle = FBlueprintCoreDelegates::OnScriptException.AddUObject(this, &USMBlueprintDebugController::OnScriptException);
}

void USMBlueprintDebugController::UnbindKismetDebugDelegates()
{
	FBlueprintContextTracker::OnEnterScriptContext.Remove(OnScriptExecutionDelegateHandle);
	FBlueprintCoreDelegates::OnScriptException.Remove(OnScriptExceptionHandle);
}

void USMBlueprintDebugController::OnEnterScriptExecution(const FBlueprintContextTracker& BlueprintContext,
                                            const UObject* ContextObject, const UFunction* ContextFunction)
{
	ON_SCOPE_EXIT
	{
		bCommandJustExecuted = false;
	};
	
	if (!ContextObject || !ContextObject->IsA<USMInstance>())
	{
		return;
	}
	
	const bool bIsOurSMGraph = ContextFunction
				&& ContextFunction->GetName().StartsWith(USMGraphK2Node_DebugEntryNode::StaticClass()->GetName());
	const bool bIsOurLocalEntry = ContextFunction
				&& ContextFunction->GetName().StartsWith(USMGraphK2Node_StateEntryNode::StaticClass()->GetName());
	const bool bIsOurNode = bIsOurSMGraph || bIsOurLocalEntry;
	
	if (FKismetDebugUtilities::IsSingleStepping() || bCommandJustExecuted)
	{
		if (bCommandJustExecuted && bIsOurNode)
		{
			if (CurrentDebugCommandType == LD::Editor::Debug::EDebugCommandType::StepInto)
			{
				RequestStepInto(BlueprintContext, ContextObject, ContextFunction);
			}
			// bIsSteppingOut handled on new command start.
		}
	}
	else if (bIsOurSMGraph && ExpectedGraphOnNextTemporaryBreakpoint.IsValid())
	{
		// We might be entering a state that should invalidate other temporary breakpoints. In the event we are doing
		// a step over, or a step out, that can occur in the future. If another state is entered we must determine if
		// it should invalidate other temporary breakpoints.
		if (const USMGraphNode_StateNodeBase* StateNode = GetBestStateNodeFromFunction(
			ContextObject, ContextFunction, BlueprintContext.GetScriptEntryTag()))
		{
			if (StateNode->GetBreakpoint() == nullptr)
			{
				const USMBlueprint* ThisBlueprint = Cast<USMBlueprint>(FSMBlueprintEditorUtils::FindBlueprintForNodeChecked(StateNode));
				const USMBlueprint* ExpectedBlueprint = Cast<USMBlueprint>(FSMBlueprintEditorUtils::FindBlueprintForGraphChecked(ExpectedGraphOnNextTemporaryBreakpoint.Get()));
			
				// Keep comparisons to the same blueprint.
				if (ExpectedBlueprint == ThisBlueprint)
				{
					const USMGraph* ThisGraph = StateNode->GetOwningStateMachineGraph();
					if (ThisGraph == ExpectedGraphOnNextTemporaryBreakpoint.Get())
					{
						// We're on the expected graph, but this state has no breakpoint which means we've entered
						// a state that wasn't connected to the state we skipped over to.
						CleanupTemporaryBreakpoints(ThisBlueprint);
					}
					else
					{
						// We're on an unexpected graph, this could be okay if we're doing a step out and this graph
						// is a child graph.
						bool bIsChildGraph = false;
						const UObject* CurrentOuter = ThisGraph;
						while (CurrentOuter)
						{
							CurrentOuter = CurrentOuter->GetOuter();
							if (CurrentOuter == ExpectedGraphOnNextTemporaryBreakpoint.Get())
							{
								bIsChildGraph = true;
								break;
							}
						}

						if (!bIsChildGraph)
						{
							// We're on an unrelated graph, cleanup temporary breakpoints
							CleanupTemporaryBreakpoints(ThisBlueprint);
						}
					}
				}
			}
		}
	}
}

void USMBlueprintDebugController::OnScriptException(const UObject* ActiveObject, const FFrame& StackFrame,
                                            const FBlueprintExceptionInfo& Info)
{
	if (Info.GetType() == EBlueprintExceptionType::Type::Breakpoint)
	{
		if (!ActiveObject || !ActiveObject->IsA<USMInstance>() 
			|| GetBestStateNodeFromFunction(StackFrame.Object, StackFrame.Node,
				GetOpcodeOffsetFromStackFrame(StackFrame)) == nullptr)
		{
			CancelAndCleanup();
			return;
		}
		
		if (const USMGraphNode_StateNodeBase* StateNode = GetBestStateNodeFromFunction(StackFrame.Object, StackFrame.Node))
		{
			RecordBreakpointStateNode(StateNode);
		}
	}
	
	// If we just haven't executed a command, and we aren't in a breakpoint, reset our commands.
	if (Info.GetType() != EBlueprintExceptionType::Type::Tracepoint
		&& Info.GetType() != EBlueprintExceptionType::Type::WireTracepoint
		&& !bCommandJustExecuted)
	{
		ResetCommand();
	}
}

void USMBlueprintDebugController::OnDebugScopeStart()
{
	if (++StateMachineDebugStackDepth == 1)
	{
		BindKismetDebugDelegates();
	}
}

void USMBlueprintDebugController::OnDebugScopeEnd()
{
	check(StateMachineDebugStackDepth > 0);
	if (--StateMachineDebugStackDepth == 0)
	{
		UnbindKismetDebugDelegates();
	}
}

void USMBlueprintDebugController::TrackBlueprint(USMBlueprint* InBlueprint)
{
	DebuggedBlueprints.Add(InBlueprint);
}

void USMBlueprintDebugController::CleanupTemporaryBreakpoints()
{
	for (const TWeakObjectPtr<USMBlueprint>& Blueprint : DebuggedBlueprints)
	{
		if (Blueprint.IsValid())
		{
			CleanupTemporaryBreakpoints(Blueprint.Get());
		}
	}
	
	DebuggedBlueprints.Empty();
	ExpectedGraphOnNextTemporaryBreakpoint.Reset();
}

void USMBlueprintDebugController::RecordBreakpointStateNode(const USMGraphNode_StateNodeBase* StateNode)
{
	MostRecentRecordedStateNode = StateNode;
}

void USMBlueprintDebugController::ApplyTemporaryBreakpointsToOutputStates(const USMGraphNode_StateNodeBase* StateNode)
{
	check(StateNode);
	TArray<USMGraphNode_StateNodeBase*> OutputStates;
	USMGraphNode_StateNodeBase::FOutputStateArgs OutputStateArgs;
	OutputStateArgs.bForwardLinkState = true;
	OutputStateArgs.bCheckAnyStates = true;
	StateNode->GetOutputStates(OutputStates, OutputStateArgs);
			
	bool bAppliedBreakpoints = false;
	
	for (USMGraphNode_StateNodeBase* OutputState : OutputStates)
	{
		if (ApplyTemporaryBreakpointToState(OutputState))
		{
			bAppliedBreakpoints = true;
		}
	}
	
	if (bAppliedBreakpoints)
	{
		USMBlueprint* Blueprint = CastChecked<USMBlueprint>(FSMBlueprintEditorUtils::FindBlueprintForNodeChecked(StateNode));
		TrackBlueprint(Blueprint);
	}
	
	ExpectedGraphOnNextTemporaryBreakpoint = StateNode->GetOwningStateMachineGraph();
}

bool USMBlueprintDebugController::ApplyTemporaryBreakpointToState(USMGraphNode_StateNodeBase* StateNode)
{
	if (StateNode->CanPlaceBreakpoints() && !StateNode->GetBreakpoint())
	{
		// We can't rely on the UE debugger to remove the breakpoint since we're not dealing with
		// linear progression of states in the same tick. CreateBreakpointForSingleStep will make sure
		// our breakpoint gets removed later if applicable, but we have to also check for this type of 
		// breakpoint and clean it manually. 
		StateNode->CreateBreakpointForSingleStep();
		FBlueprintBreakpoint* Breakpoint = StateNode->GetBreakpoint();
		FKismetDebugUtilities::SetBreakpointEnabledForSingleStep(*Breakpoint, true);
		return true;
	}
	
	return false;
}

const USMGraphNode_StateNodeBase* USMBlueprintDebugController::GetBestStateNodeFromFunction(const UObject* InContextObject,
                                                                                            const UFunction* InContextFunction, const int32 OpcodeOffset) const
{
	if (const UEdGraphNode* StoppedNode = FKismetDebugUtilities::FindSourceNodeForCodeLocation(InContextObject,
	const_cast<UFunction*>(InContextFunction), OpcodeOffset, true))
	{
		if (StoppedNode->IsA<USMGraphK2Node_DebugEntryNode>()
			|| StoppedNode->IsA<USMGraphK2Node_StateEntryNode>()
			|| StoppedNode->IsA<USMGraphNode_StateNodeBase>()
			// We don't want to continue breakpointing on these, and almost certainly should handle this as a step out.
			|| StoppedNode->IsA<USMGraphK2Node_PropertyNode_Base>())
		{
			// Depending on the node mapping we could already be at a state level node.
			const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(StoppedNode);
			
			// Often we are at the entry or a specific debug node and need to find the state node. This can especially
			// occur when stepping into a state.
			if (!StateNode
				 && (StoppedNode->IsA<USMGraphK2Node_DebugEntryNode>() || StoppedNode->IsA<USMGraphK2Node_StateEntryNode>()
				 	|| StoppedNode->IsA<USMGraphK2Node_PropertyNode_Base>()))
			{
				const UObject* CurrentObject = StoppedNode;
				while (CurrentObject)
				{
					if (const USMGraphNode_StateNodeBase* CandidateStateNode = Cast<USMGraphNode_StateNodeBase>(CurrentObject))
					{
						StateNode = CandidateStateNode;
						break;
					}
	
					// The outer could be either a graph or a node, so we need to handle both cases
					const UObject* Outer = CurrentObject->GetOuter();
					if (const UEdGraph* GraphOuter = Cast<UEdGraph>(Outer))
					{
						// If the outer is a graph, check its outer for a node
						CurrentObject = GraphOuter->GetOuter();
					}
					else
					{
						// Otherwise try to cast directly to a node
						CurrentObject = Cast<UEdGraphNode>(Outer);
					}
				}
			}
			
			return StateNode;
		}
	}
	
	return nullptr;
}

const USMGraphNode_StateNodeBase* USMBlueprintDebugController::GetMostRecentStateNode() const
{
	if (const USMGraphNode_StateNodeBase* RecentNode = FindClosestStateNodeInStack(true))
	{
		return RecentNode;
	}
	
	return MostRecentRecordedStateNode.Get();
}

const USMGraphNode_StateNodeBase* USMBlueprintDebugController::FindClosestStateNodeInStack(bool bRequireCurrentDepth) const
{
	const TArrayView<const FFrame* const> ScriptStack = FBlueprintContextTracker::Get().GetCurrentScriptStack();
	for (int32 Idx = ScriptStack.Num() - 1; Idx >= 0; --Idx)
	{
		const FFrame* StackFrame = ScriptStack[Idx];
		if (StackFrame->Object && StackFrame->Object->IsA<USMInstance>())
		{
			const int32 BreakpointOffset = GetOpcodeOffsetFromStackFrame(*StackFrame);
			if (const USMGraphNode_StateNodeBase* StateNode = GetBestStateNodeFromFunction(StackFrame->Object, StackFrame->Node, BreakpointOffset))
			{
				return StateNode;
			}
		}
		if (bRequireCurrentDepth)
		{
			break;
		}
	}
	
	return nullptr;
}

USMBlueprintDebugController::EResult USMBlueprintDebugController::ShouldUEHandleStepOver() const
{
	// Iterate the stack recent to last. We might be at the end of a nested function and *think* we need to handle it,
	// but the calling function has more k2 nodes for UE to handle instead.
	const TArrayView<const FFrame* const> ScriptStack = FBlueprintContextTracker::Get().GetCurrentScriptStack();
	const int32 StackIdxStart = ScriptStack.Num() - 1;
	for (int32 Idx = StackIdxStart; Idx >= 0; --Idx)
	{
		const FFrame* StackFrame = ScriptStack[Idx];
		const int32 BreakpointOffset = GetOpcodeOffsetFromStackFrame(*StackFrame);
		if (const UEdGraphNode* StoppedNode = FKismetDebugUtilities::FindSourceNodeForCodeLocation(StackFrame->Object,
		StackFrame->Node, BreakpointOffset, true))
		{
			if (StoppedNode->IsA<USMGraphNode_Base>() || (Idx < StackIdxStart && StoppedNode->IsA<USMGraphK2Node_StateEntryNode>()))
			{
				// If a previous stack entry point was the local graph entry, then we know there's no more local execution,
				// and we should be handling this.
					
				return EResult::No;
			}
			const EResult Result = ShouldUEHandleStepOver(StoppedNode);
			if (StoppedNode->IsA<USMGraphK2Node_PropertyNode_Base>() && Result == EResult::No)
			{
				return Result;
			}
				
			if (Result != EResult::No)
			{
				return Result;
			}
		}
	}
	
	return EResult::No;
}

USMBlueprintDebugController::EResult USMBlueprintDebugController::ShouldUEHandleStepOver(const UEdGraphNode* StoppedNode) const
{
	if (StoppedNode && !StoppedNode->IsA<USMGraphNode_Base>())
	{
		// Tunnel nodes can have valid exec pins, but we need to check the input sink node.
		if (const UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(StoppedNode))
		{
			if (Tunnel->InputSinkNode)
			{
				StoppedNode = Tunnel->InputSinkNode;
			}
		}
		
		const EResult Result = DoesNodeExecToAnotherNode(StoppedNode);
		if (Result != EResult::No)
		{
			return Result;
		}
		
		if (StoppedNode->IsA<USMGraphK2Node_TransitionInstance_OnTransitionInitialized>())
		{
			// Special case since OnStateBegin can end up getting called after OnTransitionInitialized.
			return EResult::Yes;
		}
	}
	return EResult::No;
}

USMBlueprintDebugController::EResult USMBlueprintDebugController::ShouldUEHandleStepOut() const
{
	if (const UEdGraphNode* Instruction = FKismetDebugUtilities::GetCurrentInstruction())
	{
		// Tunnel nodes can have valid exec pins, but we need to check the input sink node.
		if (const UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Instruction))
		{
			if (Tunnel->InputSinkNode)
			{
				if (DoesNodeExecToAnotherNode(Tunnel->InputSinkNode) == EResult::Yes)
				{
					return EResult::Yes;
				}
			}
		}
		
		if (Instruction->IsA<USMGraphNode_Base>()
			|| Instruction->GetGraph()->IsA<USMStateGraph>())
		{
			return EResult::No;
		}
	}
	return EResult::Yes;
}

int32 USMBlueprintDebugController::GetOpcodeOffsetFromStackFrame(const FFrame& StackFrame)
{
	return StackFrame.Code - StackFrame.Node->Script.GetData() - 1;
}

USMBlueprintDebugController::EResult USMBlueprintDebugController::DoesNodeExecToAnotherNode(const UEdGraphNode* Node)
{
	if (Node)
	{
		// Check the correct output pins for actual connections. If it's a valid pin but no connection, then this could
		// be something like an if/then/else, where we don't know which branch to take, and one branch may be empty,
		// and in that case we will need to handle stepping out.
		int32 PossiblePinCount = 0;
		int32 DefinitePinCount = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				PossiblePinCount++;
				for (const UEdGraphPin* LinkedTo : Pin->LinkedTo)
				{
					if (UEdGraphNode* GraphNode = LinkedTo->GetOwningNode())
					{
						DefinitePinCount++;
						break;
					}
				}
			}
		}
		// A positive PossiblePinCount is irrelevant if there isn't a single definite pin count, since that
		// means this would be the end of execution.
		if (DefinitePinCount > 0)
		{
			// Definite == Possible means all branches are accounted for, and we know this node must progress
			// to another k2 node. There may be exceptions to account for such as async nodes.
			return DefinitePinCount == PossiblePinCount ? EResult::Yes : EResult::Unknown;
		}
	}
	
	return EResult::No;
}

