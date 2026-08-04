// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SGraphPin.h"
#include "Widgets/SCompoundWidget.h"

enum class ESMPropertyInteractionType : uint8;
struct FSMPropertyInteractionData;
class FSMPropertyInteractionManager;
class UEdGraphPin;
class SGraphNode_StateNode;
class SGraphPin_PropertySelectionPin;
class SSMGraphProperty_Base;
class SSMGraphProperty_Connector;
class USMGraphK2Node_PropertyIOReaderNode;
class USMGraphK2Node_PropertyIOWriterNode;
class USMGraphK2Node_PropertyNode_Base;
class USMGraphNode_StateNodeBase;
class ISMStateMachineBlueprintEditor;

class SSMGraphProperty_ConnectorPin final : public SGraphPin
{
	DECLARE_DELEGATE_TwoParams(FOnPinClicked, const FGeometry& SenderGeometry, const FPointerEvent& MouseEvent);
	
	SLATE_BEGIN_ARGS(SSMGraphProperty_ConnectorPin)
	{}
		SLATE_EVENT(FOnPinClicked, OnPinClicked)
		SLATE_ARGUMENT(TWeakPtr<SSMGraphProperty_Connector>, OwningConnector)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const UEdGraphPin* InResultPin, const UEdGraphPin* InPropertyPin);

	/** Set the property pin. */
	void SetPropertyPin(const UEdGraphPin* InPropertyPin);

	/** Retrieve the property pin this connector is for. */
	UEdGraphPin* GetPropertyPin() const { return PropertyPin; }

	/** The owning connector. May not be set. */
	const TWeakPtr<SSMGraphProperty_Connector>& GetOwningConnector() const { return OwningConnector; }
	
	/** If this pin represents an IO node, otherwise it represents the main property, or could be null. */
	bool DoesPinRepresentIONode() const;

	// SGraphPin
	/** Base class isn't virtual, needed for pin visuals. */
	bool IsConnected() const;

	/** Exact same as parent, but this will call our IsConnected(). */
	virtual const FSlateBrush* GetPinIcon() const override;

	virtual TSharedRef<FDragDropOperation> SpawnPinDragEvent(const TSharedRef<SGraphPanel>& InGraphPanel, const TArray<TSharedRef<SGraphPin>>& InStartingPins) override;

	virtual FReply OnPinMouseDown(const FGeometry& SenderGeometry, const FPointerEvent& MouseEvent) override;
	// ~SGraphPin

protected:
	/** Custom handling for breaking a connection. */
	void BreakPinLinks();
	
private:
	UEdGraphPin* PropertyPin = nullptr;
	
	FOnPinClicked OnPinClickedEvent;

	/** Connector owning this pin. */
	TWeakPtr<SSMGraphProperty_Connector> OwningConnector;
};

/** Represents a connector in the graph property system. */
class SSMGraphProperty_Connector : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SSMGraphProperty_Connector) {}
		SLATE_ARGUMENT(TSharedPtr<SSMGraphProperty_Base>, ParentGraphPropertyWidget)
		SLATE_ARGUMENT(TSharedPtr<SGraphNode_StateNode>, ParentStateWidget)
	SLATE_END_ARGS()

	SSMGraphProperty_Connector();
	virtual ~SSMGraphProperty_Connector() override;
	
	void Construct(const FArguments& InArgs, const UEdGraphPin* InResultPinObj);

	void OpenConnector();
	void CloseConnector();

	// SWidget
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	// ~SWidget
	
	/** We want to qualify has hovered if the combo box is opened. Setting the hovered attribute doesn't really work for this. */
	bool IsHovered() const;

	/** If we are currently selected. */
	bool IsSelected() const;

	/** Freezes the display mode until a visibility change occurs. */
	void FreezeDisplayModeUntilVisibilityChange(bool bValue);

	/**
	 * The direction of the connector. Each connector will have an assigned direction, even when the owning
	 * property node is bidirectional.
	 */
	EEdGraphPinDirection GetConnectorDirection() const;
	
	/** If this is drawn in compact mode. */
	bool IsInCompactMode() const;

	/** If this is being interacted with in some way. */
	bool IsInteractedWith() const;
	
	/** Called when interaction level changes. */
	void OnInteractionChange(ESMPropertyInteractionType InteractionType);

	/** Notify the connector that its mode has changed. */
	void NotifyModeChange();
	
