// Copyright Recursoft LLC. All Rights Reserved.

#include "SSMGraphProperty_Connector.h"

#include "Blueprints/SMBlueprintEditor.h"
#include "Configuration/SMEditorStyle.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOReaderNode.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOWriterNode.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SlateNodes/Properties/SSMGraphProperty_Base.h"
#include "Graph/Nodes/SlateNodes/SGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/Pins/PropertySelection/SGraphPin_PropertySelectionPin.h"
#include "Helpers/SMDragDropHelpers.h"
#include "Helpers/SMIODragDropOp.h"
#include "Helpers/SSMDragDropInterceptor.h"
#include "Properties/PropertyInteraction/SMPropertyInteractionManager.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "SMGraphProperty_Base.h"

#include "Animation/AnimNodeBase.h"
#include "BPVariableDragDropAction.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"
#include "SGraphPanel.h"
#include "SMUnrealTypeDefs.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SSMGraphProperty_Connector"

void SSMGraphProperty_ConnectorPin::Construct(const FArguments& InArgs, const UEdGraphPin* InResultPin,
	const UEdGraphPin* InPropertyPin)
{
	// Possible to be hit during compile.
	if (!InResultPin || !InPropertyPin || InResultPin->GetOwningNodeUnchecked() == nullptr || InResultPin->bWasTrashed)
	{
		return;
	}
	OnPinClickedEvent = InArgs._OnPinClicked;
	OwningConnector = InArgs._OwningConnector;
	SetPropertyPin(InPropertyPin);
	SetShowLabel(false);
	SetOnlyShowDefaultValue(false);
	SGraphPin::Construct(SGraphPin::FArguments(), const_cast<UEdGraphPin*>(InResultPin));
}

void SSMGraphProperty_ConnectorPin::SetPropertyPin(const UEdGraphPin* InPropertyPin)
{
	PropertyPin = const_cast<UEdGraphPin*>(InPropertyPin);
}

bool SSMGraphProperty_ConnectorPin::DoesPinRepresentIONode() const
{
	return PropertyPin && Cast<USMGraphK2Node_PropertyIOBase>(PropertyPin->GetOwningNodeUnchecked()) != nullptr;
}

bool SSMGraphProperty_ConnectorPin::IsConnected() const
{
	return (PropertyPin && DoesPinRepresentIONode()) ? !PropertyPin->DefaultValue.IsEmpty() : SGraphPin::IsConnected();
}

const FSlateBrush* SSMGraphProperty_ConnectorPin::GetPinIcon() const
{
	UEdGraphPin* GraphPin = GetPinObj();
	if (!GraphPin || GraphPin->IsPendingKill() || !GraphPin->GetOwningNodeUnchecked())
	{
		return CachedImg_Pin_Disconnected;
	}

	if (Custom_Brush_Connected && Custom_Brush_Disconnected)
	{
		if (IsConnected())
		{
			return Custom_Brush_Connected;
		}
		else
		{
			return Custom_Brush_Disconnected;
		}
	}

	if (IsArray())
	{
		if (IsConnected())
		{
			return CachedImg_ArrayPin_Connected;
		}
		else
		{
			return CachedImg_ArrayPin_Disconnected;
		}
	}
	else if(IsDelegate())
	{
		if (IsConnected())
		{
			return CachedImg_DelegatePin_Connected;		
		}
		else
		{
			return CachedImg_DelegatePin_Disconnected;
		}
	}
	else if (GraphPin->bDisplayAsMutableRef || IsByMutableRef())
	{
		if (IsConnected())
		{
			return CachedImg_RefPin_Connected;		
		}
		else
		{
			return CachedImg_RefPin_Disconnected;
		}

	}
	else if (IsSet())
	{
		return CachedImg_SetPin;
	}
	else if (IsMap())
	{
		return CachedImg_MapPinKey;
	}
	else if (GraphPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && ((GraphPin->PinType.PinSubCategoryObject == FPoseLink::StaticStruct()) || (GraphPin->PinType.PinSubCategoryObject == FComponentSpacePoseLink::StaticStruct())))
	{
		if (IsConnected())
		{
			return CachedImg_PosePin_Connected;
		}
		else
		{
			return CachedImg_PosePin_Disconnected;
		}
	}
	else
	{
		if (IsConnected())
		{
			return CachedImg_Pin_Connected;
		}
		else
		{
			return CachedImg_Pin_Disconnected;
		}
	}
}

TSharedRef<FDragDropOperation> SSMGraphProperty_ConnectorPin::SpawnPinDragEvent(
	const TSharedRef<SGraphPanel>& InGraphPanel, const TArray<TSharedRef<SGraphPin>>& InStartingPins)
{
	check(PropertyPin);
	UBlueprint* OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForGraphChecked(InGraphPanel->GetGraphObj());
	return FSMIODragDropOp::New(PropertyPin, OwningBlueprint,
		StaticCastSharedRef<SSMGraphProperty_ConnectorPin>(AsShared()).ToWeakPtr());
}

