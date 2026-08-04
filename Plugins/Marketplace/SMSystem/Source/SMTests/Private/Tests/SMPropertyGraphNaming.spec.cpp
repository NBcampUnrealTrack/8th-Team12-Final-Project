// Copyright Recursoft LLC. All Rights Reserved.

#include "Helpers/SMTestBoilerplate.h"
#include "Helpers/SMTestHelpers.h"
#include "SMTestContext.h"

#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/SMGraph.h"
#include "Graph/SMPropertyGraph.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Blueprints/SMBlueprint.h"
#include "SMGraphProperty_Base.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "ObjectTools.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FPropertyGraphNamingSpec,
                  "LogicDriver.PropertyGraph.Naming",
                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::
                  EngineFilter)
	SETUP_SPEC_TEST_DEFINES()

	/**
	 * Verify that every property graph on the given state node has an object name based on
	 * ObjectTools::SanitizeObjectName(VariableName). Returns the number of graphs checked.
	 */
	int32 VerifyGraphNamesMatchVariableNames(const USMGraphNode_StateNode* InStateNode)
	{
		int32 CheckedCount = 0;

		const TMap<FGuid, TObjectPtr<USMPropertyGraph>>& PropertyGraphs = InStateNode->GetAllPropertyGraphs();
		for (const TTuple<FGuid, TObjectPtr<USMPropertyGraph>>& KeyVal : PropertyGraphs)
		{
			const USMPropertyGraph* PropertyGraph = KeyVal.Value;
			if (!TestNotNull(TEXT("Property graph is valid"), PropertyGraph))
			{
				continue;
			}

			if (!TestNotNull(TEXT("ResultNode is valid"), PropertyGraph->ResultNode.Get()))
			{
				continue;
			}

			const FSMGraphProperty_Base* PropertyNode = PropertyGraph->ResultNode->GetPropertyNodeConst();
			if (!TestNotNull(TEXT("PropertyNode is valid"), PropertyNode))
			{
				continue;
			}

			const FString ExpectedBaseName = ObjectTools::SanitizeObjectName(PropertyNode->VariableName.ToString());
			const FString CurrentName = PropertyGraph->GetName();

			TestTrue(
				FString::Printf(TEXT("Graph '%s' name starts with expected base '%s'"),
					*CurrentName, *ExpectedBaseName),
				CurrentName.StartsWith(ExpectedBaseName));

			// Verify the sanitized name does not differ from itself (no illegal characters remain).
			TestEqual(
				FString::Printf(TEXT("Graph '%s' object name is fully sanitized"), *CurrentName),
				ObjectTools::SanitizeObjectName(CurrentName), CurrentName);

			++CheckedCount;
		}

		return CheckedCount;
	}

	/**
	 * Simulate old display-name-based graph naming by renaming each property graph
	 * on a state node to the given name. For arrays, appends _N suffix for entries after the first.
	 */
	void RenameAllPropertyGraphs(const USMGraphNode_StateNode* InStateNode, const FString& InBaseName)
	{
		int32 Index = 0;
		const TMap<FGuid, TObjectPtr<USMPropertyGraph>>& PropertyGraphs = InStateNode->GetAllPropertyGraphs();
		for (const TTuple<FGuid, TObjectPtr<USMPropertyGraph>>& KeyVal : PropertyGraphs)
		{
			USMPropertyGraph* PropertyGraph = KeyVal.Value;
			if (!TestNotNull(TEXT("Property graph is valid for rename"), PropertyGraph))
			{
				continue;
			}

			const FString OldName = Index == 0
				? InBaseName
				: FString::Printf(TEXT("%s_%d"), *InBaseName, Index);

			PropertyGraph->Rename(*OldName, PropertyGraph->GetOuter(),
				REN_DoNotDirty);

			TestEqual(
				FString::Printf(TEXT("Graph renamed to simulated old name '%s'"), *OldName),
				PropertyGraph->GetName(), OldName);

			++Index;
		}
	}

	/** Build a linear state machine and return the first state node with the given node class. */
	USMGraphNode_StateNode* BuildAndGetFirstStateNode(const TSubclassOf<USMNodeInstance> InNodeClass)
	{
		UEdGraphPin* LastStatePin = nullptr;
		TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin, InNodeClass);

		TArray<USMGraphNode_StateNode*> FoundStateNodes;
		FSMBlueprintEditorUtils::GetAllNodesOfClassNested<USMGraphNode_StateNode>(StateMachineGraph, FoundStateNodes);

		if (!TestTrue(TEXT("State nodes found"), FoundStateNodes.Num() > 0))
		{
			return nullptr;
		}

		return FoundStateNodes[0];
	}

	/** Record graph names on a state node keyed by GUID. */
	TMap<FGuid, FString> RecordGraphNames(const USMGraphNode_StateNode* InStateNode)
	{
		TMap<FGuid, FString> Names;
		const TMap<FGuid, TObjectPtr<USMPropertyGraph>>& PropertyGraphs = InStateNode->GetAllPropertyGraphs();
		for (const TTuple<FGuid, TObjectPtr<USMPropertyGraph>>& KeyVal : PropertyGraphs)
		{
			if (!TestNotNull(TEXT("Property graph is valid for recording name"), KeyVal.Value.Get()))
			{
				continue;
			}
			Names.Add(KeyVal.Key, KeyVal.Value->GetName());
		}
		return Names;
	}

	/** Verify graph names match a previously recorded set. */
	void VerifyGraphNamesUnchanged(const USMGraphNode_StateNode* InStateNode, const TMap<FGuid, FString>& InOriginalNames)
	{
		const TMap<FGuid, TObjectPtr<USMPropertyGraph>>& PropertyGraphs = InStateNode->GetAllPropertyGraphs();
		for (const TTuple<FGuid, TObjectPtr<USMPropertyGraph>>& KeyVal : PropertyGraphs)
		{
			if (!TestNotNull(TEXT("Property graph is valid for name comparison"), KeyVal.Value.Get()))
			{
				continue;
			}

			const FString* OriginalName = InOriginalNames.Find(KeyVal.Key);
			if (!TestNotNull(TEXT("Original name found for GUID"), OriginalName))
			{
				continue;
			}

			TestEqual(
				FString::Printf(TEXT("Graph name unchanged: '%s'"), **OriginalName),
				KeyVal.Value->GetName(), *OriginalName);
		}
	}

	/** Duplicate a state node and return the duplicated state node. */
	USMGraphNode_StateNode* DuplicateStateNode(USMGraphNode_StateNode* InSourceNode)
	{
		const TSet<UEdGraphNode*> DuplicatedNodes = TestHelpers::DuplicateNodes(TArray<UEdGraphNode*>{ InSourceNode });
		TestEqual(TEXT("One node duplicated"), DuplicatedNodes.Num(), 1);

		for (UEdGraphNode* Node : DuplicatedNodes)
		{
			if (USMGraphNode_StateNode* StateNode = Cast<USMGraphNode_StateNode>(Node))
			{
				return StateNode;
			}
		}

		return nullptr;
	}

	/** Verify no null entries exist in a state node's property graph map. */
	void VerifyNoNullPropertyGraphs(const USMGraphNode_StateNode* InStateNode)
	{
		const TMap<FGuid, TObjectPtr<USMPropertyGraph>>& PropertyGraphs = InStateNode->GetAllPropertyGraphs();
		for (const TTuple<FGuid, TObjectPtr<USMPropertyGraph>>& KeyVal : PropertyGraphs)
		{
			TestNotNull(
				FString::Printf(TEXT("Property graph not null for GUID %s"), *KeyVal.Key.ToString()),
				KeyVal.Value.Get());
		}
	}
