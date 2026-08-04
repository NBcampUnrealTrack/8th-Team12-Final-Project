// Copyright Recursoft LLC. All Rights Reserved.

#include "SMNode_Base.h"

#include "ExposedFunctions/SMExposedFunctionDefines.h"
#include "SMCachedPropertyData.h"
#include "SMLogging.h"
#include "SMNodeInstance.h"
#include "SMPropertyUtils.h"
#include "SMRuntimeSettings.h"
#include "SMStateInstance.h"
#include "SMUtils.h"

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
#include "Misc/App.h"
#endif

#define LOGICDRIVER_FUNCTION_HANDLER_TYPE FSMNode_FunctionHandlers

#if WITH_EDITORONLY_DATA
bool FSMNode_Base::bValidateGuids = false;
#endif

#if WITH_EDITOR
#include "Blueprints/SMBlueprintDebugEditorBridge.h"
#endif

FSMNode_Base::FSMNode_Base() : NodePosition(ForceInitToZero), TimeInState(0),
                               DuplicateId(0), bIsInEndState(false),
                               bHasUpdated(false), bHasInputEvents(false),
                               OwningInstance(nullptr),
                               NodeInstance(nullptr), NodeInstanceClass(nullptr)
{
	/*
	 * Originally the Guid was initialized here. This caused warnings to show up during packaging because
	 * Unreal does safety checks on struct native constructors by comparing multiple initializations with different
	 * addresses and verifying each property matches. That doesn't work with a Guid because it is guaranteed to
	 * be unique each time.
	 */
}

FSMNode_Base& FSMNode_Base::operator=(const FSMNode_Base& Node)
{
	if (this == &Node)
	{
		return *this;
	}

	NodePosition = Node.NodePosition;
	TimeInState = Node.TimeInState;
	DuplicateId = Node.DuplicateId;
	bIsInEndState = Node.bIsInEndState;
	bHasUpdated = Node.bHasUpdated;
	bHasInputEvents = Node.bHasInputEvents;
	Guid = Node.Guid;
	OwnerGuid = Node.OwnerGuid;
	PathGuid = Node.PathGuid;
	NodeName = Node.NodeName;
	TemplateName = Node.TemplateName;
	StackTemplateNames = Node.StackTemplateNames;
	StackNodeInstances = Node.StackNodeInstances;
	OwningInstance = Node.OwningInstance;
	NodeInstance = Node.NodeInstance;
	TemplateVariableGraphProperties = Node.TemplateVariableGraphProperties;
	NodeInstanceClass = Node.NodeInstanceClass;
	
	// Must ensure RuntimeData is properly cloned after struct copying instead of being shared.
	// The = operator is essential for this. Multiple automation tests fail, quick verification with SMInstance.IsStopping.
	RuntimeData = Node.RuntimeData.IsValid() ? Node.RuntimeData->Clone() : nullptr;

	return *this;
}

void FSMNode_Base::Initialize(USMInstance* Instance)
{
	OwningInstance = Instance;
}

void FSMNode_Base::InitializeFunctionHandlers()
{
	// InitializeFunctionHandlers must be implemented for each FSMNode type.
	unimplemented();

	// Define `FSM[NodeType]_FunctionHandlers` under FSMExposedNodeFunctions that gets set by the BP compiler.
	// Each implementation needs `#define LOGICDRIVER_FUNCTION_HANDLER_TYPE FSM[NodeType]_FunctionHandlers` set in the CPP.
	// Then each overload of InitializeFunctionHandlers just needs to call `INITIALIZE_NODE_FUNCTION_HANDLER();`
}

