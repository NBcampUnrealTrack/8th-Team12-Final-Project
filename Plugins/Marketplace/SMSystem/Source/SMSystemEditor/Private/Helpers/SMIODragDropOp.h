// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "GraphEditorDragDropAction.h"
#include "SGraphPin.h"
#include "UObject/WeakObjectPtrTemplates.h"

class SBox;
class SGraphPin;
class UBlueprint;
class USMGraph;
class SSMGraphProperty_ConnectorPin;

namespace LD::Editor::PropertyUtils::IO
{
	struct FPropertyData_Base;
}

class FSMIODragDropOp : public FGraphEditorDragDropAction
{
public:
	DRAG_DROP_OPERATOR_TYPE(FSMIODragDropOp, FGraphEditorDragDropAction)

	virtual ~FSMIODragDropOp() override;
	
	static TSharedRef<FSMIODragDropOp> New(const FGraphPinHandle& InStartingPin,
		UBlueprint* InBlueprint, const TWeakPtr<SSMGraphProperty_ConnectorPin>& InConnectorPin);

	virtual void Construct() override;
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
	virtual void HoverTargetChanged() override;
	virtual FReply DroppedOnPin(FVector2D ScreenPosition, FVector2D GraphPosition) override;

	/** The connector pin which spawned us. */
	const TWeakPtr<SSMGraphProperty_ConnectorPin>& GetOwningConnectorPin() const { return OwningConnectorPin; }
	
protected:
	/** Checks if the hovered pin is completely valid. */
	bool IsHoveredPinValid() const;
	
	/** Find the pin we are dragging. */
	UEdGraphPin* GetDraggedPin() const;

	const FProperty* GetDraggedProperty() const;
	FText GetDraggedPropertyText() const;
	
	FSMIODragDropOp(const FGraphPinHandle& InStartingPin, UBlueprint* InBlueprint,
		const TWeakPtr<SSMGraphProperty_ConnectorPin>& InConnectorPin);

private:
	TWeakObjectPtr<UBlueprint> Blueprint;
	TWeakObjectPtr<USMGraph> OwningSMGraph;
	EEdGraphPinDirection PinDirection;
	FGraphPinHandle StartingPin;
	TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base> DraggedPropertyData;
	/** The connector pin which spawned us. */
	TWeakPtr<SSMGraphProperty_ConnectorPin> OwningConnectorPin;
};
