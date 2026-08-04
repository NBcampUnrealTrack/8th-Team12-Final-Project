// Copyright Recursoft LLC. All Rights Reserved.

#include "SMTextGraphProperty.h"

FSMTextGraphProperty::FSMTextGraphProperty() : Super()
{
#if WITH_EDITORONLY_DATA
	GraphModuleClassName = "SMExtendedEditor";
	GraphClassName = "SMTextPropertyGraph";
	GraphSchemaClassName = "SMTextPropertyGraphSchema";
#endif
}
