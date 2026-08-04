// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphSchema.h"

class UEdGraphNode;
class UEdGraphPin;

/** Action to add a node to the graph */
struct SMSYSTEMEDITOR_API FSMGraphSchemaAction_NewNode : FEdGraphSchemaAction
{
	FSMGraphSchemaAction_NewNode(): OwnerOfTemporaries(nullptr), GraphNodeTemplate(nullptr),
									bDontOverrideDefaultClass(false),
									bDontCallPostPlacedNode(false)
	{
	}

	FSMGraphSchemaAction_NewNode(const FText& InNodeCategory, const FText& InMenuDesc, const FText& InToolTip, const int32 InGrouping)
		: FEdGraphSchemaAction(InNodeCategory, InMenuDesc, InToolTip, InGrouping), OwnerOfTemporaries(nullptr),
		  GraphNodeTemplate(nullptr),
		  bDontOverrideDefaultClass(false), bDontCallPostPlacedNode(false)
	{
	}

	// FEdGraphSchemaAction
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode = true) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// ~FEdGraphSchemaAction

	/** Only used if the TransientPackage is provided as the outer. */
	TObjectPtr<UEdGraph> OwnerOfTemporaries;

	/** The UEdGraphNode to be spawned. */
	TObjectPtr<UEdGraphNode> GraphNodeTemplate;

	TSoftClassPtr<UObject> NodeSoftClassPath;
	bool bDontOverrideDefaultClass;
	bool bDontCallPostPlacedNode;
};

/** Action to create new comment */
struct FSMGraphSchemaAction_NewComment : FEdGraphSchemaAction
{
	FSMGraphSchemaAction_NewComment()
	{}

	FSMGraphSchemaAction_NewComment(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
	{}

	// FEdGraphSchemaAction
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode = true) override;
	// ~FEdGraphSchemaAction
};
