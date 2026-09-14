// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UAIPerceptionService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyAIPerception, Log, All);

// Editor / Engine
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// AI Perception
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AIPerceptionSystem.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// =================================================================
// Local helpers (private to this translation unit)
// =================================================================

namespace
{
	/** Locate the UAIPerceptionComponent on the target actor (or nullptr). */
	UAIPerceptionComponent* FindPerceptionComponent(const FString& Query)
	{
		AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, Query);
		if (!Actor)
		{
			return nullptr;
		}
		return Actor->FindComponentByClass<UAIPerceptionComponent>();
	}

	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString AIPerceptionJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
	{
		if (!JsonObj.IsValid())
		{
			return FString();
		}
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
		return OutStr;
	}

	/** Serialize a JSON array to a compact FString. */
	FString AIPerceptionJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** Append perceived actors for a specific sense class into the output array. */
	void AppendPerceivedForSense(
		UAIPerceptionComponent* PerceptionComp,
		TSubclassOf<UAISense> SenseClass,
		TArray<TSharedPtr<FJsonValue>>& OutJsonArr)
	{
		if (!PerceptionComp || !SenseClass)
		{
			return;
		}

		TArray<AActor*> Perceived;
		PerceptionComp->GetCurrentlyPerceivedActors(SenseClass, Perceived);
		for (AActor* Actor : Perceived)
		{
			if (!Actor) { continue; }

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
			Entry->SetStringField(TEXT("class"),
				Actor->GetClass() ? Actor->GetClass()->GetName() : FString());
			OutJsonArr.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
}

// =================================================================
// Query
// =================================================================

FString UAIPerceptionService::GetPerceptionComponentInfo(const FString& ActorNameOrPath)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("GetPerceptionComponentInfo: actor '%s' not found."), *ActorNameOrPath);
		return FString(TEXT("{}"));
	}

	UAIPerceptionComponent* PerceptionComp = Actor->FindComponentByClass<UAIPerceptionComponent>();
	if (!PerceptionComp)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("GetPerceptionComponentInfo: no UAIPerceptionComponent on '%s'."),
			*Actor->GetName());
		return FString(TEXT("{}"));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	// Sight config
	TSharedPtr<FJsonObject> SightObj = MakeShared<FJsonObject>();
	if (UAISenseConfig_Sight* SightCfg = PerceptionComp->GetSenseConfig<UAISenseConfig_Sight>())
	{
		SightObj->SetNumberField(TEXT("radius"), SightCfg->SightRadius);
		SightObj->SetNumberField(TEXT("lose_sight"), SightCfg->LoseSightRadius);
		SightObj->SetNumberField(TEXT("peripheral_angle"), SightCfg->PeripheralVisionAngleDegrees);
	}
	Root->SetObjectField(TEXT("sight_config"), SightObj);

	// Hearing config
	TSharedPtr<FJsonObject> HearingObj = MakeShared<FJsonObject>();
	if (UAISenseConfig_Hearing* HearingCfg = PerceptionComp->GetSenseConfig<UAISenseConfig_Hearing>())
	{
		HearingObj->SetNumberField(TEXT("range"), HearingCfg->HearingRange);
	}
	Root->SetObjectField(TEXT("hearing_config"), HearingObj);

	// Enumerated senses
	TArray<TSharedPtr<FJsonValue>> Senses;
	for (auto It = PerceptionComp->GetSensesConfigIterator(); It; ++It)
	{
		if (const UAISenseConfig* Cfg = *It)
		{
			if (Cast<const UAISenseConfig_Sight>(Cfg))
			{
				Senses.Add(MakeShared<FJsonValueString>(TEXT("Sight")));
			}
			else if (Cast<const UAISenseConfig_Hearing>(Cfg))
			{
				Senses.Add(MakeShared<FJsonValueString>(TEXT("Hearing")));
			}
			else
			{
				Senses.Add(MakeShared<FJsonValueString>(Cfg->GetClass()->GetName()));
			}
		}
	}
	Root->SetArrayField(TEXT("senses"), Senses);

	return AIPerceptionJsonObjectToString(Root);
}

