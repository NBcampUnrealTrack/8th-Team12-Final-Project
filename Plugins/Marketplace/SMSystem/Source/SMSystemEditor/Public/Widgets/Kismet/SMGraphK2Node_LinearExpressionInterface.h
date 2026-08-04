// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "UObject/Interface.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "SMGraphK2Node_LinearExpressionInterface.generated.h"

class UEdGraphPin;
class SWidget;

UINTERFACE(meta=(CannotImplementInterfaceInBlueprint))
class USMGraphK2Node_LinearExpressionInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for creating a custom widget when evaluating a linear expression within Logic Driver assets.
 */
class ISMGraphK2Node_LinearExpressionInterface
{
	GENERATED_BODY()

public:
	/**
	 * Generate a widget for displaying a node in a linear expression.
	 * @return A widget to display in an expression, or nullptr to use the default widget.
	 */
	virtual TSharedPtr<SWidget> MakeLinearExpressionNodeWidget() const { return nullptr;}

	/**
	 * Generate a widget for displaying a pin owned by this node in a linear expression.
	 * @return A widget to display in an expression, or nullptr to use the default widget.
	 */
	virtual TSharedPtr<SWidget> MakeLinearExpressionPinWidget(const UEdGraphPin* InPin) const { return nullptr; }

	/** Whether to display the evaluation "( )" around the pin. */
	virtual bool DisplayParenthesis() const { return true; }
};
