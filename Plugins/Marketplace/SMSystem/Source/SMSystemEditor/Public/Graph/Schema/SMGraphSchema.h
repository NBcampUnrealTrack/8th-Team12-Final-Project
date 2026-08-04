// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphSchema.h"
#include "Templates/SubclassOf.h"

#include "SMGraphSchema.generated.h"

class USMGraphNode_Base;
class USMGraphNode_StateNode;
class USMGraphNode_TransitionEdge;
class USMTransitionInstance;

UCLASS()
class SMSYSTEMEDITOR_API USMGraphSchema : public UEdGraphSchema
{
	GENERATED_UCLASS_BODY()

public:
	// UEdGraphSchema
	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;
	virtual EGraphType GetGraphType(const UEdGraph* TestEdGraph) const override;
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const override;
	virtual bool TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const override;
	virtual bool CreateAutomaticConversionNodeAndConnections(UEdGraphPin* A, UEdGraphPin* B) const override;
	virtual bool TryRelinkConnectionTarget(UEdGraphPin* SourcePin, UEdGraphPin* OldTargetPin, UEdGraphPin* NewTargetPin, const TArray<UEdGraphNode*>& InSelectedGraphNodes) const override;
	virtual bool IsConnectionRelinkingAllowed(UEdGraphPin* InPin) const override;
	virtual const FPinConnectionResponse CanRelinkConnectionToPin(const UEdGraphPin* OldSourcePin, const UEdGraphPin* TargetPinCandidate) const override;
	virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual void GetGraphDisplayInformation(const UEdGraph& Graph, /*out*/ FGraphDisplayInfo& DisplayInfo) const override;

	virtual void BreakNodeLinks(UEdGraphNode& TargetNode) const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const override;
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;

	virtual bool SupportsDropPinOnNode(UEdGraphNode* InTargetNode, const FEdGraphPinType& InSourcePinType, EEdGraphPinDirection InSourcePinDirection, FText& OutErrorMessage) const override;
	virtual bool CanDuplicateGraph(UEdGraph* InSourceGraph) const override { return false; }
	virtual void HandleGraphBeingDeleted(UEdGraph& GraphBeingRemoved) const override;
	virtual void OnPinConnectionDoubleCicked(UEdGraphPin* PinA, UEdGraphPin* PinB, const FVector2f& GraphPosition) const override;
	// ~UEdGraphSchema

	static bool DoesUserAllowPlacement(const UEdGraphNode* A, const UEdGraphNode* B, FPinConnectionResponse& ResponseOut);

	static bool CanReplaceNode(const UEdGraphNode* InGraphNode);
	static bool CanReplaceNodeWith(const UEdGraphNode* InGraphNode, bool& bStateMachine, bool& bStateMachineRef, bool& bState, bool& bConduit, bool& bStateMachineParent);

	/** Determine the transition class to use when creating a new connection based on user rules. */
	static bool SetTransitionClassFromRules(USMGraphNode_TransitionEdge* InTransitionEdge, const UClass* InFromStateClass,
		const UClass* InToStateClass, const UClass* InStateMachineClass, TSubclassOf<USMTransitionInstance> InBaseClass = nullptr);

	/** Attempt to rename the graph node based on the node instance template information. */
	static void RenameNodeFromTemplateData(USMGraphNode_Base* InGraphNode);
	
protected:
	void GetReplaceWithMenuActions(class FMenuBuilder& MenuBuilder, const UEdGraphNode* InGraphNode) const;
};
