// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class UEdGraphNode;
class UEdGraphPin;

/**
 * Logic Driver's custom linear expression widget. We would rather derive from SKismetLinearExpression,
 * but none of its methods are virtual, so it makes more sense just to reproduce it, and introduce an
 * interface K2 nodes can implement to produce their own custom widget when being evaluated.
 *
 * @see ISMGraphK2Node_LinearExpressionInterface
 */
class SSMKismetLinearExpression : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SSMKismetLinearExpression)
	{}
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const UEdGraphPin* InitialInputPin);

	void SetExpressionRoot(const UEdGraphPin* InputPin);
	
protected:
	TSharedRef<SWidget> MakeNodeWidget(const UEdGraphNode* Node, const UEdGraphPin* FromPin);
	TSharedRef<SWidget> MakePinWidget(const UEdGraphPin* Pin);

protected:
	TSet<const UEdGraphNode*> VisitedNodes;
};