void FSMNode_Base::InitializeGraphFunctions()
{
	check(IsInGameThread());

	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SMNode_Base::InitializeFunctionHandlers"), STAT_SMNode_InitializeFunctionHandlers, STATGROUP_LogicDriver);
		check(OwningInstance);
		InitializeFunctionHandlers();
		checkf(GetFunctionHandlers() != nullptr || &OwningInstance->GetRootStateMachine() == this,
			TEXT("Exposed functions not set for node `%s` in state machine `%s`. If this is a cooked build make sure you have cooked your assets since your last change."),
			*GetNodeName(), *OwningInstance->GetName());
	}

	INITIALIZE_EXPOSED_FUNCTIONS(OnRootStateMachineStartedGraphEvaluator);
	INITIALIZE_EXPOSED_FUNCTIONS(OnRootStateMachineStoppedGraphEvaluator);

	INITIALIZE_EXPOSED_FUNCTIONS(NodeInitializedGraphEvaluators);
	INITIALIZE_EXPOSED_FUNCTIONS(NodeShutdownGraphEvaluators);

#if WITH_EDITORONLY_DATA
	INITIALIZE_EXPOSED_FUNCTIONS(DebugEntryGraphEvaluator);
#endif

	const TSharedPtr<FSMNodeRuntimeData>& NodeRuntimeData = GetRuntimeData();
	
	// Graph properties have been extracted but not initialized.
	for (FSMGraphProperty_Base_Runtime* GraphProperty : NodeRuntimeData->GraphProperties)
	{
		if (GraphProperty->GetGuid().IsValid())
		{
			// The GraphProperties array is either custom graph properties (text graph) which we want or pointers to
			// template data which won't have valid guids but will be initialized below.
			GraphProperty->Initialize(this);
			continue;
		}
		ensure(GraphProperty->LinkedProperty == nullptr);
	}

	// Variable properties already have everything they need and just need to be initialized.
	for (TTuple<FGuid, FSMGraphPropertyTemplateOwner>& TemplateGraphProperty : TemplateVariableGraphProperties)
	{
		for (FSMGraphProperty_Base_Runtime& GraphProperty : TemplateGraphProperty.Value.VariableGraphProperties)
		{
			GraphProperty.Initialize(this);
		}
	}
	
	NodeRuntimeData->bHaveGraphFunctionsInitialized = true;
}

void FSMNode_Base::Reset()
{
	GetRuntimeData()->FunctionHandlers = nullptr;
}

void FSMNode_Base::OnStartedByInstance(USMInstance* Instance)
{
	if (Instance == GetOwningInstance())
	{
		EXECUTE_EXPOSED_FUNCTIONS(OnRootStateMachineStartedGraphEvaluator);
	}
}

void FSMNode_Base::OnStoppedByInstance(USMInstance* Instance)
{
	if (Instance == GetOwningInstance())
	{
		EXECUTE_EXPOSED_FUNCTIONS(OnRootStateMachineStoppedGraphEvaluator);
	}
}

const FGuid& FSMNode_Base::GetNodeGuid() const
{
	return Guid;
}

void FSMNode_Base::GenerateNewNodeGuid()
{
	SetNodeGuid(FGuid::NewGuid());
}

const FGuid& FSMNode_Base::GetGuid() const
{
	return PathGuid;
}

