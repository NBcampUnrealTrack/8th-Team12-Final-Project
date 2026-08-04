// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"

#if WITH_EDITOR

/** 
 * Communication with the editor module for debugging blueprints.
 * This hooks debug events when executing graphs.
 * 
 * FDebugOnScope should be placed within the scope of a graph execution to trigger debug events.
 */
class FSMBlueprintDebugEditorBridge
{
public:
	friend class USMBlueprintDebugController;
	
	SMSYSTEM_API static FSMBlueprintDebugEditorBridge& Get();

	FSMBlueprintDebugEditorBridge(const FSMBlueprintDebugEditorBridge&) = delete;
	FSMBlueprintDebugEditorBridge& operator=(const FSMBlueprintDebugEditorBridge&) = delete;

	/**
	 * Facilitates debug event triggering within a scoped lifecycle.
	 * Debug start and end events are invoked on creation and destruction of the instance, respectively.
	 */
	struct FDebugOnScope
	{
		FDebugOnScope();
		~FDebugOnScope();
	};
	
private:
	FSMBlueprintDebugEditorBridge() = default;

	FSimpleDelegate OnDebugScopeStartEvent;
	FSimpleDelegate OnDebugScopeEndEvent;
};

#endif