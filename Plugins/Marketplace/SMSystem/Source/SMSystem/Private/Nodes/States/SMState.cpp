// Copyright Recursoft LLC. All Rights Reserved.

#include "SMState.h"

#include "ExposedFunctions/SMExposedFunctionDefines.h"
#include "SMDefines.h"
#include "SMLogging.h"
#include "SMPropertyUtils.h"
#include "SMStateInstance.h"
#include "SMTransition.h"
#include "SMUtils.h"

#if WITH_EDITOR
#include "Blueprints/SMBlueprintDebugEditorBridge.h"
#endif

#define LOGICDRIVER_FUNCTION_HANDLER_TYPE FSMState_FunctionHandlers
// Create a local variable ref to our runtime data.
#define USING_NODE_RUNTIME_DATA() const TSharedPtr<FSMStateBaseRuntimeData>& StateRuntimeData = GetRuntimeDataAs<FSMStateBaseRuntimeData>();
// Access runtime data directly.
#define GET_NODE_RUNTIME_DATA() GetRuntimeDataAs<FSMStateBaseRuntimeData>()

void FSMState_Base::UpdateReadStates()
{
	Super::UpdateReadStates();
	bIsInEndState = IsEndState();
	bHasUpdated = HasUpdated();
	TimeInState = GetActiveTime();
}

void FSMState_Base::ResetReadStates()
{
	bHasUpdated = false;
	bIsInEndState = false;
	TimeInState = 0.f;
	SetServerTimeInState(SM_ACTIVE_TIME_NOT_SET);
}

FSMState_Base::FSMState_Base() : Super(), bIsRootNode(false), bAlwaysUpdate(false), bEvalTransitionsOnStart(false),
                                 bDisableTickTransitionEvaluation(false), bStayActiveOnStateChange(false),
                                 bAllowParallelReentry(false),
                                 bCanBeEndState(true)
{
}

void FSMState_Base::Initialize(USMInstance* Instance)
{
	Super::Initialize(Instance);

	ResetReadStates();
	SortTransitions();
}

void FSMState_Base::InitializeGraphFunctions()
{
	FSMNode_Base::InitializeGraphFunctions();
}

void FSMState_Base::Reset()
{
	Super::Reset();
	ResetReadStates();
}

bool FSMState_Base::IsNodeInstanceClassCompatible(UClass* NewNodeInstanceClass) const
{
	return NewNodeInstanceClass && NewNodeInstanceClass->IsChildOf<USMStateInstance>();
}

UClass* FSMState_Base::GetDefaultNodeInstanceClass() const
{
	return USMStateInstance::StaticClass();
}

void FSMState_Base::ExecuteInitializeNodes()
{
	Super::ExecuteInitializeNodes();
}

void FSMState_Base::ExecuteShutdownNodes()
{
	// Output variables should execute after a state has run its On State End logic.
	TryExecuteGraphProperties(ESMGraphPropertyEvalFlags::OnEnd);
	
	FSMNode_Base::ExecuteShutdownNodes();
}

void FSMState_Base::GetAllTransitionChains(TArray<FSMTransition*>& OutTransitions) const
{
	USING_NODE_RUNTIME_DATA();
	for (const FSMTransition* Transition : StateRuntimeData->OutgoingTransitions)
	{
		Transition->GetConnectedTransitions(OutTransitions);
	}
}

bool FSMState_Base::StartState()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SMState_Base::StartState"), STAT_SMState_Start, STATGROUP_LogicDriver);

	USING_NODE_RUNTIME_DATA();
	
	StateRuntimeData->NextTransition = nullptr;

	if (IsActive() && (!HasBeenReenteredFromParallelState() || !bAllowParallelReentry))
	{
		return false;
	}

	SetStartTime(FDateTime::UtcNow());

	ResetReadStates();

#if WITH_EDITORONLY_DATA
	StateRuntimeData->StartCycle = FPlatformTime::Cycles64();
#endif

	TryResetVariables();

	TryExecuteGraphProperties(ESMGraphPropertyEvalFlags::OnStart);

	SetActive(true);

	FirePreStartEvents();

	NotifyInstanceStateHasStarted();

	InitializeTransitions();
	
