// Copyright Recursoft LLC. All Rights Reserved.

#include "SSMAboutDialog.h"

#include "Configuration/SMUtilityLauncherStyle.h"
#include "SMSystemInfo.h"

#include "SMUnrealTypeDefs.h"

#include "HAL/PlatformApplicationMisc.h"
#include "SPrimaryButton.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SSMAboutDialog"

void SSMAboutDialog::Construct(const FArguments& InArgs, const TSharedPtr<FSMSystemInfo>& InSupportInfo)
{
	SystemInfo = InSupportInfo;
	check(SystemInfo.IsValid());

	constexpr float HorizontalOffset = 4.f;
	constexpr float VerticalOffset = 4.f;
	
	SWindow::Construct(SWindow::FArguments()
	.Title(LOCTEXT("LogicDriverAboutPage", "About Logic Driver"))
	.SizingRule(ESizingRule::FixedSize)
	.ClientSize(FVector2D(700.f, 580.f))
	.SupportsMaximize(false)
	.SupportsMinimize(false)
	[
		SNew(SBorder)
		.BorderImage(FSMUnrealAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(16.f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(0.f, 4.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.AutoHeight()
			[
				SNew(SImage)
				.Image(FSMUtilityLauncherStyle::Get()->GetBrush(TEXT("About.Logo")))
			]
			+SVerticalBox::Slot()
			.Padding(0.f, 16.f)
			.AutoHeight()
			[
				SNew(SGridPanel)
				+SGridPanel::Slot(0, 0)
				.Padding(FMargin(0, VerticalOffset))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Platform", "Platform:"))
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("BoldFont"))
					.Justification(ETextJustify::Left)
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]
				+SGridPanel::Slot(1, 0)
				.Padding(FMargin(HorizontalOffset, VerticalOffset))
				[
					SNew(STextBlock)
					.Text(SystemInfo->OSVersion)
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("PropertyWindow.LargeFont"))
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]

				+SGridPanel::Slot(0, 1)
				.Padding(FMargin(0, VerticalOffset))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EngineVersion", "Engine:"))
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("BoldFont"))
					.Justification(ETextJustify::Left)
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]
				+SGridPanel::Slot(1, 1)
				.Padding(FMargin(HorizontalOffset, VerticalOffset))
				[
					SNew(STextBlock)
					.Text(FText::FromString(SystemInfo->EngineVersion))
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("PropertyWindow.LargeFont"))
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]

				+SGridPanel::Slot(0, 2)
				.Padding(FMargin(0, VerticalOffset))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PluginVersion", "Plugin:"))
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("BoldFont"))
					.Justification(ETextJustify::Left)
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]
				+SGridPanel::Slot(1, 2)
				.Padding(FMargin(HorizontalOffset, VerticalOffset))
				[
					SNew(STextBlock)
					.Text(FText::FromString(SystemInfo->PluginVersion))
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("PropertyWindow.LargeFont"))
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]

				+SGridPanel::Slot(0, 3)
				.Padding(FMargin(0, VerticalOffset))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BuildType", "Build:"))
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("BoldFont"))
					.Justification(ETextJustify::Left)
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]
				+SGridPanel::Slot(1, 3)
				.Padding(FMargin(HorizontalOffset, VerticalOffset))
				[
					SNew(STextBlock)
					.Text(FText::FromString(SystemInfo->GetBuildType()))
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("PropertyWindow.LargeFont"))
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]
			]
			+SVerticalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Fill)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 2.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Copyright", "Logic Driver Pro © Recursoft LLC. All rights reserved."))
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("PropertyWindow.LargeFont"))
					.Justification(ETextJustify::Left)
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Trademark", "Logic Driver® is a registered trademark of Recursoft LLC."))
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("PropertyWindow.LargeFont"))
					.Justification(ETextJustify::Left)
					.ColorAndOpacity(FStyleColors::ForegroundHover)
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Bottom)
#if !LOGICDRIVER_IS_MARKETPLACE_BUILD
			.HAlign(HAlign_Right)
#endif
			[
				SNew(SHorizontalBox)
#if LOGICDRIVER_IS_MARKETPLACE_BUILD
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.Padding(0.f, 0.f, 0.f, 0.f)
				[
					SNew(SHyperlink)
						.Style(FSMUnrealAppStyle::Get(), TEXT("NavigationHyperlink"))
						.Text(LOCTEXT("ReviewButton_Text", "Write a review on FAB"))
						.ToolTipText(LOCTEXT("ReviewButton_Tooltip", "Leaving a review on FAB is greatly appreciated and helps support ongoing development."))
						.OnNavigate_Lambda([]()
						{
							FPlatformProcess::LaunchURL(TEXT("https://logicdriver.com/marketplace-pro"), nullptr, nullptr);
						})
				]
#endif
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Fill)
				.Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Text(LOCTEXT("CopyButton_Text", "Copy System Info"))
					.ToolTipText(LOCTEXT("CopyButton_Tooltip", "Copy system information to the clipboard."))
					.OnClicked(this, &SSMAboutDialog::OnCopyClicked)
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				[
					SNew(SPrimaryButton)
					.Text(LOCTEXT("CloseButton_Text", "Close"))
					.ToolTipText(LOCTEXT("CloseButton_Tooltip", "Close the window."))
					.OnClicked_Lambda([this]()
					{
						RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]
	]);
}

FReply SSMAboutDialog::OnCopyClicked()
{
	check(SystemInfo.IsValid());
	
	FPlatformApplicationMisc::ClipboardCopy(*SystemInfo->ToString());
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
