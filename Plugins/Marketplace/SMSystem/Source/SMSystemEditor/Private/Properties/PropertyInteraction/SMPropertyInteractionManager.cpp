// Copyright Recursoft LLC. All Rights Reserved.

#include "Properties/PropertyInteraction/SMPropertyInteractionManager.h"

#include "Blueprints/SMBlueprintEditor.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/SMGraph.h"
#include "Properties/SMEditorPropertyIOUtils.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Framework/Application/SlateApplication.h"
#include "SGraphPanel.h"

// Same color as state selection
FLinearColor FSMPropertyInteractionManager::PropertySelectedColor = FLinearColor(0.8f, 0.6f, 0.2f, 1);
FLinearColor FSMPropertyInteractionManager::PropertyHoveredColor = FLinearColor(0.6f, 0.6f, 0.6f, 1);
FLinearColor FSMPropertyInteractionManager::PropertySelectedAndHoveredColor = FLinearColor(0.9098f, 0.6196f, 0.f, 1);

FSMPropertyInteractionManager::FSMPropertyInteractionManager(USMGraphNode_StateNodeBase* InStateNodeOwner)
{
	OwningStateNode = InStateNodeOwner;
	OwningBlueprint = FSMBlueprintEditorUtils::FindBlueprintForNode(InStateNodeOwner);
	
	BlueprintEditorOpenedHandle = ISMStateMachineBlueprintEditor::OnBlueprintEditorOpened.AddRaw(this, &FSMPropertyInteractionManager::OnStateMachineEditorOpened);
	BlueprintEditorClosedHandle = ISMStateMachineBlueprintEditor::OnBlueprintEditorClosed.AddRaw(this, &FSMPropertyInteractionManager::OnStateMachineEditorClosed);

	// The editor may be opened already when this is loaded, so bind now.
	BindToStateMachineEditorEvents(FSMBlueprintEditorUtils::GetStateMachineEditor(OwningBlueprint.Get()));
}

FSMPropertyInteractionManager::~FSMPropertyInteractionManager()
{
	ISMStateMachineBlueprintEditor::OnBlueprintEditorOpened.Remove(BlueprintEditorOpenedHandle);
	ISMStateMachineBlueprintEditor::OnBlueprintEditorClosed.Remove(BlueprintEditorClosedHandle);
	
	if (ISMStateMachineBlueprintEditor* Editor = FSMBlueprintEditorUtils::GetStateMachineEditor(OwningBlueprint.Get()))
	{
		UnbindFromStateMachineEditorEvents(Editor);
	}
}

