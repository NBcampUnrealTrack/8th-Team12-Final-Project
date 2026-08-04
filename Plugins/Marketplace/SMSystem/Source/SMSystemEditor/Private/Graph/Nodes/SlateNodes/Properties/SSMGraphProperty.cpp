// Copyright Recursoft LLC. All Rights Reserved.

#include "SSMGraphProperty.h"

#include "Configuration/SMEditorSettings.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_GraphPropertyNode.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SlateNodes/SGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/SMPropertyGraph.h"
#include "Helpers/SMDragDropHelpers.h"
#include "Helpers/SSMDragDropInterceptor.h"
#include "Properties/PropertyInteraction/SMPropertyInteractionManager.h"
#include "Properties/SMEditorPropertyUtils.h"
#include "SSMGraphProperty_Connector.h"
#include "Utilities/SMBlueprintEditorUtils.h"
#include "Widgets/Kismet/SSMKismetLinearExpression.h"

#include "Blueprints/SMBlueprint.h"

#include "BPVariableDragDropAction.h"
#include "Editor/Kismet/Private/BPFunctionDragDropAction.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "NodeFactory.h"
#include "SGraphPin.h"
#include "SMUnrealTypeDefs.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "SSMGraphProperty"

SSMGraphProperty::SSMGraphProperty() : bIsValidDragDrop(false)
{
}

SSMGraphProperty::~SSMGraphProperty()
{
	if (USMGraphK2Node_GraphPropertyNode* Node = GetGraphNodeAs<USMGraphK2Node_GraphPropertyNode>())
	{
		Node->ForceVisualRefreshEvent.RemoveAll(this);
		Node->GetOnPropertyInteractionChangedEvent().RemoveAll(this);
	}
}

void SSMGraphProperty::Construct(const FArguments& InArgs)
{
	GraphNode = InArgs._GraphNode;
	WidgetInfo = InArgs._WidgetInfo ? *InArgs._WidgetInfo : FSMTextDisplayWidgetInfo();
	OwningBlueprint = Cast<USMBlueprint>(FBlueprintEditorUtils::FindBlueprintForNode(GraphNode.Get()));
	
	FText DefaultText = WidgetInfo.DefaultText;

	if (USMGraphK2Node_GraphPropertyNode* Node = GetGraphNodeAs<USMGraphK2Node_GraphPropertyNode>())
	{
		Node->ForceVisualRefreshEvent.AddSP(this, &SSMGraphProperty::Refresh);
		Node->GetOnPropertyInteractionChangedEvent().AddSP(this, &SSMGraphProperty::OnInteractProperty);
		
		if (DefaultText.IsEmpty())
		{
			DefaultText = Node->GetPropertyNodeChecked()->GetDisplayName();
		}

		if (Node->GetPropertyNodeChecked()->bIsInArray)
		{
			DefaultText = LD::Editor::PropertyUtils::GetPropertyDisplayName(DefaultText, Node->GetPropertyNodeChecked()->ArrayIndex);
		}

		SetToolTipText(BuildPropertyTooltipText());
	}

	ChildSlot
	[
		SNew(SOverlay)
		.IsEnabled(this, &SSMGraphProperty::IsGraphPropertyVisuallyEnabled)
		+SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FSMUnrealAppStyle::Get().GetBrush("Graph.StateNode.ColorSpill"))
			.BorderBackgroundColor(this, &SSMGraphProperty::GetBackgroundColor)
			.Padding(1.f)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(NotifyPadding)
				[
					MakeNotifyIconWidget()
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				[
					SNew(SBox)
					.MinDesiredWidth(WidgetInfo.MinWidth)
					.MaxDesiredWidth(WidgetInfo.MaxWidth)
					.MinDesiredHeight(WidgetInfo.MinHeight)
					.MaxDesiredHeight(WidgetInfo.MaxHeight)
					.Clipping(WidgetInfo.Clipping)
					.Padding(1.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.VAlign(VAlign_Center)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							[
								// Default text.
								SNew(STextBlock)
								.Text(DefaultText)
								.TextStyle(&WidgetInfo.DefaultTextStyle)
								.Margin(FMargin(1.f))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Center)
							.Padding(2.f) // Padding needed to help with zoom resize issues.
							[
								SNew(SOverlay)
								+ SOverlay::Slot()
								[
									SAssignNew(PinContainer, SBox)
								]
								// HACK: Fill the overlay with our interceptor for function drops.
								+ SOverlay::Slot()
								.VAlign(VAlign_Fill)
								.HAlign(HAlign_Fill)
								[
									SNew(SSMDragDropInterceptor)
									.InterceptType(SSMDragDropInterceptor::EInterceptType::OwningBPFunction)
								]
							]
						]
					]
				]
			]
		]
		// Optional highlight border.
		+SOverlay::Slot()
		.Padding(HighlightPadding)
		[
			MakeHighlightBorder()
		]
	];
}

