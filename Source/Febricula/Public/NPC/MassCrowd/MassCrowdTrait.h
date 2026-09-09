// Fill out your copyright notice in the Description page of Project Settings.
//
// 엔티티에 "행인이 되기 위한 조각들"을 붙이는 트레이트.
//
// [트레이트가 하는 일]
//   Mass에서 엔티티는 조각의 조합으로 정의된다. 어떤 조각을 붙일지는 코드가 아니라
//   MassEntityConfig 애셋에서 정하는데, 그 애셋에 얹는 단위가 트레이트다.
//
//   즉 이 클래스는 "행인 = 경로조각 + 회피조각 + 부딪힘조각 + 걷는중태그" 라는
//   구성을 선언할 뿐이고, 실제 조립은 데이터(애셋)에서 이뤄진다.
//   액터 버전에서 클래스 하나에 멤버를 다 박아뒀던 것과 대비되는 지점이다.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "NPC/MassCrowd/MassCrowdFragments.h"
#include "MassCrowdTrait.generated.h"

UCLASS(meta = (DisplayName = "Crowd Path Follow"))
class FEBRICULA_API UMassCrowdPathFollowTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

public:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;

protected:
	/**
	 * 이 유형의 공유 설정.
	 *
	 * 개체마다 복사되지 않고 한 벌만 만들어져 공유된다. 유형이 3개면 3벌이고,
	 * 개체가 1000명이어도 그대로 3벌이다.
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd")
	FMassCrowdSharedConfig SharedConfig;
};
