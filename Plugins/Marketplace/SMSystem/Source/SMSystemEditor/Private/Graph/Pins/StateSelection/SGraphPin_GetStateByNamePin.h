// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Widgets/StateSelection/SSMStateSelectionTreeView.h"

#include "EdGraphUtilities.h"
#include "SGraphPin.h"

class SButton;
class SMenuAnchor;
class USMBlueprintGeneratedClass;

struct FSMGetStateByNamePinFactory : FGraphPanelPinFactory
{
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* InPin) const override;
	static void RegisterFactory();
};

class SGraphPin_GetStateByNamePin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPin_GetStateByNamePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

	/** Attempt to retrieve the SM BPGC from a pin. */
	static USMBlueprintGeneratedClass* GetBlueprintGeneratedClass(const UEdGraphPin* InGraphPin);
	
protected:
	// SGraphPin
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;
	virtual bool DoesWidgetHandleSettingEditingEnabled() const override { return true; }
	// ~SGraphPin

	/** The primary content of the drop down. */
	TSharedRef<SWidget> OnGetMenuContent();
	
	/** Get default text for the picker combo */
	FText OnGetDefaultComboText() const;
	
	/** Combo Button Color and Opacity delegate */
	FSlateColor OnGetComboForeground() const;
	
	/** Button Color and Opacity delegate */
	FSlateColor OnGetWidgetBackground() const;
	
	/** If widget is displayed. */
	EVisibility OnGetWidgetVisibility() const;

	/** User selected a state. */
	void OnStateSelected(FSMStateTreeItemPtr SelectedState);

	/** Closes the combo button for the asset name. */
	void CloseComboButton();

private:
	/** Menu anchor for opening and closing the state picker. */
	TSharedPtr<SMenuAnchor> StatePickerAnchor;

	/** The tree view of available states. */
	TSharedPtr<SSMStateSelectionTreeView> StateTreeView;
};
