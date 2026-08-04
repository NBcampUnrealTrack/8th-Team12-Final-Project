// Copyright Recursoft LLC. All Rights Reserved.

#include "Helpers/SMTestBoilerplate.h"
#include "Helpers/SMTestHelpers.h"

#include "Debug/SMBlueprintDebugController.h"
#include "Graph/Nodes/SMGraphNode_ConduitNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineEntryNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_TransitionEdge.h"
#include "Graph/SMGraph.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Blueprints/SMBlueprint.h"

#include "Kismet2/Breakpoint.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FDebuggerBreakpointSpec,
                  "LogicDriver.Debugger.Breakpoints",
                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
	SETUP_SPEC_TEST_DEFINES()
END_DEFINE_SPEC(FDebuggerBreakpointSpec)

void FDebuggerBreakpointSpec::Define()
{
	BeforeEach([this]()
	{
		SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(4)
		return true;
	});

	Describe("Create and Remove", [this]()
	{
		It("should create and remove a breakpoint on a state node", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());

			TestTrue("State can place breakpoints", FirstState->CanPlaceBreakpoints());
			TestNull("No breakpoint initially", FirstState->GetBreakpoint());

			FirstState->CreateBreakpoint(true);
			TestNotNull("Breakpoint created", FirstState->GetBreakpoint());
			TestTrue("Breakpoint is enabled", FirstState->GetBreakpoint()->IsEnabled());
			TestTrue("Breakpoint is enabled by user", FirstState->GetBreakpoint()->IsEnabledByUser());

			FirstState->RemoveBreakpoint();
			TestNull("Breakpoint removed", FirstState->GetBreakpoint());
		});

		It("should not create a duplicate breakpoint on the same node", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());

			FirstState->CreateBreakpoint(true);
			const FBlueprintBreakpoint* OriginalBreakpoint = FirstState->GetBreakpoint();

			// Creating again should be a no-op.
			FirstState->CreateBreakpoint(true);
			TestTrue("Same breakpoint instance", FirstState->GetBreakpoint() == OriginalBreakpoint);

			FirstState->RemoveBreakpoint();
			TestNull("Breakpoint removed", FirstState->GetBreakpoint());
		});

		It("should safely handle removing a non-existent breakpoint", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());

			TestNull("No breakpoint exists", FirstState->GetBreakpoint());
			FirstState->RemoveBreakpoint();
			TestNull("Still no breakpoint after remove", FirstState->GetBreakpoint());
		});

		It("should create a disabled breakpoint", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());

			FirstState->CreateBreakpoint(false);
			TestNotNull("Disabled breakpoint created", FirstState->GetBreakpoint());
			TestFalse("Breakpoint is disabled", FirstState->GetBreakpoint()->IsEnabled());

			FirstState->RemoveBreakpoint();
			TestNull("Disabled breakpoint removed", FirstState->GetBreakpoint());
		});
	});

	Describe("Enable and Disable", [this]()
	{
		It("should toggle breakpoint enabled state", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());

			FirstState->CreateBreakpoint(true);
			TestTrue("Starts enabled", FirstState->GetBreakpoint()->IsEnabled());

			FirstState->SetBreakpointEnabled(false);
			TestFalse("Now disabled", FirstState->GetBreakpoint()->IsEnabled());

			FirstState->SetBreakpointEnabled(true);
			TestTrue("Re-enabled", FirstState->GetBreakpoint()->IsEnabled());

			FirstState->RemoveBreakpoint();
			TestNull("Breakpoint removed after toggle", FirstState->GetBreakpoint());
		});
	});

	Describe("CanPlaceBreakpoints", [this]()
	{
		It("should allow breakpoints on state nodes but not entry nodes", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateMachineEntryNode* EntryNode = StateMachineGraph->GetEntryNode();
			TestFalse("Entry node cannot place breakpoints", EntryNode->CanPlaceBreakpoints());

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(EntryNode->GetOutputNode());
			TestTrue("State node can place breakpoints", FirstState->CanPlaceBreakpoints());
		});

		It("should allow breakpoints on transition edges", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());
			USMGraphNode_TransitionEdge* Transition = FirstState->GetNextTransition();
			TestNotNull("Transition exists", Transition);
			TestTrue("Transition can place breakpoints", Transition->CanPlaceBreakpoints());
		});

		It("should allow breakpoints on conduit nodes", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());
			USMGraphNode_StateNode* SecondState = CastChecked<USMGraphNode_StateNode>(FirstState->GetNextState());

			USMGraphNode_ConduitNode* ConduitNode = FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_ConduitNode>(SecondState);
			TestNotNull("Conduit node created", ConduitNode);

			FKismetEditorUtilities::CompileBlueprint(NewBP);
			TestTrue("Conduit can place breakpoints", ConduitNode->CanPlaceBreakpoints());
		});

		It("should allow breakpoints on nested state machine nodes", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());
			USMGraphNode_StateNode* SecondState = CastChecked<USMGraphNode_StateNode>(FirstState->GetNextState());

			TSet<UObject*> SelectedNodes;
			SelectedNodes.Add(SecondState);
			FSMBlueprintEditorUtils::CollapseNodesAndCreateStateMachine(SelectedNodes);

			USMGraphNode_StateMachineStateNode* NestedSMNode = Cast<USMGraphNode_StateMachineStateNode>(FirstState->GetNextState());
			TestNotNull("Nested SM node created", NestedSMNode);

			FKismetEditorUtilities::CompileBlueprint(NewBP);
			TestTrue("Nested SM can place breakpoints", NestedSMNode->CanPlaceBreakpoints());
		});
	});

	Describe("Temporary breakpoints", [this]()
	{
		It("should create a temporary breakpoint for single step", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());

			FirstState->CreateBreakpointForSingleStep();

			FBlueprintBreakpoint* Breakpoint = FirstState->GetBreakpoint();
			TestNotNull("Single step breakpoint created", Breakpoint);
			TestFalse("Starts disabled", Breakpoint->IsEnabled());
			TestFalse("Not enabled by user", Breakpoint->IsEnabledByUser());

			// Not yet temporary because it's not enabled.
			TestFalse("Not temporary while disabled", FirstState->HasTemporaryBreakpoint());

			// Enable for single step.
			FKismetDebugUtilities::SetBreakpointEnabledForSingleStep(*Breakpoint, true);

			TestTrue("Now enabled", Breakpoint->IsEnabled());
			TestFalse("Still not enabled by user", Breakpoint->IsEnabledByUser());
			TestTrue("Node reports temporary", FirstState->HasTemporaryBreakpoint());

			FirstState->RemoveBreakpoint();
			TestNull("Temporary breakpoint removed", FirstState->GetBreakpoint());
		});

		It("should not overwrite an existing user breakpoint with a temporary one", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());

			// Create user breakpoint first.
			FirstState->CreateBreakpoint(true);
			TestTrue("User breakpoint enabled by user", FirstState->GetBreakpoint()->IsEnabledByUser());

			// Attempt to create temporary breakpoint -- should be a no-op since one already exists.
			FirstState->CreateBreakpointForSingleStep();
			TestTrue("Still enabled by user", FirstState->GetBreakpoint()->IsEnabledByUser());
			TestFalse("Not considered temporary", FirstState->HasTemporaryBreakpoint());

			FirstState->RemoveBreakpoint();
			TestNull("User breakpoint removed", FirstState->GetBreakpoint());
		});

		It("should correctly differentiate user vs temporary breakpoints", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());
			USMGraphNode_StateNode* SecondState = CastChecked<USMGraphNode_StateNode>(FirstState->GetNextState());
			USMGraphNode_StateNode* ThirdState = CastChecked<USMGraphNode_StateNode>(SecondState->GetNextState());

			// User enabled -- not temporary.
			FirstState->CreateBreakpoint(true);
			TestFalse("User enabled is not temporary", FirstState->HasTemporaryBreakpoint());
			TestFalse("User enabled is not temporary (static)", USMBlueprintDebugController::IsBreakpointTemporary(*FirstState->GetBreakpoint()));

			// User disabled -- not temporary.
			SecondState->CreateBreakpoint(false);
			TestFalse("User disabled is not temporary", SecondState->HasTemporaryBreakpoint());
			TestFalse("User disabled is not temporary (static)", USMBlueprintDebugController::IsBreakpointTemporary(*SecondState->GetBreakpoint()));

			// Single step enabled -- is temporary.
			ThirdState->CreateBreakpointForSingleStep();
			FKismetDebugUtilities::SetBreakpointEnabledForSingleStep(*ThirdState->GetBreakpoint(), true);
			TestTrue("Single step enabled is temporary", ThirdState->HasTemporaryBreakpoint());
			TestTrue("Single step enabled is temporary (static)", USMBlueprintDebugController::IsBreakpointTemporary(*ThirdState->GetBreakpoint()));

			FirstState->RemoveBreakpoint();
			TestNull("First removed", FirstState->GetBreakpoint());
			SecondState->RemoveBreakpoint();
			TestNull("Second removed", SecondState->GetBreakpoint());
			ThirdState->RemoveBreakpoint();
			TestNull("Third removed", ThirdState->GetBreakpoint());
		});
	});

	Describe("Cleanup temporary breakpoints", [this]()
	{
		It("should remove only temporary breakpoints and preserve user breakpoints", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());
			USMGraphNode_StateNode* SecondState = CastChecked<USMGraphNode_StateNode>(FirstState->GetNextState());
			USMGraphNode_StateNode* ThirdState = CastChecked<USMGraphNode_StateNode>(SecondState->GetNextState());
			USMGraphNode_StateNode* FourthState = CastChecked<USMGraphNode_StateNode>(ThirdState->GetNextState());

			// First: user breakpoint (enabled).
			FirstState->CreateBreakpoint(true);

			// Second: temporary breakpoint.
			SecondState->CreateBreakpointForSingleStep();
			FKismetDebugUtilities::SetBreakpointEnabledForSingleStep(*SecondState->GetBreakpoint(), true);

			// Third: temporary breakpoint.
			ThirdState->CreateBreakpointForSingleStep();
			FKismetDebugUtilities::SetBreakpointEnabledForSingleStep(*ThirdState->GetBreakpoint(), true);

			// Fourth: no breakpoint.

			USMBlueprintDebugController::CleanupTemporaryBreakpoints(NewBP);

			TestNotNull("User breakpoint survives", FirstState->GetBreakpoint());
			TestTrue("User breakpoint still enabled", FirstState->GetBreakpoint()->IsEnabled());
			TestNull("Second temporary removed", SecondState->GetBreakpoint());
			TestNull("Third temporary removed", ThirdState->GetBreakpoint());
			TestNull("Fourth still has none", FourthState->GetBreakpoint());

			FirstState->RemoveBreakpoint();
			TestNull("User breakpoint removed after cleanup test", FirstState->GetBreakpoint());
		});

		It("should preserve both enabled and disabled user breakpoints during cleanup", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());
			USMGraphNode_StateNode* SecondState = CastChecked<USMGraphNode_StateNode>(FirstState->GetNextState());
			USMGraphNode_StateNode* ThirdState = CastChecked<USMGraphNode_StateNode>(SecondState->GetNextState());

			// Enabled user breakpoint.
			FirstState->CreateBreakpoint(true);

			// Disabled user breakpoint.
			SecondState->CreateBreakpoint(false);

			// Temporary breakpoint.
			ThirdState->CreateBreakpointForSingleStep();
			FKismetDebugUtilities::SetBreakpointEnabledForSingleStep(*ThirdState->GetBreakpoint(), true);

			USMBlueprintDebugController::CleanupTemporaryBreakpoints(NewBP);

			TestNotNull("Enabled user breakpoint survives", FirstState->GetBreakpoint());
			TestTrue("Still enabled", FirstState->GetBreakpoint()->IsEnabled());

			TestNotNull("Disabled user breakpoint survives", SecondState->GetBreakpoint());
			TestFalse("Still disabled", SecondState->GetBreakpoint()->IsEnabled());

			TestNull("Temporary removed", ThirdState->GetBreakpoint());

			FirstState->RemoveBreakpoint();
			TestNull("Enabled user removed", FirstState->GetBreakpoint());
			SecondState->RemoveBreakpoint();
			TestNull("Disabled user removed", SecondState->GetBreakpoint());
		});

		It("should scope cleanup to the specified blueprint only", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* BP1_State = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());

			// Create a second blueprint.
			FAssetHandler SecondAsset;
			USMBlueprint* SecondBP = nullptr;
			USMGraphK2Node_StateMachineNode* SecondRootNode = nullptr;
			USMGraph* SecondGraph = nullptr;
			check(SetupTest(this, SecondAsset, &SecondBP, &SecondRootNode, &SecondGraph));

			UEdGraphPin* LastStatePin2 = nullptr;
			int32 SecondTotalStates = 2;
			TestHelpers::BuildLinearStateMachine(this, SecondGraph, SecondTotalStates, &LastStatePin2);
			FKismetEditorUtilities::CompileBlueprint(SecondBP);

			USMGraphNode_StateNode* BP2_State = CastChecked<USMGraphNode_StateNode>(SecondGraph->GetEntryNode()->GetOutputNode());

			// Add temporary breakpoints to both blueprints.
			BP1_State->CreateBreakpointForSingleStep();
			FKismetDebugUtilities::SetBreakpointEnabledForSingleStep(*BP1_State->GetBreakpoint(), true);

			BP2_State->CreateBreakpointForSingleStep();
			FKismetDebugUtilities::SetBreakpointEnabledForSingleStep(*BP2_State->GetBreakpoint(), true);

			// Cleanup only the first blueprint.
			USMBlueprintDebugController::CleanupTemporaryBreakpoints(NewBP);

			TestNull("BP1 temporary removed", BP1_State->GetBreakpoint());
			TestNotNull("BP2 temporary still exists", BP2_State->GetBreakpoint());

			// Cleanup second blueprint.
			USMBlueprintDebugController::CleanupTemporaryBreakpoints(SecondBP);
			TestNull("BP2 temporary now removed", BP2_State->GetBreakpoint());

			SecondAsset.DeleteAsset(this);
		});
	});

	Describe("Multiple states", [this]()
	{
		It("should independently manage breakpoints across states", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());
			USMGraphNode_StateNode* SecondState = CastChecked<USMGraphNode_StateNode>(FirstState->GetNextState());
			USMGraphNode_StateNode* ThirdState = CastChecked<USMGraphNode_StateNode>(SecondState->GetNextState());
			USMGraphNode_StateNode* FourthState = CastChecked<USMGraphNode_StateNode>(ThirdState->GetNextState());

			// Breakpoints on first and third only.
			FirstState->CreateBreakpoint(true);
			ThirdState->CreateBreakpoint(true);

			TestNotNull("First has breakpoint", FirstState->GetBreakpoint());
			TestNull("Second has none", SecondState->GetBreakpoint());
			TestNotNull("Third has breakpoint", ThirdState->GetBreakpoint());
			TestNull("Fourth has none", FourthState->GetBreakpoint());

			// Disable first.
			FirstState->SetBreakpointEnabled(false);
			TestFalse("First disabled", FirstState->GetBreakpoint()->IsEnabled());
			TestTrue("Third still enabled", ThirdState->GetBreakpoint()->IsEnabled());

			// Remove third.
			ThirdState->RemoveBreakpoint();
			TestNull("Third removed", ThirdState->GetBreakpoint());
			TestNotNull("First still exists", FirstState->GetBreakpoint());

			FirstState->RemoveBreakpoint();
			TestNull("First finally removed", FirstState->GetBreakpoint());
		});
	});

	Describe("Conduit breakpoints", [this]()
	{
		It("should create and remove breakpoints on conduit nodes", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin);

			USMGraphNode_StateNode* FirstState = CastChecked<USMGraphNode_StateNode>(StateMachineGraph->GetEntryNode()->GetOutputNode());
			USMGraphNode_StateNode* SecondState = CastChecked<USMGraphNode_StateNode>(FirstState->GetNextState());

			USMGraphNode_ConduitNode* ConduitNode = FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_ConduitNode>(SecondState);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			TestTrue("Conduit can place breakpoints", ConduitNode->CanPlaceBreakpoints());

			ConduitNode->CreateBreakpoint(true);
			TestNotNull("Breakpoint on conduit", ConduitNode->GetBreakpoint());
			TestTrue("Conduit breakpoint enabled", ConduitNode->GetBreakpoint()->IsEnabled());

			ConduitNode->RemoveBreakpoint();
			TestNull("Conduit breakpoint removed", ConduitNode->GetBreakpoint());
		});
	});

	Describe("Nested state machine breakpoints", [this]()
	{
		It("should support breakpoints on the nested SM node and its inner states", [this]()
		{
			UEdGraphPin* LastStatePin = nullptr;
			UEdGraphPin* NestedPinOut = nullptr;
			const int32 NestedStatesCount = 2;
			USMGraphNode_StateMachineStateNode* NestedSMNode = TestHelpers::BuildNestedStateMachine(
				this, StateMachineGraph, NestedStatesCount, &LastStatePin, &NestedPinOut);
			TestNotNull("Nested SM created", NestedSMNode);
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			// Breakpoint on the nested SM node itself.
			NestedSMNode->CreateBreakpoint(true);
			TestNotNull("Breakpoint on nested SM", NestedSMNode->GetBreakpoint());

			// Breakpoints on inner states.
			USMGraph* NestedGraph = Cast<USMGraph>(NestedSMNode->GetBoundGraph());
			TestNotNull("Nested SM graph exists", NestedGraph);

			TArray<USMGraphNode_StateNode*> NestedStates;
			FSMBlueprintEditorUtils::GetAllNodesOfClassNested<USMGraphNode_StateNode>(NestedGraph, NestedStates);
			TestTrue("Nested SM has states", NestedStates.Num() > 0);

			if (NestedStates.Num() > 0)
			{
				USMGraphNode_StateNode* InnerState = NestedStates[0];
				TestTrue("Inner state can place breakpoints", InnerState->CanPlaceBreakpoints());
				InnerState->CreateBreakpoint(true);
				TestNotNull("Breakpoint on inner state", InnerState->GetBreakpoint());
				InnerState->RemoveBreakpoint();
				TestNull("Inner state breakpoint removed", InnerState->GetBreakpoint());
			}

			NestedSMNode->RemoveBreakpoint();
			TestNull("Nested SM breakpoint removed", NestedSMNode->GetBreakpoint());
		});
	});

	AfterEach([this]()
	{
		NewAsset.DeleteAsset(this);
		SPEC_TEST_AFTER_EACH()
	});
}

#endif

#endif //WITH_DEV_AUTOMATION_TESTS
