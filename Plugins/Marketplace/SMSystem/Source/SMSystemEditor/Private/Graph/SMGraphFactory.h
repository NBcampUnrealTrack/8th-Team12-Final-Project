// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "EdGraphUtilities.h"

struct FSMGraphPanelNodeFactory : FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override;
};

struct FSMGraphPinFactory : FGraphPanelPinFactory
{
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override;
};
