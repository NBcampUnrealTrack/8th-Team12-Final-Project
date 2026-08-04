// Copyright Recursoft LLC. All Rights Reserved.

#include "Blueprints/SMBlueprintDebugEditorBridge.h"

#if WITH_EDITOR

FSMBlueprintDebugEditorBridge& FSMBlueprintDebugEditorBridge::Get()
{
	static FSMBlueprintDebugEditorBridge Instance;
	return Instance;
}

FSMBlueprintDebugEditorBridge::FDebugOnScope::FDebugOnScope()
{
	Get().OnDebugScopeStartEvent.ExecuteIfBound();
}

FSMBlueprintDebugEditorBridge::FDebugOnScope::~FDebugOnScope()
{
	Get().OnDebugScopeEndEvent.ExecuteIfBound();
}

#endif