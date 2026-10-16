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
	if (ATargetPoint* NowTarget = CachedTargetPoints.FindRef(TargetTag).Get())
	{
		if (UTargetTypeComponent* TargetTypeComponent = NowTarget->GetComponentByClass<UTargetTypeComponent>())
		{
			if (TargetTypeComponent->TargetTypeRole == TargetTypeRole || TargetTypeRole == ETargetTypeRole::WildCard)
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
	if (ATargetPoint* NowTarget = CachedTargetPoints.FindRef(TargetTag).Get())
	{
		if (UTargetTypeComponent* TargetTypeComponent = NowTarget->GetComponentByClass<UTargetTypeComponent>())
		{
			if (TargetTypeComponent->TargetTypeRole == TargetTypeRole || TargetTypeRole == ETargetTypeRole::WildCard)
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

// [skydodo] 2026-10-13 버그 수정: 태그가 없을 때 경고 후 '존재하지 않는 액터' 경고까지 이어서 찍던 것 수정
bool UActorCacheSubsystem::AddActorToCache(ATargetPoint* Actor)
{
	if (Actor)
	{
		if (Actor->Tags.Num() > 0)
		{
			CachedTargetPoints.Add(Actor->Tags[0], Actor);
			return true;
		}
		UE_LOG(LogTemp, Warning, TEXT("액터에 태그가 할당되지 않음"));
		return false;
	}
	UE_LOG(LogTemp, Warning, TEXT("존재하지 않는 액터"));
	return false;
}

bool UActorCacheSubsystem::RemoveActorFromCache(FName TargetTag)
{
	if (CachedTargetPoints.Contains(TargetTag))
	{
		CachedTargetPoints.Remove(TargetTag);
		return true;
	}
	UE_LOG(LogTemp, Warning, TEXT("캐싱되지 않은 Target Tag"))
	return false;
}

UTargetTypeComponent* UActorCacheSubsystem::GetTargetTypeComponent(FName TargetTag)
{
	if (ATargetPoint* Target = CachedTargetPoints.FindRef(TargetTag).Get())
	{
		if (UTargetTypeComponent* TargetTypeComponent = Target->GetComponentByClass<UTargetTypeComponent>())
		{
			return TargetTypeComponent;
		}
	}
	return nullptr;
}

// [skydodo] 2026-10-13 버그 수정: 헤더만 Shipping 가드가 있고 정의는 없어 Shipping 빌드가 깨졌다. 캐시가 약참조로 바뀌어 파괴된 액터도 확인
#if !UE_BUILD_SHIPPING
void UActorCacheSubsystem::PrintAllCachedTarget() const
{
	UE_LOG(LogTemp, Warning, TEXT("PrintAllCachedTarget"));
	for (const TPair<FName, TWeakObjectPtr<ATargetPoint>>& Pair : CachedTargetPoints)
	{
		if (const ATargetPoint* Target = Pair.Value.Get())
		{
			UE_LOG(LogTemp, Warning, TEXT("%s : %s : %s"), *Pair.Key.ToString(), *Target->GetActorLocation().ToString(), *Target->GetActorRotation().ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("%s : (파괴됨)"), *Pair.Key.ToString());
		}
	}
}
#endif
