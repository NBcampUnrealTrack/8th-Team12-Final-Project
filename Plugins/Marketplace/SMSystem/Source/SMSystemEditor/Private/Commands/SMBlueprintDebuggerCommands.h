// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Debug/SMDebugSteps.h"

#include "Containers/Array.h"
#include "Templates/SharedPointer.h"

class FUICommandInfo;
class FUICommandList;
struct FUIAction;

/**
 * Hooks for UE debugging commands we intercept and run our own logic on.
 * This is necessary because the FKismetDebugUtilities doesn't expose all necessary data we need to accurately
 * determine specific commands, like 'step into' vs. 'step over'.
 * 
 * We could map actions localized to our toolkit commands, but then the level toolbar version, and importantly, the hotkey,
 * wouldn't be recognized. Hooking the blueprint debugger commands at a global level is the most complete solution and
 * offers the best experience. It is highly unlikely these commands are being hooked by other tools.
 * 
 * Ideally, the engine would broadcast events for each type of debugging command executed.
 */
class FSMBlueprintDebuggerCommands
{
public:
	FSMBlueprintDebuggerCommands();
	~FSMBlueprintDebuggerCommands();

	/** Map a UE command to our own so we can run additional logic, then call the original command. */
	void MapGlobalAction(const TSharedPtr<FUICommandInfo>& InUECommand, LD::Editor::Debug::EDebugCommandType InDebugType);
	/** Unmap and restore the original action. */
	void UnmapGlobalAction(const TSharedPtr<FUICommandInfo>& InUECommand);
	
	/** Replace all UE actions we need with our own. This will still call the original command. */
	void HookAllUEActions();
	/** Remove our hooks and restore the original actions. */
	void RestoreAllUEActions();

private:
	void CommandExecute(TSharedPtr<FUIAction> OriginalAction, LD::Editor::Debug::EDebugCommandType InDebugType);
	bool CanExecuteCommand(TSharedPtr<FUIAction> OriginalAction) const;
	bool IsCommandVisible(TSharedPtr<FUIAction> OriginalAction) const;

	/** Reset the command executing. */
	void ResetCommand();

	/** Reset our commands if we aren't executing. */
	void ResetCommandIfNotExecuting();
	
	void OnPieSingleStep(bool bIsSimulating);
	void OnCancelPie();
	
private:
	/** The original actions provided by UE which we need to execute or restore. */
	TMap<TSharedPtr<FUICommandInfo>, TSharedPtr<FUIAction>> OriginalUECommandActionMap;
	
	/** If we are currently executing a command. */
	bool bIsExecutingCommand = false;
};
