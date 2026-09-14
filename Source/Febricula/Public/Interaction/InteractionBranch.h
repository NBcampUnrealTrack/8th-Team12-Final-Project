#pragma once
#include "Dialogue/DialogueDataTypes.h"
#include "InteractionBranch.generated.h"

USTRUCT(BlueprintType)
struct FInteractionBranch
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Branch")
	TArray<FGameCondition> Conditions;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction Branch")
	TArray<FGameAction> Actions;
};
