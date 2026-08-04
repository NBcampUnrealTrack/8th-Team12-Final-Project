// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMGraphK2Node_PropertyIOBase.h"
#include "Widgets/Kismet/SMGraphK2Node_LinearExpressionInterface.h"

#include "SMGraphK2Node_PropertyIOReaderNode.generated.h"

class USMGraphK2Node_PropertyNode_Base;

/**
 * Reads output from state properties.
 */
UCLASS()
class USMGraphK2Node_PropertyIOReaderNode : public USMGraphK2Node_PropertyIOBase, public ISMGraphK2Node_LinearExpressionInterface
{
	GENERATED_BODY()

public:
	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual bool IsCompatibleWithGraph(UEdGraph const* Graph) const override;
	virtual bool IsActionFilteredOut(FBlueprintActionFilter const& Filter) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual bool CanUserDeleteNode() const override { return true; }
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	// ~UEdGraphNode

	// UK2Node
	virtual bool IsNodePure() const override { return true; }
	// ~UK2Node

	// USMGraphK2Node_Base
	virtual void PreCompileValidate(FCompilerResultsLog& MessageLog) override;
	// ~USMGraphK2Node_Base
	
	// USMGraphK2Node_RuntimeNodeReference
	virtual bool HandlesOwnExpansion() const override { return true; }
	virtual void CustomExpandNode(FSMKismetCompilerContext& CompilerContext, USMGraphK2Node_RuntimeNodeContainer* RuntimeNodeContainer, FProperty* NodeProperty) override;
	// ~USMGraphK2Node_RuntimeNodeReference

	// USMGraphK2Node_PropertyIOBase
	virtual bool IsConnectedToOwningPropertyNode() const override;
	virtual UEdGraphPin* TryGetOwningPropertyNodeRelatedPin() const override;
	virtual bool IsPropertySelectionPin(const UEdGraphPin* Pin) const override;
	virtual bool IsPropertyConnectorPin(const UEdGraphPin* Pin) const override;
	virtual bool CanDisplayConnectorPin(const TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base>& InPropertyData, const UEdGraphPin* InPin) const override;
	virtual bool ShouldBreakResultPinLinksOnTypeChange() const override { return true; }
	virtual bool CanAcceptConnection(const USMGraphK2Node_PropertyIOBase* OtherPropertyIONode) const override;
	virtual bool CanAcceptConnection(const USMGraphK2Node_PropertyNode_Base* OtherPropertyNode) const override;
protected:
	virtual int32 GetMaxReferencedIONodes() const override { return 1; }
	// ~USMGraphK2Node_PropertyIOBase

public:
	// ISMGraphK2Node_LinearExpressionInterface
	virtual TSharedPtr<SWidget> MakeLinearExpressionNodeWidget() const override;
	virtual TSharedPtr<SWidget> MakeLinearExpressionPinWidget(const UEdGraphPin* InPin) const override;
	virtual bool DisplayParenthesis() const override { return false; }
	// ~ISMGraphK2Node_LinearExpressionInterface

	/** Get the property data of the selected property. */
	SMSYSTEMEDITOR_API TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base> GetSelectedNodePropertyData() const;

	SMSYSTEMEDITOR_API UEdGraphPin* GetPropertyPinIn() const;
	SMSYSTEMEDITOR_API UEdGraphPin* GetPropertyPinInChecked() const;

	SMSYSTEMEDITOR_API UEdGraphPin* GetConnectorPinOut() const;
	SMSYSTEMEDITOR_API UEdGraphPin* GetConnectorPinOutChecked() const;

	static FName CreatePropertyPinInName();
	static FName CreateConnectorPinOutName();
};