void FSMNode_Base::CalculatePathGuid(TMap<FString, int32>& InOutMappedPaths, bool bUseGuidCache)
{
	const USMInstance* PrimaryInstance = OwningInstance ? OwningInstance->GetPrimaryReferenceOwnerConst() : nullptr;

	static TMap<FGuid, FSMGuidMap> EmptyGuidCache;
	const TMap<FGuid, FSMGuidMap>& GuidCache = PrimaryInstance ? PrimaryInstance->GetRootPathGuidCache() : EmptyGuidCache;
	
	if (bUseGuidCache && PrimaryInstance && &OwningInstance->GetRootStateMachine() != this &&
		GuidCache.Num() > 0 /* Will be empty if caching is disabled. */)
	{
		if (const FSMGuidMap* NodeMap = GuidCache.Find(
			OwningInstance->GetRootStateMachine().GetGuid()))
		{
			if (const FGuid* CachedPathGuid = NodeMap->NodeToPathGuids.Find(GetNodeGuid()))
			{
				PathGuid = *CachedPathGuid;

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
				// Only verify in debug builds as this is a very slow check
				if (
					FApp::GetBuildConfiguration() == EBuildConfiguration::DebugGame ||
					FApp::GetBuildConfiguration() == EBuildConfiguration::Debug
#if WITH_EDITORONLY_DATA
					|| bValidateGuids
#endif
					)
				{
					checkCode(
						FGuid ConfirmGuid;
						auto ConfirmMappedPaths = InOutMappedPaths;
						USMUtils::PathToGuid(GetGuidPath(ConfirmMappedPaths), &ConfirmGuid);
						check(ConfirmGuid == PathGuid);
					);
				}
#endif
			}
		}

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
		if (!PathGuid.IsValid())
		{
			if (OwningInstance == PrimaryInstance)
			{
				LD_LOG_WARNING(TEXT("Guid cache specified but none found for node '%s' in SMInstance '%s'. Try recompiling applicable blueprints."),
					*GetNodeName(), *OwningInstance->GetName())
			}
			else
			{
				LD_LOG_WARNING(TEXT("Guid cache specified but none found for node '%s' in SMInstance '%s' which has a primary reference owner of '%s'. Try recompiling applicable blueprints."),
					*GetNodeName(), *OwningInstance->GetName(), *PrimaryInstance->GetName())
			}

#if WITH_EDITORONLY_DATA
			if (bValidateGuids)
			{
				checkNoEntry();
			}
#endif
		}
#endif
	}
	else
	{
		PathGuid.Invalidate();
	}

	if (!PathGuid.IsValid())
	{
		USMUtils::PathToGuid(GetGuidPath(InOutMappedPaths), &PathGuid);
	}
}

FString FSMNode_Base::GetGuidPath(TMap<FString, int32>& InOutMappedPaths) const
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SMNode_Base::GetGuidPath"), STAT_SMNode_Base_GetGuidPath, STATGROUP_LogicDriver);
	TArray<const FSMNode_Base*> Owners;
	USMUtils::TryGetAllOwners(this, Owners);
	return USMUtils::BuildGuidPathFromNodes(Owners, &InOutMappedPaths);
}

FGuid FSMNode_Base::CalculatePathGuidConst() const
{
	TMap<FString, int32> PathToStateMachine;
	const FString Path = GetGuidPath(PathToStateMachine);
	return USMUtils::PathToGuid(Path);
}

void FSMNode_Base::GenerateNewNodeGuidIfNotSet()
{
	if (Guid.IsValid())
	{
		return;
	}

	GenerateNewNodeGuid();
}

void FSMNode_Base::CreateNodeInstance()
{
	GetRuntimeData()->GraphProperties.Reset();
	
	if (!NodeInstanceClass)
	{
		SetNodeInstanceClass(GetDefaultNodeInstanceClass());
		check(NodeInstanceClass);
	}

	UObject* TemplateInstance = nullptr;
	if (TemplateName != NAME_None && OwningInstance)
	{
		TemplateInstance = USMUtils::FindTemplateFromInstance(OwningInstance, TemplateName);
		if (TemplateInstance == nullptr)
		{
			LD_LOG_ERROR(TEXT("Could not find node template %s for use on node %s from package %s. Loading defaults."), *TemplateName.ToString(), *GetNodeName(), *OwningInstance->GetName());
		}
	}

#if WITH_EDITORONLY_DATA
	if (TemplateInstance && OwningInstance && TemplateInstance->GetClass() != NodeInstanceClass && TemplateInstance->GetClass()->GetName().StartsWith("REINST_"))
	{
		LD_LOG_ERROR(TEXT("Node class mismatch. Node %s has template class %s but is expecting %s. Try recompiling the blueprint %s."),
			*GetNodeName(), *TemplateInstance->GetClass()->GetName(), *NodeInstanceClass->GetName(), *OwningInstance->GetName());
		return;
	}
#endif

	if (!CanEverCreateNodeInstance() ||
		(IsUsingDefaultNodeClass() && !GetDefault<USMRuntimeSettings>()->bPreloadDefaultNodes &&
			StackTemplateNames.Num() == 0 && !bHasInputEvents))
	{
		// Default node instances are created on demand. If part of a stack they should still be created.
		// Input events always end up checking the node instance anyway, so initialize them now. The frame counter
		// on the instance requires them initialized now too or input events may not fire.
		return;
	}
	
	if (!IsInGameThread())
	{
		if (const USMNodeInstance* TemplateNode = Cast<USMNodeInstance>(TemplateInstance))
		{
			if (!TemplateNode->IsInitializationThreadSafe())
			{
				LD_LOG_INFO(TEXT("CreateNodeInstance called asynchronously for node %s that isn't marked thread safe. Queuing to initialize on the game thread."), *GetNodeName());
				if (USMInstance* Instance = GetOwningInstance())
				{
					Instance->GetPrimaryReferenceOwner()->AddNonThreadSafeNode(this);
				}

				return;
			}
		}
	}

	if (OwningInstance->ShouldLookUpReplicatedNodeInstance())
	{
		if (const USMNodeInstance* TemplateNode = Cast<USMNodeInstance>(TemplateInstance))
		{
			if (TemplateNode->IsReplicationEnabled())
			{
				NodeInstance = OwningInstance->FindReplicatedNodeInstance(PathGuid, 0);
				if (!NodeInstance)
				{
					LD_LOG_WARNING(TEXT("Could not find replicated node instance for node %s with guid %s. Creating a new one."), *GetNodeName(), *PathGuid.ToString());
				}
			}
		}
	}
	
	if (!NodeInstance)
	{
		NodeInstance = NewObject<USMNodeInstance>(OwningInstance, NodeInstanceClass, NAME_None, RF_NoFlags, TemplateInstance);
		OwningInstance->AddReplicatedNodeInstance(PathGuid, 0, NodeInstance);
	}
	NodeInstance->SetOwningNode(this);

	CreateStackInstances();
	CreateGraphProperties();
}

