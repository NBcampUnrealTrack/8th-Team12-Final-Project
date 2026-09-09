// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UEnvironmentQueryService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyEQS, Log, All);

// Engine
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/Guid.h"

// EQS
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"

// Editor asset library (still used to load the UEnvQuery template)
#include "EditorAssetLibrary.h"

// Shared helpers (actor + world resolution)
#include "Core/URevvyCommonHelpers.h"

// JSON
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// =================================================================
// Local helpers (private to this translation unit)
// =================================================================

namespace
{
	/** Parse a run-mode string into EEnvQueryRunMode::Type. Defaults to SingleResult. */
	EEnvQueryRunMode::Type ParseRunMode(const FString& RunModeStr)
	{
		const FString Lower = RunModeStr.ToLower();

		if (Lower == TEXT("singlebest") || Lower == TEXT("singleresult") ||
			Lower == TEXT("best") || Lower == TEXT("single"))
		{
			return EEnvQueryRunMode::SingleResult;
		}
		if (Lower == TEXT("allmatching") || Lower == TEXT("all"))
		{
			return EEnvQueryRunMode::AllMatching;
		}
		if (Lower == TEXT("randombest5pct") || Lower == TEXT("random5") ||
			Lower == TEXT("randombest5"))
		{
			return EEnvQueryRunMode::RandomBest5Pct;
		}
		if (Lower == TEXT("randombest25pct") || Lower == TEXT("random25") ||
			Lower == TEXT("randombest25"))
		{
			return EEnvQueryRunMode::RandomBest25Pct;
		}

		UE_LOG(LogRevvyEQS, Warning,
			TEXT("ParseRunMode: unknown RunModeStr '%s' — defaulting to SingleResult."),
			*RunModeStr);
		return EEnvQueryRunMode::SingleResult;
	}

	// =============================================================
	// Active-query registry (translation-unit-local storage).
	//
	// Wrappers are kept alive by TStrongObjectPtr so they aren't
	// collected while the underlying EQS instance is still processing.
	// Receivers are stored in a parallel map keyed by the same handle
	// so their UFUNCTION binding to OnQueryFinishedEvent survives.
	// =============================================================
	TMap<FString, TStrongObjectPtr<UEnvQueryInstanceBlueprintWrapper>>& GetActiveQueries()
	{
		static TMap<FString, TStrongObjectPtr<UEnvQueryInstanceBlueprintWrapper>> Instance;
		return Instance;
	}

	TMap<FString, TStrongObjectPtr<UEQSQueryHandleReceiver>>& GetActiveReceivers()
	{
		static TMap<FString, TStrongObjectPtr<UEQSQueryHandleReceiver>> Instance;
		return Instance;
	}

	/** Erase a handle from both parallel registries. */
	void RemoveHandle(const FString& Handle)
	{
		GetActiveQueries().Remove(Handle);
		GetActiveReceivers().Remove(Handle);
	}

	/** Convert a JSON value array into a compact stringified JSON payload. */
	FString ArrayToJsonString(const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Values, Writer);
		return Out;
	}
}

// =================================================================
// UEQSQueryHandleReceiver
// =================================================================

void UEQSQueryHandleReceiver::OnQueryFinished(
	UEnvQueryInstanceBlueprintWrapper* /*QueryInstance*/,
	EEnvQueryStatus::Type /*QueryStatus*/)
{
	UE_LOG(LogRevvyEQS, Verbose,
		TEXT("OnQueryFinished: cleaning up handle '%s'."), *Handle);

	// Note: we intentionally do NOT drop the wrapper synchronously here —
	// callers may still want to query results after completion. The
	// AbortEQSQuery / GetEQS* code paths handle late cleanup. For now we
	// leave the entries live so `get_eqs_result_actors(handle)` after
	// completion still works. Callers should call AbortEQSQuery to
	// release strong refs.
	// If a stricter policy is desired, uncomment the line below.
	// RemoveHandle(Handle);
}

// =================================================================
// Query Execution
// =================================================================

