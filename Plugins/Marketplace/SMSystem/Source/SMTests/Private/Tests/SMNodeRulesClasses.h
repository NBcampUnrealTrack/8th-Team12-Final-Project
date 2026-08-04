// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMStateInstance.h"
#include "SMStateMachineInstance.h"
#include "SMTransitionInstance.h"

#include "SMNodeRulesClasses.generated.h"

UCLASS(Blueprintable)
class USMTestNodeRules_PreviousState : public USMStateInstance
{
	GENERATED_BODY()
};

UCLASS(Blueprintable)
class USMTestNodeRules_NextState : public USMStateInstance
{
	GENERATED_BODY()
};

UCLASS(Blueprintable)
class USMTestNodeRules_StateMachineOwner : public USMStateMachineInstance
{
	GENERATED_BODY()
};

UCLASS(Blueprintable)
class USMTestNodeRules_MainState : public USMStateInstance
{
	GENERATED_BODY()

public:
	FSMStateConnectionValidator& GetConnectionValidatorEdit() { return ConnectionRules; }
};

UCLASS(Blueprintable)
class USMTestNodeRules_Transition : public USMTransitionInstance
{
	GENERATED_BODY()

public:
	FSMTransitionConnectionValidator& GetConnectionValidatorEdit() { return ConnectionRules; }
};
