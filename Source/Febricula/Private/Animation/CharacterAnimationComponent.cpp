// Fill out your copyright notice in the Description page of Project Settings.


#include "Animation/CharacterAnimationComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"


// Sets default values for this component's properties
UCharacterAnimationComponent::UCharacterAnimationComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

bool UCharacterAnimationComponent::PlayAnim(FName AnimId, bool bLoop)
{
	const TObjectPtr<UAnimSequenceBase>* Found = AnimLibrary.Find(AnimId);
	if (!Found || !*Found)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: AnimLibrary에 '%s' 항목이 없습니다."),
			*GetNameSafe(this), *AnimId.ToString());
		return false;
	}
	
	
	if (ACharacter* Owner = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* MeshComp = Owner->GetMesh())
		{
			UE_LOG(LogTemp, Warning, TEXT("[PlayAnim] AnimMode=%d"), (int32)MeshComp->GetAnimationMode());
			UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
			if (AnimInstance && !bPreferSingleNodeAnim)
			{
				//AnimInstance->Montage_Stop(0.f);   // 모든 몽타주 즉시 중단+
				//AnimInstance->StopSlotAnimation(0.f, AnimSlotName);
				if (UCharacterMovementComponent* Movement = Owner->GetCharacterMovement())
				{
					Movement->bOrientRotationToMovement = true;
				}
				// AnimSequence를 런타임에 몽타주로 감싸 슬롯에 얹는다. 몽타주 애셋이 없어도 된다.
				// LoopCount는 반복 횟수라서 무한 루프가 없다. 지속 포즈는 큰 값으로 깔아두고
				// StopNpcAnim으로 끊는 방식이 안전하다.
				const int32 LoopCount = bLoop ? 1000 : 1;

				UAnimMontage* Played = AnimInstance->PlaySlotAnimationAsDynamicMontage(
					*Found, AnimSlotName, AnimBlendTime, AnimBlendTime, /*InPlayRate=*/1.f, LoopCount);
				
				if (Played)
				{
					return true;
				}

				UE_LOG(LogTemp, Warning,
					TEXT("%s: 슬롯 '%s' 재생에 실패했습니다. 애님 블루프린트 그래프에 같은 이름의 Slot 노드가 있는지 확인하세요. 단일 노드 모드로 대체합니다."),
					*GetNameSafe(this), *AnimSlotName.ToString());
			}

			// 폴백: 애님 블루프린트를 거치지 않고 시퀀스를 직접 재생한다.
			// 애님 BP가 아직 없는 NPC(T포즈 상태)에서도 애니메이션 확인이 가능하다.
			// 재생 중에는 로코모션이 나오지 않지만, 애초에 애님 BP가 없으면 로코모션도 없으므로 손해가 없다.
			MeshComp->PlayAnimation(*Found, bLoop);
			return true;
		}
	}
	return false;
}

void UCharacterAnimationComponent::StopAnim(float BlendOutTime)
{
	//UE_LOG(LogTemp, Warning, TEXT("STOP ANIM!"));
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner) return;

	USkeletalMeshComponent* MeshComp = Owner->GetMesh();
	if (!MeshComp) return;

	if (bPreferSingleNodeAnim)
	{
		MeshComp->Stop();
		// 애님 BP로 복귀
		MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		return;
	}

	if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	{
		AnimInstance->StopSlotAnimation(BlendOutTime, AnimSlotName);
	}
}

void UCharacterAnimationComponent::TickComponent(float DeltaTime, enum ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	if (USkeletalMeshComponent* MeshComp = Cast<ACharacter>(GetOwner())->GetMesh())
	{
		if (UAnimInstance* Anim = MeshComp->GetAnimInstance())
		{
			if (bLogSlotWeight)
			{
				// UE_LOG(LogTemp, Warning, TEXT("[슬롯] %s: 가중치=%.2f 몽타주=%d"),
				// 	*GetOwner()->GetName(),
				// 	Anim->GetSlotMontageLocalWeight(TEXT("DefaultSlot")),
				// 	Anim->IsAnyMontagePlaying());
			}
		}
	}

}
