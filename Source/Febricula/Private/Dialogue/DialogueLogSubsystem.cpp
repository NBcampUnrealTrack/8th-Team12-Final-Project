// Fill out your copyright notice in the Description page of Project Settings.

#include "Dialogue/DialogueLogSubsystem.h"
#include "SaveGame/FebriculaSaveGame.h"

void UDialogueLogSubsystem::AddLine(const FText& Speaker, const FText& Line)
{
	FDialogueLogEntry Entry;
	Entry.Speaker = Speaker;
	Entry.Line = Line;
	Entry.bIsPlayerChoice = false;
	AddLogEntry(Entry);
}

void UDialogueLogSubsystem::AttachChoices(const TArray<FText>& Choices)
{
	if (DialogueLog.Num() == 0)
	{
		return;
	}
	DialogueLog.Last().Choices = Choices;
	OnLogUpdated.Broadcast();
}

void UDialogueLogSubsystem::AddPlayerChoice(const FText& ChoiceText)
{
	FDialogueLogEntry Entry;
	Entry.Speaker = FText::FromString(TEXT("나"));
	Entry.Line = ChoiceText;
	Entry.bIsPlayerChoice = true;
	AddLogEntry(Entry);
}

void UDialogueLogSubsystem::ClearDialogueLog()
{
	DialogueLog.Reset();
	OnLogUpdated.Broadcast();
}

void UDialogueLogSubsystem::LoadDialogueLog(const TArray<FDialogueLogEntry>& InLog)
{
	DialogueLog = InLog;
	OnLogUpdated.Broadcast();
}

void UDialogueLogSubsystem::PackIntoSave(UFebriculaSaveGame* SaveData) const
{
	if (!SaveData)
	{
		return;
	}
	SaveData->DialogueLog = DialogueLog;
}

void UDialogueLogSubsystem::RestoreFromSave(const UFebriculaSaveGame* SaveData)
{
	if (!SaveData)
	{
		return;
	}
	DialogueLog = SaveData->DialogueLog;
	OnLogUpdated.Broadcast();
}

void UDialogueLogSubsystem::AddLogEntry(const FDialogueLogEntry& Entry)
{
	if (DialogueLog.Num() >= MaxLogEntries)
	{
		DialogueLog.RemoveAt(0);
	}
	DialogueLog.Add(Entry);
	OnLogUpdated.Broadcast();
}
