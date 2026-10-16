// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameMode/TargetTypeRole.h"
#include "Subsystems/WorldSubsystem.h"
#include "ActorCacheSubsystem.generated.h"

class UTargetTypeComponent;
enum class ETargetTypeRole : uint8;
class ATargetPoint;
/**
 * 
 */
UCLASS()
class FEBRICULA_API UActorCacheSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
	
public:
	void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable)
	FTransform GetTargetTransform(FName TargetTag, ETargetTypeRole TargetTypeRole = ETargetTypeRole::WildCard);
	UFUNCTION(BlueprintCallable)
	FVector GetTargetLocation(FName TargetTag, ETargetTypeRole TargetTypeRole = ETargetTypeRole::WildCard);
	
	UFUNCTION(BlueprintCallable)
	bool AddActorToCache(ATargetPoint* Actor);
	UFUNCTION(BlueprintCallable)
	bool RemoveActorFromCache(FName TargetTag);
	
	UFUNCTION(BlueprintCallable)
	UTargetTypeComponent* GetTargetTypeComponent(FName TargetTag);
	
#if !UE_BUILD_SHIPPING
	void PrintAllCachedTarget() const;
	
#endif

	
protected:
	// [skydodo] 2026-10-13 버그 수정: UPROPERTY 없는 TObjectPtr라 GC가 몰랐고, 파괴된 TargetPoint가 댕글링으로 남았다 -> 약참조
	UPROPERTY()
	TMap<FName, TWeakObjectPtr<ATargetPoint>> CachedTargetPoints;
	
};
