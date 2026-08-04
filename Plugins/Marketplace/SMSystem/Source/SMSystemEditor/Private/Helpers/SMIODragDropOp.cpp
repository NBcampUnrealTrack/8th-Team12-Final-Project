// Copyright Recursoft LLC. All Rights Reserved.

#include "SMIODragDropOp.h"

#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOBase.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/SMGraph.h"
#include "Properties/PropertyInteraction/SMPropertyInteractionManager.h"
#include "Widgets/Properties/SSMPropertyWidget.h"

#include "EdGraphHandleTypes.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "SMUnrealTypeDefs.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SMIODragDropOp"

FSMIODragDropOp::~FSMIODragDropOp()
{
	if (OwningSMGraph.IsValid())
	{
		const ESMPropertyInteractionType InteractionType = PinDirection == EGPD_Output ? ESMPropertyInteractionType::UnsetDisplayInputConnector
		: ESMPropertyInteractionType::UnsetDisplayOutputConnector;
		FSMPropertyInteractionManager::SetAllPropertyInteractionsForGraph(OwningSMGraph.Get(), InteractionType);
	}
}

TSharedRef<FSMIODragDropOp> FSMIODragDropOp::New(const FGraphPinHandle& InStartingPin, UBlueprint* InBlueprint,
	const TWeakPtr<SSMGraphProperty_ConnectorPin>& InConnectorPin)
{
	TSharedRef<FSMIODragDropOp> Operation = MakeShareable(new FSMIODragDropOp(InStartingPin, InBlueprint, InConnectorPin));
	Operation->Construct();

	return Operation;
}

void FSMIODragDropOp::Construct()
{
	if (OwningSMGraph.IsValid())
	{
		const ESMPropertyInteractionType InteractionType = PinDirection == EGPD_Output ?
			ESMPropertyInteractionType::SetDisplayInputConnector : ESMPropertyInteractionType::SetDisplayOutputConnector;
		FSMPropertyInteractionManager::FBulkInteractionArgs InteractionArgs;
		InteractionArgs.ScopedGraphPanel = nullptr;
		FSMPropertyInteractionManager::SetAllPropertyInteractionsForGraph(OwningSMGraph.Get(), InteractionType, InteractionArgs);
	}

	// Skip FGraphEditorDragDropAction::Construct so a decorator window is created.
	FDragDropOperation::Construct();
	HoverTargetChanged();
}

TSharedPtr<SWidget> FSMIODragDropOp::GetDefaultDecorator() const
{
	return
		SNew(SBorder)
		.Visibility(EVisibility::Visible)
		.BorderImage(FSMUnrealAppStyle::GetBrush("Graph.ConnectorFeedback.Border"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(3.0f)
				[
					SNew(SImage)
					.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateRaw(this, &FSMIODragDropOp::GetErrorIconVisible)))
					.Image(FSMUnrealAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error")))
					.ColorAndOpacity(FLinearColor::White)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(3.0f)
				[
					SNew(SImage)
					.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateRaw(this, &FSMIODragDropOp::GetIconVisible)))
					.Image(FSMUnrealAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK")))
					.ColorAndOpacity(FLinearColor::White)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSMPropertyWidget)
					.Property(this, &FSMIODragDropOp::GetDraggedProperty)
					.DisplayText(this, &FSMIODragDropOp::GetDraggedPropertyText)
				]
			]
		];
}

void FSMIODragDropOp::HoverTargetChanged()
{
	FGraphEditorDragDropAction::HoverTargetChanged();
	bDropTargetValid = false;
	
	if (!IsHoveredPinValid())
	{
		return;
	}
	
	if (const UEdGraphPin* ValidSourcePin = GetDraggedPin())
	{
		const FEdGraphPinHandle PinB(GetHoveredPin());
		const FEdGraphPinHandle PinA(ValidSourcePin);
		UEdGraphPin* PinAPtr = PinA.GetPin();
		UEdGraphPin* PinBPtr = PinB.GetPin();
		if (PinAPtr != nullptr && PinBPtr != nullptr)
		{
			// Make sure the IO pins are compatible. We can't directly check to IO Nodes because the hovered node will always
			// return the property pin, not the IO pin. Therefor the from pin is for an IO node, but the to pin is for a property.
			// So the most we can do is make sure the directions are compatible.
			if (PinAPtr->Direction != PinBPtr->Direction)
			{
				if (const USMGraphK2Node_PropertyIOBase* AOutputNode = Cast<USMGraphK2Node_PropertyIOBase>(PinAPtr->GetOwningNode()))
				{
					// Make sure the property can accept the connection.
					if (const USMGraphK2Node_PropertyNode_Base* BNode = Cast<USMGraphK2Node_PropertyNode_Base>(PinBPtr->GetOwningNode()))
					{
						bDropTargetValid = AOutputNode->CanAcceptConnection(BNode);
					}
				}
				else if (const USMGraphK2Node_PropertyIOBase* BOutputNode = Cast<USMGraphK2Node_PropertyIOBase>(PinBPtr->GetOwningNode()))
				{
					// Make sure the property can accept the connection.
					if (const USMGraphK2Node_PropertyNode_Base* ANode = Cast<USMGraphK2Node_PropertyNode_Base>(PinAPtr->GetOwningNode()))
					{
						bDropTargetValid = BOutputNode->CanAcceptConnection(ANode);
					}
				}
			}
		}
	}
}

