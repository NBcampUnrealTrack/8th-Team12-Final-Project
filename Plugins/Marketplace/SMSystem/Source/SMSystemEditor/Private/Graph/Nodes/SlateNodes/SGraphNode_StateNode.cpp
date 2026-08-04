// Copyright Recursoft LLC. All Rights Reserved.

#include "SGraphNode_StateNode.h"

#include "Blueprints/SMBlueprintEditor.h"
#include "Configuration/SMEditorSettings.h"
#include "Configuration/SMEditorStyle.h"
#include "Debug/SMBlueprintDebugController.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SlateNodes/Properties/SSMGraphProperty_Base.h"
#include "Graph/Nodes/SMGraphNode_AnyStateNode.h"
#include "Graph/Nodes/SMGraphNode_ConduitNode.h"
#include "Graph/Nodes/SMGraphNode_LinkStateNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineParentNode.h"
#include "Graph/Nodes/SMGraphNode_StateMachineStateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/Pins/SGraphPin_StatePin.h"
#include "Helpers/SMDragDropHelpers.h"
#include "Properties/PropertyInteraction/SMPropertyInteractionManager.h"
#include "Properties/SGraphNode_PropertyContent.h"
#include "Utilities/SMBlueprintEditorUtils.h"
#include "Utilities/SMNodeInstanceUtils.h"

#include "SMConduit.h"

#include "BPVariableDragDropAction.h"
#include "GraphEditorSettings.h"
#include "Kismet2/Breakpoint.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "SCommentBubble.h"
#include "SGraphPin.h"
#include "SGraphPreviewer.h"
#include "SlateOptMacros.h"
#include "SMUnrealTypeDefs.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "SGraphStateNode"

void SGraphNode_StateNode::Construct(const FArguments& InArgs, USMGraphNode_StateNodeBase* InNode)
{
	SGraphNode_BaseNode::Construct(SGraphNode_BaseNode::FArguments(), InNode);
	ContentPadding = InArgs._ContentPadding;
	PinPadding = InArgs._PinPadding.Get(FSMBlueprintEditorUtils::GetEditorSettings()->StateConnectionSize);

	CastChecked<USMGraphNode_Base>(GraphNode)->OnWidgetConstruct();
	
	UpdateGraphNode();
	SetCursor(EMouseCursor::CardinalCross);

	const FSlateBrush* FastPathImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.FastPath"));
	FastPathWidget =
		SNew(SImage)
		.Image(FastPathImageBrush)
		.ToolTipText(LOCTEXT("StateNodeFastPathTooltip", "Fast path enabled: All execution points avoid going through the blueprint graph."))
		.Visibility(EVisibility::Visible);
	
	const FSlateBrush* ReplicationImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.Replicated"));
	ReplicationWidget =
		SNew(SImage)
		.Image(ReplicationImageBrush)
		.ToolTipText_Lambda([this]()
		{
			if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(GraphNode))
			{
				switch (StateNode->GetNodeReplicationEnableLevel())
				{
				case USMGraphNode_Base::EnabledNodeDisabledSystem:
					{
						return LOCTEXT("ReplicationDisabledSystemTooltip",
							"Replication is enabled for this node instance but disabled for the system. Configure under `Class Defaults` with `Can Replicate Node Instances`.");
					}
				case USMGraphNode_Base::EnabledNodeAndSystem:
					{
						return LOCTEXT("ReplicationEnabledTooltip", "This node instance supports replicated variables and RPCs.");
					}
				case USMGraphNode_Base::Disabled:
					break;
				}
			}
			
			return FText::GetEmpty();
		})
		.Visibility(EVisibility::Visible);
	
	const USMEditorSettings* EditorSettings = FSMBlueprintEditorUtils::GetEditorSettings();
	{
		if (EditorSettings->bEnableAnimations)
		{
			ZoomCurve = SpawnAnim.AddCurve(.0f, .15f, ECurveEaseFunction::QuadInOut);
			FadeCurve = SpawnAnim.AddCurve(.1f, .15f, ECurveEaseFunction::Linear);
		}
		else
		{
			ZoomCurve = SpawnAnim.AddCurve(0.f, 0.f);
			FadeCurve = SpawnAnim.AddCurve(0.f, 0.f);
		}
	}
}

void SGraphNode_StateNode::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SGraphNode::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	StateNode->UpdateTime(InDeltaTime);

	if (StateNode->bRequestInitialAnimation)
	{
		StateNode->bRequestInitialAnimation = false;
		PlaySpawnEffect();
	}
}