void SSMGraphProperty::Finalize()
{
	// Close any open connectors. This could be a refresh from construction scripts, and in that case
	// a new connector will be created.
	for (TTuple<EEdGraphPinDirection, TSharedPtr<FPinData>>& PinData : PinDataMap)
	{
		if (PinData.Value.IsValid())
		{
			FPinDataChangeArgs DataChangeArgs;
			DataChangeArgs.bForceChange = true;
			PinData.Value->OnConnectorDeactivatedDelegate.ExecuteIfBound(DataChangeArgs);
		}
	}
	
	PinDataMap.Reset();
	
	USMGraphK2Node_GraphPropertyNode* Node = GetGraphNodeAs<USMGraphK2Node_GraphPropertyNode>();

	const bool bIsInputProperty = Node ? Node->IsInputProperty() : false;
	const bool bIsOutputProperty = Node ? Node->IsOutputProperty() : false;

	TArray<UEdGraphPin*> UsedResultPins;
	
	/** Create slate pin for a result pin. */
	auto CreatePin = [this, Node](UEdGraphPin* ResultPin) -> TSharedPtr<SGraphPin>
	{
		if (ResultPin)
		{
			const TSharedPtr<SGraphNode> ParentNode = GetParentStateWidget();
			TSharedPtr<SGraphPin> NewPin = ParentNode.IsValid() ? FNodeFactory::CreatePinWidget(ResultPin) : nullptr;
	
			// Don't display if invalid or another connection is present. We only want this to display / edit the default value.
			if (NewPin.IsValid() && !NewPin->IsConnected())
			{
				NewPin->SetOwner(ParentNode.ToSharedRef());
				NewPin->SetOnlyShowDefaultValue(true);
				NewPin->SetShowLabel(false);
				NewPin->SetPinColorModifier(FSMBlueprintEditorUtils::GetEditorSettings()->PropertyPinColorModifier); // Without this the color can wash out the text.
				if (Node)
				{
					if (const FSMGraphProperty_Base* Prop = Node->GetPropertyNode())
					{
						NewPin->SetIsEditable(!Prop->IsVariableReadOnly());
					}
				}

				const TWeakPtr<SHorizontalBox> Row = NewPin->GetFullPinHorizontalRowWidget();
				// We want to hide the k2 selection pin as this is only for defaults.
				if (ResultPin->Direction == EGPD_Input && Row.Pin().IsValid())
				{
					if (FChildren* Children = Row.Pin()->GetChildren())
					{
						// The first child should be the pin.
						if (Children->Num() > 0)
						{
							Children->GetChildAt(0)->SetVisibility(EVisibility::Collapsed);
						}
					}
				}
			
				return NewPin;
			}
		}
		
		return nullptr;
	};

	const TSharedPtr<SHorizontalBox> PinContainerHBox = MakeShared<SHorizontalBox>();
	if (bIsInputProperty)
	{
		constexpr EEdGraphPinDirection Direction = EGPD_Input;
		if (UEdGraphPin* ResultPin = FindResultPin(Direction))
		{
			UsedResultPins.Add(ResultPin);
		
			TSharedPtr<FPinData> Data = MakeShared<FPinData>();		
			TSharedPtr<SSMKismetLinearExpression> ExpressionWidget;

			PinContainerHBox->AddSlot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				// Linear expression.
				SAssignNew(ExpressionWidget, SSMKismetLinearExpression, ResultPin)
				.Clipping(EWidgetClipping::ClipToBounds)
			];
		
			if (const TSharedPtr<SGraphPin> Pin = CreatePin(ResultPin))
			{
				PinContainerHBox->AddSlot()
				.AutoWidth()
				[
					Pin.ToSharedRef()
				];
			
				Data->PinPtr = Pin;
			}
		
			CreateConnector(ResultPin, Data);
		
			Data->ExpressionWidgetPtr = ExpressionWidget;
			Data->ResultPin = ResultPin;
			PinDataMap.Add(Direction, MoveTemp(Data));
		}
	}

	if (bIsOutputProperty)
	{
		// Draw arrow indicating this is an output property.
		PinContainerHBox->AddSlot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(FMargin(2.0f, 0.f, 2.f, 0.0f)) 
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Icons.ArrowRight"))
			.ColorAndOpacity(FSlateColor(FLinearColor::White))
		];
		
		constexpr EEdGraphPinDirection Direction = EGPD_Output;
		UEdGraphPin* ResultPin = FindResultPin(Direction);
		UsedResultPins.Add(ResultPin);
		
		TSharedPtr<FPinData> Data = MakeShared<FPinData>();
		TSharedPtr<SSMKismetLinearExpression> ExpressionWidget;
		
		PinContainerHBox->AddSlot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			// Linear expression.
			SAssignNew(ExpressionWidget, SSMKismetLinearExpression, ResultPin)
			.Clipping(EWidgetClipping::ClipToBounds)
		];
		
		CreateConnector(ResultPin, Data);
		
		Data->ExpressionWidgetPtr = ExpressionWidget;
		Data->ResultPin = ResultPin;
		PinDataMap.Add(Direction, MoveTemp(Data));
	}

	check(PinContainer.IsValid());
	PinContainer->SetContent(PinContainerHBox.ToSharedRef());

	// Update expressions after everything has been assigned.
	for (const UEdGraphPin* ResultPin : UsedResultPins)
	{
		HandleExpressionChange(ResultPin);
	}

	FPinDataChangeArgs DataChange;
	DataChange.bJustConstructed = true;
	HandleInteractionChange(MoveTemp(DataChange));

	SSMGraphProperty_Base::Finalize();
}

