// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SGraphNode_BaseNode.h"

#include "Widgets/Images/SImage.h"

class USMGraphNode_ConduitNode;
class USMGraphNode_StateNodeBase;
class SSMGraphNode_PropertyContent;

class SGraphNode_StateNode : public SGraphNode_BaseNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_StateNode) : _ContentPadding(FMargin(4.f, 0.f, 4.f, 0.f)) {}
	SLATE_ARGUMENT(FMargin, ContentPadding)
	SLATE_ARGUMENT(TOptional<float>, PinPadding)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, USMGraphNode_StateNodeBase* InNode);

	// SGraphNode
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	virtual void UpdateGraphNode() override;
	virtual void CreatePinWidgets() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	virtual TSharedPtr<SToolTip> GetComplexTooltip() override;
	virtual void GetOverlayBrushes(bool bSelected, const FVector2f& WidgetSize, TArray<FOverlayBrushInfo>& Brushes) const override;
	virtual TArray<FOverlayWidgetInfo> GetOverlayWidgets(bool bSelected, const FVector2f& WidgetSize) const override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void RequestRenameOnSpawn() override;
	virtual bool IsNameReadOnly() const override;
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// ~SGraphNode

	// SNodePanel::SNode
	virtual void GetNodeInfoPopups(FNodeInfoContext* Context, TArray<FGraphInformationPopupInfo>& Popups) const override;
	// ~SNodePanel::SNode

	// SGraphNode_BaseNode
	virtual void OnRefreshRequested(USMGraphNode_Base* InNode, bool bFullRefresh) override;
	// ~SGraphNode_BaseNode

	/**
	 * Data for registering external widgets as an overlay. This ends up being converted to FOverlayWidgetInfo.
	 * The main difference is we need to be able to evaluate offset dynamically, and the engine OverlayOffset isn't an
	 * attribute.
	 */
	struct FExternalOverlayWidget
	{
		/** The widget to display. */
		TSharedPtr<SWidget> Widget;
		/** Offset function, taking this widget's size as input. */
		TFunction<FVector2f(const FVector2f& InSize)> OverlayOffset;
	};
	
	/** Add an overlay widget externally. Returns the assigned Guid of this overlay widget. */
	FGuid AddOverlayWidget(FExternalOverlayWidget&& InOverlayWidgetInfo);
	/** Remove an overlay widget by guid. */
	void RemoveOverlayWidget(const FGuid& InGuid);
	/** Access the externally added overlay widgets. */
	const TMap<FGuid, FExternalOverlayWidget>& GetExternalOverlayWidgets() const { return ExternalOverlayWidgets; }
	
protected:
	virtual TSharedPtr<SWidget> CreateContentBox();
	virtual FText GetNodeTitle() const;
	virtual FSlateColor GetBorderBackgroundColor() const;
	virtual const FSlateBrush* GetNameIcon() const;
	virtual TSharedPtr<SVerticalBox> BuildComplexTooltip();
	virtual UEdGraph* GetGraphToUseForTooltip() const;
	/** Get the content scale as a Vector2D. */
	FVector2D GetContentScale_Internal() const;

	virtual void CalculateAnyStateImpact();
	virtual void CalculateLinkStateImpact();

private:
	const struct FBlueprintBreakpoint* GetBreakpoint() const;
	FText GetBreakpointTooltipText() const;

protected:
	TArray<TSharedPtr<SWidget>> AnyStateImpactWidgets;
	TSharedPtr<SWidget> LinkStateImpactWidget;
	TSharedPtr<SWidget> FastPathWidget;
	TSharedPtr<SWidget> ReplicationWidget;
	mutable TSharedPtr<SImage> BreakpointWidget;
	TSharedPtr<SSMGraphNode_PropertyContent> PropertyContent;
	TSharedPtr<SImage> NodeIcon;
	FMargin ContentPadding;
	/** Mouse is over the main node, not the pin connector. */
	bool bMouseOverCoreNode = false;
	float PinPadding = 4.f;
	const int32 OverlayWidgetPadding = 20;

private:
	/** Overlay widgets added externally. */
	TMap<FGuid, FExternalOverlayWidget> ExternalOverlayWidgets;
};

class SGraphNode_ConduitNode : public SGraphNode_StateNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_ConduitNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, USMGraphNode_ConduitNode* InNode);
	// SNodePanel::SNode
	virtual void GetNodeInfoPopups(FNodeInfoContext* Context, TArray<FGraphInformationPopupInfo>& Popups) const override;
	// ~SNodePanel::SNode
protected:
	virtual const FSlateBrush* GetNameIcon() const override;
};