FReply SSMGraphProperty_ConnectorPin::OnPinMouseDown(const FGeometry& SenderGeometry, const FPointerEvent& MouseEvent)
{
	OnPinClickedEvent.ExecuteIfBound(SenderGeometry, MouseEvent);
	
	bIsMovingLinks = false;

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && ensure(!bGraphDataInvalid))
	{
		if (IsEditingEnabled())
		{
			if (MouseEvent.IsAltDown())
			{
				// Alt-Left clicking will break all existing connections to a pin
				BreakPinLinks();
				return FReply::Handled();
			}

			TSharedPtr<SGraphNode> OwnerNodePinned = OwnerNodePtr.Pin();
			
			if ((MouseEvent.IsControlDown() || MouseEvent.IsShiftDown()) && (GraphPinObj->LinkedTo.Num() > 0))
			{
				// Get a reference to the owning panel widget
				check(OwnerNodePinned.IsValid());
				TSharedPtr<SGraphPanel> OwnerPanelPtr = OwnerNodePinned->GetOwnerPanel();
				check(OwnerPanelPtr.IsValid());

				// Obtain the set of all pins within the panel
				TSet<TSharedRef<SWidget> > AllPins;
				OwnerPanelPtr->GetAllPins(AllPins);

				// Construct a UEdGraphPin->SGraphPin mapping for the full pin set
				TMap< FGraphPinHandle, TSharedRef<SGraphPin> > PinToPinWidgetMap;
				for (const TSharedRef<SWidget>& SomePinWidget : AllPins)
				{
					const SGraphPin& PinWidget = static_cast<const SGraphPin&>(SomePinWidget.Get());

					UEdGraphPin* GraphPin = PinWidget.GetPinObj();
					if (GraphPin->LinkedTo.Num() > 0)
					{
						PinToPinWidgetMap.Add(FGraphPinHandle(GraphPin), StaticCastSharedRef<SGraphPin>(SomePinWidget));
					}
				}

				// Define a local struct to temporarily store lookup information for pins that we are currently linked to
				struct FLinkedToPinInfo
				{
					// Pin name string
					FName PinName;

					// A weak reference to the node object that owns the pin
					TWeakObjectPtr<UEdGraphNode> OwnerNodePtr;

					// The direction of the pin
					EEdGraphPinDirection Direction;
				};

				// Build a lookup table containing information about the set of pins that we're currently linked to
				TArray<FLinkedToPinInfo> LinkedToPinInfoArray;
				for (UEdGraphPin* Pin : GetPinObj()->LinkedTo)
				{
					if (TSharedRef<SGraphPin>* PinWidget = PinToPinWidgetMap.Find(Pin))
					{
						FLinkedToPinInfo PinInfo;
						PinInfo.PinName = (*PinWidget)->GetPinObj()->PinName;
						// LD: This isn't accessible and we don't use the pin array anyway.
						// PinInfo.OwnerNodePtr = (*PinWidget)->OwnerNodePtr.Pin()->GetNodeObj();
						PinInfo.Direction = (*PinWidget)->GetPinObj()->Direction;
						LinkedToPinInfoArray.Add(MoveTemp(PinInfo));
					}
				}


				// Now iterate over our lookup table to find the instances of pin widgets that we had previously linked to
				TArray<TSharedRef<SGraphPin>> PinArray;
				for (FLinkedToPinInfo PinInfo : LinkedToPinInfoArray)
				{
					if (UEdGraphNode* OwnerNodeObj = PinInfo.OwnerNodePtr.Get())
					{
						for (UEdGraphPin* Pin : PinInfo.OwnerNodePtr.Get()->Pins)
						{
							if (Pin->PinName == PinInfo.PinName && Pin->Direction == PinInfo.Direction)
							{
								if (TSharedRef<SGraphPin>* pWidget = PinToPinWidgetMap.Find(FGraphPinHandle(Pin)))
								{
									PinArray.Add(*pWidget);
								}
							}
						}
					}
				}

				TSharedPtr<FDragDropOperation> DragEvent;
				if (PinArray.Num() > 0)
				{
					DragEvent = SpawnPinDragEvent(OwnerPanelPtr.ToSharedRef(), PinArray);
				}

				// Control-Left clicking will break all existing connections to a pin
				// Note: that for some nodes, this can cause reconstruction. In that case, pins we had previously linked to may now be destroyed. 
				//       So the break MUST come after the SpawnPinDragEvent(), since that acquires handles from PinArray (the pins need to be
				//       around for us to construct valid handles from).
				if (MouseEvent.IsControlDown())
				{
					BreakPinLinks();
				}

				if (DragEvent.IsValid())
				{
					bIsMovingLinks = true;
					return FReply::Handled().BeginDragDrop(DragEvent.ToSharedRef());
				}
				else
				{
					// Shouldn't get here, but just in case we lose our previous links somehow after breaking them, we'll just skip the drag.
					return FReply::Handled();
				}
			}
			
			if (!MouseEvent.IsShiftDown() && !GraphPinObj->bNotConnectable && bDragAndDropEnabled)
			{
				// Start a drag-drop on the pin
				if (ensure(OwnerNodePinned.IsValid()))
				{
					TArray<TSharedRef<SGraphPin>> PinArray;
					PinArray.Add(SharedThis(this));

					if (TSharedPtr<SGraphPanel> OwnerGraphPanel = OwnerNodePinned->GetOwnerPanel())
					{
						return FReply::Handled().BeginDragDrop(SpawnPinDragEvent(OwnerGraphPanel.ToSharedRef(), PinArray));
					}
				}
				else
				{
					return FReply::Unhandled();
				}
			}
		}

		// It's not connectible, but we don't want anything above us to process this left click.
		return FReply::Handled();
	}
	else
	{
		return FReply::Unhandled();
	}
}

void SSMGraphProperty_ConnectorPin::BreakPinLinks()
{
	check(PropertyPin);
	LD::Editor::PropertyUtils::IO::BreakPropertyConnection(PropertyPin);
}

SSMGraphProperty_Connector::SSMGraphProperty_Connector(): PropertyBackgroundColor(),
                                                          bIsComboBoxOpened(0), bFreezeDisplayMode(0), bLastVisible(0),
                                                          bWasInCompactMode(0),
                                                          bInCompactMode(0)
{
}

SSMGraphProperty_Connector::~SSMGraphProperty_Connector()
{
	if (PropertyGraph.IsValid())
	{
		PropertyGraph->RemoveOnGraphChangedHandler(GraphChangedHandle);
	}

	CloseConnector();
}

void SSMGraphProperty_Connector::Construct(const FArguments& InArgs, const UEdGraphPin* InResultPinObj)
{
	check(InResultPinObj);
	
	SetCursor(EMouseCursor::Default);
	SetVisibility(MakeAttributeSP(this, &SSMGraphProperty_Connector::GetConnectorVisibility));
	SetEnabled(MakeAttributeSP(this, &SSMGraphProperty_Connector::CanConnectorBeModified));
	
	PrimaryResultGraphPin = InResultPinObj;
	ParentStateWidget = InArgs._ParentStateWidget;
	ParentGraphPropertyWidget = InArgs._ParentGraphPropertyWidget;

	USMGraphK2Node_PropertyNode_Base* PropertyNode = CastChecked<USMGraphK2Node_PropertyNode_Base>(InResultPinObj->GetOwningNode());
	PropertyGraph = PropertyNode->GetGraph();
	OwningPropertyNode = PropertyNode;
	
	GraphChangedHandle = PropertyGraph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateSP(this, &SSMGraphProperty_Connector::OnGraphChanged));
	
	PropertyBackgroundColor = PropertyNode->GetNodeTitleColor();
	PropertyBackgroundColor.A *= 0.5;
	
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ConstructCompactWidget()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ConstructFullDisplayWidget()
			]
		]
		+ SOverlay::Slot()
		.Padding(SSMGraphProperty_Base::GetHighlightPadding())
		[
			SNew(SBorder)
			.BorderImage(FSMEditorStyle::Get()->GetBrush("BoxHighlight"))
			.BorderBackgroundColor(this, &SSMGraphProperty_Connector::GetHighlightColor)
			.Visibility(this, &SSMGraphProperty_Connector::GetHighlightBorderVisibility)
		]
	];
	
	CreateNestedPins();
}