bool FSMPropertyInteractionManager::SetPropertyInteraction(USMGraphK2Node_PropertyNode_Base* InProperty,
                                                           const ESMPropertyInteractionType InInteractionType,
                                                           const FInteractionArgs& InInteractionArgs)
{
	if (!InProperty || !IsGraphPanelSupported(InInteractionArgs.ScopedGraphPanel.Get(ScopedGraphPanel)))
	{
		return false;
	}
	
	FSMPropertyInteractionData OldPropertyData;
	TSharedPtr<FSMPropertyInteractionData> NewPropertyData;
	
	if (const TSharedPtr<FSMPropertyInteractionData> ExistingPropertyData = GetPropertyInteractionData(InProperty))
	{
		if (ExistingPropertyData->bInteractionGuard)
		{
			// Prevent recursive interaction. IE, a selection triggers an unselection before the selection finishes.
			// While the system generally avoids this, with all the different interaction types, and the possible ways for
			// them to be called, this is an extra safeguard. Most noticeable if a property on one state is selected,
			// then you attempt to select a property on another state, which triggers a new node selection. The new
			// node selection will first clear the current selection (default UE behavior) which will trigger a deselection
			// of the property being selected.
			return false;
		}
		
		OldPropertyData = *ExistingPropertyData;
		NewPropertyData = ExistingPropertyData;
	}
	else
	{
		NewPropertyData = MakeShared<FSMPropertyInteractionData>();
		InteractedPropertyData.Add(InProperty, NewPropertyData);
	}
	
	NewPropertyData->bInteractionGuard = true;
	ON_SCOPE_EXIT
	{
		NewPropertyData->bInteractionGuard = false;
	};

	// Request came from another state.
	const bool bIsExternalRequest = IsInteractionFromAnotherNode(InInteractionArgs);
	if (bIsExternalRequest)
	{
		TemporaryOwningStateNodes.Add(InInteractionArgs.OriginatingRequest);
	}

	if (InInteractionArgs.ScopedGraphPanel.IsSet())
	{
		ScopedGraphPanel = InInteractionArgs.ScopedGraphPanel.GetValue();
	}
	
	bool bSetHighlight = false;
	
	USMGraphK2Node_PropertyNode_Base::FHighlightArgs HighlightArgs;
	HighlightArgs.bClearOnCompile = false;

	if (InInteractionType == ESMPropertyInteractionType::None)
	{
		HighlightArgs.bEnable = false;
		NewPropertyData->bSelected = false;
		NewPropertyData->bHovered = false;
	}
	else
	{
		if (EnumHasAnyFlags(InInteractionType, ESMPropertyInteractionType::UnsetHover))
		{
			HighlightArgs.bEnable = NewPropertyData->bSelected;
			HighlightArgs.Color = PropertySelectedColor;
			NewPropertyData->bHovered = false;
			bSetHighlight = true;
		}
		if (EnumHasAnyFlags(InInteractionType, ESMPropertyInteractionType::UnsetSelect))
		{
			HighlightArgs.bEnable = NewPropertyData->bHovered;
			HighlightArgs.Color = PropertyHoveredColor;
			NewPropertyData->bSelected = false;
			NewPropertyData->ExternalSelector.Reset();
			bSetHighlight = true;
		}
		if (EnumHasAnyFlags(InInteractionType, ESMPropertyInteractionType::SetHover))
		{
			HighlightArgs.bEnable = true;
			HighlightArgs.Color = NewPropertyData->bSelected ? PropertySelectedAndHoveredColor : PropertyHoveredColor;
			NewPropertyData->bHovered = true;
			bSetHighlight = true;
		}
		if (EnumHasAnyFlags(InInteractionType, ESMPropertyInteractionType::SetSelect))
		{
			// Make sure the owning state node is selected as long as we made the request ourselves.
			if (!bIsExternalRequest)
			{
				SelectOwningStateNode();
			}
			
			HighlightArgs.bEnable = true;
			HighlightArgs.Color = NewPropertyData->bHovered ? PropertySelectedAndHoveredColor : PropertySelectedColor;
			NewPropertyData->bSelected = true;
			NewPropertyData->ExternalSelector = InInteractionArgs.OriginatingRequest;
			bSetHighlight = true;

			TMap<TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>, TSharedPtr<FSMPropertyInteractionData>> CurrentAccessedProperties =
				InteractedPropertyData;
			// Deselect other properties.
			if (!InInteractionArgs.bAllowMultiSelect)
			{
				constexpr ESMPropertyInteractionType UnselectType = ESMPropertyInteractionType::UnsetSelect;
				const FInteractionArgs OtherStateInteraction = BuildInteractionArgsForExternalSelections(UnselectType, GetScopedGraphPanel());
				for (const TTuple<TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>, TSharedPtr<FSMPropertyInteractionData>>& AccessedProperties : CurrentAccessedProperties)
				{
					if (AccessedProperties.Key.Get() != InProperty && AccessedProperties.Value->bSelected)
					{
						SetPropertyInteraction(AccessedProperties.Key.Get(), UnselectType, OtherStateInteraction);
					}
				}
			}
		}
		if (EnumHasAnyFlags(InInteractionType, ESMPropertyInteractionType::SetDisplayInputConnector))
		{
			NewPropertyData->bForceDisplayInputConnectors = true;
		}
		if (EnumHasAnyFlags(InInteractionType, ESMPropertyInteractionType::UnsetDisplayInputConnector))
		{
			NewPropertyData->bForceDisplayInputConnectors = false;
		}
		if (EnumHasAnyFlags(InInteractionType, ESMPropertyInteractionType::SetDisplayOutputConnector))
		{
			NewPropertyData->bForceDisplayOutputConnectors = true;
		}
		if (EnumHasAnyFlags(InInteractionType, ESMPropertyInteractionType::UnsetDisplayOutputConnector))
		{
			NewPropertyData->bForceDisplayOutputConnectors = false;
		}
	}

	if (InInteractionArgs.bExpandCategory.IsSet()
		&& NewPropertyData->bExpandCategory != InInteractionArgs.bExpandCategory.GetValue())
	{
		NewPropertyData->bExpandCategory = InInteractionArgs.bExpandCategory.GetValue();
		GetOnExpansionStateChanged().Broadcast(InProperty, NewPropertyData->bExpandCategory);
	}

	if (bSetHighlight)
	{
		InProperty->SetHighlightedArgs(HighlightArgs);
	}
	
	if (InInteractionArgs.ConnectedPropertyInteractionArgs.InteractionType != ESMPropertyInteractionType::None)
	{
		const TArray<USMGraphK2Node_PropertyNode_Base*> ConnectedProperties = LD::Editor::PropertyUtils::IO::GetAllConnectedPropertyNodes(InProperty);
		for (USMGraphK2Node_PropertyNode_Base* PropertyNode : ConnectedProperties)
		{
			USMGraphNode_StateNodeBase* OtherState = CastChecked<USMGraphNode_StateNodeBase>(PropertyNode->GetOwningGraphNodeChecked());
			
			// Skip connected behavior if this is referencing itself or belongs to another graph.
			if ((InInteractionArgs.ConnectedPropertyInteractionArgs.bApplyBehaviorToOwningGraphOnly &&
				(OtherState->GetOwningStateMachineGraph()
					!= InProperty->GetOwningGraphNodeChecked()->GetOwningStateMachineGraph()))
				|| PropertyNode == InProperty)
			{
				continue;
			}

			FInteractionArgs ConnectedInteractionArgs;
			ConnectedInteractionArgs.OriginatingRequest = OwningStateNode;
			ConnectedInteractionArgs.bAllowMultiSelect = InInteractionArgs.ConnectedPropertyInteractionArgs.bAllowMultiSelect;
			ConnectedInteractionArgs.bExpandCategory = InInteractionArgs.ConnectedPropertyInteractionArgs.bExpandCategory;
			ConnectedInteractionArgs.ScopedGraphPanel = InInteractionArgs.ScopedGraphPanel;

			TemporaryControlledStateNodes.Add(OtherState);
			
			OtherState->GetPropertyInteractionManager().SetPropertyInteraction(PropertyNode, InInteractionArgs.ConnectedPropertyInteractionArgs.InteractionType, ConnectedInteractionArgs);
		}
	}
	
	// Only broadcast a change.
	if (OldPropertyData != *NewPropertyData || InInteractionArgs.bForce)
	{
		InProperty->OnPropertyInteractionChanged(InInteractionType, GetScopedGraphPanel(), InInteractionArgs.bForce);

		// Cleanup inactive interaction data.
		if (!NewPropertyData->HasInteraction())
		{
			InteractedPropertyData.Remove(InProperty);
		}
		
		return true;
	}

	return false;
}