void FSMNode_Base::CreateStackInstances()
{
	int32 StackIdx = 0;
	for (const FName& StackTemplateName : StackTemplateNames)
	{
		UObject* TemplateInstance = USMUtils::FindTemplateFromInstance(OwningInstance, StackTemplateName);
		if (TemplateInstance == nullptr)
		{
			LD_LOG_ERROR(TEXT("Could not find node stack template %s for use on node %s from package %s."), *StackTemplateName.ToString(), *GetNodeName(), *OwningInstance->GetName());
			continue;
		}
		
		USMNodeInstance* NewInstance = nullptr;
		if (const USMNodeInstance* TemplateNode = Cast<USMNodeInstance>(TemplateInstance))
		{
			if (TemplateNode->IsReplicationEnabled() && OwningInstance->ShouldLookUpReplicatedNodeInstance())
			{
				NewInstance = OwningInstance->FindReplicatedNodeInstance(PathGuid, StackIdx + 1);
				if (!NewInstance)
				{
					LD_LOG_WARNING(TEXT("Could not find replicated node stack instance for node %s with guid %s and stack idx %s. Creating a new one."),
						*GetNodeName(), *PathGuid.ToString(), *FString::FromInt(StackIdx));
				}
			}
		}
		
		if (!NewInstance)
		{
			NewInstance = NewObject<USMNodeInstance>(OwningInstance, TemplateInstance->GetClass(), NAME_None, RF_NoFlags, TemplateInstance);
			OwningInstance->AddReplicatedNodeInstance(PathGuid, StackIdx + 1, NewInstance);
		}
		NewInstance->SetOwningNode(this);

		StackNodeInstances.Add(NewInstance);
		StackIdx++;
	}
}

void FSMNode_Base::RunConstructionScripts()
{
	if (NodeInstance)
	{
		USMNodeInstance* NodeInstanceCDO = CastChecked<USMNodeInstance>(NodeInstance->GetClass()->GetDefaultObject());
		if (NodeInstanceCDO->bHasGameConstructionScripts)
		{
			NodeInstance->RunConstructionScript();
		}
	}
	
	for (USMNodeInstance* StackInstance : StackNodeInstances)
	{
		if (StackInstance)
		{
			USMNodeInstance* StackInstanceCDO = CastChecked<USMNodeInstance>(StackInstance->GetClass()->GetDefaultObject());
			if (StackInstanceCDO->bHasGameConstructionScripts)
			{
				StackInstance->RunConstructionScript();
			}
		}
	}
}

