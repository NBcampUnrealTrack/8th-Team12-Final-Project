// Copyright Recursoft LLC. All Rights Reserved.

#include "Properties/SMEditorPropertyIOUtils.h"

#include "Construction/ISMEditorConstructionManager.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOReaderNode.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOWriterNode.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/SMGraph.h"
#include "Properties/PropertyInteraction/SMPropertyInteractionManager.h"
#include "Properties/SMEditorPropertyUtils.h"
#include "SMSystemEditorLog.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Blueprints/SMBlueprint.h"

#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "SMUnrealTypeDefs.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMEditorPropertyUtilsIO"

int32 LD::Editor::PropertyUtils::IO::GetPropertySerializerVersion()
{
	return 0;
}

FString LD::Editor::PropertyUtils::IO::SerializePropertyDataToString(const TSharedPtr<FPropertyData_Base>& InPropertyData,
                                                                     const TOptional<FName>& InVarName)
{
	check(InPropertyData.IsValid());
	check(InPropertyData->PropertyOwnerType != EPropertyOwnerType::Invalid);
	check(InPropertyData->Property);
	
	const UClass* OwnerClass = InPropertyData->Property->GetOwnerClass();
	
	if (InPropertyData->PropertyOwnerType == EPropertyOwnerType::NodeInstance)
	{
		const TSharedPtr<FNodePropertyData> PropertyNodeData = InPropertyData->As<FNodePropertyData>();
		check(PropertyNodeData.IsValid());
		check(PropertyNodeData->NodeGuid.IsValid());
		// InTemplateGuid may invalid.
		
		const FString PropertyString = FString::Printf(TEXT("%d|%u|%s|%s|%s|%s|%d"),
			GetPropertySerializerVersion(),
			PropertyNodeData->PropertyOwnerType,
			*PropertyNodeData->NodeGuid.ToString(),
			*PropertyNodeData->TemplateGuid.ToString(),
			*OwnerClass->GetClassPathName().ToString(),
			*InVarName.Get(PropertyNodeData->Property->GetFName()).ToString(),
			PropertyNodeData->ArrayIndex);
		return PropertyString;
	}
	if (InPropertyData->PropertyOwnerType == EPropertyOwnerType::OwningBlueprint)
	{
		const TSharedPtr<FOwningBlueprintPropertyData> BlueprintNodeData = InPropertyData->As<FOwningBlueprintPropertyData>();
		check(BlueprintNodeData.IsValid());
		const FString PropertyString = FString::Printf(TEXT("%d|%u|%s|%s"),
			GetPropertySerializerVersion(),
			BlueprintNodeData->PropertyOwnerType,
			*OwnerClass->GetClassPathName().ToString(),
			*InVarName.Get(BlueprintNodeData->Property->GetFName()).ToString());
		return PropertyString;
	}

	checkNoEntry();
	return TEXT("");
}

TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base> LD::Editor::PropertyUtils::IO::DeserializeStringToPropertyData(
	const FString& InPropertyString)
{
	if (InPropertyString.IsEmpty())
	{
		return nullptr;
	}
	
	TArray<FString> Components;
	InPropertyString.ParseIntoArray(Components, TEXT("|"));

	if (!ensure(Components.Num() > 2)) // 0 is version, 1 is type. Required to figure out correct num components.
	{
		return nullptr;
	}
	
	const int32 Version = FCString::Atoi(*Components[0]);
	if (Version != GetPropertySerializerVersion())
	{
		LDEDITOR_LOG_WARNING(TEXT("Mismatched property version. Expected %d, got %d."), GetPropertySerializerVersion(), Version);
		// If we change the format we can use this to import.
	}

	const uint8 OwnerTypeNumeric = FCString::Atoi(*Components[1]);
	const EPropertyOwnerType OwnerType = static_cast<EPropertyOwnerType>(OwnerTypeNumeric);

	switch (OwnerType)
	{
	case EPropertyOwnerType::NodeInstance:
		{
			TSharedPtr<FNodePropertyData> NodePropertyData = MakeShared<FNodePropertyData>();
			
			constexpr int32 ExpectedComponents = 7;
	
			if (!ensure(Components.Num() == ExpectedComponents))
			{
				LDEDITOR_LOG_ERROR(TEXT("Invalid property string format. Expected %d components, got %d."), ExpectedComponents, Components.Num());
				return nullptr;
			}
		
			NodePropertyData->NodeGuid = FGuid(Components[2]);
			NodePropertyData->TemplateGuid = FGuid(Components[3]);

			// Parse ClassPath and locate the class.
			const FString& ClassPath = Components[4];
			const UClass* FoundClass = FindObject<UClass>(nullptr, *ClassPath);
			if (!FoundClass)
			{
				LDEDITOR_LOG_ERROR(TEXT("Can't deserialize property string, class \"%s\" not found."), *ClassPath);
				return nullptr;
			}

			// Parse PropertyName and locate the property.
			const FName PropertyName(*Components[5]);
			NodePropertyData->Property = FoundClass->FindPropertyByName(PropertyName);
			if (!NodePropertyData->Property)
			{
				LDEDITOR_LOG_ERROR(TEXT("Can't deserialize property string, property \"%s\" not found in class \"%s\"."), *PropertyName.ToString(), *ClassPath);
				return nullptr;
			}
		
			NodePropertyData->ArrayIndex = FCString::Atoi(*Components[6]);
	
			return NodePropertyData;
		}
	case EPropertyOwnerType::OwningBlueprint:
		{
			TSharedPtr<FOwningBlueprintPropertyData> OwningBlueprintPropertyData = MakeShared<FOwningBlueprintPropertyData>();
			
			constexpr int32 ExpectedComponents = 4;
	
			if (!ensure(Components.Num() == ExpectedComponents))
			{
				LDEDITOR_LOG_ERROR(TEXT("Invalid property string format. Expected %d components, got %d."), ExpectedComponents, Components.Num());
				return nullptr;
			}
		
			// Parse ClassPath and locate the class.
			const FString& ClassPath = Components[2];
			UClass* FoundClass = FindObject<UClass>(nullptr, *ClassPath);
			FoundClass = FSMBlueprintEditorUtils::GetMostUpToDateClass(FoundClass);
			if (!FoundClass)
			{
				LDEDITOR_LOG_ERROR(TEXT("Can't deserialize property string, class \"%s\" not found."), *ClassPath);
				return nullptr;
			}

			// May not find a blueprint if the property owner class belongs to native parent.
			const UBlueprint* FoundBlueprint = UBlueprint::GetBlueprintFromClass(FoundClass);

			// Parse PropertyName and locate the property.
			const FName PropertyName(*Components[3]);
			OwningBlueprintPropertyData->Property = FoundBlueprint
				? FSMBlueprintEditorUtils::GetPropertyByName(FoundBlueprint, PropertyName)
				: FoundClass->FindPropertyByName(PropertyName);
			
			if (!OwningBlueprintPropertyData->Property)
			{
				LDEDITOR_LOG_ERROR(TEXT("Can't deserialize property string, property \"%s\" not found in class \"%s\"."), *PropertyName.ToString(), *ClassPath);
				return nullptr;
			}
	
			return OwningBlueprintPropertyData;
		}
	}

	return nullptr;
}

TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base> LD::Editor::PropertyUtils::IO::GetPropertyDataFromPin(const UEdGraphPin* Pin)
{
	check(Pin);
	const TSharedPtr<FPropertyData_Base> Result = DeserializeStringToPropertyData(Pin->GetDefaultAsString());
	if (Result.IsValid())
	{
		Result->SourceObject = Pin->GetOwningNode();
	}
	return Result;
}

UEdGraph* LD::Editor::PropertyUtils::IO::GetPropertyOwnerStateGraph(const TSharedPtr<FPropertyData_Base>& InPropertyData, USMBlueprint* InBlueprint)
{
	if (InPropertyData.IsValid() && InPropertyData->Property)
	{
		if (const TSharedPtr<FNodePropertyData> NodePropertyData = InPropertyData->As<FNodePropertyData>())
		{
			UObject* OwningObject = InPropertyData->SourceObject ? InPropertyData->SourceObject : InPropertyData->Property->GetOwnerClass();
			if (!InBlueprint)
			{
				InBlueprint = FSMBlueprintEditorUtils::FindBlueprintFromObject(OwningObject);
			}
			if (InBlueprint)
			{
				if (USMGraph* RootGraph = FSMBlueprintEditorUtils::GetRootStateMachineGraph(InBlueprint))
				{
					if (UEdGraph* Graph = FSMBlueprintEditorUtils::FindGraphFromRuntimeNodeGuid(RootGraph, NodePropertyData->NodeGuid))
					{
						return Graph;
					}
				}
			}
		}
	}

	return nullptr;
}

