// Copyright Recursoft LLC. All Rights Reserved.

#include "SMBlueprintSpawnNodeCommands.h"

#include "Graph/Nodes/SMGraphNode_AnyStateNode.h"
#include "Graph/Nodes/SMGraphNode_ConduitNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/Schema/SMGraphSchemaActions.h"
#include "SMSystemEditorLog.h"

#include "SMStateInstance.h"
#include "Utilities/SMNodeRuleUtils.h"

#include "EdGraphSchema_K2.h"
#include "Framework/Commands/InputChord.h"
#include "Misc/ConfigCacheIni.h"

class FSMStateNodeSpawnInfo
{
public:
	virtual ~FSMStateNodeSpawnInfo() = default;

	FSMStateNodeSpawnInfo() : StateGraphNodeClass(nullptr), NodeInstanceClass(nullptr)
	{
	}

	FSMStateNodeSpawnInfo(UClass* InStateGraphNodeClass, const TSoftClassPtr<USMNodeInstance>& InStateClass) : StateGraphNodeClass(InStateGraphNodeClass), NodeInstanceClass(InStateClass) {}

	// ~FNodeSpawnInfo
	virtual void GetActions(UEdGraph* InDestGraph, FVector2f& InOutDestPosition, TArray<UEdGraphNode*>& OutNodes)
	{
		const LD::NodeRuleUtils::FGraphRules GraphRules = LD::NodeRuleUtils::GetGraphRules(InDestGraph);
	
		const bool bBaseStatesAllowed = GraphRules.bBaseStatesAllowed;
		const bool bBaseStateMachinesAllowed = GraphRules.bBaseStateMachinesAllowed;
		//const bool bReferencesAllowed = GraphRules.bReferencesAllowed;
		//const bool bParentsAllowed = GraphRules.bParentsAllowed;
		const bool bConduitsAllowed = GraphRules.bConduitsAllowed;
	
		TSoftClassPtr<USMStateMachineInstance> BaseStateMachineClass = GraphRules.BaseStateMachineClass;
		const USMStateMachineInstance* StateMachineDefault = GraphRules.StateMachineDefault;
		
		if (NodeInstanceClass == USMStateInstance::StaticClass() && !bBaseStatesAllowed)
		{
			return;
		}
		
		if (NodeInstanceClass == USMStateMachineInstance::StaticClass() && !bBaseStateMachinesAllowed)
		{
			return;
		}

		if (NodeInstanceClass == USMConduitInstance::StaticClass() && !bConduitsAllowed)
		{
			return;
		}
		
		// Validate custom node classes
		{
			// Validate allowed placement in state machine.
			if (StateMachineDefault && NodeInstanceClass && !FSMNodeClassRule::IsBaseClass(NodeInstanceClass)
				&& !StateMachineDefault->GetAllowedNodes().IsStateAllowed(NodeInstanceClass))
			{
				return;
			}
		}
		
		const TSharedPtr<FSMGraphSchemaAction_NewNode> NewNodeAction = MakeShared<FSMGraphSchemaAction_NewNode>();
		NewNodeAction->GraphNodeTemplate = NewObject<USMGraphNode_StateNodeBase>(GetTransientPackage(), StateGraphNodeClass);
		NewNodeAction->NodeSoftClassPath = NodeInstanceClass;
		
		UEdGraphNode* GraphNode = NewNodeAction->PerformAction(InDestGraph, nullptr, InOutDestPosition, false);
		InOutDestPosition.Y = FMath::Max(InOutDestPosition.Y, GraphNode->NodePosY + UEdGraphSchema_K2::EstimateNodeHeight(GraphNode));
		OutNodes.Add(GraphNode);
	}
	// ~FNodeSpawnInfo

	/** Holds the UI Command to verify chords for this action are held. */
	TSharedPtr<FUICommandInfo> CommandInfo;
	
private:
	/** The class type the graph node should be. */
	UClass* StateGraphNodeClass;
	
	/** The node class to apply for custom nodes. */
	TSoftClassPtr<USMNodeInstance> NodeInstanceClass;
};

