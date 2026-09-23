// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ActorCacheSubsystem.generated.h"

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

	FTransform GetTargetTransform(FName TargetTag, ETargetTypeRole TargetTypeRole);
	FVector GetTargetLocation(FName TargetTag, ETargetTypeRole TargetTypeRole);
	
	UFUNCTION(BlueprintCallable)
	bool AddActorToCache(ATargetPoint* Actor);
	
#if !UE_BUILD_SHIPPING
	void PrintAllCachedTarget() const;
	
#endif

	
protected:
	TMap<FName, TObjectPtr<ATargetPoint>> CachedTargetPoints;
	
};