#if WITH_EDITOR
	TriggerDebugBreakpoint();
#endif

	return true;
}

bool FSMState_Base::UpdateState(float DeltaSeconds)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SMState_Base::UpdateState"), STAT_SMState_Update, STATGROUP_LogicDriver);

	if (!IsActive())
	{
		return false;
	}

	TimeInState += DeltaSeconds;
	UpdateReadStates();

	TryExecuteGraphProperties(ESMGraphPropertyEvalFlags::OnUpdate);

	if (USMStateInstance_Base* StateInstance = Cast<USMStateInstance_Base>(NodeInstance))
	{
		StateInstance->OnStateUpdateEvent.Broadcast(StateInstance, DeltaSeconds);
	}

	return bHasUpdated = true;
}

bool FSMState_Base::EndState(float DeltaSeconds, const FSMTransition* TransitionToTake)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SMState_Base::EndState"), STAT_SMState_End, STATGROUP_LogicDriver);

	if (!IsActive())
	{
		return false;
	}

	SetEndTime(FDateTime::UtcNow());

	SetTransitionToTake(TransitionToTake);

	if (bAlwaysUpdate && !HasUpdated())
	{
		UpdateState(DeltaSeconds);
	}
	else
	{
		// Record the extra time for accuracy.
		TimeInState += DeltaSeconds;
	}

	UpdateReadStates();

	if (USMStateInstance_Base* StateInstance = Cast<USMStateInstance_Base>(NodeInstance))
	{
		StateInstance->OnStateEndEvent.Broadcast(StateInstance);
	}

	SetActive(false);

	return true;
}

void FSMState_Base::OnStartedByInstance(USMInstance* Instance)
{
	// Only execute if allowed and if it's this owning instance starting it.
	// This means reference nodes won't initialize until their owning blueprint is started.
	if (CanExecuteLogic() && Instance == GetOwningInstance())
	{
		UpdateReadStates();

		TryExecuteGraphProperties(ESMGraphPropertyEvalFlags::OnRootStateMachineStart);

		EXECUTE_EXPOSED_FUNCTIONS(OnRootStateMachineStartedGraphEvaluator);
	}
}

void FSMState_Base::OnStoppedByInstance(USMInstance* Instance)
{
	// Only execute if allowed and if it's this owning instance.
	if (CanExecuteLogic() && Instance == GetOwningInstance())
	{
		UpdateReadStates();

		TryExecuteGraphProperties(ESMGraphPropertyEvalFlags::OnRootStateMachineStop);

		EXECUTE_EXPOSED_FUNCTIONS(OnRootStateMachineStoppedGraphEvaluator);
	}
}

bool FSMState_Base::GetValidTransition(TArray<TArray<FSMTransition*>>& OutTransitions)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SMState_Base::GetValidTransition"), STAT_SMState_GetValidTransition,
	                            STATGROUP_LogicDriver);

	USING_NODE_RUNTIME_DATA();
	for (FSMTransition* Transition : StateRuntimeData->OutgoingTransitions)
	{
		TArray<FSMTransition*> Chain;
		if (Transition->CanTransition(Chain))
		{
			OutTransitions.Add(MoveTemp(Chain));

			// Check if blocking or not.
			if (IsConduit() || !Transition->bRunParallel)
			{
				return true;
			}
		}
	}

	return OutTransitions.Num() > 0;
}

bool FSMState_Base::IsEndState() const
{
	if (!bCanBeEndState)
	{
		return false;
	}

	USING_NODE_RUNTIME_DATA();
	for (const FSMTransition* Transition : StateRuntimeData->OutgoingTransitions)
	{
		// Look for at least one valid transition.
		if (!Transition->bAlwaysFalse)
		{
			return false;
		}
	}

	return true;
}

bool FSMState_Base::IsInEndState() const
{
	return IsEndState();
}

bool FSMState_Base::HasUpdated() const
{
	return bHasUpdated;
}

void FSMState_Base::SetCanExecuteLogic(bool bValue)
{
	GET_NODE_RUNTIME_DATA()->bCanExecuteLogic = bValue;
}

