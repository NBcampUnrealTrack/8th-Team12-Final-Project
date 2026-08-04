// Copyright Recursoft LLC. All Rights Reserved.

#include "SMConduit.h"

#include "SMConduitInstance.h"
#include "SMUtils.h"
#include "ExposedFunctions/SMExposedFunctionDefines.h"

#define LOGICDRIVER_FUNCTION_HANDLER_TYPE FSMConduit_FunctionHandlers
// Create a local variable ref to our runtime data.
#define USING_NODE_RUNTIME_DATA() const TSharedPtr<FSMConduitRuntimeData>& ConduitRuntimeData = GetRuntimeDataAs<FSMConduitRuntimeData>();
// Access runtime data directly.
#define GET_NODE_RUNTIME_DATA() GetRuntimeDataAs<FSMConduitRuntimeData>()

FSMConduit::FSMConduit() : Super(), bCanEnterTransition(false), bCanEvaluate(true), bEvalWithTransitions(false),
                           ConditionalEvaluationType()
{
}

void FSMConduit::Initialize(USMInstance* Instance)
{
	Super::Initialize(Instance);
}

void FSMConduit::InitializeFunctionHandlers()
{
	INITIALIZE_NODE_FUNCTION_HANDLER();
}

void FSMConduit::InitializeGraphFunctions()
{
	FSMState_Base::InitializeGraphFunctions();

	INITIALIZE_EXPOSED_FUNCTIONS(CanEnterConduitGraphEvaluator);
	INITIALIZE_EXPOSED_FUNCTIONS(ConduitEnteredGraphEvaluator);
}

void FSMConduit::Reset()
{
	Super::Reset();
}

void FSMConduit::ExecuteInitializeNodes()
{
	if (IsInitializedForRun())
	{
		return;
	}

	if (bEvalWithTransitions)
	{
		TryResetVariables();
	}
	
	TryExecuteGraphProperties(ESMGraphPropertyEvalFlags::OnTransitionInitialize);

	if (NodeInstance)
	{
		NodeInstance->NativeInitialize();
	}
	
	Super::ExecuteInitializeNodes();
}

void FSMConduit::ExecuteShutdownNodes()
{
	Super::ExecuteShutdownNodes();

	if (NodeInstance)
	{
		NodeInstance->NativeShutdown();
	}
}

bool FSMConduit::IsNodeInstanceClassCompatible(UClass* NewNodeInstanceClass) const
{
	return NewNodeInstanceClass && NewNodeInstanceClass->IsChildOf<USMConduitInstance>();
}

UClass* FSMConduit::GetDefaultNodeInstanceClass() const
{
	return USMConduitInstance::StaticClass();
}

bool FSMConduit::StartState()
{
	const bool bResult = Super::StartState();

	EXECUTE_EXPOSED_FUNCTIONS(ConduitEnteredGraphEvaluator);

	if (USMConduitInstance* ConduitInstance = Cast<USMConduitInstance>(GetNodeInstance()))
	{
		ConduitInstance->OnStateBegin();
	}

	FirePostStartEvents();

	return bResult;
}

bool FSMConduit::UpdateState(float DeltaSeconds)
{
	const bool bResult = Super::UpdateState(DeltaSeconds);

	if (USMConduitInstance* ConduitInstance = Cast<USMConduitInstance>(GetNodeInstance()))
	{
		ConduitInstance->OnStateUpdate(DeltaSeconds);
	}

	return bResult;
}

bool FSMConduit::EndState(float DeltaSeconds, const FSMTransition* TransitionToTake)
{
	const bool bResult = Super::EndState(DeltaSeconds, TransitionToTake);

	if (USMConduitInstance* ConduitInstance = Cast<USMConduitInstance>(GetNodeInstance()))
	{
		ConduitInstance->OnStateEnd();
	}

	ShutdownTransitions();
	
	return bResult;
}

bool FSMConduit::GetValidTransition(TArray<TArray<FSMTransition*>>& OutTransitions)
{
	USING_NODE_RUNTIME_DATA();
	
	if (ConduitRuntimeData->bCheckedForTransitions || !bCanEvaluate)
	{
		return false;
	}

	ConduitRuntimeData->bIsEvaluating = true;
#if WITH_EDITORONLY_DATA
	ConduitRuntimeData->bWasEvaluating = true; // Will be set to false from the editor.
#endif
	
	// First check that the conduit passes.
	TryExecuteGraphProperties(ESMGraphPropertyEvalFlags::OnTransitionEval);
	PrepareGraphExecution();
	
	if (ConditionalEvaluationType == ESMConditionalEvaluationType::SM_AlwaysTrue)
	{
		// Skip BP graph eval if not needed.
		bCanEnterTransition = true;
	}
	else if (ConditionalEvaluationType == ESMConditionalEvaluationType::SM_NodeInstance)
	{
		bCanEnterTransition = CastChecked<USMConduitInstance>(GetOrCreateNodeInstance())->CanEnterTransition();
	}
	else
	{
		EXECUTE_EXPOSED_FUNCTIONS(CanEnterConduitGraphEvaluator);
	}

	ConduitRuntimeData->bIsEvaluating = false;
	
	if (!bCanEnterTransition)
	{
		return false;
	}

	ConduitRuntimeData->bCheckedForTransitions = true;
	
	// Passes, find the best transition.
	const bool bResult = Super::GetValidTransition(OutTransitions);
	
	ConduitRuntimeData->bCheckedForTransitions = false;
	return bResult;
}

void FSMConduit::EnterConduitWithTransition()
{
	if (IsConfiguredAsTransition())
	{
		SetActive(true);
#if WITH_EDITOR
		TriggerDebugBreakpoint();
#endif
		EXECUTE_EXPOSED_FUNCTIONS(ConduitEnteredGraphEvaluator);
		SetActive(false);
	}
}

#undef GET_NODE_RUNTIME_DATA
#undef USING_NODE_RUNTIME_DATA
#undef LOGICDRIVER_FUNCTION_HANDLER_TYPE
