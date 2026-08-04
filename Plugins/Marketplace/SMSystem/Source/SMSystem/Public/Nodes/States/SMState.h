// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMNode_Base.h"

#include "SMState.generated.h"

class USMInstance;
struct FSMTransition;
struct FSMStateInfo;

struct FSMStateBaseRuntimeData : FSMNodeRuntimeData
{
	FSMStateBaseRuntimeData() : NextTransition(nullptr), PreviousActiveState(nullptr),
	                            PreviousActiveTransition(nullptr),
	                            bReenteredByParallelState(false),
	                            bCanExecuteLogic(true), bIsStateEnding(false)
	{
	}
	
	virtual TSharedPtr<FSMNodeRuntimeData> Clone() const override
	{
		return MakeShared<FSMStateBaseRuntimeData>(*this);
	}
	
	friend struct FSMState_Base;
	friend struct FSMState;
	
private:
	TArray<FSMTransition*> IncomingTransitions;
	TArray<FSMTransition*> OutgoingTransitions;
	const FSMTransition* NextTransition;
	
	/** The last active state before this state. Resets on entry. */
	FSMState_Base* PreviousActiveState;

	/** The last active transition before this state. Resets on entry. */
	FSMTransition* PreviousActiveTransition;

#if WITH_EDITORONLY_DATA
	double StartCycle = 0.f;
#endif

protected:
	/** UTC time the state started. */
	FDateTime StartTime;

	/** UTC time the state ended. */
	FDateTime EndTime;
	
	/** True only when already active and entered from a parallel state. */
	uint8 bReenteredByParallelState: 1;

	/** If this state machine can execute state logic. */
	uint8 bCanExecuteLogic: 1;

	/** True while the state is ending and graph execution is occurring. Prevents restarting this state when it triggers transitions while ending. */
	uint8 bIsStateEnding: 1;
};


/**
 * The base class for all state typed nodes. This should never be instantiated by itself but inherited by children.
 */
USTRUCT(BlueprintInternalUseOnly)
struct SMSYSTEM_API FSMState_Base : public FSMNode_Base
{
	GENERATED_USTRUCT_BODY()

public:
	/** Entry node to state machine. */
	UPROPERTY()
	uint16 bIsRootNode: 1;

	/** Always call state update at least once before ending. */
	UPROPERTY()
	uint16 bAlwaysUpdate: 1;

	/** Allows transitions to be evaluated in the same tick as StartState. */
	UPROPERTY()
	uint16 bEvalTransitionsOnStart: 1;

	/** Prevents conditional transitions for this state from being evaluated on Tick. */
	UPROPERTY()
	uint16 bDisableTickTransitionEvaluation: 1;

	/** If the state should remain active even after a transition is taken from this state. */
	UPROPERTY()
	uint16 bStayActiveOnStateChange: 1;

	/** If this state can be reentered from a parallel state if this state is already active. */
	UPROPERTY()
	uint16 bAllowParallelReentry: 1;

	/** If this state can be considered an end state. */
	UPROPERTY()
	uint16 bCanBeEndState: 1;
	
public:
	virtual void UpdateReadStates() override;
	virtual void ResetReadStates();

public:
	FSMState_Base();