USMGraphK2Node_PropertyNode_Base* LD::Editor::PropertyUtils::IO::GetPropertyNode(const TSharedPtr<FPropertyData_Base>& InPropertyData,
	USMBlueprint* InBlueprint)
{
	if (const UEdGraph* StateGraph = GetPropertyOwnerStateGraph(InPropertyData, InBlueprint))
	{
		if (const USMGraphNode_StateNodeBase* StateNode =
			Cast<USMGraphNode_StateNodeBase>(FSMBlueprintEditorUtils::FindTopLevelOwningNode(StateGraph)))
		{
			const TSharedPtr<FNodePropertyData> NodePropertyData = InPropertyData->AsChecked<FNodePropertyData>();
			return StateNode->GetGraphPropertyNode(NodePropertyData->Property->GetFName(), NodePropertyData->TemplateGuid, NodePropertyData->ArrayIndex);
		}
	}

	return nullptr;
}

FText LD::Editor::PropertyUtils::IO::GetPropertyOwnerDisplayName(const TSharedPtr<FPropertyData_Base>& InPropertyData)
{
	if (InPropertyData.IsValid() && InPropertyData->Property)
	{
		UObject* OwningObject = InPropertyData->SourceObject ? InPropertyData->SourceObject : InPropertyData->Property->GetOwnerClass();
		if (USMBlueprint* Blueprint = FSMBlueprintEditorUtils::FindBlueprintFromObject(OwningObject))
		{
			if (const UEdGraph* Graph = GetPropertyOwnerStateGraph(InPropertyData, Blueprint))
			{
				return FText::FromString(Graph->GetName());
			}
			else if (InPropertyData->IsA<FOwningBlueprintPropertyData>())
			{
				return FText::FromString(Blueprint->GetName());
			}
		}
	}
	
	return FText::GetEmpty();
}

FText LD::Editor::PropertyUtils::IO::GetFormattedPropertyDisplayName(
	const TSharedPtr<FPropertyData_Base>& InPropertyData, bool bIncludeOwnerName)
{
	FText PropertyName = FText::GetEmpty();
	if (InPropertyData.IsValid())
	{
		if (InPropertyData->Property)
		{
			using namespace LD::Editor::PropertyUtils;
			int32 ArrayIndex = INDEX_NONE;
			if (const TSharedPtr<FNodePropertyData> NodePropertyData = InPropertyData->As<FNodePropertyData>())
			{
				ArrayIndex = NodePropertyData->ArrayIndex;
			}
			PropertyName = GetPropertyDisplayName(InPropertyData->Property->GetDisplayNameText(), ArrayIndex);
		}
	}

	if (bIncludeOwnerName)
	{
		const FText OwnerName = GetPropertyOwnerDisplayName(InPropertyData);
		if (!OwnerName.IsEmpty())
		{
			PropertyName = FText::Format(LOCTEXT("FullPropertyName", "{0}.{1}"), OwnerName, PropertyName);
		}
	}
	
	return PropertyName;
}

USMGraphK2Node_PropertyIOReaderNode* LD::Editor::PropertyUtils::IO::GetConnectedPropertyReaderNode(
	USMGraphK2Node_PropertyNode_Base* InPropertyNode)
{
	return FSMBlueprintEditorUtils::FindFirstConnectedNode<USMGraphK2Node_PropertyIOReaderNode>(InPropertyNode, EGPD_Input);
}

USMGraphK2Node_PropertyIOReaderNode* LD::Editor::PropertyUtils::IO::GetOrPlacePropertyReaderNode(
	USMGraphK2Node_PropertyNode_Base* InPropertyNode, bool* bJustCreated)
{
	if (bJustCreated)
	{
		*bJustCreated = false;
	}
	USMGraphK2Node_PropertyIOReaderNode* ConnectedIONode = GetConnectedPropertyReaderNode(InPropertyNode);
	if (!ConnectedIONode && InPropertyNode->IsInputProperty())
	{
		UEdGraph* OwningGraph = InPropertyNode->GetGraph();
		// Next check any node on the graph.
		ConnectedIONode = FSMBlueprintEditorUtils::GetFirstNodeOfClassNested<USMGraphK2Node_PropertyIOReaderNode>(OwningGraph);
		if (ConnectedIONode && ConnectedIONode->GetSelectedNodePropertyData().IsValid())
		{
			// But only if it isn't connected to anything.
			ConnectedIONode = nullptr;
		}
		if (!ConnectedIONode && !InPropertyNode->IsGraphReadOnly())
		{
			FScopedTransaction Transaction(LOCTEXT("PropertyUtils_PlaceIOReader", "Place IO Reader"));
			
			OwningGraph->Modify();
			FGraphNodeCreator<USMGraphK2Node_PropertyIOReaderNode> NodeCreator(*OwningGraph);
			ConnectedIONode = NodeCreator.CreateNode();
			NodeCreator.Finalize();

			if (bJustCreated)
			{
				*bJustCreated = true;
			}
		}
	}
	
	return ConnectedIONode;
}

