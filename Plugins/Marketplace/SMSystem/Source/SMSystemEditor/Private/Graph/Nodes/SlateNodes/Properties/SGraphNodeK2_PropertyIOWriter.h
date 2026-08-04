// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "KismetNodes/SGraphNodeK2Base.h"

class UK2Node;

/**
 * Custom slate node for the IO writer. Recognizes IK2Node_AddPinInterface.
 */
class SGraphNodeK2_PropertyIOWriter : public SGraphNodeK2Base
{
	SLATE_BEGIN_ARGS(SGraphNodeK2_PropertyIOWriter) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Node* InNode);
	
protected:
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	
	virtual void CreateOutputSideAddButton(TSharedPtr<SVerticalBox> OutputBox) override;
	virtual FReply OnAddPin() override;
	virtual EVisibility IsAddPinButtonVisible() const override;
};