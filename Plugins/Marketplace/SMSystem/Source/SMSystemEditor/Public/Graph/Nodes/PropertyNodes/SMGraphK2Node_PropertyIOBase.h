// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Graph/Nodes/FunctionNodes/SMGraphK2Node_FunctionNode.h"
#include "Properties/SMEditorPropertyIOUtils.h"

#include "SMGraphK2Node_PropertyIOBase.generated.h"

class USMGraphNode_Base;
class USMGraphK2Node_PropertyNode_Base;
class UK2Node_MakeStruct;

/**
 * Base node for output variables (reading and writing).
 */
UCLASS(MinimalAPI, Abstract)
class USMGraphK2Node_PropertyIOBase : public USMGraphK2Node_FunctionNode
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual bool IsCompatibleWithGraph(UEdGraph const* Graph) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void DestroyNode() override;
	virtual void ReconstructNode() override;
	// ~UEdGraphNode
	
	// USMGraphK2Node_Base
	virtual bool CanCollapseNode() const override { return false; }
	virtual void PreCompileValidate(FCompilerResultsLog& MessageLog) override;
	// ~USMGraphK2Node_Base

	/** Called from the property node responsible for this IO Node. */
	virtual void PreCompile();
	
	/**
	 * Perform processing when a property pin has changed. This will update a corresponding connector pin
	 * matching the property pin.
	 * @param InPropertyPin The property pin we are updating.
	 * @param bIsUserAction Whether the user initiated the action directly or not.
	 */
	void UpdatePropertySelectionPin(UEdGraphPin* InPropertyPin, const bool bIsUserAction);

	/**
	 * Set the pin's value.
	 * @param InPropertyPin The graph pin to set.
	 * @param InPropertyValue The value to set the graph pin.
	 * @param InPropertyNameOverride The property name, if it should be different from the InPropertyValue data.
	 */
	void SetPropertySelectionPinValue(UEdGraphPin* InPropertyPin,
		const TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base>& InPropertyValue,
		const TOptional<FName>& InPropertyNameOverride = TOptional<FName>());
	
	/** Build node property data of this node owner. */
	TSharedPtr<LD::Editor::PropertyUtils::IO::FNodePropertyData> GetOwningPropertyNodePropertyData() const;
	
	/** Return the property node connected to this execution. */
	USMGraphK2Node_PropertyNode_Base* GetOwningPropertyNode() const;

	/** Find the owning graph node containing this property output node. */
	USMGraphNode_Base* GetOwningGraphNode() const;
	
	/** Try to locate the related pin to our property node. */
	virtual UEdGraphPin* TryGetOwningPropertyNodeRelatedPin() const { return nullptr; }

	/** If we should also break the result pin links on a type change. */
	virtual bool ShouldBreakResultPinLinksOnTypeChange() const { return false; }

	/** Check for a direct connection to the owning property node. */
	virtual bool IsConnectedToOwningPropertyNode() const { return false; }
	
	/** The generic identifier used to determine if this pin is for a property. */
	static FName GetPropertySelectionPinIdentifierType();

	/** Checks if a pin should allow selecting a property. This needs to return true for our custom pin widget to display. */
	virtual bool IsPropertySelectionPin(const UEdGraphPin* Pin) const;
	
	/** Checks if a pin is input or output to another pin. This needs to return true for our custom pin widget to display. */
	virtual bool IsPropertyConnectorPin(const UEdGraphPin* Pin) const;

	/** If the connector pin should be displayed on the same line as the property selector. */
	virtual bool CanDisplayConnectorPin(const TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base>& InPropertyData, const UEdGraphPin* InPin) const { return false;}

	/** If we're allowed to create a connection to another node. */
	virtual bool CanAcceptConnection(const USMGraphK2Node_PropertyIOBase* OtherPropertyIONode) const;

	/** If we're allowed to accept a connection given just a property node. */
	virtual bool CanAcceptConnection(const USMGraphK2Node_PropertyNode_Base* OtherPropertyNode) const;
	
	/** Retrieve all pins that are property selections. */
	TArray<UEdGraphPin*> GetAllPropertySelectionPins() const;
	
	/** Add a property node we are referencing through IO. */
	void AddReferencedIONode(const FGuid& InPinGuid, USMGraphK2Node_PropertyNode_Base* InNode);

	/** Remove a referenced property from IO. */
	void RemoveReferencedIONode(const FGuid& InPinGuid);

	/** Return how many times another property is referenced. */
	int32 GetIOReferenceCount(const USMGraphK2Node_PropertyNode_Base* InPropertyNode) const;
	
	/** Find another property node we are referencing from a pin. */
	USMGraphK2Node_PropertyNode_Base* GetReferencedIONode(const FGuid& InPinGuid) const;

	/** Return all referenced IO nodes. */
	SMSYSTEMEDITOR_API TArray<USMGraphK2Node_PropertyNode_Base*> GetAllReferencedIONodes() const;

	/** Attempt to remake any connections. Can be useful if the node was cloned. */
	void ReapplyConnections();
	
	/** Remove any IO nodes references that are no longer valid. */
	void CleanInvalidReferencedIONodes();
	
protected:
	/** Compile validation the pin data is correct and the corresponding property referenced. */
	virtual void VerifyCorrectPinData(const UEdGraphPin* InPin, FCompilerResultsLog& InMessageLog) const;
	
	/** Create a struct with property path args consisting of property data, and wire to the output pin. */
	UK2Node_MakeStruct* MakeAndWirePropertyPathStruct(FSMKismetCompilerContext& CompilerContext,
		const TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base>& PropertyData,
		UEdGraphPin* OutputToPin);
	
	/** Set the defaults of the selection pin. */
	void SetPropertySelectionPinDefaults(UEdGraphPin* Pin);

	/** Set the defaults of the connector pin. */
	void SetPropertyConnectorPinDefaults(UEdGraphPin* Pin);

public:
	/** Look for a pin matching the name of the pin in the opposite direction. */
	SMSYSTEMEDITOR_API UEdGraphPin* FindOppositePin(const UEdGraphPin* InPin) const;
	UEdGraphPin* FindOppositePin(const FName& InPinName) const;
	
	static FString GetInputPinPrefix() { return TEXT("In_"); }
	static FString GetOutputPinPrefix() { return TEXT("Out_"); }

	/** Retrieve the maximum allowed referenced IO nodes. */
	virtual int32 GetMaxReferencedIONodes() const { /** Can't pure virtual*/ checkNoEntry(); return 0; };

protected:
	/** If we are setting the default value of a pin not through user interaction. */
	bool bProgramaticallySettingPropertyPinValue = false;

	/** If UpdatePropertySelectionPin() should be called on a default value change. */
	bool bCallUpdatePropertySelectionOnValueChange = true;
	
private:
	/** Other nodes we are referencing. Pin Guid to Reference */
	UPROPERTY()
	TMap<FGuid, TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>> ReferencedIONodes;
};
