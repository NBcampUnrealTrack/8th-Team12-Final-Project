// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TargetTypeRole.h"
#include "Components/ActorComponent.h"
#include "TargetTypeComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FEBRICULA_API UTargetTypeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TargetType")
	ETargetTypeRole TargetTypeRole;
	
	void ActivateTarget();
	void BeginPlay() override;

protected:
	UPROPERTY(EditAnywhere, meta=(EditCondition="TargetTypeRole == ETargetTypeRole::Prop"))
	TSoftObjectPtr<UStaticMesh> PropMesh;



};