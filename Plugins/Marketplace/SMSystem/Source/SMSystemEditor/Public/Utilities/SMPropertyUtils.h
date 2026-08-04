// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Properties/SMEditorPropertyUtils.h"

namespace UE_DEPRECATED(5.5, "Include Properties/SMEditorPropertyUtils.h instead of Utilities/SMPropertyUtils.h.") LD
{
	namespace UE_DEPRECATED(5.5, "Use LD::Editor::PropertyUtils. Include Properties/SMEditorPropertyUtils.h.") PropertyUtils
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		using namespace LD::Editor::PropertyUtils;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}