// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMTransitionEventTypes.generated.h"

UENUM(BlueprintType)
enum ESMDelegateOwner : uint8 
{
	/** This state machine instance. */
	SMDO_This			UMETA(DisplayName = "This"),
	/**
	 * The context object for this state machine.
	 * The class is not known until run-time and needs to be chosen manually.
	 */
	SMDO_Context		UMETA(DisplayName = "Context"),
	/**
	 * The previous state instance. The class is determined by the state.
	 */
	SMDO_PreviousState	UMETA(DisplayName = "Previous State")
};