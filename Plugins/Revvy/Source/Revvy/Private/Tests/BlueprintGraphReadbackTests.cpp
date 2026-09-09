// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Misc/ScopeExit.h"
#include "PythonAPI/UBlueprintService.h"
#include "UObject/Package.h"

/**
 * Gates 1-6 and 8-10 of docs/design/unreal-bp-graph-readback-contract.md, run
 * against a real Blueprint asset built by the test rather than a mock.
 *
 * Gate 7 (asset timestamp unchanged) needs a saved asset on disk and is not run
 * here; the observable half of it — describe_graph changing neither compile
 * status nor package dirtiness — is covered by gate 6. Gate 11 is the Python
 * source-anchor test. Gate 5's macro-graph leg has no creation API on
 * UBlueprintService and is reported as not exercised rather than faked.
 */
namespace RevvyDescribeGraphTests
{
	const TCHAR* FixtureFolder = TEXT("/Game/RevvyAutomation");
	const TCHAR* FixtureName = TEXT("BP_RevvyDescribeGraphFixture");
	const TCHAR* EventGraph = TEXT("EventGraph");

	/** Create (or reuse) the fixture Blueprint and return its resolved path. */
	FString MakeFixture(FAutomationTestBase& Test, const TCHAR* Name)
	{
		const FString Path = UBlueprintService::CreateBlueprint(Name, TEXT("Actor"), FixtureFolder);
		if (Path.IsEmpty())
		{
			Test.AddError(FString::Printf(TEXT("Could not create fixture Blueprint '%s'"), Name));
		}
		return Path;
	}

	void DestroyFixture(const FString& BlueprintPath)
	{
		if (!BlueprintPath.IsEmpty() && UEditorAssetLibrary::DoesAssetExist(BlueprintPath))
		{
			UEditorAssetLibrary::DeleteAsset(BlueprintPath);
		}
	}

	const FBlueprintGraphNodeDescription* FindNode(
		const FBlueprintGraphDescription& Graph, const FString& NodeId)
	{
		return Graph.Nodes.FindByPredicate(
			[&NodeId](const FBlueprintGraphNodeDescription& Node) { return Node.NodeId == NodeId; });
	}

	const FBlueprintPinDetailedInfo* FindPin(
		const FBlueprintGraphNodeDescription& Node, const TCHAR* PinName)
	{
		return Node.Pins.FindByPredicate(
			[PinName](const FBlueprintPinDetailedInfo& Pin) { return Pin.PinName == PinName; });
	}

	FString EdgeKey(const FBlueprintConnectionInfo& Edge)
	{
		return FString::Printf(TEXT("%s.%s->%s.%s"),
			*Edge.SourceNodeId, *Edge.SourcePinName, *Edge.TargetNodeId, *Edge.TargetPinName);
	}
}