USMGraphK2Node_PropertyIOWriterNode* LD::Editor::PropertyUtils::IO::GetConnectedPropertyWriterNode(USMGraphK2Node_PropertyNode_Base* InPropertyNode)
{
	return FSMBlueprintEditorUtils::FindFirstConnectedExecNode<USMGraphK2Node_PropertyIOWriterNode>(InPropertyNode, EGPD_Output);
}

USMGraphK2Node_PropertyIOWriterNode* LD::Editor::PropertyUtils::IO::GetOrPlacePropertyWriterNode(
	USMGraphK2Node_PropertyNode_Base* InPropertyNode, bool* bJustCreated)
{
	if (bJustCreated)
	{
		*bJustCreated = false;
	}
	
	USMGraphK2Node_PropertyIOWriterNode* ConnectedIONode = GetConnectedPropertyWriterNode(InPropertyNode);
	if (!ConnectedIONode && InPropertyNode->IsOutputProperty())
	{
		UEdGraph* OwningGraph = InPropertyNode->GetGraph();
		// Look for any node on the same graph that isn't connected to anything. For IO writers only one can exist.
		ConnectedIONode = FSMBlueprintEditorUtils::GetFirstNodeOfClassNested<USMGraphK2Node_PropertyIOWriterNode>(OwningGraph);
		if (!ConnectedIONode && !InPropertyNode->IsGraphReadOnly())
		{
			// Don't transact IO writer creation - IO writer nodes are meant to be permanent with few exceptions.
			
			OwningGraph->Modify();
			
			FGraphNodeCreator<USMGraphK2Node_PropertyIOWriterNode> NodeCreator(*OwningGraph);
			ConnectedIONode = NodeCreator.CreateNode();
			ConnectedIONode->NodePosX = InPropertyNode->NodePosX + 500;
			ConnectedIONode->NodePosY = InPropertyNode->NodePosY;
			NodeCreator.Finalize();
			if (bJustCreated)
			{
				*bJustCreated = true;
			}

			if (UBlueprint* Blueprint = FSMBlueprintEditorUtils::FindBlueprintFromObject(InPropertyNode))
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}
		}
	}

	// Exists but isn't connected.
	if (ConnectedIONode && !ConnectedIONode->IsConnectedToOwningPropertyNode())
	{
		if (UEdGraphPin* OutputThenPin = InPropertyNode->GetThenPin())
		{
			UEdGraphPin* InputExecPin = ConnectedIONode->GetExecPin();
			check(InputExecPin);
			InPropertyNode->GetSchema()->TryCreateConnection(OutputThenPin, InputExecPin);
			
			if (UEdGraphPin* OutputValuePin = InPropertyNode->GetResultPin(EGPD_Output))
			{
				UEdGraphPin* InputValuePin = ConnectedIONode->GetPrimaryInputPin();
				check(InputValuePin);
				InPropertyNode->GetSchema()->TryCreateConnection(OutputValuePin, InputValuePin);
			}
		}
	}
	
	return ConnectedIONode;
}

USMGraphK2Node_PropertyIOBase* LD::Editor::PropertyUtils::IO::
FindPropertyIONodeThatShouldLinkToPropertyOutput(USMGraphK2Node_PropertyNode_Base* InPropertyNode,
	const USMGraphK2Node_PropertyIOBase* InToOutputProperty)
{
	if (InToOutputProperty->IsA<USMGraphK2Node_PropertyIOReaderNode>())
	{
		if (USMGraphK2Node_PropertyIOWriterNode* OtherWriter = GetConnectedPropertyWriterNode(InPropertyNode))
		{
			return OtherWriter;
		}
	}
	if (InToOutputProperty->IsA<USMGraphK2Node_PropertyIOWriterNode>())
	{
		if (USMGraphK2Node_PropertyIOReaderNode* OtherReader = GetConnectedPropertyReaderNode(InPropertyNode))
		{
			return OtherReader;
		}
	}
	
	return nullptr;
}