void FSMPropertyInteractionManager::SetAllPropertyInteractions(const ESMPropertyInteractionType InInteractionType, const FBulkInteractionArgs& InInteractionArgs)
{
	FInteractionArgs IndividualInteractionArgs;
	IndividualInteractionArgs.ScopedGraphPanel = InInteractionArgs.ScopedGraphPanel;
	IndividualInteractionArgs.bForce = InInteractionArgs.bForce;
	if (const USMGraphNode_StateNodeBase* StateNode = OwningStateNode.Get())
	{
		if (!InInteractionArgs.ExcludeStates.Contains(StateNode))
		{
			for (const TTuple<FGuid, TObjectPtr<USMGraphK2Node_PropertyNode_Base>>& PropertyGraphKeyVal : StateNode->GetAllPropertyGraphNodes())
			{
				if (!InInteractionArgs.ExcludeProperties.Contains(PropertyGraphKeyVal.Value))
				{
					SetPropertyInteraction(PropertyGraphKeyVal.Value.Get(), InInteractionType, IndividualInteractionArgs);
				}
			}
		}
	}
}

void FSMPropertyInteractionManager::TogglePropertySelection(USMGraphK2Node_PropertyNode_Base* InProperty, const TOptional<TWeakPtr<SGraphPanel>>& InGraphPanel)
{
	check(InProperty);
	const ESMPropertyInteractionType InteractionType = IsPropertySelected(InProperty)
		                                             ? ESMPropertyInteractionType::UnsetSelect
		                                             : ESMPropertyInteractionType::SetSelect;

	// Build external interaction args... normally handled under graph property OnInteractProperty, but toggling is called
	// independently.
	FInteractionArgs InteractionArgs = BuildInteractionArgsForExternalSelections(InteractionType,
		InGraphPanel);
	InteractionArgs.ScopedGraphPanel = InGraphPanel;
	
	SetPropertyInteraction(
		InProperty,
		InteractionType,
		InteractionArgs);
}