bool FSMState_Base::CanExecuteGraphProperties(ESMGraphPropertyEvalFlags OnEvent, const USMStateInstance_Base* ForTemplate, ESMGraphPropertyDirection InDirection) const
{
	if (ForTemplate != nullptr)
	{
		if (!ForTemplate->bAutoEvalExposedProperties)
		{
			return false;
		}

		switch (InDirection)
		{
		case ESMGraphPropertyDirection::Input:
			return LD::PropertyUtils::DoEvalFlagsContainFlag(ForTemplate->InputPropertyEvalFlags, OnEvent);
		case ESMGraphPropertyDirection::Output:
			return LD::PropertyUtils::DoEvalFlagsContainFlag(ForTemplate->OutputPropertyEvalFlags, OnEvent);
		case ESMGraphPropertyDirection::Both:
			return LD::PropertyUtils::DoEvalFlagsContainFlag(ForTemplate->InputPropertyEvalFlags, OnEvent)
			&& LD::PropertyUtils::DoEvalFlagsContainFlag(ForTemplate->OutputPropertyEvalFlags, OnEvent);
		}

		checkNoEntry();
	}

	return false;
}

bool FSMState_Base::CanEvaluateTransitionsOnTick() const
{
	if (bDisableTickTransitionEvaluation)
	{
		USING_NODE_RUNTIME_DATA();
		/* Check if any immediate outgoing transition has just completed from an event before returning false. */
		for (FSMTransition* Transition : StateRuntimeData->OutgoingTransitions)
		{
			if (Transition->CanTransitionFromEvent())
			{
				return true;
			}
		}
	}

	return !bDisableTickTransitionEvaluation;
}

void FSMState_Base::SortTransitions()
{
	USING_NODE_RUNTIME_DATA();
	
	StateRuntimeData->OutgoingTransitions.Sort([](const FSMTransition& lhs, const FSMTransition& rhs)
	{
		return lhs.Priority < rhs.Priority;
	});

	StateRuntimeData->IncomingTransitions.Sort([](const FSMTransition& lhs, const FSMTransition& rhs)
	{
		return lhs.Priority < rhs.Priority;
	});
}

void FSMState_Base::SetTransitionToTake(const FSMTransition* Transition)
{
	USING_NODE_RUNTIME_DATA();
	
	StateRuntimeData->NextTransition = Transition;

	if (StateRuntimeData->NextTransition)
	{
		SetServerTimeInState(StateRuntimeData->NextTransition->GetServerTimeInState());
	}
}

void FSMState_Base::SetPreviousActiveState(FSMState_Base* InPreviousState)
{
	GET_NODE_RUNTIME_DATA()->PreviousActiveState = InPreviousState;
}

void FSMState_Base::SetPreviousActiveTransition(FSMTransition* InPreviousTransition)
{
	GET_NODE_RUNTIME_DATA()->PreviousActiveTransition = InPreviousTransition;
}

void FSMState_Base::NotifyOfParallelReentry(bool bValue)
{
	GET_NODE_RUNTIME_DATA()->bReenteredByParallelState = bValue;
}

void FSMState_Base::SetStartTime(const FDateTime& InStartTime)
{
	GET_NODE_RUNTIME_DATA()->StartTime = InStartTime;
}

void FSMState_Base::SetEndTime(const FDateTime& InEndTime)
{
	GET_NODE_RUNTIME_DATA()->EndTime = InEndTime;
}

#if WITH_EDITOR
void FSMState_Base::ResetGeneratedValues()
{
	FSMNode_Base::ResetGeneratedValues();

	USING_NODE_RUNTIME_DATA();
	StateRuntimeData->OutgoingTransitions.Empty();
	StateRuntimeData->IncomingTransitions.Empty();
}
#endif

void FSMState_Base::AddOutgoingTransition(FSMTransition* Transition)
{
	GET_NODE_RUNTIME_DATA()->OutgoingTransitions.AddUnique(Transition);
}

