// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "K2Node.h"

#include "SMGraphK2Node_Base.generated.h"

#define STATE_MACHINE_HELPER_CATEGORY TEXT("Logic Driver|Local Graph Nodes")
#define STATE_MACHINE_INSTANCE_CALL_CATEGORY TEXT("Logic Driver|Local Graph Nodes|Instance Functions")

UCLASS()
class SMSYSTEMEDITOR_API USMGraphK2Node_Base : public UK2Node
{
	GENERATED_UCLASS_BODY()

public:
	// UK2Node
	virtual ERedirectType DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	// ~UK2Node
	
	virtual UEdGraphPin* GetInputPin() const;
	virtual UEdGraphPin* GetOutputPin() const;
	virtual UEdGraphNode* GetOutputNode() const;
	virtual UEdGraphPin* GetThenPin() const;
	
	/** In case validation needs to occur before being moved to the consolidated event graph. */
	virtual void PreConsolidatedEventGraphValidate(FCompilerResultsLog& MessageLog) {}
	/**
	 * Called during pre-compile phase. This occurs during the same stage as OnPreCompileValidate, but after
	 * custom user validation has run.
	 */
	virtual void PreCompileValidate(FCompilerResultsLog& MessageLog) {}
	/** Called during post-compile phase. */
	virtual void PostCompileValidate(FCompilerResultsLog& MessageLog) {}
	
	/** Restrict all collapse options from showing up in the context menu. */
	virtual bool CanCollapseNode() const { return true; }
	/** Restricts just function and macro context options. */
	virtual bool CanCollapseToFunctionOrMacro() const { return true; }
};