FReply FSMIODragDropOp::DroppedOnPin(FVector2D ScreenPosition, FVector2D GraphPosition)
{
	bool bError = false;
	TSet<FEdGraphNodeHandle> NodeList;
	
	const FScopedTransaction Transaction(LOCTEXT("FSMIODragDropOp_CreateConnection", "Create Pin Link"));

	FEdGraphPinHandle PinB(GetHoveredPin());

	if (UEdGraphPin* ValidSourcePin = GetDraggedPin())
	{
		FEdGraphPinHandle PinA(ValidSourcePin);
		UEdGraphPin* PinAPtr = PinA.GetPin();
		UEdGraphPin* PinBPtr = PinB.GetPin();
		if (PinAPtr != nullptr && PinBPtr != nullptr)
		{
			// Make sure the IO pins are compatible. We can't directly check to IO Nodes because the hovered node will always
			// return the property pin, not the IO pin. Therefor the from pin is for an IO node, but the to pin is for a property.
			// So the most we can do is make sure the directions are compatible.
			if (PinAPtr->Direction != PinBPtr->Direction)
			{
				bool bConnectionSuccessful = false;
				if (USMGraphK2Node_PropertyIOBase* AOutputNode = Cast<USMGraphK2Node_PropertyIOBase>(PinAPtr->GetOwningNode()))
				{
					if (USMGraphK2Node_PropertyNode_Base* BNode = Cast<USMGraphK2Node_PropertyNode_Base>(PinBPtr->GetOwningNode()))
					{
						bConnectionSuccessful = LD::Editor::PropertyUtils::IO::TryMakePropertyConnection(PinAPtr, BNode);
					}
				}
				else if (USMGraphK2Node_PropertyIOBase* BOutputNode = Cast<USMGraphK2Node_PropertyIOBase>(PinBPtr->GetOwningNode()))
				{
					if (USMGraphK2Node_PropertyNode_Base* ANode = Cast<USMGraphK2Node_PropertyNode_Base>(PinAPtr->GetOwningNode()))
					{
						bConnectionSuccessful = LD::Editor::PropertyUtils::IO::TryMakePropertyConnection(PinBPtr, ANode);
					}
				}
			
				// the pin may change during the creation of the link
				if (bConnectionSuccessful)
				{
					if (PinAPtr && !PinAPtr->IsPendingKill())
					{
						NodeList.Add(PinAPtr->GetOwningNode());
					}
					if (PinBPtr && !PinBPtr->IsPendingKill())
					{
						NodeList.Add(PinB.GetNode());
					}
				}
			}
		}	
		else
		{
			bError = true;
		}
	}
	
	// Send all nodes that received a new pin connection a notification
	for (auto It = NodeList.CreateConstIterator(); It; ++It)
	{
		if(UEdGraphNode* Node = It->GetNode())
		{
			Node->NodeConnectionListChanged();
		}
	}

	if (bError)
	{
		return FReply::Unhandled();
	}

	return FReply::Handled();
}

bool FSMIODragDropOp::IsHoveredPinValid() const
{
	const UEdGraphPin* HoveredPinPtr = GetHoveredPin();
	if (!HoveredPinPtr || HoveredPinPtr->bWasTrashed
		|| HoveredPinPtr->GetOwningNodeUnchecked() == nullptr
		|| !IsValid(HoveredPinPtr->GetOwningNodeUnchecked()))
	{
		return false;
	}

	return true;
}

UEdGraphPin* FSMIODragDropOp::GetDraggedPin() const
{
	// Custom handling to find the UEdGraphNode. Default lookup via graph panel isn't possible since the graph panel
	// for our case is the SM Graph Panel, while the pin is owned by the property graph panel.
	if (StartingPin.IsValid() && Blueprint.IsValid())
	{
		if (const UEdGraphNode* GraphNode = FBlueprintEditorUtils::GetNodeByGUID(Blueprint.Get(), StartingPin.NodeGuid))
		{
			return GraphNode->FindPinById(StartingPin.PinId);
		}
	}
	
	return nullptr;
}

const FProperty* FSMIODragDropOp::GetDraggedProperty() const
{
	return DraggedPropertyData.IsValid() ? DraggedPropertyData->Property : nullptr;
}

FText FSMIODragDropOp::GetDraggedPropertyText() const
{
	return GetFormattedPropertyDisplayName(DraggedPropertyData, true);
}

FSMIODragDropOp::FSMIODragDropOp(const FGraphPinHandle& InStartingPin, UBlueprint* InBlueprint,
	const TWeakPtr<SSMGraphProperty_ConnectorPin>& InConnectorPin)
{
	StartingPin = InStartingPin;
	Blueprint = InBlueprint;
	OwningConnectorPin = InConnectorPin;
	if (const UEdGraphPin* DraggedPin = GetDraggedPin())
	{
		PinDirection = DraggedPin->Direction;
		if (const USMGraphK2Node_PropertyIOBase* IONode = Cast<USMGraphK2Node_PropertyIOBase>(DraggedPin->GetOwningNode()))
		{
			if (const USMGraphNode_Base* OwningGraphNode = IONode->GetOwningGraphNode())
			{
				OwningSMGraph = OwningGraphNode->GetOwningStateMachineGraph();
			}
			
			DraggedPropertyData = IONode->GetOwningPropertyNodePropertyData();
		}
	}
}

#undef LOCTEXT_NAMESPACE