void SSMGraphProperty::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SSMGraphProperty_Base::OnMouseEnter(MyGeometry, MouseEvent);
}

void SSMGraphProperty::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SSMGraphProperty_Base::OnMouseLeave(MouseEvent);
}

FReply SSMGraphProperty::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const bool bSuccess = FSMDragDropHelpers::IsDragDropValidForPropertyNode(Cast<USMGraphK2Node_PropertyNode_Base>(GraphNode),
		DragDropEvent);
	
	// Tooltip message.
	FSMDragDropHelpers::SetDragDropMessage(DragDropEvent, bSuccess);
	
	if (bSuccess)
	{
		bIsValidDragDrop = true;
		SetCursor(EMouseCursor::GrabHand);
	}

	return FReply::Handled();
}

void SSMGraphProperty::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SetCursor(EMouseCursor::CardinalCross);
	bIsValidDragDrop = false;
	SSMGraphProperty_Base::OnDragLeave(DragDropEvent);
}

FReply SSMGraphProperty::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const USMGraphK2Node_PropertyNode_Base* PropertyNode = GetPropertyNodeChecked();

	// If only output use output, otherwise input with a more complex check below.
	EEdGraphPinDirection Direction = PropertyNode->IsOutputProperty() && !PropertyNode->IsInputProperty()
		? EGPD_Output : EGPD_Input;

	// We are both, to determine the direction to utilize we need to look at the specific geometry we are dropping on.
	if (PropertyNode->IsOutputProperty() && PropertyNode->IsInputProperty())
	{
		if (const TSharedPtr<FPinData> PinData = TryGetPinDataFromDirection(EGPD_Output))
		{
			if (const TSharedPtr<SSMKismetLinearExpression> ExpressionWidget = PinData->ExpressionWidgetPtr.Pin())
			{
				if (ExpressionWidget->GetCachedGeometry().IsUnderLocation(DragDropEvent.GetScreenSpacePosition()))
				{
					Direction = EGPD_Output;
				}
			}
		}
	}
	
	return OnDirectionalDrop(MyGeometry, DragDropEvent, Direction);
}

