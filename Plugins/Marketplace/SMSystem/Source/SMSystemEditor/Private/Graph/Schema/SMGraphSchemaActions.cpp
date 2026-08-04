// Copyright Recursoft LLC. All Rights Reserved.

#include "Graph/Schema/SMGraphSchemaActions.h"

#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/Nodes/SMGraphNode_TransitionEdge.h"
#include "Graph/Schema/SMGraphSchema.h"
#include "Utilities/SMBlueprintEditorUtils.h"
#include "Utilities/SMNodeInstanceUtils.h"

#include "EdGraphNode_Comment.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

UEdGraphNode* FSMGraphSchemaAction_NewNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode /*= true*/)
{
	UEdGraphNode* ResultNode = nullptr;

	if (GraphNodeTemplate != nullptr)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "AddNode", "Add Node"));
		ParentGraph->Modify();
		if (FromPin != nullptr)
		{
			FromPin->Modify();
		}

		// When called from a context menu an owner of temporaries is provided, but otherwise one generally isn't.
		// We need to be parented to a UEdGraph for proper handling through UE, especially on undo operations when
		// not having a graph outer can trigger an ensure.
		if (GraphNodeTemplate->GetOuter() == GetTransientPackage())
		{
			OwnerOfTemporaries = NewObject<UEdGraph>(GetTransientPackage());
			GraphNodeTemplate->Rename(nullptr, OwnerOfTemporaries, REN_DontCreateRedirectors | REN_AllowPackageLinkerMismatch);
		}

		// Setup transaction history early so undoing the creation of a transition will properly place the template
		// back on the owner of temporaries. Otherwise stale data will persist during a copy unless GC occurs first.
		GraphNodeTemplate->SetFlags(RF_Transactional);
		GraphNodeTemplate->Modify();

		GraphNodeTemplate->Rename(nullptr, ParentGraph, REN_DontCreateRedirectors | REN_AllowPackageLinkerMismatch);
		ParentGraph->AddNode(GraphNodeTemplate, true, bSelectNewNode);

		GraphNodeTemplate->CreateNewGuid();

		UClass* NodeClass = NodeSoftClassPath.LoadSynchronous();

		// Optimization to not double generate a template.
		if (USMGraphNode_Base* GraphNode = Cast<USMGraphNode_Base>(GraphNodeTemplate))
		{
			GraphNode->bGenerateTemplateOnNodePlacement = NodeClass == nullptr;
		}

		if (!bDontCallPostPlacedNode)
		{
			GraphNodeTemplate->PostPlacedNewNode();
		}

		GraphNodeTemplate->AllocateDefaultPins();
		
		GraphNodeTemplate->NodePosX = Location.X;
		GraphNodeTemplate->NodePosY = Location.Y;

		ResultNode = GraphNodeTemplate;

		if (NodeClass == nullptr && !bDontOverrideDefaultClass)
		{
			// Check for node defaults set under project settings.
			// Custom rules will still override these.
			
			NodeClass = FSMNodeInstanceUtils::GetDefaultNodeClassForGraphNode(GraphNodeTemplate);
		}
		
		// Set the actual node class if one is set.
		if (NodeClass)
		{
			if (USMGraphNode_Base* GraphNode = Cast<USMGraphNode_Base>(GraphNodeTemplate))
			{
				if (GraphNode->GetBoundGraph())
				{
					// If no graph is assigned we fail a check.
					GraphNode->SetNodeClass(NodeClass);
				}
				else
				{
					// Only a transition may legitimately not have a graph assigned at this point. It gets set later.
					ensureMsgf(GraphNodeTemplate->IsA<USMGraphNode_TransitionEdge>(),
						TEXT("Graph node '%s' of type '%s' could not assign node class '%s' because there is no bound graph."),
						*GraphNode->GetNodeName(), *GraphNodeTemplate->GetClass()->GetName(), *NodeClass->GetName());
				}
				GraphNode->CreateGraphPropertyGraphs();

				// If the instance has a custom name supplied use that.
				USMGraphSchema::RenameNodeFromTemplateData(GraphNode);
			}
		}

		// Check for transition that needs to be set from the previous state to this one.
		if (FromPin != nullptr)
		{
			if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(GraphNodeTemplate))
			{
				if (USMGraphNode_TransitionEdge* TransitionNode = StateNode->GetPreviousTransition())
				{
					if (const USMGraphNode_StateNodeBase* FromNode = TransitionNode->GetFromState())
					{
						const UClass* StateMachineClass = FSMBlueprintEditorUtils::GetStateMachineClassFromGraph(ParentGraph);
						USMGraphSchema::SetTransitionClassFromRules(TransitionNode, FromNode ? FromNode->GetNodeClass() : nullptr,
							NodeClass, StateMachineClass);
					}
				}
			}

			GraphNodeTemplate->AutowireNewNode(FromPin);
		}
		ParentGraph->NotifyGraphChanged();
		
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraphChecked(ParentGraph);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	return ResultNode;
}

void FSMGraphSchemaAction_NewNode::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEdGraphSchemaAction::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(GraphNodeTemplate);
	if (OwnerOfTemporaries)
	{
		Collector.AddReferencedObject(OwnerOfTemporaries);
	}
}

UEdGraphNode* FSMGraphSchemaAction_NewComment::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin,
                                                             const FVector2f& Location, bool bSelectNewNode)
{
	// Add menu item for creating comment boxes
	UEdGraphNode_Comment* CommentTemplate = NewObject<UEdGraphNode_Comment>();

	const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(ParentGraph);

	FVector2f SpawnLocation = Location;

	FSlateRect Bounds;
	if (Blueprint != nullptr && FKismetEditorUtilities::GetBoundsForSelectedNodes(Blueprint, Bounds, 50.0f))
	{
		CommentTemplate->SetBounds(Bounds);
		SpawnLocation.X = CommentTemplate->NodePosX;
		SpawnLocation.Y = CommentTemplate->NodePosY;
	}

	return FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UEdGraphNode_Comment>(ParentGraph, CommentTemplate, SpawnLocation);
}
