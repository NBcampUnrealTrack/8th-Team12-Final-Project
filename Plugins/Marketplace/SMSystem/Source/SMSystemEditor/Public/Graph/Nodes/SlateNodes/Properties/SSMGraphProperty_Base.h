// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

enum EEdGraphPinDirection : int;
enum class ESMPropertyInteractionType : uint8;
class USMGraphNode_StateNodeBase;
class SBorder;
class SGraphNode;
class SGraphNode_StateNode;
class SGraphPanel;
class UEdGraphNode;
class UEdGraphPin;
class USMGraphK2Node_PropertyNode_Base;
struct FSlateBrush;

/**
 * Base representation of an exposed graph property. Extend this to implement custom graph properties.
 */
class SMSYSTEMEDITOR_API SSMGraphProperty_Base : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SSMGraphProperty_Base)
		: _GraphNode(nullptr)
	{
	}
		/** Graph node containing the property. */
		SLATE_ARGUMENT(UEdGraphNode*, GraphNode)
	SLATE_END_ARGS()

	SSMGraphProperty_Base();
	virtual ~SSMGraphProperty_Base() override;

	virtual void Finalize();
	virtual void Refresh(const UEdGraphPin* InPin) {}
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;

	/** Special drop when the directionality of the variable is known (ie, input or output variable) */
	virtual FReply OnDirectionalDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, EEdGraphPinDirection PinDirection);

	TWeakObjectPtr<UEdGraphNode> GetGraphNode() const { return GraphNode; }
	template<typename T>
	T* GetGraphNodeAs() const
	{
		return Cast<T>(GetGraphNode());
	}

	/** The property node we represent. */
	USMGraphK2Node_PropertyNode_Base* GetPropertyNode() const;
	USMGraphK2Node_PropertyNode_Base* GetPropertyNodeChecked() const;

	/** Return the sm graph node owning this widget. */
	USMGraphNode_StateNodeBase* GetOwningStateNode() const;
	/** @deprecated This function is deprecated, use GetParentStateWidget() instead. */
	UE_DEPRECATED(5.5, "This function is deprecated, use GetParentStateWidget() instead.")
	TSharedPtr<SGraphNode> FindParentGraphNode() const;
	/** Return the parent slate graph node. */
	TSharedPtr<SGraphNode_StateNode> GetParentStateWidget() const;
	TSharedPtr<SGraphNode_StateNode> GetParentStateWidgetChecked() const;
	
	/** Find a result pin given a direction. */
	UEdGraphPin* FindResultPin(const EEdGraphPinDirection Direction) const;
	/** Find all result pins regardless of direction. */
	TArray<UEdGraphPin*> FindAllResultPins() const;

	/**
	 * Access this property.
	 * @param InteractionType The type of interaction with this property.
	 */
	void InteractWithProperty(const ESMPropertyInteractionType InteractionType);

	/** Checks if the category this property is under is expanded. */
	bool IsCategoryExpanded() const;
	
	/** Padding for our highlight border. */
	static float GetHighlightPadding()
	{
		return HighlightPadding;
	}
	
protected:
	/** Called when the owning graph property node is interacted with. */
	virtual void OnInteractProperty(const ESMPropertyInteractionType InteractionType, const TWeakPtr<SGraphPanel>& GraphPanel, bool bForce) {}

	/**
	 * Special handling for IsEnabled(), to be called by children on specific widgets,
	 * so we still allow interaction while dimming the property.
	 */
	virtual bool IsGraphPropertyVisuallyEnabled() const;
	
	TSharedRef<SBorder> MakeHighlightBorder();
	TSharedRef<SWidget> MakeNotifyIconWidget();

	EVisibility GetHighlightVisibility() const;
	FSlateColor GetHighlightColor() const;

	EVisibility GetNotifyVisibility() const;
	const FSlateBrush* GetNotifyIconBrush() const;
	FSlateColor GetNotifyIconColor() const;
	FText GetNotifyIconTooltip() const;

	/** Creates tooltip text for the given property. */
	FText BuildPropertyTooltipText() const;

protected:
	TWeakObjectPtr<UEdGraphNode> GraphNode;
	static constexpr float HighlightPadding = -6.f;
	static const FMargin NotifyPadding;

private:
	/** Cached ptr to parent state widget. */
	mutable TWeakPtr<SGraphNode_StateNode> CachedParentStateWidget;

	friend class SSMNodeInstancePropertyView;
	/** Pointer back to the tree item that owns us. */
	TWeakPtr<struct FSMGraphPropertyTreeItem> TreeItemWeakPtr;

	/** Guid assigned to our widget from the owning state node. */
	FGuid RegisteredWidgetGuid;
};
