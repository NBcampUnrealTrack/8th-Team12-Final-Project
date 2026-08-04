// Copyright Recursoft LLC. All Rights Reserved.

#include "SMBlueprintDebuggerCommands.h"

#include "Debug/SMBlueprintDebugController.h"

#include "Editor.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/UICommandList.h"
#include "Kismet2/DebuggerCommands.h"

FSMBlueprintDebuggerCommands::FSMBlueprintDebuggerCommands()
{
	FEditorDelegates::SingleStepPIE.AddRaw(this, &FSMBlueprintDebuggerCommands::OnPieSingleStep);
	FEditorDelegates::CancelPIE.AddRaw(this, &FSMBlueprintDebuggerCommands::OnCancelPie);
	
	HookAllUEActions();
}

FSMBlueprintDebuggerCommands::~FSMBlueprintDebuggerCommands()
{
	FEditorDelegates::SingleStepPIE.RemoveAll(this);
	FEditorDelegates::CancelPIE.RemoveAll(this);

	RestoreAllUEActions();
}

void FSMBlueprintDebuggerCommands::MapGlobalAction(const TSharedPtr<FUICommandInfo>& InUECommand, LD::Editor::Debug::EDebugCommandType InDebugType)
{
	const FUIAction* OriginalCommandPtr = FPlayWorldCommands::GlobalPlayWorldActions->GetActionForCommand(InUECommand);
	check(OriginalCommandPtr);
	
	TSharedPtr<FUIAction> CommandAction = MakeShared<FUIAction>(*OriginalCommandPtr);
	OriginalUECommandActionMap.Add(InUECommand, CommandAction);
	
	FPlayWorldCommands::GlobalPlayWorldActions->MapAction(
		InUECommand,
		FExecuteAction::CreateRaw(this, &FSMBlueprintDebuggerCommands::CommandExecute, CommandAction, InDebugType),
		FCanExecuteAction::CreateRaw(this, &FSMBlueprintDebuggerCommands::CanExecuteCommand, CommandAction),
		FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSMBlueprintDebuggerCommands::IsCommandVisible, CommandAction)
	);
}

void FSMBlueprintDebuggerCommands::UnmapGlobalAction(const TSharedPtr<FUICommandInfo>& InUECommand)
{
	const TSharedPtr<FUIAction> CommandAction = OriginalUECommandActionMap.FindAndRemoveChecked(InUECommand);
	// Overwrite our mapping with the original action.
	FPlayWorldCommands::GlobalPlayWorldActions->MapAction(InUECommand, *CommandAction);
}

void FSMBlueprintDebuggerCommands::HookAllUEActions()
{
	const FPlayWorldCommands& Commands = FPlayWorldCommands::Get();
	MapGlobalAction(Commands.StepInto, LD::Editor::Debug::EDebugCommandType::StepInto);
	MapGlobalAction(Commands.StepOver, LD::Editor::Debug::EDebugCommandType::StepOver);
	MapGlobalAction(Commands.StepOut, LD::Editor::Debug::EDebugCommandType::StepOut);
	
	// Empty commands, used for cleanup.
	MapGlobalAction(Commands.ResumePlaySession, LD::Editor::Debug::EDebugCommandType::Default);
	MapGlobalAction(Commands.ContinueExecution, LD::Editor::Debug::EDebugCommandType::Default);
	MapGlobalAction(Commands.SingleFrameAdvance, LD::Editor::Debug::EDebugCommandType::Default);
}

void FSMBlueprintDebuggerCommands::RestoreAllUEActions()
{
	TArray<TSharedPtr<FUICommandInfo>> Keys;
	OriginalUECommandActionMap.GetKeys(Keys);
	for (const TSharedPtr<FUICommandInfo>& Key : Keys)
	{
		UnmapGlobalAction(Key);
	}
}

void FSMBlueprintDebuggerCommands::CommandExecute(TSharedPtr<FUIAction> OriginalAction, LD::Editor::Debug::EDebugCommandType InDebugType)
{
	bool bExecuteOriginalCommand = true;
	if (USMBlueprintDebugController::IsInitialized())
	{
		bIsExecutingCommand = true;
		bExecuteOriginalCommand = USMBlueprintDebugController::GetInstance()->NotifyNewCommandStarting(InDebugType);
	}
	
	if (bExecuteOriginalCommand)
	{
		OriginalAction->Execute();
	}
	
	bIsExecutingCommand = false;
}

bool FSMBlueprintDebuggerCommands::CanExecuteCommand(TSharedPtr<FUIAction> OriginalAction) const
{
	return OriginalAction->CanExecute();
}

bool FSMBlueprintDebuggerCommands::IsCommandVisible(TSharedPtr<FUIAction> OriginalAction) const
{
	return OriginalAction->IsActionVisibleDelegate.IsBound() ? OriginalAction->IsActionVisibleDelegate.Execute() : true;
}

void FSMBlueprintDebuggerCommands::ResetCommand()
{
	if (USMBlueprintDebugController::IsInitialized())
	{
		USMBlueprintDebugController::GetInstance()->ResetCommand();
	}
}

void FSMBlueprintDebuggerCommands::ResetCommandIfNotExecuting()
{
	if (!bIsExecutingCommand)
	{
		ResetCommand();
	}
}

void FSMBlueprintDebuggerCommands::OnPieSingleStep(bool bIsSimulating)
{
	ResetCommandIfNotExecuting();
}

void FSMBlueprintDebuggerCommands::OnCancelPie()
{
	ResetCommandIfNotExecuting();
	
	if (USMBlueprintDebugController::IsInitialized())
	{
		USMBlueprintDebugController::GetInstance()->CancelAndCleanup();
	}
}
