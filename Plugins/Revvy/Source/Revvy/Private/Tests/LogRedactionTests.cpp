// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

//
// Tests for the fail-closed argument-log redaction (MCP/LogRedaction.h).
// The contract: a secret never reaches the log verbatim, whether it arrives
// under a secret-shaped KEY or embedded as a secret-shaped assignment inside a
// serialized VALUE — while ordinary arguments, including prose that merely
// mentions the word "token", stay diagnosable.
//

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MCP/LogRedaction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyLogRedactionShapesTest,
	"Revvy.Core.LogRedaction.Shapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyLogRedactionShapesTest::RunTest(const FString& Parameters)
{
	using namespace RevvyLogRedaction;

	// Secret-shaped keys, any casing/affix, are redacted outright.
	for (const FString& Key : { FString(TEXT("api_key")), FString(TEXT("ApiKey")),
		FString(TEXT("bearer_token")), FString(TEXT("AUTHORIZATION")),
		FString(TEXT("client_secret")), FString(TEXT("proxy-password")) })
	{
		TestEqual(FString::Printf(TEXT("key '%s' redacts"), *Key),
			RedactArgumentForLog(Key, TEXT("hunter2")), FString(GRedactedPlaceholder));
	}

	// Secret-shaped assignments INSIDE a value (ParamsJson blobs, CLI strings).
	TestEqual(TEXT("json assignment redacts"),
		RedactArgumentForLog(TEXT("ParamsJson"), TEXT("{\"url\":\"x\",\"api_key\": \"sk-123\"}")),
		FString(GRedactedPlaceholder));
	TestEqual(TEXT("cli assignment redacts"),
		RedactArgumentForLog(TEXT("extra_args"), TEXT("--verbose --token=abc123")),
		FString(GRedactedPlaceholder));

	// Ordinary values stay verbatim — including prose that mentions the word.
	TestEqual(TEXT("plain value passes"),
		RedactArgumentForLog(TEXT("blueprint_path"), TEXT("/Game/BP_Player")),
		FString(TEXT("/Game/BP_Player")));
	TestEqual(TEXT("prose mention passes"),
		RedactArgumentForLog(TEXT("message"), TEXT("the progress token concept is unrelated")),
		FString(TEXT("the progress token concept is unrelated")));

	// Oversized values are elided with their true length reported.
	FString Long;
	for (int32 Index = 0; Index < 300; ++Index)
	{
		Long.AppendChar(TEXT('a'));
	}
	const FString Elided = RedactArgumentForLog(TEXT("script"), Long);
	TestTrue(TEXT("long value elided"), Elided.Len() < Long.Len());
	TestTrue(TEXT("elision reports true length"), Elided.Contains(TEXT("300 chars")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