FReply SSMGraphProperty::OnDirectionalDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent,
	EEdGraphPinDirection PinDirection)
{
	if (FSMDragDropHelpers::IsDragDropValidForPropertyNode(Cast<USMGraphK2Node_PropertyNode_Base>(GraphNode),
		DragDropEvent))
	{
		const UEdGraphPin* ResultPin = nullptr;
		if (const USMGraphK2Node_GraphPropertyNode* Node = Cast<USMGraphK2Node_GraphPropertyNode>(GraphNode))
		{
			USMPropertyGraph* Graph = Node->GetPropertyGraph();
			check(Graph);

			USMPropertyGraph::FPlacementArgs PlacementArgs;
			PlacementArgs.Direction = PinDirection;
			// Input needs to destroy previous input, but output shouldn't destroy input.
			PlacementArgs.bDestructive = PinDirection == EGPD_Input;
			// We don't want to destroy outputs since they are always added to. The user can manually remove
			// individual outputs if needed.
			PlacementArgs.bKeepOutputProperties = true;
			if (const TSharedPtr<FKismetVariableDragDropAction> VariableDragDrop = DragDropEvent.GetOperationAs<FKismetVariableDragDropAction>())
			{
				FProperty* Property = VariableDragDrop->GetVariableProperty();
				Graph->SetPropertyOnGraph(Property, PlacementArgs);
			}
			else if (const TSharedPtr<FKismetFunctionDragDropAction> FunctionDragDrop = DragDropEvent.GetOperationAs<FKismetFunctionDragDropAction>())
			{
				UFunction const* Function = FSMDragDropAction_Function::GetFunction(FunctionDragDrop.Get());
				Graph->SetFunctionOnGraph(const_cast<UFunction*>(Function), PlacementArgs);
			}

			ResultPin = Node->GetResultPin();
		}

		SetCursor(EMouseCursor::CardinalCross);

		bIsValidDragDrop = false;
		HandleExpressionChange(ResultPin);
		HandleInteractionChange(ResultPin);

		return FReply::Handled();
	}

	return SSMGraphProperty_Base::OnDirectionalDrop(MyGeometry, DragDropEvent, PinDirection);
}

void SSMGraphProperty::Refresh(const UEdGraphPin* InPin)
{
	if (const UEdGraphPin* ResultPin = InPin ? InPin : FindResultPin(EGPD_Input))
	{
		HandleExpressionChange(ResultPin);
	}
	else
	{
		HandleExpressionChange();
	}
}

void SSMGraphProperty::OnInteractProperty(const ESMPropertyInteractionType InteractionType,
	const TWeakPtr<SGraphPanel>& GraphPanel, bool bForce)
{
	SSMGraphProperty_Base::OnInteractProperty(InteractionType, GraphPanel, bForce);
	FPinDataChangeArgs DataChangeArgs;
	DataChangeArgs.InteractionType = InteractionType;
	DataChangeArgs.GraphPanel = GraphPanel;
	DataChangeArgs.bForceChange = bForce;
	HandleInteractionChange(MoveTemp(DataChangeArgs));
}

