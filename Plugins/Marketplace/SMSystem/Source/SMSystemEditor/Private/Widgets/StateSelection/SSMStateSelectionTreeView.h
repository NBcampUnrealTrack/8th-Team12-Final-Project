// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Misc/TextFilter.h"
#include "Widgets/Views/STreeView.h"

struct FSMEditorStateMachine;
class USMGraphNode_StateNodeBase;
class USMBlueprintGeneratedClass;
class USMStateInstance_Base;
class USMStateMachineInstance;
class SSearchBox;

/**
 * Represents a state item in the tree.
 */
struct FSMStateTreeItem
{
	FString StateName;
	
	TArray<TSharedPtr<FSMStateTreeItem>> ChildItems;
	TWeakPtr<FSMStateTreeItem> Parent;

	TWeakObjectPtr<USMStateInstance_Base> NodeInstance;
	TWeakObjectPtr<USMGraphNode_StateNodeBase> StateNode;

	FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	
	FSMStateTreeItem() = default;
	virtual ~FSMStateTreeItem() = default;
	FSMStateTreeItem(const FSMStateTreeItem& Other) = default;
	
	/** Checks if this particular item should be expanded based on its contents. */
	bool ShouldItemBeExpanded() const;

	/** Retrieve the item's display name. */
	virtual FText GetItemDisplayName() const { return FText::FromString(StateName); }
	
	/** Search up parents building a complete qualified name. */
	FString BuildQualifiedNameString() const;

	/** The type of this struct for easy comparisons later. */
	static FName StaticTypeName() { return TEXT("FSMStateTreeItem"); }
	virtual FName GetTypeName() const { return StaticTypeName(); }

	/** Copy this object. */
	virtual TSharedPtr<FSMStateTreeItem> Clone() const
	{
		return MakeShared<FSMStateTreeItem>(*this);
	}
	
	bool operator==(const FSMStateTreeItem& Other) const
	{
		return GetItemDisplayName().EqualTo(Other.GetItemDisplayName())
		&& Parent == Other.Parent
		&& NodeInstance == Other.NodeInstance
		&& StateNode == Other.StateNode
		&& GetTypeName() == Other.GetTypeName();
	}
};

// Place in children structs to define their type for comparison.
#define LOGICDRIVER_DEFINE_STATE_TREE_ITEM_CHILD(Type) \
static FName StaticTypeName() { return TEXT(#Type); } \
virtual FName GetTypeName() const override { return StaticTypeName(); } \
virtual TSharedPtr<FSMStateTreeItem> Clone() const override { return MakeShared<Type>(*this); }

typedef TSharedPtr<FSMStateTreeItem> FSMStateTreeItemPtr;
typedef TSharedRef<FSMStateTreeItem> FSMStateTreeItemRef;

/**
 * A tree view for selecting states in a state machine.
 */
class SSMStateTreeView : public STreeView<FSMStateTreeItemPtr>
{
public:
	void Construct(const FArguments& InArgs);

	/** Recursively set expansion state of tree view to match items. */
	void SetExpansionStateFromItems(const TArray<FSMStateTreeItemPtr>& InTreeItems);
};

/**
 * Each row of the tree view.
 */
class SSMStateTreeViewRow : public SMultiColumnTableRow<FSMStateTreeItemPtr>
{
public:
	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FOnGenerateAdditionalWidgetContent, FSMStateTreeItemPtr)
	
	SLATE_BEGIN_ARGS(SSMStateTreeViewRow) {}

		/** The list item for this row */
		SLATE_ARGUMENT(FSMStateTreeItemPtr, Item)

		/** Optional hook to include additional content when generating the widget. */
		SLATE_EVENT(FOnGenerateAdditionalWidgetContent, OnGenerateAdditionalWidgetContent)
	SLATE_END_ARGS()

	/** Construct function for this widget */
	void Construct(const FArguments& InArgs, const TSharedRef<SSMStateTreeView>& TreeView);

	/** Overridden from SMultiColumnTableRow. Generates a widget for this column of the tree row. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	/** The item associated with this row of data */
	TWeakPtr<FSMStateTreeItem> Item;

	/** Called when generating the widget row. */
	FOnGenerateAdditionalWidgetContent OnGenerateAdditionalWidgetContent;
};

/**
 * Visual representation of all available states.
 */
class SSMStateSelectionTreeView : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnStateTreeItemSelected, FSMStateTreeItemPtr)
	
	SLATE_BEGIN_ARGS(SSMStateSelectionTreeView) :
		_SelectionMode(ESelectionMode::Type::Single),
		_IncludeChildrenOnSearchMatch(true)
		{}
		
		/** Called when the user selects an item. */
		SLATE_EVENT(FOnStateTreeItemSelected, OnSelectionChanged)
		/** The selection mode of the property view. */
		SLATE_ARGUMENT(ESelectionMode::Type, SelectionMode)
		/** If an item in the tree matches the search filter, should all children be included in the results? */
		SLATE_ARGUMENT(bool, IncludeChildrenOnSearchMatch)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, USMBlueprintGeneratedClass* GeneratedClass);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	
	/** Adds a state item to the root tree. */
	void AddItemToRootTree(const FSMStateTreeItemRef& InItem);

	/** The name of the primary state column. */
	static FName StateColumnName() { return "State"; }

	/** The currently selected item. */
	FSMStateTreeItemPtr GetSelectedStateItem() const { return SelectedStateItem; }

	/** The root items in the tree. */
	const TArray<FSMStateTreeItemPtr>& GetRootTreeItems() const { return RootTreeItems; }

	/** The filtered items in the tree. */
	const TArray<FSMStateTreeItemPtr>& GetFilteredTreeItems() const { return FilteredItems; }
	
