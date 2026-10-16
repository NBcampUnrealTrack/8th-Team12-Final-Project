#include "CustomerAIController.h"

#include "StoreSubsystem.h"
#include "Components/StateTreeAIComponent.h"
#include "Engine/GameInstance.h"

ACustomerAIController::ACustomerAIController()
{
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	check(StateTreeAI);

	// 폰을 잡은 **뒤에** 켠다. 자동 시작이면 트리가 폰을 못 찾은 채 돌기 시작해
	// Context 바인딩이 비고, 그러면 첫 태스크가 조용히 실패한다.
	bStartAILogicOnPossess = false;
	StateTreeAI->SetStartLogicAutomatically(false);

	// EQS·포커스가 제대로 돌게 폰에 붙인다 (Combat 변형과 같은 이유)
	bAttachToPawn = true;
}

void ACustomerAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	StateTreeAI->StartLogic();
}

void ACustomerAIController::OnUnPossess()
{
	// ⚠ 컨트롤러가 폰을 놓는 것도 **릴리스 누락 경로**다 (§9.5 1번 버그).
	//    `EndPlay` 는 폰이 파괴될 때만 돌므로 빙의만 풀리면 자리가 죽는다.
	if (APawn* P = GetPawn())
	{
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (UStoreSubsystem* Store = GI->GetSubsystem<UStoreSubsystem>())
			{
				Store->ReleaseAllSlotsOf(P);
				Store->LeaveWaitArea(P);
			}
		}
	}
	Super::OnUnPossess();
}