void SGraphNode_StateNode::MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);
	USMGraphNode_Base* StateNode = CastChecked<USMGraphNode_Base>(GraphNode);
	StateNode->OnNodeMoved(NewPosition, bMarkDirty);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SGraphNode_StateNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	// Reset variables that are going to be exposed, in case we are refreshing an already setup node.
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	PropertyContent.Reset();

	this->RemoveSlot(ENodeZone::Center);

	const FLinearColor TitleShadowColor(0.6f, 0.6f, 0.6f);

	SetupErrorReporting();
	TSharedPtr<SErrorText> ErrorText;
	const TSharedPtr<SWidget> ContentBox = CreateContentBox();
	const TAttribute<const FSlateBrush*> SelectedBrush = TAttribute<const FSlateBrush*>::Create(TAttribute<const FSlateBrush*>::FGetter::CreateSP(this, &SGraphNode_StateNode::GetNameIcon));
	
	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FSMUnrealAppStyle::Get().GetBrush("Graph.StateNode.Body"))
			.Padding(0)
			.BorderBackgroundColor(this, &SGraphNode_StateNode::GetBorderBackgroundColor)
			// For animation (todo: this should likely be a 2f and we can use the default GetContentScale, but UE hasn't converted it yet)
			.ContentScale(this, &SGraphNode_StateNode::GetContentScale_Internal)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			// End animation
			[
				SNew(SOverlay)

				// PIN AREA
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SAssignNew(RightNodeBox, SVerticalBox)
				]

				// STATE NAME AREA
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(PinPadding)
				[
					SNew(SBorder)
					.BorderImage(FSMUnrealAppStyle::Get().GetBrush("Graph.StateNode.ColorSpill"))
					.BorderBackgroundColor(TitleShadowColor)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Visibility(EVisibility::SelfHitTestInvisible)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							// POPUP ERROR MESSAGE
							SAssignNew(ErrorText, SErrorText)
							.BackgroundColor(this, &SGraphNode_StateNode::GetErrorColor)
							.ToolTipText(this, &SGraphNode_StateNode::GetErrorMsgToolTip)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SAssignNew(NodeIcon, SImage)
							.Image(SelectedBrush)
						]
						+ SHorizontalBox::Slot()
						.Padding(ContentPadding)
						[
							ContentBox.ToSharedRef()
						]
					]
				]
			]
		];

	// Finalize all property widgets now that parent - child relationship is complete.
	if (PropertyContent.IsValid())
	{
		PropertyContent->Finalize();
	}
	
	// Create comment bubble
	TSharedPtr<SCommentBubble> CommentBubble;
	const FSlateColor CommentColor = GetDefault<UGraphEditorSettings>()->DefaultCommentNodeTitleColor;

	SAssignNew(CommentBubble, SCommentBubble)
		.GraphNode(GraphNode)
		.Text(this, &SGraphNode::GetNodeComment)
		.OnTextCommitted(this, &SGraphNode::OnCommentTextCommitted)
		.ColorAndOpacity(CommentColor)
		.AllowPinning(true)
		.EnableTitleBarBubble(true)
		.EnableBubbleCtrls(true)
		.GraphLOD(this, &SGraphNode::GetCurrentLOD)
		.IsGraphNodeHovered(this, &SGraphNode::IsHovered);

	GetOrAddSlot(ENodeZone::TopCenter)
		.SlotOffset2f(TAttribute<FVector2f>(CommentBubble.Get(), &SCommentBubble::GetOffset2f))
		.SlotSize2f(TAttribute<FVector2f>(CommentBubble.Get(), &SCommentBubble::GetSize2f))
		.AllowScaling(TAttribute<bool>(CommentBubble.Get(), &SCommentBubble::IsScalingAllowed))
		.VAlign(VAlign_Top)
		[
			CommentBubble.ToSharedRef()
		];
	
	ErrorReporting = ErrorText;
	ErrorReporting->SetError(ErrorMsg);
	CreatePinWidgets();

	CalculateAnyStateImpact();
	CalculateLinkStateImpact();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SGraphNode_StateNode::CreatePinWidgets()
{
	const USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);

	UEdGraphPin* PinToUse = StateNode->GetOutputPin();
	if (!PinToUse)
	{
		// For cases where there is no output pin.
		PinToUse = StateNode->GetInputPin();
	}

	if (!PinToUse->bHidden)
	{
		const TSharedPtr<SGraphPin> NewPin = SNew(SSMGraphPin_StatePin, PinToUse);
		this->AddPin(NewPin.ToSharedRef());
	}
}

void SGraphNode_StateNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));
	RightNodeBox->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.FillHeight(1.0f)
		[
			PinToAdd
		];
	OutputPins.Add(PinToAdd);
}

TSharedPtr<SToolTip> SGraphNode_StateNode::GetComplexTooltip()
{
	/* Display a pop-up on mouse hover with useful information. */
	const TSharedPtr<SVerticalBox> Widget = BuildComplexTooltip();

	return SNew(SToolTip)
		[
			Widget.ToSharedRef()
		];
}

void SGraphNode_StateNode::GetOverlayBrushes(bool bSelected, const FVector2f& WidgetSize,
	TArray<FOverlayBrushInfo>& Brushes) const
{
	SGraphNode_BaseNode::GetOverlayBrushes(bSelected, WidgetSize, Brushes);

	// Is this the current instruction?
	if (FKismetDebugUtilities::GetCurrentInstruction() == GraphNode)
	{
		FOverlayBrushInfo IPOverlayInfo;

		// Pick icon depending on whether we are on a hit breakpoint or single stepping instructions.
		bool bIsOnHitBreakpoint = FKismetDebugUtilities::GetMostRecentBreakpointHit() == GraphNode
			&& GetOwningNodeAsChecked<USMGraphNode_Base>()->GetBreakpoint();
		
		IPOverlayInfo.Brush = FSMUnrealAppStyle::Get().GetBrush(bIsOnHitBreakpoint ?
			TEXT("Kismet.DebuggerOverlay.InstructionPointerBreakpoint") : TEXT("Kismet.DebuggerOverlay.InstructionPointer"));

		if (IPOverlayInfo.Brush != nullptr)
		{
			const float Overlap = 10.f;
			IPOverlayInfo.OverlayOffset.X = (WidgetSize.X/2.f) - (IPOverlayInfo.Brush->ImageSize.X/2.f);
			IPOverlayInfo.OverlayOffset.Y = (Overlap - IPOverlayInfo.Brush->ImageSize.Y);
		}

		IPOverlayInfo.AnimationEnvelope = FVector2D(0.f, 10.f);

		Brushes.Add(IPOverlayInfo);
	}
}

