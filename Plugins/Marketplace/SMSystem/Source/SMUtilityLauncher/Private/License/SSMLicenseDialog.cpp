// Copyright Recursoft LLC. All Rights Reserved.

#include "SSMLicenseDialog.h"

#include "SMUnrealTypeDefs.h"
#include "Utilities/SMEditorAuthentication.h"

#include "Support/SMSupportUtils.h"
#include "Support/SMSystemInfo.h"

#include "SPrimaryButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SSMLicenseDialog"

SSMLicenseDialog::~SSMLicenseDialog()
{
#if LOGICDRIVER_IS_MARKETPLACE_BUILD != LOGICDRIVER_IS_AUTH_BUILD
	// Reset if failed, so we don't leave any notifications for the user.
	// On non-marketplace builds the license could still be valid, such as if an enterprise user
	// tried running the marketplace validation.
	if (!FSMAuthenticator::Get().IsAuthenticated())
	{
		FSMAuthenticator::Get().ResetAuthentication();
	}
#endif
}

void SSMLicenseDialog::Construct(const FArguments& InArgs)
{
	constexpr float HorizontalOffset = 6.f;
	constexpr float VerticalOffset = 4.f;

	SystemInfo = LD::Support::GenerateSystemInfo();
	
	SWindow::Construct(SWindow::FArguments()
	.Title(LOCTEXT("LogicDriverLicenseInfo", "Logic Driver License Info"))
	.SizingRule(ESizingRule::Autosized)
	.LayoutBorder(FMargin(5, 0))
	.SupportsMaximize(false)
	.SupportsMinimize(false)
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.Padding(0.f, 8.f, 0.f, 0.f)
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			SNew(SGridPanel)
			+SGridPanel::Slot(0, 0)
			.Padding(FMargin(HorizontalOffset, VerticalOffset))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LicenseType", "License Type:"))
				.Font(FSMUnrealAppStyle::Get().GetFontStyle("StandardDialog.LargeFont"))
			]
			+SGridPanel::Slot(1, 0)
			.Padding(FMargin(HorizontalOffset, VerticalOffset))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SSMLicenseDialog::GetLicenseTypeText)
				.Font(FSMUnrealAppStyle::Get().GetFontStyle("StandardDialog.LargeFont"))
			]
			+SGridPanel::Slot(0, 1)
			.Padding(FMargin(HorizontalOffset, VerticalOffset))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LicenseValid", "License Valid:"))
				.Font(FSMUnrealAppStyle::Get().GetFontStyle("StandardDialog.LargeFont"))
			]
			+SGridPanel::Slot(1, 1)
			.Padding(FMargin(HorizontalOffset, VerticalOffset))
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(this, &SSMLicenseDialog::GetValidLicenseText)
					.Font(FSMUnrealAppStyle::Get().GetFontStyle("StandardDialog.LargeFont"))
					.ColorAndOpacity(this, &SSMLicenseDialog::GetValidLicenseTextColor)
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.f, 0.f)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Visibility_Lambda([this]()
					{
						return IsMarketplaceLicense()
							&& FSMAuthenticator::Get().HasAuthenticationRun()
							&& !FSMAuthenticator::Get().IsAuthenticated(true) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Image(FSMUnrealAppStyle::Get().GetBrush("Icons.Help"))
					.ToolTipText(LOCTEXT("FailureInfo_Text", "This may indicate that the signed-in Epic Games account does not own Logic Driver Pro, or that\ncommunication with the Epic Games Launcher failed (for example, if the launcher crashes or times out)."))
				]
			]
			+SGridPanel::Slot(0, 2)
			.Padding(FMargin(HorizontalOffset, VerticalOffset))
			.VAlign(VAlign_Center)
			[
				SNew(SPrimaryButton)
				.Text(LOCTEXT("ValidateLicense_Text", "Validate License"))
				.ToolTipText(LOCTEXT("ValidateLicense_Tooltip",
					"Run an entitlement check against the logged in user's Epic Games account. This is only applicable for Marketplace licenses."))
				.OnClicked(this, &SSMLicenseDialog::OnValidateMarketplaceLicenseClicked)
				.IsEnabled(this, &SSMLicenseDialog::IsMarketplaceLicense)
			]
			+SGridPanel::Slot(1, 2)
			.Padding(FMargin(HorizontalOffset, VerticalOffset))
			.VAlign(VAlign_Center)
			[
				// Display a checkbox forcing a marketplace build. Only needed if license type isn't known.
				SNew(SCheckBox)
				.Visibility_Lambda([this]()
				{
					return IsDefinitelyMarketplaceBuild() ? EVisibility::Collapsed : EVisibility::Visible;
				})
				.IsChecked(this, &SSMLicenseDialog::GetIsMarketplaceLicenseCheckboxState)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
				{
					SystemInfo->bIsMarketplace = NewState == ECheckBoxState::Checked;
				})
				.Content()
				[
					SNew(STextBlock)
					.Margin(2.f)
					.Text(LOCTEXT("ForceMarketplaceLicenseCheckbox_Text", "I am on a Marketplace license."))
					.ToolTipText(LOCTEXT("ForceMarketplaceLicenseCheckbox_Tooltip", "Manually specify the license type so validation can be performed."))
				]
			]
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(HorizontalOffset, 24, HorizontalOffset, 24.f))
		[
			SNew(STextBlock)
			.Text(this, &SSMLicenseDialog::GetLicenseInfoText)
			.Font(FSMUnrealAppStyle::Get().GetFontStyle("StandardDialog.LargeFont"))
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(HorizontalOffset, 0, HorizontalOffset, 12.f))
		.HAlign(HAlign_Fill)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SHyperlink)
					.Style(FSMUnrealAppStyle::Get(), TEXT("NavigationHyperlink"))
					.Text(FText::FromString("View Logic Driver licensing options"))
					.ToolTipText(FText::FromString("Click to visit the Logic Driver website."))
					.OnNavigate(this, &SSMLicenseDialog::OnLicenseLinkClicked)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(LOCTEXT("CancelButton_Text", "Cancel"))
				.ToolTipText(LOCTEXT("CancelButton_Tooltip", "Close the window."))
				.OnClicked_Lambda([this]()
				{
					RequestDestroyWindow();
					return FReply::Handled();
				})
			]
		]
	]);
}

