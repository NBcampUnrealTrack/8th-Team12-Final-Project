// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMGraphK2Node_PropertyIOBase.h"
#include "Widgets/Kismet/SMGraphK2Node_LinearExpressionInterface.h"

#include "K2Node_AddPinInterface.h"

#include "SMGraphK2Node_PropertyIOWriterNode.generated.h"

class USMGraphK2Node_PropertyNode_Base;

/**
 * Write output data to variables.
 *
 * Blueprint Variables - Variables in the owning blueprint will be written to immediately.
 * State Variables - When connecting to a state input variable, the output value is written to a proxy variable first.
 * When the state input reads this output, it will query the value of the proxy at that time.
 */
UCLASS(MinimalAPI)
class USMGraphK2Node_PropertyIOWriterNode : public USMGraphK2Node_PropertyIOBase, public IK2Node_AddPinInterface, public ISMGraphK2Node_LinearExpressionInterface
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual bool IsActionFilteredOut(FBlueprintActionFilter const& Filter) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual bool CanUserDeleteNode() const override;
	virtual bool CanDuplicateNode() const override { return false; }
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void OnPinRemoved(UEdGraphPin* InRemovedPin) override;
	// ~UEdGraphNode

	// UK2Node
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	// ~UK2Node
	
	// IK2Node_AddPinInterface
	virtual void AddInputPin() override;
	virtual bool CanAddPin() const override;
	/** Provide either an input or output pin. */
	virtual void RemoveInputPin(UEdGraphPin* Pin) override;
	virtual bool CanRemovePin(const UEdGraphPin* Pin) const override;
	// ~IK2Node_AddPinInterface

	// USMGraphK2Node_Base
	virtual void PreCompileValidate(FCompilerResultsLog& MessageLog) override;
	// ~USMGraphK2Node_Base
	
	// USMGraphK2Node_RuntimeNodeReference
	virtual bool HandlesOwnExpansion() const override { return true; }
	virtual void CustomExpandNode(FSMKismetCompilerContext& CompilerContext, USMGraphK2Node_RuntimeNodeContainer* RuntimeNodeContainer, FProperty* NodeProperty) override;
	// ~USMGraphK2Node_RuntimeNodeReference

	// USMGraphK2Node_PropertyIOBase
	virtual UEdGraphPin* TryGetOwningPropertyNodeRelatedPin() const override;
	virtual bool IsConnectedToOwningPropertyNode() const override;
	virtual bool IsPropertySelectionPin(const UEdGraphPin* Pin) const override;
	virtual bool IsPropertyConnectorPin(const UEdGraphPin* Pin) const override;
	virtual bool CanDisplayConnectorPin(const TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base>& InPropertyData, const UEdGraphPin* InPin) const override;
	virtual bool CanAcceptConnection(const USMGraphK2Node_PropertyIOBase* OtherPropertyIONode) const override;
	virtual bool CanAcceptConnection(const USMGraphK2Node_PropertyNode_Base* OtherPropertyNode) const override;
protected:
	virtual int32 GetMaxReferencedIONodes() const override { return NumOutputs; }
	virtual void VerifyCorrectPinData(const UEdGraphPin* InPin, FCompilerResultsLog& InMessageLog) const override;
	// ~USMGraphK2Node_PropertyIOBase

public:
	// ISMGraphK2Node_LinearExpressionInterface
	virtual TSharedPtr<SWidget> MakeLinearExpressionNodeWidget() const override;
	virtual TSharedPtr<SWidget> MakeLinearExpressionPinWidget(const UEdGraphPin* InPin) const override;
	virtual bool DisplayParenthesis() const override { return false; }
	// ~ISMGraphK2Node_LinearExpressionInterface
	
	/** Checks if we can remove all input and output pins. */
	bool CanRemoveAllPins() const;
	/** Remove all input and output pins. */
	void RemoveAllPins();

	/** Remove a specific property pin. Different from RemoveInputPin so we can pass our own arguments. */
	void RemovePropertyPin(UEdGraphPin* Pin, bool bModify);

	/** Make sure primary input pin is the correct type. */
	void UpdatePrimaryInputPin(UEdGraphPin* PrimaryInputPin);

	/** Find or create a new pin and set the property data if not already set. */
	UEdGraphPin* FindOrAddAndSetPropertyPinForPropertyData(const TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base>& InPropertyData);

	/** Remove a pin matching the property data, or clear it if it's the only entry. */
	void ClearOrRemovePropertyPinForPropertyData(const TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base>& InPropertyData);
	
	/** Retrieve the primary input pin. */
	UEdGraphPin* GetPrimaryInputPin() const;
	
	/** Checks if a pin is the primary input pin. */
	static bool IsPrimaryInputPin(const UEdGraphPin* Pin);

	/** Checks if this node has any proxy outputs (other state variables). */
	bool IsWritingToProxyOutput() const;

	/** Find pins where this node is writing to the owning blueprint variables. */
	SMSYSTEMEDITOR_API TArray<TTuple<UEdGraphPin*,
	TSharedPtr<LD::Editor::PropertyUtils::IO::FOwningBlueprintPropertyData>>> GetOwningBlueprintOutputPins() const;

	/** Total number of user outputs. */
	int32 GetNumOutputs() const { return NumOutputs; }

	/** Return array of property output pins. */
	SMSYSTEMEDITOR_API TArray<UEdGraphPin*> GetAllPropertyOutputs() const;
	
protected:
	/** Create a new connector pin and return the result and increment total count. */
	UEdGraphPin* CreateConnectorPin();
	/** Add an output pin and a matching input pin. */
	UEdGraphPin* AddConnectorAndSelectionPinsImpl(int32 InIndex);
	/** Check if we need an available pin and create one. */
	void TryCreateAvailablePin();

	FName CreateInputPinName(int32 InNameIndex) const;
	FName CreateOutputPinName(int32 InNameIndex) const;
	static FName CreatePrimaryInputPinName();

private:
	/**
	 * Number of outputs this node will write to. The IK2Node_AddPinInterface interface refers to this as inputs,
	 * but we actually add outputs, which will then create inputs depending on their type.
	 */
	UPROPERTY()
	int32 NumOutputs = 1;
};