TArray<FOverlayWidgetInfo> SGraphNode_StateNode::GetOverlayWidgets(bool bSelected, const FVector2f& WidgetSize) const
{
	TArray<FOverlayWidgetInfo> Widgets;
	const USMEditorSettings* EditorSettings = FSMBlueprintEditorUtils::GetEditorSettings();
	
	if (const FBlueprintBreakpoint* Breakpoint = GetBreakpoint())
	{
		if (!USMBlueprintDebugController::IsBreakpointTemporary(*Breakpoint) || EditorSettings->bDisplayTemporaryBreakpoints)
		{
			const FSlateBrush* BreakpointBrush = FSMUnrealAppStyle::Get().GetBrush(
				Breakpoint->IsEnabledByUser()
				? "Kismet.Breakpoint.EnabledAndValid"
				: Breakpoint->IsEnabled() 
					? "Kismet.Breakpoint.MixedStatus" // TEMPORARY BREAKPOINT - We may want to use our own image/color.
					: "Kismet.Breakpoint.Disabled");
		
			const FVector2D BreakpointSize = BreakpointBrush->GetImageSize() * 1.3f;
		
			if (!BreakpointWidget.IsValid())
			{
				SAssignNew(BreakpointWidget, SImage)
				// Best to have as a function, or it won't show up if we're constantly setting it.
				.ToolTipText(this, &SGraphNode_StateNode::GetBreakpointTooltipText);
			}

			// Set directly instead of use a function to lower the number of breakpoint lookup calls we make.
			BreakpointWidget->SetImage(BreakpointBrush);
			BreakpointWidget->SetDesiredSizeOverride(BreakpointSize);
		
			FOverlayWidgetInfo Info;
			Info.OverlayOffset = FVector2D(WidgetSize.X - (BreakpointSize.X * 0.5f) - (Widgets.Num() * OverlayWidgetPadding),
				-(BreakpointSize.Y * 0.5f));
			Info.Widget = BreakpointWidget;

			Widgets.Add(MoveTemp(Info));
		}
	}
	
	if (!EditorSettings->bDisableVisualCues)
	{
		if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(GraphNode))
		{
			const FSlateBrush* LinkStateImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.LinkState"));
			if (LinkStateImpactWidget.IsValid())
			{
				FOverlayWidgetInfo Info;
				Info.OverlayOffset = FVector2D(WidgetSize.X - (LinkStateImageBrush->ImageSize.X * 0.5f) - (Widgets.Num() * OverlayWidgetPadding), -(LinkStateImageBrush->ImageSize.Y * 0.5f));
				Info.Widget = LinkStateImpactWidget;

				Widgets.Add(MoveTemp(Info));
			}

			const FSlateBrush* AnyStateImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.AnyState"));
			for (const TSharedPtr<SWidget>& AnyStateWidget : AnyStateImpactWidgets)
			{
				FOverlayWidgetInfo Info;
				Info.OverlayOffset = FVector2D(WidgetSize.X - (AnyStateImageBrush->ImageSize.X * 0.5f) - (Widgets.Num() * OverlayWidgetPadding),
					-(AnyStateImageBrush->ImageSize.Y * 0.5f));
				Info.Widget = AnyStateWidget;

				Widgets.Add(MoveTemp(Info));
			}
			if (EditorSettings->bDisplayFastPath && StateNode->IsNodeFastPathEnabled())
			{
				const FSlateBrush* FastPathImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.FastPath"));

				FOverlayWidgetInfo Info;
				Info.OverlayOffset = FVector2D(WidgetSize.X - (FastPathImageBrush->ImageSize.X * 0.5f) - (Widgets.Num() * OverlayWidgetPadding),
					-(FastPathImageBrush->ImageSize.Y * 0.5f));
				Info.Widget = FastPathWidget;

				Widgets.Add(MoveTemp(Info));
			}
			switch (StateNode->GetNodeReplicationEnableLevel())
			{
			case USMGraphNode_Base::Disabled:
				break;
			case USMGraphNode_Base::EnabledNodeDisabledSystem:
				{
					const FSlateBrush* ReplicationImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.Replicated"));
					FOverlayWidgetInfo Info;
					Info.OverlayOffset = FVector2D(WidgetSize.X - (ReplicationImageBrush->ImageSize.X * 0.5f) - (Widgets.Num() * OverlayWidgetPadding),
						-(ReplicationImageBrush->ImageSize.Y * 0.5f));
					ReplicationWidget->SetRenderOpacity(0.5f);
					Info.Widget = ReplicationWidget;
					
					Widgets.Add(MoveTemp(Info));
					break;
				}
			case USMGraphNode_Base::EnabledNodeAndSystem:
				{
					const FSlateBrush* ReplicationImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.Replicated"));
					FOverlayWidgetInfo Info;
					Info.OverlayOffset = FVector2D(WidgetSize.X - (ReplicationImageBrush->ImageSize.X * 0.5f) - (Widgets.Num() * OverlayWidgetPadding),
						-(ReplicationImageBrush->ImageSize.Y * 0.5f));
					ReplicationWidget->SetRenderOpacity(1.f);
					Info.Widget = ReplicationWidget;
					
					Widgets.Add(MoveTemp(Info));
					break;
				}
			}
		}
	}

	for (const TTuple<FGuid, FExternalOverlayWidget>& ExternalWidget : ExternalOverlayWidgets)
	{
		if (ExternalWidget.Value.Widget.IsValid())
		{
			FOverlayWidgetInfo OverlayWidgetInfo;
			OverlayWidgetInfo.Widget = ExternalWidget.Value.Widget;
			check(ExternalWidget.Value.OverlayOffset);
			OverlayWidgetInfo.OverlayOffset = ExternalWidget.Value.OverlayOffset(WidgetSize);
			Widgets.Add(MoveTemp(OverlayWidgetInfo));
		}
	}
	
	return Widgets;
}