// ============================================================================
// Gates 1, 2, 8 — round trip, edge model, self-consistency
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyDescribeGraphRoundTripTest,
	"Revvy.Blueprints.DescribeGraph.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyDescribeGraphRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace RevvyDescribeGraphTests;

	const FString BlueprintPath = MakeFixture(*this, FixtureName);
	if (BlueprintPath.IsEmpty())
	{
		return false;
	}

	ON_SCOPE_EXIT { DestroyFixture(BlueprintPath); };

	const FString EventId = UBlueprintService::AddCustomEventNode(
		BlueprintPath, EventGraph, TEXT("RevvyDescribeGraphEvent"), 0.0f, 0.0f);
	const FString PrintId = UBlueprintService::AddFunctionCallNode(
		BlueprintPath, EventGraph, TEXT("KismetSystemLibrary"), TEXT("PrintString"), 400.0f, 0.0f);

	TestFalse(TEXT("Custom event node was created"), EventId.IsEmpty());
	TestFalse(TEXT("PrintString node was created"), PrintId.IsEmpty());
	if (EventId.IsEmpty() || PrintId.IsEmpty())
	{
		return false;
	}

	TestTrue(
		TEXT("Event exec wires into PrintString"),
		UBlueprintService::ConnectNodes(
			BlueprintPath, EventGraph, EventId, TEXT("then"), PrintId, TEXT("execute")));

	FBlueprintGraphDescription Graph;
	FString Error;
	const bool bDescribed = UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, Graph, Error);

	// --- gate 1: both created nodes come back by GUID -----------------------
	TestTrue(FString::Printf(TEXT("describe_graph succeeded: %s"), *Error), bDescribed);
	TestEqual(TEXT("Graph name is resolved as UE spells it"), Graph.GraphName, FString(EventGraph));
	TestEqual(TEXT("EventGraph is reported as an ubergraph"), Graph.GraphType, FString(TEXT("ubergraph")));
	TestTrue(TEXT("The custom event node is present by GUID"), FindNode(Graph, EventId) != nullptr);
	TestTrue(TEXT("The PrintString node is present by GUID"), FindNode(Graph, PrintId) != nullptr);

	if (const FBlueprintGraphNodeDescription* PrintNode = FindNode(Graph, PrintId))
	{
		TestEqual(TEXT("Call-function nodes report their function"),
			PrintNode->FunctionName, FString(TEXT("PrintString")));
		TestFalse(TEXT("A call-function node carries pins"), PrintNode->Pins.IsEmpty());
	}

	// --- gate 2: edges are directional, deduplicated, and agree with GetConnections
	const TArray<FBlueprintConnectionInfo> Existing =
		UBlueprintService::GetConnections(BlueprintPath, EventGraph);
	TestEqual(TEXT("describe_graph and get_connections agree on edge count"),
		Graph.Edges.Num(), Existing.Num());

	TSet<FString> DescribedKeys;
	for (const FBlueprintConnectionInfo& Edge : Graph.Edges)
	{
		bool bAlreadyPresent = false;
		DescribedKeys.Add(EdgeKey(Edge), &bAlreadyPresent);
		TestFalse(
			FString::Printf(TEXT("Edge %s appears exactly once"), *EdgeKey(Edge)),
			bAlreadyPresent);
	}
	for (const FBlueprintConnectionInfo& Edge : Existing)
	{
		TestTrue(
			FString::Printf(TEXT("get_connections edge %s is present in describe_graph"), *EdgeKey(Edge)),
			DescribedKeys.Contains(EdgeKey(Edge)));
	}

	const FBlueprintConnectionInfo* ExecEdge = Graph.Edges.FindByPredicate(
		[&EventId, &PrintId](const FBlueprintConnectionInfo& Edge)
		{
			return Edge.SourceNodeId == EventId && Edge.TargetNodeId == PrintId;
		});
	TestTrue(TEXT("The wired edge is reported source-to-target"), ExecEdge != nullptr);
	if (ExecEdge)
	{
		// Source is the OUTPUT pin: the direction must not depend on walk order.
		TestEqual(TEXT("Edge source is the output pin"), ExecEdge->SourcePinName, FString(TEXT("then")));
		TestEqual(TEXT("Edge target is the input pin"), ExecEdge->TargetPinName, FString(TEXT("execute")));
	}

	// --- gate 8: every edge endpoint exists in the same snapshot -------------
	for (const FBlueprintConnectionInfo& Edge : Graph.Edges)
	{
		TestTrue(
			FString::Printf(TEXT("Edge source %s is present in nodes"), *Edge.SourceNodeId),
			FindNode(Graph, Edge.SourceNodeId) != nullptr);
		TestTrue(
			FString::Printf(TEXT("Edge target %s is present in nodes"), *Edge.TargetNodeId),
			FindNode(Graph, Edge.TargetNodeId) != nullptr);
	}

	// A mutation between two reads must leave each individual result consistent.
	const FString SecondPrintId = UBlueprintService::AddFunctionCallNode(
		BlueprintPath, EventGraph, TEXT("KismetSystemLibrary"), TEXT("PrintString"), 800.0f, 0.0f);
	TestFalse(TEXT("A second node was created between reads"), SecondPrintId.IsEmpty());

	FBlueprintGraphDescription AfterEdit;
	TestTrue(TEXT("describe_graph succeeds after an interleaved edit"),
		UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, AfterEdit, Error));
	TestTrue(TEXT("The interleaved node appears in the later snapshot"),
		FindNode(AfterEdit, SecondPrintId) != nullptr);
	TestTrue(TEXT("The earlier snapshot did not gain the later node"),
		FindNode(Graph, SecondPrintId) == nullptr);
	for (const FBlueprintConnectionInfo& Edge : AfterEdit.Edges)
	{
		TestTrue(
			FString::Printf(TEXT("Post-edit edge source %s is present"), *Edge.SourceNodeId),
			FindNode(AfterEdit, Edge.SourceNodeId) != nullptr);
		TestTrue(
			FString::Printf(TEXT("Post-edit edge target %s is present"), *Edge.TargetNodeId),
			FindNode(AfterEdit, Edge.TargetNodeId) != nullptr);
	}

	return true;
}

