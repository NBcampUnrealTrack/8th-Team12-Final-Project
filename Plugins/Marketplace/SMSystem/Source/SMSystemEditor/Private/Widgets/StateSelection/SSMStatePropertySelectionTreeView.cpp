// Copyright Recursoft LLC. All Rights Reserved.

#include "SSMStatePropertySelectionTreeView.h"

#include "Configuration/SMEditorSettings.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Utilities/SMBlueprintEditorUtils.h"
#include "Utilities/SMNodeInstanceUtils.h"
#include "Properties/SMEditorPropertyUtils.h"
#include "Properties/SMEditorPropertyIOUtils.h"
#include "Widgets/Properties/SSMPropertyWidget.h"

#include "SMInstance.h"
#include "SMStateInstance.h"

FText FSMStateTreeGraphPropertyItem::GetItemDisplayName() const
{
	FText DefaultText;

	if (USMGraphK2Node_PropertyNode_Base* Node = PropertyNode.Get())
	{
		if (DefaultText.IsEmpty())
		{
			DefaultText = Node->GetPropertyNodeChecked()->GetDisplayName();
		}

		if (Node->GetPropertyNodeChecked()->bIsInArray)
		{
			DefaultText = LD::Editor::PropertyUtils::GetPropertyDisplayName(DefaultText, Node->GetPropertyNodeChecked()->ArrayIndex);
		}
	}
	
	return DefaultText;
}

FText FSMStateTreeStandardPropertyItem::GetItemDisplayName() const
{
	return Property ? Property->GetDisplayNameText() : FSMStateTreeItem::GetItemDisplayName();
}

void SSMStatePropertySelectionTreeView::Construct(const FArguments& InArgs, USMBlueprintGeneratedClass* GeneratedClass)
{
	OnPropertySelected = InArgs._OnPropertySelected;
	PropertyFilter = InArgs._PropertyFilter;
	
	SSMStateSelectionTreeView::FArguments ParentArgs;
	ParentArgs.SelectionMode(ESelectionMode::Single);
	SSMStateSelectionTreeView::Construct(ParentArgs, GeneratedClass);
}

TSharedRef<ITableRow> SSMStatePropertySelectionTreeView::OnGenerateRowForTree(FSMStateTreeItemPtr Item,
																			  const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SSMStateTreeViewRow, StateTreeView.ToSharedRef())
		.Item(Item)
		.OnGenerateAdditionalWidgetContent_Raw(this, &SSMStatePropertySelectionTreeView::OnGenerateAdditionalStateWidgetContent);
}

void SSMStatePropertySelectionTreeView::OnCreateTreeItem(const FSMStateTreeItemPtr& InNewItem) const
{
	SSMStateSelectionTreeView::OnCreateTreeItem(InNewItem);

	using namespace LD::Editor::PropertyUtils;
	using namespace LD::Editor::PropertyUtils::IO;

	// Excluding node properties.
	if (PropertyFilter.PropertyOwnerTypes.Num() > 0 &&
		!PropertyFilter.PropertyOwnerTypes.Contains(EPropertyOwnerType::NodeInstance))
	{
		return;
	}

	if (InNewItem->Parent == nullptr && InNewItem->StateName == USMInstance::GetRootNodeNameDefault())
	{
		// Make it clear we are displaying the state machine.
		InNewItem->StateName = FString::Printf(TEXT("%s (State Machine)"), *USMInstance::GetRootNodeNameDefault());
	}
	
	// Larger title for states.
	InNewItem->TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	
	if (InNewItem->StateNode.IsValid())
	{
		const USMEditorSettings* EditorSettings = FSMBlueprintEditorUtils::GetEditorSettings();
		
		// Graph Properties
		TMap<USMNodeInstance*, TArray<USMGraphK2Node_PropertyNode_Base*>> TemplatePropertyMap;
		MapNodeTemplatesToProperties(InNewItem->StateNode.Get(), TemplatePropertyMap);
		
		for (const TTuple<USMNodeInstance*, TArray<USMGraphK2Node_PropertyNode_Base*>>& NodeToPropertiesKeyVal : TemplatePropertyMap)
		{
			FSMStateTreeItemPtr StateItem = InNewItem;

			// State stack (create a new category)
			if (NodeToPropertiesKeyVal.Key->GetTemplateGuid().IsValid() && EditorSettings->bDisplayStateStackClassNames)
			{
				StateItem = MakeShared<FSMStateTreeItem>();
				StateItem->StateNode = InNewItem->StateNode;
				StateItem->Parent = InNewItem;
				StateItem->StateName = FSMNodeInstanceUtils::GetNodeDisplayName(NodeToPropertiesKeyVal.Key);
				StateItem->TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 8); // Smaller size than state title.
			}
			
			for (USMGraphK2Node_PropertyNode_Base* PropertyNode : NodeToPropertiesKeyVal.Value)
			{
				if (PropertyFilter.ExcludeProperties.Contains(PropertyNode->GetProperty())
					|| (PropertyFilter.GraphPropertyFlags.IsSet()
						&& !PropertyNode->GetPropertyNodeConstChecked()->HasAnyPropertyFlags(PropertyFilter.GraphPropertyFlags.GetValue()))
						// Check custom filter function.
						|| (PropertyFilter.OnFilterGraphProperty.IsBound()
							&& PropertyFilter.OnFilterGraphProperty.Execute(*PropertyNode->GetPropertyNodeConstChecked())))
				{
					continue;
				}
				
				const FSMStateTreeGraphPropertyItemPtr PropertyItem = MakeShared<FSMStateTreeGraphPropertyItem>();

				PropertyItem->PropertyNode = PropertyNode;
				
				PropertyItem->StateNode = StateItem->StateNode;
				PropertyItem->NodeInstance = MakeWeakObjectPtr(CastChecked<USMStateInstance_Base>(NodeToPropertiesKeyVal.Key));
				PropertyItem->Parent = StateItem;
	
				StateItem->ChildItems.Add(PropertyItem);
			}

			// Only add if there was at least one property not filtered.
			if (StateItem != InNewItem && StateItem->ChildItems.Num() > 0)
			{
				InNewItem->ChildItems.Add(StateItem);
			}
		}
	}
}