void SSMGraphProperty_Connector::OpenConnector()
{
	if (StateOverlayGuid.IsValid())
	{
		return;
	}

	if (const TSharedPtr<SGraphNode_StateNode> SStateNode = ParentStateWidget.Pin())
	{
		SGraphNode_StateNode::FExternalOverlayWidget ExternalOverlayWidget;
		ExternalOverlayWidget.Widget = SharedThis(this);
		ExternalOverlayWidget.OverlayOffset = [this](const FVector2f& InStateSize)
		{
			return GetOffset(InStateSize);
		};
		
		StateOverlayGuid = SStateNode->AddOverlayWidget(MoveTemp(ExternalOverlayWidget));
	}
}

void SSMGraphProperty_Connector::CloseConnector()
{
	if (const TSharedPtr<SGraphNode_StateNode> SStateNode = ParentStateWidget.Pin())
	{
		SStateNode->RemoveOverlayWidget(StateOverlayGuid);
		StateOverlayGuid.Invalidate();
	}
}

FReply SSMGraphProperty_Connector::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Handle so our OnMouseButtonUp can fire.
	return FReply::Handled();
}

FReply SSMGraphProperty_Connector::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bPreviouslySelected = CurrentInteractionData.IsValid() && CurrentInteractionData.Pin()->bSelected;
	
	SelectOwningProperty();
	
	if (bPreviouslySelected && MouseEvent.GetEffectingButton() != EKeys::RightMouseButton)
	{
		// Toggle the selection if previously selected and we are clicking directly on this node.
		if (const TSharedPtr<SSMGraphProperty_Base> PropertyWidgetPin = ParentGraphPropertyWidget.Pin())
		{
			PropertyWidgetPin->InteractWithProperty(ESMPropertyInteractionType::UnsetSelect);
		}
	}
	
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OpenContextMenu(MouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}
	
	return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
}

void SSMGraphProperty_Connector::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (ParentGraphPropertyWidget.IsValid())
	{
		ParentGraphPropertyWidget.Pin()->InteractWithProperty(ESMPropertyInteractionType::SetHover);
	}
}

void SSMGraphProperty_Connector::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	if (ParentGraphPropertyWidget.IsValid())
	{
		ParentGraphPropertyWidget.Pin()->InteractWithProperty(ESMPropertyInteractionType::UnsetHover);
	}
}

void SSMGraphProperty_Connector::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);
	if (ParentGraphPropertyWidget.IsValid()
		&& FSMDragDropHelpers::IsDragDropEventBPVariableOrFunction(DragDropEvent)
		&& FSMDragDropHelpers::IsDragDropValidForPropertyNode(OwningPropertyNode.Get(), DragDropEvent))
	{
		ParentGraphPropertyWidget.Pin()->InteractWithProperty(ESMPropertyInteractionType::SetHover);
	}
}

void SSMGraphProperty_Connector::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragLeave(DragDropEvent);
	if (ParentGraphPropertyWidget.IsValid()
		&& FSMDragDropHelpers::IsDragDropEventBPVariableOrFunction(DragDropEvent)
		&& FSMDragDropHelpers::IsDragDropValidForPropertyNode(OwningPropertyNode.Get(), DragDropEvent))
	{
		ParentGraphPropertyWidget.Pin()->InteractWithProperty(ESMPropertyInteractionType::UnsetHover);
	}
}

bool SSMGraphProperty_Connector::IsHovered() const
{
	const TWeakPtr<SGraphPanel> ScopedPanel = GetPropertyInteractionManager().GetScopedGraphPanel();
	return (
		ParentGraphPropertyWidget.IsValid() && ParentStateWidget.IsValid()
		&& ParentGraphPropertyWidget.Pin()->IsCategoryExpanded())
		// Hovering only matters if its over our graph panel.
		&& (!ScopedPanel.IsValid() || ScopedPanel == ParentStateWidget.Pin()->GetOwnerPanel())
		&& ((CurrentInteractionData.IsValid() && CurrentInteractionData.Pin()->bHovered)
		|| SCompoundWidget::IsHovered()
		|| (ParentGraphPropertyWidget.Pin()->IsHovered())
		|| bIsComboBoxOpened);
}

bool SSMGraphProperty_Connector::IsSelected() const
{
	return (CurrentInteractionData.IsValid() && CurrentInteractionData.Pin()->bSelected);
}

void SSMGraphProperty_Connector::FreezeDisplayModeUntilVisibilityChange(bool bValue)
{
	bFreezeDisplayMode = bValue;
}

EEdGraphPinDirection SSMGraphProperty_Connector::GetConnectorDirection() const
{
	check(PrimaryResultGraphPin);
	return PrimaryResultGraphPin->Direction.GetValue();
}

bool SSMGraphProperty_Connector::IsInCompactMode() const
{
	if (!bFreezeDisplayMode)
	{
		bool bIsDraggingFromUs = false;
		if (FSlateApplication::Get().IsDragDropping()
			&& FSlateApplication::Get().GetDragDroppingContent()->IsOfType<FSMIODragDropOp>())
		{
			const TSharedPtr<FSMIODragDropOp> OurDragDropOp = StaticCastSharedPtr<FSMIODragDropOp>(FSlateApplication::Get().GetDragDroppingContent());

			if (const TSharedPtr<SSMGraphProperty_ConnectorPin> ConnectorPin = OurDragDropOp->GetOwningConnectorPin().Pin())
			{
				if (const TSharedPtr<SSMGraphProperty_Connector> OwningConnector = ConnectorPin->GetOwningConnector().Pin())
				{
					// User initiated a drag from us or our opposite direction (for bidirectional variables).
					// Always collapse so we don't get in the way if the states are close to each other.
					bIsDraggingFromUs = OwningConnector->GetOwningPropertyNode() == GetOwningPropertyNode();
				}
			}
		}
		
		bInCompactMode =
			bIsDraggingFromUs
			|| (!IsSelected()
			// If this is externally managed we can only show the compact mode for room.
			|| (CurrentInteractionData.IsValid() && CurrentInteractionData.Pin()->IsSelectedExternally())
			// If dragging owning BP items, just display the compact mode.
			// @todo output variables full connector drop - consider removing this and allowing precise drops in non-compact mode.
			|| (FSlateApplication::Get().IsDragDropping()
				&& (FSlateApplication::Get().GetDragDroppingContent()->IsOfType<FKismetFunctionDragDropAction>()
				|| FSlateApplication::Get().GetDragDroppingContent()->IsOfType<FKismetVariableDragDropAction>())));

		if (bInCompactMode != bWasInCompactMode)
		{
			bWasInCompactMode = bInCompactMode;
			const_cast<SSMGraphProperty_Connector*>(this)->NotifyModeChange();
		}
	}
	
	return bInCompactMode;
}

