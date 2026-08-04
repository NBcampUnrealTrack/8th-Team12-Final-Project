// Copyright Recursoft LLC. All Rights Reserved.

#include "Graph/Nodes/RootNodes/SMGraphK2Node_DebugEntryNode.h"

#include "EdGraphSchema_K2.h"

#define LOCTEXT_NAMESPACE "SMDebugEntryNode"

void USMGraphK2Node_DebugEntryNode::AllocateDefaultPins()
{
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
}

FText USMGraphK2Node_DebugEntryNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString(TEXT("On Debug Begin"));
}

#undef LOCTEXT_NAMESPACE