void FSMBlueprintSpawnNodeCommands::RegisterCommands()
{
	const FString ConfigSection = TEXT("LogicDriverSpawnNodes");
	const FString SettingName = TEXT("Node");
	TArray<FString> NodeSpawns;
	GConfig->GetArray(*ConfigSection, *SettingName, NodeSpawns, GEditorPerProjectIni);

	for (int32 x = 0; x < NodeSpawns.Num(); ++x)
	{
		FString ClassName;
		if (!FParse::Value(*NodeSpawns[x], TEXT("Class="), ClassName))
		{
			// Could not find a class name, cannot continue with this line
			continue;
		}
		
		// Our class in question, either native or blueprint.
		TSoftClassPtr<USMNodeInstance> StateInstanceClass(ClassName);
		
		// Validators find the NativeClass without having to load blueprint classes.
		const LD::NodeRuleUtils::FExtractedNodeConnectionValidator NodeConnectionValidator = LD::NodeRuleUtils::GetConnectionValidators(StateInstanceClass);
		if (!NodeConnectionValidator.IsValid())
		{
			LDEDITOR_LOG_ERROR(TEXT("SMBlueprintSpawnNodeCommands: Failed to get connection validators for node class '%s'."), *ClassName);
			continue;
		}
		
		FString CommandLabel;
		TSharedPtr<FSMStateNodeSpawnInfo> InfoPtr;

		if (NodeConnectionValidator.NativeClass && NodeConnectionValidator.NativeClass->IsChildOf(USMStateInstance_Base::StaticClass()))
		{
			const USMGraphNode_StateNodeBase* GraphNode = nullptr;
			
			if (NodeConnectionValidator.NativeClass == USMAnyStateInstance::StaticClass())
			{
				GraphNode = CastChecked<USMGraphNode_AnyStateNode>(USMGraphNode_AnyStateNode::StaticClass()->GetDefaultObject());
				StateInstanceClass = nullptr;
			}
			else if (NodeConnectionValidator.NativeClass->IsChildOf(USMStateInstance::StaticClass()))
			{
				GraphNode = CastChecked<USMGraphNode_StateNode>(USMGraphNode_StateNode::StaticClass()->GetDefaultObject());
			}
			else if (NodeConnectionValidator.NativeClass->IsChildOf(USMStateMachineInstance::StaticClass()))
			{
				GraphNode = CastChecked<USMGraphNode_StateMachineStateNode>(USMGraphNode_StateMachineStateNode::StaticClass()->GetDefaultObject());
			}
			else if (NodeConnectionValidator.NativeClass->IsChildOf(USMConduitInstance::StaticClass()))
			{
				GraphNode = CastChecked<USMGraphNode_ConduitNode>(USMGraphNode_ConduitNode::StaticClass()->GetDefaultObject());
			}
			if (!GraphNode)
			{
				continue;
			}

			CommandLabel = GraphNode->GetNodeTitle(ENodeTitleType::ListView).ToString();

			if (StateInstanceClass && CommandLabel.Len() == 0)
			{
				CommandLabel = StateInstanceClass->GetName();
			}

			InfoPtr = MakeShareable(new FSMStateNodeSpawnInfo(GraphNode->GetClass(), StateInstanceClass));
		}

		// If spawn info was created, setup a UI Command for keybinding.
		if (InfoPtr.IsValid())
		{
			TSharedPtr<FUICommandInfo> CommandInfo;

			FKey Key;
			bool bShift = false;
			bool bCtrl = false;
			bool bAlt = false;
			bool bCmd = false;
			
			// Parse the keybinding information
			FString KeyString;
			if (FParse::Value(*NodeSpawns[x], TEXT("Key="), KeyString))
			{
				Key = *KeyString;
			}

			if (Key.IsValid())
			{
				FParse::Bool(*NodeSpawns[x], TEXT("Shift="), bShift);
				FParse::Bool(*NodeSpawns[x], TEXT("Alt="), bAlt);
				FParse::Bool(*NodeSpawns[x], TEXT("Ctrl="), bCtrl);
				FParse::Bool(*NodeSpawns[x], TEXT("Cmd="), bCmd);
			}

			FInputChord Chord(Key, EModifierKey::FromBools(bCtrl, bAlt, bShift, bCmd));

			FText CommandLabelText = FText::FromString( CommandLabel );
			FText Description = FText::Format(NSLOCTEXT("BlueprintEditor", "NodeSpawnDescription", "Hold down the bound keys and left click in the graph panel to spawn a {0} node."), CommandLabelText);

			FUICommandInfo::MakeCommandInfo(this->AsShared(), CommandInfo, FName(*NodeSpawns[x]), CommandLabelText, Description, FSlateIcon(FAppStyle::GetAppStyleSetName(), *FString::Printf(TEXT("%s.%s"), *this->GetContextName().ToString(), *NodeSpawns[x])), EUserInterfaceActionType::Button, Chord);

			InfoPtr->CommandInfo = CommandInfo;

			NodeCommands.Add(InfoPtr);
		}
	}
}

void FSMBlueprintSpawnNodeCommands::GetGraphActionByChord(const FInputChord& InChord, UEdGraph* InDestGraph, FVector2f& InOutDestPosition, TArray<UEdGraphNode*>& OutNodes) const
{
	if(InChord.IsValidChord())
	{
		for(int32 x = 0; x < NodeCommands.Num(); ++x)
		{
			if(NodeCommands[x]->CommandInfo->HasActiveChord(InChord))
			{
				NodeCommands[x]->GetActions(InDestGraph, InOutDestPosition, OutNodes);
			}
		}
	}
}
