// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tools/PythonTools.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TSharedPtr<FJsonObject> ParsePythonToolResult(const FString& Json)
	{
		TSharedPtr<FJsonObject> Result;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Result);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyPythonWarningDoesNotFailTest,
	"Revvy.Tools.Python.DeprecationWarningDoesNotFail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyPythonWarningDoesNotFailTest::RunTest(const FString& Parameters)
{
	const FString Json = UPythonTools::ExecutePythonCode(TEXT(
		"import warnings\n"
		"warnings.simplefilter('always', DeprecationWarning)\n"
		"warnings.warn('revvy expected deprecation warning', DeprecationWarning)\n"
		"print('revvy_warning_ok')"));
	const TSharedPtr<FJsonObject> Result = ParsePythonToolResult(Json);
	if (!TestTrue(TEXT("Python tool returned valid JSON"), Result.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("A warning does not fail successful Python"), Result->GetBoolField(TEXT("success")));
	TestTrue(TEXT("stdout is preserved"), Result->GetStringField(TEXT("output")).Contains(TEXT("revvy_warning_ok")));
	const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
	TestTrue(TEXT("Warnings are exposed separately"), Result->TryGetArrayField(TEXT("warnings"), Warnings));
	TestTrue(TEXT("The warning text is retained"), Warnings && Warnings->ContainsByPredicate([](const TSharedPtr<FJsonValue>& Value)
	{
		return Value.IsValid() && Value->AsString().Contains(TEXT("revvy expected deprecation warning"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyPythonRuntimeExceptionRecoveryTest,
	"Revvy.Tools.Python.RuntimeExceptionDoesNotPoison",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyPythonRuntimeExceptionRecoveryTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Traceback (most recent call last):"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("File \"<string>\", line 1, in <module>"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("RuntimeError: revvy expected runtime exception"), EAutomationExpectedErrorFlags::Contains, 1, false);

	const TSharedPtr<FJsonObject> Failure = ParsePythonToolResult(
		UPythonTools::ExecutePythonCode(TEXT("raise RuntimeError('revvy expected runtime exception')")));
	if (!TestTrue(TEXT("Runtime exception returned valid JSON"), Failure.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("A normal Python exception still fails the command"), Failure->GetBoolField(TEXT("success")));
	TestEqual(TEXT("A normal exception is not a hard-crash code"),
		Failure->GetStringField(TEXT("error_code")), FString(TEXT("PYTHON_RUNTIME_ERROR")));

	const TSharedPtr<FJsonObject> Recovery = ParsePythonToolResult(
		UPythonTools::ExecutePythonCode(TEXT("print('revvy_runtime_recovered')")));
	if (!TestTrue(TEXT("Recovery command returned valid JSON"), Recovery.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("The interpreter remains usable after a normal exception"), Recovery->GetBoolField(TEXT("success")));
	TestTrue(TEXT("Recovery stdout is preserved"), Recovery->GetStringField(TEXT("output")).Contains(TEXT("revvy_runtime_recovered")));
	return true;
}

#endif