bool SSMGraphProperty_Connector::IsInteractedWith() const
{
	if (IsHovered())
	{
		return true;
	}

	TSharedPtr<FSMPropertyInteractionData> InteractionData;
	if (GetPropertyInteractionManager().IsPropertyInteractedWith(GetOwningPropertyNode(), &InteractionData))
	{
		if (!InteractionData->bSelected)
		{
			// Verify this specific connector is interacted with.
			return IsForcedToDisplayForInteraction(*InteractionData);
		}
		// Need to check if we truly are interacted within the scoped panel, if it is set.
		const TWeakPtr<SGraphPanel> FocusedPanel = GetPropertyInteractionManager().GetScopedGraphPanel();
		return !FocusedPanel.IsValid() || FocusedPanel == ParentStateWidget.Pin()->GetOwnerPanel();
	}

	return false;
}

void SSMGraphProperty_Connector::OnInteractionChange(ESMPropertyInteractionType InteractionType)
{
	if (const USMGraphK2Node_PropertyNode_Base* Property = GetOwningPropertyNode())
	{
		CurrentInteractionData = GetPropertyInteractionManager().GetPropertyInteractionData(Property);
		if (const TSharedPtr<FSMPropertyInteractionData> CurrentInteractionDataPin = CurrentInteractionData.Pin())
		{
			if (!LastInteractionData.IsValid()
				|| (CurrentInteractionDataPin != LastInteractionData))
			{
				NotifyModeChange();
			}
			
			LastInteractionData = MakeShared<FSMPropertyInteractionData>(*CurrentInteractionData.Pin());
		}
		else
		{
			LastInteractionData.Reset();
		}
	}
}

void SSMGraphProperty_Connector::NotifyModeChange()
{
	// Invalidating only the prepass when we change to or from compact mode, or adjust visibility, can greatly smooth out
	// the user experience. Otherwise, there can be flickering as the connector repositions itself, based on its
	// own geometry and graph property geometry.
	Invalidate(EInvalidateWidget::Prepass);
}

void SSMGraphProperty_Connector::CreateInputPin()
{
	check(GetConnectorDirection() == EGPD_Input);
	check(PinContainer->NumSlots() == 0);

	SelectOwningProperty();
	bool bJustCreated = false;
	if (USMGraphK2Node_PropertyIOReaderNode* IOReaderNode = GetIOReaderNode(true, &bJustCreated))
	{
		UpdatePins();
	}
}

void SSMGraphProperty_Connector::AddOutputPin()
{
	check(GetConnectorDirection() == EGPD_Output);
	
	SelectOwningProperty();
	bool bJustCreated = false;
	if (USMGraphK2Node_PropertyIOWriterNode* IOWriterNode = GetIOWriterNode(true, &bJustCreated))
	{
		// Input pin is added by default on new nodes.
		if (!bJustCreated)
		{
			IOWriterNode->AddInputPin();
		}
	}
}

void SSMGraphProperty_Connector::RemoveOutputPin(UEdGraphPin* InPin)
{
	check(GetConnectorDirection() == EGPD_Output);
	
	SelectOwningProperty();
	if (USMGraphK2Node_PropertyIOWriterNode* IOWriterNode = GetIOWriterNode())
	{
		IOWriterNode->RemoveInputPin(InPin);
	}
}

void SSMGraphProperty_Connector::RemoveAllOutputPins()
{
	check(GetConnectorDirection() == EGPD_Output);
	
	if (USMGraphK2Node_PropertyIOWriterNode* IOWriterNode = GetIOWriterNode())
	{
		IOWriterNode->RemoveAllPins();
	}
}

void SSMGraphProperty_Connector::ResetToDefaultValue(UEdGraphPin* InPin)
{
	check(InPin);
	const FScopedTransaction Transaction(LOCTEXT("ResetPinToDefaultValue", "Reset Pin To Default Value"));

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	InPin->Modify();
	K2Schema->ResetPinToAutogeneratedDefaultValue(InPin);
}

bool SSMGraphProperty_Connector::CanResetToDefaultValue(UEdGraphPin* InPin)
{
	check(InPin);
	return !InPin->DoesDefaultValueMatchAutogenerated();
}

EVisibility SSMGraphProperty_Connector::GetConnectorVisibility() const
{
	EVisibility Visibility;
	if (
		const FSMPropertyInteractionManager& InteractionManager = GetPropertyInteractionManager();
		// We were hidden last frame and still aren't interacted with, or another connector is fully visible
		// (helps with flickering and overlap, when we are displaying all pins in compact mode.
		(!bLastVisible && (!IsInteractedWith()
			|| IsAnotherPropertyConnectorFullyVisible()))
		// There is another selected property.
		|| (InteractionManager.GetSelectedProperty(true) && CurrentInteractionData.IsValid() && !CurrentInteractionData.Pin()->bSelected)
		// If we couldn't locate the correct pin to display.
		|| (IsInCompactMode() && (!CompactConnectorPin.IsValid() || CompactConnectorPin->GetPropertyPin() == nullptr))
		// Our parent property isn't valid or is collapsed.
		|| (!ParentGraphPropertyWidget.IsValid() || !ParentGraphPropertyWidget.Pin()->IsCategoryExpanded()
		// If we are drag dropping an operation we don't support (such as a transition) we don't want to get in the way.
		|| (FSlateApplication::Get().IsDragDropping()
			&& !(FSlateApplication::Get().GetDragDroppingContent()->IsOfType<FKismetFunctionDragDropAction>()
			|| FSlateApplication::Get().GetDragDroppingContent()->IsOfType<FKismetVariableDragDropAction>()
			|| FSlateApplication::Get().GetDragDroppingContent()->IsOfType<FSMIODragDropOp>())))
		)
	{
		Visibility = EVisibility::Collapsed;
	}
	else
	{
		Visibility = EVisibility::Visible;
	}

	const bool bNewVisible = Visibility == EVisibility::Visible;
	if (bNewVisible != bLastVisible)
	{
		const_cast<SSMGraphProperty_Connector*>(this)->OnConnectorVisibilityChange(bNewVisible);
	}

	return Visibility;
}

