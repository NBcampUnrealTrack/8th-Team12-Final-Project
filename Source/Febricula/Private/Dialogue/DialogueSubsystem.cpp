// Fill out your copyright notice in the Description page of Project Settings.

#include "Dialogue/DialogueSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Activity/ActivitySetting.h"
#include "GameFramework/PlayerController.h"
#include "GameMode/TimeSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Progress/GameProgressSubsystem.h"
#include "NPC/BaseNpcCharacter.h"
#include "NPC/MainNpc/MainNpcAIController.h"
#include "NPC/MainNpc/MainNpcCharacter.h"
#include "NPC/MainNpc/NpcRelationshipSubsystem.h"

TStatId UDialogueSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDialogueSubsystem, STATGROUP_Tickables);
}

void UDialogueSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 자동 진행을 기다리는 중일 때만 일이 있다.
	if (!bDialogueActive || bWaitingForChoice || PendingAutoAdvance == EDialogueAutoAdvance::None)
	{
		return;
	}

	if (!IsAutoAdvanceSatisfied(DeltaTime))
	{
		return;
	}

	PendingAutoAdvance = EDialogueAutoAdvance::None;
	AutoAdvanceNpcId = NAME_None;
	AutoAdvanceElapsed = 0.f;

	if (bRunningSimpleLines)
	{
		// 즉석 대사 모드에서도 자동 진행을 쓸 수 있게 같은 경로를 탄다.
		if (SimpleLineQueue.Num() > 0)
		{
			const FText Next = SimpleLineQueue[0];
			SimpleLineQueue.RemoveAt(0);
			OnDialogueLine.Broadcast(SimpleLineSpeaker, Next);
		}
		else
		{
			FinishDialogue();
		}
		return;
	}

	ResolveNodeExit();
}

bool UDialogueSubsystem::IsAutoAdvanceSatisfied(float DeltaTime)
{
	switch (PendingAutoAdvance)
	{
	case EDialogueAutoAdvance::AfterDelay:
		AutoAdvanceElapsed += DeltaTime;
		return AutoAdvanceElapsed >= AutoAdvanceValue;

	case EDialogueAutoAdvance::WhenPlayerNearNpc:
		{
			const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
			// 기준 NPC를 따로 지정했으면 그쪽, 아니면 상호작용한 상대
			const AActor* Reference = AutoAdvanceNpcId.IsNone()
				? ToRawPtr(CurrentInstigator)
				: Cast<AActor>(FindNpcById(AutoAdvanceNpcId));

			if (!PlayerPawn || !Reference)
			{
				return false;
			}
			const float RadiusSq = FMath::Square(FMath::Max(AutoAdvanceValue, 1.f));
			return FVector::DistSquared2D(PlayerPawn->GetActorLocation(), Reference->GetActorLocation()) <= RadiusSq;
		}

	case EDialogueAutoAdvance::WhenNpcArrived:
		{
			// 여러 NPC가 동시에 이동할 때 누구의 도착을 기다릴지 고를 수 있다.
			const ABaseNpcCharacter* Npc = AutoAdvanceNpcId.IsNone()
				? Cast<ABaseNpcCharacter>(CurrentInstigator)
				: FindNpcById(AutoAdvanceNpcId);

			if (!Npc)
			{
				return false;
			}
			const AMainNpcAIController* AI = Cast<AMainNpcAIController>(Npc->GetController());
			if (!AI)
			{
				return false;
			}
			return AI->HasReachedRouteTarget(FMath::Max(AutoAdvanceValue, 1.f));
		}

	default:
		return false;
	}
}

void UDialogueSubsystem::SetDataTables(UDataTable* InEventTable, UDataTable* InDialogueTable)
{
	EventTable = InEventTable;
	DialogueTable = InDialogueTable;
}

// ---------------------------------------------------------------------------
// 대화 시작
// ---------------------------------------------------------------------------