void FSMPropertyInteractionManager::DeselectAllProperties()
{
	RemoveTemporaryControl();
	TemporaryOwningStateNodes.Empty();
	
	TMap<TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>, TSharedPtr<FSMPropertyInteractionData>> CurrentAccessedProperties =
	InteractedPropertyData;
	for (const TTuple<TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>, TSharedPtr<FSMPropertyInteractionData>>& PropertyData : CurrentAccessedProperties)
	{
		SetPropertyInteraction(PropertyData.Key.Get(), ESMPropertyInteractionType::UnsetSelect
			| ESMPropertyInteractionType::UnsetHover
			| ESMPropertyInteractionType::UnsetDisplayInputConnector
			| ESMPropertyInteractionType::UnsetDisplayOutputConnector);
	}
}

bool FSMPropertyInteractionManager::IsPropertySelected(const USMGraphK2Node_PropertyNode_Base* InProperty, bool bRequireDirectSelection) const
{
	if (const TSharedPtr<FSMPropertyInteractionData>* PropertyData = InteractedPropertyData.Find(InProperty))
	{
		return (*PropertyData)->bSelected && (!bRequireDirectSelection || !(*PropertyData)->IsSelectedExternally());
	}
	
	return false;
}

bool FSMPropertyInteractionManager::IsPropertyHovered(const USMGraphK2Node_PropertyNode_Base* InProperty) const
{
	if (const TSharedPtr<FSMPropertyInteractionData>* PropertyData = InteractedPropertyData.Find(InProperty))
	{
		return (*PropertyData)->bHovered;
	}
	
	return false;
}

bool FSMPropertyInteractionManager::IsPropertyInteractedWith(const USMGraphK2Node_PropertyNode_Base* InProperty,
	TSharedPtr<FSMPropertyInteractionData>* OutInteractionData, const TOptional<TWeakPtr<SGraphPanel>>& InGraphPanel) const
{
	bool bInteractedWith = false;
	if (const TSharedPtr<FSMPropertyInteractionData>* PropertyDataPtr = InteractedPropertyData.Find(InProperty))
	{
		const FSMPropertyInteractionData& PropertyData = *PropertyDataPtr->Get();
		if ((!InGraphPanel.IsSet() || InGraphPanel.GetValue() == GetScopedGraphPanel())
		&& (PropertyData.bHovered
		|| PropertyData.bSelected
		|| (PropertyData.bForceDisplayInputConnectors && InProperty->IsInputProperty())
		|| (PropertyData.bForceDisplayOutputConnectors && InProperty->IsOutputProperty())))
		{
			bInteractedWith = true;
		}

		if (OutInteractionData)
		{
			*OutInteractionData = *PropertyDataPtr;
		}
	}
	
	return bInteractedWith;
}

bool FSMPropertyInteractionManager::IsPropertyDirectlyInteractedWith(const USMGraphK2Node_PropertyNode_Base* InProperty,
	bool bRequireDirectSelection, const TOptional<TWeakPtr<SGraphPanel>>& InGraphPanel) const
{
	return (IsPropertySelected(InProperty, bRequireDirectSelection) || IsPropertyHovered(InProperty))
		&& ((!InGraphPanel.IsSet() || !GetScopedGraphPanel().IsValid()) || InGraphPanel.GetValue() == GetScopedGraphPanel());
}

USMGraphK2Node_PropertyNode_Base* FSMPropertyInteractionManager::GetSelectedProperty(bool bRequireDirectSelection) const
{
	for (const TTuple<TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>, TSharedPtr<FSMPropertyInteractionData>>& PropertyData : InteractedPropertyData)
	{
		if (PropertyData.Key.IsValid() && PropertyData.Value->bSelected
			&& (!bRequireDirectSelection || !PropertyData.Value->IsSelectedExternally()))
		{
			return PropertyData.Key.Get();
		}
	}

	return nullptr;
}


TSharedPtr<FSMPropertyInteractionData> FSMPropertyInteractionManager::GetPropertyInteractionData(
	const USMGraphK2Node_PropertyNode_Base* InProperty) const
{
	if (const TSharedPtr<FSMPropertyInteractionData>* PropertyData = InteractedPropertyData.Find(InProperty))
	{
		return *PropertyData;
	}

	return nullptr;
}