FString UEnvironmentQueryService::RunEQSQuery(
	const FString& QueryTemplatePath,
	const FString& QuerierActorNameOrPath,
	const FString& RunModeStr)
{
	if (QueryTemplatePath.IsEmpty())
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("RunEQSQuery: QueryTemplatePath is empty."));
		return FString();
	}

	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("RunEQSQuery: no editor world available."));
		return FString();
	}

	UObject* LoadedTemplate = UEditorAssetLibrary::LoadAsset(QueryTemplatePath);
	UEnvQuery* QueryTemplate = Cast<UEnvQuery>(LoadedTemplate);
	if (!QueryTemplate)
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("RunEQSQuery: could not load UEnvQuery at '%s'."),
			*QueryTemplatePath);
		return FString();
	}

	AActor* Querier = URevvyCommonHelpers::FindActorByNameOrPath(World, QuerierActorNameOrPath);
	if (!Querier)
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("RunEQSQuery: could not resolve querier '%s'."),
			*QuerierActorNameOrPath);
		return FString();
	}

	const EEnvQueryRunMode::Type RunMode = ParseRunMode(RunModeStr);

	UEnvQueryInstanceBlueprintWrapper* Wrapper = UEnvQueryManager::RunEQSQuery(
		World,
		QueryTemplate,
		Querier,
		RunMode,
		UEnvQueryInstanceBlueprintWrapper::StaticClass());

	if (!Wrapper)
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("RunEQSQuery: UEnvQueryManager::RunEQSQuery returned null for '%s'."),
			*QueryTemplatePath);
		return FString();
	}

	const FString Handle =
		FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

	// Register the wrapper first so the finish-callback sees a live entry.
	GetActiveQueries().Add(Handle, TStrongObjectPtr<UEnvQueryInstanceBlueprintWrapper>(Wrapper));

	// Spawn + register a receiver, then bind it to OnQueryFinishedEvent.
	UEQSQueryHandleReceiver* Receiver =
		NewObject<UEQSQueryHandleReceiver>(GetTransientPackage());
	Receiver->Handle = Handle;
	GetActiveReceivers().Add(Handle, TStrongObjectPtr<UEQSQueryHandleReceiver>(Receiver));

	Wrapper->GetOnQueryFinishedEvent().AddDynamic(
		Receiver, &UEQSQueryHandleReceiver::OnQueryFinished);

	UE_LOG(LogRevvyEQS, Log,
		TEXT("RunEQSQuery: started '%s' as handle '%s' (RunMode=%d, querier='%s')."),
		*QueryTemplatePath, *Handle,
		static_cast<int32>(RunMode),
		*Querier->GetActorLabel());

	return Handle;
}

bool UEnvironmentQueryService::AbortEQSQuery(const FString& QueryHandle)
{
	TStrongObjectPtr<UEnvQueryInstanceBlueprintWrapper>* Found =
		GetActiveQueries().Find(QueryHandle);
	if (!Found || !Found->IsValid())
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("AbortEQSQuery: unknown handle '%s'."), *QueryHandle);
		return false;
	}

	if (UEnvQueryInstanceBlueprintWrapper* Wrapper = Found->Get())
	{
		Wrapper->GetOnQueryFinishedEvent().Clear();
	}

	RemoveHandle(QueryHandle);
	UE_LOG(LogRevvyEQS, Log,
		TEXT("AbortEQSQuery: cleaned up handle '%s'."), *QueryHandle);
	return true;
}

// =================================================================
// Query Status
// =================================================================

bool UEnvironmentQueryService::IsEQSQueryRunning(const FString& QueryHandle)
{
	TStrongObjectPtr<UEnvQueryInstanceBlueprintWrapper>* Found =
		GetActiveQueries().Find(QueryHandle);
	if (!Found || !Found->IsValid())
	{
		return false;
	}

	UEnvQueryInstanceBlueprintWrapper* Wrapper = Found->Get();
	if (!Wrapper)
	{
		return false;
	}

	if (const FEnvQueryResult* Result = Wrapper->GetQueryResult())
	{
		return !Result->IsFinished();
	}

	// No result payload yet — treat as still processing.
	return true;
}

// =================================================================
// Results
// =================================================================

