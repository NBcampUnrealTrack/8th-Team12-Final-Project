// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/ToolRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyReadOnlyActionAnnotationTest,
	"Revvy.Core.ToolScope.ReadOnlyActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyReadOnlyActionAnnotationTest::RunTest(const FString& Parameters)
{
	const FToolRegistry& Registry = FToolRegistry::Get();

	// These lists decide what an unauthenticated Safe session may run, so a verb
	// added here must be provably non-mutating in the tool's own handler.
	struct FExpectation
	{
		const TCHAR* Tool;
		TArray<FString> Allowed;
		TArray<FString> Forbidden;
	};

	const TArray<FExpectation> Expectations = {
		{
			TEXT("manage_asset"),
			{TEXT("search"), TEXT("find"), TEXT("list"), TEXT("help")},
			{TEXT("open"), TEXT("save"), TEXT("save_all"), TEXT("duplicate"),
			 TEXT("move"), TEXT("delete"), TEXT("import")}
		},
		{
			TEXT("terrain_data"),
			{TEXT("preview_elevation"), TEXT("list_styles")},
			{TEXT("generate_heightmap"), TEXT("get_map_image"), TEXT("get_water_features")}
		},
		{
			TEXT("manage_skills"),
			{TEXT("list"), TEXT("suggest")},
			{TEXT("load")}
		},
	};

	for (const FExpectation& Expected : Expectations)
	{
		const FToolMetadata* Tool = Registry.FindTool(Expected.Tool);
		if (!TestNotNull(*FString::Printf(TEXT("%s is registered"), Expected.Tool), Tool))
		{
			continue;
		}

		TestTrue(
			*FString::Printf(TEXT("%s carries explicit annotations"), Expected.Tool),
			Tool->Annotations.bAnnotated);

		for (const FString& Action : Expected.Allowed)
		{
			TestTrue(
				*FString::Printf(TEXT("%s allows '%s' in a read-only session"), Expected.Tool, *Action),
				Tool->Annotations.ReadOnlyActions.Contains(Action));
		}
		for (const FString& Action : Expected.Forbidden)
		{
			TestFalse(
				*FString::Printf(TEXT("%s never allows '%s' in a read-only session"), Expected.Tool, *Action),
				Tool->Annotations.ReadOnlyActions.Contains(Action));
		}

		for (const FString& Action : Tool->Annotations.ReadOnlyActions)
		{
			TestEqual(
				*FString::Printf(TEXT("%s read-only action '%s' is normalized"), Expected.Tool, *Action),
				Action, Action.TrimStartAndEnd().ToLower());
		}
	}

	// A fully read-only tool needs no action list; the whole tool is already allowed.
	if (const FToolMetadata* Discovery = Registry.FindTool(TEXT("discover_python_class")))
	{
		TestTrue(TEXT("discover_python_class stays fully read-only"), Discovery->Annotations.bReadOnlyHint);
		TestEqual(
			TEXT("A fully read-only tool carries no action list"),
			Discovery->Annotations.ReadOnlyActions.Num(), 0);
	}

	// read_blueprint is the reason a read-only session can see a Blueprint at all.
	// All three flags are asserted because IsSafeReadOnlyTool() reads all three:
	// bReadOnlyHint alone, with bDestructiveHint left at its spec default of
	// true, would silently drop the tool from read-only sessions.
	{
		const FToolMetadata* Blueprint = Registry.FindTool(TEXT("read_blueprint"));
		TestNotNull(TEXT("read_blueprint is registered"), Blueprint);
		if (Blueprint)
		{
			TestTrue(TEXT("read_blueprint is annotated"), Blueprint->Annotations.bAnnotated);
			TestTrue(TEXT("read_blueprint is read-only"), Blueprint->Annotations.bReadOnlyHint);
			TestFalse(TEXT("read_blueprint is not destructive"), Blueprint->Annotations.bDestructiveHint);
			TestEqual(
				TEXT("A fully read-only tool carries no action list"),
				Blueprint->Annotations.ReadOnlyActions.Num(), 0);
		}
	}

	// execute_python_code must never become reachable from a read-only session.
	if (const FToolMetadata* Python = Registry.FindTool(TEXT("execute_python_code")))
	{
		TestFalse(TEXT("execute_python_code is not read-only"), Python->Annotations.bReadOnlyHint);
		TestEqual(
			TEXT("execute_python_code exposes no read-only action"),
			Python->Annotations.ReadOnlyActions.Num(), 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
