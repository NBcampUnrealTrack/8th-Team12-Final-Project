// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Framework/SlateDelegates.h"
#include "Widgets/SLeafWidget.h"

/**
 * Intercept drag-drops so we can always apply custom handling if required. Should be used within a panel slot.
 * This widget never renders anything.
 */
class SSMDragDropInterceptor final : public SLeafWidget
{
public:
	enum class EInterceptType : uint8
	{
		None             = 0,
		OwningBPVariable = 1 << 0,
		OwningBPFunction = 1 << 1
	};
	FRIEND_ENUM_CLASS_FLAGS(EInterceptType)
	
	SLATE_BEGIN_ARGS(SSMDragDropInterceptor) : _InterceptType(EInterceptType::OwningBPFunction | EInterceptType::OwningBPVariable)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		/** The specific type of interception this is for. */
		SLATE_ARGUMENT(EInterceptType, InterceptType)
		SLATE_EVENT(FOnDrop, OnDrop)
		SLATE_EVENT(FOnDragOver, OnDragOver)
		SLATE_EVENT(FOnDragLeave, OnDragLeave)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		return LayerId;
	}
	
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

	/**
	 * Intercept drag-drops depending on the intercept type.
	 */
	EVisibility ComputeVisibility() const;

private:
	EInterceptType InterceptType;
	FOnDrop OnDropEvent;
	FOnDragOver OnDragOverEvent;
	FOnDragLeave OnDragLeaveEvent;
};

ENUM_CLASS_FLAGS(SSMDragDropInterceptor::EInterceptType)
