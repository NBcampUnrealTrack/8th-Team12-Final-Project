#include "SlotPoint.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"

ASlotPoint::ASlotPoint()
{
	// 슬롯은 가만히 있는다 — 틱도 필요 없다. 37개 + 를 깔기 때문에
	// 기본값을 켜 두면 그만큼 공짜로 낭비한다.
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(Root);
		Arrow->ArrowSize = 0.6f;
		Arrow->ArrowColor = FColor(80, 200, 255);
		Arrow->bIsScreenSizeScaled = true;
	}
#endif
}

FTransform ASlotPoint::GetUseTransform() const
{
	const FTransform& T = GetActorTransform();
	// ⚠ 오프셋은 **로컬** 이다. 월드로 더하면 슬롯이 회전했을 때 엉뚱한 데로 간다.
	return FTransform(T.GetRotation(), T.TransformPosition(LocalOffset), FVector::OneVector);
}