void SGraphNode_StateNode::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bMouseOverCoreNode = false;
	SGraphNode_BaseNode::OnMouseEnter(MyGeometry, MouseEvent);
}

void SGraphNode_StateNode::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SGraphNode_BaseNode::OnMouseLeave(MouseEvent);

	if (const USMGraphNode_StateNodeBase* Node = Cast<USMGraphNode_StateNodeBase>(GraphNode))
	{
		if (!FSMBlueprintEditorUtils::IsNodeSelected(Node))
		{
			Node->GetPropertyInteractionManager().SetAllPropertyInteractions(ESMPropertyInteractionType::UnsetDisplayInputConnector | ESMPropertyInteractionType::UnsetDisplayOutputConnector);
		}
	}
}

FReply SGraphNode_StateNode::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bLastHoverValue = bMouseOverCoreNode;
	
	const FVector2D LocalMousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D NodeSize = MyGeometry.GetLocalSize();
	
	// Check if mouse is within node bounds but outside pin padding area.
	const bool bWithinNodeBounds = LocalMousePosition.X >= 0 && LocalMousePosition.X <= NodeSize.X &&
		LocalMousePosition.Y >= 0 && LocalMousePosition.Y <= NodeSize.Y;
		
	const bool bWithinPinArea = LocalMousePosition.X <= PinPadding || LocalMousePosition.X >= (NodeSize.X - PinPadding) ||
		LocalMousePosition.Y <= PinPadding || LocalMousePosition.Y >= (NodeSize.Y - PinPadding);
		
	bMouseOverCoreNode = bWithinNodeBounds && !bWithinPinArea;

	if (bLastHoverValue != bMouseOverCoreNode)
	{
		if (const USMGraphNode_StateNodeBase* Node = Cast<USMGraphNode_StateNodeBase>(GraphNode))
		{
			if (bMouseOverCoreNode)
			{
				FSMPropertyInteractionManager::FBulkInteractionArgs BulkInteractionArgs;

				// Force clear previous interactions -- required in case we have two graphs open to the same node,
				// and the user hovers the node in one graph, then quickly moves to the same node in the next graph.
				{
					BulkInteractionArgs.bForce = true;
					Node->GetPropertyInteractionManager().SetAllPropertyInteractions(
									ESMPropertyInteractionType::UnsetDisplayInputConnector
									| ESMPropertyInteractionType::UnsetDisplayOutputConnector, BulkInteractionArgs);
				}

				// Show all connectors for this state.
				{
					BulkInteractionArgs.bForce = false;
					BulkInteractionArgs.ScopedGraphPanel = GetOwnerPanel();
					Node->GetPropertyInteractionManager().SetAllPropertyInteractions(
									ESMPropertyInteractionType::SetDisplayInputConnector
									| ESMPropertyInteractionType::SetDisplayOutputConnector, BulkInteractionArgs);
				}
			}
			else if (!FSMBlueprintEditorUtils::IsNodeSelected(Node))
			{
				Node->GetPropertyInteractionManager().SetAllPropertyInteractions(ESMPropertyInteractionType::UnsetDisplayInputConnector | ESMPropertyInteractionType::UnsetDisplayOutputConnector);
			}
		}
	}
	
	return SGraphNode_BaseNode::OnMouseMove(MyGeometry, MouseEvent);
}

FReply SGraphNode_StateNode::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	// Prevent double click from stealing interaction with widget.
	// TODO: See if property has any handling and call that, continue, or cancel.
	if (PropertyContent.IsValid())
	{
		for (const auto& KeyVal : PropertyContent->GetPropertyWidgets())
		{
			if (KeyVal.Key->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
			{
				return FReply::Handled();
			}
		}
	}
	
	return SGraphNode::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}

void SGraphNode_StateNode::RequestRenameOnSpawn()
{
	if (PropertyContent.IsValid())
	{
		if (const USMGraphNode_Base* Node = Cast<USMGraphNode_Base>(GraphNode))
		{
			if (const USMStateInstance_Base* NodeInstance = Cast<USMStateInstance_Base>(Node->GetNodeTemplate()))
			{
				if (!NodeInstance->ShouldDisplayNameWidget() || NodeInstance->ShouldUseDisplayNameOnly())
				{
					// No name widget to display -- see if there are other widgets to display.
					for (const auto& KeyVal : PropertyContent->GetPropertyWidgets())
					{
						if (KeyVal.Value->IsConsideredForDefaultProperty())
						{
							KeyVal.Value->DefaultPropertyActionWhenPlaced(StaticCastSharedPtr<SWidget>(KeyVal.Key));
							break;
						}
					}
				
					return;
				}
			}
		}
	}

	SGraphNode::RequestRenameOnSpawn();
}

