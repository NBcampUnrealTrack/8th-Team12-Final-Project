// Copyright Recursoft LLC. All Rights Reserved.

#include "SSMStateSelectionTreeView.h"

#include "Construction/ISMEditorConstructionManager.h"
#include "Construction/SMEditorInstance.h"
#include "Construction/SMEditorStateMachine.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Utilities/SMNodeInstanceUtils.h"

#include "Blueprints/SMBlueprint.h"
#include "Blueprints/SMBlueprintGeneratedClass.h"

#include "Framework/Application/SlateApplication.h"
#include "SMUnrealTypeDefs.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SSMStateTreeView"

bool FSMStateTreeItem::ShouldItemBeExpanded() const
{
	return true;
}

FString FSMStateTreeItem::BuildQualifiedNameString() const
{
	FString FullName = GetItemDisplayName().ToString();
	TWeakPtr<FSMStateTreeItem> ParentToCheck = Parent;
	while (ParentToCheck.IsValid())
	{
		FullName = ParentToCheck.Pin()->GetItemDisplayName().ToString() + "." + FullName;
		ParentToCheck = ParentToCheck.Pin()->Parent;
	}

	FullName.RemoveFromStart(TEXT("Root."));

	return FullName;
}

void SSMStateTreeView::Construct(const FArguments& InArgs)
{
	STreeView::Construct(InArgs);
}

void SSMStateTreeView::SetExpansionStateFromItems(const TArray<FSMStateTreeItemPtr>& InTreeItems)
{
	for (const FSMStateTreeItemPtr& TreeItem: InTreeItems)
	{
		SetItemExpansion(TreeItem, TreeItem->ShouldItemBeExpanded());
		SetExpansionStateFromItems(TreeItem->ChildItems);
	}
}

void SSMStateTreeViewRow::Construct(const FArguments& InArgs, const TSharedRef<SSMStateTreeView>& TreeView)
{
	Item = InArgs._Item;
	OnGenerateAdditionalWidgetContent = InArgs._OnGenerateAdditionalWidgetContent;

	FSuperRowType::FArguments SuperArgs;
	SuperArgs.Padding(FMargin(4.f, 2.f));
	SMultiColumnTableRow::Construct(MoveTemp(SuperArgs), TreeView);
}

TSharedRef<SWidget> SSMStateTreeViewRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	const TSharedPtr<FSMStateTreeItem> ItemPtr = Item.Pin();
	if (ItemPtr.IsValid() && ColumnName == SSMStateSelectionTreeView::StateColumnName())
	{
		if (ItemPtr->GetTypeName() == FSMStateTreeItem::StaticTypeName())
		{
			// Display the category name if it isn't the default category.
			const FText ItemDisplayName = ItemPtr->GetItemDisplayName();

			TSharedPtr<SVerticalBox> StateContextBox;
		
			const TSharedPtr<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SExpanderArrow, SharedThis(this))
				.StyleSet(&FSMUnrealAppStyle::Get())
				.IndentAmount(5)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(StateContextBox, SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					// Normal style would be "ExpandableArea.TitleFont" which is equal to "Bold, 8". With additional content
					// we want it a bit larger to signify this is a title/expander. May want to make this an attribute if
					// this treeview is used for anything else.
					.Font(ItemPtr->TitleFont)
					.Text(ItemDisplayName)
				]
			];
			return HorizontalBox.ToSharedRef();
		}
		else if (OnGenerateAdditionalWidgetContent.IsBound())
		{
			return OnGenerateAdditionalWidgetContent.Execute(ItemPtr);
		}
	}

	return SNullWidget::NullWidget;
}

