// [skydodo] 2026-10-08 작성
// 요약: 메인 NPC 스포너 구현 - 일차/시간대/조건으로 자리 선택, 시간 변경 시 재배치, 테이블 검사
// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/MainNpcSpawner.h"

#include "Components/CapsuleComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameMode/TimeSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/MainNpc/MainNpcAIController.h"
#include "NPC/MainNpc/MainNpcCharacter.h"
#include "NPC/MainNpc/MainNpcSpawnPoint.h"
#include "NPC/MainNpc/NpcRoute.h"
#include "Progress/GameProgressSubsystem.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMainNpcSpawner, Log, All);


namespace MainNpcSpawnerPrivate
{
	/**
	 * 두 행이 "서 있는 모습"이 같은가. 같으면 NPC를 다시 만들 필요가 없다.
	 * 예) 1일차 행과 2일차 행이 같은 자리·같은 설정이면 날이 바뀌어도 그대로 둔다.
	 */
	bool IsSamePlacement(const FMainNpcSpawnRow& A, const FMainNpcSpawnRow& B)
	{
		return A.NpcClass == B.NpcClass
			&& A.Level == B.Level
			&& A.SpawnPointId == B.SpawnPointId
			&& A.StartAnimId == B.StartAnimId
			&& A.StartAIState == B.StartAIState
			&& A.RouteId == B.RouteId
			&& A.EventTable == B.EventTable
			&& A.DialogueTable == B.DialogueTable;
	}
}

AMainNpcSpawner::AMainNpcSpawner()
{
	// 레벨 진입과 시간 변경 때만 일한다.
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AMainNpcSpawner::BeginPlay()
{
	Super::BeginPlay();
	
	/**
	 * 랜덤 확률 기반 스폰 로직
	 */

	if (!ScheduleTable)
	{
		UE_LOG(LogMainNpcSpawner, Warning,
			TEXT("%s: Schedule Table이 지정되지 않아 메인 NPC가 생성되지 않습니다."), *GetNameSafe(this));
		return;
	}

	if (bValidateOnBeginPlay)
	{
		ValidateSchedule();

		// 한 레벨 안에서 자리 이름이 겹치면 어느 쪽에 설지 알 수 없다.
		TSet<FName> SeenPointIds;
		for (TActorIterator<AMainNpcSpawnPoint> It(GetWorld()); It; ++It)
		{
			
			if (It->GetSpawnPointType() == ESpawnPointType::MultiByRandom) continue;
			
			const FName PointId = It->GetSpawnPointId();
			if (PointId.IsNone())
			{
				UE_LOG(LogMainNpcSpawner, Warning, TEXT("%s: SpawnPointId가 비어 있습니다."), *GetNameSafe(*It));
			}
			else if (SeenPointIds.Contains(PointId))
			{
				UE_LOG(LogMainNpcSpawner, Warning,
					TEXT("%s: SpawnPointId '%s'가 이 레벨에 두 개 이상 있습니다."), *GetNameSafe(*It), *PointId.ToString());
			}
			SeenPointIds.Add(PointId);
		}
	}

	if (UTimeSubsystem* Time = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTimeSubsystem>() : nullptr)
	{
		Time->OnDayChanged.AddUniqueDynamic(this, &AMainNpcSpawner::HandleDayChanged);
		Time->OnScheduleChanged.AddUniqueDynamic(this, &AMainNpcSpawner::HandleScheduleChanged);
	}

	// 레벨 진입 시점에는 미루지 않고 바로 세운다.
	RefreshNpcs();
}

void AMainNpcSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 시간 서브시스템은 GameInstance 소속이라 레벨이 바뀌어도 살아 있다. 끊어주지 않으면 죽은 스포너를 부른다.
	if (UTimeSubsystem* Time = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTimeSubsystem>() : nullptr)
	{
		Time->OnDayChanged.RemoveDynamic(this, &AMainNpcSpawner::HandleDayChanged);
		Time->OnScheduleChanged.RemoveDynamic(this, &AMainNpcSpawner::HandleScheduleChanged);
	}

	GetWorldTimerManager().ClearTimer(RefreshTimerHandle);
	GetWorldTimerManager().ClearTimer(DeferredRetryHandle);

	Super::EndPlay(EndPlayReason);
}