void SSMGraphProperty::CreateConnector(UEdGraphPin* ResultPin, const TSharedPtr<FPinData>& Data)
{
	Data->OnConnectorActivatedDelegate.BindLambda([this, ResultPin, Data](const FPinDataChangeArgs& InArgs)
	{
		if (Data.IsValid() && ResultPin)
		{
			if (!Data->ConnectorWidget.IsValid()
				// Only activate if output variables are present. Optimization and reduces clutter.
				&& FSMBlueprintEditorUtils::DoesBlueprintHaveOutputVariables(OwningBlueprint.Get()))
			{
				const USMGraphK2Node_PropertyNode_Base* PropertyNode = GetPropertyNode();
				const USMGraphNode_StateNodeBase* StateNode = GetOwningStateNode();

				if (!StateNode || !PropertyNode)
				{
					return;
				}

				// Check if a different property is selected on a hover. In this case we don't want to display
				// the connector.
				if (EnumHasAnyFlags(InArgs.InteractionType, ESMPropertyInteractionType::SetHover))
				{
					const USMGraphK2Node_PropertyNode_Base* SelectedProperty = StateNode->GetPropertyInteractionManager().GetSelectedProperty();
					if ((SelectedProperty && SelectedProperty != PropertyNode))
					{
						return;
					}
				}

				const TSharedPtr<SGraphNode_StateNode> ParentGraphNode = GetParentStateWidgetChecked();
						
				const TSharedPtr<SSMGraphProperty_Connector> ConnectorWidget = SNew(SSMGraphProperty_Connector, ResultPin)
					.ParentStateWidget(ParentGraphNode)
					.ParentGraphPropertyWidget(SharedThis(this));

				Data->ConnectorWidget = ConnectorWidget;

				ConnectorWidget->OpenConnector();
			}
		}
	});

	Data->OnConnectorDeactivatedDelegate.BindLambda([this, Data](const FPinDataChangeArgs& InArgs)
	{
		if (Data.IsValid() && Data->ConnectorWidget.IsValid())
		{
			const TSharedPtr<SSMGraphProperty_Connector> ConnectorWidget = Data->ConnectorWidget.Pin();

			// Force close and unregister.
			if (InArgs.bForceChange
				// Always deactivate if no output variables are present. Optimization and reduces clutter.
				|| !FSMBlueprintEditorUtils::DoesBlueprintHaveOutputVariables(OwningBlueprint.Get()))
			{
				if (Data->UnhoveredTimerHandle.IsValid())
				{
					ConnectorWidget->FreezeDisplayModeUntilVisibilityChange(false);
					UnRegisterActiveTimer(Data->UnhoveredTimerHandle.Pin().ToSharedRef());
				}
				ConnectorWidget->CloseConnector();
				return;
			}
			
			const bool bIsDirectlyInteractedWith = ConnectorWidget->IsSelected();
			// Freeze if not interacted with so the size change isn't jarring.
			ConnectorWidget->FreezeDisplayModeUntilVisibilityChange(!bIsDirectlyInteractedWith);
			
			// Delay processing in case user moves back in range quickly, and to capture creation
			// of any submenus which user is hovering.
			Data->UnhoveredTimerHandle = this->RegisterActiveTimer(0.5f,
	          FWidgetActiveTimerDelegate::CreateLambda([this, Data](double InCurrentTime, float InDeltaTime)
	          {
	              if (!Data.IsValid() || !Data->ConnectorWidget.IsValid() || Data->ConnectorWidget.Pin()->IsHovered())
	              {
	                  return EActiveTimerReturnType::Stop;
	              }
	              if (const TSharedPtr<SSMGraphProperty_Connector> ConnectorWidget = Data->ConnectorWidget.Pin())
	              {
		              if (!ConnectorWidget->IsInteractedWith())
		              {
						  ConnectorWidget->CloseConnector();
						  return EActiveTimerReturnType::Stop;
					  }
					  else if (!ConnectorWidget->IsHovered() && !ConnectorWidget->IsSelected())
					  {
						  ConnectorWidget->FreezeDisplayModeUntilVisibilityChange(false);
					  }
	              }

	              return EActiveTimerReturnType::Continue;
	          }));
		}
	});
}

void SSMGraphProperty::HandleExpressionChange()
{
	for (const UEdGraphPin* ResultPin : FindAllResultPins())
	{
		HandleExpressionChange(ResultPin);
	}
}

