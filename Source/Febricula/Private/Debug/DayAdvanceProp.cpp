// [skydodo] 2026-10-08 작성
// 요약: 상호작용하면 다음 날 아침으로 넘기는 테스트용 오브젝트 구현
// [테스트용] 테스트 맵 전용 오브젝트. 실제 게임 레벨에는 배치하지 말 것 (실제 게임의 시간 경과는 이벤트 액션 TimeAdvanceDay 등으로 한다)

#include "Debug/DayAdvanceProp.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameMode/TimeSubsystem.h"
#include "UObject/ConstructorHelpers.h"

ADayAdvanceProp::ADayAdvanceProp()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// 눈에 띄게 엔진 기본 큐브를 쓴다. 액터 위치가 바닥이 되도록 반 높이만큼 올린다.
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 50.f));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	InteractionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionVolume"));
	InteractionVolume->SetupAttachment(Mesh);
	InteractionVolume->SetBoxExtent(FVector(80.f));
	// 상호작용 컴포넌트의 오버랩 검사에만 걸리면 되므로 물리 충돌은 끈다 (AInteractableProp와 같은 설정).
	InteractionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionVolume->SetCollisionResponseToAllChannels(ECR_Overlap);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(40.f);
	Label->SetTextRenderColor(FColor(255, 200, 0));
	Label->SetText(NSLOCTEXT("DayAdvance", "Label", "다음 날로 (E)"));

	InteractionPrompt = NSLOCTEXT("DayAdvance", "Prompt", "E - 다음 날로");
}

bool ADayAdvanceProp::CanInteract_Implementation(AActor* Interactor) const
{
	// 대화 중에는 날짜를 넘기지 않는다.
	const UDialogueSubsystem* Dialogue = GetWorld() ? GetWorld()->GetSubsystem<UDialogueSubsystem>() : nullptr;
	return !(Dialogue && Dialogue->IsDialogueActive());
}

void ADayAdvanceProp::Interact_Implementation(AActor* Interactor)
{
	UTimeSubsystem* Time = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTimeSubsystem>() : nullptr;
	if (!Time)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 시간 시스템이 없어 날짜를 넘기지 못했습니다."), *GetNameSafe(this));
		return;
	}

	AdvanceToNextMorning(Time);

	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		const FText Line = FText::Format(NSLOCTEXT("DayAdvance", "Line", "하루가 지났다. {0}일차 아침."),
			FText::AsNumber(Time->GetAbsoluteDay()));
		Dialogue->StartSimpleLines(FText::GetEmpty(), {Line}, this);
	}
}

FText ADayAdvanceProp::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return InteractionPrompt;
}

void ADayAdvanceProp::AdvanceToNextMorning(UTimeSubsystem* Time)
{
	if (!Time)
	{
		return;
	}

	const int32 Before = Time->GetAbsoluteDay();
	for (int32 Guard = 0; Guard < 64 && Time->GetAbsoluteDay() == Before; ++Guard)
	{
		Time->SpendSchedule();
	}
	if (Time->GetAbsoluteDay() == Before)
	{
		Time->SpendDay();
	}
}
