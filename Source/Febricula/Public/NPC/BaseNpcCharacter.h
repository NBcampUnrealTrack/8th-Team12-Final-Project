#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interaction/Interactable.h"
#include "NpcTypes.h"
#include "BaseNpcCharacter.generated.h"

class UBehaviorTree;
class AAIController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNpcMovementStateChanged, ENpcMovementState, OldState, ENpcMovementState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNpcInteracted, AActor*, Interactor);

UCLASS(Abstract)
class FEBRICULA_API ABaseNpcCharacter : public ACharacter, public IInteractable
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ABaseNpcCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void PossessedBy(AController* NewController) override;

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	
	UFUNCTION(BlueprintPure, Category = "NPC")
	virtual bool IsMainNpc() const { return false; }

	UFUNCTION(BlueprintPure, Category = "NPC")
	FName GetNpcId() const { return NpcId; }

	UFUNCTION(BlueprintPure, Category = "NPC")
	ENpcSpawnMode GetDefaultSpawnMode() const { return DefaultSpawnMode; }

	UFUNCTION(BlueprintPure, Category = "NPC|Movement")
	ENpcMovementState GetMovementState() const { return CurrentMovementState; }
	
	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	virtual void SetMovementState(ENpcMovementState NewState);

	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	virtual void MoveToLocation(const FVector& Destination, float AcceptanceRadius = 50.f);

	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	virtual void MoveToTargetActor(AActor* TargetActor, float AcceptanceRadius = 100.f);

	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	virtual void StopMovementAndIdle();

	UPROPERTY(BlueprintAssignable, Category = "NPC|Movement")
	FOnNpcMovementStateChanged OnNpcMovementStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "NPC|Interaction")
	FOnNpcInteracted OnNpcInteracted;

protected:

	UFUNCTION(BlueprintImplementableEvent, Category = "NPC|Interaction")
	void OnInteracted(AActor* Interactor);

	AAIController* GetNpcAIController() const;

	void PushMovementStateToBlackboard(ENpcMovementState State) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC", meta = (DisplayPriority = 0))
	FName NpcId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC")
	ENpcSpawnMode DefaultSpawnMode = ENpcSpawnMode::FixedInLevel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|AI")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|AI")
	FName MovementStateBlackboardKey = TEXT("MovementState");

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Movement")
	ENpcMovementState CurrentMovementState = ENpcMovementState::Idle;
};