void SSMGraphProperty::HandleExpressionChange(const UEdGraphPin* ResultPin)
{
	if (ResultPin)
	{
		if (const TSharedPtr<FPinData> PinData = TryGetPinDataFromDirection(ResultPin->Direction))
		{
			if (ResultPin->LinkedTo.Num())
			{
				// Display normal object evaluation.
				if (const TSharedPtr<SSMKismetLinearExpression> ExpressionWidget = PinData->ExpressionWidgetPtr.Pin())
				{
					ExpressionWidget->SetVisibility(EVisibility::HitTestInvisible);
					ExpressionWidget->SetExpressionRoot(ResultPin);
				}
				if (const TSharedPtr<SGraphPin> PinPtr = PinData->PinPtr.Pin())
				{
					PinPtr->SetVisibility(EVisibility::Collapsed);
				}
			}
			else
			{
				// Display default text only.
				if (const TSharedPtr<SSMKismetLinearExpression> ExpressionWidget = PinData->ExpressionWidgetPtr.Pin())
				{
					ExpressionWidget->SetVisibility(EVisibility::Collapsed);
				}
				if (const TSharedPtr<SGraphPin> PinPtr = PinData->PinPtr.Pin())
				{
					PinPtr->SetVisibility(EVisibility::Visible);
				}
			}
		}
	}
}

void SSMGraphProperty::HandleInteractionChange(const FPinDataChangeArgs& InDataChange)
{
	for (const UEdGraphPin* ResultPin : FindAllResultPins())
	{
		HandleInteractionChange(ResultPin, InDataChange);
	}
}

void SSMGraphProperty::HandleInteractionChange(const UEdGraphPin* ResultPin, const FPinDataChangeArgs& InDataChange)
{
	if (ResultPin)
	{
		bool bWasJustHovered = false;
		
		if (InDataChange.bJustConstructed)
		{
			const USMGraphNode_StateNodeBase* StateNodeBase = GetOwningStateNode();
			check(StateNodeBase);
			const USMGraphK2Node_PropertyNode_Base* PropertyNode = GetPropertyNodeChecked();
			bWasJustHovered = StateNodeBase->GetPropertyInteractionManager().IsPropertyInteractedWith(PropertyNode);
		}
		
		if (const TSharedPtr<FPinData> PinData = TryGetPinDataFromDirection(ResultPin->Direction))
		{
			// Initial activation.
			const bool bFromHover = IsHovered() || bWasJustHovered;
			const bool bDisplayConnectors = (EnumHasAnyFlags(InDataChange.InteractionType, ESMPropertyInteractionType::SetDisplayInputConnector)
					&& ResultPin->Direction == EGPD_Input)
					|| (EnumHasAnyFlags(InDataChange.InteractionType, ESMPropertyInteractionType::SetDisplayOutputConnector)
					&& ResultPin->Direction == EGPD_Output);
			const bool bForOurGraphPanel = !InDataChange.GraphPanel.IsValid()
				|| (InDataChange.GraphPanel.HasSameObject(GetParentStateWidgetChecked()->GetOwnerPanel().Get()));
			if (bForOurGraphPanel && (bFromHover || bDisplayConnectors)
				// Always check if the graph panel we belong to supports output variables (e.g. the search panel doesn't)
				&& FSMPropertyInteractionManager::IsGraphPanelSupported(GetParentStateWidgetChecked()->GetOwnerPanel()))
			{
				PinData->OnConnectorActivatedDelegate.ExecuteIfBound(InDataChange);
			}
			// Inform new/existing of interaction state.
			if (const TSharedPtr<SSMGraphProperty_Connector> Connector = PinData->ConnectorWidget.Pin())
			{
				// Reset the unhover timer if applicable.
				if (PinData->UnhoveredTimerHandle.IsValid())
				{
					Connector->FreezeDisplayModeUntilVisibilityChange(false);
					UnRegisterActiveTimer(PinData->UnhoveredTimerHandle.Pin().ToSharedRef());
				}
				Connector->OnInteractionChange(InDataChange.InteractionType);
			}
			// Don't do else, this lets close logic run for bWasJustHovered if needed.
			if (!IsHovered())
			{
				PinData->OnConnectorDeactivatedDelegate.ExecuteIfBound(InDataChange);
			}
		}
	}
}

FSlateColor SSMGraphProperty::GetBackgroundColor() const
{
	return bIsValidDragDrop ? WidgetInfo.OnDropBackgroundColor : WidgetInfo.BackgroundColor;
}

TSharedPtr<SSMGraphProperty::FPinData> SSMGraphProperty::TryGetPinDataFromDirection(const EEdGraphPinDirection Direction) const
{
	if (const TSharedPtr<FPinData>* PinData = PinDataMap.Find(Direction))
	{
		return *PinData;
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
