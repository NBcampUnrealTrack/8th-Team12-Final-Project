// Copyright Recursoft LLC. All Rights Reserved.

#include "Helpers/SMTestBoilerplate.h"
#include "Helpers/SMTestHelpers.h"
#include "SMOutputVariablesTestClasses.h"
#include "SMOutputVariablesTestDefines.h"

#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOReaderNode.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOWriterNode.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_ConduitNode.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Graph/SMGraph.h"
#include "Properties/SMEditorPropertyIOUtils.h"
#include "Properties/SMEditorPropertyUtils.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Blueprints/SMBlueprint.h"

#include "Kismet2/KismetEditorUtilities.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

// Test a property connection between any two graph nodes.
#define IT_NODE_CONNECTION(SrcNodeExpr, SrcClass, SrcProp, DstNodeExpr, DstClass, DstProp, bExpected) \
	It(FString::Printf(TEXT("connect %s→%s should %s"), TEXT(#SrcProp), TEXT(#DstProp), \
		(bExpected) ? TEXT("succeed") : TEXT("fail")), \
	[this]() \
	{ \
		TestNodeConnection(SrcNodeExpr, GET_MEMBER_NAME_CHECKED(SrcClass, SrcProp), \
			DstNodeExpr, GET_MEMBER_NAME_CHECKED(DstClass, DstProp), bExpected); \
	})

BEGIN_DEFINE_SPEC(FOutputVariableSpec,
                  "LogicDriver.NodeInstance.Variables.Output",
                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::
                  EngineFilter)
	SETUP_SPEC_TEST_DEFINES()
	FAssetHandler NewNodeAsset;
	TArray<USMGraphNode_StateNode*> StateNodes;
	USMGraphNode_ConduitNode* ConduitNode = nullptr;
	USMGraphNode_StateMachineStateNode* StateMachineStateNode = nullptr;
	int32 OutputVarStateCount = 3;

	USMGraphK2Node_PropertyNode_Base* GetProp(const int32 InIndex, const FName& InName, const int32 InArrayIndex = 0,
		const int32 InTemplateIndex = INDEX_NONE)
	{
		check(InIndex >= 0 && InIndex < StateNodes.Num());
		// Find state stack to use.
		const USMNodeInstance* Template = StateNodes[InIndex]->GetTemplateFromIndex(InTemplateIndex);
		USMGraphK2Node_PropertyNode_Base* PropertyNode = StateNodes[InIndex]->GetGraphPropertyNode(InName, Template, InArrayIndex);
		check(PropertyNode);
		return PropertyNode;
	}

	FString SetPropValue(UObject* Object, const FName& PropName, const FString& Value, const int32 ArrayIndex = 0)
	{
		FProperty* Property = Object->GetClass()->FindPropertyByName(PropName);
		check(Property);
		LD::Editor::PropertyUtils::SetPropertyValue(Property, Value, Object, ArrayIndex);
		// Return fully formatted value since floating points can add digits.
		return LD::Editor::PropertyUtils::GetPropertyValue(Property, Object, ArrayIndex);
	}

	void TestPropValue(UObject* Object, const FName& PropName, const FString& Value, bool bShouldMatch,
	                   const int32 ArrayIndex = 0)
	{
		FProperty* Property = Object->GetClass()->FindPropertyByName(PropName);
		check(Property);
		const FString PropertyValue = LD::Editor::PropertyUtils::GetPropertyValue(Property, Object, ArrayIndex);
		// Normalize for lazy testing between truncated values from casting since we're dealing with the exported string
		// value, and this could be going from an int to a float or vise versa. IE, "5" could be converted to "5.0000".
		// While this isn't precise testing for floats and doubles, this test really isn't about the cast itself,
		// and more that the values carried over across connections.
		// We also just remove "INVTEXT" to assist with text-> string conversion testing.
		FString UnwrappedPropertyValue = PropertyValue;
		if (PropertyValue.StartsWith(TEXT("INVTEXT(\"")))
		{
			UnwrappedPropertyValue = PropertyValue.Mid(9, PropertyValue.Len() - 11); // Remove INVTEXT(" and ")
		}
		else if (PropertyValue.StartsWith(TEXT("LOCGEN_NUMBER_GROUPED(")))
		{
			UnwrappedPropertyValue = PropertyValue.Mid(22, PropertyValue.Len() - 26); // Remove LOCGEN_NUMBER_GROUPED(" and ")
		}
		const FString NormalizedPropertyValue = TestHelpers::NormalizeNumericString(UnwrappedPropertyValue);
		const FString UnwrappedValue = Value;
		
		const FString NormalizedValue = TestHelpers::NormalizeNumericString(UnwrappedValue);
		if (bShouldMatch)
		{
			TestEqual(TEXT("Property has bExpected value"), NormalizedPropertyValue, NormalizedValue);
		}
		else
		{
			TestNotEqual(TEXT("Property not set to value"), NormalizedPropertyValue, NormalizedValue);
		}
	}

	/**
	 * Runtime testing for owning BP connections.
	 */
	void TestOwningBPConnectionsRuntime(const FName& StatePropName, const FName& BPPropName,
		const FString& OutputValue, int32 TemplateIdx = INDEX_NONE)
	{
		USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
		USMOutputVariablesStateInstance_Base* FirstState =
			CastChecked<USMOutputVariablesStateInstance_Base>(
				Instance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());

		if (TemplateIdx != INDEX_NONE)
		{
			FirstState = CastChecked<USMOutputVariablesStateInstance_Base>(FirstState->GetStateInStack(TemplateIdx));
		}
		
		// This will be updated during OnStateBegin, by retrieving the value after we set it, to account for additional
		// formatting changes specific to each property.
		FString FormattedOutputValue = OutputValue;

		// Set output properties only after OnStateBegin fires. This is to support "Both" direction properties,
		// since when the state begins their value will be set from the graph property graph.
		FirstState->OnTestStateBeginEvent.BindLambda([&]()
		{
			// Test state does not have the output value set.
			TestPropValue(FirstState, StatePropName, OutputValue, false);

			// Set the initial states output value which will be written to the BP variable.
			FormattedOutputValue = SetPropValue(FirstState, StatePropName, OutputValue);

			// Test BP does not have an output value.
			TestPropValue(Instance, BPPropName, OutputValue, false);
		});

		// Run the state machine, allowing the states to write output and then read that output.
		TestHelpers::RunAllStateMachinesToCompletion(this, Instance);

		// Test initial state has our set output values (sanity check).
		TestPropValue(FirstState, StatePropName, FormattedOutputValue, true);

		// Test the BP variable has the output variable written.
		TestPropValue(Instance, BPPropName, FormattedOutputValue, true);
	}

	/**
	 * Runtime testing for state connections.
	 */
	void TestStateConnectionsRuntime(const FName& OutputPropName, const FName& InputPropName,
	                                 const FString& OutputValue, int32 State1Idx = 0, int32 State2Idx = 1,
	                                 bool bValueShouldMatchOnEnd = true, int32 OutputArrIdx = 0, int32 InputArrIdx = 0,
	                                 int32 OutputTemplateIdx = INDEX_NONE, int32 InputTemplateIdx = INDEX_NONE)
	{
		USMInstance* Instance = TestHelpers::CompileAndCreateStateMachineInstanceFromBP(NewBP, true);
		USMOutputVariablesStateInstance_Base* FirstState =
			CastChecked<USMOutputVariablesStateInstance_Base>(
				Instance->GetRootStateMachine().GetSingleInitialState()->GetNodeInstance());

		USMOutputVariablesStateInstance_Base* SecondState =
			CastChecked<USMOutputVariablesStateInstance_Base>(FirstState->GetNextStateByTransitionIndex(0));

		USMOutputVariablesStateInstance_Base* ThirdState =
			CastChecked<USMOutputVariablesStateInstance_Base>(SecondState->GetNextStateByTransitionIndex(0));
		
		// This will be updated during OnStateBegin, by retrieving the value after we set it, to account for additional
		// formatting changes specific to each property.
		FString FormattedOutputValue = OutputValue;

		USMOutputVariablesStateInstance_Base* SourceState;
		USMOutputVariablesStateInstance_Base* DestinationState;
		
		if (State1Idx == 0)
		{
			SourceState = FirstState;
		}
		else if (State1Idx == 1) 
		{
			SourceState = SecondState;
		}
		else
		{
			SourceState = ThirdState;
		}

		if (OutputTemplateIdx != INDEX_NONE)
		{
			SourceState = CastChecked<USMOutputVariablesStateInstance_Base>(SourceState->GetStateInStack(OutputTemplateIdx));
		}

		if (State2Idx == 0)
		{
			DestinationState = FirstState;
		}
		else if (State2Idx == 1)
		{
			DestinationState = SecondState;
		}
		else
		{
			DestinationState = ThirdState;
		}

		if (InputTemplateIdx != INDEX_NONE)
		{
			DestinationState = CastChecked<USMOutputVariablesStateInstance_Base>(DestinationState->GetStateInStack(InputTemplateIdx));
		}
		
		// Set output properties only after OnStateBegin fires. This is to support "Both" direction properties,
		// since when the state begins their value will be set from the graph property graph.
		SourceState->OnTestStateBeginEvent.BindLambda([&]()
		{
			// Test initial state does not have an output value.
			TestPropValue(SourceState, OutputPropName, OutputValue, false, OutputArrIdx);

			// Set the initial states output value which will be read into the next state.
			FormattedOutputValue = SetPropValue(SourceState, OutputPropName, OutputValue, OutputArrIdx);

			// Make sure the next state doesn't yet match our bExpected value.
			TestPropValue(DestinationState, InputPropName, FormattedOutputValue, false, InputArrIdx);
		});

		// Run the state machine, allowing the states to write output and then read that output.
		TestHelpers::RunAllStateMachinesToCompletion(this, Instance);

		// Test initial state has our set output values (sanity check).
		TestPropValue(SourceState, OutputPropName, FormattedOutputValue, true, OutputArrIdx);

		// Second state input should match our output.
		TestPropValue(DestinationState, InputPropName, FormattedOutputValue, bValueShouldMatchOnEnd, InputArrIdx);
	}

	/** Connect owning bp prop to state output. */
	bool ConnectProps(const FName& StateOutputPropName, const FName& InstancePropName, int32 StateIndex = 0,
	                  bool bShouldMakeInputConnection = true, int32 StatePropArrayIndex = 0,
	                  int32 StatePropTemplateIndex = INDEX_NONE)
	{
		using namespace LD::Editor::PropertyUtils::IO;

		const FProperty* InstanceProperty = FSMBlueprintEditorUtils::GetPropertyByName(NewBP, InstancePropName);
		check(InstanceProperty);

		USMGraphK2Node_PropertyNode_Base* StatePropNode = GetProp(StateIndex, StateOutputPropName, StatePropArrayIndex, StatePropTemplateIndex);
		check(StatePropNode);

		// Check writer is set.
		USMGraphK2Node_PropertyIOWriterNode* Writer = GetConnectedPropertyWriterNode(StatePropNode);
		if (!TestNotNull(TEXT("Writer node exists"), Writer))
		{
			return false;
		}

		// Set the output variable value to owning blueprint value.
		MakeWriterConnectionToOwningBlueprintProperty(Writer, InstanceProperty);

		const TArray<UEdGraphPin*> Outputs = Writer->GetAllPropertyOutputs();
		UEdGraphPin* const* MatchingOutputPin = Outputs.FindByPredicate(
			[InstancePropName](const UEdGraphPin* Pin)
			{
				const TSharedPtr<FPropertyData_Base> PropData = GetPropertyDataFromPin(Pin);
				return PropData.IsValid() && PropData->Property
					&& PropData->Property->GetFName() == InstancePropName
					&& PropData->IsA<FOwningBlueprintPropertyData>();
			});

		if (!TestNotNull(TEXT("Output property found and set"), MatchingOutputPin))
		{
			return false;
		}

		// Test for pin connections directly to the variable output. Only necessary for writing to owning BP properties.
		const UEdGraphPin* InputPin = Writer->FindOppositePin(*MatchingOutputPin);
		if (!TestNotNull(TEXT("Input pin found"), InputPin))
		{
			return false;
		}

		USMGraphK2Node_PropertyNode_Base* ConnectedPropertyNode = FSMBlueprintEditorUtils::FindFirstConnectedNode<
			USMGraphK2Node_PropertyNode_Base>(InputPin);
		// Supposed to succeed.
		if (bShouldMakeInputConnection)
		{
			if (!TestNotNull(TEXT("Property node connected to input pin"), ConnectedPropertyNode))
			{
				return false;
			}
		}
		// Supposed to fail.
		else if (TestNull(TEXT("Property node not connected to input pin"), ConnectedPropertyNode))
		{
			return false;
		}

		TestEqual(TEXT("Original property node found"), ConnectedPropertyNode, StatePropNode);

		return true;
	}

	/**
	 * Validate reader and writer nodes after a successful property connection.
	 * Returns false if the connection is not truly usable (e.g. no valid cast node).
	 */
	bool ValidatePropertyConnection(USMGraphK2Node_PropertyNode_Base* Src, const FName& SrcPropName,
		USMGraphK2Node_PropertyNode_Base* Dst, const FName& DstPropName)
	{
		using namespace LD::Editor::PropertyUtils::IO;

		// Check reader is set.
		{
			const USMGraphK2Node_PropertyIOReaderNode* Reader = GetConnectedPropertyReaderNode(Dst);
			if (!Reader)
			{
				// No valid cast node means the connection is not truly usable.
				// For example, attempting to connect Text to Integer isn't possible by default.
				// todo Output Variables - We may want TryMakePropertyConnection to detect this and fail.
				return false;
			}

			const TSharedPtr<FPropertyData_Base> ReaderData = GetPropertyDataFromPin(Reader->GetPropertyPinIn());
			if (!TestTrue(TEXT("ReaderData is valid"), ReaderData.IsValid()))
			{
				return false;
			}

			TestTrue(TEXT("ReaderData is node data"), ReaderData->IsA<FNodePropertyData>());

			if (!TestNotNull(TEXT("ReaderData property is set"), ReaderData->Property))
			{
				return false;
			}

			TestEqual(TEXT("Reader property found and set"), ReaderData->Property->GetFName(), SrcPropName);
			TestTrue(
				TEXT("Reader contains references to source property"),
				Reader->GetAllReferencedIONodes().Contains(Src));
		}

		// Check writer is set.
		{
			const USMGraphK2Node_PropertyIOWriterNode* Writer = GetConnectedPropertyWriterNode(Src);
			if (!TestNotNull(TEXT("Writer node exists"), Writer))
			{
				return false;
			}

			const TArray<UEdGraphPin*> Outputs = Writer->GetAllPropertyOutputs();
			UEdGraphPin* const* MatchingOutputPin = Outputs.FindByPredicate(
				[&](const UEdGraphPin* Pin)
				{
					const TSharedPtr<FPropertyData_Base> PropData = GetPropertyDataFromPin(Pin);
					return PropData.IsValid() && PropData->Property && PropData->Property->GetFName() == DstPropName;
				});

			if (!TestNotNull(TEXT("Output property found and set"), MatchingOutputPin))
			{
				return false;
			}

			TestTrue(
				TEXT("Writer contains reference to destination property"),
				Writer->GetAllReferencedIONodes().Contains(Dst));
		}

		return true;
	}

	/** Connect state props. */
	bool ConnectProps(int32 SrcNodeIndex, const FName& SrcProp, int32 SrcPropArrIdx, int32 SrcPropTemplateIdx,
		int32 DstNodeIndex, const FName& DstProp, int32 DstPropArrIdx, int32 DstPropTemplateIdx)
	{
		using namespace LD::Editor::PropertyUtils::IO;
		// Src is writer output...
		USMGraphK2Node_PropertyNode_Base* Src = GetProp(SrcNodeIndex, SrcProp, SrcPropArrIdx, SrcPropTemplateIdx);
		// ... to be read by Dst reader input.
		USMGraphK2Node_PropertyNode_Base* Dst = GetProp(DstNodeIndex, DstProp, DstPropArrIdx, DstPropTemplateIdx);
		const bool bConnected = TryMakePropertyConnection(Src, Dst);

		if (bConnected && !ValidatePropertyConnection(Src, SrcProp, Dst, DstProp))
		{
			return false;
		}

		return bConnected;
	}

	void TestOwningBlueprintConnectionsBody()
	{
		Describe("1 to 1 Connection", [this]()
		{
			Describe("Standard Type Connections", [this]()
			{
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputInt, IntVariable, 256, true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputFloat, FloatVariable, 192.f, true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputStr, StringVariable, "Test owning BP string", true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputText, TextVariable,
												 NSLOCTEXT("OutputTest", "OwningBPOutputText",
													 "Localized Output for Owning BP Text"), true);
			});

			Describe("State Stack", [this]()
			{
				BeforeEach([this]()
				{
					UClass* NodeClassToUse = USMOutputVariablesStateInstance::StaticClass();
					if (NewNodeAsset.Object)
					{
						NodeClassToUse = NewNodeAsset.GetObjectAs<USMNodeBlueprint>()->GeneratedClass;
					}
					TestHelpers::AddToStateStack(StateNodes[0], NodeClassToUse);
					TestHelpers::AddToStateStack(StateNodes[0], NodeClassToUse);
				});
				
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputInt, IntVariable, 256, true, 0);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputInt, IntVariable, 256, true, INDEX_NONE);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputFloat, FloatVariable, 192.5f, true, 0);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputFloat, FloatVariable, 192.5f, true, INDEX_NONE);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputStr, StringVariable, "Test stack 0", true, 0);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputStr, StringVariable, "Test stack base", true, INDEX_NONE);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputDouble, FloatVariable, 123.456, true, 0);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputDouble, FloatVariable, 123.456, true, INDEX_NONE);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputText, TextVariable, NSLOCTEXT("OutputTest", "Stack0", "Stack 0 Text"), true, 0);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputText, TextVariable, NSLOCTEXT("OutputTest", "StackBase", "Stack Base Text"), true, INDEX_NONE);
				
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputInt, IntVariable, 512, true, 1);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputFloat, FloatVariable, 314.159f, true, 1); 
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputStr, StringVariable, "Test stack 1", true, 1);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputDouble, FloatVariable, 271.828, true, 1);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(OutputText, TextVariable, NSLOCTEXT("OutputTest", "Stack1", "Stack 1 Text"), true, 1);
			});
			
			Describe("Casted Connections", [this]()
			{
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputFloat, IntVariable, 256, true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputInt, FloatVariable, 192.f, true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputText, StringVariable, "Test owning BP string", true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputStr, TextVariable, TEXT("Localized Output for Owning BP Text"),
												 true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputDouble, IntVariable, 123, true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputDouble, FloatVariable, 456.789, true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputStr, IntVariable, "789", true);
				IT_CONNECT_BP_PROPS_TEST_RUNTIME(OutputStr, FloatVariable, "123.456", true);
			});

			Describe("Fail Connections (no conversion)", [this]()
			{
				IT_CONNECT_BP_PROPS(OutputText, IntVariable, false);
				IT_CONNECT_BP_PROPS(OutputText, FloatVariable, false);
			});
		});

		Describe("1 to 2 Connections", [this]()
		{
			// Integer output to multiple variables
			IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(OutputInt, IntVariable, 256, OutputInt, FloatVariable, 256, true);
			IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(OutputInt, StringVariable, 512, OutputInt, TextVariable, 512, true);

			// Float output to multiple variables  
			IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(OutputFloat, FloatVariable, 3.14159f, OutputFloat, IntVariable, 3.14159f, true);
			IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(OutputFloat, StringVariable, 2.71828f, OutputFloat, TextVariable, 2.71828f, true);

			// Double output to multiple variables
			IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(OutputDouble, FloatVariable, 123.456, OutputDouble, IntVariable, 123.456, true);
			IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(OutputDouble, StringVariable, 789.012, OutputDouble, TextVariable, 789.012, true);

			// String output to multiple variables
			IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(OutputStr, StringVariable, "Test String 1", OutputStr, TextVariable, "Test String 1", true);
			IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(OutputStr, IntVariable, "42", OutputStr, FloatVariable, "3.14159", true);

			// Text output to multiple string variables
			IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(OutputText, TextVariable, 
				NSLOCTEXT("OutputTest", "OutputText1", "First Output"), 
				OutputText, StringVariable,
				NSLOCTEXT("OutputTest", "OutputText1", "First Output"), true);
		});

		Describe("1 to 3 Connections", [this]()
		{
			// Integer output to multiple variables
			IT_CONNECT_THREE_BP_PROPS_TEST_RUNTIME(OutputInt, IntVariable, 256, 
				OutputInt, FloatVariable, 256,
				OutputInt, StringVariable, 256, true);

			// Float output to multiple variables
			IT_CONNECT_THREE_BP_PROPS_TEST_RUNTIME(OutputFloat, FloatVariable, 3.14159f,
				OutputFloat, IntVariable, 3.14159f,
				OutputFloat, StringVariable, "3.14159", true);

			// Double output to multiple variables  
			IT_CONNECT_THREE_BP_PROPS_TEST_RUNTIME(OutputDouble, FloatVariable, 123.456,
				OutputDouble, IntVariable, 123.456,
				OutputDouble, StringVariable, "123.456", true);

			// String output to multiple variables
			IT_CONNECT_THREE_BP_PROPS_TEST_RUNTIME(OutputStr, StringVariable, "Test String",
				OutputStr, TextVariable, "Test String",
				OutputStr, IntVariable, "42", true);

			// Text output to multiple variables  
			IT_CONNECT_THREE_BP_PROPS_TEST_RUNTIME(OutputText, TextVariable,
				NSLOCTEXT("OutputTest", "OutputText1", "First Output"),
				OutputText, StringVariable, 
				NSLOCTEXT("OutputTest", "OutputText1", "First Output"),
				OutputText, TextVariable,
				NSLOCTEXT("OutputTest", "OutputText1", "First Output"), true);
		});
	}

	void TestStateConnectionsBody()
	{
		Describe("1 to 1 Connection", [this]()
		{
			Describe("State Stack", [this]()
			{
				BeforeEach([this]()
				{
					UClass* NodeClassToUse = USMOutputVariablesStateInstance::StaticClass();
					if (NewNodeAsset.Object)
					{
						NodeClassToUse = NewNodeAsset.GetObjectAs<USMNodeBlueprint>()->GeneratedClass;
					}
					TestHelpers::AddToStateStack(StateNodes[0], NodeClassToUse);
					TestHelpers::AddToStateStack(StateNodes[0], NodeClassToUse);
					
					TestHelpers::AddToStateStack(StateNodes[1], NodeClassToUse);
					TestHelpers::AddToStateStack(StateNodes[1], NodeClassToUse);
					
					TestHelpers::AddToStateStack(StateNodes[2], NodeClassToUse);
					TestHelpers::AddToStateStack(StateNodes[2], NodeClassToUse);
				});

				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputInt, /* Stack */ INDEX_NONE, 1, InputInt, /* Stack */ INDEX_NONE, 526, true);

				// Mix/match
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputInt, /* Stack */ 0, 1, InputInt, /* Stack */ 0, 468, true);
				
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputInt, /* Stack */ 1, 1, InputInt, /* Stack */ 1, 789, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputFloat, /* Stack */ INDEX_NONE, 1, InputFloat, /* Stack */ INDEX_NONE, 3.14159f, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputFloat, /* Stack */ 0, 1, InputFloat, /* Stack */ 0, 2.71828f, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputDouble, /* Stack */ INDEX_NONE, 1, InputDouble, /* Stack */ INDEX_NONE, 123.456, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputDouble, /* Stack */ 1, 1, InputDouble, /* Stack */ 1, 789.012, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputStr, /* Stack */ INDEX_NONE, 1, InputStr, /* Stack */ INDEX_NONE, "Base String Test", true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputStr, /* Stack */ 0, 1, InputStr, /* Stack */ 0, "Stack 0 String Test", true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputText, /* Stack */ INDEX_NONE, 1, InputText, /* Stack */ INDEX_NONE, NSLOCTEXT("OutputTest", "BaseStackText", "Base Stack Text"), true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, OutputText, /* Stack */ 1, 1, InputText, /* Stack */ 1, NSLOCTEXT("OutputTest", "Stack1Text", "Stack 1 Text"), true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, BothInt, /* Stack */ INDEX_NONE, 1, BothInt, /* Stack */ INDEX_NONE, 951, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(0, InputOutputStr, /* Stack */ 0, 1, InputOutputStr, /* Stack */ 0, "IO String Stack 0", true);

				// State stack array tests
				Describe("Array Connections", [this]()
				{
					IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK_ARRAY(0, InputOutputIntArray, 0, /* Stack */ INDEX_NONE, 1, InputInt, 0, /* Stack */ INDEX_NONE, 123, true);
					IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK_ARRAY(0, InputOutputIntArray, 1, /* Stack */ 0, 1, InputInt, 0, /* Stack */ 0, 456, true);
					IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK_ARRAY(0, InputOutputIntArray, 0, /* Stack */ 1, 1, InputOutputIntArray, 0, /* Stack */ 1, 789, true);
					IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK_ARRAY(0, InputOutputIntArray, 0, /* Stack */ 1, 1, InputOutputIntArray, 1, /* Stack */ 1, 789, true);
					IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK_ARRAY(0, InputOutputIntArray, 1, /* Stack */ 1, 1, InputOutputIntArray, 1, /* Stack */ 1, 789, true);
				});
			});
			
			Describe("Standard Connections", [this]()
			{
				// NOTE: Don't use any number starting at 0, such as 0.123. 0 Is considered not set,
				// and we truncate decimal values when testing. 

				// Integer
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputInt, 25, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputStr, 6, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputFloat, 29001, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputOutputStr, 201, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, BothInt, 42, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputDouble, 123, true);

				// Float 
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputFloat, 201.1534f, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputInt, 6.125143f, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputStr, 5.0083f, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputOutputStr, 3.14159f, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, BothInt, 2.71828f, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputDouble, 3.14159f, true);

				// Double
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputDouble, 123.456, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputStr, 789.012, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputFloat, 345.678, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputInt, 567.890, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputOutputStr, 901.234, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, BothInt, 432.109, true);

				// String
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, InputStr, "OutputStr to InputStr", true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, InputOutputStr, "OutputStr to InputOutputStr",
				                                    true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, InputInt, "5", true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, BothInt, "202", true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, InputFloat, "3.1415", true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, InputDouble, "123.456", true);

				// Text
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputText, 1, InputText,
				                                    NSLOCTEXT("OutputTest", "OutputText", "Localized Output"), true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, OutputText, 1, InputStr,
				                                    NSLOCTEXT("OutputTest", "OutputText", "Localized Output"), true);

				// Both direction type tests
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, InputOutputStr, 1, InputOutputStr,
													"InputOutputStr to InputOutputStr", true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, BothInt, 1, InputOutputStr, 256, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, InputOutputStr, 1, BothInt, "512", true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME(0, BothInt, 1, BothInt, 358, true);
			});

			Describe("Array Connections", [this]()
			{
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_ARRAY(0, InputOutputIntArray, 0, 1, InputInt, 0, 7, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_ARRAY(0, InputOutputIntArray, 1, 1, InputInt, 0, 8, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_ARRAY(0, InputOutputIntArray, 0, 1, InputOutputIntArray, 0, 45, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_ARRAY(0, InputOutputIntArray, 1, 1, InputOutputIntArray, 1, 46, true);
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_ARRAY(0, InputOutputIntArray, 1, 1, InputOutputIntArray, 0, 47, true);
			});

			Describe("Fail Connections (incorrect directions or no conversion)", [this]()
			{
				IT_CONNECT_STATE_PROPS(0, InputInt, 1, InputInt, false);
				IT_CONNECT_STATE_PROPS(0, OutputInt, 1, OutputInt, false);

				IT_CONNECT_STATE_PROPS(0, OutputText, 1, InputInt, false);
				IT_CONNECT_STATE_PROPS(0, OutputText, 1, InputFloat, false);
				IT_CONNECT_STATE_PROPS(0, OutputText, 1, InputDouble, false);
			});

			Describe("Fail Runtime (Incorrect state evaluation order)", [this]()
			{
				// Try to read output from a future state.
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_CALLBACK(1, OutputInt, 0, InputInt, 358, false, [this]()
				{
					AddExpectedError("Could not locate property data", EAutomationExpectedErrorFlags::Contains, 0);
				});
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_CALLBACK(1, OutputStr, 0, InputStr, 358, false, [this]()
				{
					AddExpectedError("Could not locate property data", EAutomationExpectedErrorFlags::Contains, 0);
				});
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_CALLBACK(1, InputOutputStr, 0, InputOutputStr, 358, false, [this]()
				{
					AddExpectedError("Could not locate property data", EAutomationExpectedErrorFlags::Contains, 0);
				});
				IT_CONNECT_STATE_PROPS_TEST_RUNTIME_CALLBACK(1, BothInt, 0, InputOutputStr, 358, false, [this]()
				{
					AddExpectedError("Could not locate property data", EAutomationExpectedErrorFlags::Contains, 0);
				});
			});

			Describe("Unsupported Property Types", [this]()
			{
				IT_CONNECT_STATE_PROPS(0, TextGraph, 1, TextGraph, false);
				IT_CONNECT_STATE_PROPS(0, InputOutputStr, 1, TextGraph, false);
			});
		});

		Describe("1 to 2 Connections (Between 2 states)", [this]()
		{
			Describe("Standard Connections", [this]()
			{
				// NOTE: Don't use any number starting at 0, such as 0.123. 0 Is considered not set,
				// and we truncate decimal values when testing. 

				// Integer output to multiple numeric inputs
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputInt, 25, 0, OutputInt, 1, InputFloat, 25);
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputDouble, 42, 0, OutputInt, 1, BothInt, 42);

				// Float output to multiple numeric inputs
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputFloat, 3.14159f, 0, OutputFloat, 1,
														InputInt, 3.14159f);
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputDouble, 2.71828f, 0, OutputFloat, 1,
														BothInt, 2.71828f);

				// Double output to multiple numeric inputs
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputDouble, 123.456, 0, OutputDouble, 1,
														InputFloat, 123.456);
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputInt, 789.012, 0, OutputDouble, 1, BothInt,
														789.012);

				// String output to multiple string and numeric inputs
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, InputStr, "Test String", 0, OutputStr, 1,
														InputOutputStr, "Test String");
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, InputInt, "42", 0, OutputStr, 1, InputFloat,
														"3.14159");

				// Text output to multiple text and string inputs
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, OutputText, 1, InputText,
														NSLOCTEXT("OutputTest", "OutputText1", "First Output"),
														0, OutputText, 1, InputStr,
														NSLOCTEXT("OutputTest", "OutputText2", "Second Output"));

				// Both direction type connecting to multiple inputs
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, BothInt, 1, InputInt, 256, 0, BothInt, 1, InputOutputStr, 256);

				// InputOutput variables tests
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, InputOutputStr, 1, InputFloat, "123.456", 0, InputOutputStr, 1, InputInt, "789");
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, InputOutputStr, 1, InputDouble, "3.14159", 0, InputOutputStr, 1, BothInt, "42");
				IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(0, InputOutputStr, 1, InputText, "Test InputOutput Text", 0, InputOutputStr, 1, InputStr, "Test String");
			});
		});

		Describe("1 to 3 Connections (Between 3 states)", [this]()
		{
			// NOTE: Don't use any number starting at 0, such as 0.123. 0 Is considered not set,
			// and we truncate decimal values when testing. 

			Describe("Standard Connections", [this]()
			{
				// Integer output to multiple numeric inputs
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputInt, 25, 0, OutputInt, 1, InputFloat, 25, 0, OutputInt, 2, InputInt, 25);
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputDouble, 42, 0, OutputInt, 1, BothInt, 42, 0, OutputInt, 2, InputInt, 42);
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputInt, 1, InputStr, "123", 0, OutputInt, 1, InputFloat, 123, 0, OutputInt, 2, InputDouble, 123);

				// Float output to multiple numeric inputs
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputFloat, 3.14159f, 0, OutputFloat, 1, InputInt, 3.14159f, 0, OutputFloat, 2, InputDouble, 3.14159f);
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputStr, "2.71828", 0, OutputFloat, 1, BothInt, 2.71828f, 0, OutputFloat, 2, InputFloat, 2.71828f);
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputFloat, 1, InputDouble, 1.41421f, 0, OutputFloat, 1, InputOutputStr, "1.41421", 0, OutputFloat, 2, InputInt, 1.41421f);

				// Double output to multiple numeric and string inputs
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputDouble, 123.456, 0, OutputDouble, 1, InputFloat, 123.456, 0, OutputDouble, 2, InputInt, 123.456);
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputStr, "789.012", 0, OutputDouble, 1, BothInt, 789.012, 0, OutputDouble, 2, InputOutputStr, "789.012");
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputDouble, 1, InputDouble, 456.789, 0, OutputDouble, 1, InputInt, 456.789, 0, OutputDouble, 2, InputFloat, 456.789);

				// String output to multiple string and numeric inputs
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, InputStr, "Test String 1", 0, OutputStr, 1, InputOutputStr, "Test String 1", 0, OutputStr, 2, InputStr, "Test String 1");
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, InputInt, "42", 0, OutputStr, 1, InputFloat, "3.14159", 0, OutputStr, 2, InputDouble, "2.71828");
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputStr, 1, BothInt, "256", 0, OutputStr, 1, InputOutputStr, "256", 0, OutputStr, 2, InputStr, "256");

				// Text output to multiple text and string inputs
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, OutputText, 1, InputText, NSLOCTEXT("OutputTest", "OutputText1", "First Output"), 0, OutputText, 1, InputStr, NSLOCTEXT("OutputTest", "OutputText1", "First Output"), 0, OutputText, 2, InputText, NSLOCTEXT("OutputTest", "OutputText1", "First Output"));
				
				// Both direction type connecting to multiple inputs
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, BothInt, 1, InputInt, 512, 0, BothInt, 1, InputFloat, 512, 0, BothInt, 2, InputDouble, 512);
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, BothInt, 1, InputStr, "1024", 0, BothInt, 1, InputOutputStr, "1024", 0, BothInt, 2, BothInt, 1024);

				// InputOutput string to multiple inputs
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, InputOutputStr, 1, InputStr, "IO String Test", 0, InputOutputStr, 1, InputOutputStr, "IO String Test", 0, InputOutputStr, 2, InputText, "IO String Test");
				IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(0, InputOutputStr, 1, InputInt, "789", 0, InputOutputStr, 1, InputFloat, "7.89", 0, InputOutputStr, 2, InputDouble, "7.89");
			});
		});
	}

	/**
	 * Test that a property connection between two graph nodes succeeds or fails as expected.
	 * Uses ValidatePropertyConnection for reader/writer validation, consistent with ConnectProps.
	 */
	bool TestNodeConnection(const USMGraphNode_Base* InSrcNode, const FName& SrcPropName,
		const USMGraphNode_Base* InDstNode, const FName& DstPropName, bool bExpected)
	{
		using namespace LD::Editor::PropertyUtils::IO;

		USMGraphK2Node_PropertyNode_Base* Src = InSrcNode->GetGraphPropertyNode(SrcPropName);
		if (!TestNotNull(FString::Printf(TEXT("Source property %s found"), *SrcPropName.ToString()), Src))
		{
			return false;
		}

		USMGraphK2Node_PropertyNode_Base* Dst = InDstNode->GetGraphPropertyNode(DstPropName);
		if (!TestNotNull(FString::Printf(TEXT("Destination property %s found"), *DstPropName.ToString()), Dst))
		{
			return false;
		}

		bool bConnected = TryMakePropertyConnection(Src, Dst);

		if (bConnected && !ValidatePropertyConnection(Src, SrcPropName, Dst, DstPropName))
		{
			bConnected = false;
		}

		TestEqual(FString::Printf(TEXT("Connection %s→%s"), *SrcPropName.ToString(), *DstPropName.ToString()),
			bConnected, bExpected);

		return bConnected;
	}