bool SSMLicenseDialog::IsDefinitelyMarketplaceBuild() const
{
#if LOGICDRIVER_IS_MARKETPLACE_BUILD
	return true;
#else
	return false;
#endif
}

bool SSMLicenseDialog::IsMarketplaceLicense() const
{
	return SystemInfo->bIsMarketplace;
}

ECheckBoxState SSMLicenseDialog::GetIsMarketplaceLicenseCheckboxState() const
{
	return IsMarketplaceLicense() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FText SSMLicenseDialog::GetLicenseTypeText() const
{
	if (IsMarketplaceLicense())
	{
		return LOCTEXT("MarketplaceLicense", "UEM or FAB");
	}
	
	return LOCTEXT("UnknownLicense", "Unknown");
}

FText SSMLicenseDialog::GetLicenseInfoText() const
{
	FText ResultText;
	if (IsMarketplaceLicense())
	{
		ResultText = LOCTEXT("MarketplaceLicenseText",
"Marketplace code plugins are licensed per seat, as specified by the UE or FAB Marketplace EULAs.\
\n\nUsers who purchase a seat from FAB must select the license tier that matches their eligibility\
\nat the time of purchase. Please note, a valid entitlement check does not indicate which license tier\
\nis purchased; it is the user’s responsibility to ensure compliance with the correct tier.\
\n\nThis automated license check is to assist studios that may be unaware of their licensing status.\
\nIf you have any questions or wish to discuss licensing solutions for your studio, please contact Recursoft.\
");
	}
	else
	{
		ResultText = LOCTEXT("UnknownLicenseText",
"The license type cannot be determined from this build. This is expected if downloaded from GitHub.\
\nConsult your license manager or reach out to support@recursoft.com to validate your license.\
\n\nPossible license types are:\
\n- A project-based or custom license with Recursoft.\
\n- An Epic Games per-seat Marketplace license.\
\n- An evaluation license."
);
	}

	return ResultText;
}

FText SSMLicenseDialog::GetValidLicenseText() const
{
	FString ResultString = (IsMarketplaceLicense() && FSMAuthenticator::Get().HasAuthenticationRun()) ?
		(FSMAuthenticator::Get().IsAuthenticated(true) ?
		TEXT("Valid License") : TEXT("Invalid License"))
		: TEXT("Unknown");
	return FText::FromString(ResultString);
}

FSlateColor SSMLicenseDialog::GetValidLicenseTextColor() const
{
	if (IsMarketplaceLicense())
	{
		if (FSMAuthenticator::Get().IsAuthenticated(true))
		{
			// Definitely valid.
			return FSlateColor(FLinearColor::Green);
		}
		else if (FSMAuthenticator::Get().HasAuthenticationRun())
		{
			// Definitely invalid.
			return FSlateColor(FLinearColor::Red);
		}
	}

	// Unknown.
	return FSlateColor::UseForeground();
}

FReply SSMLicenseDialog::OnValidateMarketplaceLicenseClicked()
{
	constexpr bool bForceAuthenticate = true;
	
	FSimpleDelegate AuthDelegate;
	LD::EditorAuth::AuthenticateAndRecordResult(AuthDelegate,
		IPluginWardenModule::EUnauthorizedErrorHandling::ShowMessageOpenStore,
		bForceAuthenticate);
	return FReply::Handled();
}

void SSMLicenseDialog::OnLicenseLinkClicked()
{
	FPlatformProcess::LaunchURL(TEXT("https://logicdriver.com/licensing/"), nullptr, nullptr);
}

#undef LOCTEXT_NAMESPACE