void FSMState_Base::AddIncomingTransition(FSMTransition* Transition)
{
	GET_NODE_RUNTIME_DATA()->IncomingTransitions.AddUnique(Transition);
}

void FSMState_Base::InitializeTransitions()
{
	ExecuteInitializeNodes();

	TArray<FSMTransition*> AllTransitions;
	GetAllTransitionChains(AllTransitions);

	for (FSMTransition* Transition : AllTransitions)
	{
		Transition->ExecuteInitializeNodes();
	}
}

void FSMState_Base::ShutdownTransitions()
{
	TArray<FSMTransition*> AllTransitions;
	GetAllTransitionChains(AllTransitions);

	for (FSMTransition* Transition : AllTransitions)
	{
		Transition->ExecuteShutdownNodes();
	}

	ExecuteShutdownNodes();
}

void FSMState_Base::NotifyInstanceStateHasStarted()
{
	if (USMInstance* Instance = GetOwningInstance())
	{
		Instance->NotifyStateStarted(*this);
	}
}

void FSMState_Base::FirePreStartEvents()
{
	if (USMStateInstance_Base* StateInstance = Cast<USMStateInstance_Base>(NodeInstance))
	{
		StateInstance->OnStateBeginEvent.Broadcast(StateInstance);
	}
}

void FSMState_Base::FirePostStartEvents()
{
	if (USMStateInstance_Base* StateInstance = Cast<USMStateInstance_Base>(NodeInstance))
	{
		StateInstance->OnPostStateBeginEvent.Broadcast(StateInstance);
	}
}

FSMState::FSMState() : Super()
{
}

void FSMState::Initialize(USMInstance* Instance)
{
	Super::Initialize(Instance);
}

void FSMState::InitializeFunctionHandlers()
{
	INITIALIZE_NODE_FUNCTION_HANDLER();
}

void FSMState::InitializeGraphFunctions()
{
	FSMState_Base::InitializeGraphFunctions();

	INITIALIZE_EXPOSED_FUNCTIONS(BeginStateGraphEvaluator);
	INITIALIZE_EXPOSED_FUNCTIONS(UpdateStateGraphEvaluator);
	INITIALIZE_EXPOSED_FUNCTIONS(EndStateGraphEvaluator);
}

void FSMState::Reset()
{
	Super::Reset();
}

void FSMState::ExecuteInitializeNodes()
{
	if (IsInitializedForRun())
	{
		return;
	}
	
	FSMState_Base::TryExecuteGraphProperties(ESMGraphPropertyEvalFlags::OnTransitionInitialize);

	if (NodeInstance)
	{
		NodeInstance->NativeInitialize();
	}

	Super::ExecuteInitializeNodes();

	for (USMNodeInstance* StackInstance : StackNodeInstances)
	{
		if (USMStateInstance* StateInstance = Cast<USMStateInstance>(StackInstance))
		{
			StateInstance->NativeInitialize();
			StateInstance->OnStateInitialized();
		}
	}
}

void FSMState::ExecuteShutdownNodes()
{
	Super::ExecuteShutdownNodes();

	if (NodeInstance)
	{
		NodeInstance->NativeShutdown();
	}

	for (USMNodeInstance* StackInstance : StackNodeInstances)
	{
		if (USMStateInstance* StateInstance = Cast<USMStateInstance>(StackInstance))
		{
			StateInstance->OnStateShutdown();
			StateInstance->NativeShutdown();
		}
	}
}

bool FSMState::StartState()
{
	if (!Super::StartState())
	{
		return false;
	}

	if (CanExecuteLogic())
	{
		PrepareGraphExecution();
		{
#if WITH_EDITOR
			FSMBlueprintDebugEditorBridge::FDebugOnScope DebugDataOnScope;
#endif
			EXECUTE_EXPOSED_FUNCTIONS(BeginStateGraphEvaluator);
		}
		
		for (USMNodeInstance* StackInstance : StackNodeInstances)
		{
			if (USMStateInstance* StateInstance = Cast<USMStateInstance>(StackInstance))
			{
				StateInstance->OnStateBeginEvent.Broadcast(StateInstance);
				StateInstance->OnStateBegin();
				StateInstance->OnPostStateBeginEvent.Broadcast(StateInstance);
			}
		}
	}

	FirePostStartEvents();

	return true;
}