bool SGraphNode_StateNode::IsNameReadOnly() const
{
	const USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	if (const USMStateInstance_Base* NodeInstance = Cast<USMStateInstance_Base>(StateNode->GetNodeTemplate()))
	{
		if (NodeInstance->ShouldUseDisplayNameOnly())
		{
			return true;
		}
	}

	return SGraphNode_BaseNode::IsNameReadOnly();
}

void SGraphNode_StateNode::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SGraphNode_BaseNode::OnDragEnter(MyGeometry, DragDropEvent);
	
	ESMPropertyInteractionType InteractionType = ESMPropertyInteractionType::None;

	if (DragDropEvent.GetOperationAs<FKismetVariableDragDropAction>())
	{
		// Variables can be either input or output.
		InteractionType = ESMPropertyInteractionType::SetDisplayInputConnector | ESMPropertyInteractionType::SetDisplayOutputConnector;
	}
	else if (DragDropEvent.GetOperationAs<FKismetFunctionDragDropAction>())
	{
		// Functions can only be given to input through drop.
		InteractionType = ESMPropertyInteractionType::SetDisplayInputConnector;
	}

	if (InteractionType != ESMPropertyInteractionType::None)
	{
		if (const USMGraphNode_StateNodeBase* Node = Cast<USMGraphNode_StateNodeBase>(GraphNode))
		{
			FSMPropertyInteractionManager::FBulkInteractionArgs BulkInteractionArgs;
			BulkInteractionArgs.ScopedGraphPanel = GetOwnerPanel();
			Node->GetPropertyInteractionManager().SetAllPropertyInteractions(InteractionType, BulkInteractionArgs);
		}
	}
}

void SGraphNode_StateNode::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SGraphNode_BaseNode::OnDragLeave(DragDropEvent);

	ESMPropertyInteractionType InteractionType = ESMPropertyInteractionType::None;
	
	if (DragDropEvent.GetOperationAs<FKismetVariableDragDropAction>())
	{
		// Variables can be either input or output.
		InteractionType = ESMPropertyInteractionType::UnsetDisplayInputConnector | ESMPropertyInteractionType::UnsetDisplayOutputConnector;
	}
	else if (DragDropEvent.GetOperationAs<FKismetFunctionDragDropAction>())
	{
		// Functions can only be given to input through drop.
		InteractionType = ESMPropertyInteractionType::UnsetDisplayInputConnector;
	}

	if (InteractionType != ESMPropertyInteractionType::None)
	{
		if (const USMGraphNode_StateNodeBase* Node = Cast<USMGraphNode_StateNodeBase>(GraphNode))
		{
			FSMPropertyInteractionManager::FBulkInteractionArgs BulkInteractionArgs;
			Node->GetPropertyInteractionManager().SetAllPropertyInteractions(InteractionType, BulkInteractionArgs);
		}
	}
}

FReply SGraphNode_StateNode::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return FReply::Handled();
}