END_DEFINE_SPEC(FPropertyGraphNamingSpec)

void FPropertyGraphNamingSpec::Define()
{
	BeforeEach([this]()
	{
		SPEC_TEST_BEFORE_EACH()
	});

	AfterEach([this]()
	{
		SPEC_TEST_AFTER_EACH()
		NewAsset.DeleteAsset(this);
	});

	Describe("New graph naming", [this]()
	{
		It("should name a single FSMTextGraphProperty graph after its VariableName", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const int32 Count = VerifyGraphNamesMatchVariableNames(StateNode);
			TestTrue(TEXT("At least one property graph verified"), Count > 0);
		});

		It("should name TArray<FSMTextGraphProperty> graphs with suffixed VariableNames", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphArrayState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const int32 Count = VerifyGraphNamesMatchVariableNames(StateNode);
			// USMTextGraphArrayState constructs with 2 default entries.
			TestTrue(TEXT("Multiple property graphs verified for array"), Count >= 2);
		});

		It("should name graphs correctly on a node with mixed property types", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			// USMTextGraphStateExtra inherits TextGraph from USMTextGraphState and adds StringVar.
			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphStateExtra::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const int32 Count = VerifyGraphNamesMatchVariableNames(StateNode);
			TestTrue(TEXT("Property graphs verified for mixed type node"), Count > 0);
		});

		It("should sanitize the object name when the DisplayName contains a dot", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			// USMTextGraphDotDisplayNameState has a property with
			// meta = (DisplayName = "Audio Line Name (.wem)") — the exact crash trigger.
			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(
				USMTextGraphDotDisplayNameState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const int32 Count = VerifyGraphNamesMatchVariableNames(StateNode);
			// TextGraph + DotDisplayNameProperty + CharacterTag (if it generates a graph).
			TestTrue(TEXT("Dot DisplayName property graphs verified"), Count >= 2);

			// Verify specifically that the DotDisplayNameProperty graph does not contain a dot.
			const TMap<FGuid, TObjectPtr<USMPropertyGraph>>& PropertyGraphs = StateNode->GetAllPropertyGraphs();
			for (const TTuple<FGuid, TObjectPtr<USMPropertyGraph>>& KeyVal : PropertyGraphs)
			{
				const USMPropertyGraph* PropertyGraph = KeyVal.Value;
				if (!TestNotNull(TEXT("Property graph is valid"), PropertyGraph))
				{
					continue;
				}

				if (!TestNotNull(TEXT("ResultNode is valid"), PropertyGraph->ResultNode.Get()))
				{
					continue;
				}

				const FSMGraphProperty_Base* PropertyNode = PropertyGraph->ResultNode->GetPropertyNodeConst();
				if (!TestNotNull(TEXT("PropertyNode is valid"), PropertyNode))
				{
					continue;
				}

				const FString ObjectName = PropertyGraph->GetName();
				TestFalse(
					FString::Printf(TEXT("Graph '%s' object name does not contain a dot"), *ObjectName),
					ObjectName.Contains(TEXT(".")));

				// Verify the RealDisplayName has been populated from the FProperty's DisplayName meta.
				if (PropertyNode->VariableName == GET_MEMBER_NAME_CHECKED(USMTextGraphDotDisplayNameState, DotDisplayNameProperty))
				{
					TestFalse(TEXT("RealDisplayName is not empty for DotDisplayNameProperty"),
						PropertyNode->RealDisplayName.IsEmpty());
					TestTrue(TEXT("RealDisplayName contains the dot from the DisplayName meta"),
						PropertyNode->RealDisplayName.ToString().Contains(TEXT(".")));
				}
			}
		});
	});

	Describe("Correctly named graphs are not renamed on compile", [this]()
	{
		It("should not rename a correctly named single property graph", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const TMap<FGuid, FString> OriginalNames = RecordGraphNames(StateNode);

			// Compile triggers PreCompile which runs the migration path.
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			VerifyGraphNamesUnchanged(StateNode, OriginalNames);
		});

		It("should not rename correctly named TArray property graphs", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphArrayState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const TMap<FGuid, FString> OriginalNames = RecordGraphNames(StateNode);
			TestTrue(TEXT("Multiple array graphs recorded"), OriginalNames.Num() >= 2);

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			VerifyGraphNamesUnchanged(StateNode, OriginalNames);
		});

		It("should not rename correctly named mixed property type graphs", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphStateExtra::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const TMap<FGuid, FString> OriginalNames = RecordGraphNames(StateNode);

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			VerifyGraphNamesUnchanged(StateNode, OriginalNames);
		});

		It("should not rename dot DisplayName property graphs after initial creation", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(
				USMTextGraphDotDisplayNameState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const TMap<FGuid, FString> OriginalNames = RecordGraphNames(StateNode);

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			VerifyGraphNamesUnchanged(StateNode, OriginalNames);
		});
	});

	Describe("Old graph migration on compile", [this]()
	{
		It("should rename a graph with spaces in the name", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			// Simulate old display-name-based naming: "Text Graph" instead of "TextGraph".
			RenameAllPropertyGraphs(StateNode, TEXT("Text Graph"));

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			const int32 Count = VerifyGraphNamesMatchVariableNames(StateNode);
			TestTrue(TEXT("Property graphs migrated from spaced names"), Count > 0);
		});

		It("should rename a graph named with (.wem) matching the original crash report", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			// Simulate the exact crash scenario from the customer report: a DisplayName containing
			// "(.wem)" which introduces a dot into the UObject name, breaking UE object-path
			// serialization during clipboard copy-paste.
			RenameAllPropertyGraphs(StateNode, TEXT("Audio Line Name (.wem)"));

			// Verify the bad name is actually set with the dot.
			const TMap<FGuid, TObjectPtr<USMPropertyGraph>>& PropertyGraphs = StateNode->GetAllPropertyGraphs();
			for (const TTuple<FGuid, TObjectPtr<USMPropertyGraph>>& KeyVal : PropertyGraphs)
			{
				if (!TestNotNull(TEXT("Property graph is valid before migration check"), KeyVal.Value.Get()))
				{
					continue;
				}
				TestTrue(TEXT("Graph name contains dot before migration"),
					KeyVal.Value->GetName().Contains(TEXT(".")));
			}

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			const int32 Count = VerifyGraphNamesMatchVariableNames(StateNode);
			TestTrue(TEXT("Property graphs migrated from dot-containing names"), Count > 0);
		});

		It("should rename old-style TArray graphs", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphArrayState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			RenameAllPropertyGraphs(StateNode, TEXT("Text Graph Array"));

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			const int32 Count = VerifyGraphNamesMatchVariableNames(StateNode);
			TestTrue(TEXT("All array property graphs migrated"), Count >= 2);
		});

		It("should rename old-style TArray graphs containing (.wem) dots", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphArrayState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			RenameAllPropertyGraphs(StateNode, TEXT("Bad Array (.wem)"));

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			const int32 Count = VerifyGraphNamesMatchVariableNames(StateNode);
			TestTrue(TEXT("All array property graphs migrated from dot names"), Count >= 2);
		});
	});

	Describe("Display name", [this]()
	{
		It("should return a human-readable display name from the schema while the object name is sanitized", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const TMap<FGuid, TObjectPtr<USMPropertyGraph>>& PropertyGraphs = StateNode->GetAllPropertyGraphs();
			for (const TTuple<FGuid, TObjectPtr<USMPropertyGraph>>& KeyVal : PropertyGraphs)
			{
				const USMPropertyGraph* PropertyGraph = KeyVal.Value;
				if (!TestNotNull(TEXT("Property graph is valid"), PropertyGraph))
				{
					continue;
				}

				if (!TestNotNull(TEXT("ResultNode is valid"), PropertyGraph->ResultNode.Get()))
				{
					continue;
				}

				const FSMGraphProperty_Base* PropertyNode = PropertyGraph->ResultNode->GetPropertyNodeConst();
				if (!TestNotNull(TEXT("PropertyNode is valid"), PropertyNode))
				{
					continue;
				}

				// The object name should be sanitized (no spaces).
				const FString ObjectName = PropertyGraph->GetName();
				TestEqual(TEXT("Object name is sanitized"),
					ObjectTools::SanitizeObjectName(ObjectName), ObjectName);

				// The display name from the schema should contain the human-readable property name.
				const UEdGraphSchema* Schema = PropertyGraph->GetSchema();
				if (!TestNotNull(TEXT("Schema is valid"), Schema))
				{
					continue;
				}

				FGraphDisplayInfo DisplayInfo;
				Schema->GetGraphDisplayInformation(*PropertyGraph, DisplayInfo);

				const FText DisplayName = PropertyNode->GetDisplayName();
				TestFalse(TEXT("Display name is not empty"), DisplayName.IsEmpty());
				TestEqual(TEXT("Schema display name matches property display name"),
					DisplayInfo.PlainName.ToString(), DisplayName.ToString());
			}
		});

		It("should preserve the dot-containing display name while the object name is sanitized", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			const USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(
				USMTextGraphDotDisplayNameState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const TMap<FGuid, TObjectPtr<USMPropertyGraph>>& PropertyGraphs = StateNode->GetAllPropertyGraphs();
			for (const TTuple<FGuid, TObjectPtr<USMPropertyGraph>>& KeyVal : PropertyGraphs)
			{
				const USMPropertyGraph* PropertyGraph = KeyVal.Value;
				if (!TestNotNull(TEXT("Property graph is valid"), PropertyGraph))
				{
					continue;
				}

				if (!TestNotNull(TEXT("ResultNode is valid"), PropertyGraph->ResultNode.Get()))
				{
					continue;
				}

				const FSMGraphProperty_Base* PropertyNode = PropertyGraph->ResultNode->GetPropertyNodeConst();
				if (!TestNotNull(TEXT("PropertyNode is valid"), PropertyNode))
				{
					continue;
				}

				// Object name must be sanitized regardless of what DisplayName contains.
				const FString ObjectName = PropertyGraph->GetName();
				TestFalse(
					FString::Printf(TEXT("Object name '%s' does not contain a dot"), *ObjectName),
					ObjectName.Contains(TEXT(".")));

				// For the DotDisplayNameProperty specifically, verify the display name preserves
				// the original "Audio Line Name (.wem)" including the dot.
				if (PropertyNode->VariableName == GET_MEMBER_NAME_CHECKED(USMTextGraphDotDisplayNameState, DotDisplayNameProperty))
				{
					const UEdGraphSchema* Schema = PropertyGraph->GetSchema();
					if (!TestNotNull(TEXT("Schema is valid"), Schema))
					{
						continue;
					}

					FGraphDisplayInfo DisplayInfo;
					Schema->GetGraphDisplayInformation(*PropertyGraph, DisplayInfo);

					TestTrue(TEXT("Display name contains (.wem) dot"),
						DisplayInfo.PlainName.ToString().Contains(TEXT("(.wem)")));
					TestEqual(TEXT("Object name is based on VariableName, not DisplayName"),
						ObjectName, TEXT("DotDisplayNameProperty"));
				}
			}
		});
	});

	Describe("Copy and paste", [this]()
	{
		It("should duplicate a single text graph property node without null graphs", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const USMGraphNode_StateNode* DuplicatedStateNode = DuplicateStateNode(StateNode);
			if (!TestNotNull(TEXT("Duplicated state node found"), DuplicatedStateNode))
			{
				return;
			}

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			VerifyNoNullPropertyGraphs(DuplicatedStateNode);

			const int32 Count = VerifyGraphNamesMatchVariableNames(DuplicatedStateNode);
			TestTrue(TEXT("Duplicated node property graphs verified"), Count > 0);
		});

		It("should duplicate a TArray text graph property node without null graphs", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphArrayState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const USMGraphNode_StateNode* DuplicatedStateNode = DuplicateStateNode(StateNode);
			if (!TestNotNull(TEXT("Duplicated state node found"), DuplicatedStateNode))
			{
				return;
			}

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			VerifyNoNullPropertyGraphs(DuplicatedStateNode);

			const int32 Count = VerifyGraphNamesMatchVariableNames(DuplicatedStateNode);
			TestTrue(TEXT("Duplicated array node property graphs verified"), Count >= 2);
		});

		It("should duplicate a mixed property type node without null graphs", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphStateExtra::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const USMGraphNode_StateNode* DuplicatedStateNode = DuplicateStateNode(StateNode);
			if (!TestNotNull(TEXT("Duplicated state node found"), DuplicatedStateNode))
			{
				return;
			}

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			VerifyNoNullPropertyGraphs(DuplicatedStateNode);

			const int32 Count = VerifyGraphNamesMatchVariableNames(DuplicatedStateNode);
			TestTrue(TEXT("Duplicated mixed type node property graphs verified"), Count > 0);
		});

		It("should duplicate a dot DisplayName property node without null graphs", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(
				USMTextGraphDotDisplayNameState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			const USMGraphNode_StateNode* DuplicatedStateNode = DuplicateStateNode(StateNode);
			if (!TestNotNull(TEXT("Duplicated state node found"), DuplicatedStateNode))
			{
				return;
			}

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			VerifyNoNullPropertyGraphs(DuplicatedStateNode);

			const int32 Count = VerifyGraphNamesMatchVariableNames(DuplicatedStateNode);
			TestTrue(TEXT("Duplicated dot DisplayName node property graphs verified"), Count >= 2);
		});

		It("should duplicate a node that was migrated from old (.wem) names", [this]()
		{
			SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(2)

			USMGraphNode_StateNode* StateNode = BuildAndGetFirstStateNode(USMTextGraphState::StaticClass());
			if (!StateNode)
			{
				return;
			}

			// Simulate the original crash scenario: rename to a name with (.wem), compile to
			// trigger migration, then duplicate. This is the exact sequence that would crash
			// prior to the fix.
			RenameAllPropertyGraphs(StateNode, TEXT("Audio Line Name (.wem)"));
			FKismetEditorUtilities::CompileBlueprint(NewBP);

			// Verify migration happened first.
			VerifyGraphNamesMatchVariableNames(StateNode);

			// Now duplicate — this is the operation that originally crashed.
			const USMGraphNode_StateNode* DuplicatedStateNode = DuplicateStateNode(StateNode);
			if (!TestNotNull(TEXT("Duplicated state node found after migration"), DuplicatedStateNode))
			{
				return;
			}

			FKismetEditorUtilities::CompileBlueprint(NewBP);

			VerifyNoNullPropertyGraphs(DuplicatedStateNode);

			const int32 Count = VerifyGraphNamesMatchVariableNames(DuplicatedStateNode);
			TestTrue(TEXT("Duplicated migrated node property graphs verified"), Count > 0);
		});
	});
}

#endif

#endif
