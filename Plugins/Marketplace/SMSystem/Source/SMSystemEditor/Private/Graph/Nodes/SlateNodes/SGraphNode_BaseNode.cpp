// Copyright Recursoft LLC. All Rights Reserved.

#include "SGraphNode_BaseNode.h"

#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Helpers/SMDragDropHelpers.h"

#include "SGraphPin.h"

#define LOCTEXT_NAMESPACE "SGraphBaseNode"

SGraphNode_BaseNode::~SGraphNode_BaseNode()
{
	if (NodeRefreshHandle.IsValid())
	{
		if (USMGraphNode_Base* SMGraphNode = Cast<USMGraphNode_Base>(GraphNode))
		{
			SMGraphNode->OnGraphNodeRefreshRequestedEvent.Remove(NodeRefreshHandle);
		}
	}
}

void SGraphNode_BaseNode::Construct(const FArguments& InArgs, USMGraphNode_Base* InNode)
{
	check(InNode);
	GraphNode = InNode;
	
	NodeRefreshHandle = InNode->OnGraphNodeRefreshRequestedEvent.AddSP(this, &SGraphNode_BaseNode::OnRefreshRequested);
}

FReply SGraphNode_BaseNode::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const FReply Reply = SGraphNode::OnDragOver(MyGeometry, DragDropEvent);
	if (Reply.IsEventHandled())
	{
		return Reply;
	}

	FSMDragDropHelpers::SetDragDropMessage(DragDropEvent, false);
	
	return FReply::Unhandled();
}

void SGraphNode_BaseNode::OnRefreshRequested(USMGraphNode_Base* InNode, bool bFullRefresh)
{
	UpdateGraphNode();
}

bool SGraphNode_BaseNode::IsAnyOutputPinHovered() const
{
	for (const TSharedRef<SGraphPin>& Pin : OutputPins)
	{
		if (Pin->IsHovered())
		{
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