private:
	/** Creates the input pin if needed. */
	void CreateInputPin();
	
	/** Add a property pin. */
	void AddOutputPin();

	/** Remove a property pin. */
	void RemoveOutputPin(UEdGraphPin* InPin);

	/** Remove all output pins. */
	void RemoveAllOutputPins();

	/** Reset the pin value to defaults. */
	void ResetToDefaultValue(UEdGraphPin* InPin);

	/** Can the pin reset to the default value? */
	bool CanResetToDefaultValue(UEdGraphPin* InPin);
	
	/**
	 * The visibility of the connector. Connectors should only be added to the owning widget when they are most
	 * likely will be visible, and they should be removed when they are no longer needed, preventing any draw calls
	 * or visibility checks.
	 *
	 * The normal removal routine has a timer before removing completely to help with UX. This visibility check is
	 * to assist in specific cases where we want to change visibility immediately.
	 */
	EVisibility GetConnectorVisibility() const;

	/** Get the last visibility state of the connector. */
	bool GetConnectorLastVisible() const { return bLastVisible; }

	/** If this connector can be modified by the user. */
	bool CanConnectorBeModified() const;
	
	/** Return the primary property node owner. */
	USMGraphK2Node_PropertyNode_Base* GetOwningPropertyNode() const;
	USMGraphK2Node_PropertyNode_Base* GetOwningPropertyNodeChecked() const;

	/** Return the state node containing this connector. */
	USMGraphNode_StateNodeBase* GetOwningStateNode() const;
	USMGraphNode_StateNodeBase* GetOwningStateNodeChecked() const;
	
	/** Recreate or update contained pins. */
	void UpdatePins();
	
	/** Create any contained pins. */
	void CreateNestedPins();

	/** Remove our nested pins. */
	void ClearNestedPins();
	
	/** Open our context menu. */
	void OpenContextMenu(const FVector2D& ScreenSpacePosition);

	/** Add 'Remove All Outputs' button to the context menu. */
	void AddRemoveAllOutputsContextButton(FMenuBuilder& InMenuBuilder);

	/** Construct the context menu for the property selection. */
	TSharedPtr<SWidget> OnPropertyContextMenuOpening(UEdGraphPin* InPropertyPin);
	
	/** Create a specific input pin. */
	TSharedRef<SWidget> ConstructInputPin(UEdGraphPin* InInputPin);
	
	/** Create a specific output pin. */
	TSharedRef<SWidget> ConstructOutputPin(UEdGraphPin* InOutputPin);

	/** Construct a property selection pin widget for a pin. */
	TSharedRef<SGraphPin_PropertySelectionPin> ConstructPropertySelectionPin(UEdGraphPin* InPin);

	/** Construct the pin used to establish a connection. */
	TSharedRef<SSMGraphProperty_ConnectorPin> ConstructGraphConnectionPin(const UEdGraphPin* InPropertyPin);

	/** Construct the compact version of this widget. */
	TSharedRef<SWidget> ConstructCompactWidget();
	
	/** Construct the full version of this widget. */
	TSharedRef<SWidget> ConstructFullDisplayWidget();

	/** Select the owning property. */
	void SelectOwningProperty();

	/** Checks if another property in the owning state has a visible connector, that isn't compact, that isn't this connector. */
	bool IsAnotherPropertyConnectorFullyVisible() const;

	/** If the user can add or remove pin at any point. */
	bool CanUserModifyPins() const;

	/** Get visibility level of pin modification. */
	EVisibility GetModifyPinsVisibility() const { return CanUserModifyPins() ? EVisibility::Visible : EVisibility::Collapsed; }

	/** Retrieve the padding for the modify pins button. */
	FMargin GetModifyPinsPadding() const;
	
	/** Text display for modifying pins. */
	FText GetModifyPinsText() const;

	/** Text display for the modify pins tooltip. */
	FText GetModifyPinsTooltipText() const;

	/** When the user clicks on the modify pins button. */
	FReply OnModifyPinsClicked();
	
	/** Whether the selection border around the widget is visible. */
	EVisibility GetHighlightBorderVisibility() const;

	/** Get the visibility for the display mode. */
	EVisibility GetDisplayModeVisibility(bool bIsCompact) const;

	/** Border highlight color. */
	FSlateColor GetHighlightColor() const;

	/** Main background color of the owning state template. */
	FSlateColor GetStateBackgroundColor() const;

	/** Color of the owning property. */
	FSlateColor GetPropertyBackgroundColor() const;
	
	/** The offset of this in relation to the owning state node. */
	FVector2f GetOffset(const FVector2f& InStateSize) const;

	/** Get the IO reader node if it exists. */
	USMGraphK2Node_PropertyIOReaderNode* GetIOReaderNode(bool bCreateIfNeeded = false, bool* bJustCreated = nullptr) const;
	
	/** Get the IO writer node containing the real UEdGraphPins this connector visualizes. */
	USMGraphK2Node_PropertyIOWriterNode* GetIOWriterNode(bool bCreateIfNeeded = false, bool* bJustCreated = nullptr) const;

	/** Find the owning blueprint editor. */
	ISMStateMachineBlueprintEditor* GetOwningBlueprintEditor() const;

	/** Retrieve the interaction manager. */
	FSMPropertyInteractionManager& GetPropertyInteractionManager() const;

	/** When our visibility status has changed. */
	void OnConnectorVisibilityChange(bool bNewVisibility);
	
	/** When the owning graph has changed. */
	void OnGraphChanged(const FEdGraphEditAction &Action);

	/** When a property selection combo box has been opened or closed. */
	void OnPropertySelectionOpenChanged(bool bOpen, UEdGraphPin* InPin);

	/** Find the best property pin to use to represent the connector. */
	UEdGraphPin* FindBestPropertyPinForDisplay() const;

	/** If this property is forced to display regardless of direct interaction. */
	bool IsForcedToDisplayForInteraction(const FSMPropertyInteractionData& InInteractionData) const;

	/** Callback when an entire pin widget receives a drop originating from My BP panel. */
	FReply OnPinDropFromMyBP(const FGeometry& InMyGeometry, const FDragDropEvent& InDragDropEvent, const UEdGraphPin* InPin);
	
	/** Callback when an entire pin widget is dragged over originating from My BP panel. */
	FReply OnPinDragOverFromMyBP(const FGeometry& InMyGeometry, const FDragDropEvent& InDragDropEvent, const UEdGraphPin* InPin);

	/** Callback when leaving an entire pin while dragging originating from My BP panel. */
	void OnPinDragLeaveFromMyBP(const FDragDropEvent& InDragDropEvent);
	