void AMainNpcSpawner::HandleDayChanged(int32 ChangedDay)
{
	RequestRefresh();
}

void AMainNpcSpawner::HandleScheduleChanged(FGameplayTag ChangedScheduleName)
{
	RequestRefresh();
}

void AMainNpcSpawner::HandleNpcMovementStateChanged(ENpcMovementState OldState, ENpcMovementState NewState)
{
	if (bRefreshDeferred)
	{
		RequestRefresh();
	}
}

void AMainNpcSpawner::RequestRefresh()
{
	// 날이 바뀌면 OnDayChanged 직후 시간대도 초기화되면서 OnScheduleChanged가 이어서 온다.
	// 중간 상태로 한 번 세웠다가 바로 치우는 일이 없게, 다음 틱에 한 번만 계산한다.
	if (!RefreshTimerHandle.IsValid())
	{
		RefreshTimerHandle = GetWorldTimerManager().SetTimerForNextTick(this, &AMainNpcSpawner::RefreshNpcs);
	}
}

void AMainNpcSpawner::RefreshNpcs()
{
	RefreshTimerHandle.Invalidate();
	bRefreshDeferred = false;


	if (!ScheduleTable)
	{
		return;
	}

	const TMap<FName, const FMainNpcSpawnRow*> CurrentRows = ResolveCurrentRows();

	// 지금 세워야 할 NPC + 이미 세워 둔 NPC 둘 다 살핀다 (빠져야 할 NPC도 있으므로).
	TSet<FName> NpcIds;
	CurrentRows.GetKeys(NpcIds);
	for (const TPair<FName, TObjectPtr<AMainNpcCharacter>>& Pair : SpawnedNpcs)
	{
		NpcIds.Add(Pair.Key);
	}

	// [skydodo] 2026-10-08 수정: 내리기를 전부 끝낸 뒤에 세운다.
	// 한 번에 처리하면 A가 비우기 전에 B가 같은 자리에 먼저 생겨서 겹치고, 스폰 겹침 보정 때문에 옆으로 밀려난다.
	TArray<const FMainNpcSpawnRow*> RowsToSpawn;
	
	TArray<FName> SortedNpcIds = NpcIds.Array();

	for (const FName& NpcId : NpcIds)
	{
		SortedNpcIds.Sort(FNameLexicalLess());
	}
	
	TArray<const FMainNpcSpawnRow*> ToSpawn;
	
	
	const UGameProgressSubsystem* GameProgress = GetWorld()->GetGameInstance()->GetSubsystem<UGameProgressSubsystem>();
	
	for (const FName& NpcId : SortedNpcIds)
	{
		const FMainNpcSpawnRow* const* FoundRow = CurrentRows.Find(NpcId);
		const FMainNpcSpawnRow* WantedRow = nullptr;

		if (GameProgress)
		{
			if (FoundRow && IsRowForThisLevel(**FoundRow))
			{
				//RandomStream.Initialize(GameProgress->GetSpawnerSeed());
				if (GameProgress->CheckNpcCanSpawn(**FoundRow))
				{
					WantedRow = *FoundRow;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("Request Refresh: 현재 Seed는 %d"), GameProgress->GetSpawnerSeed());

		}
		
		if (AMainNpcCharacter* Current = FindSpawnedNpc(NpcId))
		{
			// [skydodo] 2026-10-08 수정: 행이 달라도 서 있는 모습이 같으면 그대로 둔다 (날이 바뀔 때 깜빡이지 않게)
			const FMainNpcSpawnRow* CurrentRow = SpawnedRows.FindRef(NpcId);
			if (WantedRow && CurrentRow && MainNpcSpawnerPrivate::IsSamePlacement(*CurrentRow, *WantedRow))
			{
				SpawnedRows.Add(NpcId, WantedRow);
				continue;	// 이미 같은 자리/애니메이션/AIState/대화로 서 있다
			}

			// 대화/동행/안내/추적 중에 사라지면 이벤트가 깨진다. 끝나면 다시 돈다 (HandleNpcMovementStateChanged, DeferredRetryHandle).
			if (IsNpcBusy(Current))
			{
				bRefreshDeferred = true;
				continue;
			}

			DespawnNpc(NpcId);
		}
		else
		{
			// 밖에서 지워졌으면 기록만 정리한다.
			SpawnedNpcs.Remove(NpcId);
			SpawnedRows.Remove(NpcId);
			SpawnedPoints.Remove(NpcId);
		}

		if (WantedRow)
		{
			ToSpawn.Add(WantedRow);
			
		}
	}
	
	for (const FMainNpcSpawnRow* Row : ToSpawn)
	{
		SpawnFromRow(*Row);
	}

	// 안내/추적이 끝나는 건 이동 상태 이벤트로 오지 않을 수 있어서, 미뤘으면 잠시 뒤에 다시 본다.
	if (bRefreshDeferred)
	{
		GetWorldTimerManager().SetTimer(DeferredRetryHandle, this, &AMainNpcSpawner::RequestRefresh, 1.f, false);
	}
}

TMap<FName, const FMainNpcSpawnRow*> AMainNpcSpawner::ResolveCurrentRows() const
{
	TMap<FName, const FMainNpcSpawnRow*> Result;

	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance || !ScheduleTable)
	{
		return Result;
	}

	// 일차는 GameProgressSubsystem을 거친다. 시간 시스템이 없을 때 FallbackDay로 테스트할 수 있다.
	const UGameProgressSubsystem* Progress = GameInstance->GetSubsystem<UGameProgressSubsystem>();
	const int32 CurrentDay = Progress ? Progress->GetCurrentDay() : 1;

	const UTimeSubsystem* Time = GameInstance->GetSubsystem<UTimeSubsystem>();
	const FGameplayTag CurrentSchedule = Time ? Time->GetNowScheduleTag() : FGameplayTag();

	TArray<FMainNpcSpawnRow*> Rows;
	ScheduleTable->GetAllRows<FMainNpcSpawnRow>(TEXT("AMainNpcSpawner::ResolveCurrentRows"), Rows);
	for (const FMainNpcSpawnRow* Row : Rows)
	{
		if (!Row || Row->NpcId.IsNone())
		{
			continue;
		}
		
		if (Row->Day > 0 && Row->Day != CurrentDay)
		{
			continue;
		}

		// 시간대가 비어 있으면 하루 종일. 지정돼 있으면 지금 시간대와 같아야 한다.
		if (!Row->ScheduleTag.IsEmpty() && !Row->ScheduleTag.HasTagExact(CurrentSchedule))
		{
			continue;
		}
		
		if (Row->Conditions.Num() > 0 && !(Progress && Progress->EvaluateConditions(Row->Conditions)))
		{
			continue;
		}

		const FMainNpcSpawnRow*& Best = Result.FindOrAdd(Row->NpcId);
		if (!Best || IsRowPreferred(*Row, *Best))
		{
			Best = Row;
		}
	}

	return Result;
}

