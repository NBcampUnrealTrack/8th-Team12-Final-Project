// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMDefines.h"
#include "SMGraphProperty_Base.h"

#include "SMNode_Base.generated.h"

class USMInstance;
class USMNodeInstance;

/**
 * Maintains runtime data specific to a node that doesn't need to be serialized to disk. This is used to group
 * properties that can be instantiated on the heap, lowering struct memory offset costs during BP compilation GC.
 */
struct FSMNodeRuntimeData
{
	virtual ~FSMNodeRuntimeData() = default;
	FSMNodeRuntimeData() : OwnerNode(nullptr), FunctionHandlers(nullptr),
	                       ServerTimeInState(SM_ACTIVE_TIME_NOT_SET),
	                       bHaveGraphFunctionsInitialized(false),
	                       bIsInitializedForRun(0),
	                       bIsActive(false)
	{
#if WITH_EDITORONLY_DATA
		bWasActive = false;
#endif
	}
	
	virtual TSharedPtr<FSMNodeRuntimeData> Clone() const
	{
		return MakeShared<FSMNodeRuntimeData>(*this);
	}
	
	friend struct FSMNode_Base;
	
protected:
	/** Custom graph structs with special handling. Dynamically loaded on initialization from embedded structs. */
	TArray<FSMGraphProperty_Base_Runtime*> GraphProperties;
	
	/** The node directly owning this node. Should be a StateMachine. */
	FSMNode_Base* OwnerNode;
	
public:
	/**
	 * Contains all function handler pointers. Every child node should implement their own type and instantiate it under InitializeFunctionHandlers.
	 * This is used to lower struct memory offset costs during GC.
	 */
	FSMNode_FunctionHandlers* FunctionHandlers;

private:
	/** Last recorded active time in state from the server. */
	float ServerTimeInState;
	
	uint8 bHaveGraphFunctionsInitialized: 1;
	uint8 bIsInitializedForRun: 1;
	uint8 bIsActive: 1;
	
public:
#if WITH_EDITORONLY_DATA
	/** Debug helper in case a state switches to inactive in one frame. */
	mutable uint8 bWasActive: 1;
#endif
};

/**
 * Base struct for all state machine nodes. The Guid MUST be manually initialized right after construction.
 */
USTRUCT(BlueprintInternalUseOnly)
struct SMSYSTEM_API FSMNode_Base
{
	GENERATED_USTRUCT_BODY()

	friend class FSMEditorConstructionManager;

private:
	/**
	 * Shared pointer to runtime data associated with a state machine node.
	 * Manages details relevant to the node during execution. Kept as a shared ptr
	 * to minimize the struct size of the node during blueprint compilation.
	 */
	mutable TSharedPtr<FSMNodeRuntimeData> RuntimeData;
	
public:
#if WITH_EDITORONLY_DATA
	/** Check whether compiled guid cache matches run-time guid calculation. */
	static bool bValidateGuids;
#endif

	FSMNode_FunctionHandlers* GetFunctionHandlers() const { return GetRuntimeData()->FunctionHandlers; }

public:
	/** The node position in the graph. Set automatically. */
	UPROPERTY()
	FVector2D NodePosition;
	
	/** The current time spent in the state. */
	UPROPERTY(BlueprintReadWrite, Category = "State Machines")
	float TimeInState;

	/** Special indicator in case this node is a duplicate within the same blueprint. If this isn't 0 then the NodeGuid will have been adjusted. */
	UPROPERTY()
	int32 DuplicateId;
	
	/**
	 * State Machine is in end state or the state is an end state.
	 * 
	 * The main case for this is when a local transition graph uses "Is State Machine in End State" (USMGraphK2Node_StateMachineReadNode_InEndState)
	 * @todo This should be moved to transitions only, but the now deprecated USMNodeInstance::IsInEndState() uses this.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "State Machines")
	uint8 bIsInEndState: 1;

	/** State has updated at least once. */
	UPROPERTY(BlueprintReadWrite, Category = "State Machines")
	uint8 bHasUpdated: 1;

	/** This node has at least one input event present. */
	UPROPERTY()
	uint8 bHasInputEvents: 1;

public:
	virtual void UpdateReadStates() {}

public:
	FSMNode_Base();
	virtual ~FSMNode_Base() = default;
	FSMNode_Base(const FSMNode_Base& Node) = default;
	
	FSMNode_Base& operator=(const FSMNode_Base& Node);