void SSMStateSelectionTreeView::Construct(const FArguments& InArgs, USMBlueprintGeneratedClass* GeneratedClass)
{
	check(GeneratedClass);

	bIncludeChildrenOnSearchMatch = InArgs._IncludeChildrenOnSearchMatch;
	
	SearchBoxFilter
		= MakeShared<TTextFilter<FSMStateTreeItemPtr>>(
			TTextFilter<FSMStateTreeItemPtr>::FItemToStringArray::CreateSP(
				this, &SSMStateSelectionTreeView::GetSearchStrings));
	SearchBoxFilter->OnChanged().AddSP(this, &SSMStateSelectionTreeView::FilterItems);
	
	OnItemSelectedDelegate = InArgs._OnSelectionChanged;

	// Setup the columns.
	{
		SAssignNew(HeaderRow, SHeaderRow);

		SHeaderRow::FColumn::FArguments ColumnArgs;
		ColumnArgs.ColumnId(StateColumnName());
		ColumnArgs.DefaultLabel(LOCTEXT("ItemLabel_HeaderText", "State"));

		// Don't draw the column header.
		HeaderRow->SetVisibility(EVisibility::Collapsed);
		HeaderRow->AddColumn(ColumnArgs);
	}

	if (USMBlueprint* Blueprint = Cast<USMBlueprint>(UBlueprint::GetBlueprintFromClass(GeneratedClass)))
	{
		OwningBlueprint = Blueprint;

		OnPrepopulateRootTree();
		
		FSMEditorStateMachine& EditorStateMachine = ISMEditorConstructionManager::Get().CreateEditorStateMachine(Blueprint);

		const FSMStateTreeItemPtr StateMachineItem = MakeShared<FSMStateTreeItem>();
		StateMachineItem->NodeInstance = EditorStateMachine.StateMachineEditorInstance->GetRootStateMachineNodeInstance();
		StateMachineItem->StateName = StateMachineItem->NodeInstance->GetNodeName();
		if (UEdGraphNode** FoundGraphNode = EditorStateMachine.RuntimeNodeToGraphNode.Find(StateMachineItem->NodeInstance->GetOwningNode()))
		{
			StateMachineItem->StateNode = Cast<USMGraphNode_StateNodeBase>(*FoundGraphNode);
		}

		OnCreateTreeItem(StateMachineItem);
		RootTreeItems.Add(StateMachineItem);
		
		PopulateTreeItems(StateMachineItem, EditorStateMachine);

		// Allow any cleanup.
		OnPostPopulateRootTree();
		
		ISMEditorConstructionManager::Get().CleanupEditorStateMachine(Blueprint);

		SAssignNew(VerticalScrollBar, SScrollBar)
		.AlwaysShowScrollbar(true);
		
		ChildSlot
		.Padding(2.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(1.0f, 0.0f, 1.0f, 2.0f)
			[
				SAssignNew(SearchBox, SSearchBox)
				.DelayChangeNotificationsWhileTyping(true)
				.ToolTipText(LOCTEXT("Search_Tooltip", "Type here to search."))
				.OnTextChanged(this, &SSMStateSelectionTreeView::OnSearchTextChanged)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.MaxHeight(400.f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SAssignNew(StateTreeView, SSMStateTreeView)
					.SelectionMode(InArgs._SelectionMode)
					.TreeItemsSource(&FilteredItems)
					.HeaderRow(HeaderRow)
					.OnSelectionChanged(this, &SSMStateSelectionTreeView::OnSelectedItemChanged)
					.OnGenerateRow(this, &SSMStateSelectionTreeView::OnGenerateRowForTree)
					.OnGetChildren(this, &SSMStateSelectionTreeView::OnGetChildrenForTree)
					.ExternalScrollbar(VerticalScrollBar)
				]
			]
		];

		StateTreeView->SetExpansionStateFromItems(FilteredItems);
	}
	bSearchNeedsFocus = true;
}

void SSMStateSelectionTreeView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bSearchNeedsFocus)
	{
		bSearchNeedsFocus = false;
		FSlateApplication::Get().SetKeyboardFocus(SearchBox, EFocusCause::SetDirectly);
	}
}

void SSMStateSelectionTreeView::AddItemToRootTree(const FSMStateTreeItemRef& InItem)
{
	RootTreeItems.Add(InItem);
}

TSharedRef<ITableRow> SSMStateSelectionTreeView::OnGenerateRowForTree(FSMStateTreeItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SSMStateTreeViewRow, StateTreeView.ToSharedRef()).Item(Item);
}

void SSMStateSelectionTreeView::OnGetChildrenForTree(FSMStateTreeItemPtr InParent, TArray<FSMStateTreeItemPtr>& OutChildren)
{
	OutChildren.Append(InParent->ChildItems);
}

void SSMStateSelectionTreeView::OnSelectedItemChanged(FSMStateTreeItemPtr InSelectedItem, ESelectInfo::Type InSelectInfo)
{
	SelectedStateItem = InSelectedItem;

	if (InSelectInfo == ESelectInfo::Type::OnKeyPress || InSelectInfo == ESelectInfo::Type::OnMouseClick)
	{
		OnItemSelectedDelegate.ExecuteIfBound(SelectedStateItem);
	}
}