void LD::Editor::PropertyUtils::IO::MakeWriterConnectionToOwningBlueprintProperty(
	USMGraphK2Node_PropertyIOWriterNode* InWriterNode, const FProperty* InProperty)
{
	check(InWriterNode);
	check(InProperty);

	const TSharedPtr<FOwningBlueprintPropertyData> BlueprintPropertyData = MakeShared<FOwningBlueprintPropertyData>(InProperty);
	InWriterNode->FindOrAddAndSetPropertyPinForPropertyData(BlueprintPropertyData);
}

bool LD::Editor::PropertyUtils::IO::TryMakePropertyConnection(USMGraphK2Node_PropertyNode_Base* InPropertyWithOutput,
	USMGraphK2Node_PropertyNode_Base* InPropertyWithInput)
{
	if ((!InPropertyWithOutput || !InPropertyWithOutput->IsOutputProperty())
		|| (!InPropertyWithInput || !InPropertyWithInput->IsInputProperty()) )
	{
		return false;
	}

	if (const USMGraphK2Node_PropertyIOReaderNode* ReaderNode = GetOrPlacePropertyReaderNode(InPropertyWithInput))
	{
		return TryMakePropertyConnection(ReaderNode->GetPropertyPinIn(), InPropertyWithOutput);
	}
	return false;
}

bool LD::Editor::PropertyUtils::IO::TryMakePropertyConnection(UEdGraphPin* InFromIONodePropertyPin,
                                                              USMGraphK2Node_PropertyNode_Base* InToPropertyNode)
{
	check(InToPropertyNode);
	if (InToPropertyNode->IsGraphReadOnly())
	{
		return false;
	}
	
	FScopedTransaction Transaction(LOCTEXT("PropertyUtils_MakePropertyConnection", "Make Property Connection"));
	
	USMGraphK2Node_PropertyIOBase* FromIONode = CastChecked<USMGraphK2Node_PropertyIOBase>(InFromIONodePropertyPin->GetOwningNode());

	const TSharedPtr<FNodePropertyData> FromNodePropertyData = FromIONode->
		GetOwningPropertyNodePropertyData();

	const USMGraphK2Node_PropertyIOBase* ToIONode = nullptr;
	
	if (FromIONode->IsA<USMGraphK2Node_PropertyIOWriterNode>())
	{
		if (USMGraphK2Node_PropertyIOReaderNode* ReaderNode = GetOrPlacePropertyReaderNode(InToPropertyNode))
		{
			if (ReaderNode->CanAcceptConnection(FromIONode))
			{
				// Only break once a connection is confirmed and before any values are set.
				BreakPropertyConnection(InFromIONodePropertyPin);
	
				// If this node is currently connected to another property we need to break that connection.
				if (const TSharedPtr<FPropertyData_Base> ReaderNodeCurrentPropertyData = ReaderNode->GetSelectedNodePropertyData())
				{
					if (const USMGraphK2Node_PropertyNode_Base* CurrentConnectedPropertyNode = GetPropertyNode(ReaderNodeCurrentPropertyData))
					{
						if (CurrentConnectedPropertyNode != FromIONode->GetOwningPropertyNode())
						{
							BreakPropertyConnection(ReaderNode->GetPropertyPinInChecked());
						}
					}
				}
		
				// Will add reference to our from node.
				ReaderNode->SetPropertySelectionPinValue(ReaderNode->GetPropertyPinInChecked(), FromNodePropertyData);
				ToIONode = ReaderNode;
			}
		}
	}
	else if (FromIONode->IsA<USMGraphK2Node_PropertyIOReaderNode>())
	{
		if (USMGraphK2Node_PropertyIOWriterNode* WriterNode = GetOrPlacePropertyWriterNode(InToPropertyNode))
		{
			if (WriterNode->CanAcceptConnection(FromIONode))
			{
				// Only break once a connection is confirmed and before any values are set.
				BreakPropertyConnection(InFromIONodePropertyPin);
				
				// Will add reference to our from node.
				WriterNode->FindOrAddAndSetPropertyPinForPropertyData(FromNodePropertyData);
				ToIONode = WriterNode;
			}
		}
	}

	if (ToIONode)
	{
		FromIONode->SetPropertySelectionPinValue(InFromIONodePropertyPin, ToIONode->GetOwningPropertyNodePropertyData());

		if (const USMGraphNode_StateNodeBase* OwningStateNode = Cast<USMGraphNode_StateNodeBase>(FromIONode->GetOwningGraphNode()))
		{
			// Update selection UI of other states impacted by this connection.
			OwningStateNode->GetPropertyInteractionManager().ReapplyConnectedSelections();
		}
		return true;
	}
	
	return false;
}

