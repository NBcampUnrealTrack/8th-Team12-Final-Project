// Fill out your copyright notice in the Description page of Project Settings.


#include "Animation/CharacterAnimationComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NPC/MainNpc/MainNpcCharacter.h"


// Sets default values for this component's properties
UCharacterAnimationComponent::UCharacterAnimationComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

void UCharacterAnimationComponent::HandleAnimInitialized()
{
	UE_LOG(LogTemp, Warning, TEXT("[AnimCheck] 애님 재초기화 (프레임 %llu)\n블루프린트 스택:\n%s"),
	GFrameCounter, *FFrame::GetScriptCallstack());   // ← 여기에 브레이크포인트
}

void UCharacterAnimationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		if (AMainNpcCharacter* Character = Cast<AMainNpcCharacter>(Owner))
		{
			FName Key = Character->GetDefaultAnimation();
			if (Key == NAME_None) return;
			PlayAnim(Key, true);
		}
	}
	
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetMesh()->OnAnimInitialized.AddDynamic(this, &UCharacterAnimationComponent::HandleAnimInitialized);
	}

}

bool UCharacterAnimationComponent::PlayAnim(FName AnimId, bool bLoop)
{
	return PlayAnimOnSlot(AnimId, AnimSlotName, bLoop);
}

void UCharacterAnimationComponent::StopAnimOnSlot(FName SlotName, float BlendOutTime)
{
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* MeshComp = Owner ? Owner->GetMesh() : nullptr;
	if (UAnimInstance* AnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr)
	{
		AnimInstance->StopSlotAnimation(BlendOutTime, SlotName);
	}
}

bool UCharacterAnimationComponent::PlayAnimOnSlot(FName AnimId, FName SlotName, bool bLoop)
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
			// UE_LOG(LogTemp, Warning, TEXT("[PlayAnim] AnimMode=%d"), (int32)MeshComp->GetAnimationMode());
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
					*Found, SlotName, AnimBlendTime, AnimBlendTime, /*InPlayRate=*/1.f, LoopCount);

				if (Played)
				{
					AnimInstance->OnMontageEnded.RemoveDynamic(this, &UCharacterAnimationComponent::HandleMontageEnded);
					AnimInstance->OnMontageEnded.AddDynamic(this, &UCharacterAnimationComponent::HandleMontageEnded);

					// UE_LOG(LogTemp, Warning, TEXT("[PlayAnim] 재생 시작: %s, 길이 %.2f"),
					// 	*GetNameSafe(Played), Played->GetPlayLength());

					return true;
				}

				UE_LOG(LogTemp, Warning,
					TEXT("%s: 슬롯 '%s' 재생에 실패했습니다. 애님 블루프린트 그래프에 같은 이름의 Slot 노드가 있는지 확인하세요. 단일 노드 모드로 대체합니다."),
					*GetNameSafe(this), *SlotName.ToString());
			}

			// 상체 슬롯처럼 기본 슬롯이 아닌 곳에 실패했으면 대체 재생하지 않는다.
			// 아래 폴백은 전신을 덮어써서 걷기 애니메이션이 사라진다.
			if (SlotName != AnimSlotName)
			{
				return false;
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

void UCharacterAnimationComponent::StopAnim(float BlendOutTime, bool bFromLogic)
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
		if (bFromLogic)
		{
			if (AActor* Actor = GetOwner())
			{
				if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(Actor))
				{
					FName AnimName = MainNpc->GetDefaultAnimation();
					if (AnimName != NAME_None && AnimLibrary.Contains(AnimName))
					{
						PlayAnim(AnimName, true);
						return;
					}
				}
			}
		}
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
	
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Owner ? Owner->GetMesh() : nullptr;
	if (!Mesh) return;

	const int32 Mode = (int32)Mesh->GetAnimationMode();
	const UAnimInstance* Anim = Mesh->GetAnimInstance();

	if (Mode != LastAnimMode || Anim != LastAnimInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AnimCheck] 프레임 %llu: 모드 %d → %d, 인스턴스 %p → %p"),
			GFrameCounter, LastAnimMode, Mode, LastAnimInstance, Anim);
		LastAnimMode = Mode;
		LastAnimInstance = Anim;
	}


}

void UCharacterAnimationComponent::HandleMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// UE_LOG(LogTemp, Warning, TEXT("[PlayAnim] 몽타주 종료: %s, 중단됨=%d"),
	// *GetNameSafe(Montage), bInterrupted);   // ← 여기에 브레이크포인트

}