bool SSMGraphProperty_Connector::CanConnectorBeModified() const
{
	return OwningPropertyNode.IsValid() && !OwningPropertyNode->IsGraphReadOnly();
}

USMGraphK2Node_PropertyNode_Base* SSMGraphProperty_Connector::GetOwningPropertyNode() const
{
	return OwningPropertyNode.Get();
}

USMGraphK2Node_PropertyNode_Base* SSMGraphProperty_Connector::GetOwningPropertyNodeChecked() const
{
	USMGraphK2Node_PropertyNode_Base* PropertyNode = GetOwningPropertyNode();
	check(PropertyNode);
	return PropertyNode;
}

USMGraphNode_StateNodeBase* SSMGraphProperty_Connector::GetOwningStateNode() const
{
	if (const USMGraphK2Node_PropertyNode_Base* Property = GetOwningPropertyNode())
	{
		if (USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Property->GetOwningGraphNode()))
		{
			return StateNode;
		}
	}

	return nullptr;
}

USMGraphNode_StateNodeBase* SSMGraphProperty_Connector::GetOwningStateNodeChecked() const
{
	USMGraphNode_StateNodeBase* StateNode = GetOwningStateNode();
	check(StateNode);
	return StateNode;
}

void SSMGraphProperty_Connector::UpdatePins()
{
	CreateNestedPins();

	if (CompactConnectorPin.IsValid())
	{
		const UEdGraphPin* PropertyPin = FindBestPropertyPinForDisplay();
		CompactConnectorPin->SetPropertyPin(PropertyPin);
	}
}

void SSMGraphProperty_Connector::CreateNestedPins()
{
	PinContainer->ClearChildren();

	const EEdGraphPinDirection Direction = GetConnectorDirection();
	
	if (ParentStateWidget.IsValid()) // Could be null if triggered from a graph refresh before this has been cleaned up.
	{
		if (Direction == EGPD_Output)
		{
			if (const USMGraphK2Node_PropertyIOWriterNode* WriterNode = GetIOWriterNode())
			{
				const TArray<UEdGraphPin*> Outputs = WriterNode->GetAllPropertyOutputs();
				for (UEdGraphPin* Pin : Outputs)
				{
					PinContainer->AddSlot()
					.HAlign(HAlign_Right)
					.AutoHeight()
					[
						ConstructOutputPin(Pin)
					];
				}
			}
		}
		else if (Direction == EGPD_Input)
		{
			if (const USMGraphK2Node_PropertyIOReaderNode* ReaderNode = GetIOReaderNode())
			{
				if (UEdGraphPin* InputPin = ReaderNode->GetPropertyPinIn())
				{
					PinContainer->AddSlot()
					.HAlign(HAlign_Right)
					.AutoHeight()
					[
						ConstructInputPin(InputPin)
					];
				}
			}
		}
	}
}

void SSMGraphProperty_Connector::ClearNestedPins()
{
	PinContainer.Reset();
}

void SSMGraphProperty_Connector::OpenContextMenu(const FVector2D& ScreenSpacePosition)
{
	if (GetConnectorDirection() == EGPD_Output)
	{
		FMenuBuilder MenuBuilder(true, nullptr);

		AddRemoveAllOutputsContextButton(MenuBuilder);
	
		FSlateApplication::Get().PushMenu(
			SharedThis(this),
			FWidgetPath(),
			MenuBuilder.MakeWidget(),
			ScreenSpacePosition,
			FPopupTransitionEffect::ContextMenu
		);
	}
}

void SSMGraphProperty_Connector::AddRemoveAllOutputsContextButton(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.AddMenuEntry(
		LOCTEXT("PropertyContextMenuRemoveAllOutputsPin", "Remove All Outputs"),
		LOCTEXT("PropertyContextMenuRemoveAllOutputsPin_Tooltip", "Remove all property outputs."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
		FUIAction(FExecuteAction::CreateSP(this, &SSMGraphProperty_Connector::RemoveAllOutputPins))
	);
}

TSharedPtr<SWidget> SSMGraphProperty_Connector::OnPropertyContextMenuOpening(UEdGraphPin* InPropertyPin)
{
	if (InPropertyPin)
	{
		if (InPropertyPin->Direction == EGPD_Output)
		{
			FMenuBuilder MenuBuilder(true, nullptr);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("PropertyContextMenuRemoveOutputPin", "Remove Output"),
				LOCTEXT("PropertyContextMenuRemoveOutputPin_Tooltip", "Remove this output"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
				FUIAction(FExecuteAction::CreateSP(this, &SSMGraphProperty_Connector::RemoveOutputPin, InPropertyPin))
			);
		
			AddRemoveAllOutputsContextButton(MenuBuilder);

			return MenuBuilder.MakeWidget();
		}
		if (InPropertyPin->Direction == EGPD_Input)
		{
			FMenuBuilder MenuBuilder(true, nullptr);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("PropertyContextMenuResetDefaults", "Reset to Default Value"),
				LOCTEXT("PropertyContextMenuResetDefaults_Tooltip", "Reset value of this pin to the default"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SSMGraphProperty_Connector::ResetToDefaultValue, InPropertyPin),
					FCanExecuteAction::CreateSP(this, &SSMGraphProperty_Connector::CanResetToDefaultValue, InPropertyPin))
			);

			return MenuBuilder.MakeWidget();
		}
	}

	return nullptr;
}

TSharedRef<SWidget> SSMGraphProperty_Connector::ConstructInputPin(UEdGraphPin* InInputPin)
{
	check(InInputPin && InInputPin->Direction == EGPD_Input);
	
	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Center)
	[
		ConstructGraphConnectionPin(InInputPin)
	]
	+ SHorizontalBox::Slot()
	.AutoWidth()
	[
		ConstructPropertySelectionPin(InInputPin)
	];
}

TSharedRef<SWidget> SSMGraphProperty_Connector::ConstructOutputPin(UEdGraphPin* InOutputPin)
{
	check(InOutputPin && InOutputPin->Direction == EGPD_Output);

	SBox::FArguments InterceptorBoxArgs;
	InterceptorBoxArgs.HAlign(HAlign_Fill);
	InterceptorBoxArgs.VAlign(VAlign_Fill);
	return
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ConstructPropertySelectionPin(InOutputPin)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				ConstructGraphConnectionPin(InOutputPin)
			]
		]
		// Detect all drag-drops over this entire pin
		+ SOverlay::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		[
			SNew(SSMDragDropInterceptor)
			.InterceptType(SSMDragDropInterceptor::EInterceptType::OwningBPFunction | SSMDragDropInterceptor::EInterceptType::OwningBPVariable)
			.OnDrop(this, &SSMGraphProperty_Connector::OnPinDropFromMyBP, const_cast<const UEdGraphPin*>(InOutputPin))
			.OnDragOver(this, &SSMGraphProperty_Connector::OnPinDragOverFromMyBP, const_cast<const UEdGraphPin*>(InOutputPin))
			.OnDragLeave(this, &SSMGraphProperty_Connector::OnPinDragLeaveFromMyBP)
		];
}