bool LD::Editor::PropertyUtils::IO::TryMakePropertyConnection(UEdGraphPin* InFromIONodePropertyPin,
	const TSharedPtr<FPropertyData_Base>& InToPropertyData, USMBlueprint* InBlueprint)
{
	if (USMGraphK2Node_PropertyNode_Base* ToPropertyNode = GetPropertyNode(InToPropertyData, InBlueprint))
	{
		return TryMakePropertyConnection(InFromIONodePropertyPin, ToPropertyNode);
	}

	return false;
}

void LD::Editor::PropertyUtils::IO::BreakPropertyConnection(UEdGraphPin* InFromIONodePropertyPin)
{
	USMGraphK2Node_PropertyIOBase* FromIONode = CastChecked<USMGraphK2Node_PropertyIOBase>(InFromIONodePropertyPin->GetOwningNode());

	if (const USMGraphK2Node_PropertyNode_Base* OwningPropertyNode = FromIONode->GetOwningPropertyNode())
	{
		if (OwningPropertyNode->IsGraphReadOnly())
		{
			return;
		}
	}
	
	USMBlueprint* OwningBlueprint = FSMBlueprintEditorUtils::FindBlueprintFromObject(FromIONode);
	
	const bool bCanTransact = OwningBlueprint
		&& !ISMEditorConstructionManager::Get().IsConditionallyCompiling(OwningBlueprint);
	
	FScopedTransaction Transaction(LOCTEXT("PropertyUtils_BreakPropertyConnection", "Break Property Connection"));

	ON_SCOPE_EXIT
	{
		// Don't transact if this is happening during our compile. We can't just pass a param to the transaction, because
		// the graph schema will create one when breaking pins, which we don't want.
		if (!bCanTransact)
		{
			Transaction.Cancel();
		}
	};
	
	FromIONode->Modify();
	
	const FGuid FromPinGuid = InFromIONodePropertyPin->PinId;
	
	if (USMGraphK2Node_PropertyNode_Base* ReferencedPropertyNode = FromIONode->GetReferencedIONode(FromPinGuid))
	{
		if (FromIONode->IsA<USMGraphK2Node_PropertyIOWriterNode>())
		{
			// If we reference the same property multiple times we don't want to clear the connected reader.
			if (FromIONode->GetIOReferenceCount(ReferencedPropertyNode) <= 1)
			{
				if (USMGraphK2Node_PropertyIOReaderNode* ReaderNode = GetConnectedPropertyReaderNode(ReferencedPropertyNode))
				{
					ReaderNode->SetPropertySelectionPinValue(ReaderNode->GetPropertyPinInChecked(), nullptr);
				}
			}
		}
		else if (FromIONode->IsA<USMGraphK2Node_PropertyIOReaderNode>())
		{
			if (USMGraphK2Node_PropertyIOWriterNode* WriterNode = GetConnectedPropertyWriterNode(ReferencedPropertyNode))
			{
				const TSharedPtr<FNodePropertyData> FromNodePropertyData = FromIONode->
					GetOwningPropertyNodePropertyData();

				WriterNode->ClearOrRemovePropertyPinForPropertyData(FromNodePropertyData);
			}
		}

		// Clear the reference in the from node.
		FromIONode->RemoveReferencedIONode(FromPinGuid);
	}

	FromIONode->SetPropertySelectionPinValue(InFromIONodePropertyPin, nullptr);

	// Possible could be null if something went wrong, like a force delete.
	if (const USMGraphNode_StateNodeBase* OwningStateNode = Cast<USMGraphNode_StateNodeBase>(FromIONode->GetOwningGraphNode()))
	{
		// Update selection UI of other states impacted by this connection.
		OwningStateNode->GetPropertyInteractionManager().ReapplyConnectedSelections();
	}
}