void SSMStateSelectionTreeView::OnPostPopulateRootTree()
{
	FilterItems();
}

void SSMStateSelectionTreeView::PopulateTreeItems(const FSMStateTreeItemPtr& InInitialItem, FSMEditorStateMachine& InEditorStateMachine)
{
	if (const USMStateMachineInstance* StateMachineInstance = Cast<USMStateMachineInstance>(InInitialItem->NodeInstance.Get()))
	{
		TArray<USMStateInstance_Base*> AllStateInstances;
		StateMachineInstance->GetAllStateInstances(AllStateInstances);

		for (USMStateInstance_Base* StateInstance : AllStateInstances)
		{
			FSMStateTreeItemPtr StateItem = MakeShared<FSMStateTreeItem>();

			StateItem->StateName = StateInstance->GetNodeName();
			StateItem->Parent = InInitialItem;
			StateItem->NodeInstance = StateInstance;
			if (UEdGraphNode** FoundGraphNode = InEditorStateMachine.RuntimeNodeToGraphNode.Find(StateItem->NodeInstance->GetOwningNode()))
			{
				StateItem->StateNode = Cast<USMGraphNode_StateNodeBase>(*FoundGraphNode);
			}

			OnCreateTreeItem(StateItem);
			
			InInitialItem->ChildItems.Add(StateItem);
			
			if (USMStateMachineInstance* NestedStateMachineInstance = Cast<USMStateMachineInstance>(StateInstance))
			{
				PopulateTreeItems(StateItem, InEditorStateMachine);
			}
		}
	}
}

void SSMStateSelectionTreeView::FilterItems()
{
	FilteredItems.Empty();

	for (const TSharedPtr<FSMStateTreeItem>& Root : RootTreeItems)
	{
		if (TSharedPtr<FSMStateTreeItem> FilteredRoot = FilterItemRecursive(Root))
		{
			FilteredItems.Add(FilteredRoot);
		}
	}

	if (StateTreeView)
	{
		StateTreeView->SetExpansionStateFromItems(FilteredItems);
		StateTreeView->RequestListRefresh();
	}
}

TSharedPtr<FSMStateTreeItem> SSMStateSelectionTreeView::FilterItemRecursive(const TSharedPtr<FSMStateTreeItem>& Item)
{
	// 1. Check if this item matches the filter.
	const bool bMatchesThis = SearchBoxFilter->PassesFilter(Item);
	const bool bAllChildrenPass = bMatchesThis && bIncludeChildrenOnSearchMatch;
	
	// 2. Recurse into children, collecting only those that match (or have matching descendants).
	TArray<TSharedPtr<FSMStateTreeItem>> FilteredChildren;
	if (bAllChildrenPass)
	{
		FilteredChildren.Reserve(Item->ChildItems.Num());
	}
	for (TSharedPtr<FSMStateTreeItem>& Child : Item->ChildItems)
	{
		if (bAllChildrenPass)
		{
			TSharedPtr<FSMStateTreeItem> ChildCopy = Child->Clone();
			FilteredChildren.Add(ChildCopy);
		}
		else if (TSharedPtr<FSMStateTreeItem> ChildCopy = FilterItemRecursive(Child))
		{
			FilteredChildren.Add(ChildCopy);
		}
	}

	// 3. If neither this item nor any child matched, drop it.
	if (!bMatchesThis && FilteredChildren.Num() == 0)
	{
		return nullptr;
	}

	// 4. Otherwise, make a copy of this item, but override its ChildItems to only the filtered ones.
	TSharedRef<FSMStateTreeItem> ItemCopy = Item->Clone().ToSharedRef();
	ItemCopy->ChildItems = MoveTemp(FilteredChildren);

	// 5. Fix up Parent pointers on copied children.
	for (const TSharedPtr<FSMStateTreeItem>& ChildCopy : ItemCopy->ChildItems)
	{
		ChildCopy->Parent = ItemCopy;
	}

	return ItemCopy;
}

void SSMStateSelectionTreeView::OnSearchTextChanged(const FText& InSearchText)
{
	SearchBoxFilter->SetRawFilterText(InSearchText);
	SearchBox->SetError(SearchBoxFilter->GetFilterErrorText());
}

#undef LOCTEXT_NAMESPACE