void FSMPropertyInteractionManager::RemoveTemporaryOwnerState(
	const TWeakObjectPtr<USMGraphNode_StateNodeBase>& OwnerState)
{
	TArray<TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>> PropertiesToDeselect;
	for (const TTuple<TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>, TSharedPtr<FSMPropertyInteractionData>>&
	     InteractionData : InteractedPropertyData)
	{
		if (InteractionData.Key.IsValid() && InteractionData.Value.IsValid()
			&& InteractionData.Value->ExternalSelector == OwnerState)
		{
			PropertiesToDeselect.Add(InteractionData.Key);
		}
	}

	const ESMPropertyInteractionType UnselectFlags = GetInteractionTypeFlagsForExternalSelection(
		ESMPropertyInteractionType::UnsetSelect);

	for (const TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>& PropertyNode : PropertiesToDeselect)
	{
		if (PropertyNode.IsValid())
		{
			SetPropertyInteraction(PropertyNode.Get(), UnselectFlags);
		}
	}
	
	TemporaryOwningStateNodes.Remove(OwnerState);
}

void FSMPropertyInteractionManager::ReapplyConnectedSelections()
{
	// Remove all external connections and reapply as necessary.
	RemoveTemporaryControl();
	if (USMGraphK2Node_PropertyNode_Base* SelectedProperty = GetSelectedProperty(true))
	{
		SetPropertyInteraction(SelectedProperty, ESMPropertyInteractionType::SetSelect,
			BuildInteractionArgsForExternalSelections(ESMPropertyInteractionType::SetSelect, GetScopedGraphPanel()));
	}
}

void FSMPropertyInteractionManager::SetAllPropertyInteractionsForGraph(USMGraph* InGraph,
                                                                       const ESMPropertyInteractionType InInteractionType,
                                                                       const FBulkInteractionArgs& InInteractionArgs)
{
	for (const TObjectPtr<UEdGraphNode>& Node : InGraph->Nodes)
	{
		if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(Node))
		{
			if (InInteractionArgs.ExcludeStates.Contains(StateNode))
			{
				continue;
			}

			StateNode->GetPropertyInteractionManager().SetAllPropertyInteractions(InInteractionType, InInteractionArgs);
		}
	}
}

FSMPropertyInteractionManager::FInteractionArgs FSMPropertyInteractionManager::BuildInteractionArgsForExternalSelections(
	ESMPropertyInteractionType InInteraction, const TOptional<TWeakPtr<SGraphPanel>>& InGraphPanel)
{
	FInteractionArgs Args;
	Args.ConnectedPropertyInteractionArgs.InteractionType = GetInteractionTypeFlagsForExternalSelection(InInteraction);
	Args.ScopedGraphPanel = InGraphPanel;
	// Expand the category if we're selecting it.
	/* todo output variables determine -- we shouldn't auto expand on a normal selection unless we also remember
	 * the previous state, so we can restore it unselection. Otherwise if users are collapsing categories they
	 * will stay expanded which may be annoying.
	if (EnumHasAnyFlags(Args.ConnectedPropertyInteractionArgs.InteractionType,
			ESMPropertyInteractionType::SetSelect))
	{
		Args.ConnectedPropertyInteractionArgs.bExpandCategory = true;
	}*/
	
	return Args;
}

ESMPropertyInteractionType FSMPropertyInteractionManager::GetInteractionTypeFlagsForExternalSelection(
	ESMPropertyInteractionType InInteraction)
{
	// If we're performing a selection we should select any connected properties.
	if (EnumHasAnyFlags(InInteraction, ESMPropertyInteractionType::SetSelect))
	{
		return ESMPropertyInteractionType::SetSelect
		| ESMPropertyInteractionType::SetDisplayInputConnector
		| ESMPropertyInteractionType::SetDisplayOutputConnector;
	}
	if (EnumHasAnyFlags(InInteraction, ESMPropertyInteractionType::UnsetSelect))
	{
		return ESMPropertyInteractionType::UnsetSelect
		| ESMPropertyInteractionType::UnsetDisplayInputConnector
		| ESMPropertyInteractionType::UnsetDisplayOutputConnector;
	}

	return ESMPropertyInteractionType::None;
}