END_DEFINE_SPEC(FOutputVariableSpec)

void FOutputVariableSpec::Define()
{
	Describe("Output variable cache", [this]()
	{
		BeforeEach([this]()
		{
			SPEC_TEST_BEFORE_EACH()
		});

		It("is false without output variables", [this]()
		{
			// ——— Setup new state machine ———
			LD::Tests::Setup::ParentAssetClass = USMOutputVariablesSMInstance::StaticClass();
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2) // Do two states just so the test can switch to an end state.

			TestFalse("Output Variables cache not set",
			          FSMBlueprintEditorUtils::DoesBlueprintHaveOutputVariables(NewBP));

			OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(USMOutputVariablesStateInstance_NO_OUTPUT::StaticClass());

			TestFalse("Output Variables cache not set",
			          FSMBlueprintEditorUtils::DoesBlueprintHaveOutputVariables(NewBP));
		});

		It("is true with output variables", [this]()
		{
			// ——— Setup new state machine ———
			LD::Tests::Setup::ParentAssetClass = USMOutputVariablesSMInstance::StaticClass();
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2) // Do two states just so the test can switch to an end state.

			TestFalse("Output Variables cache not set",
			          FSMBlueprintEditorUtils::DoesBlueprintHaveOutputVariables(NewBP));

			OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(USMOutputVariablesStateInstance::StaticClass());

			TestTrue("Output Variables cache set", FSMBlueprintEditorUtils::DoesBlueprintHaveOutputVariables(NewBP));
		});
	});

	Describe("Owning blueprint connections", [this]()
	{
		/** Properties defined in native instance classes only, inherited to BP. */
		Describe("Native properties", [this]()
		{
			BeforeEach([this]()
			{
				SPEC_TEST_BEFORE_EACH()

				// ——— Setup new state machine ———
				LD::Tests::Setup::ParentAssetClass = USMOutputVariablesSMInstance::StaticClass();
				SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2) // Do two states just so the test can switch to an end state.

				OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(USMOutputVariablesStateInstance::StaticClass());
			});

			TestOwningBlueprintConnectionsBody();
		});

		/** Properties defined in BP only. */
		Describe("Blueprint properties", [this]()
		{
			BeforeEach([this]()
			{
				SPEC_TEST_BEFORE_EACH()

				// ——— Setup new state machine ———
				LD::Tests::Setup::ParentAssetClass = USMInstance::StaticClass();
				LD::Tests::Setup::BlueprintTemplateClass = USMOutputVariablesSMInstance::StaticClass();
				SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(OutputVarStateCount) // Do two states just so the test can switch to an end state.

				OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(USMOutputVariablesStateInstance::StaticClass());
			});

			TestOwningBlueprintConnectionsBody();
		});
	});

	Describe("State to state connections", [this]()
	{
		Describe("Native types", [this]()
		{
			BeforeEach([this]()
			{
				SPEC_TEST_BEFORE_EACH()

				// ——— Setup new state machine ———
				SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(OutputVarStateCount)

				OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(USMOutputVariablesStateInstance::StaticClass());
			});

			TestStateConnectionsBody();
		});

		Describe("Blueprint types", [this]()
		{
			BeforeEach([this]()
			{
				SPEC_TEST_BEFORE_EACH()

				// ——— Setup new state machine ———
				SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(OutputVarStateCount)

				// Create a blueprint class matching our native, recreating its properties strictly in BP.
				TestHelpers::TryCreateNewNodeBlueprintAssetFromNativeTemplate(this,
				                                                              USMOutputVariablesStateInstance::StaticClass(),
				                                                              USMOutputVariablesStateInstance_Base::StaticClass(),
				                                                              NewNodeAsset);

				const USMNodeBlueprint* NodeBlueprint = NewNodeAsset.GetObjectAs<USMNodeBlueprint>();

				OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(NodeBlueprint->GeneratedClass);
			});

			TestStateConnectionsBody();
		});
	});

	Describe("Delete nodes with output variable connections", [this]()
	{
		BeforeEach([this]()
		{
			SPEC_TEST_BEFORE_EACH()
		});

		It("should not crash when deleting a state with connections", [this]()
		{
			using namespace LD::Editor::PropertyUtils::IO;

			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(3)
			OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(USMOutputVariablesStateInstance::StaticClass());

			// Connect state 0 OutputInt → state 1 InputInt.
			USMGraphK2Node_PropertyNode_Base* Src = GetProp(0, GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputInt));
			USMGraphK2Node_PropertyNode_Base* Dst = GetProp(1, GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, InputInt));
			TestTrue(TEXT("Connection made"), TryMakePropertyConnection(Src, Dst));

			// Delete state 0 (the source of the output connection).
			FBlueprintEditorUtils::RemoveNode(NewBP, StateNodes[0]);

			// Verify no crash occurred and the blueprint is still functional.
			FKismetEditorUtilities::CompileBlueprint(NewBP);
		});

		It("should not crash when deleting a conduit with connections", [this]()
		{
			using namespace LD::Editor::PropertyUtils::IO;

			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(3)
			OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(USMOutputVariablesStateInstance::StaticClass());

			// Convert state 1 to a conduit with output variable properties.
			USMGraphNode_ConduitNode* LocalConduitNode = FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_ConduitNode>(StateNodes[1]);
			TestHelpers::SetNodeClass(this, LocalConduitNode, USMOutputVariablesConduitInstance::StaticClass());

			// Connect state 0 OutputInt → conduit InputInt.
			USMGraphK2Node_PropertyNode_Base* Src = GetProp(0, GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputInt));
			USMGraphK2Node_PropertyNode_Base* Dst = LocalConduitNode->GetGraphPropertyNode(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesConduitInstance, InputInt));
			TestNotNull(TEXT("Conduit property node found"), Dst);
			TestTrue(TEXT("Connection made"), TryMakePropertyConnection(Src, Dst));

			// Delete the conduit.
			FBlueprintEditorUtils::RemoveNode(NewBP, LocalConduitNode);

			FKismetEditorUtilities::CompileBlueprint(NewBP);
		});

		It("should not crash when deleting a state machine state with connections", [this]()
		{
			using namespace LD::Editor::PropertyUtils::IO;

			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, 1, &LastStatePin,
				USMOutputVariablesStateInstance::StaticClass());

			// Create a nested state machine node after the first state.
			USMGraphNode_StateMachineStateNode* NestedSMNode =
				TestHelpers::CreateNewNode<USMGraphNode_StateMachineStateNode>(this, StateMachineGraph, LastStatePin);
			TestHelpers::SetNodeClass(this, NestedSMNode, USMOutputVariablesStateMachineInstance::StaticClass());

			// Build a single state inside the nested SM so it's valid.
			UEdGraphPin* NestedPin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, Cast<USMGraph>(NestedSMNode->GetBoundGraph()), 1, &NestedPin);

			// Gather state nodes for GetProp.
			StateNodes.Empty();
			FSMBlueprintEditorUtils::GetAllNodesOfClassNested(NewBP, StateNodes);

			// Connect state 0 OutputInt → nested SM InputInt.
			USMGraphK2Node_PropertyNode_Base* Src = GetProp(0, GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputInt));
			USMGraphK2Node_PropertyNode_Base* Dst = NestedSMNode->GetGraphPropertyNode(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateMachineInstance, InputInt));
			TestNotNull(TEXT("Nested SM property node found"), Dst);
			TestTrue(TEXT("Connection made"), TryMakePropertyConnection(Src, Dst));

			// Delete the nested state machine node.
			FBlueprintEditorUtils::RemoveNode(NewBP, NestedSMNode);

			FKismetEditorUtilities::CompileBlueprint(NewBP);
		});

		It("should not crash when deleting a state machine reference with connections", [this]()
		{
			using namespace LD::Editor::PropertyUtils::IO;

			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			UEdGraphPin* LastStatePin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, 1, &LastStatePin,
				USMOutputVariablesStateInstance::StaticClass());

			// Create a nested state machine, then convert it to a reference.
			USMGraphNode_StateMachineStateNode* NestedSMNode =
				TestHelpers::CreateNewNode<USMGraphNode_StateMachineStateNode>(this, StateMachineGraph, LastStatePin);

			UEdGraphPin* NestedPin = nullptr;
			TestHelpers::BuildLinearStateMachine(this, Cast<USMGraph>(NestedSMNode->GetBoundGraph()), 1, &NestedPin);

			USMBlueprint* ReferencedBlueprint = FSMBlueprintEditorUtils::ConvertStateMachineToReference(
				NestedSMNode, false, nullptr, nullptr);
			TestNotNull(TEXT("Reference blueprint created"), ReferencedBlueprint);
			FKismetEditorUtilities::CompileBlueprint(ReferencedBlueprint);

			// Set a node class with output variable properties on the reference.
			TestHelpers::SetNodeClass(this, NestedSMNode, USMOutputVariablesStateMachineInstance::StaticClass());

			// Gather state nodes for GetProp.
			StateNodes.Empty();
			FSMBlueprintEditorUtils::GetAllNodesOfClassNested(NewBP, StateNodes);

			// Connect state 0 OutputInt → reference InputInt.
			USMGraphK2Node_PropertyNode_Base* Src = GetProp(0, GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, OutputInt));
			USMGraphK2Node_PropertyNode_Base* Dst = NestedSMNode->GetGraphPropertyNode(
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateMachineInstance, InputInt));
			TestNotNull(TEXT("Reference property node found"), Dst);
			TestTrue(TEXT("Connection made"), TryMakePropertyConnection(Src, Dst));

			// Delete the state machine reference.
			FBlueprintEditorUtils::RemoveNode(NewBP, NestedSMNode);

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			// Clean up the referenced blueprint asset.
			FAssetHandler RefAsset = TestHelpers::CreateAssetFromBlueprint(ReferencedBlueprint);
			RefAsset.DeleteAsset(this);
		});
	});

	Describe("Conduit connections", [this]()
	{
		BeforeEach([this]()
		{
			SPEC_TEST_BEFORE_EACH()
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(OutputVarStateCount)
			OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(USMOutputVariablesStateInstance::StaticClass());

			// Convert middle state to conduit with output variable properties.
			ConduitNode = FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_ConduitNode>(StateNodes[1]);
			TestHelpers::SetNodeClass(this, ConduitNode, USMOutputVariablesConduitInstance::StaticClass());

			// Re-fetch state nodes (conduit excluded from USMGraphNode_StateNode query).
			StateNodes.Empty();
			FSMBlueprintEditorUtils::GetAllNodesOfClassNested(NewBP, StateNodes);
			check(StateNodes.Num() == 2);
		});

		Describe("State to conduit", [this]()
		{
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputInt,
				ConduitNode, USMOutputVariablesConduitInstance, InputInt, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputFloat,
				ConduitNode, USMOutputVariablesConduitInstance, InputFloat, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputDouble,
				ConduitNode, USMOutputVariablesConduitInstance, InputDouble, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputStr,
				ConduitNode, USMOutputVariablesConduitInstance, InputStr, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputText,
				ConduitNode, USMOutputVariablesConduitInstance, InputText, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, BothInt,
				ConduitNode, USMOutputVariablesConduitInstance, BothInt, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, InputOutputStr,
				ConduitNode, USMOutputVariablesConduitInstance, InputOutputStr, true);

			// Cross-type connections
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputInt,
				ConduitNode, USMOutputVariablesConduitInstance, InputFloat, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputStr,
				ConduitNode, USMOutputVariablesConduitInstance, InputInt, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputDouble,
				ConduitNode, USMOutputVariablesConduitInstance, InputFloat, true);
		});

		Describe("Conduit to state", [this]()
		{
			IT_NODE_CONNECTION(ConduitNode, USMOutputVariablesConduitInstance, OutputInt,
				StateNodes[1], USMOutputVariablesStateInstance, InputInt, true);
			IT_NODE_CONNECTION(ConduitNode, USMOutputVariablesConduitInstance, OutputFloat,
				StateNodes[1], USMOutputVariablesStateInstance, InputFloat, true);
			IT_NODE_CONNECTION(ConduitNode, USMOutputVariablesConduitInstance, OutputDouble,
				StateNodes[1], USMOutputVariablesStateInstance, InputDouble, true);
			IT_NODE_CONNECTION(ConduitNode, USMOutputVariablesConduitInstance, OutputStr,
				StateNodes[1], USMOutputVariablesStateInstance, InputStr, true);
			IT_NODE_CONNECTION(ConduitNode, USMOutputVariablesConduitInstance, OutputText,
				StateNodes[1], USMOutputVariablesStateInstance, InputText, true);
			IT_NODE_CONNECTION(ConduitNode, USMOutputVariablesConduitInstance, BothInt,
				StateNodes[1], USMOutputVariablesStateInstance, BothInt, true);
			IT_NODE_CONNECTION(ConduitNode, USMOutputVariablesConduitInstance, InputOutputStr,
				StateNodes[1], USMOutputVariablesStateInstance, InputOutputStr, true);
		});

		Describe("Fail Connections", [this]()
		{
			// Same direction should fail.
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, InputInt,
				ConduitNode, USMOutputVariablesConduitInstance, InputInt, false);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputInt,
				ConduitNode, USMOutputVariablesConduitInstance, OutputInt, false);

			// No conversion available.
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputText,
				ConduitNode, USMOutputVariablesConduitInstance, InputInt, false);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputText,
				ConduitNode, USMOutputVariablesConduitInstance, InputFloat, false);

			// Unsupported property type.
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, TextGraph,
				ConduitNode, USMOutputVariablesConduitInstance, TextGraph, false);
		});
	});

	Describe("State machine state connections", [this]()
	{
		BeforeEach([this]()
		{
			SPEC_TEST_BEFORE_EACH()
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(OutputVarStateCount)
			OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(USMOutputVariablesStateInstance::StaticClass());

			// Convert middle state to a state machine state node.
			StateMachineStateNode = FSMBlueprintEditorUtils::ConvertNodeTo<USMGraphNode_StateMachineStateNode>(StateNodes[1]);
			TestHelpers::SetNodeClass(this, StateMachineStateNode, USMOutputVariablesStateMachineInstance::StaticClass());

			// Re-fetch state nodes (SM state excluded from USMGraphNode_StateNode query).
			StateNodes.Empty();
			FSMBlueprintEditorUtils::GetAllNodesOfClassNested(NewBP, StateNodes);
			check(StateNodes.Num() == 2);
		});

		Describe("State to state machine state", [this]()
		{
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputInt,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputInt, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputFloat,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputFloat, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputDouble,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputDouble, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputStr,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputStr, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputText,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputText, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, BothInt,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, BothInt, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, InputOutputStr,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputOutputStr, true);

			// Cross-type connections
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputInt,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputFloat, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputStr,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputInt, true);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputDouble,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputFloat, true);
		});

		Describe("State machine state to state", [this]()
		{
			IT_NODE_CONNECTION(StateMachineStateNode, USMOutputVariablesStateMachineInstance, OutputInt,
				StateNodes[1], USMOutputVariablesStateInstance, InputInt, true);
			IT_NODE_CONNECTION(StateMachineStateNode, USMOutputVariablesStateMachineInstance, OutputFloat,
				StateNodes[1], USMOutputVariablesStateInstance, InputFloat, true);
			IT_NODE_CONNECTION(StateMachineStateNode, USMOutputVariablesStateMachineInstance, OutputDouble,
				StateNodes[1], USMOutputVariablesStateInstance, InputDouble, true);
			IT_NODE_CONNECTION(StateMachineStateNode, USMOutputVariablesStateMachineInstance, OutputStr,
				StateNodes[1], USMOutputVariablesStateInstance, InputStr, true);
			IT_NODE_CONNECTION(StateMachineStateNode, USMOutputVariablesStateMachineInstance, OutputText,
				StateNodes[1], USMOutputVariablesStateInstance, InputText, true);
			IT_NODE_CONNECTION(StateMachineStateNode, USMOutputVariablesStateMachineInstance, BothInt,
				StateNodes[1], USMOutputVariablesStateInstance, BothInt, true);
			IT_NODE_CONNECTION(StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputOutputStr,
				StateNodes[1], USMOutputVariablesStateInstance, InputOutputStr, true);
		});

		Describe("Fail Connections", [this]()
		{
			// Same direction should fail.
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, InputInt,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputInt, false);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputInt,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, OutputInt, false);

			// No conversion available.
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputText,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputInt, false);
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, OutputText,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, InputFloat, false);

			// Unsupported property type.
			IT_NODE_CONNECTION(StateNodes[0], USMOutputVariablesStateInstance, TextGraph,
				StateMachineStateNode, USMOutputVariablesStateMachineInstance, TextGraph, false);
		});
	});

	AfterEach([this]()
	{
		SPEC_TEST_AFTER_EACH()

		ConduitNode = nullptr;
		StateMachineStateNode = nullptr;

		// DeleteAsset does not clean up transient packages that have no on-disk file.
		// Use UnloadAsset to fully purge them from memory, preventing PIE startup in
		// later tests from recompiling stale dirty blueprints.
		NewAsset.UnloadAsset();
		if (NewNodeAsset.GetObject())
		{
			NewNodeAsset.UnloadAsset();
		}
	});
}

#endif

#endif //WITH_DEV_AUTOMATION_TESTS