bool AMainNpcSpawner::IsRowPreferred(const FMainNpcSpawnRow& A, const FMainNpcSpawnRow& B)
{
	// 기본 자리(조건 없음)와 호감도/진행 조건을 채운 자리가 둘 다 맞으면 조건 쪽이 이긴다.
	// 조건을 채웠다는 건 "평소와 다른 곳에 있어야 할 이유"가 생겼다는 뜻이다.
	const bool bAConditional = A.Conditions.Num() > 0;
	const bool bBConditional = B.Conditions.Num() > 0;
	if (bAConditional != bBConditional)
	{
		return bAConditional;
	}

	// 조건 행끼리 동시에 맞으면 (예: 신뢰 이상 -> 옥상, 친밀 이상 -> 카페) 우선순위로 가른다.
	if (A.Priority != B.Priority)
	{
		return A.Priority > B.Priority;
	}

	// 우선순위가 같으면 "하루 종일" 보다 시간대를 콕 집은 행이 이긴다.
	// (하루 종일 도서관, 점심만 식당 같은 식으로 적을 수 있게)
	// 그것도 같으면 테이블에서 먼저 나온 행이 이긴다.
	return A.ScheduleTag.IsValid() && !B.ScheduleTag.IsValid();
}

AMainNpcCharacter* AMainNpcSpawner::SpawnFromRow(const FMainNpcSpawnRow& Row)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	
	// 한 NPC는 한 명만. 레벨에 직접 배치해 둔 것도 포함해서 이미 있으면 새로 만들지 않는다.
	if (AMainNpcCharacter* Existing = FindNpcInWorld(Row.NpcId))
	{
		UE_LOG(LogMainNpcSpawner, Warning,
			TEXT("%s: %s 은(는) 이미 레벨에 있어서(%s) 스폰하지 않습니다."),
			*GetNameSafe(this), *Row.NpcId.ToString(), *GetNameSafe(Existing));
		return nullptr;
	}

	TArray<AMainNpcSpawnPoint*> SpawnPoints = FindSpawnPoint(Row.SpawnPointId, Row.SpawnPointType);
	if (SpawnPoints.Num() == 0)
	{
		UE_LOG(LogMainNpcSpawner, Warning,
			TEXT("%s: %s 의 자리 '%s'를 이 레벨에서 찾지 못했습니다."),
			*GetNameSafe(this), *Row.NpcId.ToString(), *Row.SpawnPointId.ToString());
		return nullptr;
	}

	UClass* NpcClass = Row.NpcClass.LoadSynchronous();
	if (!NpcClass)
	{
		UE_LOG(LogMainNpcSpawner, Warning,
			TEXT("%s: %s 의 Npc Class를 불러오지 못했습니다."), *GetNameSafe(this), *Row.NpcId.ToString());
		return nullptr;
	}

	TSet<const AMainNpcSpawnPoint*> OccupiedPoints;
	
	for (const TPair<FName, TObjectPtr<AMainNpcSpawnPoint>>& SpawnedPoint: SpawnedPoints)
	{
		OccupiedPoints.Add(SpawnedPoint.Value.Get());
	}
	
	SpawnPoints.Sort([](const AMainNpcSpawnPoint& A, const AMainNpcSpawnPoint& B)
	{
		return A.GetFName().LexicalLess(B.GetFName());
	});
	
	if (SpawnPoints.Num() > 1)
	{
		if (const UGameProgressSubsystem* GameProgress = GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
		{
			const FRandomStream Stream = GameProgress->MakeRandomStream(Row, /*Salt=*/1);
			for (int32 i = SpawnPoints.Num() - 1; i > 0; --i)
			{
				SpawnPoints.Swap(i, Stream.RandRange(0, i));
			}
		}
	}
	
	AMainNpcSpawnPoint* SpawnPoint = nullptr;
	
	for (AMainNpcSpawnPoint* Candidate : SpawnPoints)
	{
		if (!OccupiedPoints.Contains(Candidate))
		{
			SpawnPoint = Candidate;
			break;
		}
	}
	if (!SpawnPoint)
	{
		UE_LOG(LogMainNpcSpawner, Warning,
			TEXT("%s: %s 의 자리 '%s'가 모두 차 있어 스폰하지 않습니다."),
			*GetNameSafe(this), *Row.NpcId.ToString(), *Row.SpawnPointId.ToString());
		return nullptr;
	}
	// 자리 액터는 발 위치다. 캐릭터 원점은 캡슐 중심이므로 반 높이만큼 올린다.
	FTransform SpawnTransform = SpawnPoint->GetActorTransform();
	if (const AMainNpcCharacter* Cdo = NpcClass->GetDefaultObject<AMainNpcCharacter>())
	{
		SpawnTransform.AddToTranslation(FVector(0.f, 0.f, Cdo->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	}
	SpawnTransform.SetScale3D(FVector::OneVector);

	// NpcId를 BeginPlay 전에 넣어야 하므로 지연 스폰한다.
	AMainNpcCharacter* Npc = World->SpawnActorDeferred<AMainNpcCharacter>(
		NpcClass, SpawnTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Npc)
	{
		UE_LOG(LogMainNpcSpawner, Warning,
			TEXT("%s: %s 스폰에 실패했습니다."), *GetNameSafe(this), *Row.NpcId.ToString());
		return nullptr;
	}

	// 블루프린트에 NpcId가 고정돼 있으면 그대로 둔다 (SetNpcId는 덮어쓰기를 막고 경고를 낸다).
	// 테이블과 다르면 데이터 실수다.
	if (Npc->GetNpcId().IsNone())
	{
		Npc->SetNpcId(Row.NpcId);
	}
	else if (Npc->GetNpcId() != Row.NpcId)
	{
		UE_LOG(LogMainNpcSpawner, Warning,
			TEXT("%s: 테이블 NpcId(%s)와 블루프린트 NpcId(%s)가 다릅니다."),
			*GetNameSafe(this), *Row.NpcId.ToString(), *Npc->GetNpcId().ToString());
	}

	// 애니메이션 컴포넌트가 BeginPlay에서 DefaultAnimation을 읽으므로 FinishSpawning 전에 넣는다.
	if (!Row.StartAnimId.IsNone())
	{
		Npc->SetDefaultAnimation(Row.StartAnimId);
	}

	// [skydodo] 2026-10-08 추가: 자리(상황)별 대화. 비어 있으면 블루프린트 설정을 그대로 둔다.
	Npc->SetDialogueTables(Row.EventTable.LoadSynchronous(), Row.DialogueTable);

	Npc->FinishSpawning(SpawnTransform);

	// FinishSpawning 안에서 AIController 빙의와 BehaviorTree 기동까지 끝나 있다.
	ApplyStartAIState(Npc, Row);

	Npc->OnNpcMovementStateChanged.AddUniqueDynamic(this, &AMainNpcSpawner::HandleNpcMovementStateChanged);

	SpawnedNpcs.Add(Row.NpcId, Npc);
	SpawnedRows.Add(Row.NpcId, &Row);
	
	SpawnedPoints.Add(Row.NpcId, SpawnPoint);
	
	UE_LOG(LogMainNpcSpawner, Log, TEXT("%s: %s -> %s"),
		*GetNameSafe(this), *Row.NpcId.ToString(), *Row.SpawnPointId.ToString());
	
	if (Row.SpawnPointType == ESpawnPointType::MultiByRandom)
	{
		UE_LOG(LogMainNpcSpawner, Log, TEXT("%s: %s -> %s"),
			*GetNameSafe(this), *Row.NpcId.ToString(), *GetNameSafe(SpawnPoint));
	}
	return Npc;
}

void AMainNpcSpawner::ApplyStartAIState(AMainNpcCharacter* Npc, const FMainNpcSpawnRow& Row) const
{
	AMainNpcAIController* NpcAIController = Cast<AMainNpcAIController>(Npc->GetController());
	if (!NpcAIController)
	{
		if (Row.StartAIState != ENpcAIState::Idle)
		{
			UE_LOG(LogMainNpcSpawner, Warning,
				TEXT("%s: %s 에 AMainNpcAIController가 없어 AIState를 넣지 못했습니다."),
				*GetNameSafe(this), *Row.NpcId.ToString());
		}
		return;
	}

	switch (Row.StartAIState)
	{
	case ENpcAIState::Patrol:
	case ENpcAIState::Guide:
	{
		ANpcRoute* Route = Npc->GetRoute(Row.RouteId);
		if (!Route)
		{
			UE_LOG(LogMainNpcSpawner, Warning,
				TEXT("%s: %s 의 경로 '%s'를 찾지 못해 Idle로 둡니다. ANpcRoute에 같은 이름의 Actor Tag가 있는지 확인하세요."),
				*GetNameSafe(this), *Row.NpcId.ToString(), *Row.RouteId.ToString());
			NpcAIController->SetAIState(ENpcAIState::Idle);
			return;
		}

		if (Row.StartAIState == ENpcAIState::Patrol)
		{
			NpcAIController->StartPatrol(Route);
		}
		else
		{
			NpcAIController->StartGuide(Route);
		}
		break;
	}

	case ENpcAIState::Chase:
		NpcAIController->StartChase(UGameplayStatics::GetPlayerPawn(this, 0));
		break;

	default:
		NpcAIController->SetAIState(ENpcAIState::Idle);
		break;
	}
}

void AMainNpcSpawner::DespawnNpc(FName NpcId)
{
	if (AMainNpcCharacter* Npc = FindSpawnedNpc(NpcId))
	{
		Npc->OnNpcMovementStateChanged.RemoveDynamic(this, &AMainNpcSpawner::HandleNpcMovementStateChanged);
		Npc->Destroy();
		const FMainNpcSpawnRow* Row = SpawnedRows.FindRef(NpcId);
		UE_LOG(LogMainNpcSpawner, Log, TEXT("%s: %s 내림 (%s)"),
			*GetNameSafe(this), *NpcId.ToString(), Row ? *Row->SpawnPointId.ToString() : TEXT("-"));
		UE_LOG(LogMainNpcSpawner, Log, TEXT("%s: %s 내림 (%s)"),
	*GetNameSafe(this), *NpcId.ToString(), *GetNameSafe(SpawnedPoints.FindRef(NpcId)));
	}
	
	SpawnedNpcs.Remove(NpcId);
	SpawnedRows.Remove(NpcId);
	SpawnedPoints.Remove(NpcId);
}

bool AMainNpcSpawner::IsNpcBusy(const AMainNpcCharacter* Npc)
{
	const ENpcMovementState State = Npc->GetMovementState();
	if (State == ENpcMovementState::Talk
		|| State == ENpcMovementState::Follow
		|| State == ENpcMovementState::Interacting)
	{
		return true;
	}

	// 안내/추적은 플레이어가 끼어 있는 연출이라 도중에 사라지면 안 된다. 순찰은 혼자 도는 것이라 괜찮다.
	if (const AMainNpcAIController* NpcAIController = Cast<AMainNpcAIController>(Npc->GetController()))
	{
		const ENpcAIState AIState = NpcAIController->GetAIState();
		return AIState == ENpcAIState::Guide || AIState == ENpcAIState::Chase;
	}
	return false;
}

AMainNpcCharacter* AMainNpcSpawner::FindSpawnedNpc(FName NpcId) const
{
	const TObjectPtr<AMainNpcCharacter>* Found = SpawnedNpcs.Find(NpcId);
	return Found && IsValid(*Found) ? Found->Get() : nullptr;
}

AMainNpcCharacter* AMainNpcSpawner::FindNpcInWorld(FName NpcId) const
{
	for (TActorIterator<AMainNpcCharacter> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It) && It->GetNpcId() == NpcId)
		{
			return *It;
		}
	}
	return nullptr;
}