TSharedRef<SGraphPin_PropertySelectionPin> SSMGraphProperty_Connector::ConstructPropertySelectionPin(
	UEdGraphPin* InPin)
{
	check(ParentStateWidget.IsValid());
	
	const TSharedPtr<SGraphPin_PropertySelectionPin> PropertySelectionPin = SNew(SGraphPin_PropertySelectionPin, InPin)
	.PropertySelectionDirection(InPin->Direction)
	.OnPropertySelectionOpenChanged(this, &SSMGraphProperty_Connector::OnPropertySelectionOpenChanged, InPin)
	.OnContextMenuOpening(this, &SSMGraphProperty_Connector::OnPropertyContextMenuOpening, InPin)
	.OnMouseButtonDown(this, &SSMGraphProperty_Connector::OnMouseButtonDown)
	.CanDrawArrows(false)
	.DisplayPinTooltips(false);
	PropertySelectionPin->SetOwner(ParentStateWidget.Pin().ToSharedRef());

	return PropertySelectionPin.ToSharedRef();
}

TSharedRef<SSMGraphProperty_ConnectorPin> SSMGraphProperty_Connector::ConstructGraphConnectionPin(const UEdGraphPin* InPropertyPin)
{
	check(ParentStateWidget.IsValid() && PrimaryResultGraphPin);
	const TSharedPtr<SSMGraphProperty_ConnectorPin> NewPin = SNew(SSMGraphProperty_ConnectorPin, PrimaryResultGraphPin, InPropertyPin)
		.OwningConnector(StaticCastSharedRef<SSMGraphProperty_Connector>(AsShared()))
		.OnPinClicked_Lambda([this](const FGeometry& SenderGeometry, const FPointerEvent& MouseEvent)
		{
			SelectOwningProperty();
		});
	NewPin->SetOwner(ParentStateWidget.Pin().ToSharedRef());
	return NewPin.ToSharedRef();
}

TSharedRef<SWidget> SSMGraphProperty_Connector::ConstructCompactWidget()
{
	// Find the best property pin to use.
	const UEdGraphPin* PropertyPin = FindBestPropertyPinForDisplay();
	CompactConnectorPin = ConstructGraphConnectionPin(PropertyPin);
	CompactConnectorPin->SetCursor(EMouseCursor::Hand); // Helps indicate this can be clicked on.
	
	SBox::FArguments InterceptorBoxArgs;
	InterceptorBoxArgs.HAlign(HAlign_Fill);
	InterceptorBoxArgs.VAlign(VAlign_Fill);
	
	return
		SNew(SBorder)
		.Cursor(EMouseCursor::Hand) // Helps indicate this can be clicked on.
		.Visibility(this, &SSMGraphProperty_Connector::GetDisplayModeVisibility, true)
		.BorderImage(FSMUnrealAppStyle::Get().GetBrush("Graph.StateNode.ColorSpill"))
		.BorderBackgroundColor(this, &SSMGraphProperty_Connector::GetStateBackgroundColor)
		.Padding(3.f)
		[
			SNew(SBorder)
			.BorderImage(FSMUnrealAppStyle::Get().GetBrush("Graph.StateNode.ColorSpill"))
			.BorderBackgroundColor(this, &SSMGraphProperty_Connector::GetPropertyBackgroundColor)
			.Padding(1.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						CompactConnectorPin.ToSharedRef()
					]
					// Detect all drag-drops over this entire pin
					+ SOverlay::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					[
						SNew(SSMDragDropInterceptor)
						.InterceptType(SSMDragDropInterceptor::EInterceptType::OwningBPFunction | SSMDragDropInterceptor::EInterceptType::OwningBPVariable)
						.OnDrop(this, &SSMGraphProperty_Connector::OnPinDropFromMyBP, PropertyPin)
						.OnDragOver(this, &SSMGraphProperty_Connector::OnPinDragOverFromMyBP, PropertyPin)
						.OnDragLeave(this, &SSMGraphProperty_Connector::OnPinDragLeaveFromMyBP)
					]
				]
			]
		];
}

TSharedRef<SWidget> SSMGraphProperty_Connector::ConstructFullDisplayWidget()
{
	const USMGraphK2Node_PropertyNode_Base* PropertyNode = GetOwningPropertyNodeChecked();
	
	return
		SNew(SBorder)
		.Visibility(this, &SSMGraphProperty_Connector::GetDisplayModeVisibility, false)
		.BorderImage(FSMUnrealAppStyle::Get().GetBrush("Brushes.Panel"))
		.BorderBackgroundColor(this, &SSMGraphProperty_Connector::GetPropertyBackgroundColor)
		.Padding(2)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FSMUnrealAppStyle::Get().GetBrush("Graph.Node.TitleBackground"))
				.BorderBackgroundColor(PropertyBackgroundColor)
				.Padding(2)
				[
					SNew(STextBlock)
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("NormalFontBold"))
					.Text(PropertyNode->GetPropertyNodeConstChecked()->GetDisplayName())
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(PinContainer, SVerticalBox)
			]
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.Padding(TAttribute<FMargin>::CreateSP(this, &SSMGraphProperty_Connector::GetModifyPinsPadding))
			[
				SNew(SButton)
				.Visibility(this, &SSMGraphProperty_Connector::GetModifyPinsVisibility)
				.ButtonStyle(FSMUnrealAppStyle::Get(), "SimpleButton")
				.Text(this, &SSMGraphProperty_Connector::GetModifyPinsText)
				.ToolTipText(this, &SSMGraphProperty_Connector::GetModifyPinsTooltipText)
				.OnClicked(this, &SSMGraphProperty_Connector::OnModifyPinsClicked)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(TEXT("Icons.PlusCircle")))
				]
			]
		];
}

void SSMGraphProperty_Connector::SelectOwningProperty()
{
	if (const TSharedPtr<SSMGraphProperty_Base> PropertyWidgetPin = ParentGraphPropertyWidget.Pin())
	{
		PropertyWidgetPin->InteractWithProperty(ESMPropertyInteractionType::SetSelect);
	}
}