	/**
	 * Initializes various properties and associate the node with its owning state machine instance.
	 *
	 * @param Instance The state machine instance used to initialize the node.
	 */
	virtual void Initialize(USMInstance* Instance);
	
	/** Retrieve the runtime-only data relevant to this node. */
	const TSharedPtr<FSMNodeRuntimeData>& GetRuntimeData() const
	{
		if (!RuntimeData.IsValid())
		{
			RuntimeData = CreateRuntimeData();
		}
		return RuntimeData;
	}
	
	template<typename T>
	TSharedPtr<T> GetRuntimeDataAs() const
	{
		return StaticCastSharedPtr<T>(GetRuntimeData());
	}
	
protected:
	virtual TSharedPtr<FSMNodeRuntimeData> CreateRuntimeData() const
	{
		return MakeShared<FSMNodeRuntimeData>();
	}
	
	/** Map the FunctionHandler pointer. Must be implemented per child struct! */
	virtual void InitializeFunctionHandlers();

public:
	/** Initialize all graph evaluator functions. Must be called from GameThread! */
	virtual void InitializeGraphFunctions();

	/** Resets persistent data. */
	virtual void Reset();
	
	/** Called when the blueprint owning this node is started. */
	virtual void OnStartedByInstance(USMInstance* Instance);

	/** Called when the blueprint owning this node has stopped. */
	virtual void OnStoppedByInstance(USMInstance* Instance);
	
	/** If all graph function initialization has taken place once. */
	bool HaveGraphFunctionsInitialized() const { return GetRuntimeData()->bHaveGraphFunctionsInitialized; }

	/** If the node is currently initialized for this run. */
	bool IsInitializedForRun() const { return GetRuntimeData()->bIsInitializedForRun; }

	/** Unique identifier used in constructing nodes from a graph. May not be unique if this is from a parent graph or a reference. */
	const FGuid& GetNodeGuid() const;
	void GenerateNewNodeGuid();
	
	/** Unique identifier taking into account qualified path. Unique across blueprints if called after Instance initialization. */
	const FGuid& GetGuid() const;
	/** Calculate the value returned from GetGuid(). Gets all owner nodes and builds a path to this node. Hashes the path and sets PathGuid. */
	virtual void CalculatePathGuid(TMap<FString, int32>& InOutMappedPaths, bool bUseGuidCache = false);
	/** Unhashed string format of the guid path. MappedPaths are used to adjust for collisions. */
	FString GetGuidPath(TMap<FString, int32>& InOutMappedPaths) const;
	/** Calculate the path guid but do not set the guid. */
	FGuid CalculatePathGuidConst() const;
	
	/** Only generate a new guid if the current guid is invalid. This needs to be called
	 * on new nodes. */
	void GenerateNewNodeGuidIfNotSet();
	void SetNodeGuid(const FGuid& NewGuid) { Guid = NewGuid; }

	/** The state machine's NodeGuid owning this node. */
	void SetOwnerNodeGuid(const FGuid& NewGuid) { OwnerGuid = NewGuid; }
	/** Unique identifier to help determine which state machine this node belongs to. */
	const FGuid& GetOwnerNodeGuid() const { return OwnerGuid; }

	/** Property name of the NodeGuid. */
	static FName GetNodeGuidPropertyName() { return GET_MEMBER_NAME_CHECKED(FSMNode_Base, Guid); }

	/** Property name of the PathGuid. */
	static FName GetPathGuidPropertyName() { return GET_MEMBER_NAME_CHECKED(FSMNode_Base, PathGuid); }
	
	/** The node directly owning this node. Should be a StateMachine. */
	void SetOwnerNode(FSMNode_Base* Owner) { check(Owner != this); GetRuntimeData()->OwnerNode = Owner; }
	/** The node directly owning this node. Should be a StateMachine. */
	virtual FSMNode_Base* GetOwnerNode() const { return GetRuntimeData()->OwnerNode; }

	/** The state machine instance owning this node. */
	USMInstance* GetOwningInstance() const { return OwningInstance; }

	/** Create the node instance if a node instance class is set. */
	void CreateNodeInstance();
	void CreateStackInstances();
	virtual void RunConstructionScripts();
	
	/** Calls CheckNodeInstanceCompatible. */
	void SetNodeInstanceClass(UClass* NewNodeInstanceClass);
	
