// Fill out your copyright notice in the Description page of Project Settings.

#include "Dialogue/DialogueSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Progress/GameProgressSubsystem.h"
#include "NPC/BaseNpcCharacter.h"
#include "NPC/MainNpc/MainNpcAIController.h"
#include "NPC/MainNpc/MainNpcCharacter.h"
#include "NPC/MainNpc/NpcRelationshipSubsystem.h"

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

	EnterNode(BestRow->RootNodeId);
	return true;
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
	PendingChoices.Reset();
	SimpleLineQueue.Reset();

	// NPC를 Talk 상태에서 풀어준다 (BT도 여기서 다시 돌기 시작한다).
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

	ExecuteActions(Row->OnEnterActions);

	OnDialogueLine.Broadcast(Row->SpeakerName, Row->Text);
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

	// 1) 선택지가 있으면 선택지 표시
	if (Row->Choices.Num() > 0)
	{
		PendingChoices = Row->Choices;
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
				AI->StartPatrol(MainNpc->GetDefaultPatrolRoute());
			}
		}
		break;

	case EGameActionType::NpcStopPatrol:
		if (const ABaseNpcCharacter* Npc = FindNpcById(Action.Key))
		{
			if (AMainNpcAIController* AI = Cast<AMainNpcAIController>(Npc->GetController()))
			{
				AI->StopPatrol();
			}
		}
		break;

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
