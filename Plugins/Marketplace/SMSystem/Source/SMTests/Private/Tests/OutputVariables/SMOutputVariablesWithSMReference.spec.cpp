// Copyright Recursoft LLC. All Rights Reserved.
//
// Regression tests for variable output inside State Machine References.
//
// Bug that was fixed: state-to-state output variable connections inside a referenced SM
// did not propagate values -- the destination state always received the default value.
// Root cause: FindOrAddOutputProxyPropertyData / FindOutputProxyPropertyData did not
// redirect to the primary (owning) instance, so GetNodeByGuid failed on the reference
// instance and the proxy write/read returned nullptr.
//
// The "same name and type" variants (BothInt, InputOutputStr) cover a related corner case
// where the source and destination property share the same name. These are included to
// document the scenario; if they fail it indicates a separate bug distinct from the
// EXECUTE_ON_PRIMARY fix above.

#include "Helpers/SMTestBoilerplate.h"
#include "Helpers/SMTestHelpers.h"
#include "SMOutputVariablesTestClasses.h"

#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Graph/Nodes/SMGraphNode_TransitionEdge.h"
#include "Graph/SMGraph.h"
#include "Properties/SMEditorPropertyIOUtils.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Blueprints/SMBlueprint.h"
#include "SMInstance.h"


