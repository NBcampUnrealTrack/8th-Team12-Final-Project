// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

class USMGraphNode_Base;

class SGraphNode_BaseNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_BaseNode) {}
	SLATE_ARGUMENT(FMargin, ContentPadding)
	SLATE_END_ARGS()

	virtual ~SGraphNode_BaseNode() override;
	
	void Construct(const FArguments& InArgs, USMGraphNode_Base* InNode);

	// SGraphNode
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// ~SGraphNode
	
	virtual void OnRefreshRequested(USMGraphNode_Base* InNode, bool bFullRefresh);

	/** Checks if any output pins are hovered. */
	bool IsAnyOutputPinHovered() const;
	
	/** Retrieve the owning sm node cast to a type. */
	template<typename T>
	T* GetOwningNodeAs() const { return Cast<T>(GraphNode); }
	template<typename T>
	T* GetOwningNodeAsChecked() const { return CastChecked<T>(GraphNode); }

private:
	FDelegateHandle NodeRefreshHandle;
};