FString UEnvironmentQueryService::GetEQSResultActors(const FString& QueryHandle)
{
	TStrongObjectPtr<UEnvQueryInstanceBlueprintWrapper>* Found =
		GetActiveQueries().Find(QueryHandle);
	if (!Found || !Found->IsValid())
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("GetEQSResultActors: unknown handle '%s'."), *QueryHandle);
		return TEXT("[]");
	}

	UEnvQueryInstanceBlueprintWrapper* Wrapper = Found->Get();
	if (!Wrapper)
	{
		return TEXT("[]");
	}

	const FEnvQueryResult* Result = Wrapper->GetQueryResult();
	if (!Result || !Result->IsSuccessful())
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("GetEQSResultActors: handle '%s' not in Success state (status=%d)."),
			*QueryHandle,
			Result ? static_cast<int32>(Result->GetRawStatus()) : -1);
		return TEXT("[]");
	}

	TArray<AActor*> Actors;
	if (!Wrapper->GetQueryResultsAsActors(Actors))
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("GetEQSResultActors: no actor results for handle '%s'."),
			*QueryHandle);
		return TEXT("[]");
	}

	TArray<TSharedPtr<FJsonValue>> Payload;
	Payload.Reserve(Actors.Num());
	for (int32 i = 0; i < Actors.Num(); ++i)
	{
		AActor* Actor = Actors[i];
		if (!Actor) { continue; }

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
		Entry->SetNumberField(TEXT("score"), Wrapper->GetItemScore(i));
		Payload.Add(MakeShared<FJsonValueObject>(Entry));
	}

	return ArrayToJsonString(Payload);
}

FString UEnvironmentQueryService::GetEQSResultLocations(const FString& QueryHandle)
{
	TStrongObjectPtr<UEnvQueryInstanceBlueprintWrapper>* Found =
		GetActiveQueries().Find(QueryHandle);
	if (!Found || !Found->IsValid())
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("GetEQSResultLocations: unknown handle '%s'."), *QueryHandle);
		return TEXT("[]");
	}

	UEnvQueryInstanceBlueprintWrapper* Wrapper = Found->Get();
	if (!Wrapper)
	{
		return TEXT("[]");
	}

	const FEnvQueryResult* Result = Wrapper->GetQueryResult();
	if (!Result || !Result->IsSuccessful())
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("GetEQSResultLocations: handle '%s' not in Success state (status=%d)."),
			*QueryHandle,
			Result ? static_cast<int32>(Result->GetRawStatus()) : -1);
		return TEXT("[]");
	}

	TArray<FVector> Locations;
	if (!Wrapper->GetQueryResultsAsLocations(Locations))
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("GetEQSResultLocations: no location results for handle '%s'."),
			*QueryHandle);
		return TEXT("[]");
	}

	TArray<TSharedPtr<FJsonValue>> Payload;
	Payload.Reserve(Locations.Num());
	for (int32 i = 0; i < Locations.Num(); ++i)
	{
		const FVector& L = Locations[i];
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("x"), L.X);
		Entry->SetNumberField(TEXT("y"), L.Y);
		Entry->SetNumberField(TEXT("z"), L.Z);
		Entry->SetNumberField(TEXT("score"), Wrapper->GetItemScore(i));
		Payload.Add(MakeShared<FJsonValueObject>(Entry));
	}

	return ArrayToJsonString(Payload);
}

float UEnvironmentQueryService::GetEQSResultScore(
	const FString& QueryHandle,
	int32 ItemIndex)
{
	TStrongObjectPtr<UEnvQueryInstanceBlueprintWrapper>* Found =
		GetActiveQueries().Find(QueryHandle);
	if (!Found || !Found->IsValid())
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("GetEQSResultScore: unknown handle '%s'."), *QueryHandle);
		return -1.0f;
	}

	UEnvQueryInstanceBlueprintWrapper* Wrapper = Found->Get();
	if (!Wrapper)
	{
		return -1.0f;
	}

	if (ItemIndex < 0)
	{
		UE_LOG(LogRevvyEQS, Warning,
			TEXT("GetEQSResultScore: negative ItemIndex %d for handle '%s'."),
			ItemIndex, *QueryHandle);
		return -1.0f;
	}

	return Wrapper->GetItemScore(ItemIndex);
}