bool UDialogueSubsystem::TryStartEventForNpc(FName NpcId, AActor* InInstigator)
{
	if (bDialogueActive || !EventTable)
	{
		return false;
	}

	const UGameProgressSubsystem* Progress = GetProgress();
	if (!Progress)
	{
		return false;
	}

	// 조건을 만족하는 이벤트 중 Priority가 가장 높은 것을 고른다.
	// 동점이면 테이블에 먼저 나온 쪽이 이긴다 (데이터 순서 = 이벤트 트리 순서).
	const FNpcEventRow* BestRow = nullptr;
	FName BestRowName = NAME_None;

	static const FString ContextString(TEXT("TryStartEventForNpc"));
	for (const TPair<FName, uint8*>& Pair : EventTable->GetRowMap())
	{
		const FNpcEventRow* Row = reinterpret_cast<const FNpcEventRow*>(Pair.Value);
		if (!Row || Row->NpcId != NpcId)
		{
			continue;
		}

		// 반복 불가 이벤트인데 이미 완료했으면 후보에서 제외
		if (!Row->bRepeatable && Progress->IsEventCompleted(Pair.Key))
		{
			continue;
		}

		if (!Progress->EvaluateConditions(Row->Preconditions))
		{
			continue;
		}

		if (!BestRow || Row->Priority > BestRow->Priority)
		{
			BestRow = Row;
			BestRowName = Pair.Key;
		}
	}

	if (!BestRow)
	{
		return false;
	}

	CurrentInstigator = InInstigator;
	ActiveEventId = BestRowName;
	bCompleteEventOnFinish = BestRow->bCompleteOnFinish;
	bDialogueActive = true;
	bRunningSimpleLines = false;

	// 여러 NPC가 함께 나오는 장면이면 전원을 Talk 상태로 세운다.
	// 상호작용한 NPC는 이미 AMainNpcCharacter::Interact에서 StartTalk을 호출했지만,
	// StartTalk은 중복 호출해도 안전하므로 목록에 같이 넣어 끝날 때 한꺼번에 풀어준다.
	{
		TArray<FName> Participants = BestRow->ParticipantNpcIds;
		Participants.AddUnique(NpcId);
		EnterTalkForParticipants(Participants, InInstigator);
	}

	EnterNode(BestRow->RootNodeId);
	return true;
}

void UDialogueSubsystem::EnterTalkForParticipants(const TArray<FName>& NpcIds, AActor* Interactor)
{
	for (const FName& ParticipantId : NpcIds)
	{
		ABaseNpcCharacter* Npc = FindNpcById(ParticipantId);
		if (!Npc)
		{
			UE_LOG(LogTemp, Warning, TEXT("UDialogueSubsystem: 참가자 NPC '%s'를 월드에서 찾지 못했습니다."),
				*ParticipantId.ToString());
			continue;
		}

		ActiveParticipants.AddUnique(Npc);

		// 메인 NPC만 Talk 상태(=BT 정지 + 상대 응시)를 가진다.
		// 서브 NPC는 그냥 플레이어 쪽을 보게만 해준다.
		if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(Npc))
		{
			MainNpc->StartTalk(Interactor);
		}
		else
		{
			Npc->FaceTowards(Interactor);
		}
	}
}

void UDialogueSubsystem::ReleaseParticipants()
{
	for (ABaseNpcCharacter* Npc : ActiveParticipants)
	{
		if (!Npc)
		{
			continue;
		}

		// 대사 중에 재생시킨 슬롯 애니메이션이 남아있지 않도록 정리한다.
		Npc->StopNpcAnim();

		if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(Npc))
		{
			MainNpc->EndTalk();
		}
	}
	ActiveParticipants.Reset();
}

bool UDialogueSubsystem::StartDialogueAtNode(FName NodeId, AActor* InInstigator)
{
	if (bDialogueActive || !DialogueTable || NodeId.IsNone())
	{
		return false;
	}

	CurrentInstigator = InInstigator;
	ActiveEventId = NAME_None;
	bCompleteEventOnFinish = false;
	bDialogueActive = true;
	bRunningSimpleLines = false;

	EnterNode(NodeId);
	return true;
}

