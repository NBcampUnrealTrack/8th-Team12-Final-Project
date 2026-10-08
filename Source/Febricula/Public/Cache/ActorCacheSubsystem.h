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
	TMap<FName, TObjectPtr<ATargetPoint>> CachedTargetPoints;
	
};
