// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMInputTypes.generated.h"

UENUM()
namespace ESMStateMachineInput
{
	enum Type : uint8
	{
		Disabled                = 0,
		/** Use the controller assigned to the context if one is available. */
		UseContextController    = 1,
		/** Use the controller assigned via USMInstance::SetManualInputController(). */
		UseManualController     = 10,
		Player0	                = 2,
		Player1	                = 3,
		Player2	                = 4,
		Player3	                = 5,
		Player4	                = 6,
		Player5	                = 7,
		Player6	                = 8,
		Player7	                = 9,
	};
}

UENUM()
namespace ESMNodeInput
{
	enum Type : uint8
	{
		Disabled,
		/** All input values are determined by the owning state machine. */
		UseOwningStateMachine,
		/** Use the controller assigned to the context if one is available. */
		UseContextController,
		Player0,
		Player1,
		Player2,
		Player3,
		Player4,
		Player5,
		Player6,
		Player7,
	};
}