bool UDialogueSubsystem::StartSimpleLines(const FText& Speaker, const TArray<FText>& Lines, AActor* InInstigator)
{
	if (bDialogueActive || Lines.Num() == 0)
	{
		return false;
	}

	CurrentInstigator = InInstigator;
	ActiveEventId = NAME_None;
	bCompleteEventOnFinish = false;
	bDialogueActive = true;
	bWaitingForChoice = false;

	// 테이블을 거치지 않는 즉석 대사 모드. 사물 조사 문구 등에 쓴다.
	bRunningSimpleLines = true;
	SimpleLineSpeaker = Speaker;
	SimpleLineQueue = Lines;

	const FText First = SimpleLineQueue[0];
	SimpleLineQueue.RemoveAt(0);
	OnDialogueLine.Broadcast(SimpleLineSpeaker, First);
	return true;
}

// ---------------------------------------------------------------------------
// 진행
// ---------------------------------------------------------------------------

void UDialogueSubsystem::Advance()
{
	if (!bDialogueActive)
	{
		return;
	}

	// 선택지가 떠 있는 동안은 클릭으로 넘길 수 없다 (반드시 선택해야 함).
	if (bWaitingForChoice)
	{
		return;
	}

	// 자동 진행 대기 중에도 클릭을 막는다.
	// 걸어가는 도중에 대사를 건너뛰면 "도착했을 때 나와야 할 대사"가 미리 나와서 연출이 어긋난다.
	if (PendingAutoAdvance != EDialogueAutoAdvance::None)
	{
		return;
	}

	if (bRunningSimpleLines)
	{
		if (SimpleLineQueue.Num() > 0)
		{
			const FText Next = SimpleLineQueue[0];
			SimpleLineQueue.RemoveAt(0);
			OnDialogueLine.Broadcast(SimpleLineSpeaker, Next);
		}
		else
		{
			FinishDialogue();
		}
		return;
	}

	ResolveNodeExit();
}

void UDialogueSubsystem::SelectChoice(int32 ChoiceIndex)
{
	if (!bDialogueActive || !bWaitingForChoice)
	{
		return;
	}

	if (!PendingChoices.IsValidIndex(ChoiceIndex))
	{
		return;
	}

	const FDialogueChoice Chosen = PendingChoices[ChoiceIndex];

	// 조건 미달 선택지는 UI에서 비활성으로 보이지만, 혹시 모르니 여기서도 막는다.
	const UGameProgressSubsystem* Progress = GetProgress();
	if (Progress && !Progress->EvaluateConditions(Chosen.Conditions))
	{
		return;
	}

	PendingChoices.Reset();
	bWaitingForChoice = false;

	ExecuteActions(Chosen.Actions);

	if (Chosen.NextNodeId.IsNone())
	{
		FinishDialogue();
	}
	else
	{
		EnterNode(Chosen.NextNodeId);
	}
}

void UDialogueSubsystem::FinishDialogue()
{
	if (!bDialogueActive)
	{
		return;
	}

	// 이벤트 완료 기록. 반복 가능 이벤트도 "한 번은 봤다"는 기록은 남겨두면
	// 후속 이벤트의 EventCompleted 조건에서 쓸 수 있다.
	if (bCompleteEventOnFinish && !ActiveEventId.IsNone())
	{
		if (UGameProgressSubsystem* Progress = GetProgress())
		{
			Progress->MarkEventCompleted(ActiveEventId);
		}
	}

	bDialogueActive = false;
	bWaitingForChoice = false;
	bRunningSimpleLines = false;
	CurrentNodeId = NAME_None;
	ActiveEventId = NAME_None;
	PendingAutoAdvance = EDialogueAutoAdvance::None;
	AutoAdvanceNpcId = NAME_None;
	AutoAdvanceElapsed = 0.f;
	PendingChoices.Reset();
	SimpleLineQueue.Reset();

	// 참가자 전원을 Talk 상태에서 풀어준다 (BT도 여기서 다시 돌기 시작한다).
	ReleaseParticipants();

	// 참가자 목록에 안 들어간 경우(사물 조사, 단발 노드 재생 등)를 위한 보정.
	if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(CurrentInstigator))
	{
		MainNpc->EndTalk();
	}
	CurrentInstigator = nullptr;

	OnDialogueFinished.Broadcast();
}

