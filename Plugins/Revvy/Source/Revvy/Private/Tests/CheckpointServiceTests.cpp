// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

//
// Tests for FRevvyCheckpointService and the manage_checkpoints tool contract.
// The capture path is exercised through RecordBackup directly (a real package
// save cannot run inside a unit test), and restore is verified as a file
// round-trip on a fake, never-loaded package name. Entries are recorded under
// /Game/__RevvyQa/ so they are recognisable in a real checkpoint listing; they
// are bytes-sized and age out with normal pruning.
//

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/CheckpointService.h"
#include "Core/ToolRegistry.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ParseCheckpointJson(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyCheckpointRoundTripTest,
	"Revvy.Core.Checkpoints.RecordListRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyCheckpointRoundTripTest::RunTest(const FString& Parameters)
{
	FRevvyCheckpointService& Service = FRevvyCheckpointService::Get();

	const FString TestDir = FPaths::ProjectSavedDir() / TEXT("Revvy") / TEXT("CheckpointTests");
	const FString SourceFile = TestDir / TEXT("FakeAsset.uasset");
	const FString PackageName = TEXT("/Game/__RevvyQa/FakeAsset");

	if (!TestTrue(TEXT("test source file written"),
		FFileHelper::SaveStringToFile(TEXT("VERSION-ONE"), *SourceFile)))
	{
		return false;
	}

	// Capture the pre-change version.
	FString BackupId;
	if (!TestTrue(TEXT("RecordBackup succeeds"),
		Service.RecordBackup(PackageName, SourceFile, TEXT("qa_test"), BackupId)))
	{
		return false;
	}
	TestFalse(TEXT("backup id assigned"), BackupId.IsEmpty());

	// Simulate the tool overwriting the asset.
	FFileHelper::SaveStringToFile(TEXT("VERSION-TWO"), *SourceFile);

	// The entry is listed, newest first, and filterable.
	bool bListed = false;
	for (const TSharedPtr<FJsonObject>& Entry : Service.ListCheckpoints(TEXT("__RevvyQa"), 10))
	{
		FString Id;
		if (Entry->TryGetStringField(TEXT("id"), Id) && Id == BackupId)
		{
			bListed = true;
			TestEqual(TEXT("entry package"), Entry->GetStringField(TEXT("package")), PackageName);
			TestEqual(TEXT("entry tool"), Entry->GetStringField(TEXT("tool")), TEXT("qa_test"));
		}
	}
	TestTrue(TEXT("backup appears in list"), bListed);

	// Restore: the fake package is never loaded, so this is a plain file copy.
	FString Error;
	FString RestoredPackage;
	if (TestTrue(FString::Printf(TEXT("restore succeeds: %s"), *Error),
		Service.RestoreCheckpoint(BackupId, /*bForce=*/false, Error, RestoredPackage)))
	{
		TestEqual(TEXT("restored package name"), RestoredPackage, PackageName);
		FString Content;
		FFileHelper::LoadFileToString(Content, *SourceFile);
		TestEqual(TEXT("file content reverted"), Content, TEXT("VERSION-ONE"));
	}

	// Unknown id is a structured failure.
	FString UnknownError;
	FString Unused;
	TestFalse(TEXT("unknown id fails"),
		Service.RestoreCheckpoint(TEXT("no-such-id"), false, UnknownError, Unused));
	TestTrue(TEXT("unknown id error mentions list"), UnknownError.Contains(TEXT("list")));

	IFileManager::Get().DeleteDirectory(*TestDir, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyCheckpointToolContractTest,
	"Revvy.Core.Checkpoints.ToolContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyCheckpointToolContractTest::RunTest(const FString& Parameters)
{
	auto RunTool = [](std::initializer_list<TPair<FString, FString>> Args)
	{
		TMap<FString, FString> Params;
		for (const TPair<FString, FString>& Pair : Args)
		{
			Params.Add(Pair.Key, Pair.Value);
		}
		return FToolRegistry::Get().ExecuteTool(TEXT("manage_checkpoints"), Params);
	};

	TSharedPtr<FJsonObject> Status;
	if (!TestTrue(TEXT("status returns JSON"),
		ParseCheckpointJson(RunTool({{TEXT("action"), TEXT("status")}}), Status)))
	{
		return false;
	}
	TestTrue(TEXT("status has root_dir"), Status->HasField(TEXT("root_dir")));
	TestTrue(TEXT("status has scope_mode"), Status->HasField(TEXT("scope_mode")));

	TSharedPtr<FJsonObject> List;
	if (ParseCheckpointJson(RunTool({{TEXT("action"), TEXT("list")}}), List))
	{
		TestTrue(TEXT("list succeeds"), List->GetBoolField(TEXT("success")));
		TestTrue(TEXT("list has checkpoints array"), List->HasField(TEXT("checkpoints")));
	}

	TSharedPtr<FJsonObject> NoId;
	if (ParseCheckpointJson(RunTool({{TEXT("action"), TEXT("restore")}}), NoId))
	{
		TestFalse(TEXT("restore without id fails"), NoId->GetBoolField(TEXT("success")));
		TestEqual(TEXT("missing id code"), NoId->GetStringField(TEXT("error_code")), TEXT("MISSING_ID"));
	}

	TSharedPtr<FJsonObject> Unknown;
	if (ParseCheckpointJson(RunTool({{TEXT("action"), TEXT("defrag")}}), Unknown))
	{
		TestEqual(TEXT("unknown action code"),
			Unknown->GetStringField(TEXT("error_code")), TEXT("UNKNOWN_ACTION"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
