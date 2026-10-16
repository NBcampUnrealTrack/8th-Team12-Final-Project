// [skydodo] 2026-10-07 작성 (최종 수정 2026-10-08)
// 요약: 원경 행인 애니메이션 굽기 콘솔 명령(crowd.BakeFarCrowd) - 에디터 전용
// Fill out your copyright notice in the Description page of Project Settings.
//
// 원경 행인 애니메이션 굽기 — 에디터 전용 콘솔 명령 crowd.BakeFarCrowd.
//
// [하는 일]
//   원경 행인(ASubNpcFarCrowdInstancer)이 쓰는 AnimToTexture 데이터를 행인 스켈레탈 메시로 굽는다.
//   1. 데이터 에셋의 스태틱 메시가 없으면 스켈레탈 메시의 낮은 LOD 하나(기본: 마지막)를 스태틱 메시로 변환한다
//   2. 스태틱 LOD마다 뼈 애니메이션을 굽는다
//      (뼈 텍스처는 LOD와 상관없이 같고, LOD마다 정점 → 뼈 대응 UV만 따로 쓴다)
//   3. 스태틱 메시에 붙은 머티리얼 인스턴스에 구운 결과(텍스처, 프레임 수 등)를 넣는다
//
// [쓰는 법] Docs/CrowdFarLOD.md 참고. 콘솔에서
//   crowd.BakeFarCrowd /Game/NPC/SubNpc/FarCrowd/DA_PasserbyFar [스켈레탈 LOD]
//   끝나면 콘텐츠 브라우저에서 모두 저장.

#if WITH_EDITOR

#include "AnimToTextureBPLibrary.h"
#include "AnimToTextureDataAsset.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "NPC/SubNpc/SubNpcLog.h"

