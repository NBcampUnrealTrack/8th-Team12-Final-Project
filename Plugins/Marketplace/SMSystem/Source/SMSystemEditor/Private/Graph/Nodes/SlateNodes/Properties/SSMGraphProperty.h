// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Graph/Nodes/SlateNodes/Properties/SSMGraphProperty_Base.h"
#include "Properties/PropertyInteraction/SMPropertyInteractionData.h"
#include "SMNodeWidgetInfo.h"

#include "Styling/SlateColor.h"
#include "Templates/SharedPointer.h"

class USMBlueprint;
class UEdGraphNode;
class UEdGraphPin;
class SSMKismetLinearExpression;
class SSMGraphProperty_Connector;
class SBox;
class SGraphPin;

/**
 * Visual representation of an exposed graph property.
 */
class SSMGraphProperty : public SSMGraphProperty_Base
{
	SLATE_BEGIN_ARGS(SSMGraphProperty)
		: _GraphNode(nullptr)
		, _WidgetInfo(nullptr)
		{}
		/** Graph node containing the property. */
		SLATE_ARGUMENT(UEdGraphNode*, GraphNode)
		SLATE_ARGUMENT(const FSMTextDisplayWidgetInfo*, WidgetInfo)
	SLATE_END_ARGS()
	
	SSMGraphProperty();
	virtual ~SSMGraphProperty() override;
	
	void Construct(const FArguments& InArgs);
	virtual void Finalize() override;

	// SWidget
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// ~SWidget

	/** Internal handling of a drop based on input or output variables. */
	virtual FReply OnDirectionalDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, EEdGraphPinDirection PinDirection) override;
	
	virtual void Refresh(const UEdGraphPin* InPin) override;

	/** If the graph property is checking/initiating a data change. */
	struct FPinDataChangeArgs
	{
		FPinDataChangeArgs() : bJustConstructed(false), bForceChange(false),
		                       InteractionType(ESMPropertyInteractionType::None)
		{
		}

		/** If we have just been constructed. */
		uint8 bJustConstructed: 1;
		/** Forces the operation through. */
		uint8 bForceChange: 1;
		/** Should the pin be selected. */
		ESMPropertyInteractionType InteractionType;
		/** The panel receiving the interaction. */
		TWeakPtr<SGraphPanel> GraphPanel;
	};
	
	/** Stored pin data. */
	struct FPinData
	{
		DECLARE_DELEGATE_OneParam(FOnChange, const FPinDataChangeArgs&);

		FPinData(): ResultPin(nullptr) {}

		/** The graph pin widget. Can be null. */
		TWeakPtr<SGraphPin> PinPtr;
		/** The corresponding result pin. Should always be valid. */
		UEdGraphPin* ResultPin;
		/** The corresponding linear expression widget used when an expression is connected to the pin. */
		TWeakPtr<SSMKismetLinearExpression> ExpressionWidgetPtr;
		/** The connector widget. */
		TWeakPtr<SSMGraphProperty_Connector> ConnectorWidget;
		/** When a pin is activated (connector change). */
		FOnChange OnConnectorActivatedDelegate;
		/** When a pin is deactivated (connector change). */
		FOnChange OnConnectorDeactivatedDelegate;
		/** Handle for when checking for unhover. */
		TWeakPtr<FActiveTimerHandle> UnhoveredTimerHandle;
	};
	
private:
	virtual void OnInteractProperty(const ESMPropertyInteractionType InteractionType,
		const TWeakPtr<SGraphPanel>& GraphPanel, bool bForce) override;

	/** Creates a connector for a given result pin. */
	void CreateConnector(UEdGraphPin* ResultPin, const TSharedPtr<FPinData>& Data);
	
	void HandleExpressionChange();
	void HandleExpressionChange(const UEdGraphPin* ResultPin);

	void HandleInteractionChange(const FPinDataChangeArgs& InDataChange = FPinDataChangeArgs());
	void HandleInteractionChange(const UEdGraphPin* ResultPin, const FPinDataChangeArgs& InDataChange = FPinDataChangeArgs());
	FSlateColor GetBackgroundColor() const;

	/** Attempt to find pin data given a direction. */
	TSharedPtr<FPinData> TryGetPinDataFromDirection(EEdGraphPinDirection Direction) const;
	
private:
	/** Container for all pins on the graph property. */
	TSharedPtr<SBox> PinContainer;
	/** Map of direction to pin data for this property. */
	TMap<EEdGraphPinDirection, TSharedPtr<FPinData>> PinDataMap;
	/** The blueprint owning this property. */
	TWeakObjectPtr<USMBlueprint> OwningBlueprint;
	
	FSMTextDisplayWidgetInfo WidgetInfo;
	bool bIsValidDragDrop;
	
};