TSharedPtr<SVerticalBox> SGraphNode_StateNode::BuildComplexTooltip()
{
	USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);

	const bool bCanExecute = StateNode->HasInputConnections();
	const bool bIsEndState = StateNode->IsEndState(false);
	bool bIsAnyState = false;
	
	FString NodeType = "State";
	if (StateNode->IsA<USMGraphNode_StateMachineParentNode>())
	{
		NodeType = "Parent";
	}
	else if (const USMGraphNode_StateMachineStateNode* StateMachineNode = Cast<USMGraphNode_StateMachineStateNode>(StateNode))
	{
		NodeType = StateMachineNode->IsStateMachineReference() ? "State Machine Reference" : "State Machine";
	}
	else if (USMGraphNode_AnyStateNode* AnyStateNode = Cast<USMGraphNode_AnyStateNode>(StateNode))
	{
		NodeType = "Any State";
		bIsAnyState = true;
	}
	else if (USMGraphNode_ConduitNode* ConduitNode = Cast<USMGraphNode_ConduitNode>(StateNode))
	{
		NodeType = "Conduit";
	}

	const bool bAnyStateImpactsThisNode = !bIsAnyState && FSMBlueprintEditorUtils::IsNodeImpactedFromAnyStateNode(StateNode);
	
	const FSlateBrush* FastPathImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.FastPath_32x"));
	
	TSharedPtr<SVerticalBox> Widget = SNew(SVerticalBox);
	Widget->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, 4.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.TextStyle(FSMEditorStyle::Get(), "SMGraph.Tooltip.Title")
				.Text(FText::Format(LOCTEXT("StatePopupTitle", "{0} ({1})"), FText::FromString(StateNode->GetStateName()), FText::FromString(NodeType)))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, -4.f, 0.f, 0.f)
			[
				SNew(SImage)
				.Image(FastPathImageBrush)
				.Visibility_Lambda([StateNode]()
				{
					return StateNode && StateNode->IsNodeFastPathEnabled() ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
		];

	if (!StateNode->IsUsingDefaultNodeClass())
	{
		if (const USMNodeInstance* NodeTemplate = StateNode->GetNodeTemplate())
		{
			const TSharedPtr<SWidget> NodeClassWidget =
				FSMNodeInstanceUtils::CreateNodeClassWidgetDisplay(NodeTemplate);
		
			Widget->AddSlot()
			.AutoHeight()
			[
				NodeClassWidget.ToSharedRef()
			];
		}
	}
	if (UEdGraph* Graph = GetGraphToUseForTooltip())
	{
		Widget->AddSlot()
			.AutoHeight()
			[
				SNew(SGraphPreviewer, Graph)
				.ShowGraphStateOverlay(false)
			];
	}
	if (!bCanExecute && !bIsAnyState)
	{
		Widget->AddSlot()
			.AutoHeight()
			.Padding(FMargin(2.f, 4.f, 2.f, 2.f))
			[
				SNew(STextBlock)
				.TextStyle(FSMEditorStyle::Get(), "SMGraph.Tooltip.Warning")
				.Text(LOCTEXT("StateCantExecuteTooltip", "No Valid Input: State will never execute"))
			];
	}

	if (bIsEndState)
	{
		const FText EndStateTooltip = StateNode->IsEndState(true) ? LOCTEXT("EndStateTooltip", "End State: State will never exit") :
			LOCTEXT("NotEndStateTooltip", "Not an End State: An Any State node is adding transitions to this node");
		
		Widget->AddSlot()
			.AutoHeight()
			.Padding(FMargin(2.f, 4.f, 2.f, 2.f))
			[
				SNew(STextBlock)
				.TextStyle(FSMEditorStyle::Get(), "SMGraph.Tooltip.Info")
				.Text(EndStateTooltip)
			];
	}
	else if (bAnyStateImpactsThisNode)
	{
		Widget->AddSlot()
			.AutoHeight()
			.Padding(FMargin(2.f, 4.f, 2.f, 2.f))
			[
				SNew(STextBlock)
				.TextStyle(FSMEditorStyle::Get(), "SMGraph.Tooltip.Info")
				.Text(LOCTEXT("AnyStateImpactTooltip", "An Any State node is adding transitions to this node"))
			];
	}

	return Widget;
}

UEdGraph* SGraphNode_StateNode::GetGraphToUseForTooltip() const
{
	const USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	return StateNode->GetBoundGraph();
}

FVector2D SGraphNode_StateNode::GetContentScale_Internal() const
{
	const float CurZoomValue = ZoomCurve.GetLerp();
	return FVector2D(CurZoomValue, CurZoomValue);
}

void SGraphNode_StateNode::CalculateAnyStateImpact()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SGraphNode_StateNode::CalculateAnyStateImpact"), STAT_CalculateAnyStateImpact, STATGROUP_LogicDriverEditor);

	AnyStateImpactWidgets.Reset();
	
	const USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	TArray<USMGraphNode_AnyStateNode*> AnyStates;

	const USMEditorSettings* EditorSettings = FSMBlueprintEditorUtils::GetEditorSettings();
	if (EditorSettings->MaxAnyStateIcons > 0 && FSMBlueprintEditorUtils::IsNodeImpactedFromAnyStateNode(StateNode, &AnyStates))
	{
		// Sort first so similar colors are grouped. Luminance seems to provide quickest and best results.
		AnyStates.Sort([](const USMGraphNode_AnyStateNode& AnyStateA, const USMGraphNode_AnyStateNode& AnyStateB)
		{
			return AnyStateA.GetAnyStateColor().GetLuminance() >= AnyStateB.GetAnyStateColor().GetLuminance();
		});
		
		int32 ColorsOverLimit = 1;
		for (int32 AnyStateIdx = 0; AnyStateIdx < AnyStates.Num(); ++AnyStateIdx)
		{
			const USMGraphNode_AnyStateNode* AnyState = AnyStates[AnyStateIdx];
			const bool bIsGrouped = AnyStateIdx >= EditorSettings->MaxAnyStateIcons;
			const bool bIsLastIteration = AnyStateIdx == AnyStates.Num() - 1;
			
			FLinearColor AnyStateColor = AnyState->GetAnyStateColor();
			
			if (bIsGrouped)
			{
				ColorsOverLimit++;
				if (!bIsLastIteration)
				{
					// Skip until end.
					continue;
				}
			}
			
			FText TooltipText;

			if (bIsGrouped)
			{
				// Replace the last one with the grouped widget.
				AnyStateImpactWidgets.RemoveAt(0);
				
				TooltipText = FText::FromString(FString::Printf(TEXT("An additional %s Any State nodes are adding transitions to this node."),
					*FString::FromInt(ColorsOverLimit)));

				AnyStateColor = FLinearColor::White;
			}
			else
			{
				// Display individual any state.
				TooltipText = FText::FromString(FString::Printf(TEXT("The Any State node '%s' is adding one or more transitions to this state."),
					*AnyState->GetStateName()));
			}

			AnyStateColor.A = 0.72f;
			
			const FSlateBrush* ImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.AnyState"));
			TSharedPtr<SWidget> Widget =
				SNew(SBorder)
				.BorderImage(FSMUnrealAppStyle::Get().GetBrush("NoBorder"))
				.Cursor(bIsGrouped ? EMouseCursor::Default : EMouseCursor::Hand)
				.Padding(0.f)
				.VAlign(VAlign_Center)
				.OnMouseDoubleClick_Lambda([AnyState, bIsGrouped](const FGeometry&, const FPointerEvent&)
				{
					if (!bIsGrouped && AnyState)
					{
						FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(AnyState);
					}
					return FReply::Handled();
				})
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(ImageBrush)
						.ToolTipText(TooltipText)
						.ColorAndOpacity(AnyStateColor)
						.Visibility(EVisibility::Visible)
					]
					+ SOverlay::Slot()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Visibility(bIsGrouped ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
						.Text(FText::FromString(FString::FromInt(ColorsOverLimit)))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FLinearColor::Black)
					]
				];

			AnyStateImpactWidgets.Insert(Widget, 0);
		}
	}
}