#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FOutputVariableSMReferenceSpec,
                  "LogicDriver.NodeInstance.Variables.Output.State machine reference",
                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::
                  EngineFilter)
	SETUP_SPEC_TEST_DEFINES()

	/** Asset handlers for referenced blueprints created by ConvertStateMachineToReference. */
	TArray<FAssetHandler> CreatedReferenceAssets;

	/**
	 * Build an outer state machine containing a nested SM reference whose inner states use
	 * USMOutputVariablesStateInstance. The outer SM layout is:
	 *   (entry) --> [outer state A] --> [Nested SM / Reference with 2 inner states] --> [outer state B]
	 *
	 * A property connection between the two inner states is set up before converting to a reference.
	 *
	 * @param SrcPropName	Property on inner State 0 to use as output.
	 * @param DstPropName	Property on inner State 1 to use as input.
	 * @return true if setup succeeded.
	 */
	bool BuildOuterSMWithReference(
		const FName& SrcPropName,
		const FName& DstPropName)
	{
		using namespace LD::Editor::PropertyUtils::IO;

		// --- Outer SM: one entry state before the reference ---
		UEdGraphPin* LastStatePin = nullptr;
		TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, 1, &LastStatePin);

		// --- Nested SM node, wired from the previous state's output ---
		USMGraphNode_StateMachineStateNode* NestedSMNode = TestHelpers::CreateNewNode<USMGraphNode_StateMachineStateNode>(
			this, StateMachineGraph, LastStatePin);
		if (!TestNotNull(TEXT("Nested SM node created"), NestedSMNode))
		{
			return false;
		}

		// The transition into the nested SM must fire so the reference is entered.
		if (NestedSMNode->GetInputPin()->LinkedTo.Num() > 0)
		{
			if (USMGraphNode_TransitionEdge* TransitionIn = Cast<USMGraphNode_TransitionEdge>(
				NestedSMNode->GetInputPin()->LinkedTo[0]->GetOwningNode()))
			{
				TestHelpers::AddTransitionResultLogic(this, TransitionIn);
			}
		}

		// Wait for the inner SM to reach its end state before evaluating the exit transition.
		// Without this, the outer SM exits the reference node immediately (bWaitForEndState defaults
		// to false), so inner states A and B never run and no variable transfer occurs.
		NestedSMNode->GetNodeTemplateAs<USMStateMachineInstance>()->SetWaitForEndState(true);

		// --- Build 2 inner states using the output-variable-capable class ---
		// bForceTransitionsToTrue = true: ensures the inner A->B transition has result logic so
		// it actually fires at runtime; without this the transition gets bAlwaysFalse = true,
		// State A becomes an immediate end state, and State B never enters.
		UEdGraphPin* LastNestedPin = nullptr;
		TestHelpers::BuildLinearStateMachine(this,
			Cast<USMGraph>(NestedSMNode->GetBoundGraph()),
			2,
			&LastNestedPin,
			USMOutputVariablesStateInstance::StaticClass(),
			nullptr,
			true);

		// --- Locate the two inner state nodes ---
		TArray<USMGraphNode_StateNode*> InnerStateNodes;
		FSMBlueprintEditorUtils::GetAllNodesOfClassNested(NestedSMNode->GetBoundGraph(), InnerStateNodes);
		if (!TestEqual(TEXT("Inner SM has exactly 2 state nodes"), InnerStateNodes.Num(), 2))
		{
			return false;
		}

		// InnerStateNodes[0] is the entry (output) state; InnerStateNodes[1] is the next (input) state.
		// This ordering is reliable: BuildLinearStateMachine creates nodes sequentially and
		// GetAllNodesOfClassNested returns them in insertion order.
		USMGraphK2Node_PropertyNode_Base* Src =
			InnerStateNodes[0]->GetGraphPropertyNode(SrcPropName);
		USMGraphK2Node_PropertyNode_Base* Dst =
			InnerStateNodes[1]->GetGraphPropertyNode(DstPropName);

		if (!TestNotNull(FString::Printf(TEXT("Source property '%s' found on inner state 0"), *SrcPropName.ToString()), Src) ||
			!TestNotNull(FString::Printf(TEXT("Dest property '%s' found on inner state 1"), *DstPropName.ToString()), Dst))
		{
			return false;
		}

		if (!TestTrue(FString::Printf(TEXT("Property connection %s -> %s created"),
			*SrcPropName.ToString(), *DstPropName.ToString()),
			TryMakePropertyConnection(Src, Dst)))
		{
			return false;
		}

		// --- Convert nested SM to a reference ---
		// ConvertStateMachineToReference compiles the new blueprint internally.
		USMBlueprint* ReferencedBP = FSMBlueprintEditorUtils::ConvertStateMachineToReference(
			NestedSMNode, false, nullptr, nullptr);
		if (!TestNotNull(TEXT("Referenced blueprint created"), ReferencedBP))
		{
			return false;
		}
		CreatedReferenceAssets.Add(TestHelpers::CreateAssetFromBlueprint(ReferencedBP));

		// --- Outer SM: one exit state after the reference (provides a proper end state) ---
		// BuildLinearStateMachine already adds result logic to the exit transition by default
		// (bForceTransitionsToTrue = true), so no explicit AddTransitionResultLogic call is needed.
		LastStatePin = NestedSMNode->GetOutputPin();
		TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, 1, &LastStatePin);

		return true;
	}

	/**
	 * Find the reference instance (at any nesting depth) whose initial state uses
	 * USMOutputVariablesStateInstance as its node class.
	 *
	 * Must be called on the primary (outer) instance, not on a reference. Background:
	 * BuildStateMachineMap — which populates StateMachineGuids — is only called on the primary
	 * reference owner. Reference instances never call it, so GetAllReferencedInstances on a
	 * non-primary always returns empty. Calling with bIncludeChildren=true on the primary returns
	 * all nested references across every depth.
	 */
	USMInstance* FindReferenceWithOutputVarStates(USMInstance* PrimaryInstance)
	{
		const TArray<USMInstance*> AllRefs = PrimaryInstance->GetAllReferencedInstances(true);
		for (USMInstance* Ref : AllRefs)
		{
			const FSMState_Base* Initial = Ref->GetRootStateMachine().GetSingleInitialState();
			if (Initial && Initial->GetNodeInstance() &&
				Initial->GetNodeInstance()->IsA<USMOutputVariablesStateInstance>())
			{
				return Ref;
			}
		}
		return nullptr;
	}

	/**
	 * Build an outer state machine where the nested reference (L1) itself contains a further
	 * nested reference (L2). The output variable connection lives between the two innermost
	 * states inside L2. This verifies that EXECUTE_ON_PRIMARY chains correctly through two
	 * levels of reference indirection, routing proxy data writes/reads all the way back to the
	 * primary (outer) instance.
	 *
	 * Layout:
	 *   Outer SM:  [state0] --> [L1 ref (wait)] --> [state1]
	 *   L1 ref:    [inner state] --> [L2 ref (wait)] --> [inner state]
	 *   L2 ref:    [deep A (OutputVars)] --connection--> [deep B (OutputVars)]
	 *
	 * @param SrcPropName	Property on deep State A to use as output.
	 * @param DstPropName	Property on deep State B to use as input.
	 * @return true if setup succeeded.
	 */
	bool BuildOuterSMWithDoubleReference(
		const FName& SrcPropName,
		const FName& DstPropName)
	{
		using namespace LD::Editor::PropertyUtils::IO;

		// --- Outer SM: one entry state ---
		UEdGraphPin* LastOuterPin = nullptr;
		TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, 1, &LastOuterPin);

		// --- L1 nested SM node ---
		USMGraphNode_StateMachineStateNode* L1SMNode = TestHelpers::CreateNewNode<USMGraphNode_StateMachineStateNode>(
			this, StateMachineGraph, LastOuterPin);
		if (!TestNotNull(TEXT("L1 SM node created"), L1SMNode))
		{
			return false;
		}

		if (L1SMNode->GetInputPin()->LinkedTo.Num() > 0)
		{
			if (USMGraphNode_TransitionEdge* TransIn = Cast<USMGraphNode_TransitionEdge>(
				L1SMNode->GetInputPin()->LinkedTo[0]->GetOwningNode()))
			{
				TestHelpers::AddTransitionResultLogic(this, TransIn);
			}
		}
		L1SMNode->GetNodeTemplateAs<USMStateMachineInstance>()->SetWaitForEndState(true);

		// --- Inside L1: one entry state, then L2 nested SM ---
		USMGraph* L1Graph = Cast<USMGraph>(L1SMNode->GetBoundGraph());
		UEdGraphPin* LastL1Pin = nullptr;
		TestHelpers::BuildLinearStateMachine(this, L1Graph, 1, &LastL1Pin);

		USMGraphNode_StateMachineStateNode* L2SMNode = TestHelpers::CreateNewNode<USMGraphNode_StateMachineStateNode>(
			this, L1Graph, LastL1Pin);
		if (!TestNotNull(TEXT("L2 SM node created"), L2SMNode))
		{
			return false;
		}

		if (L2SMNode->GetInputPin()->LinkedTo.Num() > 0)
		{
			if (USMGraphNode_TransitionEdge* TransIn = Cast<USMGraphNode_TransitionEdge>(
				L2SMNode->GetInputPin()->LinkedTo[0]->GetOwningNode()))
			{
				TestHelpers::AddTransitionResultLogic(this, TransIn);
			}
		}
		L2SMNode->GetNodeTemplateAs<USMStateMachineInstance>()->SetWaitForEndState(true);

		// --- Inside L2: 2 deep states with output variable class ---
		UEdGraphPin* LastL2Pin = nullptr;
		TestHelpers::BuildLinearStateMachine(this,
			Cast<USMGraph>(L2SMNode->GetBoundGraph()),
			2, &LastL2Pin,
			USMOutputVariablesStateInstance::StaticClass(),
			nullptr, true);

		TArray<USMGraphNode_StateNode*> DeepStateNodes;
		FSMBlueprintEditorUtils::GetAllNodesOfClassNested(L2SMNode->GetBoundGraph(), DeepStateNodes);
		if (!TestEqual(TEXT("L2 has exactly 2 deep state nodes"), DeepStateNodes.Num(), 2))
		{
			return false;
		}

		// See BuildOuterSMWithReference: ordering is reliable (BuildLinearStateMachine insertion order).
		USMGraphK2Node_PropertyNode_Base* Src = DeepStateNodes[0]->GetGraphPropertyNode(SrcPropName);
		USMGraphK2Node_PropertyNode_Base* Dst = DeepStateNodes[1]->GetGraphPropertyNode(DstPropName);
		if (!TestNotNull(FString::Printf(TEXT("Deep source property '%s' found"), *SrcPropName.ToString()), Src) ||
			!TestNotNull(FString::Printf(TEXT("Deep dest property '%s' found"), *DstPropName.ToString()), Dst))
		{
			return false;
		}

		if (!TestTrue(FString::Printf(TEXT("Deep property connection %s -> %s created"),
			*SrcPropName.ToString(), *DstPropName.ToString()),
			TryMakePropertyConnection(Src, Dst)))
		{
			return false;
		}

		// --- Convert L2 to reference first, then add L1's exit state, then convert L1 ---
		// Explicit asset names are required because both SM nodes share the same default state name
		// ("State Machine"), which would produce identical auto-generated names and cause a name
		// collision when FactoryCreateNew tries to create the second blueprint in the same package.
		// ConvertStateMachineToReference compiles the new blueprint internally.
		const FString L2AssetName = TEXT("BP_DoubleRef_L2Inner");
		USMBlueprint* L2ReferencedBP = FSMBlueprintEditorUtils::ConvertStateMachineToReference(
			L2SMNode, false, &L2AssetName, nullptr);
		if (!TestNotNull(TEXT("L2 referenced blueprint created"), L2ReferencedBP))
		{
			return false;
		}
		CreatedReferenceAssets.Add(TestHelpers::CreateAssetFromBlueprint(L2ReferencedBP));

		// L1 exit state — uses L2's reference node output pin as the connection point.
		// BuildLinearStateMachine default adds result logic to the exit transition.
		UEdGraphPin* L1ExitPin = L2SMNode->GetOutputPin();
		TestHelpers::BuildLinearStateMachine(this, L1Graph, 1, &L1ExitPin);

		const FString L1AssetName = TEXT("BP_DoubleRef_L1Outer");
		USMBlueprint* L1ReferencedBP = FSMBlueprintEditorUtils::ConvertStateMachineToReference(
			L1SMNode, false, &L1AssetName, nullptr);
		if (!TestNotNull(TEXT("L1 referenced blueprint created"), L1ReferencedBP))
		{
			return false;
		}
		CreatedReferenceAssets.Add(TestHelpers::CreateAssetFromBlueprint(L1ReferencedBP));

		// Outer exit state.
		UEdGraphPin* OuterExitPin = L1SMNode->GetOutputPin();
		TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, 1, &OuterExitPin);

		return true;
	}

