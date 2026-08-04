// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "SMGraphProperty_Base.h"

namespace LD::PropertyUtils
{
	/** Convert property flags to direction. */
	FORCEINLINE ESMGraphPropertyDirection ConvertFlagsToDirection(const ESMGraphPropertyFlags InFlags)
	{
		switch (static_cast<uint8>(InFlags & (ESMGraphPropertyFlags::Input | ESMGraphPropertyFlags::Output)))
		{
		case static_cast<uint8>(ESMGraphPropertyFlags::Input | ESMGraphPropertyFlags::Output):
			return ESMGraphPropertyDirection::Both;

		case static_cast<uint8>(ESMGraphPropertyFlags::Input):
			return ESMGraphPropertyDirection::Input;

		case static_cast<uint8>(ESMGraphPropertyFlags::Output):
			return ESMGraphPropertyDirection::Output;

		default:
			return ESMGraphPropertyDirection::Both;
		}
	}

	/** Convert a direction to property flags. */
	FORCEINLINE ESMGraphPropertyFlags ConvertDirectionToFlags(const ESMGraphPropertyDirection InDirection)
	{
		switch (InDirection)
		{
		case ESMGraphPropertyDirection::Both:
			return ESMGraphPropertyFlags::Input | ESMGraphPropertyFlags::Output;
		case ESMGraphPropertyDirection::Input:
			return ESMGraphPropertyFlags::Input;
		case ESMGraphPropertyDirection::Output:
			return ESMGraphPropertyFlags::Output;
		}
		return ESMGraphPropertyFlags::None;
	}

	/** Checks if a graph property eval flag contains a given flag. */
	FORCEINLINE bool DoEvalFlagsContainFlag(uint8 InFlags, const ESMGraphPropertyEvalFlags InRequiredEventFlag)
	{
		return EnumHasAnyFlags(static_cast<ESMGraphPropertyEvalFlags>(InFlags), InRequiredEventFlag);
	}

	// TODO refactor graph property related utilities here.
}
