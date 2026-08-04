// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

/**
 * Displays any kind of property.
 */
class SSMPropertyWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SSMPropertyWidget) {}
		SLATE_ATTRIBUTE(const FProperty*, Property)
		SLATE_ATTRIBUTE(FText, DisplayText)
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);

private:
	FText GetPropertyText() const;
	FSlateColor GetPropertyColor() const;

private:
	TAttribute<const FProperty*> PropertyAttribute;
	TAttribute<FText> DisplayTextAttribute;
};