	// FSMNode_Base
	virtual void Initialize(USMInstance* Instance) override;
	virtual void InitializeGraphFunctions() override;
	virtual void Reset() override;
	virtual bool IsNodeInstanceClassCompatible(UClass* NewNodeInstanceClass) const override;
	virtual UClass* GetDefaultNodeInstanceClass() const override;
	virtual void ExecuteInitializeNodes() override;
	virtual void ExecuteShutdownNodes() override;
	
protected:
	virtual TSharedPtr<FSMNodeRuntimeData> CreateRuntimeData() const override
	{
		return MakeShared<FSMStateBaseRuntimeData>();
	}
	// ~ FSMNode_Base
public:
	/** The transitions leading out from this state, sorted lowest to highest priority. */
	const TArray<FSMTransition*>& GetOutgoingTransitions() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->OutgoingTransitions; }

	/** The transitions leading to this state. */
	const TArray<FSMTransition*>& GetIncomingTransitions() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->IncomingTransitions; }
	
	/** Returns all connected transitions from this state, including ones connected to transition conduits. */
	void GetAllTransitionChains(TArray<FSMTransition*>& OutTransitions) const;
	
	/** Sets the state as active and begins execution. */
	virtual bool StartState();
	
	/** Runs the update execution. */
	virtual bool UpdateState(float DeltaSeconds);
	
	/** Runs the end state execution. Transition to take is so the state knows where it is going only. */
	virtual bool EndState(float DeltaSeconds, const FSMTransition* TransitionToTake = nullptr);

	/** Called when the blueprint owning this node is started. */
	virtual void OnStartedByInstance(USMInstance* Instance) override;

	/** Called when the blueprint owning this node has stopped. */
	virtual void OnStoppedByInstance(USMInstance* Instance) override;
	
	/**
	 * Runs through the transitions executing their graphs until a result is found.
	 * Builds an ordered list of transitions to take.
	 * @param OutTransitions Found transitions. 2D Array of valid paths. If the total size is more than one that means these transitions are leading to parallel states.
	 * if each path is more than one that means there are transition conduits involved.
	 * @return True if a valid path is found, false otherwise.
	 */
	virtual bool GetValidTransition(TArray<TArray<FSMTransition*>>& OutTransitions);

	/** If the state itself is an end state. */
	virtual bool IsEndState() const;

	/** @deprecated Use FSMStateMachine::IsStateMachineInEndState() instead. */
	UE_DEPRECATED(5.1, "This function is deprecated. Use FSMStateMachine::IsStateMachineInEndState().")
	virtual bool IsInEndState() const;

	/** Has updated at least once. */
	virtual bool HasUpdated() const;

	/** Easy way to check if this state struct is a state machine. */
	virtual bool IsStateMachine() const { return false; }

	/** Easy way to check if this state struct is a conduit. */
	virtual bool IsConduit() const { return false; }

	/** If this node is an initial entry point. */
	bool IsRootNode() const { return bIsRootNode; }
	
	/** Current time in seconds this state has been active. */
	float GetActiveTime() const { return TimeInState; }

	/** Set if this state is allowed to execute its logic. */
	void SetCanExecuteLogic(bool bValue);

	/** If this state is allowed to execute logic. */
	bool CanExecuteLogic() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->bCanExecuteLogic; }

	/** Check if the state can execute its graph properties. */
	virtual bool CanExecuteGraphProperties(ESMGraphPropertyEvalFlags OnEvent, const USMStateInstance_Base* ForTemplate, ESMGraphPropertyDirection InDirection) const override;
	
	/** If this state is allowed to evaluate its transitions on tick. This can return true even when tick evaluation is false in the event
	 * an outgoing transition has just completed from an event. */
	bool CanEvaluateTransitionsOnTick() const;

	/** Sort incoming and outgoing transitions by priority. */
	void SortTransitions();

	/** The transition this state will be taking. */
	void SetTransitionToTake(const FSMTransition* Transition);
	
	/** If set this is the transition the state will take. Generally only valid when EndState is called and if this state is not an end state. */
	const FSMTransition* GetTransitionToTake() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->NextTransition; }

	/** Record the previous active state before this one. */
	void SetPreviousActiveState(FSMState_Base* InPreviousState);

	/** Record the previous active transition taken to this state. */
	void SetPreviousActiveTransition(FSMTransition* InPreviousTransition);
	
	/** The last state entered previous to this state. */
	FSMState_Base* GetPreviousActiveState() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->PreviousActiveState; }

	/** The last transition taken to this state. */
	FSMTransition* GetPreviousActiveTransition() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->PreviousActiveTransition; }
	
	/** This state is being reentered from a parallel state. */
	void NotifyOfParallelReentry(bool bValue = true);

	/** This state has just been entered from a parallel state while already active. May be true only for OnStateBegin. */
	bool HasBeenReenteredFromParallelState() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->bReenteredByParallelState; }

	/** True while the state is ending and graph execution is occurring. Prevents restarting this state when it triggers transitions while ending. */
	bool IsStateEnding() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->bIsStateEnding; }

	/** UTC time the state started. */
	const FDateTime& GetStartTime() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->StartTime; }

	/** UTC time the state ended. */
	const FDateTime& GetEndTime() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->EndTime; }
	
	/** Set the local start time. */
	virtual void SetStartTime(const FDateTime& InStartTime);

	/** Set the local end time. */
	virtual void SetEndTime(const FDateTime& InEndTime);
	
#if WITH_EDITORONLY_DATA
public:
	/** High resolution timer for when this state started. */
	double GetStartCycle() const { return GetRuntimeDataAs<FSMStateBaseRuntimeData>()->StartCycle; }
#endif

#if WITH_EDITOR
public:
	virtual void ResetGeneratedValues() override;
#endif

protected:
	friend struct FSMTransition;
	void AddOutgoingTransition(FSMTransition* Transition);
	void AddIncomingTransition(FSMTransition* Transition);
	
	/** Helpers to call any special transition logic. */
	void InitializeTransitions();
	void ShutdownTransitions();

	/** Call the owning instance letting it know this state has started. */
	virtual void NotifyInstanceStateHasStarted();

	/** Fire all instance pre start events. */
	virtual void FirePreStartEvents();

	/** Fire all instance post start events. */
	virtual void FirePostStartEvents();
};

/**
 * State nodes that can execute Blueprint logic.
 */
USTRUCT(BlueprintInternalUseOnly)
struct SMSYSTEM_API FSMState : public FSMState_Base
{
	GENERATED_USTRUCT_BODY()

	FSMState();

	// FSMNode_Base
	virtual void Initialize(USMInstance* Instance) override;
protected:
	virtual void InitializeFunctionHandlers() override;
public:
	virtual void InitializeGraphFunctions() override;
	virtual void Reset() override;
	virtual void ExecuteInitializeNodes() override;
	virtual void ExecuteShutdownNodes() override;
	// ~ FSMNode_Base

	// FSMState_Base
	virtual bool StartState() override;
	virtual bool UpdateState(float DeltaSeconds) override;
	virtual bool EndState(float DeltaSeconds, const FSMTransition* TransitionToTake = nullptr) override;

	virtual bool TryExecuteGraphProperties(ESMGraphPropertyEvalFlags OnEvent, ESMGraphPropertyDirection InDirection) override;

	virtual void OnStartedByInstance(USMInstance* Instance) override;
	virtual void OnStoppedByInstance(USMInstance* Instance) override;
	
	virtual bool GetValidTransition(TArray<TArray<FSMTransition*>>& OutTransitions) override;
	// ~FSMState_Base
};
