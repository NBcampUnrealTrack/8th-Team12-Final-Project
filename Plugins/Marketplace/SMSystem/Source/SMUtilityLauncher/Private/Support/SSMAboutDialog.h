// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

struct FSMSystemInfo;

class SSMAboutDialog : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SSMAboutDialog)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedPtr<FSMSystemInfo>& InSupportInfo);

private:
	FReply OnCopyClicked();
	
private:
	TSharedPtr<FSMSystemInfo> SystemInfo;
};