void FSMNode_Base::SetNodeInstanceClass(UClass* NewNodeInstanceClass)
{
	if (NewNodeInstanceClass && !IsNodeInstanceClassCompatible(NewNodeInstanceClass))
	{
		LD_LOG_ERROR(TEXT("Could not set node instance class %s on node %s. The types are not compatible."), *NewNodeInstanceClass->GetName(), *GetNodeName());
		return;
	}

	NodeInstanceClass = NewNodeInstanceClass;
}

bool FSMNode_Base::IsNodeInstanceClassCompatible(UClass* NewNodeInstanceClass) const
{
	ensureMsgf(false, TEXT("FSMNode_Base IsNodeInstanceClassCompatible hit for node %s and instance class %s. This should always be overidden in child classes."),
		*GetNodeName(), NewNodeInstanceClass ? *NewNodeInstanceClass->GetName() : TEXT("None"));
	return false;
}

USMNodeInstance* FSMNode_Base::GetOrCreateNodeInstance()
{
	if (!NodeInstance && CanEverCreateNodeInstance())
	{
		if (!HaveGraphFunctionsInitialized())
		{
			LD_LOG_ERROR(TEXT("GetOrCreateNodeInstance called on node %s before it has initialized."), *GetNodeName());
			return nullptr;
		}

		if (!NodeInstanceClass)
		{
			LD_LOG_ERROR(TEXT("GetOrCreateNodeInstance called on node %s with null NodeInstanceClass."), *GetNodeName());
			return nullptr;
		}
	
		NodeInstance = NewObject<USMNodeInstance>(OwningInstance, NodeInstanceClass, NAME_None, RF_NoFlags);
		NodeInstance->SetOwningNode(this);
		if (IsInitializedForRun())
		{
			NodeInstance->NativeInitialize();
		}
	}
	
	return NodeInstance;
}

USMNodeInstance* FSMNode_Base::GetNodeInStack(int32 Index) const
{
	if (Index >= 0 && Index < StackNodeInstances.Num())
	{
		return StackNodeInstances[Index];
	}

	return nullptr;
}

USMNodeInstance* FSMNode_Base::GetNodeInStack(const FGuid& InTemplateGuid) const
{
	for (const TObjectPtr<USMNodeInstance>& StackInstance : StackNodeInstances)
	{
		if (StackInstance->GetTemplateGuid() == InTemplateGuid)
		{
			return StackInstance;
		}
	}

	return nullptr;
}

void FSMNode_Base::AddVariableGraphProperty(const FSMGraphProperty_Base_Runtime& GraphProperty, const FGuid& OwningTemplateGuid)
{
	FSMGraphPropertyTemplateOwner& TemplateGraphOwner = TemplateVariableGraphProperties.FindOrAdd(OwningTemplateGuid);
	TemplateGraphOwner.VariableGraphProperties.Add(GraphProperty);
}

void FSMNode_Base::SetNodeName(const FString& Name)
{
	NodeName = Name;
}

void FSMNode_Base::SetTemplateName(const FName& Name)
{
	TemplateName = Name;
}

FName FSMNode_Base::GetTemplateName(USMNodeInstance* InNodeInstance) const
{
	if (InNodeInstance == nullptr || InNodeInstance == NodeInstance)
	{
		return TemplateName;
	}

	const int32 StackIdx = StackNodeInstances.IndexOfByKey(InNodeInstance);
	if (StackIdx == INDEX_NONE)
	{
		return NAME_None;
	}

	check(StackIdx < StackTemplateNames.Num());

	return StackTemplateNames[StackIdx];
}

void FSMNode_Base::AddStackTemplateName(const FName& Name, UClass* TemplateClass)
{
	// TemplateClass originally tracked for dependency loading with blueprint nativization. No longer required
	// since nativization isn't supported, and the array that tracked it added to the struct size.
	StackTemplateNames.Add(Name);
}

