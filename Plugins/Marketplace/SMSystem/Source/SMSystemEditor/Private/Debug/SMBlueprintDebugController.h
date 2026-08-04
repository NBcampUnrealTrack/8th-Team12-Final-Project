// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "SMDebugSteps.h"
#include "UObject/StrongObjectPtrTemplates.h"

#include "SMBlueprintDebugController.generated.h"

class USMInstance;
class USMBlueprint;
class USMGraphNode_StateNodeBase;
class USMGraph;
class UEdGraphNode;
struct FBlueprintBreakpoint;

/**
 * Controller responsible for managing the debugging process of state machine blueprints.
 * This class provides functionality to help with blueprint-specific debugging commands, such as step-over.
 */
UCLASS()
class USMBlueprintDebugController : public UObject
{
	GENERATED_BODY()

private:
	USMBlueprintDebugController();
	virtual ~USMBlueprintDebugController() override;

public:
	/** Return the singleton debug controller instance. */
	static USMBlueprintDebugController* GetInstance();
	
	/** Destroy the singleton and cleanup any resources. */
	static void Shutdown();
	
	/** If the controller is initialized. */
	static bool IsInitialized() { return BlueprintDebugController.IsValid(); }
	
	/** 
	 * Inform the controller we are starting execution of a new command.
	 * @return True if the original command should execute, handled by the caller.
	 */
	bool NotifyNewCommandStarting(LD::Editor::Debug::EDebugCommandType InCommandType);
	
	/** Resets command executing. */
	void ResetCommand();
	
	/** Cancel all execution and cleanup. */
	void CancelAndCleanup();

	/** Retrieve the current debug stack depth. This is the number of frames in the script call stack that are related to state machine execution. */
	uint32 GetDebugStackDepth() const { return StateMachineDebugStackDepth; }
	
	/**
	 * Determines if a Logic Driver state machine instance is currently prepared for debugging.
	 * This checks if a state machine has executed appropriate debugging scripts and that a state machine instance
	 * is present in the call stack.
	 *
	 * @param InCommandType The type of command being executed.
	 *
	 * @return true if Logic Driver is ready for debugging; false otherwise.
	 */
	bool IsLogicDriverReadyToDebug(LD::Editor::Debug::EDebugCommandType InCommandType) const;
	
	/**
	 * Determines if a Logic Driver state machine is present in the current script call stack.
	 * This function can be used to detect if a state machine instance is actively being executed as part of the
	 * current blueprint scripting context.
	 *
	 * @return true if a Logic Driver state machine is found in the script call stack; false otherwise.
	 */
	static bool IsLogicDriverInStack();
	
	/** Checks if a blueprint breakpoint is considered temporary. */
	SMSYSTEMEDITOR_API static bool IsBreakpointTemporary(const FBlueprintBreakpoint& InBreakpoint);

	/** Remove any temporary breakpoints we've inserted. For example, 'step-over` inserts breakpoints for this run. */
	SMSYSTEMEDITOR_API static void CleanupTemporaryBreakpoints(const USMBlueprint* Blueprint);
	static void CleanupTemporaryBreakpoints(const USMInstance* Instance);
	
	/** Resume the play session. */
	static void ResumeSession();
	
private:
	void RequestStepInto(const FBlueprintContextTracker& BlueprintContext, const UObject* ContextObject, const UFunction* ContextFunction);
	void RequestStepOver();
	void RequestStepOut();
	
	void BindKismetDebugDelegates();
	void UnbindKismetDebugDelegates();
	
	void OnEnterScriptExecution(const FBlueprintContextTracker& BlueprintContext, const UObject* ContextObject, const UFunction* ContextFunction);
	void OnScriptException(const UObject* ActiveObject, const FFrame& StackFrame, const FBlueprintExceptionInfo& Info);
	
	void OnDebugScopeStart();
	void OnDebugScopeEnd();
	
	/** Notify we are tracking a blueprint. */
	void TrackBlueprint(USMBlueprint* InBlueprint);
	
	/** Make sure all temporary breakpoints are removed. */
	void CleanupTemporaryBreakpoints();
	
	/** Record a state node we have break pointed on. */
	void RecordBreakpointStateNode(const USMGraphNode_StateNodeBase* StateNode);
	
	/** Apply temporary breakpoints to all output states of the given state node. */
	void ApplyTemporaryBreakpointsToOutputStates(const USMGraphNode_StateNodeBase* StateNode);
	
	/** Apply a temporary breakpoint to a specific state. Returns true if successful. */
	bool ApplyTemporaryBreakpointToState(USMGraphNode_StateNodeBase* StateNode);
	
private:
	/** Locate the best state node to use given a function node. This can be the state itself or a debug or entry point. */
	const USMGraphNode_StateNodeBase* GetBestStateNodeFromFunction(const UObject* InContextObject, const UFunction* InContextFunction, const int32 OpcodeOffset = 0) const;
	/** Looks only on the current stack frame, or falls back to the most recent recorded state node. */
	const USMGraphNode_StateNodeBase* GetMostRecentStateNode() const;
	/** Looks up the stack for the most recent state node. */
	const USMGraphNode_StateNodeBase* FindClosestStateNodeInStack(bool bRequireCurrentDepth = false) const;
	
	enum class EResult
	{
		Yes,
		No,
		Unknown
	};
	
	/** If the engine should process the step over command. */
	EResult ShouldUEHandleStepOver() const;
	EResult ShouldUEHandleStepOver(const UEdGraphNode* StoppedNode) const;
	
	EResult ShouldUEHandleStepOut() const;

	/** Find the opcode offset to use when finding the proper source node. */
	static int32 GetOpcodeOffsetFromStackFrame(const FFrame& StackFrame);

	/**
	 * Checks if the given graph node has valid "then" execution pins connected to other graph nodes.
	 * This function iterates through the output pins of the provided node, verifies if the pin is an
	 * execution pin, and ensures it is connected to at least one other node through its linked pins.
	 *
	 * @param Node The graph node to examine for valid "then" execution pins.
	 * @return Yes if all possible exec pins are connected to other nodes; No if there are no exec pins,
	 * Unknown if not all the exec pins are wired -- This is unknown because we might be stepping out to another
	 * k2 node that UE should handle, or it could be stepping out to an SM node which we should handle.
	 */
	static EResult DoesNodeExecToAnotherNode(const UEdGraphNode* Node);
	
private:
	/** The blueprint debug controller singleton. */
	static TStrongObjectPtr<USMBlueprintDebugController> BlueprintDebugController;
	
	FDelegateHandle OnScriptExecutionDelegateHandle;
	FDelegateHandle OnScriptExceptionHandle;
	
	/** The command we are executing. */
	LD::Editor::Debug::EDebugCommandType CurrentDebugCommandType = LD::Editor::Debug::EDebugCommandType::Default;
	
	/** The stack depth of state machine debug calls. */
	uint32 StateMachineDebugStackDepth = 0;
	
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<USMBlueprint>> DebuggedBlueprints;
	
	/** The most recent state node we've previously recorded. */
	TWeakObjectPtr<const USMGraphNode_StateNodeBase> MostRecentRecordedStateNode;
	/** The most recent graph node on command start. When the user first executes a debug command. */
	TWeakObjectPtr<const UEdGraphNode> MostRecentNodeOnCommandStart;
	/** The graph we are expected to hit for the next temporary breakpoint. */
	TWeakObjectPtr<const USMGraph> ExpectedGraphOnNextTemporaryBreakpoint;
	
	/** The user has just executed a debug command. */
	bool bCommandJustExecuted = false;
};