// ---------------------------------------------------------------------------
// 노드 진입 / 이탈
// ---------------------------------------------------------------------------

void UDialogueSubsystem::EnterNode(FName NodeId)
{
	if (!DialogueTable || NodeId.IsNone())
	{
		FinishDialogue();
		return;
	}

	static const FString ContextString(TEXT("EnterNode"));
	const FDialogueNodeRow* Row = DialogueTable->FindRow<FDialogueNodeRow>(NodeId, ContextString, /*bWarnIfMissing=*/false);
	if (!Row)
	{
		UE_LOG(LogTemp, Warning, TEXT("UDialogueSubsystem: 대화 노드 '%s'를 대화 테이블에서 찾지 못했습니다."), *NodeId.ToString());
		FinishDialogue();
		return;
	}

	CurrentNodeId = NodeId;
	bWaitingForChoice = false;

	// 이 대사를 말하는 NPC가 지정돼 있으면 플레이어를 바라보게 한다.
	// 여러 NPC가 함께 있는 장면에서 "지금 말하는 쪽"이 자연스럽게 드러난다.
	if (!Row->SpeakerNpcId.IsNone())
	{
		if (ABaseNpcCharacter* Speaker = FindNpcById(Row->SpeakerNpcId))
		{
			Speaker->FaceTowards(UGameplayStatics::GetPlayerPawn(this, 0));
		}
	}

	ExecuteActions(Row->OnEnterActions);

	// 선택지가 있는 노드는 반드시 골라야 하므로 자동 진행을 걸지 않는다.
	if (Row->Choices.Num() > 0)
	{
		PendingAutoAdvance = EDialogueAutoAdvance::None;
	}
	else
	{
		PendingAutoAdvance = Row->AutoAdvance;
		AutoAdvanceValue = Row->AutoAdvanceValue;
		AutoAdvanceNpcId = Row->AutoAdvanceNpcId;
	}
	AutoAdvanceElapsed = 0.f;

	// 이동 대기 노드: 대사를 띄우지 않고 창만 닫아둔 채 조건이 만족될 때까지 기다린다.
	// 플레이어는 그동안 자유롭게 NPC를 따라간다.
	if (Row->bHideBoxWhileWaiting && PendingAutoAdvance != EDialogueAutoAdvance::None)
	{
		OnDialogueBoxHidden.Broadcast();
		return;
	}

	OnDialogueLine.Broadcast(Row->SpeakerName, Row->Text);

	// 선택지가 있는 노드는 질문 대사와 선택지를 "동시에" 띄운다.
	// 대사를 먼저 보여주고 한 번 더 클릭해야 선택지가 나오면 클릭이 낭비되고,
	// 질문을 읽은 플레이어는 선택지가 바로 나올 거라고 기대하기 때문이다.
	if (Row->Choices.Num() > 0)
	{
		PresentChoices(Row->Choices);
	}
}

void UDialogueSubsystem::PresentChoices(const TArray<FDialogueChoice>& InChoices)
{
	const UGameProgressSubsystem* Progress = GetProgress();

	PendingChoices = InChoices;
	bWaitingForChoice = true;

	TArray<FDialogueChoiceView> Views;
	Views.Reserve(PendingChoices.Num());
	for (const FDialogueChoice& Choice : PendingChoices)
	{
		FDialogueChoiceView View;
		View.Text = Choice.Text;
		// 조건 미달이면 목록엔 보이되 못 고르게 한다 (매력/담력 부족 선택지 노출용)
		View.bEnabled = Progress ? Progress->EvaluateConditions(Choice.Conditions) : true;
		Views.Add(View);
	}

	OnDialogueChoices.Broadcast(Views);
}

