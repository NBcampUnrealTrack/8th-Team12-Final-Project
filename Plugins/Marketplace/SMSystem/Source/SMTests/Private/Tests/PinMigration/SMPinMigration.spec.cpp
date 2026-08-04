// Copyright Recursoft LLC. All Rights Reserved.

#include "SMPinMigrationTestClasses.h"

#include "Helpers/SMTestBoilerplate.h"
#include "Helpers/SMTestHelpers.h"

#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/SMGraph.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Blueprints/SMBlueprint.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "UObject/Class.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FPinMigrationSpec,
                  "LogicDriver.NodeInstance.Variables.PropertyPinMigration",
                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::
                  EngineFilter)
	SETUP_SPEC_TEST_DEFINES()

	FAssetHandler NodeBPAsset;

	/**
	 * Tests drive BP variables because the rename migration only fires on BP-defined
	 * properties (their MemberGuid is stable across rename; native FProperty names are not).
	 */
	bool CreateNodeInstanceBlueprint()
	{
		return TestHelpers::TryCreateNewNodeBlueprintAssetFromNativeTemplate(this,
			USMPinMigrationTestState::StaticClass(),
			USMStateInstance::StaticClass(),
			NodeBPAsset);
	}

	USMGraphNode_StateNode* BuildAndGetFirstStateNode()
	{
		USMNodeBlueprint* NodeBP = NodeBPAsset.GetObjectAs<USMNodeBlueprint>();
		if (!TestNotNull(TEXT("Node instance BP exists"), NodeBP))
		{
			return nullptr;
		}

		UEdGraphPin* LastStatePin = nullptr;
		TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin,
			NodeBP->GeneratedClass);

		TArray<USMGraphNode_StateNode*> FoundStateNodes;
		FSMBlueprintEditorUtils::GetAllNodesOfClassNested<USMGraphNode_StateNode>(StateMachineGraph, FoundStateNodes);
		if (!TestTrue(TEXT("State nodes found"), FoundStateNodes.Num() > 0))
		{
			return nullptr;
		}
		return FoundStateNodes[0];
	}

	USMGraphK2Node_PropertyNode_Base* FindPropertyNode(const USMGraphNode_StateNode* InStateNode,
		const FName InVariableName, const int32 InArrayIndex = INDEX_NONE)
	{
		USMGraphK2Node_PropertyNode_Base* PropertyNode = InStateNode->GetGraphPropertyNode(
			InVariableName, static_cast<const USMNodeInstance*>(nullptr), InArrayIndex);
		TestNotNull(
			FString::Printf(TEXT("Property node found for '%s' index %d"),
				*InVariableName.ToString(), InArrayIndex),
			PropertyNode);
		return PropertyNode;
	}

	void SetResultPinDefaultValue(USMGraphK2Node_PropertyNode_Base* InPropertyNode, const FString& InValue)
	{
		UEdGraphPin* ResultPin = InPropertyNode->GetResultPin(EGPD_Input);
		if (!TestNotNull(TEXT("Result input pin exists before setting default"), ResultPin))
		{
			return;
		}
		ResultPin->GetSchema()->TrySetDefaultValue(*ResultPin, InValue);
	}

	void SetResultPinDefaultObject(USMGraphK2Node_PropertyNode_Base* InPropertyNode, UObject* InObject)
	{
		UEdGraphPin* ResultPin = InPropertyNode->GetResultPin(EGPD_Input);
		if (!TestNotNull(TEXT("Result input pin exists before setting default object"), ResultPin))
		{
			return;
		}
		ResultPin->GetSchema()->TrySetDefaultObject(*ResultPin, InObject);
	}

	/** Wraps a MakeLiteral* function in a UK2Node_CallFunction and wires its return pin to the result pin. */
	UK2Node_CallFunction* CreateLiteralSourceAndWire(USMGraphK2Node_PropertyNode_Base* InPropertyNode,
		UFunction* InLiteralFunction, const FString& InLiteralValue)
	{
		UEdGraph* PropertyGraph = InPropertyNode->GetGraph();
		if (!TestNotNull(TEXT("Property graph exists"), PropertyGraph))
		{
			return nullptr;
		}

		UEdGraphPin* ResultPin = InPropertyNode->GetResultPin(EGPD_Input);
		if (!TestNotNull(TEXT("Result input pin exists before wiring source"), ResultPin))
		{
			return nullptr;
		}

		UK2Node_CallFunction* LiteralNode = NewObject<UK2Node_CallFunction>(PropertyGraph);
		LiteralNode->CreateNewGuid();
		LiteralNode->PostPlacedNewNode();
		LiteralNode->SetFromFunction(InLiteralFunction);
		LiteralNode->SetFlags(RF_Transactional);
		LiteralNode->AllocateDefaultPins();
		UEdGraphSchema_K2::SetNodeMetaData(LiteralNode, FNodeMetadata::DefaultGraphNode);
		PropertyGraph->AddNode(LiteralNode);

		if (UEdGraphPin* LiteralInputPin = LiteralNode->FindPin(TEXT("Value"), EGPD_Input))
		{
			LiteralInputPin->GetSchema()->TrySetDefaultValue(*LiteralInputPin, InLiteralValue);
		}

		UEdGraphPin* LiteralOutPin = nullptr;
		for (UEdGraphPin* Pin : LiteralNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType == ResultPin->PinType)
			{
				LiteralOutPin = Pin;
				break;
			}
		}

		if (!TestNotNull(TEXT("Literal node output pin of matching type"), LiteralOutPin))
		{
			return nullptr;
		}

		const UEdGraphSchema_K2* K2Schema = CastChecked<UEdGraphSchema_K2>(PropertyGraph->GetSchema());
		TestTrue(TEXT("Connected literal output pin to property result pin"),
			K2Schema->TryCreateConnection(LiteralOutPin, ResultPin));

		return LiteralNode;
	}

	/**
	 * Renames the BP variable and compiles both BPs. Both compiles are required: the
	 * node BP to regenerate its class, then the state machine BP to reconstruct its
	 * property nodes against the new class (which creates the orphans the migration handles).
	 */
	void RenameVariableAndCompileAll(const FName InOldName, const FName InNewName)
	{
		USMNodeBlueprint* NodeBP = NodeBPAsset.GetObjectAs<USMNodeBlueprint>();
		if (!TestNotNull(TEXT("Node instance BP exists for rename"), NodeBP))
		{
			return;
		}

		FBlueprintEditorUtils::RenameMemberVariable(NodeBP, InOldName, InNewName);
		FKismetEditorUtilities::CompileBlueprint(NodeBP);
		FKismetEditorUtilities::CompileBlueprint(NewBP);
	}

	/**
	 * Mutates VarType directly. FBlueprintEditorUtils::ChangeMemberVariableType shows a
	 * modal confirmation dialog that breaks unattended automation.
	 */
	void ChangeVariableTypeAndCompileAll(const FName InVariableName, const FEdGraphPinType& InNewType)
	{
		USMNodeBlueprint* NodeBP = NodeBPAsset.GetObjectAs<USMNodeBlueprint>();
		if (!TestNotNull(TEXT("Node instance BP exists for type change"), NodeBP))
		{
			return;
		}

		const int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(NodeBP, InVariableName);
		if (!TestTrue(
			FString::Printf(TEXT("Variable '%s' exists in BP"), *InVariableName.ToString()),
			VarIdx != INDEX_NONE))
		{
			return;
		}

		NodeBP->NewVariables[VarIdx].VarType = InNewType;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(NodeBP);
		FKismetEditorUtilities::CompileBlueprint(NodeBP);
		FKismetEditorUtilities::CompileBlueprint(NewBP);
	}

	/** Combined rename + retype — exercises the migration's type-mismatch guard path. */
	void RenameAndRetypeAndCompileAll(const FName InOldName, const FName InNewName,
		const FEdGraphPinType& InNewType)
	{
		USMNodeBlueprint* NodeBP = NodeBPAsset.GetObjectAs<USMNodeBlueprint>();
		if (!TestNotNull(TEXT("Node instance BP exists for rename+retype"), NodeBP))
		{
			return;
		}

		const int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(NodeBP, InOldName);
		if (!TestTrue(
			FString::Printf(TEXT("Variable '%s' exists in BP"), *InOldName.ToString()),
			VarIdx != INDEX_NONE))
		{
			return;
		}

		// Mutating VarType in place and then recompiling the BP can cause the engine
		// to log a benign LogClass: "Type mismatch in <Var> ..." warning while it
		// reconstructs the class CDO across the property type change. The warning is
		// observed on UE 5.5/5.6 for non-numeric type pairs (e.g. int -> string) and
		// not on UE 5.7. Since changing the property type here is more related to the
		// engine, it's fine to just ignore.
		LOG_SCOPE_VERBOSITY_OVERRIDE(LogClass, ELogVerbosity::Error);

		NodeBP->NewVariables[VarIdx].VarType = InNewType;
		FBlueprintEditorUtils::RenameMemberVariable(NodeBP, InOldName, InNewName);
		FKismetEditorUtilities::CompileBlueprint(NodeBP);
		FKismetEditorUtilities::CompileBlueprint(NewBP);
	}

	int32 CountOrphanInputPins(const USMGraphK2Node_PropertyNode_Base* InPropertyNode)
	{
		int32 Count = 0;
		for (const UEdGraphPin* Pin : InPropertyNode->Pins)
		{
			if (Pin && Pin->bOrphanedPin && Pin->Direction == EGPD_Input)
			{
				++Count;
			}
		}
		return Count;
	}

	FEdGraphPinType MakePinType(const FName InCategory, const FName InSubCategory = NAME_None)
	{
		FEdGraphPinType Type;
		Type.PinCategory = InCategory;
		Type.PinSubCategory = InSubCategory;
		Type.PinSubCategoryObject = nullptr;
		Type.ContainerType = EPinContainerType::None;
		Type.bIsReference = false;
		return Type;
	}
