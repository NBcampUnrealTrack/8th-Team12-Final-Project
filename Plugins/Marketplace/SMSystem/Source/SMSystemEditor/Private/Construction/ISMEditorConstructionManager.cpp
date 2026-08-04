// Copyright Recursoft LLC. All Rights Reserved.

#include "Construction/ISMEditorConstructionManager.h"

#include "SMEditorConstructionManager.h"

ISMEditorConstructionManager& ISMEditorConstructionManager::Get()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return *FSMEditorConstructionManager::GetInstance();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