// ============================================================================
// Gates 3, 4, 9 — pin type fidelity, direction agreement, node flags
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyDescribeGraphPinFidelityTest,
	"Revvy.Blueprints.DescribeGraph.PinFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyDescribeGraphPinFidelityTest::RunTest(const FString& Parameters)
{
	using namespace RevvyDescribeGraphTests;

	const FString BlueprintPath = MakeFixture(*this, TEXT("BP_RevvyDescribeGraphPins"));
	if (BlueprintPath.IsEmpty())
	{
		return false;
	}

	ON_SCOPE_EXIT { DestroyFixture(BlueprintPath); };

	// PrintString alone carries an exec pin, two bools, a struct (LinearColor),
	// and an object pin (WorldContextObject) — all four kinds gate 3 needs.
	const FString PrintId = UBlueprintService::AddFunctionCallNode(
		BlueprintPath, EventGraph, TEXT("KismetSystemLibrary"), TEXT("PrintString"), 0.0f, 0.0f);
	const FString DelayId = UBlueprintService::AddFunctionCallNode(
		BlueprintPath, EventGraph, TEXT("KismetSystemLibrary"), TEXT("Delay"), 400.0f, 0.0f);
	const FString PureId = UBlueprintService::AddFunctionCallNode(
		BlueprintPath, EventGraph, TEXT("KismetMathLibrary"), TEXT("Add_DoubleDouble"), 800.0f, 0.0f);

	TestFalse(TEXT("PrintString node was created"), PrintId.IsEmpty());
	TestFalse(TEXT("Delay node was created"), DelayId.IsEmpty());
	TestFalse(TEXT("Pure math node was created"), PureId.IsEmpty());
	if (PrintId.IsEmpty())
	{
		return false;
	}

	// A written default must read back through the snapshot, not just through the setter.
	TestTrue(TEXT("A pin default can be written"),
		UBlueprintService::SetNodePinValue(
			BlueprintPath, EventGraph, PrintId, TEXT("InString"), TEXT("RevvyGateThree")));

	FBlueprintGraphDescription Graph;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("describe_graph succeeded: %s"), *Error),
		UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, Graph, Error)))
	{
		return false;
	}

	const FBlueprintGraphNodeDescription* PrintNode = FindNode(Graph, PrintId);
	if (!PrintNode)
	{
		AddError(TEXT("The PrintString node is missing from the snapshot"));
		return false;
	}

	// --- gate 3: four pin kinds are distinguishable -------------------------
	if (const FBlueprintPinDetailedInfo* ExecPin = FindPin(*PrintNode, TEXT("execute")))
	{
		TestEqual(TEXT("Exec pins report the exec category"),
			ExecPin->PinCategory, FString(TEXT("exec")));
		TestTrue(TEXT("Exec pins are inputs on execute"), ExecPin->bIsInput);
	}
	else
	{
		AddError(TEXT("PrintString has no execute pin"));
	}

	if (const FBlueprintPinDetailedInfo* BoolPin = FindPin(*PrintNode, TEXT("bPrintToScreen")))
	{
		TestEqual(TEXT("Bool pins report the bool category"),
			BoolPin->PinCategory, FString(TEXT("bool")));
	}
	else
	{
		AddError(TEXT("PrintString has no bPrintToScreen pin"));
	}

	if (const FBlueprintPinDetailedInfo* StructPin = FindPin(*PrintNode, TEXT("TextColor")))
	{
		TestEqual(TEXT("Struct pins report the struct category"),
			StructPin->PinCategory, FString(TEXT("struct")));
		// The flattened PinType string of FBlueprintPinInfo cannot say WHICH struct;
		// this is the fidelity the contract added.
		TestTrue(TEXT("Struct pins carry a resolvable type path"),
			StructPin->TypePath.Contains(TEXT("LinearColor")));
	}
	else
	{
		AddError(TEXT("PrintString has no TextColor pin"));
	}

	const FBlueprintPinDetailedInfo* ObjectPin = PrintNode->Pins.FindByPredicate(
		[](const FBlueprintPinDetailedInfo& Pin) { return Pin.PinCategory == TEXT("object"); });
	TestTrue(TEXT("An object pin is present and categorised"), ObjectPin != nullptr);
	if (ObjectPin)
	{
		TestFalse(TEXT("Object pins carry a class type path"), ObjectPin->TypePath.IsEmpty());
	}

	if (const FBlueprintPinDetailedInfo* StringPin = FindPin(*PrintNode, TEXT("InString")))
	{
		TestEqual(TEXT("A written default reads back through the snapshot"),
			StringPin->DefaultValue, FString(TEXT("RevvyGateThree")));
	}

	// --- gate 4: direction agrees with get_node_pins for every pin ----------
	for (const FBlueprintGraphNodeDescription& Node : Graph.Nodes)
	{
		const TArray<FBlueprintPinInfo> LegacyPins =
			UBlueprintService::GetNodePins(BlueprintPath, EventGraph, Node.NodeId);
		TestEqual(
			FString::Printf(TEXT("Pin count agrees with get_node_pins on %s"), *Node.NodeTitle),
			Node.Pins.Num(), LegacyPins.Num());

		for (const FBlueprintPinDetailedInfo& Pin : Node.Pins)
		{
			const FBlueprintPinInfo* Legacy = LegacyPins.FindByPredicate(
				[&Pin](const FBlueprintPinInfo& Candidate) { return Candidate.PinName == Pin.PinName; });
			if (!Legacy)
			{
				AddError(FString::Printf(
					TEXT("Pin '%s' on '%s' is missing from get_node_pins"), *Pin.PinName, *Node.NodeTitle));
				continue;
			}
			TestEqual(
				FString::Printf(TEXT("Direction agrees for %s.%s"), *Node.NodeTitle, *Pin.PinName),
				Pin.bIsInput, Legacy->bIsInput);
			TestEqual(
				FString::Printf(TEXT("Connectedness agrees for %s.%s"), *Node.NodeTitle, *Pin.PinName),
				Pin.bIsConnected, Legacy->bIsConnected);
		}
	}

	// --- gate 9: latent, pure, and neither ----------------------------------
	if (const FBlueprintGraphNodeDescription* DelayNode = FindNode(Graph, DelayId))
	{
		TestTrue(TEXT("Delay is reported latent"), DelayNode->bIsLatent);
		TestFalse(TEXT("Delay is not pure"), DelayNode->bIsPure);
	}
	else
	{
		AddError(TEXT("The Delay node is missing from the snapshot"));
	}

	if (const FBlueprintGraphNodeDescription* PureNode = FindNode(Graph, PureId))
	{
		TestTrue(TEXT("A math node is reported pure"), PureNode->bIsPure);
		TestFalse(TEXT("A math node is not latent"), PureNode->bIsLatent);
	}
	else
	{
		AddError(TEXT("The pure math node is missing from the snapshot"));
	}

	TestFalse(TEXT("PrintString is neither pure"), PrintNode->bIsPure);
	TestFalse(TEXT("PrintString is neither latent"), PrintNode->bIsLatent);

	return true;
}

