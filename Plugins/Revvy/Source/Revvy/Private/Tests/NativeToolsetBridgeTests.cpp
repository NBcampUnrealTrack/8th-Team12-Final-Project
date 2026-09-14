// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MCP/MCPServer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ParseNativeBridgeJsonObject(
		const FString& Json,
		TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject)
			&& OutObject.IsValid();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyNativeToolsetCatalogBoundaryTest,
	"Revvy.Core.NativeToolset.UnauthenticatedCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyNativeToolsetCatalogBoundaryTest::RunTest(const FString& Parameters)
{
	TArray<TSharedPtr<FJsonValue>> Tools;
	const FString Json =
		FMCPServer::Get().GetUnauthenticatedNativeToolCatalogJson();
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	if (!TestTrue(
		TEXT("Native Toolset catalogue is a JSON array"),
		FJsonSerializer::Deserialize(Reader, Tools)))
	{
		return false;
	}

	TSet<FString> Names;
	for (const TSharedPtr<FJsonValue>& Value : Tools)
	{
		const TSharedPtr<FJsonObject> Tool = Value.IsValid()
			? Value->AsObject()
			: nullptr;
		if (!Tool.IsValid())
		{
			AddError(TEXT("Native Toolset catalogue contains a non-object entry"));
			continue;
		}
		FString Name;
		if (Tool->TryGetStringField(TEXT("name"), Name))
		{
			Names.Add(Name);
		}
	}

	const TSet<FString> Expected = {
		TEXT("discover_python_class"),
		TEXT("discover_python_function"),
		TEXT("discover_python_module"),
		TEXT("list_python_subsystems"),
		TEXT("lookup_class")
	};
	TestEqual(
		TEXT("Native Toolset catalogue has the expected size"),
		Names.Num(),
		Expected.Num());
	for (const FString& ExpectedName : Expected)
	{
		TestTrue(
			*FString::Printf(TEXT("%s is exposed"), *ExpectedName),
			Names.Contains(ExpectedName));
	}
	for (const FString& ActualName : Names)
	{
		TestTrue(
			*FString::Printf(TEXT("%s is explicitly allow-listed"), *ActualName),
			Expected.Contains(ActualName));
	}
	TestFalse(TEXT("read_logs is never exposed"), Names.Contains(TEXT("read_logs")));
	TestFalse(
		TEXT("deep_research is never exposed"),
		Names.Contains(TEXT("deep_research")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyNativeToolsetExecutionBoundaryTest,
	"Revvy.Core.NativeToolset.ExecutionBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyNativeToolsetExecutionBoundaryTest::RunTest(
	const FString& Parameters)
{
	for (const FString& DeniedTool : {
		FString(TEXT("read_logs")),
		FString(TEXT("deep_research")),
		FString(TEXT("execute_python_code"))})
	{
		const FString Response =
			FMCPServer::Get().ExecuteUnauthenticatedNativeToolInProcess(
				DeniedTool,
				TEXT("{}"));
		TSharedPtr<FJsonObject> Object;
		if (!TestTrue(
			*FString::Printf(TEXT("%s returns JSON"), *DeniedTool),
			ParseNativeBridgeJsonObject(Response, Object)))
		{
			continue;
		}
		TestFalse(
			*FString::Printf(TEXT("%s is denied"), *DeniedTool),
			Object->GetBoolField(TEXT("success")));
		TestEqual(
			*FString::Printf(TEXT("%s is denied by native scope"), *DeniedTool),
			Object->GetStringField(TEXT("error_code")),
			FString(TEXT("NATIVE_SCOPE_DENIED")));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
