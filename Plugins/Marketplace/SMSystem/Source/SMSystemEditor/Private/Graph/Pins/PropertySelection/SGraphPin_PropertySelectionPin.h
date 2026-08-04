// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "EdGraphUtilities.h"
#include "SGraphPin.h"
#include "Widgets/Input/SMenuAnchor.h"

#include "Properties/SMEditorPropertyIOUtils.h"

class SSMGraphProperty_Base;
struct FSMStateTreeItem;

struct FSMPropertySelectionPinFactory : FGraphPanelPinFactory
{
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* InPin) const override;
	static void RegisterFactory();
};

/** Select a property as a pin value. */
class SGraphPin_PropertySelectionPin : public SGraphPin
{
	SLATE_BEGIN_ARGS(SGraphPin_PropertySelectionPin) : _CanDrawArrows(true),
		_MinPropertySelectionWidth(125.f), _MaxPropertySelectionWidth(125.f) {}
		SLATE_ARGUMENT(EEdGraphPinDirection, PropertySelectionDirection)
		/** When a property selection box open status has changed. */
		SLATE_EVENT(FOnIsOpenChanged, OnPropertySelectionOpenChanged)
		/** Called when the context menu is opening. */
		SLATE_EVENT(FOnContextMenuOpening, OnContextMenuOpening)
		/** Called when the mouse button is pressed. This can override this pin's mouse down event. */
		SLATE_EVENT(FPointerEventHandler, OnMouseButtonDown)
		/** Whether arrows should be allowed to be drawn. */
		SLATE_ARGUMENT(bool, CanDrawArrows)
		/** If pins should have their default tooltips displayed. */
		SLATE_ARGUMENT(bool, DisplayPinTooltips)
		/** Min width for property selection widget. */
		SLATE_ARGUMENT(FOptionalSize, MinPropertySelectionWidth)
		/** Max width for property selection widget. */
		SLATE_ARGUMENT(FOptionalSize, MaxPropertySelectionWidth)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	// SGraphPin
	virtual FSlateColor GetPinColor() const override;
	virtual FSlateColor GetHighlightColor() const override;
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// ~SGraphPin
	
	/** When we have selected a property. */
	void OnPropertySelected(TSharedPtr<FSMStateTreeItem> InSelectedItem);

	/** If a property is currently selected. */
	bool IsPropertySelected() const { return GraphPinObj && !GraphPinObj->GetDefaultAsString().IsEmpty(); }

	/** Get the selected property. */
	const FProperty* GetProperty() const;

	/** Get the selected property text. */
	FText GetPropertyText() const;

	/** If this pin should be displayed at all. */
	bool ShouldDisplayPin() const;

	/** If the pin can be edited in its current state. */
	bool CanPinBeEdited() const;
	
	/** Determine whether a pin connector is visible. */
	EVisibility GetPinConnectorVisibility() const;

	/** Construct the go to (browse) button. */
	TSharedRef<SWidget> ConstructGoToButton();

	/** Then the browse button is clicked. */
	FReply OnGoToButtonClicked();
	
	/** Combo box dropdown. */
	TSharedRef<SWidget> OnGetMenuContent();
	/** Default combo box text. */
	FText OnGetDefaultComboStateText() const;
	/** If the combo box is enabled. */
	bool IsComboEnabled() const;
	
	EVisibility GetNoPropertySelectedTextVisibility() const;
	EVisibility GetPropertySelectedTextVisibility() const;
	
	FSlateColor OnGetComboForeground() const;
	FSlateColor OnGetWidgetBackground() const;

	/** Converts a tree item to property data. */
	TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base> ConvertTreeItemToPropertyData(const TSharedPtr<FSMStateTreeItem>& InItem) const;

	/** Update cache if it has changed. */
	bool TryUpdateCache() const;

	/** Create a filter for displaying the property tree view. */
	LD::Editor::PropertyUtils::IO::FPropertyFilter BuildPropertySelectionFilter() const;
	
private:
	/** Menu anchor for opening and closing the property picker. */
	TSharedPtr<SMenuAnchor> PropertyPickerAnchor;

	/** The direction which will display a property selector. */
	EEdGraphPinDirection PropertySelectionDirection = EGPD_Output;

	/** Combo box open changed. */
	FOnIsOpenChanged OnPropertySelectionOpenChanged;

	/** Delegate for a context menu. */
	FOnContextMenuOpening OnContextMenuOpening;

	/** Event for mouse button down. */
	FPointerEventHandler OnMouseButtonDownEvent;

	/** Property data representing this pin. */
	mutable TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base> CachedPropertyData;
	mutable FString CachedPropertyString;

	FOptionalSize MinPropertySelectionWidth;
	FOptionalSize MaxPropertySelectionWidth;
	const float MaxPropertySelectionHeight = 42.f;
};