END_DEFINE_SPEC(FOutputVariableSMReferenceSpec)

void FOutputVariableSMReferenceSpec::Define()
{
	Describe("State to state output variable propagation inside a SM Reference", [this]()
	{
		BeforeEach([this]()
		{
			SPEC_TEST_BEFORE_EACH()
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST_NO_STATES()
		});

		AfterEach([this]()
		{
			SPEC_TEST_AFTER_EACH()

			// Unload all assets to fully purge transient packages from memory.
			// DeleteAsset does not clean up transient packages that have no on-disk file,
			// leaving dirty blueprints that PIE would try to recompile in later tests.
			// Unload the outer blueprint first so it releases references to nested BPs.
			NewAsset.UnloadAsset();

			for (FAssetHandler& RefAsset : CreatedReferenceAssets)
			{
				RefAsset.UnloadAsset();
			}
			CreatedReferenceAssets.Empty();
		});

		It("should transfer int output (OutputInt->InputInt) between states inside a referenced SM", [this]()
		{
			// Build the outer SM with a reference containing an OutputInt -> InputInt connection.
			if (!BuildOuterSMWithReference(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputInt),
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputInt)))
			{
				return;
			}

			// Create instance (compiled + initialized, not yet started).
			USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
			if (!TestNotNull(TEXT("State machine instance created"), Instance))
			{
				return;
			}

			// The reference instance must exist after initialization.
			TArray<USMInstance*> ReferencedInstances = Instance->GetAllReferencedInstances();
			if (!TestEqual(TEXT("Exactly one referenced instance"), ReferencedInstances.Num(), 1))
			{
				return;
			}

			USMInstance* RefInstance = ReferencedInstances[0];

			// Navigate to the two states inside the reference.
			USMOutputVariablesStateInstance* InnerStateA = Cast<USMOutputVariablesStateInstance>(
				RefInstance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());
			USMOutputVariablesStateInstance* InnerStateB = InnerStateA
				? Cast<USMOutputVariablesStateInstance>(InnerStateA->GetNextStateByTransitionIndex(0))
				: nullptr;

			if (!TestNotNull(TEXT("Inner State A (initial state of reference)"), InnerStateA) ||
				!TestNotNull(TEXT("Inner State B (second state of reference)"), InnerStateB))
			{
				return;
			}

			constexpr int32 ExpectedValue = 42;

			// During State A's begin: set the output value that should flow to State B's input.
			InnerStateA->OnTestStateBeginEvent.BindLambda([InnerStateA]()
			{
				InnerStateA->OutputInt = 42;
			});

			// Run all state machines; cap iterations to avoid hanging if the bug stalls execution.
			TestHelpers::RunAllStateMachinesToCompletion(this, Instance, nullptr, 100);

			// Bug: without the fix, InnerStateB->InputInt will be 0 (default) instead of 42.
			TestEqual(
				TEXT("Inner State B InputInt should receive State A OutputInt value via SM Reference"),
				InnerStateB->InputInt,
				ExpectedValue);
		});

		It("should transfer float output (OutputFloat->InputFloat) between states inside a referenced SM", [this]()
		{
			if (!BuildOuterSMWithReference(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputFloat),
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputFloat)))
			{
				return;
			}

			USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
			if (!TestNotNull(TEXT("Instance created"), Instance))
			{
				return;
			}

			TArray<USMInstance*> ReferencedInstances = Instance->GetAllReferencedInstances();
			if (!TestEqual(TEXT("One referenced instance"), ReferencedInstances.Num(), 1))
			{
				return;
			}

			USMInstance* RefInstance = ReferencedInstances[0];

			USMOutputVariablesStateInstance* InnerStateA = Cast<USMOutputVariablesStateInstance>(
				RefInstance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());
			USMOutputVariablesStateInstance* InnerStateB = InnerStateA
				? Cast<USMOutputVariablesStateInstance>(InnerStateA->GetNextStateByTransitionIndex(0))
				: nullptr;

			if (!TestNotNull(TEXT("Inner State A"), InnerStateA) || !TestNotNull(TEXT("Inner State B"), InnerStateB))
			{
				return;
			}

			constexpr float ExpectedValue = 3.14159f;

			InnerStateA->OnTestStateBeginEvent.BindLambda([InnerStateA]()
			{
				InnerStateA->OutputFloat = 3.14159f;
			});

			TestHelpers::RunAllStateMachinesToCompletion(this, Instance, nullptr, 100);

			TestEqual(
				TEXT("Inner State B InputFloat should receive State A OutputFloat value via SM Reference"),
				InnerStateB->InputFloat,
				ExpectedValue);
		});

		It("should transfer string output (OutputStr->InputStr) between states inside a referenced SM", [this]()
		{
			if (!BuildOuterSMWithReference(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputStr),
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputStr)))
			{
				return;
			}

			USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
			if (!TestNotNull(TEXT("Instance created"), Instance))
			{
				return;
			}

			TArray<USMInstance*> ReferencedInstances = Instance->GetAllReferencedInstances();
			if (!TestEqual(TEXT("One referenced instance"), ReferencedInstances.Num(), 1))
			{
				return;
			}

			USMInstance* RefInstance = ReferencedInstances[0];

			USMOutputVariablesStateInstance* InnerStateA = Cast<USMOutputVariablesStateInstance>(
				RefInstance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());
			USMOutputVariablesStateInstance* InnerStateB = InnerStateA
				? Cast<USMOutputVariablesStateInstance>(InnerStateA->GetNextStateByTransitionIndex(0))
				: nullptr;

			if (!TestNotNull(TEXT("Inner State A"), InnerStateA) || !TestNotNull(TEXT("Inner State B"), InnerStateB))
			{
				return;
			}

			const FString ExpectedValue = TEXT("ReferenceOutputStringTest");

			InnerStateA->OnTestStateBeginEvent.BindLambda([InnerStateA, ExpectedValue]()
			{
				InnerStateA->OutputStr = ExpectedValue;
			});

			TestHelpers::RunAllStateMachinesToCompletion(this, Instance, nullptr, 100);

			// Bug: OutputStr is not transferred through the SM Reference (remains empty/default).
			TestEqual(
				TEXT("Inner State B InputStr should receive State A OutputStr value via SM Reference"),
				InnerStateB->InputStr,
				ExpectedValue);
		});

		It("should not corrupt data when output/input share the same property name (BothInt) inside a referenced SM", [this]()
		{
			// This covers the reported "same name and type -> data becomes inconsistent" variant.
			// BothInt is marked LD_PropertyDirection = "Both" — it exists with the same name in both states.
			if (!BuildOuterSMWithReference(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, BothInt),
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, BothInt)))
			{
				return;
			}

			USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
			if (!TestNotNull(TEXT("Instance created"), Instance))
			{
				return;
			}

			TArray<USMInstance*> ReferencedInstances = Instance->GetAllReferencedInstances();
			if (!TestEqual(TEXT("One referenced instance"), ReferencedInstances.Num(), 1))
			{
				return;
			}

			USMInstance* RefInstance = ReferencedInstances[0];

			USMOutputVariablesStateInstance* InnerStateA = Cast<USMOutputVariablesStateInstance>(
				RefInstance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());
			USMOutputVariablesStateInstance* InnerStateB = InnerStateA
				? Cast<USMOutputVariablesStateInstance>(InnerStateA->GetNextStateByTransitionIndex(0))
				: nullptr;

			if (!TestNotNull(TEXT("Inner State A"), InnerStateA) || !TestNotNull(TEXT("Inner State B"), InnerStateB))
			{
				return;
			}

			constexpr int32 ExpectedValue = 99;

			InnerStateA->OnTestStateBeginEvent.BindLambda([InnerStateA]()
			{
				InnerStateA->BothInt = 99;
			});

			TestHelpers::RunAllStateMachinesToCompletion(this, Instance, nullptr, 100);

			// Bug: same-name variables cause data to be inconsistent; B may read A's value or stale data.
			TestEqual(
				TEXT("Inner State B BothInt should receive State A BothInt value via SM Reference"),
				InnerStateB->BothInt,
				ExpectedValue);
		});

		It("should not corrupt data when output/input share the same name and type (InputOutputStr) inside a referenced SM", [this]()
		{
			if (!BuildOuterSMWithReference(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputOutputStr),
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputOutputStr)))
			{
				return;
			}

			USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
			if (!TestNotNull(TEXT("Instance created"), Instance))
			{
				return;
			}

			TArray<USMInstance*> ReferencedInstances = Instance->GetAllReferencedInstances();
			if (!TestEqual(TEXT("One referenced instance"), ReferencedInstances.Num(), 1))
			{
				return;
			}

			USMInstance* RefInstance = ReferencedInstances[0];

			USMOutputVariablesStateInstance* InnerStateA = Cast<USMOutputVariablesStateInstance>(
				RefInstance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());
			USMOutputVariablesStateInstance* InnerStateB = InnerStateA
				? Cast<USMOutputVariablesStateInstance>(InnerStateA->GetNextStateByTransitionIndex(0))
				: nullptr;

			if (!TestNotNull(TEXT("Inner State A"), InnerStateA) || !TestNotNull(TEXT("Inner State B"), InnerStateB))
			{
				return;
			}

			const FString ExpectedValue = TEXT("SameNameIOStringTest");

			InnerStateA->OnTestStateBeginEvent.BindLambda([InnerStateA, ExpectedValue]()
			{
				InnerStateA->InputOutputStr = ExpectedValue;
			});

			TestHelpers::RunAllStateMachinesToCompletion(this, Instance, nullptr, 100);

			TestEqual(
				TEXT("Inner State B InputOutputStr should receive State A InputOutputStr value via SM Reference"),
				InnerStateB->InputOutputStr,
				ExpectedValue);
		});

		It("should transfer int output at two levels of reference nesting (OutputInt->InputInt in L2 ref inside L1 ref)", [this]()
		{
			if (!BuildOuterSMWithDoubleReference(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputInt),
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputInt)))
			{
				return;
			}

			USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
			if (!TestNotNull(TEXT("Instance created (double-ref int)"), Instance))
			{
				return;
			}

			USMInstance* L2Instance = FindReferenceWithOutputVarStates(Instance);
			if (!TestNotNull(TEXT("L2 instance found"), L2Instance))
			{
				return;
			}

			USMOutputVariablesStateInstance* DeepStateA = Cast<USMOutputVariablesStateInstance>(
				L2Instance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());
			USMOutputVariablesStateInstance* DeepStateB = DeepStateA
				? Cast<USMOutputVariablesStateInstance>(DeepStateA->GetNextStateByTransitionIndex(0))
				: nullptr;

			if (!TestNotNull(TEXT("Deep State A (L2 initial)"), DeepStateA) ||
				!TestNotNull(TEXT("Deep State B (L2 second)"), DeepStateB))
			{
				return;
			}

			constexpr int32 ExpectedValue = 77;
			DeepStateA->OnTestStateBeginEvent.BindLambda([DeepStateA]()
			{
				DeepStateA->OutputInt = 77;
			});

			TestHelpers::RunAllStateMachinesToCompletion(this, Instance, nullptr, 100);

			TestEqual(
				TEXT("Deep State B InputInt should receive Deep State A OutputInt via two-level SM Reference"),
				DeepStateB->InputInt,
				ExpectedValue);
		});

		It("should transfer float output at two levels of reference nesting (OutputFloat->InputFloat in L2 ref inside L1 ref)", [this]()
		{
			if (!BuildOuterSMWithDoubleReference(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputFloat),
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputFloat)))
			{
				return;
			}

			USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
			if (!TestNotNull(TEXT("Instance created (double-ref float)"), Instance))
			{
				return;
			}

			USMInstance* L2Instance = FindReferenceWithOutputVarStates(Instance);
			if (!TestNotNull(TEXT("L2 instance found"), L2Instance))
			{
				return;
			}

			USMOutputVariablesStateInstance* DeepStateA = Cast<USMOutputVariablesStateInstance>(
				L2Instance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());
			USMOutputVariablesStateInstance* DeepStateB = DeepStateA
				? Cast<USMOutputVariablesStateInstance>(DeepStateA->GetNextStateByTransitionIndex(0))
				: nullptr;

			if (!TestNotNull(TEXT("Deep State A (L2 initial)"), DeepStateA) ||
				!TestNotNull(TEXT("Deep State B (L2 second)"), DeepStateB))
			{
				return;
			}

			constexpr float ExpectedValue = 2.71828f;
			DeepStateA->OnTestStateBeginEvent.BindLambda([DeepStateA]()
			{
				DeepStateA->OutputFloat = 2.71828f;
			});

			TestHelpers::RunAllStateMachinesToCompletion(this, Instance, nullptr, 100);

			TestEqual(
				TEXT("Deep State B InputFloat should receive Deep State A OutputFloat via two-level SM Reference"),
				DeepStateB->InputFloat,
				ExpectedValue);
		});

		It("should transfer FTransform output at two levels of reference nesting (OutputTransform->InputTransform in L2 ref inside L1 ref)", [this]()
		{
			if (!BuildOuterSMWithDoubleReference(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputTransform),
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputTransform)))
			{
				return;
			}

			USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
			if (!TestNotNull(TEXT("Instance created (double-ref transform)"), Instance))
			{
				return;
			}

			USMInstance* L2Instance = FindReferenceWithOutputVarStates(Instance);
			if (!TestNotNull(TEXT("L2 instance found"), L2Instance))
			{
				return;
			}

			USMOutputVariablesStateInstance* DeepStateA = Cast<USMOutputVariablesStateInstance>(
				L2Instance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());
			USMOutputVariablesStateInstance* DeepStateB = DeepStateA
				? Cast<USMOutputVariablesStateInstance>(DeepStateA->GetNextStateByTransitionIndex(0))
				: nullptr;

			if (!TestNotNull(TEXT("Deep State A (L2 initial)"), DeepStateA) ||
				!TestNotNull(TEXT("Deep State B (L2 second)"), DeepStateB))
			{
				return;
			}

			const FTransform ExpectedTransform(
				FQuat(FVector::ForwardVector, PI / 3.f),
				FVector(50.f, 150.f, 250.f),
				FVector(3.f, 3.f, 3.f));

			DeepStateA->OnTestStateBeginEvent.BindLambda([DeepStateA, ExpectedTransform]()
			{
				DeepStateA->OutputTransform = ExpectedTransform;
			});

			TestHelpers::RunAllStateMachinesToCompletion(this, Instance, nullptr, 100);

			TestTrue(
				TEXT("Deep State B InputTransform translation should match via two-level SM Reference"),
				DeepStateB->InputTransform.GetTranslation().Equals(ExpectedTransform.GetTranslation()));
			TestTrue(
				TEXT("Deep State B InputTransform rotation should match via two-level SM Reference"),
				DeepStateB->InputTransform.GetRotation().Equals(ExpectedTransform.GetRotation()));
			TestTrue(
				TEXT("Deep State B InputTransform scale should match via two-level SM Reference"),
				DeepStateB->InputTransform.GetScale3D().Equals(ExpectedTransform.GetScale3D()));
		});

		It("should transfer FTransform output (OutputTransform->InputTransform) between states inside a referenced SM", [this]()
		{
			// This directly mirrors the original bug report which used FTransform as the variable type.
			if (!BuildOuterSMWithReference(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputTransform),
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputTransform)))
			{
				return;
			}

			USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
			if (!TestNotNull(TEXT("Instance created"), Instance))
			{
				return;
			}

			TArray<USMInstance*> ReferencedInstances = Instance->GetAllReferencedInstances();
			if (!TestEqual(TEXT("One referenced instance"), ReferencedInstances.Num(), 1))
			{
				return;
			}

			USMInstance* RefInstance = ReferencedInstances[0];

			USMOutputVariablesStateInstance* InnerStateA = Cast<USMOutputVariablesStateInstance>(
				RefInstance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());
			USMOutputVariablesStateInstance* InnerStateB = InnerStateA
				? Cast<USMOutputVariablesStateInstance>(InnerStateA->GetNextStateByTransitionIndex(0))
				: nullptr;

			if (!TestNotNull(TEXT("Inner State A"), InnerStateA) || !TestNotNull(TEXT("Inner State B"), InnerStateB))
			{
				return;
			}

			// Use a clearly non-identity transform so a default value can't be mistaken for success.
			const FTransform ExpectedTransform(
				FQuat(FVector::UpVector, PI / 4.f),  // 45-degree rotation around Z
				FVector(100.f, 200.f, 300.f),         // non-zero translation
				FVector(2.f, 2.f, 2.f));              // non-unit scale

			InnerStateA->OnTestStateBeginEvent.BindLambda([InnerStateA, ExpectedTransform]()
			{
				InnerStateA->OutputTransform = ExpectedTransform;
			});

			TestHelpers::RunAllStateMachinesToCompletion(this, Instance, nullptr, 100);

			// Bug: InputTransform on State B will be FTransform::Identity (0,0,0 translation, identity rotation,
			// unit scale) instead of ExpectedTransform when the bug is present.
			TestTrue(
				TEXT("Inner State B InputTransform translation should match State A OutputTransform"),
				InnerStateB->InputTransform.GetTranslation().Equals(ExpectedTransform.GetTranslation()));
			TestTrue(
				TEXT("Inner State B InputTransform rotation should match State A OutputTransform"),
				InnerStateB->InputTransform.GetRotation().Equals(ExpectedTransform.GetRotation()));
			TestTrue(
				TEXT("Inner State B InputTransform scale should match State A OutputTransform"),
				InnerStateB->InputTransform.GetScale3D().Equals(ExpectedTransform.GetScale3D()));
		});
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
