// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

struct FSMSystemInfo;

namespace LD::Support
{
	TSharedPtr<FSMSystemInfo> GenerateSystemInfo();
}