END_DEFINE_SPEC(FPinMigrationSpec)

void FPinMigrationSpec::Define()
{
	BeforeEach([this]()
	{
		SPEC_TEST_BEFORE_EACH()
	});

	AfterEach([this]()
	{
		SPEC_TEST_AFTER_EACH()
		NodeBPAsset.UnloadAsset();
		NewAsset.UnloadAsset();
	});

	Describe("Default value migration on rename", [this]()
	{
		It("should migrate an int32 default value", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("42"));

			RenameVariableAndCompileAll(TEXT("IntVar"), TEXT("RenamedInt"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedInt"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Renamed int pin inherited the migrated default value"),
				NewPin->DefaultValue, FString(TEXT("42")));
		});

		It("should migrate a float default value", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("FloatVar"));
			if (!PropertyNode)
			{
				return;
			}
			// Pre-normalized form avoids mismatch after the compile pipeline reformats floats.
			SetResultPinDefaultValue(PropertyNode, TEXT("3.140000"));

			RenameVariableAndCompileAll(TEXT("FloatVar"), TEXT("RenamedFloat"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedFloat"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed float result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Renamed float pin inherited the migrated default value"),
				NewPin->DefaultValue, FString(TEXT("3.140000")));
		});

		It("should migrate a bool default value", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("bBoolVar"));
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("true"));

			RenameVariableAndCompileAll(TEXT("bBoolVar"), TEXT("bRenamedBool"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("bRenamedBool"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed bool result pin exists"), NewPin))
			{
				return;
			}
			TestTrue(TEXT("Renamed bool pin inherited the migrated default value"),
				NewPin->DefaultValue.ToBool());
		});

		It("should migrate a string default value", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("StringVar"));
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("Hello world"));

			RenameVariableAndCompileAll(TEXT("StringVar"), TEXT("RenamedString"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedString"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed string result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Renamed string pin inherited the migrated default value"),
				NewPin->DefaultValue, FString(TEXT("Hello world")));
		});

		It("should migrate a name default value", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("NameVar"));
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("SomeName"));

			RenameVariableAndCompileAll(TEXT("NameVar"), TEXT("RenamedName"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedName"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed name result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Renamed name pin inherited the migrated default value"),
				NewPin->DefaultValue, FString(TEXT("SomeName")));
		});

		It("should migrate a vector struct default value", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("VectorVar"));
			if (!PropertyNode)
			{
				return;
			}

			const FString VectorDefault(TEXT("1.000000,2.000000,3.000000"));
			SetResultPinDefaultValue(PropertyNode, VectorDefault);

			RenameVariableAndCompileAll(TEXT("VectorVar"), TEXT("RenamedVector"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedVector"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed vector result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Renamed vector pin inherited the migrated default value"),
				NewPin->DefaultValue, VectorDefault);
		});

		It("should migrate an object reference default value", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("ObjectVar"));
			if (!PropertyNode)
			{
				return;
			}

			UObject* TestObject = UObject::StaticClass()->GetDefaultObject();
			SetResultPinDefaultObject(PropertyNode, TestObject);

			RenameVariableAndCompileAll(TEXT("ObjectVar"), TEXT("RenamedObject"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedObject"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed object result pin exists"), NewPin))
			{
				return;
			}
			TestTrue(TEXT("Renamed object pin inherited the migrated default object"),
				NewPin->DefaultObject == TestObject);
		});

		It("should not affect sibling properties when only one is renamed", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* IntNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			USMGraphK2Node_PropertyNode_Base* FloatNode = FindPropertyNode(StateNode, TEXT("FloatVar"));
			USMGraphK2Node_PropertyNode_Base* StringNode = FindPropertyNode(StateNode, TEXT("StringVar"));
			if (!IntNode || !FloatNode || !StringNode)
			{
				return;
			}

			SetResultPinDefaultValue(IntNode, TEXT("100"));
			SetResultPinDefaultValue(FloatNode, TEXT("2.500000"));
			SetResultPinDefaultValue(StringNode, TEXT("SiblingString"));

			RenameVariableAndCompileAll(TEXT("IntVar"), TEXT("RenamedOnlyInt"));

			const USMGraphK2Node_PropertyNode_Base* RenamedInt = FindPropertyNode(StateNode, TEXT("RenamedOnlyInt"));
			if (RenamedInt)
			{
				const UEdGraphPin* Pin = RenamedInt->GetResultPin(EGPD_Input);
				if (TestNotNull(TEXT("Renamed int pin exists"), Pin))
				{
					TestEqual(TEXT("Renamed int kept its value"), Pin->DefaultValue, FString(TEXT("100")));
				}
			}

			const USMGraphK2Node_PropertyNode_Base* UnchangedFloat = FindPropertyNode(StateNode, TEXT("FloatVar"));
			if (UnchangedFloat)
			{
				const UEdGraphPin* Pin = UnchangedFloat->GetResultPin(EGPD_Input);
				if (TestNotNull(TEXT("Float pin exists"), Pin))
				{
					TestEqual(TEXT("Float default unchanged"), Pin->DefaultValue, FString(TEXT("2.500000")));
				}
			}

			const USMGraphK2Node_PropertyNode_Base* UnchangedString = FindPropertyNode(StateNode, TEXT("StringVar"));
			if (UnchangedString)
			{
				const UEdGraphPin* Pin = UnchangedString->GetResultPin(EGPD_Input);
				if (TestNotNull(TEXT("String pin exists"), Pin))
				{
					TestEqual(TEXT("String default unchanged"), Pin->DefaultValue, FString(TEXT("SiblingString")));
				}
			}
		});
	});

	Describe("Wire migration on rename", [this]()
	{
		It("should migrate a wired int connection", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}

			UFunction* MakeLiteralInt = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
			if (!CreateLiteralSourceAndWire(PropertyNode, MakeLiteralInt, TEXT("7")))
			{
				return;
			}

			RenameVariableAndCompileAll(TEXT("IntVar"), TEXT("RenamedInt"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedInt"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Renamed pin retained its single wire"),
				NewPin->LinkedTo.Num(), 1);
		});

		It("should migrate a wired string connection", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("StringVar"));
			if (!PropertyNode)
			{
				return;
			}

			UFunction* MakeLiteralString = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralString));
			if (!CreateLiteralSourceAndWire(PropertyNode, MakeLiteralString, TEXT("Wired")))
			{
				return;
			}

			RenameVariableAndCompileAll(TEXT("StringVar"), TEXT("RenamedString"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedString"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed string result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Renamed string pin retained its single wire"),
				NewPin->LinkedTo.Num(), 1);
		});

		It("should migrate a wired bool connection", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("bBoolVar"));
			if (!PropertyNode)
			{
				return;
			}

			UFunction* MakeLiteralBool = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralBool));
			if (!CreateLiteralSourceAndWire(PropertyNode, MakeLiteralBool, TEXT("true")))
			{
				return;
			}

			RenameVariableAndCompileAll(TEXT("bBoolVar"), TEXT("bRenamedBool"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("bRenamedBool"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed bool result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Renamed bool pin retained its single wire"),
				NewPin->LinkedTo.Num(), 1);
		});

		It("should preserve bidirectional integrity after migration", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}

			UFunction* MakeLiteralInt = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
			const UK2Node_CallFunction* LiteralNode = CreateLiteralSourceAndWire(PropertyNode, MakeLiteralInt, TEXT("99"));
			if (!LiteralNode)
			{
				return;
			}

			RenameVariableAndCompileAll(TEXT("IntVar"), TEXT("RenamedInt"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedInt"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed result pin exists"), NewPin))
			{
				return;
			}

			const UEdGraphPin* LiteralOutPin = nullptr;
			for (UEdGraphPin* Pin : LiteralNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output && Pin->PinType == NewPin->PinType)
				{
					LiteralOutPin = Pin;
					break;
				}
			}
			if (!TestNotNull(TEXT("Literal output pin found"), LiteralOutPin))
			{
				return;
			}

			bool bLinksBackToNewPin = false;
			for (const UEdGraphPin* LinkedPin : LiteralOutPin->LinkedTo)
			{
				if (LinkedPin == NewPin)
				{
					bLinksBackToNewPin = true;
					break;
				}
			}
			TestTrue(TEXT("Source literal links back to the renamed pin (not an orphan)"),
				bLinksBackToNewPin);
		});

		It("should preserve the source node's literal value through migration", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}

			UFunction* MakeLiteralInt = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
			const UK2Node_CallFunction* LiteralNode = CreateLiteralSourceAndWire(PropertyNode, MakeLiteralInt, TEXT("7"));
			if (!LiteralNode)
			{
				return;
			}

			RenameVariableAndCompileAll(TEXT("IntVar"), TEXT("RenamedInt"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedInt"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Renamed pin retained its wire"), NewPin->LinkedTo.Num(), 1);

			const UEdGraphPin* LiteralInputPin = LiteralNode->FindPin(TEXT("Value"), EGPD_Input);
			if (TestNotNull(TEXT("Literal source 'Value' pin still exists"), LiteralInputPin))
			{
				TestEqual(TEXT("Literal source retained its user-entered value"),
					LiteralInputPin->DefaultValue, FString(TEXT("7")));
			}
		});
	});

	Describe("Array property migration", [this]()
	{
		It("should migrate a single-element array's default value", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntArrayOne"), 0);
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("11"));

			RenameVariableAndCompileAll(TEXT("IntArrayOne"), TEXT("RenamedIntArrayOne"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedIntArrayOne"), 0);
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed array[0] result pin exists"), NewPin))
			{
				return;
			}
			TestEqual(TEXT("Single-element array entry migrated its default"),
				NewPin->DefaultValue, FString(TEXT("11")));
		});

		It("should migrate each entry of a three-element array", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			const FString Values[] = { TEXT("10"), TEXT("20"), TEXT("30") };
			for (int32 Idx = 0; Idx < 3; ++Idx)
			{
				USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntArrayThree"), Idx);
				if (PropertyNode)
				{
					SetResultPinDefaultValue(PropertyNode, Values[Idx]);
				}
			}

			RenameVariableAndCompileAll(TEXT("IntArrayThree"), TEXT("RenamedIntArrayThree"));

			for (int32 Idx = 0; Idx < 3; ++Idx)
			{
				USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedIntArrayThree"), Idx);
				if (!RenamedNode)
				{
					continue;
				}

				const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
				if (!TestNotNull(
					FString::Printf(TEXT("Renamed array[%d] result pin exists"), Idx), NewPin))
				{
					continue;
				}
				TestEqual(
					FString::Printf(TEXT("Array[%d] migrated its default"), Idx),
					NewPin->DefaultValue, Values[Idx]);
			}
		});

		It("should migrate each entry of a five-element float array", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			// Pre-normalized so post-compile reformatting doesn't bite us.
			const FString Values[] = {
				TEXT("1.100000"), TEXT("2.200000"), TEXT("3.300000"), TEXT("4.400000"), TEXT("5.500000")
			};
			for (int32 Idx = 0; Idx < 5; ++Idx)
			{
				USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("FloatArrayFive"), Idx);
				if (PropertyNode)
				{
					SetResultPinDefaultValue(PropertyNode, Values[Idx]);
				}
			}

			RenameVariableAndCompileAll(TEXT("FloatArrayFive"), TEXT("RenamedFloatArrayFive"));

			for (int32 Idx = 0; Idx < 5; ++Idx)
			{
				USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedFloatArrayFive"), Idx);
				if (!RenamedNode)
				{
					continue;
				}

				const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
				if (!TestNotNull(
					FString::Printf(TEXT("Renamed float array[%d] result pin exists"), Idx), NewPin))
				{
					continue;
				}
				TestEqual(
					FString::Printf(TEXT("Float array[%d] migrated its default"), Idx),
					NewPin->DefaultValue, Values[Idx]);
			}
		});
	});

	Describe("Type change handling", [this]()
	{
		It("should not migrate the default value when the type also changes (rename + retype)", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("42"));

			const FEdGraphPinType FloatType = MakePinType(UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Float);
			RenameAndRetypeAndCompileAll(TEXT("IntVar"), TEXT("RenamedToFloat"), FloatType);

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedToFloat"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed float result pin exists"), NewPin))
			{
				return;
			}

			TestEqual(TEXT("Renamed pin uses the new float type"),
				NewPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Real);
			TestFalse(TEXT("Renamed float pin did not inherit the old int default '42'"),
				NewPin->DefaultValue == FString(TEXT("42")));
		});

		It("should update to the new pin type after a type change with the same name", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("42"));

			// Same-name type change doesn't exercise migration.
			const FEdGraphPinType BoolType = MakePinType(UEdGraphSchema_K2::PC_Boolean);
			ChangeVariableTypeAndCompileAll(TEXT("IntVar"), BoolType);

			USMGraphK2Node_PropertyNode_Base* SameNameNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!SameNameNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = SameNameNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Retyped bool result pin exists"), NewPin))
			{
				return;
			}

			TestEqual(TEXT("Retyped pin uses the new bool type"),
				NewPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Boolean);
			TestEqual(TEXT("No orphan pins created when name matches"),
				CountOrphanInputPins(SameNameNode), 0);
		});

		It("should not migrate a wired connection when the type changes", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}

			UFunction* MakeLiteralInt = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
			CreateLiteralSourceAndWire(PropertyNode, MakeLiteralInt, TEXT("5"));

			const FEdGraphPinType StringType = MakePinType(UEdGraphSchema_K2::PC_String);
			RenameAndRetypeAndCompileAll(TEXT("IntVar"), TEXT("RenamedToString"), StringType);

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedToString"));
			if (!RenamedNode)
			{
				return;
			}

			const UEdGraphPin* NewPin = RenamedNode->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Renamed string result pin exists"), NewPin))
			{
				return;
			}

			TestEqual(TEXT("Retyped string pin has no inherited wire from the old int"),
				NewPin->LinkedTo.Num(), 0);
		});
	});

	Describe("Orphan cleanup", [this]()
	{
		It("should leave no orphan input pins after a successful migration", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("77"));

			RenameVariableAndCompileAll(TEXT("IntVar"), TEXT("RenamedInt"));

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedInt"));
			if (!RenamedNode)
			{
				return;
			}

			TestEqual(TEXT("No orphan input pins remain after full compile"),
				CountOrphanInputPins(RenamedNode), 0);
		});

		It("should leave no orphan input pins after a type-change rejection", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("88"));

			const FEdGraphPinType StringType = MakePinType(UEdGraphSchema_K2::PC_String);
			RenameAndRetypeAndCompileAll(TEXT("IntVar"), TEXT("RenamedAndRetyped"), StringType);

			USMGraphK2Node_PropertyNode_Base* RenamedNode = FindPropertyNode(StateNode, TEXT("RenamedAndRetyped"));
			if (!RenamedNode)
			{
				return;
			}

			TestEqual(TEXT("No orphan input pins remain even after guard rejection"),
				CountOrphanInputPins(RenamedNode), 0);
		});
	});

	Describe("No-op when nothing to migrate", [this]()
	{
		It("should leave pins unchanged when the BP is compiled without a rename", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)
			if (!CreateNodeInstanceBlueprint())
			{
				return;
			}

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode();
			if (!StateNode)
			{
				return;
			}

			USMGraphK2Node_PropertyNode_Base* PropertyNode = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!PropertyNode)
			{
				return;
			}
			SetResultPinDefaultValue(PropertyNode, TEXT("55"));

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			USMGraphK2Node_PropertyNode_Base* StillThere = FindPropertyNode(StateNode, TEXT("IntVar"));
			if (!StillThere)
			{
				return;
			}

			const UEdGraphPin* Pin = StillThere->GetResultPin(EGPD_Input);
			if (!TestNotNull(TEXT("Result pin still present"), Pin))
			{
				return;
			}
			TestEqual(TEXT("Default value unchanged by no-op compile"),
				Pin->DefaultValue, FString(TEXT("55")));
			TestEqual(TEXT("No orphan pins"), CountOrphanInputPins(StillThere), 0);
		});
	});
}

#endif // PLATFORM_DESKTOP

#endif // WITH_DEV_AUTOMATION_TESTS
