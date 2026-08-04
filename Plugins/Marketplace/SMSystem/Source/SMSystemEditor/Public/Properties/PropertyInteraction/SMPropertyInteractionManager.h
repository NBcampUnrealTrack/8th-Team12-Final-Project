// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "SMPropertyInteractionData.h"

class ISMStateMachineBlueprintEditor;
class UBlueprint;
class USMGraph;
class USMGraphK2Node_PropertyNode_Base;
class USMGraphNode_StateNodeBase;
class SGraphPanel;

/**
 * Manager class for interacting with graph properties. Selecting, hovering, and toggling connector displays
 * can be done through here.
 *
 * @note This is currently instanced per USMGraphNode_StateNodeBase, and we attempt to scope interactions to
 * a single SGraphPanel. Selections and hover should only show up for one active panel, with IO connections potentially
 * displayed across graph panels to assist with drag and drop. Ideally, we would have the manager at the SGraphPanel
 * level, but extending the Blueprint Editor's graph panel is not practical when this works almost as well.
 */
class FSMPropertyInteractionManager
{
public:
	/** Color when property selected. */
	static FLinearColor PropertySelectedColor;
	/** Color when property hovered. */
	static FLinearColor PropertyHoveredColor;
	/** Color when both selected and highlighted. */
	static FLinearColor PropertySelectedAndHoveredColor;

	/** Args for interacting with connected properties. */
	struct FConnectedPropertyInteractionArgs
	{
		FConnectedPropertyInteractionArgs(): InteractionType(ESMPropertyInteractionType::None),
		                                     bApplyBehaviorToOwningGraphOnly(true), bAllowMultiSelect(true)
		{
		}

		/**
		 * Interaction type to apply to connected properties.
		 */
		ESMPropertyInteractionType InteractionType;

		/** Only apply connected property behavior to properties on the same graph as this one. */
		bool bApplyBehaviorToOwningGraphOnly;

		/** If we should allow multiselect of connected properties. */
		bool bAllowMultiSelect;

		/** Auto expand the category if it is collapsed. */
		TOptional<bool> bExpandCategory;
	};

	/** Arguments when interacting with an individual property. */
	struct FInteractionArgs
	{
		FInteractionArgs(): bAllowMultiSelect(false), bForce(false)
		{
		}

		/**
		 * Behavior to be applied to connected properties.
		 * For example, if selecting a property we may want to highlight its connected properties.
		 */
		FConnectedPropertyInteractionArgs ConnectedPropertyInteractionArgs;

		/** The state originating this request. When empty it is assumed the owning state node made the request. */
		TWeakObjectPtr<USMGraphNode_StateNodeBase> OriginatingRequest;

		/** Scope the request to this graph panel. */
		TOptional<TWeakPtr<SGraphPanel>> ScopedGraphPanel;
		
		/** If we're allowed to multi-select more than one property per node. */
		bool bAllowMultiSelect;

		/** Should this be forced? Will force even if no change was detected. */
		bool bForce;

		/** Auto expand the category if it is collapsed. */
		TOptional<bool> bExpandCategory;
	};
	
	/** Arguments when interacting with properties in bulk. */
	struct FBulkInteractionArgs
	{
		FBulkInteractionArgs() : bForce(false) {}
		
		/** States to exclude. */
		TSet<USMGraphNode_StateNodeBase*> ExcludeStates;
		/** Properties to exclude. */
		TSet<USMGraphK2Node_PropertyNode_Base*> ExcludeProperties;
		/** Scope the request to this graph panel. */
		TOptional<TWeakPtr<SGraphPanel>> ScopedGraphPanel;
		/** Should this be forced? Will force even if no change was detected. */
		bool bForce;
	};
	
	FSMPropertyInteractionManager(USMGraphNode_StateNodeBase* InStateNodeOwner);

	~FSMPropertyInteractionManager();
	
	/**
	 * Notify a property is being interacted with.
	 * @return True if the property changed, false if there was no change.
	 */
	bool SetPropertyInteraction(USMGraphK2Node_PropertyNode_Base* InProperty, const ESMPropertyInteractionType InInteractionType,
		const FInteractionArgs& InInteractionArgs = FInteractionArgs());

	/** Set the interaction for all properties. */
	void SetAllPropertyInteractions(const ESMPropertyInteractionType InInteractionType,
		const FBulkInteractionArgs& InInteractionArgs = FBulkInteractionArgs());

	/** Toggle the property selection state of a property. */
	void TogglePropertySelection(USMGraphK2Node_PropertyNode_Base* InProperty, const TOptional<TWeakPtr<SGraphPanel>>& InGraphPanel = TOptional<TWeakPtr<SGraphPanel>>());
	
	/** Make sure all selected properties are no longer selected. */
	void DeselectAllProperties();
	
	/**
	 * Checks if a property is selected.
	 * @param InProperty The property to check.
	 * @param bRequireDirectSelection If the property needs to have been directly selected, rather than indirectly such
	 * as from another state.
	 */
	bool IsPropertySelected(const USMGraphK2Node_PropertyNode_Base* InProperty, bool bRequireDirectSelection = false) const;

	/** Checks if a property is hovered. */
	bool IsPropertyHovered(const USMGraphK2Node_PropertyNode_Base* InProperty) const;

