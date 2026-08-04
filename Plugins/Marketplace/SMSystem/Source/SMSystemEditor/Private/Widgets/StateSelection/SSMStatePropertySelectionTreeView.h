// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SSMStateSelectionTreeView.h"
#include "Properties/SMEditorPropertyIOUtils.h"

class USMGraphK2Node_PropertyNode_Base;
class SSMGraphProperty_Base;

/** Item representing a graph property. */
struct FSMStateTreeGraphPropertyItem final : FSMStateTreeItem
{
	/** The property node for this tree item. */
	TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base> PropertyNode;

	virtual FText GetItemDisplayName() const override;

	LOGICDRIVER_DEFINE_STATE_TREE_ITEM_CHILD(FSMStateTreeGraphPropertyItem)
};

/** Item representing a normal property. */
struct FSMStateTreeStandardPropertyItem final : FSMStateTreeItem
{
	/** The property node for this tree item. */
	FProperty* Property;

	virtual FText GetItemDisplayName() const override;

	LOGICDRIVER_DEFINE_STATE_TREE_ITEM_CHILD(FSMStateTreeStandardPropertyItem)
};

typedef TSharedPtr<FSMStateTreeGraphPropertyItem> FSMStateTreeGraphPropertyItemPtr;
typedef TSharedPtr<FSMStateTreeStandardPropertyItem> FSMStateTreeStandardPropertyItemPtr;

/**
 * A treeview for selecting graph properties of a state.
 * This also handles displaying owning blueprint properties prior to state property selection.
 * 
 * While it may be more proper to introduce a new widget to handle this, extending the state selection to do
 * both is currently more contained and easier to maintain.
 */
class SSMStatePropertySelectionTreeView : public SSMStateSelectionTreeView
{
public:
	DECLARE_DELEGATE_OneParam(FOnPropertySelected, FSMStateTreeItemPtr)
	
	SLATE_BEGIN_ARGS(SSMStatePropertySelectionTreeView) {}
		/** Delegate fired when the selection changes. */
		SLATE_EVENT(FOnPropertySelected, OnPropertySelected)
		/** Filter for limiting which properties to display. */
		SLATE_ARGUMENT(LD::Editor::PropertyUtils::IO::FPropertyFilter, PropertyFilter)
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, USMBlueprintGeneratedClass* GeneratedClass);

protected:
	virtual TSharedRef<ITableRow> OnGenerateRowForTree(FSMStateTreeItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable) override;
	virtual void OnCreateTreeItem(const FSMStateTreeItemPtr& InNewItem) const override;
	virtual void OnSelectedItemChanged(FSMStateTreeItemPtr InSelectedItem, ESelectInfo::Type InSelectInfo) override;

	virtual void OnPrepopulateRootTree() override;
	virtual void OnPostPopulateRootTree() override;

	/** Remove items not containing descendents with properties. */
	void PruneTree();
	
	/** Add this widget to each state row in the tree view, ie the name shows up, then this widget. */
	TSharedRef<SWidget> OnGenerateAdditionalStateWidgetContent(FSMStateTreeItemPtr Item);

private:
	/** Delegate fired when the property selection changes. */
	FOnPropertySelected OnPropertySelected;

	/** Limit properties displayed to this filter. */
	LD::Editor::PropertyUtils::IO::FPropertyFilter PropertyFilter;
};