void SSMStatePropertySelectionTreeView::OnSelectedItemChanged(FSMStateTreeItemPtr InSelectedItem,
	ESelectInfo::Type InSelectInfo)
{
	SSMStateSelectionTreeView::OnSelectedItemChanged(InSelectedItem, InSelectInfo);
	if (InSelectedItem
		&& ((InSelectedItem->GetTypeName() == FSMStateTreeGraphPropertyItem::StaticTypeName()
		|| InSelectedItem->GetTypeName() == FSMStateTreeStandardPropertyItem::StaticTypeName())
		&& OnPropertySelected.IsBound()
		&& (InSelectInfo == ESelectInfo::Type::OnKeyPress || InSelectInfo == ESelectInfo::Type::OnMouseClick)))
	{
		OnPropertySelected.Execute(InSelectedItem);
	}
}

void SSMStatePropertySelectionTreeView::OnPrepopulateRootTree()
{
	SSMStateSelectionTreeView::OnPrepopulateRootTree();

	check(OwningBlueprint.IsValid());

	using namespace LD::Editor::PropertyUtils::IO;
	
	if (PropertyFilter.PropertyOwnerTypes.IsEmpty()
		|| PropertyFilter.PropertyOwnerTypes.Contains(EPropertyOwnerType::OwningBlueprint))
	{
		TArray<FProperty*> OwningBlueprintProperties;
	
		// Owning Blueprint Properties are inserted first, before the root state machine.
		for (TFieldIterator<FProperty> PropertyItr(OwningBlueprint->SkeletonGeneratedClass
			? OwningBlueprint->SkeletonGeneratedClass : OwningBlueprint->GeneratedClass); PropertyItr; ++PropertyItr)
		{
			if (PropertyItr->HasAllPropertyFlags(CPF_BlueprintVisible)
				&& !PropertyItr->HasAnyPropertyFlags(CPF_BlueprintReadOnly)
				&& !PropertyFilter.ExcludeProperties.Contains(*PropertyItr))
			{
				OwningBlueprintProperties.Add(*PropertyItr);
			}
		}

		if (OwningBlueprintProperties.Num() > 0)
		{
			const FSMStateTreeItemPtr RootItem = MakeShared<FSMStateTreeItem>();
			RootItem->StateName = FString::Printf(TEXT("%s (Blueprint)"), *OwningBlueprint->GetName());
			RootItem->TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

			RootTreeItems.Add(RootItem);
	
			for (FProperty* Property : OwningBlueprintProperties)
			{
				const FSMStateTreeStandardPropertyItemPtr PropertyItem = MakeShared<FSMStateTreeStandardPropertyItem>();

				PropertyItem->Property = Property;
	
				RootItem->ChildItems.Add(PropertyItem);
			}
		}
	}
}