	/** Derived nodes should overload and check for the correct type. */
	virtual bool IsNodeInstanceClassCompatible(UClass* NewNodeInstanceClass) const;
	
	/** Return the current node instance. Only valid after initialization and may be nullptr. */
	virtual USMNodeInstance* GetNodeInstance() const { return NodeInstance; }

	/** Create a node instance on demand if needed. Only required for default node classes. Initialization should be completed before calling. */
	virtual USMNodeInstance* GetOrCreateNodeInstance();

	/** If the node can at some point create a node instance. */
	virtual bool CanEverCreateNodeInstance() const { return true; }
	
	/** Returns the current stack instances. */
	const TArray<TObjectPtr<USMNodeInstance>>& GetStackInstancesConst() const { return StackNodeInstances; }
	TArray<TObjectPtr<USMNodeInstance>>& GetStackInstances() { return StackNodeInstances; }

	/** Returns a specific state from the stack. */
	USMNodeInstance* GetNodeInStack(int32 Index) const;

	/** Returns a node in the stack the template guid. */
	USMNodeInstance* GetNodeInStack(const FGuid& InTemplateGuid) const;
	
	/** The default node instance class. Each derived node class needs to implement. */
	virtual UClass* GetDefaultNodeInstanceClass() const { return nullptr; }

	/** The current in use node class. */
	UClass* GetNodeInstanceClass() const { return NodeInstanceClass; }

	/** Is the default node class assigned. */
	bool IsUsingDefaultNodeClass() const { return GetDefaultNodeInstanceClass() == GetNodeInstanceClass(); }
	
	void AddVariableGraphProperty(const FSMGraphProperty_Base_Runtime& GraphProperty, const FGuid& OwningTemplateGuid);

	void SetNodeName(const FString& Name);
	const FString& GetNodeName() const { return NodeName; }
	
	void SetTemplateName(const FName& Name);
	
	/**
	 * Return the template name used to find a node instance archetype.
	 *
	 * @param InNodeInstance The current node instance we want the template name for. Can be either the main node instance
	 * or a stack instance. When nullptr the main node instance template name is used.
	 * @return The node template name used to discover node instance archetypes or NAME_None if not found.
	 */
	FName GetTemplateName(USMNodeInstance* InNodeInstance = nullptr) const;
	
	void AddStackTemplateName(const FName& Name, UClass* TemplateClass);
	
	/** If this node is active. */
	virtual bool IsActive() const { return GetRuntimeData()->bIsActive; }

	virtual void ExecuteInitializeNodes();
	virtual void ExecuteShutdownNodes();

	/** Set the time in state as recorded from the server. */
	virtual void SetServerTimeInState(float InTime);

	/** The time in state as recorded by the server. Kept in the base node as transitions can utilize it. */
	float GetServerTimeInState() const { return GetRuntimeData()->ServerTimeInState; }
	
	/**
	* Checks if the instance is allowed to execute properties automatically.
	* @param OnEvent The event to execute on.
	* @param ForTemplate The specific template to check against.
	* @param InDirection The direction of the graph properties to evaluate.
	*/
	virtual bool CanExecuteGraphProperties(ESMGraphPropertyEvalFlags OnEvent, const class USMStateInstance_Base* ForTemplate, ESMGraphPropertyDirection InDirection) const { return false; }
	
	/**
	 * Execute desired graph properties for the given event in a given direction.
	 * @param OnEvent The event to execute on.
	 * @param InDirection The direction of the graph property.
	 */
	virtual bool TryExecuteGraphProperties(ESMGraphPropertyEvalFlags OnEvent, ESMGraphPropertyDirection InDirection);

	/**
	 * Try executing all directions of graph properties.
	 * @param OnEvent The event to execute on.
	 */
	void TryExecuteGraphProperties(ESMGraphPropertyEvalFlags OnEvent);

	struct FFilterGraphPropertyArgs
	{
		FFilterGraphPropertyArgs()
		{
			NodeInstance = nullptr;
			TemplateGuid = nullptr;
			IncludeFlags = ESMGraphPropertyFlags::None;
		}
		
		/** The node instance we are evaluating. */
		USMNodeInstance* NodeInstance;
		/** If specified only graph properties for this template will be executed. If null all properties will be executed. */
		const FGuid* TemplateGuid;
		/** Include processing if these flags are present on the property. */
		ESMGraphPropertyFlags IncludeFlags;
	};
	