// ============================================================================
// Gate 5 — graph resolution and failure reporting
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyDescribeGraphResolutionTest,
	"Revvy.Blueprints.DescribeGraph.GraphResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyDescribeGraphResolutionTest::RunTest(const FString& Parameters)
{
	using namespace RevvyDescribeGraphTests;

	const FString BlueprintPath = MakeFixture(*this, TEXT("BP_RevvyDescribeGraphResolution"));
	if (BlueprintPath.IsEmpty())
	{
		return false;
	}

	ON_SCOPE_EXIT { DestroyFixture(BlueprintPath); };

	FBlueprintGraphDescription Ubergraph;
	FString Error;
	TestTrue(TEXT("An ubergraph resolves"),
		UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, Ubergraph, Error));
	TestEqual(TEXT("An ubergraph reports graph_type ubergraph"),
		Ubergraph.GraphType, FString(TEXT("ubergraph")));
	TestTrue(TEXT("A successful call leaves no error text"), Error.IsEmpty());

	const TCHAR* FunctionName = TEXT("RevvyDescribeGraphFunction");
	if (TestTrue(TEXT("A function graph can be created"),
		UBlueprintService::CreateFunction(BlueprintPath, FunctionName)))
	{
		FBlueprintGraphDescription FunctionGraph;
		TestTrue(TEXT("A function graph resolves"),
			UBlueprintService::DescribeGraph(BlueprintPath, FunctionName, FunctionGraph, Error));
		TestEqual(TEXT("A function graph reports graph_type function"),
			FunctionGraph.GraphType, FString(TEXT("function")));
		TestFalse(TEXT("A fresh function graph still has its entry node"),
			FunctionGraph.Nodes.IsEmpty());
	}

	// Macro graphs have no creation API on UBlueprintService, so that leg of gate 5
	// is not exercised. Reported rather than faked.
	AddInfo(TEXT("Gate 5 macro-graph leg not exercised: no macro graph creation API"));

	// The two negative cases below deliberately drive DescribeGraph's failure path,
	// which logs an error. Declare them so the framework counts them as the expected
	// output of the case under test rather than as test failures — the product's log
	// severity is not lowered to make a test pass.
	AddExpectedError(
		TEXT("Graph 'NoSuchGraphExists' not found"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedError(
		TEXT("Failed to load blueprint: /Game/RevvyAutomation/BP_DoesNotExist"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	// UEditorAssetLibrary::LoadAsset logs its own error before DescribeGraph sees
	// the null, so the unknown-asset case produces two lines, not one.
	AddExpectedError(
		TEXT("could not be found in the Asset Registry"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	// A missing graph must be distinguishable from an empty one.
	FBlueprintGraphDescription Missing;
	FString MissingError;
	TestFalse(TEXT("An unknown graph name fails"),
		UBlueprintService::DescribeGraph(
			BlueprintPath, TEXT("NoSuchGraphExists"), Missing, MissingError));
	TestFalse(TEXT("A failed call explains itself"), MissingError.IsEmpty());
	TestTrue(TEXT("The failure names the graph"),
		MissingError.Contains(TEXT("NoSuchGraphExists")));
	TestTrue(TEXT("A failed call returns no nodes"), Missing.Nodes.IsEmpty());

	FBlueprintGraphDescription MissingAsset;
	FString AssetError;
	TestFalse(TEXT("An unknown Blueprint fails"),
		UBlueprintService::DescribeGraph(
			TEXT("/Game/RevvyAutomation/BP_DoesNotExist"), EventGraph, MissingAsset, AssetError));
	TestFalse(TEXT("An unknown Blueprint explains itself"), AssetError.IsEmpty());

	return true;
}

// ============================================================================
// Gate 6 — compile status is truthful and never triggered
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyDescribeGraphStatusTest,
	"Revvy.Blueprints.DescribeGraph.CompileStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyDescribeGraphStatusTest::RunTest(const FString& Parameters)
{
	using namespace RevvyDescribeGraphTests;

	const FString BlueprintPath = MakeFixture(*this, TEXT("BP_RevvyDescribeGraphStatus"));
	if (BlueprintPath.IsEmpty())
	{
		return false;
	}

	ON_SCOPE_EXIT { DestroyFixture(BlueprintPath); };

	UBlueprintService::CompileBlueprint(BlueprintPath);

	FBlueprintGraphDescription Compiled;
	FString Error;
	if (!TestTrue(TEXT("describe_graph succeeds on a compiled Blueprint"),
		UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, Compiled, Error)))
	{
		return false;
	}
	TestTrue(TEXT("A compiled Blueprint reports up to date"), Compiled.Status.bIsUpToDate);
	TestFalse(TEXT("A compiled Blueprint reports no compile error"), Compiled.Status.bHasCompileErrors);
	TestFalse(TEXT("compile_status is never empty"), Compiled.Status.CompileStatus.IsEmpty());

	// The load-bearing half of the gate: describing must not change what it reports.
	FBlueprintGraphDescription Again;
	TestTrue(TEXT("describe_graph succeeds a second time"),
		UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, Again, Error));
	TestEqual(TEXT("Describing twice does not change compile status"),
		Again.Status.CompileStatus, Compiled.Status.CompileStatus);
	TestEqual(TEXT("Describing twice does not change dirtiness"),
		Again.Status.bIsDirty, Compiled.Status.bIsDirty);
	TestEqual(TEXT("Describing twice does not change package dirtiness"),
		Again.Status.bPackageDirty, Compiled.Status.bPackageDirty);
	TestEqual(TEXT("Describing twice does not change the node count"),
		Again.Nodes.Num(), Compiled.Nodes.Num());

	// Structural edits must be visible as dirt without a compile being forced.
	const FString PrintId = UBlueprintService::AddFunctionCallNode(
		BlueprintPath, EventGraph, TEXT("KismetSystemLibrary"), TEXT("PrintString"), 0.0f, 0.0f);
	TestFalse(TEXT("A node was added"), PrintId.IsEmpty());

	FBlueprintGraphDescription AfterStructuralEdit;
	TestTrue(TEXT("describe_graph succeeds after a structural edit"),
		UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, AfterStructuralEdit, Error));
	TestTrue(TEXT("A structural edit is reported as needing a recompile"),
		AfterStructuralEdit.Status.bIsDirty);
	TestFalse(TEXT("Describing did not compile the Blueprint clean again"),
		AfterStructuralEdit.Status.bIsUpToDate);

	// Contract section 5's stated unknown: does a value-only edit dirty the
	// Blueprint the same way a structural one does? Recorded either way rather
	// than asserted, because the contract explicitly refused to assume it.
	UBlueprintService::CompileBlueprint(BlueprintPath);
	FBlueprintGraphDescription BeforeValueEdit;
	UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, BeforeValueEdit, Error);

	UBlueprintService::SetNodePinValue(
		BlueprintPath, EventGraph, PrintId, TEXT("InString"), TEXT("RevvyGateSix"));

	FBlueprintGraphDescription AfterValueEdit;
	TestTrue(TEXT("describe_graph succeeds after a value-only edit"),
		UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, AfterValueEdit, Error));
	AddInfo(FString::Printf(
		TEXT("Value-only edit: compile_status %s -> %s, is_dirty %d -> %d"),
		*BeforeValueEdit.Status.CompileStatus,
		*AfterValueEdit.Status.CompileStatus,
		BeforeValueEdit.Status.bIsDirty ? 1 : 0,
		AfterValueEdit.Status.bIsDirty ? 1 : 0));

	// Whatever the engine does above, the written value must be observable — that
	// is what a caller verifying a set_node_pin_value actually needs.
	if (const FBlueprintGraphNodeDescription* PrintNode = FindNode(AfterValueEdit, PrintId))
	{
		if (const FBlueprintPinDetailedInfo* Pin = FindPin(*PrintNode, TEXT("InString")))
		{
			TestEqual(TEXT("A value-only edit is visible in the snapshot"),
				Pin->DefaultValue, FString(TEXT("RevvyGateSix")));
		}
		else
		{
			AddError(TEXT("InString pin missing after a value-only edit"));
		}
	}
	else
	{
		AddError(TEXT("PrintString node missing after a value-only edit"));
	}

	return true;
}