void UDialogueSubsystem::ResolveNodeExit()
{
	if (!DialogueTable || CurrentNodeId.IsNone())
	{
		FinishDialogue();
		return;
	}

	static const FString ContextString(TEXT("ResolveNodeExit"));
	const FDialogueNodeRow* Row = DialogueTable->FindRow<FDialogueNodeRow>(CurrentNodeId, ContextString, /*bWarnIfMissing=*/false);
	if (!Row)
	{
		FinishDialogue();
		return;
	}

	const UGameProgressSubsystem* Progress = GetProgress();

	// 1) 선택지는 EnterNode에서 이미 띄웠으므로 여기 오면 안 되지만, 안전장치로 남겨둔다
	if (Row->Choices.Num() > 0)
	{
		PresentChoices(Row->Choices);
		return;
	}

	// 2) 자동 분기. 위에서부터 검사해 첫 통과 갈래를 탄다
	if (Row->Branches.Num() > 0)
	{
		for (const FDialogueBranch& Branch : Row->Branches)
		{
			if (!Progress || Progress->EvaluateConditions(Branch.Conditions))
			{
				ExecuteActions(Branch.Actions);

				if (Branch.NextNodeId.IsNone())
				{
					FinishDialogue();
				}
				else
				{
					EnterNode(Branch.NextNodeId);
				}
				return;
			}
		}

		// 어느 갈래도 통과 못하면 대화를 끝낸다 (데이터에 else 갈래를 넣어두는 걸 권장)
		FinishDialogue();
		return;
	}

	// 3) 단순 다음 노드
	if (Row->NextNodeId.IsNone())
	{
		FinishDialogue();
	}
	else
	{
		EnterNode(Row->NextNodeId);
	}
}

// ---------------------------------------------------------------------------
// 액션 실행
// ---------------------------------------------------------------------------

void UDialogueSubsystem::ExecuteActions(const TArray<FGameAction>& Actions)
{
	for (const FGameAction& Action : Actions)
	{
		ExecuteAction(Action);
	}
}