	/**
	 * @deprecated This function is deprecated. Use the version which takes FFilterGraphPropertyArgs instead.
	 * 
	 * Evaluates graph properties.
	 * @param ForNodeInstance The node instance we are evaluating.
	 * @param ForTemplateGuid If specified only graph properties for this template will be executed. If null all properties will be executed.
	 */
	UE_DEPRECATED(5.5, "This function is deprecated. Use the version which takes FFilterGraphPropertyArgs instead.")
	void ExecuteGraphProperties(USMNodeInstance* ForNodeInstance, const FGuid* ForTemplateGuid);

	/** Evaluates graph properties. */
	void ExecuteGraphProperties(const FFilterGraphPropertyArgs& InArgs);
	
	/** Retrieve the embedded graph properties. */
	const TArray<FSMGraphProperty_Base_Runtime*>& GetGraphProperties() const { return GetRuntimeData()->GraphProperties; }

	/** Retrieve the template variable graph properties. */
	const TMap<FGuid, FSMGraphPropertyTemplateOwner>& GetTemplateGraphProperties() const { return TemplateVariableGraphProperties; }

	/** See if the user wants variables reset. */
	virtual void TryResetVariables();
	
#if WITH_EDITORONLY_DATA
	virtual bool IsDebugActive() const { return GetRuntimeData()->bIsActive; }
	virtual bool WasDebugActive() const { return GetRuntimeData()->bWasActive; }
#endif

#if WITH_EDITOR
	/** Performs a safe reset. It's possible referenced structs have changed in the BP and may not be valid. */
	virtual void EditorShutdown();

	/** Reset any values set from state machine generation. */
	virtual void ResetGeneratedValues();

	/** Triggers a debug breakpoint. */
	void TriggerDebugBreakpoint();
#endif
	
protected:
	/** Execute the graph. */
	virtual void PrepareGraphExecution();
	virtual void SetActive(bool bValue);

	void CreateGraphProperties();
	void CreateGraphPropertiesForTemplate(USMNodeInstance* Template, const TMap<FGuid, FSMGraphProperty_Base_Runtime*>& MappedGraphPropertyInstances);
protected:
	/**
	 * NodeGuid must always be unique. Do not duplicate the guid in any other node in any blueprint.
	 *
	 * This is not the same guid that is used at run-time. At run-time all NodeGuids in a path to a node
	 * are hashed to form the PathGuid. This is done to account for multiple references and parent graph calls.
	 *
	 * If you need to change the path of a node (such as collapse it to a nested state machine) and you need to maintain
	 * the old guid for run-time saves to work, you should use the GuidRedirectMap on the primary state machine instance
	 * which accepts PathGuids.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Graph Node")
	FGuid Guid;

	/** The state machine's NodeGuid owning this node. */
	UPROPERTY()
	FGuid OwnerGuid;

	/**
	 * Unique identifier calculated from this node's place in an instance.
	 * Calculated by taking the MD5 hash of the full path of all owner NodeGuids and
	 * this NodeGuid.
	 * This is what is returned from GetGuid().
	 * 
	 * ReadWrite so it can be easily read from custom graph nodes.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Graph Node")
	FGuid PathGuid;

	UPROPERTY()
	FString NodeName;

	/** The name of a template archetype to use when constructing an instance. This allows default values be passed into the instance. */
	UPROPERTY()
	FName TemplateName;

	/** The names of stack template archetypes used when constructing the stack. */
	UPROPERTY()
	TArray<FName> StackTemplateNames;

	/** The node instances for this stack. */
	UPROPERTY(BlueprintReadWrite, Transient, Category = "Node Class")
	TArray<TObjectPtr<USMNodeInstance>> StackNodeInstances;
	
	/** The state machine instance owning this node. */
	UPROPERTY()
	TObjectPtr<USMInstance> OwningInstance;

	/** The node instance for this node if it exists. */
	UPROPERTY(BlueprintReadWrite, Transient, Category = "Node Class")
	TObjectPtr<USMNodeInstance> NodeInstance;

	/** Set by the BP compiler. Template Guid -> GraphProperties. Contains data necessary to evaluate variables which have instanced BP graphs. */
	UPROPERTY()
	TMap<FGuid, FSMGraphPropertyTemplateOwner> TemplateVariableGraphProperties;
	
	/** The class to use to construct the node instance if one exists. */
	UPROPERTY(BlueprintReadWrite, Category = "Node Class")
	TObjectPtr<UClass> NodeInstanceClass;
};