bool SSMGraphProperty_Connector::IsAnotherPropertyConnectorFullyVisible() const
{
	if (const TSharedPtr<SGraphNode_StateNode> ParentWidgetPin = ParentStateWidget.Pin())
	{
		const TMap<FGuid, SGraphNode_StateNode::FExternalOverlayWidget>& ExternalOverlayWidgets = ParentWidgetPin->
			GetExternalOverlayWidgets();

		for (const TTuple<FGuid, SGraphNode_StateNode::FExternalOverlayWidget>& ExternalOverlayWidget : ExternalOverlayWidgets)
		{
			if (ExternalOverlayWidget.Value.Widget.IsValid()
				&& ExternalOverlayWidget.Value.Widget.Get()->GetType() == TEXT("SSMGraphProperty_Connector"))
			{
				const TSharedPtr<SSMGraphProperty_Connector> Connector = StaticCastSharedPtr<SSMGraphProperty_Connector>(
					ExternalOverlayWidget.Value.Widget);
				
				if (Connector->GetOwningPropertyNode() != GetOwningPropertyNode()
					&& Connector->GetConnectorLastVisible()
					&& !Connector->IsInCompactMode())
				{
					// This is another property, that is visible and not in compact mode.
					return true;
				}
			}
		}
	}

	return false;
}

bool SSMGraphProperty_Connector::CanUserModifyPins() const
{
	return GetConnectorDirection() == EGPD_Output || PinContainer->NumSlots() == 0;
}

FMargin SSMGraphProperty_Connector::GetModifyPinsPadding() const
{
	return GetConnectorDirection() == EGPD_Output
		? FMargin(7, 0, 0, 2)
		: FMargin(0, 2, 0, 2);
}

FText SSMGraphProperty_Connector::GetModifyPinsText() const
{
	return GetConnectorDirection() == EGPD_Output
		? LOCTEXT("AddOutputPin", "Add Output")
		: LOCTEXT("CreateInputPin", "Create Input");
}

FText SSMGraphProperty_Connector::GetModifyPinsTooltipText() const
{
	return GetConnectorDirection() == EGPD_Output
		? LOCTEXT("AddOutputPin_Tooltip", "Add an output reference to the property")
		: LOCTEXT("CreateInputPin_Tooltip", "Create an input reference, allowing connections to output properties");
}

FReply SSMGraphProperty_Connector::OnModifyPinsClicked()
{
	if (GetConnectorDirection() == EGPD_Output)
	{
		AddOutputPin();
	}
	else if (PinContainer->NumSlots() == 0)
	{
		CreateInputPin();
	}
	else
	{
		checkNoEntry();
	}
	return FReply::Handled();
}