void FSMNode_Base::ExecuteInitializeNodes()
{
	if (IsInitializedForRun())
	{
		return;
	}
	
	EXECUTE_EXPOSED_FUNCTIONS(NodeInitializedGraphEvaluators);
	GetRuntimeData()->bIsInitializedForRun = true;
}

void FSMNode_Base::ExecuteShutdownNodes()
{
	EXECUTE_EXPOSED_FUNCTIONS(NodeShutdownGraphEvaluators);
	GetRuntimeData()->bIsInitializedForRun = false;
}

void FSMNode_Base::SetServerTimeInState(float InTime)
{
	GetRuntimeData()->ServerTimeInState = InTime;
}

#if WITH_EDITOR

void FSMNode_Base::EditorShutdown()
{
	// Graph properties are pointers to addresses on the BP itself. If the BP was recompiled they won't be valid.
	GetRuntimeData()->GraphProperties.Reset();
	Reset();
}

void FSMNode_Base::ResetGeneratedValues()
{
	PathGuid.Invalidate();
}

void FSMNode_Base::TriggerDebugBreakpoint()
{
	FSMBlueprintDebugEditorBridge::FDebugOnScope DebugDataOnScope;
#if WITH_EDITORONLY_DATA
	EXECUTE_EXPOSED_FUNCTIONS(DebugEntryGraphEvaluator);
#endif
}

#endif

void FSMNode_Base::PrepareGraphExecution()
{
	if (!HaveGraphFunctionsInitialized())
	{
		return;
	}

	UpdateReadStates();
}

void FSMNode_Base::SetActive(bool bValue)
{
	const TSharedPtr<FSMNodeRuntimeData>& NodeRuntimeData = GetRuntimeData();
#if WITH_EDITORONLY_DATA
	NodeRuntimeData->bWasActive = NodeRuntimeData->bIsActive;
#endif
	NodeRuntimeData->bIsActive = bValue;
}

bool FSMNode_Base::TryExecuteGraphProperties(ESMGraphPropertyEvalFlags OnEvent, ESMGraphPropertyDirection InDirection)
{
	if (USMStateInstance_Base* StateInstance = Cast<USMStateInstance_Base>(GetNodeInstance()))
	{
		if (CanExecuteGraphProperties(OnEvent, StateInstance, InDirection))
		{
			FFilterGraphPropertyArgs Args;
			Args.NodeInstance = StateInstance;
			Args.TemplateGuid = &StateInstance->GetTemplateGuid();
			Args.IncludeFlags = LD::PropertyUtils::ConvertDirectionToFlags(InDirection);
			ExecuteGraphProperties(MoveTemp(Args));
			return true;
		}
	}

	return false;
}

void FSMNode_Base::TryExecuteGraphProperties(ESMGraphPropertyEvalFlags OnEvent)
{
	TryExecuteGraphProperties(OnEvent, ESMGraphPropertyDirection::Input);
	TryExecuteGraphProperties(OnEvent, ESMGraphPropertyDirection::Output);
}

void FSMNode_Base::ExecuteGraphProperties(USMNodeInstance* ForNodeInstance, const FGuid* ForTemplateGuid)
{
	FFilterGraphPropertyArgs Args;
	Args.NodeInstance = ForNodeInstance;
	Args.TemplateGuid = ForTemplateGuid;
	ExecuteGraphProperties(MoveTemp(Args));
}

