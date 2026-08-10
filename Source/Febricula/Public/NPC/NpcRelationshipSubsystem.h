#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NpcTypes.h"
#include "NpcRelationshipSubsystem.generated.h"

UCLASS(BlueprintType)
class FEBRICULA_API UNpcRelationshipSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC|Relationship")
	float GetAffinityValue(FName NpcId) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC|Relationship")
	ENpcRelationshipLevel GetRelationshipLevel(FName NpcId) const;

	UFUNCTION(BlueprintCallable, Category = "NPC|Relationship")
	float AddAffinity(FName NpcId, float Delta);

	UFUNCTION(BlueprintCallable, Category = "NPC|Relationship")
	void SetAffinity(FName NpcId, float NewValue);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC|Relationship")
	bool IsRelationshipAtLeast(FName NpcId, ENpcRelationshipLevel MinimumLevel) const;

protected:
	UPROPERTY()
	TMap<FName, float> AffinityByNpcId;

	UPROPERTY(EditDefaultsOnly, Category = "NPC|Relationship|Threshold")
	float WaryThreshold = -20.f;

	UPROPERTY(EditDefaultsOnly, Category = "NPC|Relationship|Threshold")
	float FavorThreshold = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = "NPC|Relationship|Threshold")
	float TrustThreshold = 50.f;

	UPROPERTY(EditDefaultsOnly, Category = "NPC|Relationship|Threshold")
	float IntimateThreshold = 80.f;
};
