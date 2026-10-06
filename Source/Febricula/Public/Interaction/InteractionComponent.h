// Fill out your copyright notice in the Description page of Project Settings.
//
// 플레이어에게 붙이는 상호작용 컴포넌트.
// 지금은 플레이어 캐릭터가 아직 구현되지 않은 단계라, 서드퍼슨 템플릿 캐릭터 같은
// 아무 Pawn에나 이 컴포넌트만 얹으면 바로 동작하도록 만들었다:
//   - 주기적으로 주변을 검사해 가장 가까운 IInteractable을 찾고 HUD에 프롬프트를 띄운다
//   - E 키를 누르면 그 대상의 Interact()를 호출한다
//   - HUD 위젯 생성과 DataTable 등록도 여기서 대신 해준다 (GameMode/HUD 클래스 세팅 없이 테스트 가능)
//
// 나중에 정식 플레이어 캐릭터 + Enhanced Input이 붙으면,
// bBindEKeyDirectly를 끄고 TryInteract()를 직접 호출하는 방식으로 갈아타면 된다.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

class UDataTable;
class UPlayerHUDWidget;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FEBRICULA_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 지금 조준 중인 대상과 상호작용한다.
	 * 대화 중이라면 대신 "다음 대사로 넘기기"로 동작한다 (E 키로도 대화를 진행할 수 있게).
	 */
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void TryInteract();

	UFUNCTION(Category = "Interaction")
	TWeakObjectPtr<AActor> GetFocusedActor() const { return FocusedActor; }
	FName GetFocusedActorTag() const;
	UFUNCTION(BlueprintPure, Category = "Interaction")
	UPlayerHUDWidget* GetHUDWidget() const { return HUDWidget; }

protected:
	/** 주변 검사 + 프롬프트 갱신 */
	void UpdateFocus();

	/** 소유 Pawn의 InputComponent에 E 키를 직접 바인딩한다 */
	void TryBindInput();

	// ---- 설정 ----

	/** 이 반경 안에 있는 대상만 상호작용 후보가 된다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float InteractionRadius = 250.f;

	/**
	 * 플레이어가 바라보는 방향과 대상 방향의 최소 내적값.
	 * 1에 가까울수록 정면만 인정. 0이면 좌우 90도까지, -1이면 등 뒤도 인정.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float MinFacingDot = 0.f;

	/** 검사 주기(초). 매 틱 돌릴 필요가 없어서 타이머로 돌린다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float FocusUpdateInterval = 0.15f;

	/** E 키를 이 컴포넌트가 직접 바인딩할지. 정식 입력 시스템이 붙으면 끈다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Input")
	bool bBindEKeyDirectly = true;

	// ---- HUD / 데이터 ----

	/** 화면에 띄울 HUD 위젯 클래스 (UPlayerHUDWidget을 부모로 만든 위젯 BP) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|HUD")
	TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

	// /** FNpcEventRow 기반 이벤트 테이블 */
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Data")
	// TObjectPtr<UDataTable> EventTable;
	//
	// /** FDialogueNodeRow 기반 대화 테이블 */
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Data")
	// TObjectPtr<UDataTable> DialogueTable;

	// ---- 런타임 ----

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> FocusedActor;

	UPROPERTY(Transient)
	TObjectPtr<UPlayerHUDWidget> HUDWidget;

	FTimerHandle FocusTimerHandle;
	bool bInputBound = false;
};