void FSMNode_Base::ExecuteGraphProperties(const FFilterGraphPropertyArgs& InArgs)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SMNode_Base::ExecuteGraphProperties"), STAT_SMNode_Base_ExecuteGraphProperties, STATGROUP_LogicDriver);
	
	bool bCanEvalDefaultProperties = InArgs.NodeInstance ? InArgs.NodeInstance->bEvalDefaultProperties : true;
	
	auto EvaluateProperties = [bCanEvalDefaultProperties, &InArgs](FSMGraphPropertyTemplateOwner* TemplateOwner)
	{
		for (FSMGraphProperty_Base_Runtime& GraphProperty : TemplateOwner->VariableGraphProperties)
		{
			if ((bCanEvalDefaultProperties || !GraphProperty.GetIsDefaultValueOnly())
				&& (InArgs.IncludeFlags == ESMGraphPropertyFlags::None || GraphProperty.HasAnyPropertyFlags(InArgs.IncludeFlags)))
			{
				const ESMGraphPropertyDirection Direction = LD::PropertyUtils::ConvertFlagsToDirection(InArgs.IncludeFlags);
				GraphProperty.Execute(nullptr, Direction);
			}
		}
	};
	
	if (InArgs.TemplateGuid)
	{
		if (FSMGraphPropertyTemplateOwner* TemplateOwner = TemplateVariableGraphProperties.Find(*InArgs.TemplateGuid))
		{
			EvaluateProperties(TemplateOwner);
		}
	}
	else
	{
		for (TTuple<FGuid, FSMGraphPropertyTemplateOwner>& TemplateGraphProperty : TemplateVariableGraphProperties)
		{
			EvaluateProperties(&TemplateGraphProperty.Value);
		}
	}
}

void FSMNode_Base::TryResetVariables()
{
	if (NodeInstance && NodeInstance->GetResetVariablesOnInitialize())
	{
		NodeInstance->ResetVariables();
	}
	
	for (USMNodeInstance* StackInstance : StackNodeInstances)
	{
		if (StackInstance->GetResetVariablesOnInitialize())
		{
			StackInstance->ResetVariables();
		}
	}
}

void FSMNode_Base::CreateGraphProperties()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SMNode_Base::CreateGraphProperties"), STAT_SMNode_Base_CreateGraphProperties, STATGROUP_LogicDriver);

	const TSharedPtr<FSMCachedPropertyData, ESPMode::ThreadSafe> CachedPropertyData = OwningInstance->GetCachedPropertyData();
	const TMap<FGuid, FSMGraphProperty_Base_Runtime*>& MappedGraphPropertyInstances = CachedPropertyData->
		GetMappedGraphPropertyInstances();

	CreateGraphPropertiesForTemplate(NodeInstance, MappedGraphPropertyInstances);

	for (USMNodeInstance* Template : StackNodeInstances)
	{
		CreateGraphPropertiesForTemplate(Template, MappedGraphPropertyInstances);
	}
}

void FSMNode_Base::CreateGraphPropertiesForTemplate(USMNodeInstance* Template, const TMap<FGuid, FSMGraphProperty_Base_Runtime*>& MappedGraphPropertyInstances)
{
	/* Looks for the real property stored on the owning instance. */
	auto GetRealProperty = [&](const FSMGraphProperty_Base_Runtime* GraphProperty) -> FSMGraphProperty_Base_Runtime*
	{
		if (FSMGraphProperty_Base_Runtime* const* FoundInstance = MappedGraphPropertyInstances.Find(
			GraphProperty->GetOwnerGuid()))
		{
			return *FoundInstance;
		}

		return nullptr;
	};

	checkf(Template, TEXT("Node template is null, try recompiling the state machine."));
	
	const TSharedPtr<FSMCachedPropertyData, ESPMode::ThreadSafe> CachedPropertyData = OwningInstance->GetCachedPropertyData();
	TSet<FProperty*> GraphStructProperties;
	if (USMUtils::TryGetGraphPropertiesForClass(Template->GetClass(), GraphStructProperties, CachedPropertyData))
	{
		for (FProperty* GraphStructProperty : GraphStructProperties)
		{
			TArray<FSMGraphProperty_Base_Runtime*> GraphPropertyInstances;
			USMUtils::BlueprintPropertyToNativeProperty(GraphStructProperty, Template, GraphPropertyInstances);

			for (FSMGraphProperty_Base_Runtime* GraphProperty : GraphPropertyInstances)
			{
				// The graph property being executed is actually in the template, but the graph has a duplicate created on the owning instance,
				// so we need to link them to get the owning instance result properly to template.
				GraphProperty->LinkedProperty = GetRealProperty(GraphProperty);
				GetRuntimeData()->GraphProperties.Add(GraphProperty);
			}
		}
	}
}

#undef LOGICDRIVER_FUNCTION_HANDLER_TYPE
