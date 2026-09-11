// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).
//
// discover_python_class used to serialize every method with its complete
// docstring and no ceiling. unreal.BlueprintService came back at 61,030
// characters, the caller's harness refused it, and the user got a wall of
// "result exceeds maximum allowed tokens" boilerplate instead of an answer —
// the run simply stopped. These pin the shape that keeps it moving: shorten
// prose first, drop methods only if that is not enough, and say which happened.

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tools/PythonDiscoveryService.h"
#include "Tools/PythonTools.h"

namespace
{
	TSharedPtr<FJsonObject> ParseBudgetResult(const FString& Json)
	{
		TSharedPtr<FJsonObject> Result;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Result);
		return Result;
	}

	/** A class whose methods carry `DocChars` of prose each. */
	Revvy::FPythonClassInfo MakeClassInfo(int32 MethodCount, int32 DocChars)
	{
		Revvy::FPythonClassInfo Info;
		Info.Name = TEXT("BlueprintService");
		Info.FullPath = TEXT("unreal.BlueprintService");
		Info.Docstring = TEXT("Blueprint service exposed directly to Python.");
		Info.BaseClasses.Add(TEXT("Object"));
		for (int32 Index = 0; Index < MethodCount; ++Index)
		{
			Revvy::FPythonFunctionInfo Method;
			Method.Name = FString::Printf(TEXT("method_%d"), Index);
			Method.Signature = TEXT("(blueprint_path, component_name) -> bool");
			Method.Docstring = FString::ChrN(DocChars, TCHAR('d'));
			Info.Methods.Add(Method);
		}
		return Info;
	}

	constexpr int32 BudgetChars = 24000;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyClassDiscoveryKeepsSmallClassesWholeTest,
	"Revvy.Tools.PythonClassDiscovery.KeepsSmallClassesWhole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyClassDiscoveryKeepsSmallClassesWholeTest::RunTest(const FString&)
{
	// A class that fits must come back byte-for-byte as before: the budget is a
	// ceiling, not a reformat, and most callers should see no change at all.
	const FString Json = UPythonTools::ConvertClassInfoToJson(MakeClassInfo(4, 120));
	const TSharedPtr<FJsonObject> Result = ParseBudgetResult(Json);
	TestTrue(TEXT("parses"), Result.IsValid());
	TestTrue(TEXT("within budget"), Json.Len() <= BudgetChars);
	TestFalse(TEXT("no trim flag"), Result->HasField(TEXT("docstrings_trimmed")));
	TestFalse(TEXT("no truncation flag"), Result->HasField(TEXT("truncated")));

	const TArray<TSharedPtr<FJsonValue>>* Methods = nullptr;
	TestTrue(TEXT("has methods"), Result->TryGetArrayField(TEXT("methods"), Methods));
	if (Methods)
	{
		TestEqual(TEXT("every method present"), Methods->Num(), 4);
		const TSharedPtr<FJsonObject> First = (*Methods)[0]->AsObject();
		TestEqual(TEXT("docstring untouched"), First->GetStringField(TEXT("docstring")).Len(), 120);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyClassDiscoveryTrimsProseBeforeDroppingMethodsTest,
	"Revvy.Tools.PythonClassDiscovery.TrimsProseBeforeDroppingMethods",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyClassDiscoveryTrimsProseBeforeDroppingMethodsTest::RunTest(const FString&)
{
	// 50 methods x 1200 chars of prose is the real BlueprintService shape. The
	// names and signatures are the index a caller picks its next move from, so
	// they must all survive; the prose behind any one of them is one
	// discover_python_function away.
	const FString Json = UPythonTools::ConvertClassInfoToJson(MakeClassInfo(50, 1200));
	const TSharedPtr<FJsonObject> Result = ParseBudgetResult(Json);
	TestTrue(TEXT("parses"), Result.IsValid());
	TestTrue(TEXT("within budget"), Json.Len() <= BudgetChars);
	TestTrue(TEXT("says prose was shortened"), Result->GetBoolField(TEXT("docstrings_trimmed")));
	TestFalse(TEXT("kept every method"), Result->HasField(TEXT("truncated")));

	const TArray<TSharedPtr<FJsonValue>>* Methods = nullptr;
	Result->TryGetArrayField(TEXT("methods"), Methods);
	if (Methods)
	{
		TestEqual(TEXT("no method lost"), Methods->Num(), 50);
	}
	TestTrue(TEXT("tells the caller what to do next"), Result->HasField(TEXT("next_step")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyClassDiscoveryBoundsEvenHugeClassesTest,
	"Revvy.Tools.PythonClassDiscovery.BoundsEvenHugeClasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyClassDiscoveryBoundsEvenHugeClassesTest::RunTest(const FString&)
{
	// Past the point where shortening prose is enough, methods start being held
	// back — but the reply stays inside the budget and reports the real total,
	// so the caller can narrow instead of being handed an error.
	const FString Json = UPythonTools::ConvertClassInfoToJson(MakeClassInfo(4000, 400));
	const TSharedPtr<FJsonObject> Result = ParseBudgetResult(Json);
	TestTrue(TEXT("parses"), Result.IsValid());
	TestTrue(TEXT("within budget"), Json.Len() <= BudgetChars);
	TestTrue(TEXT("says it truncated"), Result->GetBoolField(TEXT("truncated")));
	TestEqual(TEXT("reports the real total"), (int32)Result->GetNumberField(TEXT("total_methods")), 4000);

	const TArray<TSharedPtr<FJsonValue>>* Methods = nullptr;
	Result->TryGetArrayField(TEXT("methods"), Methods);
	if (Methods)
	{
		// Returning nothing would be within budget and useless; the point of the
		// binary search is to fill the budget rather than bail out of it.
		TestTrue(TEXT("still returns methods"), Methods->Num() > 0);
		TestEqual(
			TEXT("returned count matches the array"),
			(int32)Result->GetNumberField(TEXT("returned_methods")),
			Methods->Num());
	}
	return true;
}

#endif
