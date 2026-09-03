// Fill out your copyright notice in the Description page of Project Settings.

#include "Activity/ActivitySubsystem.h"
#include "Activity/ActivitySetting.h"
#include "GameMode/TimeSubsystem.h"
#include "Kismet/GameplayStatics.h"

void UActivitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UTimeSubsystem>();
	Super::Initialize(Collection);
	const auto* S = GetDefault<UActivitySetting>();

	FixedEvents = S->FixedEventAsset.LoadSynchronous();

	TArray<FActivityDefinition*> Rows;
	FixedEvents->GetAllRows(TEXT("Fixed Event"), Rows);

	for (const FName& RowName : FixedEvents->GetRowNames())
	{
		if (const FActivityDefinition* Row = FixedEvents->FindRow<FActivityDefinition>(RowName, TEXT("Make Activity Map"), false))
		{
			AllActivities.Add(Row->ActivityNameTag, *Row);
		}
	}

	if (UTimeSubsystem* TimeSub = GetGameInstance()->GetSubsystem<UTimeSubsystem>())
	{
		TimeSub->OnScheduleChanged.AddDynamic(this, &UActivitySubsystem::OnChangeSchedule);
	}

	// 레벨 로드 완료 시 활동 목록 재검사
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UActivitySubsystem::OnLevelLoaded);

	CurrentAP = 6;
	MaxAP = 6;
}

void UActivitySubsystem::FindRunnableActivities()
{
	const UWorld* World = GetWorld();
	if (!World || !FixedEvents) return;

	const FName NowLevelName = FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));

	RunnableActivities.Empty();

	for (const auto& Pair : AllActivities)
	{
		for (const FActivityLocation& Loc : Pair.Value.ActivityLocations)
		{
			UE_LOG(LogTemp, Warning, TEXT("Tag=%s Valid=%d Now=%s"), *Loc.ConnectedScheduleTag.ToString(), Loc.ConnectedScheduleTag.IsValid(), *NowSchedule.ToString());
			if (Loc.ConnectedScheduleTag.IsValid() && Loc.ConnectedScheduleTag != NowSchedule)
			{
				continue;
			}

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
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green, TEXT("Founded Activities"));
	}
}

FGameplayTag UActivitySubsystem::GetNowScheduleTag() const
{
	return NowSchedule;
}

const TMap<FGameplayTag, FActivityDefinition>& UActivitySubsystem::GetAllActivities() const
{
	return AllActivities;
}

const TMap<FGameplayTag, FActivityDefinition>& UActivitySubsystem::GetRunnableActivities() const
{
	return RunnableActivities;
}

void UActivitySubsystem::OnChangeSchedule(FGameplayTag ChangedSchedule)
{
	NowSchedule = ChangedSchedule;
	UE_LOG(LogTemp, Warning, TEXT("EventSubsystem: Detect Schedule Change"));
	UE_LOG(LogTemp, Warning, TEXT("EventSubsystem: To %s"), *NowSchedule.ToString());

	FindRunnableActivities();
}

void UActivitySubsystem::OnLevelLoaded(UWorld* LoadedWorld)
{
	FindRunnableActivities();
}

bool UActivitySubsystem::TryConsumeAP(int32 Cost)
{
	if (CurrentAP < Cost)
	{
		UE_LOG(LogTemp, Warning, TEXT("TryConsumeAP: Not enough AP (Current: %d, Required: %d)"), CurrentAP, Cost);
		return false;
	}

	CurrentAP -= Cost;
	UE_LOG(LogTemp, Log, TEXT("TryConsumeAP: Consumed %d AP (Remaining: %d)"), Cost, CurrentAP);
	return true;
}

void UActivitySubsystem::GoHome()
{
	if (CurrentAP == 0)
	{
		const UActivitySetting* AS = GetDefault<UActivitySetting>();
		UGameplayStatics::OpenLevelBySoftObjectPtr(this, *AS->Levels.Find("Room"));
	}
}

void UActivitySubsystem::RunActivity(FGameplayTag ActivityTag)
{
	const FActivityDefinition* Definition = RunnableActivities.Find(ActivityTag);
	if (!Definition)
	{
		UE_LOG(LogTemp, Warning, TEXT("RunActivity: Activity not runnable - %s"), *ActivityTag.ToString());
		return;
	}

	if (!TryConsumeAP(Definition->ActionPointCost))
	{
		return;
	}

	OnActivityRequested.Broadcast(ActivityTag, *Definition);
	GoHome();

}