void UDialogueSubsystem::ExecuteAction(const FGameAction& Action)
{
	UGameProgressSubsystem* Progress = GetProgress();

	switch (Action.Type)
	{
	case EGameActionType::None:
		break;

	case EGameActionType::AddAffinity:
		if (UNpcRelationshipSubsystem* Relationship = GetRelationship())
		{
			Relationship->AddAffinity(Action.Key, static_cast<float>(Action.Value));
		}
		break;

	case EGameActionType::GrantTag:
		if (Progress) { Progress->GrantTag(Action.Key); }
		break;

	case EGameActionType::RemoveTag:
		if (Progress) { Progress->RemoveTag(Action.Key); }
		break;

	case EGameActionType::AddCounter:
		if (Progress) { Progress->AddCounter(Action.Key, Action.Value); }
		break;

	case EGameActionType::CompleteEvent:
		if (Progress) { Progress->MarkEventCompleted(Action.Key); }
		break;

	case EGameActionType::GrantTopic:
		if (Progress) { Progress->GrantTopic(Action.Key); }
		break;

	// ---- 시간 진행 ----

	case EGameActionType::TimeAdvanceDay:
	case EGameActionType::TimeAdvanceSchedule:
		{
			const UWorld* World = GetWorld();
			UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
			UTimeSubsystem* Time = GameInstance ? GameInstance->GetSubsystem<UTimeSubsystem>() : nullptr;
			if (!Time)
			{
				UE_LOG(LogTemp, Warning, TEXT("시간 진행 액션: UTimeSubsystem을 찾지 못했습니다."));
				break;
			}

			// Value를 비워두면(0) 한 번만 진행한다. 실수로 0을 넣어도 아무 일도 안 일어나는 것보다
			// 1회 진행이 의도에 가깝기 때문이다.
			const int32 Steps = FMath::Clamp(Action.Value <= 0 ? 1 : Action.Value, 1, 100);
			for (int32 i = 0; i < Steps; ++i)
			{
				if (Action.Type == EGameActionType::TimeAdvanceDay)
				{
					Time->SpendDay();
				}
				else
				{
					Time->SpendSchedule();
				}
			}
		}
		break;

	// ---- AI 연동 ----

	case EGameActionType::NpcSetAIState:
		if (const ABaseNpcCharacter* Npc = FindNpcById(Action.Key))
		{
			if (AMainNpcAIController* AI = Cast<AMainNpcAIController>(Npc->GetController()))
			{
				AI->SetAIState(static_cast<ENpcAIState>(Action.Value));
			}
		}
		break;

	case EGameActionType::NpcFollowPlayer:
		if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(FindNpcById(Action.Key)))
		{
			if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
			{
				MainNpc->StartFollowTarget(PlayerPawn);
			}
		}
		break;

	case EGameActionType::NpcStopFollow:
		if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(FindNpcById(Action.Key)))
		{
			MainNpc->StopFollowTarget();
		}
		break;

	case EGameActionType::NpcStartPatrol:
		if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(FindNpcById(Action.Key)))
		{
			if (AMainNpcAIController* AI = Cast<AMainNpcAIController>(MainNpc->GetController()))
			{
				// Key2 = RouteId. 비워두면 NPC의 기본 경로를 쓴다.
				AI->StartPatrol(MainNpc->GetRoute(Action.Key2));
			}
		}
		break;

	case EGameActionType::NpcStartGuide:
		if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(FindNpcById(Action.Key)))
		{
			if (AMainNpcAIController* AI = Cast<AMainNpcAIController>(MainNpc->GetController()))
			{
				// 이벤트마다 다른 경로를 쓸 수 있도록 Key2로 경로를 지정한다.
				AI->StartGuide(MainNpc->GetRoute(Action.Key2));
			}
		}
		break;

	case EGameActionType::NpcStopRouteMove:
		if (const ABaseNpcCharacter* Npc = FindNpcById(Action.Key))
		{
			if (AMainNpcAIController* AI = Cast<AMainNpcAIController>(Npc->GetController()))
			{
				AI->StopRouteMovement();
			}
		}
		break;

	case EGameActionType::NpcResumeAI:
		if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(FindNpcById(Action.Key)))
		{
			MainNpc->ResumeAIDuringTalk();
		}
		break;

	case EGameActionType::NpcPauseAI:
		if (AMainNpcCharacter* MainNpc = Cast<AMainNpcCharacter>(FindNpcById(Action.Key)))
		{
			MainNpc->PauseAIDuringTalk();
		}
		break;

	// ---- 애니메이션 ----

	case EGameActionType::NpcPlayAnim:
		if (ABaseNpcCharacter* Npc = FindNpcById(Action.Key))
		{
			Npc->PlayNpcAnim(Action.Key2, /*bLoop=*/Action.Value != 0);
		}
		break;

	case EGameActionType::NpcStopAnim:
		if (ABaseNpcCharacter* Npc = FindNpcById(Action.Key))
		{
			Npc->StopNpcAnim();
		}
		break;

	case EGameActionType::NpcFacePlayer:
		if (ABaseNpcCharacter* Npc = FindNpcById(Action.Key))
		{
			Npc->FaceTowards(UGameplayStatics::GetPlayerPawn(this, 0));
		}
		break;
		
	case EGameActionType::ChangeLevel:
		if (!Action.Key2.IsNone())
		{
			if (const UGameInstance* GI = GetWorld()->GetGameInstance())
			{
				if (UTimeSubsystem* TimeSub = GI->GetSubsystem<UTimeSubsystem>())
				{
					TimeSub->SpendSchedule();
				}
			}
		}
		if (const UActivitySetting* ActivitySetting = GetDefault<UActivitySetting>())
		{
			UGameplayStatics::OpenLevelBySoftObjectPtr(this, *ActivitySetting->Levels.Find(Action.Key));
		}

	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// 내부 헬퍼
// ---------------------------------------------------------------------------

UGameProgressSubsystem* UDialogueSubsystem::GetProgress() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UGameProgressSubsystem>();
		}
	}
	return nullptr;
}

UNpcRelationshipSubsystem* UDialogueSubsystem::GetRelationship() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UNpcRelationshipSubsystem>();
		}
	}
	return nullptr;
}

ABaseNpcCharacter* UDialogueSubsystem::FindNpcById(FName NpcId) const
{
	UWorld* World = GetWorld();
	if (!World || NpcId.IsNone())
	{
		return nullptr;
	}

	for (TActorIterator<ABaseNpcCharacter> It(World); It; ++It)
	{
		if (It->GetNpcId() == NpcId)
		{
			return *It;
		}
	}
	return nullptr;
}