bool FSMState::UpdateState(float DeltaSeconds)
{
	if (!Super::UpdateState(DeltaSeconds))
	{
		return false;
	}

	if (CanExecuteLogic())
	{
		EXECUTE_EXPOSED_FUNCTIONS(UpdateStateGraphEvaluator, (void*)&DeltaSeconds);

		for (USMNodeInstance* StackInstance : StackNodeInstances)
		{
			if (USMStateInstance* StateInstance = Cast<USMStateInstance>(StackInstance))
			{
				StateInstance->OnStateUpdateEvent.Broadcast(StateInstance, DeltaSeconds);
				StateInstance->OnStateUpdate(DeltaSeconds);
			}
		}
	}

	return true;
}

bool FSMState::EndState(float DeltaSeconds, const FSMTransition* TransitionToTake)
{
	if (!Super::EndState(DeltaSeconds, TransitionToTake))
	{
		return false;
	}

	if (CanExecuteLogic())
	{
		USING_NODE_RUNTIME_DATA();
		
		StateRuntimeData->bIsStateEnding = true;
		EXECUTE_EXPOSED_FUNCTIONS(EndStateGraphEvaluator);

		for (USMNodeInstance* StackInstance : StackNodeInstances)
		{
			if (USMStateInstance* StateInstance = Cast<USMStateInstance>(StackInstance))
			{
				StateInstance->OnStateEndEvent.Broadcast(StateInstance);
				StateInstance->OnStateEnd();
			}
		}
		StateRuntimeData->bIsStateEnding = false;
	}

	ShutdownTransitions();

	return true;
}

bool FSMState::TryExecuteGraphProperties(ESMGraphPropertyEvalFlags OnEvent, ESMGraphPropertyDirection InDirection)
{
	bool bResult = Super::TryExecuteGraphProperties(OnEvent, InDirection);

	for (TObjectPtr<USMNodeInstance>& StackInstance : StackNodeInstances)
	{
		if (USMStateInstance* StateInstance = Cast<USMStateInstance>(StackInstance))
		{
			if (CanExecuteGraphProperties(OnEvent, StateInstance, InDirection))
			{
				FFilterGraphPropertyArgs Args;
				Args.NodeInstance = StateInstance;
				Args.TemplateGuid = &StateInstance->GetTemplateGuid();
				Args.IncludeFlags = LD::PropertyUtils::ConvertDirectionToFlags(InDirection);
				ExecuteGraphProperties(MoveTemp(Args));
				bResult = true;
			}
		}
	}

	return bResult;
}

void FSMState::OnStartedByInstance(USMInstance* Instance)
{
	Super::OnStartedByInstance(Instance);
	for (USMNodeInstance* StackInstance : StackNodeInstances)
	{
		if (USMStateInstance* StateInstance = Cast<USMStateInstance>(StackInstance))
		{
			StateInstance->OnRootStateMachineStart();
		}
	}
}

void FSMState::OnStoppedByInstance(USMInstance* Instance)
{
	Super::OnStoppedByInstance(Instance);
	for (USMNodeInstance* StackInstance : StackNodeInstances)
	{
		if (USMStateInstance* StateInstance = Cast<USMStateInstance>(StackInstance))
		{
			StateInstance->OnRootStateMachineStop();
		}
	}
}

bool FSMState::GetValidTransition(TArray<TArray<FSMTransition*>>& OutTransitions)
{
	// Transitions may need updated variables (mostly for output variables). Can't run in parent call
	// because conduits have to run it at a specific time, and we don't want to double fire this on conduits each
	// evaluation.
	FSMState_Base::TryExecuteGraphProperties(ESMGraphPropertyEvalFlags::OnTransitionEval);
	return FSMState_Base::GetValidTransition(OutTransitions);
}

#undef GET_NODE_RUNTIME_DATA
#undef USING_NODE_RUNTIME_DATA
#undef LOGICDRIVER_FUNCTION_HANDLER_TYPE