void LD::Editor::PropertyUtils::IO::BreakAllPropertyConnections(const USMGraphK2Node_PropertyIOBase* InPropertyNode)
{
	if (const USMGraphK2Node_PropertyIOReaderNode* ReaderNode = Cast<USMGraphK2Node_PropertyIOReaderNode>(InPropertyNode))
	{
		BreakPropertyConnection(ReaderNode->GetPropertyPinInChecked());
	}

	if (const USMGraphK2Node_PropertyIOWriterNode* WriterNode = Cast<USMGraphK2Node_PropertyIOWriterNode>(InPropertyNode))
	{
		const TArray<UEdGraphPin*> Outputs = WriterNode->GetAllPropertyOutputs();
		for (UEdGraphPin* Output : Outputs)
		{
			BreakPropertyConnection(Output);
		}
	}
}

void LD::Editor::PropertyUtils::IO::BreakAllPropertyConnections(USMGraphK2Node_PropertyNode_Base* InPropertyNode)
{
	if (const USMGraphK2Node_PropertyIOReaderNode* ReaderNode = GetConnectedPropertyReaderNode(InPropertyNode))
	{
		BreakAllPropertyConnections(ReaderNode);
	}

	if (const USMGraphK2Node_PropertyIOWriterNode* WriterNode = GetConnectedPropertyWriterNode(InPropertyNode))
	{
		BreakAllPropertyConnections(WriterNode);
	}
}

TArray<USMGraphK2Node_PropertyNode_Base*> LD::Editor::PropertyUtils::IO::GetAllConnectedPropertyNodes(
	USMGraphK2Node_PropertyNode_Base* InPropertyNode)
{
	check(InPropertyNode);
	
	TSet<USMGraphK2Node_PropertyNode_Base*> Results;

	if (const USMGraphK2Node_PropertyIOReaderNode* ReaderNode = GetConnectedPropertyReaderNode(InPropertyNode))
	{
		Results.Append(ReaderNode->GetAllReferencedIONodes());
	}

	if (const USMGraphK2Node_PropertyIOWriterNode* WriterNode = GetConnectedPropertyWriterNode(InPropertyNode))
	{
		Results.Append(WriterNode->GetAllReferencedIONodes());
	}
	
	return Results.Array();
}

TSharedPtr<SWidget> LD::Editor::PropertyUtils::IO::MakeLinearExpressionNodeWidget(
	const TSharedPtr<FPropertyData_Base>& InPropertyData, const UEdGraphSchema* InSchema)
{
	if (!InPropertyData.IsValid())
	{
		// If something was force deleted this could be null.
		return nullptr;
	}
	check(InSchema);
	FText VarName = InPropertyData->Property ? InPropertyData->Property->GetDisplayNameText() : FText::GetEmpty();

	// Format with variable index when appropriate.
	if (const TSharedPtr<FNodePropertyData> NodeData = InPropertyData->As<FNodePropertyData>())
	{
		VarName = GetPropertyDisplayName(VarName, NodeData->ArrayIndex);
	}

	const bool bIsNodeProperty = InPropertyData->PropertyOwnerType == EPropertyOwnerType::NodeInstance;
	FEdGraphPinType PinType;
	GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(InPropertyData->Property, PinType);
	
	// We break apart the "owner.property" in different parts in case we want to apply unique styling.
	const TSharedPtr<SHorizontalBox> PropertyNameBox = SNew(SHorizontalBox);
	if (bIsNodeProperty)
	{
		PropertyNameBox->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(GetPropertyOwnerDisplayName(InPropertyData))
			.TextStyle(FSMUnrealAppStyle::Get(), "NormalText")
		];
		PropertyNameBox->AddSlot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Separator", "."))
		];
	}
	PropertyNameBox->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Text(VarName)
	];
	
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("KismetExpression.ReadVariable.Body"))
				.ColorAndOpacity(InSchema->GetPinTypeColor(PinType))
			]
			+ SOverlay::Slot()
			.Padding(FMargin(6,4))
			[
				PropertyNameBox.ToSharedRef()
			]
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Visibility(EVisibility::HitTestInvisible)
				.Image(FAppStyle::GetBrush("KismetExpression.ReadVariable.Gloss"))
			]
		];
}

#undef LOCTEXT_NAMESPACE
