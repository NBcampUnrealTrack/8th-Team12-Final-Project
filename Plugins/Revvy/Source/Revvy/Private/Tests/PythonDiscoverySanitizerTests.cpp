// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tools/PythonDiscoveryService.h"
#include "Tools/PythonTools.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using Revvy::FPythonDiscoveryService;

namespace
{
	TSharedPtr<FJsonObject> ParseToolResult(const FString& Json)
	{
		TSharedPtr<FJsonObject> Result;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Result);
		return Result;
	}

	/** Closes the generated string literal, runs a payload, reopens the literal. */
	const TCHAR* const InjectionPayload =
		TEXT("x'); __import__('builtins').revvy_injection_marker = True; ('");

	bool HasUnescapedSingleQuote(const FString& Value)
	{
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			if (Value[Index] != TEXT('\''))
			{
				continue;
			}

			int32 BackslashCount = 0;
			for (int32 PrefixIndex = Index - 1;
				PrefixIndex >= 0 && Value[PrefixIndex] == TEXT('\\');
				--PrefixIndex)
			{
				++BackslashCount;
			}
			if ((BackslashCount % 2) == 0)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyPythonIdentifierValidationTest,
	"Revvy.Tools.PythonDiscovery.IdentifierValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyPythonIdentifierValidationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A class name is an identifier"),
		FPythonDiscoveryService::IsPythonIdentifier(TEXT("EditorActorSubsystem")));
	TestTrue(TEXT("A leading underscore is legal"),
		FPythonDiscoveryService::IsPythonIdentifier(TEXT("_private")));
	TestTrue(TEXT("A trailing digit is legal"),
		FPythonDiscoveryService::IsPythonIdentifier(TEXT("Vector2")));

	TestFalse(TEXT("An empty name is not an identifier"),
		FPythonDiscoveryService::IsPythonIdentifier(TEXT("")));
	TestFalse(TEXT("A leading digit is not an identifier"),
		FPythonDiscoveryService::IsPythonIdentifier(TEXT("2Vector")));
	TestFalse(TEXT("A dotted path is not a plain identifier"),
		FPythonDiscoveryService::IsPythonIdentifier(TEXT("unreal.Vector")));
	TestFalse(TEXT("Whitespace is not an identifier"),
		FPythonDiscoveryService::IsPythonIdentifier(TEXT("Editor Actor")));
	TestFalse(TEXT("An injection payload is not an identifier"),
		FPythonDiscoveryService::IsPythonIdentifier(InjectionPayload));

	TestTrue(TEXT("A bare function name is a dotted identifier"),
		FPythonDiscoveryService::IsDottedPythonIdentifier(TEXT("load_asset")));
	TestTrue(TEXT("Class.method is a dotted identifier"),
		FPythonDiscoveryService::IsDottedPythonIdentifier(TEXT("EditorActorSubsystem.spawn_actor_from_class")));

	TestFalse(TEXT("A leading dot is rejected"),
		FPythonDiscoveryService::IsDottedPythonIdentifier(TEXT(".load_asset")));
	TestFalse(TEXT("A trailing dot is rejected"),
		FPythonDiscoveryService::IsDottedPythonIdentifier(TEXT("load_asset.")));
	TestFalse(TEXT("A doubled dot is rejected"),
		FPythonDiscoveryService::IsDottedPythonIdentifier(TEXT("a..b")));
	TestFalse(TEXT("An injection payload is not a dotted identifier"),
		FPythonDiscoveryService::IsDottedPythonIdentifier(InjectionPayload));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyPythonLiteralEscapingTest,
	"Revvy.Tools.PythonDiscovery.LiteralEscaping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyPythonLiteralEscapingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Ordinary text is unchanged"),
		FPythonDiscoveryService::EscapePythonStringLiteral(TEXT("Blueprint")), FString(TEXT("Blueprint")));
	TestEqual(TEXT("A quote can no longer close the literal"),
		FPythonDiscoveryService::EscapePythonStringLiteral(TEXT("it's")), FString(TEXT("it\\'s")));
	TestEqual(TEXT("A backslash is doubled before the quote rule applies"),
		FPythonDiscoveryService::EscapePythonStringLiteral(TEXT("a\\'b")), FString(TEXT("a\\\\\\'b")));
	TestEqual(TEXT("A newline cannot break out of the statement"),
		FPythonDiscoveryService::EscapePythonStringLiteral(TEXT("a\nb")), FString(TEXT("a\\nb")));

	const FString EscapedPayload = FPythonDiscoveryService::EscapePythonStringLiteral(InjectionPayload);
	TestFalse(TEXT("No bare quote survives escaping"), HasUnescapedSingleQuote(EscapedPayload));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyPythonDiscoveryRejectsInjectionTest,
	"Revvy.Tools.PythonDiscovery.RejectsInjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyPythonDiscoveryRejectsInjectionTest::RunTest(const FString& Parameters)
{
	// discover_python_* carry readOnlyHint, so they are reachable from the
	// unauthenticated Safe endpoint. A payload must be refused, not executed.
	const TSharedPtr<FJsonObject> ClassResult =
		ParseToolResult(UPythonTools::DiscoverPythonClass(InjectionPayload));
	if (TestTrue(TEXT("discover_python_class returned valid JSON"), ClassResult.IsValid()))
	{
		TestFalse(TEXT("An injection payload is not a discoverable class"),
			ClassResult->GetBoolField(TEXT("success")));
		TestEqual(TEXT("It is rejected as a bad parameter, not attempted"),
			ClassResult->GetStringField(TEXT("error_code")), FString(TEXT("PARAM_INVALID")));
	}

	const TSharedPtr<FJsonObject> FunctionResult =
		ParseToolResult(UPythonTools::DiscoverPythonFunction(InjectionPayload));
	if (TestTrue(TEXT("discover_python_function returned valid JSON"), FunctionResult.IsValid()))
	{
		TestFalse(TEXT("An injection payload is not a discoverable function"),
			FunctionResult->GetBoolField(TEXT("success")));
		TestEqual(TEXT("It is rejected as a bad parameter, not attempted"),
			FunctionResult->GetStringField(TEXT("error_code")), FString(TEXT("PARAM_INVALID")));
	}

	// The payload sets a module-level marker if it ever reaches the interpreter.
	const TSharedPtr<FJsonObject> MarkerResult = ParseToolResult(UPythonTools::ExecutePythonCode(TEXT(
		"import builtins\n"
		"print('marker_present' if hasattr(builtins, 'revvy_injection_marker') else 'marker_absent')")));
	if (TestTrue(TEXT("Marker probe returned valid JSON"), MarkerResult.IsValid()))
	{
		TestTrue(TEXT("The payload never executed"),
			MarkerResult->GetStringField(TEXT("output")).Contains(TEXT("marker_absent")));
	}

	// A legitimate name must still pass validation — the guard is not a blanket block.
	const TSharedPtr<FJsonObject> ValidResult =
		ParseToolResult(UPythonTools::DiscoverPythonClass(TEXT("unreal.EditorActorSubsystem")));
	if (TestTrue(TEXT("A valid class name returned valid JSON"), ValidResult.IsValid()))
	{
		FString ErrorCode;
		ValidResult->TryGetStringField(TEXT("error_code"), ErrorCode);
		TestNotEqual(TEXT("A real class name is not rejected as invalid"),
			ErrorCode, FString(TEXT("PARAM_INVALID")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
