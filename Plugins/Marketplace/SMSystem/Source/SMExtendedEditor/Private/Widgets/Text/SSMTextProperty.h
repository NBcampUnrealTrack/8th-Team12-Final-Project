// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Graph/Nodes/SlateNodes/Properties/SSMGraphProperty_Base.h"

#include "SMTextGraphProperty.h"

class SBox;
class SHorizontalBox;
class SSMEditableTextBlock;
class UEdGraphNode;

class SSMTextProperty : public SSMGraphProperty_Base
{
	SLATE_BEGIN_ARGS(SSMTextProperty)
		: _GraphNode(nullptr)
		, _WidgetInfo(nullptr)
		, _RichTextInfo(nullptr)
		{}
		SLATE_ARGUMENT(UEdGraphNode*, GraphNode)
		SLATE_ARGUMENT(const FSMTextNodeWidgetInfo*, WidgetInfo)
		SLATE_ARGUMENT(const FSMTextNodeRichTextInfo*, RichTextInfo)
	SLATE_END_ARGS()
	
	SSMTextProperty();

	void Construct(const FArguments& InArgs);
	virtual void Finalize() override;

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	
	void ToggleTextEdit(bool bValue);

protected:
	virtual bool IsGraphPropertyVisuallyEnabled() const override;
	bool IsReadOnly() const;
	bool IsInGraphEditMode() const;
	FText GetRichTextBody() const;
	FText GetPlainTextBody() const;
	void OnBodyTextCommitted(const FText& InText, ETextCommit::Type CommitInfo);
	float GetWrapText() const;

private:
	FSMTextNodeWidgetInfo WidgetInfo;
	TSharedPtr<SSMEditableTextBlock> InlineEditableTextBody;
	TSharedPtr<SBox> InputPinContainer;
	TSharedPtr<SHorizontalBox> HorizontalBox;
};
