// Copyright Recursoft LLC. All Rights Reserved.

#include "SSMPropertyWidget.h"

#include "EdGraphSchema_K2.h"
#include "SMUnrealTypeDefs.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SSMPropertyWidget"

void SSMPropertyWidget::Construct(const FArguments& InArgs)
{
	PropertyAttribute = InArgs._Property;
	DisplayTextAttribute = InArgs._DisplayText;
	
	const TSharedPtr<STextBlock> TitleWidget = SNew(STextBlock)
	.TextStyle(FSMUnrealAppStyle::Get(), "NormalText.Important")
	.Text(this, &SSMPropertyWidget::GetPropertyText);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(FSMUnrealAppStyle::GetBrush("Graph.VarNode.Body"))
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		[
			SNew(SImage)
			.Image(FSMUnrealAppStyle::GetBrush("Graph.VarNode.ColorSpill"))
			.ColorAndOpacity(this, &SSMPropertyWidget::GetPropertyColor)
			.DesiredSizeOverride(FVector2D(113.136,24)) // 132,28 default, just make slightly smaller.
		]
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(FSMUnrealAppStyle::GetBrush("Graph.VarNode.Gloss"))
		]
		+SOverlay::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			TitleWidget.ToSharedRef()
		]
	];
}

FText SSMPropertyWidget::GetPropertyText() const
{
	if (DisplayTextAttribute.IsBound() || DisplayTextAttribute.IsSet())
	{
		const FText DisplayText = DisplayTextAttribute.Get();
		if (!DisplayText.IsEmpty())
		{
			return DisplayText;
		}
	}

	if (PropertyAttribute.IsBound() || PropertyAttribute.IsSet())
	{
		if (const FProperty* Property = PropertyAttribute.Get())
		{
			return Property->GetDisplayNameText();
		}
	}
	
	return LOCTEXT("InvalidProperty", "Invalid Property");
}

FSlateColor SSMPropertyWidget::GetPropertyColor() const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	FEdGraphPinType VariablePinType;
	if (K2Schema->ConvertPropertyToPinType(PropertyAttribute.Get(), VariablePinType))
	{
		return K2Schema->GetPinTypeColor(VariablePinType);
	}

	return FLinearColor::White;
}

#undef LOCTEXT_NAMESPACE
