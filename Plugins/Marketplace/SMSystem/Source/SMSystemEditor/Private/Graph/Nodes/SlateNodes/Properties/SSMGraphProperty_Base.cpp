// Copyright Recursoft LLC. All Rights Reserved.

#include "Graph/Nodes/SlateNodes/Properties/SSMGraphProperty_Base.h"

#include "Configuration/SMEditorStyle.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SlateNodes/SGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Helpers/SMDragDropHelpers.h"
#include "Properties/PropertyInteraction/SMPropertyInteractionManager.h"
#include "SMGraphProperty_Base.h"
#include "SSMGraphPropertyTreeView.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SGraphPin.h"
#include "SMUnrealTypeDefs.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "SSMGraphProperty_Base"

const FMargin SSMGraphProperty_Base::NotifyPadding = FMargin(3.f, 0.f, 0.f, 0.f);

SSMGraphProperty_Base::SSMGraphProperty_Base(): GraphNode(nullptr)
{
}

SSMGraphProperty_Base::~SSMGraphProperty_Base()
{
	if (USMGraphK2Node_PropertyNode_Base* PropertyNode = GetPropertyNode())
	{
		PropertyNode->UnregisterWidget(RegisteredWidgetGuid);
	}
}

void SSMGraphProperty_Base::Finalize()
{
	if (USMGraphK2Node_PropertyNode_Base* PropertyNode = GetPropertyNode())
	{
		RegisteredWidgetGuid = PropertyNode->RegisterWidget(AsWeak());
	}
}

void SSMGraphProperty_Base::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	InteractWithProperty(ESMPropertyInteractionType::SetHover);
}

void SSMGraphProperty_Base::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	InteractWithProperty(ESMPropertyInteractionType::UnsetHover);
}

FReply SSMGraphProperty_Base::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (USMGraphK2Node_PropertyNode_Base* Node = Cast<USMGraphK2Node_PropertyNode_Base>(GraphNode))
	{
		Node->JumpToPropertyGraph();
		return FReply::Handled();
	}

	return SCompoundWidget::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
}

void SSMGraphProperty_Base::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);
	if (FSMDragDropHelpers::IsDragDropValidForPropertyNode(GetPropertyNode(), DragDropEvent))
	{
		InteractWithProperty(ESMPropertyInteractionType::SetHover);
	}
}

void SSMGraphProperty_Base::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragLeave(DragDropEvent);
	if (FSMDragDropHelpers::IsDragDropValidForPropertyNode(GetPropertyNode(), DragDropEvent))
	{
		InteractWithProperty(ESMPropertyInteractionType::UnsetHover);
	}
}

FReply SSMGraphProperty_Base::OnDirectionalDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent,
	EEdGraphPinDirection PinDirection)
{
	return FReply::Unhandled();
}

USMGraphK2Node_PropertyNode_Base* SSMGraphProperty_Base::GetPropertyNode() const
{
	return GetGraphNodeAs<USMGraphK2Node_PropertyNode_Base>();
}

USMGraphK2Node_PropertyNode_Base* SSMGraphProperty_Base::GetPropertyNodeChecked() const
{
	USMGraphK2Node_PropertyNode_Base* PropertyNode = GetPropertyNode();
	check(PropertyNode);
	return PropertyNode;
}

USMGraphNode_StateNodeBase* SSMGraphProperty_Base::GetOwningStateNode() const
{
	if (const USMGraphK2Node_PropertyNode_Base* PropertyNode = GetPropertyNode())
	{
		return Cast<USMGraphNode_StateNodeBase>(PropertyNode->GetOwningGraphNode());
	}

	return nullptr;
}

TSharedPtr<SGraphNode> SSMGraphProperty_Base::FindParentGraphNode() const
{
	for (TSharedPtr<SWidget> Parent = GetParentWidget(); Parent.IsValid(); Parent = Parent->GetParentWidget())
	{
		FString Type = Parent->GetType().ToString();
		if (Type.Contains("SGraphNode"))
		{
			return StaticCastSharedPtr<SGraphNode>(Parent);
		}
	}

	return nullptr;
}