bool FSMPropertyInteractionManager::IsGraphPanelSupported(const TWeakPtr<const SGraphPanel>& InGraphPanel)
{
	return !InGraphPanel.IsValid() || InGraphPanel.Pin()->GetType() != TEXT("SSMSearchGraphPanel");
}

void FSMPropertyInteractionManager::BindToStateMachineEditorEvents(
   ISMStateMachineBlueprintEditor* StateMachineBlueprintEditor)
{
	if (OwningBlueprint.IsValid() && StateMachineBlueprintEditor
		&& StateMachineBlueprintEditor->GetBlueprintObj() == OwningBlueprint.Get())
	{
		NodeSelectionChangedHandle = StateMachineBlueprintEditor->OnSelectedNodesChanged().AddRaw(this,
			&FSMPropertyInteractionManager::OnStateMachineEditorSelectionChanged);
	}
}

void FSMPropertyInteractionManager::UnbindFromStateMachineEditorEvents(
	ISMStateMachineBlueprintEditor* StateMachineBlueprintEditor)
{
	if (StateMachineBlueprintEditor->GetBlueprintObj() == OwningBlueprint.Get())
	{
		StateMachineBlueprintEditor->OnSelectedNodesChanged().Remove(NodeSelectionChangedHandle);
	}
}

void FSMPropertyInteractionManager::OnStateMachineEditorOpened(
	ISMStateMachineBlueprintEditor* StateMachineBlueprintEditor)
{
	BindToStateMachineEditorEvents(StateMachineBlueprintEditor);
}

void FSMPropertyInteractionManager::OnStateMachineEditorClosed(
	ISMStateMachineBlueprintEditor* StateMachineBlueprintEditor)
{
	UnbindFromStateMachineEditorEvents(StateMachineBlueprintEditor);
}

void FSMPropertyInteractionManager::OnStateMachineEditorSelectionChanged(
	TSharedPtr<ISMStateMachineBlueprintEditor> StateMachineBlueprintEditor, const TSet<UObject*>& NewSelection)
{
	if (!OwningStateNode.IsValid())
	{
		// We've gone stale and couldn't remove ourselves from the BP editor.
		StateMachineBlueprintEditor->OnSelectedNodesChanged().Remove(NodeSelectionChangedHandle);
		return;
	}
	
	if (!NewSelection.Contains(OwningStateNode.Get()))
	{
		bool SelectionHasTemporaryOwner = false;
		for (const TWeakObjectPtr<USMGraphNode_StateNodeBase>& WeakNode : TemporaryOwningStateNodes)
		{
			if (const USMGraphNode_StateNodeBase* Node = WeakNode.Get())
			{
				if (NewSelection.Contains(Node))
				{
					SelectionHasTemporaryOwner = true;
					break;
				}
			}
		}

		if (!SelectionHasTemporaryOwner)
		{
			DeselectAllProperties();
		}
	}
}

void FSMPropertyInteractionManager::SelectOwningStateNode()
{
	if (ISMStateMachineBlueprintEditor* Editor = FSMBlueprintEditorUtils::GetStateMachineEditor(OwningStateNode.Get()))
	{
		if (USMGraphNode_StateNodeBase* StateUEdNode = OwningStateNode.Get())
		{
			if (!Editor->GetSelectedNodes().Contains(StateUEdNode))
			{
				const FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();

				const bool bAddToSelection = ModifierKeys.IsShiftDown() || ModifierKeys.IsControlDown();
				if (bAddToSelection)
				{
					Editor->AddToSelection(StateUEdNode);
				}
				else
				{
					Editor->SelectNodes({StateUEdNode});
				}
			}
		}
	}
}

bool FSMPropertyInteractionManager::IsInteractionFromAnotherNode(const FInteractionArgs& InInteractionArgs) const
{
	return InInteractionArgs.OriginatingRequest.IsValid() && InInteractionArgs.OriginatingRequest != OwningStateNode;
}

void FSMPropertyInteractionManager::RemoveTemporaryControl()
{
	for (const TWeakObjectPtr<USMGraphNode_StateNodeBase>& ControlledNode : TemporaryControlledStateNodes)
	{
		if (ControlledNode.IsValid())
		{
			ControlledNode->GetPropertyInteractionManager().RemoveTemporaryOwnerState(OwningStateNode);
		}
	}
}