EVisibility SSMGraphProperty_Connector::GetHighlightBorderVisibility() const
{
	return IsSelected() || IsHovered() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

EVisibility SSMGraphProperty_Connector::GetDisplayModeVisibility(bool bIsCompact) const
{
	if (bIsCompact)
	{
		return IsInCompactMode() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
	}
	return IsInCompactMode() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
}

FSlateColor SSMGraphProperty_Connector::GetHighlightColor() const
{
	if (IsSelected() && IsHovered())
	{
		return FSMPropertyInteractionManager::PropertySelectedAndHoveredColor;
	}
	return IsSelected() ? FSMPropertyInteractionManager::PropertySelectedColor : FSMPropertyInteractionManager::PropertyHoveredColor;
}

FSlateColor SSMGraphProperty_Connector::GetStateBackgroundColor() const
{
	if (OwningPropertyNode.IsValid())
	{
		const USMNodeInstance* OwningTemplate = OwningPropertyNode->GetOwningTemplate();
		const USMGraphNode_StateNodeBase* StateNode = GetOwningStateNodeChecked();
		FLinearColor BackgroundColor = StateNode->GetBackgroundColorForNodeInstance(OwningTemplate);
		BackgroundColor.A = 0.9f;
		return BackgroundColor;
	}

	return FLinearColor::Black;
}

FSlateColor SSMGraphProperty_Connector::GetPropertyBackgroundColor() const
{
	if (OwningPropertyNode.IsValid())
	{
		FLinearColor BackgroundColor = OwningPropertyNode->GetPropertyNodeChecked()->GetWidgetInfo()->BackgroundColor;
		BackgroundColor.A = 0.8f;
		return BackgroundColor;
	}

	return FLinearColor::Black;
}

FVector2f SSMGraphProperty_Connector::GetOffset(const FVector2f& InStateSize) const
{
	if (ParentStateWidget.IsValid() && ParentGraphPropertyWidget.IsValid())
	{
		const TSharedPtr<SGraphNode> GraphNodeWidgetPtr = ParentStateWidget.Pin();
		const TSharedPtr<SSMGraphProperty_Base> PropertyNodeWidgetPtr = ParentGraphPropertyWidget.Pin();

		const FGeometry ParentGeometry = GraphNodeWidgetPtr->GetTickSpaceGeometry();
		const FGeometry ChildGeometry = PropertyNodeWidgetPtr->GetTickSpaceGeometry();

		if (NeedsPrepass())
		{
			// Force a prepass so we can get the right size, otherwise the connector displays in the wrong place
			// for a frame.
			const_cast<SSMGraphProperty_Connector*>(this)->SlatePrepass(GetPrepassLayoutScaleMultiplier());
		}
		
		const FVector2D ChildAbsolutePosition = ChildGeometry.GetAbsolutePosition();
		const FVector2D RelativePosition = ParentGeometry.AbsoluteToLocal(ChildAbsolutePosition);

		const UE::Math::TVector2<double> GraphNodeSize = GraphNodeWidgetPtr->GetDesiredSize();
		const UE::Math::TVector2<double> PropertySize = PropertyNodeWidgetPtr->GetDesiredSize();

		constexpr float XOffset = 16.f;
		constexpr float LeftXPadding = 8.f; // Padding between left of node and when property starts.
		
		float XPos = XOffset;
		switch (GetConnectorDirection())
		{
			case EGPD_Input:
			{
				XPos = -GetDesiredSize().X + (XOffset * 2.f);
				break;
			}
			case EGPD_Output:
			{
				XPos = GraphNodeSize.X - XOffset + LeftXPadding;
				break;
			}
			default:
			{
				checkNoEntry();
				break;
			}
		}
		
		return FVector2f(XPos, (RelativePosition.Y + PropertySize.Y / 2) - (GetDesiredSize().Y / 2));
	}

	return FVector2f::Zero();
}

USMGraphK2Node_PropertyIOReaderNode* SSMGraphProperty_Connector::GetIOReaderNode(bool bCreateIfNeeded, bool* bJustCreated) const
{
	if (OwningPropertyNode.IsValid())
	{
		if (USMGraphK2Node_PropertyIOReaderNode* ReaderNode = bCreateIfNeeded ?
			LD::Editor::PropertyUtils::IO::GetOrPlacePropertyReaderNode(OwningPropertyNode.Get(), bJustCreated)
				: LD::Editor::PropertyUtils::IO::GetConnectedPropertyReaderNode(OwningPropertyNode.Get()))
		{
			return ReaderNode;
		}
	}
	
	if (PropertyGraph.IsValid())
	{
		return FSMBlueprintEditorUtils::GetFirstNodeOfClassNested<USMGraphK2Node_PropertyIOReaderNode>(PropertyGraph.Get());
	}
	return nullptr;
}

USMGraphK2Node_PropertyIOWriterNode* SSMGraphProperty_Connector::GetIOWriterNode(const bool bCreateIfNeeded, bool* bJustCreated) const
{
	if (OwningPropertyNode.IsValid())
	{
		if (USMGraphK2Node_PropertyIOWriterNode* WriterNode = bCreateIfNeeded ?
			LD::Editor::PropertyUtils::IO::GetOrPlacePropertyWriterNode(OwningPropertyNode.Get(), bJustCreated)
				: LD::Editor::PropertyUtils::IO::GetConnectedPropertyWriterNode(OwningPropertyNode.Get()))
		{
			return WriterNode;
		}
	}
	
	if (PropertyGraph.IsValid())
	{
		return FSMBlueprintEditorUtils::GetFirstNodeOfClassNested<USMGraphK2Node_PropertyIOWriterNode>(PropertyGraph.Get());
	}
	return nullptr;
}

ISMStateMachineBlueprintEditor* SSMGraphProperty_Connector::GetOwningBlueprintEditor() const
{
	return FSMBlueprintEditorUtils::GetStateMachineEditor(PropertyGraph.Get());
}

FSMPropertyInteractionManager& SSMGraphProperty_Connector::GetPropertyInteractionManager() const
{
	return GetOwningStateNodeChecked()->GetPropertyInteractionManager();
}

void SSMGraphProperty_Connector::OnConnectorVisibilityChange(bool bNewVisibility)
{
	FreezeDisplayModeUntilVisibilityChange(false);
	bLastVisible = bNewVisibility;
}

void SSMGraphProperty_Connector::OnGraphChanged(const FEdGraphEditAction& Action)
{
	UpdatePins();
}

void SSMGraphProperty_Connector::OnPropertySelectionOpenChanged(bool bOpen, UEdGraphPin* InPin)
{
	bIsComboBoxOpened = bOpen;
	if (bOpen)
	{
		SelectOwningProperty();
	}
}

UEdGraphPin* SSMGraphProperty_Connector::FindBestPropertyPinForDisplay() const
{
	UEdGraphPin* PropertyPin = nullptr;
	const EEdGraphPinDirection Direction = GetConnectorDirection(); 
	if (Direction == EGPD_Output)
	{
		if (const USMGraphK2Node_PropertyIOWriterNode* WriterNode = GetIOWriterNode())
		{
			const TArray<UEdGraphPin*> Outputs = WriterNode->GetAllPropertyOutputs();
			for (UEdGraphPin* Pin : Outputs)
			{
				if (Pin && !Pin->bWasTrashed)
				{
					if (!Pin->DefaultValue.IsEmpty())
					{
						PropertyPin = Pin;
						break;
					}
					PropertyPin = Pin;
				}
			}
		}
	}
	else if (Direction == EGPD_Input)
	{
		if (const USMGraphK2Node_PropertyIOReaderNode* ReaderNode = GetIOReaderNode())
		{
			PropertyPin = ReaderNode->GetPropertyPinIn();
		}
	}

	if (!PropertyPin)
	{
		PropertyPin = OwningPropertyNode->GetResultPin(Direction);
	}
	
	return PropertyPin;
}

bool SSMGraphProperty_Connector::IsForcedToDisplayForInteraction(const FSMPropertyInteractionData& InInteractionData) const
{
	const bool bPassInput = InInteractionData.bForceDisplayInputConnectors && GetConnectorDirection() == EGPD_Input;
	const bool bPassOutput = InInteractionData.bForceDisplayOutputConnectors && GetConnectorDirection() == EGPD_Output;
		
	return bPassInput || bPassOutput;
}

FReply SSMGraphProperty_Connector::OnPinDropFromMyBP(const FGeometry& InMyGeometry, const FDragDropEvent& InDragDropEvent,
	const UEdGraphPin* InPin)
{
	ensure(FSMDragDropHelpers::IsDragDropEventBPVariableOrFunction(InDragDropEvent)); // IO handled in SMIODragDropOp
	if (FSMDragDropHelpers::IsDragDropValidForPropertyNode(OwningPropertyNode.Get(), InDragDropEvent))
	{
		if (const TSharedPtr<SSMGraphProperty_Base> Parent = ParentGraphPropertyWidget.Pin())
		{
			Parent->OnDirectionalDrop(InMyGeometry, InDragDropEvent, GetConnectorDirection());
		}
	}
	return FReply::Handled();
}

FReply SSMGraphProperty_Connector::OnPinDragOverFromMyBP(const FGeometry& InMyGeometry, const FDragDropEvent& InDragDropEvent,
                                                 const UEdGraphPin* InPin)
{
	ensure(FSMDragDropHelpers::IsDragDropEventBPVariableOrFunction(InDragDropEvent)); // IO handled in SMIODragDropOp
	const bool bSuccess = FSMDragDropHelpers::IsDragDropValidForPropertyNode(OwningPropertyNode.Get(), InDragDropEvent);
	FSMDragDropHelpers::SetDragDropMessage(InDragDropEvent, bSuccess);
	return FReply::Handled();
}

void SSMGraphProperty_Connector::OnPinDragLeaveFromMyBP(const FDragDropEvent& InDragDropEvent)
{
	ensure(FSMDragDropHelpers::IsDragDropEventBPVariableOrFunction(InDragDropEvent)); // IO handled in SMIODragDropOp
	FSMDragDropHelpers::SetDragDropMessage(InDragDropEvent, false);	
}

#undef LOCTEXT_NAMESPACE
