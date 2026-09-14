// 버거리아 — 손님 AI 컨트롤러
//
// LL_PROJECT.md §9.1 — "`Variant_Combat` 이 네이티브 C++ StateTree 의 완성된
// 예제다. 새로 설계할 필요가 없고 패턴을 그대로 따르면 된다."
//
// `ACombatAIController` 와 같은 뼈대다. 다른 점은 둘뿐 —
//   ① `abstract` 가 아니다. 손님은 파생 BP 없이 그대로 스폰해 보는 일이 많고,
//      트리 에셋은 컴포넌트에 꽂으면 된다 (BP 로도, 파이썬으로도).
//   ② `UStateTreeAIComponent` 를 **public 으로 낸다.** `UStoreSubsystem` 이
//      이벤트를 밀 때 이걸 찾아야 하고 (§9.5 "Subsystem 이 `SendStateTreeEvent`
//      로 민다"), 에디터를 안 열고 트리를 꽂아 보려면 밖에서 잡혀야 한다.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "CustomerAIController.generated.h"

class UStateTreeAIComponent;

UCLASS()
class ACustomerAIController : public AAIController
{
	GENERATED_BODY()

public:
	ACustomerAIController();

	/** 손님의 StateTree. 여기에 `ST_Customer` 를 꽂는다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStateTreeAIComponent> StateTreeAI;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
};