TSharedPtr<SGraphNode_StateNode> SSMGraphProperty_Base::GetParentStateWidget() const
{
	if (!CachedParentStateWidget.IsValid())
	{
		// Initial lookup to cache the value.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			if (const TSharedPtr<SGraphNode> ParentSlateNode = FindParentGraphNode())
			{
				CachedParentStateWidget = StaticCastSharedPtr<SGraphNode_StateNode>(ParentSlateNode);
			}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	if (CachedParentStateWidget.IsValid())
	{
		return CachedParentStateWidget.Pin();
	}

	return nullptr;
}

TSharedPtr<SGraphNode_StateNode> SSMGraphProperty_Base::GetParentStateWidgetChecked() const
{
	const TSharedPtr<SGraphNode_StateNode> StateWidget = GetParentStateWidget();
	check(StateWidget.IsValid());
	return StateWidget;
}

UEdGraphPin* SSMGraphProperty_Base::FindResultPin(const EEdGraphPinDirection Direction) const
{
	if (const USMGraphK2Node_PropertyNode_Base* Node = GetGraphNodeAs<USMGraphK2Node_PropertyNode_Base>())
	{
		return Node->GetResultPin(Direction);
	}

	return nullptr;
}

TArray<UEdGraphPin*> SSMGraphProperty_Base::FindAllResultPins() const
{
	TArray<UEdGraphPin*> ResultPins;
	if (const USMGraphK2Node_PropertyNode_Base* Node = GetGraphNodeAs<USMGraphK2Node_PropertyNode_Base>())
	{
		if (UEdGraphPin* Pin = Node->GetResultPin(EGPD_Input))
		{
			ResultPins.Add(Pin);
		}
		if (UEdGraphPin* Pin = Node->GetResultPin(EGPD_Output))
		{
			ResultPins.Add(Pin);
		}
	}

	return ResultPins;
}

void SSMGraphProperty_Base::InteractWithProperty(const ESMPropertyInteractionType InteractionType)
{
	if (const USMGraphNode_StateNodeBase* StateNode = GetOwningStateNode())
	{
		const FSMPropertyInteractionManager::FInteractionArgs Args
			= FSMPropertyInteractionManager::BuildInteractionArgsForExternalSelections(InteractionType,
				GetParentStateWidgetChecked()->GetOwnerPanel().ToWeakPtr());
		StateNode->GetPropertyInteractionManager().SetPropertyInteraction(GetPropertyNode(), InteractionType, Args);
	}
}

bool SSMGraphProperty_Base::IsCategoryExpanded() const
{
	if (const TSharedPtr<FSMGraphPropertyTreeItem> TreeItemPin = TreeItemWeakPtr.Pin())
	{
		// Get parent category first, because the actual property doesn't have the expansion state.
		if (const TSharedPtr<FSMGraphPropertyTreeItem> ParentCategory = TreeItemPin->GetParentCategory())
		{
			return ParentCategory->ShouldItemBeExpanded();
		}
	}

	return true;
}

bool SSMGraphProperty_Base::IsGraphPropertyVisuallyEnabled() const
{
	return GraphNode.IsValid()
		&& !FSMBlueprintEditorUtils::IsGraphReadOnlyForAnyReason(GraphNode->GetGraph());
}

TSharedRef<SBorder> SSMGraphProperty_Base::MakeHighlightBorder()
{
	return SNew(SBorder)
	.BorderImage(FSMEditorStyle::Get()->GetBrush("BoxHighlight"))
	.BorderBackgroundColor(this, &SSMGraphProperty_Base::GetHighlightColor)
	.Visibility(this, &SSMGraphProperty_Base::GetHighlightVisibility);
}

TSharedRef<SWidget> SSMGraphProperty_Base::MakeNotifyIconWidget()
{
	return SNew(SImage)
	.Image(this, &SSMGraphProperty_Base::GetNotifyIconBrush)
	.ColorAndOpacity(this, &SSMGraphProperty_Base::GetNotifyIconColor)
	.ToolTipText(this, &SSMGraphProperty_Base::GetNotifyIconTooltip)
	.Visibility(this, &SSMGraphProperty_Base::GetNotifyVisibility);
}

EVisibility SSMGraphProperty_Base::GetHighlightVisibility() const
{
	bool bVisible = false;
	if (const USMGraphK2Node_PropertyNode_Base* Node = GetGraphNodeAs<USMGraphK2Node_PropertyNode_Base>())
	{
		bool bCorrectScope = true;
		if (const USMGraphNode_StateNodeBase* OwningState = GetOwningStateNode())
		{
			if (const TSharedPtr<SGraphNode_StateNode> ParentWidget = GetParentStateWidget())
			{
				// Check if a selection is present but doesn't match the scoped panel. In this case we know the
				// user has interacted with this property, but this isn't the graph panel to display. Important,
				// because without both checks we might accidentally stop displaying validation highlights.
				const TWeakPtr<SGraphPanel> OwningPanel = ParentWidget->GetOwnerPanel().ToWeakPtr();
				if (OwningState->GetPropertyInteractionManager().IsPropertyDirectlyInteractedWith(Node,
						false)
					&& !OwningState->GetPropertyInteractionManager().IsPropertyDirectlyInteractedWith(Node,
						false, OwningPanel))
				{
					bCorrectScope = false;
				}
			}
		}

		if (bCorrectScope)
		{
			const USMGraphK2Node_PropertyNode_Base::FHighlightArgs& Args = Node->GetHighlightArgs();
			bVisible = Args.bEnable;
		}
	}

	return bVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

FSlateColor SSMGraphProperty_Base::GetHighlightColor() const
{
	if (const USMGraphK2Node_PropertyNode_Base* Node = GetGraphNodeAs<USMGraphK2Node_PropertyNode_Base>())
	{
		const USMGraphK2Node_PropertyNode_Base::FHighlightArgs& Args = Node->GetHighlightArgs();
		if (Args.bEnable)
		{
			return Node->GetHighlightArgs().Color;
		}
	}

	return FLinearColor::Transparent;
}

EVisibility SSMGraphProperty_Base::GetNotifyVisibility() const
{
	bool bVisible = false;

	if (const USMGraphK2Node_PropertyNode_Base* Node = Cast<USMGraphK2Node_PropertyNode_Base>(GraphNode))
	{
		const USMGraphK2Node_PropertyNode_Base::FNotifyArgs& Args = Node->GetNotifyArgs();
		bVisible = Args.bEnable;
	}

	return bVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

const FSlateBrush* SSMGraphProperty_Base::GetNotifyIconBrush() const
{
	if (const USMGraphK2Node_PropertyNode_Base* Node = Cast<USMGraphK2Node_PropertyNode_Base>(GraphNode))
	{
		const USMGraphK2Node_PropertyNode_Base::FNotifyArgs& Args = Node->GetNotifyArgs();
		switch (Args.LogType)
		{
		case ESMLogType::Note:
			{
				return FSMUnrealAppStyle::Get().GetBrush(TEXT("Icons.Info"));
			}
		case ESMLogType::Warning:
			{
				return FSMUnrealAppStyle::Get().GetBrush(TEXT("Icons.Warning"));
			}
		case ESMLogType::Error:
			{
				return FSMUnrealAppStyle::Get().GetBrush(TEXT("Icons.Error"));
			}
		}
	}

	return FStyleDefaults::GetNoBrush();
}

FSlateColor SSMGraphProperty_Base::GetNotifyIconColor() const
{
	if (const USMGraphK2Node_PropertyNode_Base* Node = Cast<USMGraphK2Node_PropertyNode_Base>(GraphNode))
	{
		const USMGraphK2Node_PropertyNode_Base::FNotifyArgs& Args = Node->GetNotifyArgs();
		switch (Args.LogType)
		{
		case ESMLogType::Note:
			{
				return FSMUnrealAppStyle::Get().GetSlateColor("Colors.AccentBlue");
			}
		case ESMLogType::Warning:
			{
				return FSMUnrealAppStyle::Get().GetSlateColor("Colors.AccentYellow");
			}
		case ESMLogType::Error:
			{
				return FSMUnrealAppStyle::Get().GetSlateColor("Colors.AccentRed");
			}
		}
	}
	return FSlateColor::UseForeground();
}

FText SSMGraphProperty_Base::GetNotifyIconTooltip() const
{
	if (const USMGraphK2Node_PropertyNode_Base* Node = Cast<USMGraphK2Node_PropertyNode_Base>(GraphNode))
	{
		const USMGraphK2Node_PropertyNode_Base::FNotifyArgs& Args = Node->GetNotifyArgs();
		return FText::FromString(Args.Message);
	}

	return FText::GetEmpty();
}

FText SSMGraphProperty_Base::BuildPropertyTooltipText() const
{
	FText TooltipText;
	if (USMGraphK2Node_PropertyNode_Base* Node = GetPropertyNode())
	{
		if (FSMGraphProperty_Base* Prop = Node->GetPropertyNode())
		{
			if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node))
			{
				if (const FProperty* Property = Prop->MemberReference.ResolveMember<FProperty>(Blueprint))
				{
					const FText Description = Property->GetToolTipText();
					if (!Description.IsEmpty())
					{
						TooltipText = Description;
					}
					else
					{
						TooltipText = UEdGraphSchema_K2::TypeToText(Prop->VariableType);
					}
				}
			}
		}
	}

	return TooltipText;
}

#undef LOCTEXT_NAMESPACE