	/**
	 * Checks for any level of interaction with a property.
	 * @param InProperty The property to check for interaction.
	 * @param OutInteractionData [Optional] The interaction data of the property.
	 * @param InGraphPanel [Optional] If this check should be scoped to a specific graph panel.
	 *
	 * @return True if the property is interacted with.
	 */
	bool IsPropertyInteractedWith(const USMGraphK2Node_PropertyNode_Base* InProperty,
		TSharedPtr<FSMPropertyInteractionData>* OutInteractionData = nullptr, const TOptional<TWeakPtr<SGraphPanel>>& InGraphPanel = TOptional<TWeakPtr<SGraphPanel>>()) const;

	/** If a property has direct interaction, such as hover or selection. */
	bool IsPropertyDirectlyInteractedWith(const USMGraphK2Node_PropertyNode_Base* InProperty, bool bRequireDirectSelection,
		const TOptional<TWeakPtr<SGraphPanel>>& InGraphPanel = TOptional<TWeakPtr<SGraphPanel>>()) const;
	
	/** Get the first selected property. */
	USMGraphK2Node_PropertyNode_Base* GetSelectedProperty(bool bRequireDirectSelection = false) const;
	
	/** Get the interaction state of a property. */
	TSharedPtr<FSMPropertyInteractionData> GetPropertyInteractionData(const USMGraphK2Node_PropertyNode_Base* InProperty) const;

	/** Removes a temporary owner state. */
	void RemoveTemporaryOwnerState(const TWeakObjectPtr<USMGraphNode_StateNodeBase>& OwnerState);

	/** Look for updated connections and reapply any selections. */
	void ReapplyConnectedSelections();

	/** Retrieve the currently scoped graph panel. */
	const TWeakPtr<SGraphPanel>& GetScopedGraphPanel() const { return ScopedGraphPanel; }

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnExpansionStateChanged, USMGraphK2Node_PropertyNode_Base*, bool);
	FOnExpansionStateChanged& GetOnExpansionStateChanged() { return OnExpansionStateChangedEvent; }
	
	/** Run interactions for all states in a graph. */
	static void SetAllPropertyInteractionsForGraph(USMGraph* InGraph, const ESMPropertyInteractionType InInteractionType, const FBulkInteractionArgs& InInteractionArgs = FBulkInteractionArgs());

	/**
	 * Build interaction args which can propagate to connected nodes depending on the interaction type.
	 */
	static FInteractionArgs BuildInteractionArgsForExternalSelections(ESMPropertyInteractionType InInteraction,
		const TOptional<TWeakPtr<SGraphPanel>>& InGraphPanel = TOptional<TWeakPtr<SGraphPanel>>());
	
	/**
	 * Get the correct flags to use given an external selection. IE, SetSelect will produce multiple selection type flags,
	 * and UnsetSelect will produce multiple unselect type flags.
	 */
	static ESMPropertyInteractionType GetInteractionTypeFlagsForExternalSelection(ESMPropertyInteractionType InInteraction);

	/** Checks if a graph panel is supported for interaction. */
	static bool IsGraphPanelSupported(const TWeakPtr<const SGraphPanel>& InGraphPanel);
	
private:
	/** Bind to instance events for a BP Editor. */
	void BindToStateMachineEditorEvents(ISMStateMachineBlueprintEditor* StateMachineBlueprintEditor);
	
	/** Unbind from instance events of a BP editor. */
	void UnbindFromStateMachineEditorEvents(ISMStateMachineBlueprintEditor* StateMachineBlueprintEditor);
	
	/** Callback when a state machine editor is opened. */
	void OnStateMachineEditorOpened(ISMStateMachineBlueprintEditor* StateMachineBlueprintEditor);

	/** Callback when a state machine editor is closed. */
	void OnStateMachineEditorClosed(ISMStateMachineBlueprintEditor* StateMachineBlueprintEditor);
	
	/** Callback when the state machine editor has changed its selection. */
	void OnStateMachineEditorSelectionChanged(TSharedPtr<ISMStateMachineBlueprintEditor> StateMachineBlueprintEditor, const TSet<UObject*>& NewSelection);

	/** Ensure the owning state node is selected. */
	void SelectOwningStateNode();

	/** Checks if a request originated another node or if it was from us. */
	bool IsInteractionFromAnotherNode(const FInteractionArgs& InInteractionArgs) const;

	/** Removes self from nodes under our temporary control. */
	void RemoveTemporaryControl();
	
private:
	/** Property nodes to their current interaction level. */
	TMap<TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>, TSharedPtr<FSMPropertyInteractionData>> InteractedPropertyData;

	/** The state node which owns this manager. */
	TWeakObjectPtr<USMGraphNode_StateNodeBase> OwningStateNode;

	/** Weak object ptr to the owning blueprint. */
	TWeakObjectPtr<UBlueprint> OwningBlueprint;

	/**
	 * Other states which temporarily are our "owner".
	 * This occurs if another state is making an interaction request (selection) on this manager.
	 */
	TSet<TWeakObjectPtr<USMGraphNode_StateNodeBase>> TemporaryOwningStateNodes;

	/**
	 * Other state nodes which we temporarily control.
	 * This is primarily for if we are selecting properties on another manager.
	 */
	TSet<TWeakObjectPtr<USMGraphNode_StateNodeBase>> TemporaryControlledStateNodes;

	/** When the owning state machine blueprint editor changes its selection. */
	FDelegateHandle NodeSelectionChangedHandle;

	/** Handles for when a BP editor is opened or closed. */
	FDelegateHandle BlueprintEditorOpenedHandle, BlueprintEditorClosedHandle;

	/** Called when the interaction manager changes the expansion state. */
	FOnExpansionStateChanged OnExpansionStateChangedEvent;

	/** Scope highlight and selection requests to this graph panel. */
	TWeakPtr<SGraphPanel> ScopedGraphPanel;
};
