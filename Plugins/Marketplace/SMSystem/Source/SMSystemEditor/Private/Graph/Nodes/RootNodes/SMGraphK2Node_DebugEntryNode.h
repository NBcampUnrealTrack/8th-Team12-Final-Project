// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Graph/Nodes/RootNodes/SMGraphK2Node_RuntimeNodeContainer.h"

#include "SMGraphK2Node_DebugEntryNode.generated.h"

UCLASS()
class USMGraphK2Node_DebugEntryNode : public USMGraphK2Node_RuntimeNodeReference
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	// ~UEdGraphNode
};