FString UAIPerceptionService::GetPerceivedActors(
	const FString& AIActorNameOrPath,
	const FString& SenseNameStr)
{
	UAIPerceptionComponent* PerceptionComp = FindPerceptionComponent(AIActorNameOrPath);
	if (!PerceptionComp)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("GetPerceivedActors: no perception component for '%s'."),
			*AIActorNameOrPath);
		return AIPerceptionJsonArrayToString(TArray<TSharedPtr<FJsonValue>>());
	}

	TArray<TSharedPtr<FJsonValue>> OutArr;
	const FString Lower = SenseNameStr.ToLower();
	const bool bAll = Lower == TEXT("all") || Lower.IsEmpty();

	if (bAll || Lower == TEXT("sight"))
	{
		AppendPerceivedForSense(PerceptionComp, UAISense_Sight::StaticClass(), OutArr);
	}
	if (bAll || Lower == TEXT("hearing"))
	{
		AppendPerceivedForSense(PerceptionComp, UAISense_Hearing::StaticClass(), OutArr);
	}

	return AIPerceptionJsonArrayToString(OutArr);
}

// =================================================================
// Config
// =================================================================

bool UAIPerceptionService::SetSightRadius(
	const FString& AIActorNameOrPath,
	float SightRadius,
	float LoseSightRadius,
	float PeripheralVisionAngleDeg)
{
	UAIPerceptionComponent* PerceptionComp = FindPerceptionComponent(AIActorNameOrPath);
	if (!PerceptionComp)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("SetSightRadius: no perception component for '%s'."),
			*AIActorNameOrPath);
		return false;
	}

	UAISenseConfig_Sight* SightCfg = PerceptionComp->GetSenseConfig<UAISenseConfig_Sight>();
	if (!SightCfg)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("SetSightRadius: no UAISenseConfig_Sight on '%s'."),
			*AIActorNameOrPath);
		return false;
	}

	SightCfg->SightRadius = SightRadius;
	SightCfg->LoseSightRadius = LoseSightRadius;
	SightCfg->PeripheralVisionAngleDegrees = PeripheralVisionAngleDeg;

	PerceptionComp->ConfigureSense(*SightCfg);
	PerceptionComp->RequestStimuliListenerUpdate();
	return true;
}

bool UAIPerceptionService::SetHearingRange(const FString& AIActorNameOrPath, float HearingRange)
{
	UAIPerceptionComponent* PerceptionComp = FindPerceptionComponent(AIActorNameOrPath);
	if (!PerceptionComp)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("SetHearingRange: no perception component for '%s'."),
			*AIActorNameOrPath);
		return false;
	}

	UAISenseConfig_Hearing* HearingCfg = PerceptionComp->GetSenseConfig<UAISenseConfig_Hearing>();
	if (!HearingCfg)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("SetHearingRange: no UAISenseConfig_Hearing on '%s'."),
			*AIActorNameOrPath);
		return false;
	}

	HearingCfg->HearingRange = HearingRange;

	PerceptionComp->ConfigureSense(*HearingCfg);
	PerceptionComp->RequestStimuliListenerUpdate();
	return true;
}

// =================================================================
// Stimulus
// =================================================================

bool UAIPerceptionService::ReportNoiseEvent(
	const FString& InstigatorActorNameOrPath,
	FVector NoiseLocation,
	float Loudness,
	float MaxRange)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyAIPerception, Warning, TEXT("ReportNoiseEvent: no world available."));
		return false;
	}

	// Validate perception system reachable — required by the sense report path.
	if (UAIPerceptionSystem::GetCurrent(World) == nullptr)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("ReportNoiseEvent: no UAIPerceptionSystem in the current world."));
		return false;
	}

	AActor* Instigator = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, InstigatorActorNameOrPath);
	// Instigator is optional — nullptr is a valid engine argument.

	UAISense_Hearing::ReportNoiseEvent(
		World,
		NoiseLocation,
		Loudness,
		Instigator,
		MaxRange,
		NAME_None);
	return true;
}

bool UAIPerceptionService::ReportSightEvent(const FString& TargetActorNameOrPath, FVector /*TargetLocation*/)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyAIPerception, Warning, TEXT("ReportSightEvent: no world available."));
		return false;
	}

	AActor* Target = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, TargetActorNameOrPath);
	if (!Target)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("ReportSightEvent: target actor '%s' not found."), *TargetActorNameOrPath);
		return false;
	}

	UAIPerceptionSystem* PerceptionSys = UAIPerceptionSystem::GetCurrent(World);
	if (!PerceptionSys)
	{
		UE_LOG(LogRevvyAIPerception, Warning,
			TEXT("ReportSightEvent: no UAIPerceptionSystem in the current world."));
		return false;
	}

	// UAISense_Sight is passively evaluated — no direct ReportSightEvent API
	// exists on the engine sense. Registering the target as a sight source
	// makes it observable by any sight-enabled UAIPerceptionComponent this tick.
	PerceptionSys->RegisterSource<UAISense_Sight>(*Target);
	return true;
}