// ============================================================================
// Gate 10 — scale
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyDescribeGraphScaleTest,
	"Revvy.Blueprints.DescribeGraph.Scale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyDescribeGraphScaleTest::RunTest(const FString& Parameters)
{
	using namespace RevvyDescribeGraphTests;

	const FString BlueprintPath = MakeFixture(*this, TEXT("BP_RevvyDescribeGraphScale"));
	if (BlueprintPath.IsEmpty())
	{
		return false;
	}

	ON_SCOPE_EXIT { DestroyFixture(BlueprintPath); };

	constexpr int32 NodeTarget = 100;
	TArray<FString> CreatedIds;
	CreatedIds.Reserve(NodeTarget);
	for (int32 Index = 0; Index < NodeTarget; ++Index)
	{
		const FString NodeId = UBlueprintService::AddFunctionCallNode(
			BlueprintPath, EventGraph, TEXT("KismetSystemLibrary"), TEXT("PrintString"),
			static_cast<float>((Index % 10) * 300), static_cast<float>((Index / 10) * 200));
		if (NodeId.IsEmpty())
		{
			AddError(FString::Printf(TEXT("Node %d of %d could not be created"), Index, NodeTarget));
			break;
		}
		CreatedIds.Add(NodeId);
	}
	TestEqual(TEXT("The fixture reached the target node count"), CreatedIds.Num(), NodeTarget);

	FBlueprintGraphDescription Graph;
	FString Error;
	if (!TestTrue(TEXT("describe_graph succeeds on a large graph"),
		UBlueprintService::DescribeGraph(BlueprintPath, EventGraph, Graph, Error)))
	{
		return false;
	}

	const TArray<FBlueprintNodeInfo> LegacyNodes =
		UBlueprintService::GetNodesInGraph(BlueprintPath, EventGraph);
	TestEqual(TEXT("Node count matches get_nodes_in_graph"), Graph.Nodes.Num(), LegacyNodes.Num());
	TestEqual(TEXT("Edge count matches get_connections"),
		Graph.Edges.Num(), UBlueprintService::GetConnections(BlueprintPath, EventGraph).Num());
	TestTrue(TEXT("No truncation below the created node count"),
		Graph.Nodes.Num() >= CreatedIds.Num());

	for (const FString& NodeId : CreatedIds)
	{
		if (!FindNode(Graph, NodeId))
		{
			AddError(FString::Printf(TEXT("Created node %s is missing from the snapshot"), *NodeId));
			break;
		}
	}

	return true;
}

#endif