TArray<AMainNpcSpawnPoint*> AMainNpcSpawner::FindSpawnPoint(FName SpawnPointId, ESpawnPointType PointType) const
{
	if (PointType == ESpawnPointType::OneByOne && SpawnPointId.IsNone())
	{
		return TArray<AMainNpcSpawnPoint*>();
	}

	
	TArray<AMainNpcSpawnPoint*> Founded;

	if (PointType == ESpawnPointType::MultiByRandom)
	{
		for (TActorIterator<AMainNpcSpawnPoint> It(GetWorld()); It; ++It)
		{
			if (It->GetSpawnPointType() == PointType)
			{
				Founded.Add(*It);
			}
		}

	}
	else
	{
		for (TActorIterator<AMainNpcSpawnPoint> It(GetWorld()); It; ++It)
		{
			if (It->GetSpawnPointId() == SpawnPointId)
			{
				Founded.Add(*It);
			}
		}
	}
	return Founded;
}

bool AMainNpcSpawner::IsRowForThisLevel(const FMainNpcSpawnRow& Row) const
{
	if (Row.Level.IsNull())
	{
		return false;
	}

	// PIE에서는 패키지 이름 앞에 UEDPIE_0_ 이 붙으므로 떼고 비교한다.
	const FString CurrentLevel = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	return Row.Level.ToSoftObjectPath().GetLongPackageName() == CurrentLevel;
}

