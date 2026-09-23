// Fill out your copyright notice in the Description page of Project Settings.


#include "Cache/ActorCacheSubsystem.h"

#include "Engine/TargetPoint.h"
#include "GameMode/TargetTypeComponent.h"

void UActorCacheSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
}

FTransform UActorCacheSubsystem::GetTargetTransform(FName TargetTag, ETargetTypeRole TargetTypeRole)
{
	if (TObjectPtr<ATargetPoint> NowTarget = *CachedTargetPoints.Find(TargetTag))
	{
		if (UTargetTypeComponent* TargetTypeComponent = NowTarget->GetComponentByClass<UTargetTypeComponent>())
		{
			if (TargetTypeComponent->TargetTypeRole == TargetTypeRole)
			{
				return NowTarget->GetTransform();
			}
			UE_LOG(LogTemp, Warning, TEXT("%s: TargetPoint와 목표 타입이 일치하지 않음"), *TargetTag.ToString())
			return FTransform();
		}
		UE_LOG(LogTemp, Warning, TEXT("%s: TargetPoint에 UTargetTypeComponent가 없음"), *TargetTag.ToString())
		return FTransform();
	}
	UE_LOG(LogTemp, Warning, TEXT("%s: TargetPoint를 찾을 수 없음"), *TargetTag.ToString())
	return FTransform();
}

FVector UActorCacheSubsystem::GetTargetLocation(FName TargetTag, ETargetTypeRole TargetTypeRole)
{
	if (TObjectPtr<ATargetPoint> NowTarget = *CachedTargetPoints.Find(TargetTag))
	{
		if (UTargetTypeComponent* TargetTypeComponent = NowTarget->GetComponentByClass<UTargetTypeComponent>())
		{
			if (TargetTypeComponent->TargetTypeRole == TargetTypeRole)
			{
				return NowTarget->GetActorLocation();
			}
			UE_LOG(LogTemp, Warning, TEXT("%s: TargetPoint와 목표 타입이 일치하지 않음"), *TargetTag.ToString())
			return FVector::Zero();
		}
		UE_LOG(LogTemp, Warning, TEXT("%s: TargetPoint에 UTargetTypeComponent가 없음"), *TargetTag.ToString())
		return FVector::Zero();
	}
	UE_LOG(LogTemp, Warning, TEXT("%s: TargetPoint를 찾을 수 없음"), *TargetTag.ToString())
	return FVector::Zero();
}

bool UActorCacheSubsystem::AddActorToCache(ATargetPoint* Actor)
{
	if (Actor->Tags.Num() > 0)
	{
		CachedTargetPoints.Add(Actor->Tags[0], Actor);
		return true;
	}
	UE_LOG(LogTemp, Warning, TEXT("액터에 태그가 할당되지 않음"));
	return false;
}

void UActorCacheSubsystem::PrintAllCachedTarget() const
{
	UE_LOG(LogTemp, Warning, TEXT("PrintAllCachedTarget"));
	for (TPair<FName, TObjectPtr<ATargetPoint>> Pair : CachedTargetPoints)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s : %s : %s"), *Pair.Key.ToString(), *Pair.Value->GetActorLocation().ToString(), *Pair.Value->GetActorRotation().ToString());
	}
}
