// Fill out your copyright notice in the Description page of Project Settings.

#include "Interaction/InteractionComponent.h"

#include "Components/InputComponent.h"
#include "Dialogue/DialogueSubsystem.h"
// FOverlapResult는 UE5.1부터 별도 헤더로 분리됐다. Engine/World.h에는 전방 선언만 있어서
// 이걸 빼면 "정의되지 않은 형식 'FOverlapResult'" 컴파일 에러가 난다.
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/Interactable.h"
#include "TimerManager.h"
#include "UI/PlayerHUDWidget.h"

UInteractionComponent::UInteractionComponent()
{
	// 주변 검사는 타이머로 돌리므로 틱은 꺼둔다.
	PrimaryComponentTick.bCanEverTick = false;
}

void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 대화 서브시스템에 데이터 테이블을 등록한다.
	// GameMode/HUD 클래스를 따로 세팅하지 않아도 이 컴포넌트 하나로 테스트가 되도록 여기서 처리.
	// if (UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>())
	// {
	// 	if (EventTable || DialogueTable)
	// 	{
	// 		Dialogue->SetDataTables(EventTable, DialogueTable);
	// 	}
	// }

	// HUD 위젯 생성
	if (HUDWidgetClass)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			HUDWidget = CreateWidget<UPlayerHUDWidget>(PC, HUDWidgetClass);
			if (HUDWidget)
			{
				HUDWidget->AddToViewport();
			}
		}
	}

	TryBindInput();

	World->GetTimerManager().SetTimer(
		FocusTimerHandle, this, &UInteractionComponent::UpdateFocus,
		FMath::Max(FocusUpdateInterval, 0.02f), /*bLoop=*/true);
}

void UInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FocusTimerHandle);
	}

	if (HUDWidget)
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void UInteractionComponent::TryBindInput()
{
	if (!bBindEKeyDirectly || bInputBound)
	{
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
	if (!PC)
	{
		// 아직 빙의 전일 수 있으니 잠시 뒤 다시 시도한다.
		if (UWorld* World = GetWorld())
		{
			FTimerHandle RetryHandle;
			World->GetTimerManager().SetTimer(RetryHandle, this, &UInteractionComponent::TryBindInput, 0.2f, false);
		}
		return;
	}

	// EnableInput을 부르면 Pawn에 InputComponent가 보장된다.
	OwnerPawn->EnableInput(PC);

	if (OwnerPawn->InputComponent)
	{
		// Enhanced Input의 IMC/IA 애셋 없이도 바로 쓰도록 키를 직접 바인딩한다.
		OwnerPawn->InputComponent->BindKey(EKeys::E, IE_Pressed, this, &UInteractionComponent::TryInteract);
		bInputBound = true;
	}
}

void UInteractionComponent::UpdateFocus()
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();

	// 대화 중에는 대상 탐색을 멈춘다 (대화 도중 프롬프트가 깜빡이지 않게).
	if (Dialogue && Dialogue->IsDialogueActive())
	{
		return;
	}

	const FVector MyLocation = OwnerActor->GetActorLocation();
	const FVector MyForward = OwnerActor->GetActorForwardVector();

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerActor);

	World->OverlapMultiByObjectType(
		Overlaps,
		MyLocation,
		FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
		FCollisionShape::MakeSphere(InteractionRadius),
		QueryParams);

	AActor* BestActor = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || !Candidate->Implements<UInteractable>())
		{
			continue;
		}

		if (!IInteractable::Execute_CanInteract(Candidate, OwnerActor))
		{
			continue;
		}

		const FVector ToCandidate = Candidate->GetActorLocation() - MyLocation;

		// 등 뒤에 있는 대상은 제외 (MinFacingDot으로 각도 조절)
		FVector ToCandidateDir = ToCandidate;
		ToCandidateDir.Z = 0.f;
		if (!ToCandidateDir.Normalize() || FVector::DotProduct(MyForward, ToCandidateDir) < MinFacingDot)
		{
			continue;
		}

		const float DistSq = ToCandidate.SizeSquared();
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestActor = Candidate;
		}
	}

	if (BestActor == FocusedActor)
	{
		return;
	}

	FocusedActor = BestActor;

	if (HUDWidget)
	{
		if (FocusedActor.Get())
		{
			HUDWidget->ShowInteractionPrompt(IInteractable::Execute_GetInteractionPrompt(FocusedActor.Get(), OwnerActor));
		}
		else
		{
			HUDWidget->HideInteractionPrompt();
		}
	}
}

void UInteractionComponent::TryInteract()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 대화 중이면 E 키는 "다음 대사"로 동작한다.
	// 선택지 대기 중일 때는 Advance가 내부에서 무시되므로 마우스로만 고를 수 있다.
	if (UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>())
	{
		if (Dialogue->IsDialogueActive())
		{
			Dialogue->Advance();
			return;
		}
	}

	if (FocusedActor.Get() && FocusedActor->Implements<UInteractable>())
	{
		IInteractable::Execute_Interact(FocusedActor.Get(), GetOwner());
	}
}

FName UInteractionComponent::GetFocusedActorTag() const
{
	if (FocusedActor.Get())
	{
		if (FocusedActor->Tags.Num() > 0)
		{
			return FocusedActor->Tags[0];
		}
	}
	return NAME_None;
}