void SSMStatePropertySelectionTreeView::OnPostPopulateRootTree()
{
	// Remove items if none of their descendants contain properties.
	// We have to run this after a tree has been created, not during OnCreateTreeItem, because we need to check
	// all children nodes of nested state machines.
	PruneTree();

	SSMStateSelectionTreeView::OnPostPopulateRootTree();
}

void SSMStatePropertySelectionTreeView::PruneTree()
{
	struct FLocal
	{
		static void RemoveInvalidItems(const TSharedPtr<FSMStateTreeItem>& CurrentItem)
		{
			if (!CurrentItem.IsValid())
			{
				return;
			}

			for (int32 Idx = CurrentItem->ChildItems.Num() - 1; Idx >= 0; --Idx)
			{
				TSharedPtr<FSMStateTreeItem> Child = CurrentItem->ChildItems[Idx];

				RemoveInvalidItems(Child);

				if (!Child.IsValid() || !HasRequiredDescendants(Child))
				{
					CurrentItem->ChildItems.RemoveAt(Idx);
				}
			}
		}

		static bool HasRequiredDescendants(const TSharedPtr<FSMStateTreeItem>& Item)
		{
			if (!Item.IsValid())
			{
				return false;
			}

			if (Item->GetTypeName() == FSMStateTreeGraphPropertyItem::StaticTypeName() ||
				Item->GetTypeName() == FSMStateTreeStandardPropertyItem::StaticTypeName())
			{
				return true;
			}

			for (const TSharedPtr<FSMStateTreeItem>& Child : Item->ChildItems)
			{
				if (HasRequiredDescendants(Child))
				{
					return true;
				}
			}

			return false;
		}
	};
	
	for (int32 Idx = RootTreeItems.Num() - 1; Idx >= 0; --Idx)
	{
		TSharedPtr<FSMStateTreeItem> RootItem = RootTreeItems[Idx];

		FLocal::RemoveInvalidItems(RootItem);

		if (!RootItem.IsValid() || !FLocal::HasRequiredDescendants(RootItem))
		{
			RootTreeItems.RemoveAt(Idx);
		}
	}
}

TSharedRef<SWidget> SSMStatePropertySelectionTreeView::OnGenerateAdditionalStateWidgetContent(FSMStateTreeItemPtr Item)
{
	if (Item->GetTypeName() == FSMStateTreeGraphPropertyItem::StaticTypeName())
	{
		// Graph property display.
		
		const FSMStateTreeGraphPropertyItemPtr PropertyItemPtr = StaticCastSharedPtr<FSMStateTreeGraphPropertyItem>(Item);
		
		if (USMGraphK2Node_PropertyNode_Base* Node = PropertyItemPtr->PropertyNode.Get())
		{
			FProperty* Property = nullptr;
			const FText DefaultText = PropertyItemPtr->GetItemDisplayName();
			FText Tooltip;
			
			if (FSMGraphProperty_Base* Prop = Node->GetPropertyNode())
			{
				if (UBlueprint* Blueprint = FSMBlueprintEditorUtils::FindBlueprintForNode(Node))
				{
					Property = Prop->MemberReference.ResolveMember<FProperty>(Blueprint);
					if (Property)
					{
						Tooltip = Property->GetToolTipText();
					}

					if (Tooltip.IsEmpty())
					{
						Tooltip = UEdGraphSchema_K2::TypeToText(Prop->VariableType);
					}
				}
			}

			return SNew(SVerticalBox)
			+SVerticalBox::Slot()
			[
				SNew(SSMPropertyWidget)
				.Property(Property)
				.DisplayText(DefaultText)
				.ToolTipText(Tooltip)
			];
		}
	}
	else if (Item->GetTypeName() == FSMStateTreeStandardPropertyItem::StaticTypeName())
	{
		const FSMStateTreeStandardPropertyItemPtr PropertyItemPtr = StaticCastSharedPtr<FSMStateTreeStandardPropertyItem>(Item);
		
		if (FProperty* Property = PropertyItemPtr->Property)
		{
			FText TooltipText = Property->GetToolTipText();
			FEdGraphPinType PinType;
			if (TooltipText.IsEmpty() && GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType))
			{
				TooltipText = UEdGraphSchema_K2::TypeToText(PinType);
			}
			
			return SNew(SVerticalBox)
			+SVerticalBox::Slot()
			[
				SNew(SSMPropertyWidget)
				.Property(Property)
				.ToolTipText(TooltipText)
			];
		}
	}
	
	return SNullWidget::NullWidget;
}
