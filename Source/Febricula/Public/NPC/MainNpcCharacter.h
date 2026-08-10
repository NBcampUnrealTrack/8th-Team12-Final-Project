#pragma once

#include "CoreMinimal.h"
#include "BaseNpcCharacter.h"
#include "MainNpcCharacter.generated.h"

class UNpcRelationshipSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNpcTalkStateChanged, bool, bIsTalking);

UCLASS(Abstract)
class FEBRICULA_API AMainNpcCharacter : public ABaseNpcCharacter
{
	GENERATED_BODY()

public:
	virtual bool IsMainNpc() const override { return true; }

	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;

	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	virtual void StartFollowTarget(AActor* TargetToFollow);

	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	virtual void StopFollowTarget();

	UFUNCTION(BlueprintPure, Category = "NPC|Main")
	bool IsFollowing() const { return GetMovementState() == ENpcMovementState::Follow; }

	UFUNCTION(BlueprintPure, Category = "NPC|Main")
	AActor* GetFollowTarget() const { return CurrentFollowTarget; }
	
	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	virtual void StartTalk(AActor* Talker);

	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	virtual void EndTalk();

	UFUNCTION(BlueprintPure, Category = "NPC|Main")
	bool IsTalking() const { return GetMovementState() == ENpcMovementState::Talk; }

	UFUNCTION(BlueprintPure, Category = "NPC|Main")
	AActor* GetCurrentTalker() const { return CurrentTalker; }
	
	UFUNCTION(BlueprintPure, Category = "NPC|Relationship")
	bool IsRelationshipAtLeast(ENpcRelationshipLevel MinimumLevel) const;

	UFUNCTION(BlueprintPure, Category = "NPC|Relationship")
	ENpcRelationshipLevel GetRelationshipLevel() const;

	UFUNCTION(BlueprintPure, Category = "NPC|Relationship")
	float GetAffinityValue() const;

	UPROPERTY(BlueprintAssignable, Category = "NPC|Main")
	FOnNpcTalkStateChanged OnNpcTalkStateChanged;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "NPC|Main")
	void OnTalkStarted(AActor* Talker);

	UFUNCTION(BlueprintImplementableEvent, Category = "NPC|Main")
	void OnTalkEnded();

	UNpcRelationshipSubsystem* GetRelationshipSubsystem() const;

	/** Blackboard의 추적/동행 대상 키 이름 (Object 키) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|AI")
	FName FollowTargetBlackboardKey = TEXT("FollowTarget");

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Main")
	TObjectPtr<AActor> CurrentFollowTarget;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Main")
	TObjectPtr<AActor> CurrentTalker;
};
