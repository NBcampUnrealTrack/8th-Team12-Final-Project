// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

//
// Integration tests for the 2026-08-12 tool additions (run_automation_tests,
// pie_control, get_editor_context, lookup_class). Each test drives the tool
// through FToolRegistry::ExecuteTool - the same entry the MCP server uses - so
// they cover registration, annotation shape, parameter validation and the JSON
// contract, not just the tool bodies. Run them in-editor with:
//   run_automation_tests action=run, filter=Revvy.Tools.
// (run_automation_tests itself is exercised via 'list' only - a runner running
// itself would recurse.)
//

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/ToolRegistry.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ParseToolJson(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	FString RunTool(const FString& ToolName, std::initializer_list<TPair<FString, FString>> Args)
	{
		TMap<FString, FString> Params;
		for (const TPair<FString, FString>& Pair : Args)
		{
			Params.Add(Pair.Key, Pair.Value);
		}
		return FToolRegistry::Get().ExecuteTool(ToolName, Params);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyToolSurfaceRegistrationTest,
	"Revvy.Tools.Surface.Registration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyToolSurfaceRegistrationTest::RunTest(const FString& Parameters)
{
	const TArray<FToolMetadata> Tools = FToolRegistry::Get().GetEnabledTools();

	// No two tools may share a name - a duplicate silently shadows the loser.
	TSet<FString> Names;
	for (const FToolMetadata& Tool : Tools)
	{
		TestFalse(FString::Printf(TEXT("Duplicate tool name registered: %s"), *Tool.Name),
			Names.Contains(Tool.Name));
		Names.Add(Tool.Name);
	}

	// The 2026-08-12 additions are present with the intended annotation shapes.
	for (const FString& Expected : { FString(TEXT("run_automation_tests")), FString(TEXT("pie_control")),
		FString(TEXT("get_editor_context")), FString(TEXT("lookup_class")) })
	{
		TestTrue(FString::Printf(TEXT("Tool registered: %s"), *Expected), Names.Contains(Expected));
	}

	for (const FToolMetadata& Tool : Tools)
	{
		if (Tool.Name == TEXT("get_editor_context") || Tool.Name == TEXT("lookup_class"))
		{
			TestTrue(FString::Printf(TEXT("%s is annotated read-only"), *Tool.Name),
				Tool.Annotations.bAnnotated && Tool.Annotations.bReadOnlyHint);
		}
		if (Tool.Name == TEXT("run_automation_tests") || Tool.Name == TEXT("pie_control"))
		{
			TestTrue(FString::Printf(TEXT("%s is a write tool"), *Tool.Name),
				Tool.Annotations.bAnnotated && !Tool.Annotations.bReadOnlyHint);
			TestTrue(FString::Printf(TEXT("%s exposes read-only actions for /mcp/safe"), *Tool.Name),
				Tool.Annotations.ReadOnlyActions.Contains(TEXT("status")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyLookupClassResolvesActorTest,
	"Revvy.Tools.LookupClass.ResolvesActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyLookupClassResolvesActorTest::RunTest(const FString& Parameters)
{
	// Bare name resolves through prefix verification to the native class.
	TSharedPtr<FJsonObject> Result;
	if (!TestTrue(TEXT("lookup_class returns JSON"),
		ParseToolJson(RunTool(TEXT("lookup_class"), {{TEXT("class_name"), TEXT("Actor")}}), Result)))
	{
		return false;
	}
	TestTrue(TEXT("success"), Result->GetBoolField(TEXT("success")));
	TestEqual(TEXT("cpp_name"), Result->GetStringField(TEXT("cpp_name")), TEXT("AActor"));
	TestTrue(TEXT("native"), Result->GetBoolField(TEXT("native")));
	TestTrue(TEXT("inheritance chain reaches UObject"),
		Result->GetArrayField(TEXT("inheritance_chain")).Num() >= 1);
	TestTrue(TEXT("functions found"), Result->GetNumberField(TEXT("functions_total")) > 0);

	// Prefixed form resolves to the same class.
	TSharedPtr<FJsonObject> Prefixed;
	if (ParseToolJson(RunTool(TEXT("lookup_class"), {{TEXT("class_name"), TEXT("AActor")}}), Prefixed))
	{
		TestEqual(TEXT("prefixed input agrees"),
			Prefixed->GetStringField(TEXT("path")), Result->GetStringField(TEXT("path")));
	}

	// member_filter narrows and reports totals.
	TSharedPtr<FJsonObject> Filtered;
	if (ParseToolJson(RunTool(TEXT("lookup_class"),
		{{TEXT("class_name"), TEXT("Actor")}, {TEXT("member_filter"), TEXT("Destroy")}}), Filtered))
	{
		TestTrue(TEXT("filtered functions subset"),
			Filtered->GetNumberField(TEXT("functions_total")) < Result->GetNumberField(TEXT("functions_total")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyLookupClassMissingClassTest,
	"Revvy.Tools.LookupClass.MissingAndInvalid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyLookupClassMissingClassTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Result;
	if (!TestTrue(TEXT("lookup_class returns JSON for unknown class"),
		ParseToolJson(RunTool(TEXT("lookup_class"),
			{{TEXT("class_name"), TEXT("DefinitelyNotAClass_Xyzzy")}}), Result)))
	{
		return false;
	}
	TestFalse(TEXT("unknown class fails"), Result->GetBoolField(TEXT("success")));
	TestEqual(TEXT("error_code"), Result->GetStringField(TEXT("error_code")), TEXT("CLASS_NOT_FOUND"));

	// Registry-level validation refuses a call without the required parameter.
	TSharedPtr<FJsonObject> NoParam;
	if (ParseToolJson(RunTool(TEXT("lookup_class"), {}), NoParam))
	{
		TestFalse(TEXT("missing class_name fails validation"), NoParam->GetBoolField(TEXT("success")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyEditorContextSnapshotTest,
	"Revvy.Tools.EditorContext.Snapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyEditorContextSnapshotTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Result;
	if (!TestTrue(TEXT("get_editor_context returns JSON"),
		ParseToolJson(RunTool(TEXT("get_editor_context"), {}), Result)))
	{
		return false;
	}
	TestTrue(TEXT("success"), Result->GetBoolField(TEXT("success")));

	FString PlaySession;
	TestTrue(TEXT("play_session present"), Result->TryGetStringField(TEXT("play_session"), PlaySession));
	TestTrue(TEXT("play_session is a known state"),
		PlaySession == TEXT("stopped") || PlaySession == TEXT("playing") || PlaySession == TEXT("simulating"));

	FString Summary;
	TestTrue(TEXT("summary present"), Result->TryGetStringField(TEXT("summary"), Summary));
	TestTrue(TEXT("selection arrays present"),
		Result->HasField(TEXT("selected_actors")) && Result->HasField(TEXT("open_assets")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyPieControlStatusTest,
	"Revvy.Tools.PieControl.StatusContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyPieControlStatusTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Result;
	if (!TestTrue(TEXT("pie_control status returns JSON"),
		ParseToolJson(RunTool(TEXT("pie_control"), {{TEXT("action"), TEXT("status")}}), Result)))
	{
		return false;
	}
	TestTrue(TEXT("success"), Result->GetBoolField(TEXT("success")));
	FString State;
	TestTrue(TEXT("state present"), Result->TryGetStringField(TEXT("state"), State));

	// Stopping with no session must be a structured error, not a crash or a lie.
	TSharedPtr<FJsonObject> Stop;
	if (State == TEXT("stopped")
		&& ParseToolJson(RunTool(TEXT("pie_control"), {{TEXT("action"), TEXT("stop")}}), Stop))
	{
		TestFalse(TEXT("stop without session fails"), Stop->GetBoolField(TEXT("success")));
		TestEqual(TEXT("stop error code"),
			Stop->GetStringField(TEXT("error_code")), TEXT("PIE_NOT_RUNNING"));
	}

	TSharedPtr<FJsonObject> Unknown;
	if (ParseToolJson(RunTool(TEXT("pie_control"), {{TEXT("action"), TEXT("warp")}}), Unknown))
	{
		TestEqual(TEXT("unknown action code"),
			Unknown->GetStringField(TEXT("error_code")), TEXT("UNKNOWN_ACTION"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyAutomationToolListTest,
	"Revvy.Tools.AutomationRunner.ListContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyAutomationToolListTest::RunTest(const FString& Parameters)
{
	// 'list' must find this very test through the registry path.
	TSharedPtr<FJsonObject> Result;
	if (!TestTrue(TEXT("run_automation_tests list returns JSON"),
		ParseToolJson(RunTool(TEXT("run_automation_tests"),
			{{TEXT("action"), TEXT("list")}, {TEXT("filter"), TEXT("Revvy.Tools.")}}), Result)))
	{
		return false;
	}
	TestTrue(TEXT("success"), Result->GetBoolField(TEXT("success")));
	TestTrue(TEXT("finds the Revvy.Tools. tests"), Result->GetNumberField(TEXT("matched")) >= 1);

	// 'run' without a filter must refuse - running everything is never intended.
	TSharedPtr<FJsonObject> NoFilter;
	if (ParseToolJson(RunTool(TEXT("run_automation_tests"), {{TEXT("action"), TEXT("run")}}), NoFilter))
	{
		TestFalse(TEXT("run without filter fails"), NoFilter->GetBoolField(TEXT("success")));
		TestEqual(TEXT("missing filter code"),
			NoFilter->GetStringField(TEXT("error_code")), TEXT("MISSING_FILTER"));
	}

	// While THIS test executes, GIsAutomationTesting is set - 'run' must refuse
	// rather than preempt the in-flight framework state. Which guard fires depends
	// on the host: FRAMEWORK_BUSY under Session Frontend, TESTS_ALREADY_RUNNING
	// when this test is itself being run by the run_automation_tests tool.
	TSharedPtr<FJsonObject> Busy;
	if (ParseToolJson(RunTool(TEXT("run_automation_tests"),
		{{TEXT("action"), TEXT("run")}, {TEXT("filter"), TEXT("Revvy.Tools.")}}), Busy))
	{
		TestFalse(TEXT("run during a test refuses"), Busy->GetBoolField(TEXT("success")));
		const FString Code = Busy->GetStringField(TEXT("error_code"));
		TestTrue(TEXT("busy code is a concurrency guard"),
			Code == TEXT("FRAMEWORK_BUSY") || Code == TEXT("TESTS_ALREADY_RUNNING"));
	}

	// Status is callable any time; 'running' when self-hosted, 'idle' otherwise.
	TSharedPtr<FJsonObject> Status;
	if (ParseToolJson(RunTool(TEXT("run_automation_tests"), {{TEXT("action"), TEXT("status")}}), Status))
	{
		const FString RunnerState = Status->GetStringField(TEXT("status"));
		TestTrue(TEXT("runner status is idle or running"),
			RunnerState == TEXT("idle") || RunnerState == TEXT("running"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