void SGraphNode_StateNode::CalculateLinkStateImpact()
{
	LinkStateImpactWidget.Reset();

	const USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);

	const bool bIsLinkState = StateNode->IsA<USMGraphNode_LinkStateNode>() && CastChecked<USMGraphNode_LinkStateNode>(StateNode)->GetLinkedState() != nullptr;
	const bool bHasLinkStates = StateNode->GetLinkStates().Num() > 0;

	if (bIsLinkState || bHasLinkStates)
	{
		FText TooltipText;

		if (bIsLinkState)
		{
			TooltipText = LOCTEXT("LinkState_Tooltip", "This node is a proxy for another state in the graph.");
		}
		else if (bHasLinkStates)
		{
			TooltipText = LOCTEXT("LinkByStates_Tooltip", "This node is linked to from one or more proxy states in the graph.");
		}

		const FSlateBrush* ImageBrush = FSMEditorStyle::Get()->GetBrush(TEXT("SMGraph.LinkState"));
		SAssignNew(LinkStateImpactWidget, SBorder)
			.BorderImage(FSMUnrealAppStyle::Get().GetBrush("NoBorder"))
			.Cursor(EMouseCursor::Hand)
			.Padding(0.f)
			.VAlign(VAlign_Center)
			.OnMouseDoubleClick_Lambda([bIsLinkState, bHasLinkStates, StateNode](const FGeometry&, const FPointerEvent&)
			{
				if (bIsLinkState)
				{
					FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(CastChecked<USMGraphNode_LinkStateNode>(StateNode)->GetLinkedState());
				}
				else if (bHasLinkStates)
				{
					const TSet<TObjectPtr<USMGraphNode_LinkStateNode>>& LinkStates = StateNode->GetLinkStates();
					if (LinkStates.Num() == 1)
					{
						FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(*LinkStates.CreateConstIterator());
					}
					else
					{
						if (FSMStateMachineBlueprintEditor* Editor = FSMBlueprintEditorUtils::GetStateMachineEditor(StateNode))
						{
							Editor->SelectNodes(reinterpret_cast<const TSet<UEdGraphNode*>&>(LinkStates), true);
						}
					}
				}
				return FReply::Handled();
			})
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(ImageBrush)
					.ToolTipText(TooltipText)
					.Visibility(EVisibility::Visible)
				]
			];
	}
}

const FBlueprintBreakpoint* SGraphNode_StateNode::GetBreakpoint() const
{
	if (const USMGraphNode_StateNodeBase* StateNode = GetOwningNodeAs<USMGraphNode_StateNodeBase>())
	{
		return StateNode->GetBreakpoint();
	}

	return nullptr;
}

FText SGraphNode_StateNode::GetBreakpointTooltipText() const
{
	if (const FBlueprintBreakpoint* Breakpoint = GetBreakpoint())
	{
		return Breakpoint->IsEnabledByUser() ?
				LOCTEXT("EnabledBreakpoint", "Active Breakpoint - Execution will break at this location.")
				: Breakpoint->IsEnabled() ?
				LOCTEXT("TemporaryBreakpoint", "Temporary Breakpoint - This break point is to assist with single stepping and will be removed automatically.")
				: LOCTEXT("DisabledBreakpoint", "Disabled Breakpoint");
	}

	return FText::GetEmpty();
}

void SGraphNode_StateNode::GetNodeInfoPopups(FNodeInfoContext* Context,
                                             TArray<FGraphInformationPopupInfo>& Popups) const
{
	const USMGraphNode_StateNodeBase* Node = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	if (const FSMNode_Base* DebugNode = Node->GetDebugNode())
	{
		// Show active time or last active time over the node.

		if (Node->IsDebugNodeActive())
		{
			const FString StateText = FString::Printf(TEXT("Active for %.2f secs"), DebugNode->TimeInState);
			new (Popups) FGraphInformationPopupInfo(nullptr, Node->GetBackgroundColor(), StateText);
		}
		else if (Node->WasDebugNodeActive())
		{
			const USMEditorSettings* EditorSettings = FSMBlueprintEditorUtils::GetEditorSettings();

			const float StartFade = EditorSettings->TimeToDisplayLastActiveState;
			const float TimeToFade = EditorSettings->TimeToFadeLastActiveState;
			const float DebugTime = Node->GetDebugTime();

			if (DebugTime < StartFade + TimeToFade)
			{
				const FString StateText = FString::Printf(TEXT("Was Active for %.2f secs"), DebugNode->TimeInState);

				if (DebugTime > StartFade)
				{
					FLinearColor Color = Node->GetBackgroundColor();

					const float PercentComplete = TimeToFade <= 0.f ? 0.f : FMath::Clamp(Color.A * (1.f - (DebugTime - StartFade) / TimeToFade), 0.f, Color.A);
					Color.A *= PercentComplete;

					const FLinearColor ResultColor = Color;
					new (Popups) FGraphInformationPopupInfo(nullptr, ResultColor, StateText);
				}
				else
				{
					new (Popups) FGraphInformationPopupInfo(nullptr, Node->GetBackgroundColor(), StateText);
				}
			}
		}
	}
}