protected:
	/** Called by STreeView for each row being generated. */
	virtual TSharedRef<ITableRow> OnGenerateRowForTree(FSMStateTreeItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Process the new item before assigning it. */
	virtual void OnCreateTreeItem(const FSMStateTreeItemPtr& InNewItem) const {}
	
	/** Called by STreeView to get child items for the specified parent item. */
	void OnGetChildrenForTree(FSMStateTreeItemPtr InParent, TArray<FSMStateTreeItemPtr>& OutChildren);

	/** Call when item in tree was selected. */
	virtual void OnSelectedItemChanged(FSMStateTreeItemPtr InSelectedItem, ESelectInfo::Type InSelectInfo);

	/** Called before any items added to the root tree. */
	virtual void OnPrepopulateRootTree() {}

	/** called after the tree has finished populating. */
	virtual void OnPostPopulateRootTree();

	/** Generate the RootTreeItems and all children. */
	void PopulateTreeItems(const FSMStateTreeItemPtr& InInitialItem, FSMEditorStateMachine& InEditorStateMachine);
	
	/** Filters out items not passing search filter. */
	void FilterItems();

private:
	/** Retrieve the search strings for each item. */
	void GetSearchStrings(FSMStateTreeItemPtr InItem, TArray<FString>& OutStrings)
	{
		OutStrings.Add(InItem->GetItemDisplayName().ToString());
	}
	
	/** Recursively filter an item based if any children match. */
	TSharedPtr<FSMStateTreeItem> FilterItemRecursive(const TSharedPtr<FSMStateTreeItem>& Item);
	
	/** When the search text has changed. */
	void OnSearchTextChanged(const FText& InSearchText);
	
protected:
	TSharedPtr<SSMStateTreeView> StateTreeView;
	TSharedPtr<SScrollBar> VerticalScrollBar;
	TArray<FSMStateTreeItemPtr> RootTreeItems;
	/** The filtered items, starting from the root. */
	TArray<FSMStateTreeItemPtr> FilteredItems;
	TSharedPtr<SHeaderRow> HeaderRow;
	/** The search box widget. */
	TSharedPtr<SSearchBox> SearchBox;
	/** The filter linked to the search box. */
	TSharedPtr<TTextFilter<FSMStateTreeItemPtr>> SearchBoxFilter;
	
	TWeakObjectPtr<UBlueprint> OwningBlueprint;
	FSMStateTreeItemPtr SelectedStateItem;
	FOnStateTreeItemSelected OnItemSelectedDelegate;

private:
	/** If children items should be included when a parent passes a search filter. */
	bool bIncludeChildrenOnSearchMatch = false;
	/** If the search box needs focus. */
	bool bSearchNeedsFocus = false;
};
