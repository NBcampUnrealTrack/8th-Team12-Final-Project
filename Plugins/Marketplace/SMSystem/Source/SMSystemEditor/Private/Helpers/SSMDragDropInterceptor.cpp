// Copyright Recursoft LLC. All Rights Reserved.

#include "SSMDragDropInterceptor.h"

#include "BPVariableDragDropAction.h"
#include "Editor/Kismet/Private/BPFunctionDragDropAction.h"
#include "Framework/Application/SlateApplication.h"

void SSMDragDropInterceptor::Construct(const FArguments& InArgs)
{
	InterceptType = InArgs._InterceptType;
	
	OnDropEvent = InArgs._OnDrop;
	OnDragOverEvent = InArgs._OnDragOver;
	OnDragLeaveEvent = InArgs._OnDragLeave;

	SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SSMDragDropInterceptor::ComputeVisibility));
}

FReply SSMDragDropInterceptor::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (OnDropEvent.IsBound())
	{
		return OnDropEvent.Execute(MyGeometry, DragDropEvent);
	}
	return FReply::Unhandled();
}

FReply SSMDragDropInterceptor::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (OnDragOverEvent.IsBound())
	{
		return OnDragOverEvent.Execute(MyGeometry, DragDropEvent);
	}

	return FReply::Unhandled();
}

void SSMDragDropInterceptor::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	if (OnDragLeaveEvent.IsBound())
	{
		return OnDragLeaveEvent.Execute(DragDropEvent);
	}
}

EVisibility SSMDragDropInterceptor::ComputeVisibility() const
{
	if (FSlateApplication::Get().IsDragDropping())
	{
		const TSharedPtr<FDragDropOperation> Op = FSlateApplication::Get().GetDragDroppingContent();
		if (Op.IsValid()
			&&
			((EnumHasAnyFlags(InterceptType, EInterceptType::OwningBPFunction)
			&& Op->IsOfType<FKismetFunctionDragDropAction>())
			||
			(EnumHasAnyFlags(InterceptType, EInterceptType::OwningBPVariable)
			&& Op->IsOfType<FKismetVariableDragDropAction>())))
		{
			return EVisibility::Visible;
		}
	}
	
	return EVisibility::SelfHitTestInvisible;
}