TArray<FString> AMainNpcSpawner::DescribeSpawnedNpcs() const
{
	TArray<FString> Lines;
	for (const TPair<FName, const FMainNpcSpawnRow*>& Pair : SpawnedRows)
	{
		const AMainNpcCharacter* Npc = FindSpawnedNpc(Pair.Key);
		const FMainNpcSpawnRow* Row = Pair.Value;
		if (!Npc || !Row)
		{
			continue;
		}

		const AMainNpcAIController* NpcAIController = Cast<AMainNpcAIController>(Npc->GetController());
		Lines.Add(FString::Printf(TEXT("%s @ %s  anim=%s  AI=%s  move=%s"),
			*Pair.Key.ToString(),
			*GetNameSafe(SpawnedPoints.FindRef(Pair.Key)),
			*Npc->GetDefaultAnimation().ToString(),
			NpcAIController ? *UEnum::GetDisplayValueAsText(NpcAIController->GetAIState()).ToString() : TEXT("-"),
			*UEnum::GetDisplayValueAsText(Npc->GetMovementState()).ToString()));
	}
	return Lines;
}

bool AMainNpcSpawner::ValidateSchedule() const
{
	if (!ScheduleTable)
	{
		return false;
	}

	struct FEntry
	{
		FName RowName;
		const FMainNpcSpawnRow* Row;
	};
	TMap<FName, TArray<FEntry>> EntriesByNpc;

	bool bValid = true;
	ScheduleTable->ForeachRow<FMainNpcSpawnRow>(TEXT("AMainNpcSpawner::ValidateSchedule"),
		[&](const FName& RowName, const FMainNpcSpawnRow& Row)
		{
			const bool bNeedsPointId = Row.SpawnPointType == ESpawnPointType::OneByOne;

			if (Row.NpcId.IsNone() || Row.NpcClass.IsNull() || Row.Level.IsNull() || (bNeedsPointId && Row.SpawnPointId.IsNone()))
			{
				UE_LOG(LogMainNpcSpawner, Warning,
					TEXT("[Schedule] %s 행: NpcId / Npc Class / Level / SpawnPointId 중 비어 있는 값이 있습니다."),
					*RowName.ToString());
				bValid = false;
			}
			if ((Row.StartAIState == ENpcAIState::Patrol || Row.StartAIState == ENpcAIState::Guide) && Row.RouteId.IsNone())
			{
				UE_LOG(LogMainNpcSpawner, Warning,
					TEXT("[Schedule] %s 행: Patrol/Guide인데 RouteId가 비어 있습니다."), *RowName.ToString());
				bValid = false;
			}
			EntriesByNpc.FindOrAdd(Row.NpcId).Add({RowName, &Row});
		});

	// 같은 날, 같은 시간대, 같은 우선순위에 조건도 없는 행이 둘이면 뒤쪽은 절대 뽑히지 않는다.
	// 조건이 붙어 있으면 상황에 따라 갈리므로 여기서는 문제 삼지 않는다 (겹치면 테이블 순서로 정해진다).
	for (const TPair<FName, TArray<FEntry>>& Pair : EntriesByNpc)
	{
		const TArray<FEntry>& Entries = Pair.Value;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			for (int32 j = i + 1; j < Entries.Num(); ++j)
			{
				const FMainNpcSpawnRow& A = *Entries[i].Row;
				const FMainNpcSpawnRow& B = *Entries[j].Row;
				const bool bSameDay = A.Day == B.Day || A.Day <= 0 || B.Day <= 0;
				if (bSameDay && A.ScheduleTag == B.ScheduleTag && A.Priority == B.Priority
					&& A.Conditions.Num() == 0 && B.Conditions.Num() == 0)
				{
					UE_LOG(LogMainNpcSpawner, Warning,
						TEXT("[Schedule] %s 가 %d일차 같은 시간에 두 곳으로 잡혀 있습니다: %s / %s (뒤쪽은 쓰이지 않음)"),
						*Pair.Key.ToString(), A.Day, *Entries[i].RowName.ToString(), *Entries[j].RowName.ToString());
					bValid = false;
				}
			}
		}
	}

	return bValid;
}
