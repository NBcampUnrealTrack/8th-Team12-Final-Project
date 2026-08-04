// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Input/Reply.h"
#include "Internationalization/Text.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SWindow.h"

struct FSMSystemInfo;

class SSMLicenseDialog : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SSMLicenseDialog)
	{
	}
	SLATE_END_ARGS()

	virtual ~SSMLicenseDialog() override;
	
	void Construct(const FArguments& InArgs);

private:
	/** Checks the build flags. If true this came from the Marketplace. */
	bool IsDefinitelyMarketplaceBuild() const;

	/** Checks if the build is from the Marketplace or the user specified they have a Marketplace license. */
	bool IsMarketplaceLicense() const;

	/** If the user is manually specifying a marketplace license from an unknown build. */
	ECheckBoxState GetIsMarketplaceLicenseCheckboxState() const;

	/** Return the name of the license. */
	FText GetLicenseTypeText() const;

	/** Retrieve a description of the license type. */
	FText GetLicenseInfoText() const;

	/** Retrieve text indicating the status of the license. */
	FText GetValidLicenseText() const;

	/** Retrieve color for the text based on the license status. */
	FSlateColor GetValidLicenseTextColor() const;

	/** User clicked on Validate License. */
	FReply OnValidateMarketplaceLicenseClicked();

	/** Go to the website. */
	void OnLicenseLinkClicked();
	
private:
	/** System info primarily for build type. */
	TSharedPtr<FSMSystemInfo> SystemInfo;
};