private:
	/** Our nested pin container. */
	TSharedPtr<SVerticalBox> PinContainer;
	
	/**
	 * The primary result pin in relation to this connector.
	 * This connector doesn't necessarily represent this exact pin, but a pin on a connected node to this result pin.
	 */
	const UEdGraphPin* PrimaryResultGraphPin = nullptr;
	/** Immediate property graph containing the real property node. */
	TWeakObjectPtr<UEdGraph> PropertyGraph;
	/** The main property owning this connector. */
	TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base> OwningPropertyNode;
	/** The slate widget belonging to the parent state node. */
	TWeakPtr<SGraphNode_StateNode> ParentStateWidget;
	/** The Graph property owning us. */
	TWeakPtr<SSMGraphProperty_Base> ParentGraphPropertyWidget;
	/** The connector pin to represent the node in compact form. */
	TSharedPtr<SSMGraphProperty_ConnectorPin> CompactConnectorPin;
	/** When the owning graph changes. */
	FDelegateHandle GraphChangedHandle;
	/** Color of the background. */
	FLinearColor PropertyBackgroundColor;
	
	/** The current interaction type. */
	TWeakPtr<FSMPropertyInteractionData> CurrentInteractionData;

	/** The last interaction type. */
	TSharedPtr<FSMPropertyInteractionData> LastInteractionData;
	
	/** Guid assigned to us by the owning state. */
	FGuid StateOverlayGuid;
	
	/** If the property selection combo box is opened. */
	uint8 bIsComboBoxOpened: 1;
	
	/** If our display mode is frozen. */
	uint8 bFreezeDisplayMode: 1;

	/** Last visibility state of the connector. */
	uint8 bLastVisible: 1;

	/** Indicates whether the compact mode was previously active. */
	mutable uint8 bWasInCompactMode: 1;

	/** Currently in compact mode. */
	mutable uint8 bInCompactMode: 1;
};
