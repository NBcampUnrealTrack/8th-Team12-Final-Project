// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MassCrowd/MassCrowdTrait.h"

#include "MassCommonFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassEntityUtils.h"

void UMassCrowdPathFollowTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// 위치. Mass가 기본 제공하는 조각이라 표현(Representation) 쪽이 이걸 그대로 읽는다.
	BuildContext.AddFragment<FTransformFragment>();

	// 우리 조각들.
	BuildContext.AddFragment<FMassCrowdPathFragment>();
	BuildContext.AddFragment<FMassCrowdAvoidanceFragment>();
	BuildContext.AddFragment<FMassCrowdBumpFragment>();

	// 시작 상태는 "걷는 중".
	BuildContext.AddTag<FMassCrowdWalkingTag>();

	// 공유 조각은 값이 같으면 같은 인스턴스를 재사용한다.
	// 유형별로 한 벌만 만들어지는 게 이 호출 덕분이다.
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);
	const FSharedStruct SharedFragment =
		EntityManager.GetOrCreateSharedFragment<FMassCrowdSharedConfig>(SharedConfig);
	BuildContext.AddSharedFragment(SharedFragment);
}
