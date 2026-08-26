// Fill out your copyright notice in the Description page of Project Settings.


#include "Activity/ActivitySubsystem.h"
#include "Activity/ActivitySetting.h"
#include "Activity/TestActivityAsset.h"
#include "GameMode/TimeSubsystem.h"

void UActivitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UTimeSubsystem>();
	Super::Initialize(Collection);
	const auto* S = GetDefault<UActivitySetting>();
	
	FixedEvents = S->FixedEventAsset.LoadSynchronous();
	
	
	if (UTimeSubsystem* TimeSub = GetGameInstance()->GetSubsystem<UTimeSubsystem>())
	{
		TimeSub->OnScheduleChanged.AddDynamic(this, &UActivitySubsystem::OnChangeSchedule);
	}
	
	
}

void UActivitySubsystem::FindRunnableActivities()
{
	const UWorld* World = GetWorld();
	
	const FName NowLevelName = FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
	
	if (!World || !FixedEvents) return;
	
	for (const auto& Pair : FixedEvents->Activities)
	{
		for (const FActivityLocation& Loc : Pair.Value.ActivityLocations)
		{
			for (const TSoftObjectPtr<UWorld>& Level : Loc.ConnectedLevel)
			{
				const FName TargetLevelName = Level.ToSoftObjectPath().GetLongPackageFName();
				if (TargetLevelName == NowLevelName)
				{
					RunnableActivities.Add(Pair.Key, Pair.Value);
				}
			}
		}
	}
	
	if (GEngine)
	{
		for (const auto& Pair : RunnableActivities)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, FString::Printf(TEXT("%s"), *Pair.Key.GetTagName().ToString()));
		}
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, FString::Printf(TEXT("Founded Activities")));
	}
}

FGameplayTag UActivitySubsystem::GetNowScheduleTag() const 
{
	return NowSchedule;
}


void UActivitySubsystem::OnChangeSchedule(FGameplayTag ChangeSchedule)
{
	NowSchedule = ChangeSchedule;
	UE_LOG(LogTemp, Warning, TEXT("EventSubsystem: Detect Schedule Change"));
	UE_LOG(LogTemp, Warning, TEXT("EventSubsystem: To %s"), *NowSchedule.ToString());
}