namespace SubNpcFarCrowdBake
{
	static void Bake(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogSubNpc, Warning, TEXT("[FarBake] 사용법: crowd.BakeFarCrowd <데이터 에셋 경로>"));
			return;
		}

		UAnimToTextureDataAsset* DataAsset = LoadObject<UAnimToTextureDataAsset>(nullptr, *Args[0]);
		if (!DataAsset)
		{
			UE_LOG(LogSubNpc, Warning, TEXT("[FarBake] 데이터 에셋을 찾을 수 없습니다: %s"), *Args[0]);
			return;
		}
		USkeletalMesh* SkeletalMesh = DataAsset->GetSkeletalMesh();
		if (!SkeletalMesh)
		{
			UE_LOG(LogSubNpc, Warning, TEXT("[FarBake] %s 에 스켈레탈 메시가 없습니다."), *GetNameSafe(DataAsset));
			return;
		}

		// 1. 스태틱 메시: 지정돼 있지 않으면 데이터 에셋 옆에 만든다.
		//    원경은 멀리 있어서 스켈레탈 메시의 낮은 LOD 하나면 된다 (기본: 마지막 LOD, 두 번째 인자로 지정).
		//    엔진 함수의 "LOD 전부 변환"(-1)은 LOD 검사에 막혀 동작하지 않는다.
		//    어느 LOD에서 만들었는지는 데이터 에셋의 SkeletalLODIndex에 남겨 다시 구울 때 쓴다.
		UStaticMesh* StaticMesh = DataAsset->GetStaticMesh();
		if (!StaticMesh)
		{
			const int32 SourceLOD = FMath::Clamp(Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : SkeletalMesh->GetLODNum() - 1,
				0, SkeletalMesh->GetLODNum() - 1);
			const FString Folder = FPackageName::GetLongPackagePath(DataAsset->GetPackage()->GetName());
			const FString MeshPath = Folder / TEXT("SM_") + SkeletalMesh->GetName() + TEXT("_Far");
			StaticMesh = UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh(SkeletalMesh, MeshPath, SourceLOD);
			if (!StaticMesh)
			{
				UE_LOG(LogSubNpc, Warning, TEXT("[FarBake] 스태틱 메시 변환 실패: %s (스켈레탈 LOD %d)"), *MeshPath, SourceLOD);
				return;
			}
			DataAsset->StaticMesh = StaticMesh;
			DataAsset->SkeletalLODIndex = SourceLOD;
			DataAsset->MarkPackageDirty();
			UE_LOG(LogSubNpc, Log, TEXT("[FarBake] 스태틱 메시 생성: %s (스켈레탈 LOD %d에서)"), *StaticMesh->GetPathName(), SourceLOD);
		}

		// 움직이는 인스턴스라 라이트맵이 필요 없다. 라이트맵 UV를 만들면 구운 UV 채널을 덮어쓸 수 있다.
		for (int32 LOD = 0; LOD < StaticMesh->GetNumSourceModels(); ++LOD)
		{
			StaticMesh->GetSourceModel(LOD).BuildSettings.bGenerateLightmapUVs = false;
		}

		// 2. 스태틱 LOD마다 굽기 (스태틱 LOD i ← 스켈레탈 LOD SourceLOD + i)
		const int32 SourceLOD = DataAsset->SkeletalLODIndex;
		const int32 NumLODs = FMath::Min(StaticMesh->GetNumSourceModels(), SkeletalMesh->GetLODNum() - SourceLOD);
		const int32 SavedStaticLOD = DataAsset->StaticLODIndex;
		const int32 SavedSkeletalLOD = DataAsset->SkeletalLODIndex;
		int32 Baked = 0;
		for (int32 LOD = 0; LOD < NumLODs; ++LOD)
		{
			DataAsset->StaticLODIndex = LOD;
			DataAsset->SkeletalLODIndex = SourceLOD + LOD;
			if (UAnimToTextureBPLibrary::AnimationToTexture(DataAsset))
			{
				++Baked;
			}
			else
			{
				UE_LOG(LogSubNpc, Warning, TEXT("[FarBake] LOD %d 굽기 실패 (출력 로그의 LogAnimToTextureEditor 확인)"), LOD);
			}
		}
		DataAsset->StaticLODIndex = SavedStaticLOD;
		DataAsset->SkeletalLODIndex = SavedSkeletalLOD;
		DataAsset->MarkPackageDirty();

		// 3. 스태틱 메시에 붙은 머티리얼 인스턴스 갱신.
		//    데이터 에셋과 같은 폴더(원경 전용)에 있는 것만 고친다. 변환 직후에는 스켈레탈 메시의 공용 머티리얼이
		//    붙어 있으므로, 원경 머티리얼로 바꾼 뒤 한 번 더 실행해야 한다.
		const FString OwnFolder = FPackageName::GetLongPackagePath(DataAsset->GetPackage()->GetName()) + TEXT("/");
		int32 UpdatedMaterials = 0;
		for (const FStaticMaterial& Material : StaticMesh->GetStaticMaterials())
		{
			UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Material.MaterialInterface);
			if (Instance && !Instance->GetPackage()->GetName().StartsWith(OwnFolder))
			{
				UE_LOG(LogSubNpc, Warning, TEXT("[FarBake] %s 은 원경 전용 폴더 밖이라 건드리지 않습니다. 스태틱 메시의 슬롯 %s 에 원경 머티리얼을 넣고 다시 실행하세요."),
					*Instance->GetPathName(), *Material.MaterialSlotName.ToString());
				continue;
			}
			if (Instance)
			{
				UAnimToTextureBPLibrary::UpdateMaterialInstanceFromDataAsset(DataAsset, Instance);
				Instance->MarkPackageDirty();
				++UpdatedMaterials;
			}
		}

		UE_LOG(LogSubNpc, Log, TEXT("[FarBake] 완료: %s | LOD %d/%d개 구움, 프레임 %d, 뼈 %d개, 머티리얼 인스턴스 %d개 갱신. 저장을 잊지 마세요."),
			*GetNameSafe(DataAsset), Baked, NumLODs, DataAsset->NumFrames, DataAsset->NumBones, UpdatedMaterials);
	}

	static FAutoConsoleCommand BakeCommand(
		TEXT("crowd.BakeFarCrowd"),
		TEXT("원경 행인 애니메이션을 굽는다 (에디터 전용). 인자: 데이터 에셋 경로"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Bake));
}

#endif // WITH_EDITOR
