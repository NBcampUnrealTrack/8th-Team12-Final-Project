// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatform.h"
#include "Misc/EnumClassFlags.h"

#include "SMGraphPropertyFlags.generated.h"

/** Graph property configuration flags. */
UENUM()
enum class ESMGraphPropertyFlags : uint8
{
	None = 0,
	/** If only a default value is assigned (no variable connected). */
	DefaultValueOnly = 1 << 0,
	/** The variable supports input connections. */
	Input = 1 << 1,
	/** The variable supports output connections. */
	Output = 1 << 2,
};
ENUM_CLASS_FLAGS(ESMGraphPropertyFlags)

/** The direction of the graph property. */
UENUM()
enum class ESMGraphPropertyDirection : uint8
{
	/** Graph property accepts input. */
	Input,
	/** Graph property writes to output. */
	Output,
	/** Graph property accepts input and output. */
	Both
};

/** Evaluation flags for determining when graph properties should evaluate. */
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ESMGraphPropertyEvalFlags : uint8
{
	/** Should graph properties evaluate when initializing or on state start. */
	OnStart = 1 << 0,
	/** Should graph properties evaluate on state update. */
	OnUpdate = 1 << 1,
	/** Should graph properties evaluate on state end. */
	OnEnd = 1 << 2,
	/** Should graph properties evaluate on root state machine start. */
	OnRootStateMachineStart = 1 << 3,
	/** Should graph properties evaluate on root state machine end. */
	OnRootStateMachineStop = 1 << 4,
	/** Should graph properties evaluate when transitions are initializing. */
	OnTransitionInitialize = 1 << 5,
	/** Should graph properties evaluate when transitions are evaluating. */
	OnTransitionEval = 1 << 6
};
ENUM_CLASS_FLAGS(ESMGraphPropertyEvalFlags)
