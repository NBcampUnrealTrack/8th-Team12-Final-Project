// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

namespace LD::Editor::Graph::Transitions
{
	/** The direction of the transition. */
	enum ETransitionDirection : uint8
	{
		/** Previous and Next directions. */
		Both,
		/** Look behind only. */
		Previous,
		/** Look forward only. */
		Next
	};

	/** Arguments for gathering reroute data. */
	struct FGatherRerouteArgs
	{
		/** The direction along the transition to gather reroute nodes. */
		ETransitionDirection Direction = ETransitionDirection::Both;
		/** Include this object in the results. */
		bool bIncludeSelf = true;
	};
}