void SGraphNode_StateNode::OnRefreshRequested(USMGraphNode_Base* InNode, bool bFullRefresh)
{
	CalculateAnyStateImpact();
	CalculateLinkStateImpact();
	BreakpointWidget.Reset();
	
	if (!bFullRefresh && PropertyContent.IsValid())
	{
		// Optimized refresh.
		const bool bRefreshSuccess = PropertyContent->RefreshAllProperties();
		if (bRefreshSuccess)
		{
			return;
		}

		LDEDITOR_LOG_WARNING(
			TEXT("Could not perform an optimized refresh of slate node %s. This can happen if a construction script is modifying the structure of the node."),
			InNode ? *InNode->GetNodeName() : TEXT("(null)"));
	}
	
	// Full refresh
	SGraphNode_BaseNode::OnRefreshRequested(InNode, bFullRefresh);
}

FGuid SGraphNode_StateNode::AddOverlayWidget(FExternalOverlayWidget&& InOverlayWidgetInfo)
{
	const FGuid NewGuid = FGuid::NewGuid();
	ExternalOverlayWidgets.Add(NewGuid, MoveTemp(InOverlayWidgetInfo));
	return NewGuid;
}

void SGraphNode_StateNode::RemoveOverlayWidget(const FGuid& InGuid)
{
	ExternalOverlayWidgets.Remove(InGuid);
}

TSharedPtr<SWidget> SGraphNode_StateNode::CreateContentBox()
{
	TSharedPtr<SVerticalBox> Content;
	const TSharedPtr<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode)
	.Text(this, &SGraphNode_StateNode::GetNodeTitle);
	
	SAssignNew(Content, SVerticalBox);

	bool bDisplayTitle = true;
	if (USMGraphNode_Base* Node = Cast<USMGraphNode_Base>(GraphNode))
	{
		if (USMStateInstance_Base* NodeInstance = Cast<USMStateInstance_Base>(Node->GetNodeTemplate()))
		{
			if (!NodeInstance->ShouldDisplayNameWidget())
			{
				bDisplayTitle = false;
			}
		}
	}
	
	Content->AddSlot()
		.AutoHeight()
		[
			SAssignNew(InlineEditableText, SInlineEditableTextBlock)
			.Style(FSMUnrealAppStyle::Get(), "Graph.StateNode.NodeTitleInlineEditableText")
			.Text(this, &SGraphNode_StateNode::GetNodeTitle)
			.OnVerifyTextChanged(this, &SGraphNode_StateNode::OnVerifyNameTextChanged)
			.OnTextCommitted(this, &SGraphNode_StateNode::OnNameTextCommited)
			.IsReadOnly(this, &SGraphNode_StateNode::IsNameReadOnly)
			.IsSelected(this, &SGraphNode_StateNode::IsSelectedExclusively)
			.Visibility(bDisplayTitle ? EVisibility::Visible : EVisibility::Collapsed)
		];
	Content->AddSlot()
		.AutoHeight()
		[
			NodeTitle.ToSharedRef()
		];

	// Graph properties.
	Content->AddSlot()
		.AutoHeight()
		[
			SAssignNew(PropertyContent, SSMGraphNode_PropertyContent)
			.GraphNode(CastChecked<USMGraphNode_Base>(GraphNode))
		];
	
	return Content;
}

FText SGraphNode_StateNode::GetNodeTitle() const
{
	const USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	return StateNode->GetNodeTitle(ENodeTitleType::FullTitle);
}

FSlateColor SGraphNode_StateNode::GetBorderBackgroundColor() const
{
	const USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	
	FLinearColor BackgroundColor = StateNode->GetBackgroundColor();
	BackgroundColor.A *= GetColorAndOpacity().A;
	
	return BackgroundColor;
}

const FSlateBrush* SGraphNode_StateNode::GetNameIcon() const
{
	const USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	if (const FSlateBrush* Brush = StateNode->GetNodeIcon())
	{
		return Brush;
	}
	
	return FSMUnrealAppStyle::Get().GetBrush(TEXT("Graph.StateNode.Icon"));
}


void SGraphNode_ConduitNode::Construct(const FArguments& InArgs, USMGraphNode_ConduitNode* InNode)
{
	const USMEditorSettings* EditorSettings = FSMBlueprintEditorUtils::GetEditorSettings();

	SGraphNode_StateNode::FArguments Args;
	Args.ContentPadding(EditorSettings->StateContentPadding);
	
	SGraphNode_StateNode::Construct(MoveTemp(Args), InNode);
}

void SGraphNode_ConduitNode::GetNodeInfoPopups(FNodeInfoContext* Context, TArray<FGraphInformationPopupInfo>& Popups) const
{
	USMGraphNode_ConduitNode* Node = CastChecked<USMGraphNode_ConduitNode>(GraphNode);
	if (const FSMConduit* DebugNode = (FSMConduit*)Node->GetDebugNode())
	{
		if (Node->ShouldEvalWithTransitions() && Node->WasEvaluating())
		{
			// Transition evaluation, don't show active information.
			return;
		}
	}
	
	SGraphNode_StateNode::GetNodeInfoPopups(Context, Popups);
}

const FSlateBrush* SGraphNode_ConduitNode::GetNameIcon() const
{
	USMGraphNode_StateNodeBase* StateNode = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	if (const FSlateBrush* Brush = StateNode->GetNodeIcon())
	{
		return Brush;
	}
	
	return FSMUnrealAppStyle::Get().GetBrush(TEXT("Graph.ConduitNode.Icon"));
}

#undef LOCTEXT_NAMESPACE