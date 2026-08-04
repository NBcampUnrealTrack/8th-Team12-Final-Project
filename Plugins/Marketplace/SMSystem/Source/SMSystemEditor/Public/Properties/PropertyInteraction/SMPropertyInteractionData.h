// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class USMGraphNode_StateNodeBase;

/** Method for interacting with graph properties. */
enum class ESMPropertyInteractionType : uint8
{
	None                        = 0,
	SetHover                    = 1 << 0,
	SetSelect                   = 1 << 1,
	UnsetHover                  = 1 << 2,
	UnsetSelect                 = 1 << 3,
	SetDisplayInputConnector    = 1 << 4,
	UnsetDisplayInputConnector  = 1 << 5,
	SetDisplayOutputConnector   = 1 << 6,
	UnsetDisplayOutputConnector = 1 << 7,
};
ENUM_CLASS_FLAGS(ESMPropertyInteractionType)

struct FSMPropertyInteractionData
{
	FSMPropertyInteractionData() : bHovered(false), bSelected(false), bForceDisplayInputConnectors(false),
								   bForceDisplayOutputConnectors(false), bExpandCategory(false), bInteractionGuard(false)
	{
	}

	/** Property is hovered. */
	uint8 bHovered: 1;
	/** Property is selected. */
	uint8 bSelected: 1;
	/** If input connectors should be displayed. */
	uint8 bForceDisplayInputConnectors: 1;
	/** If output connectors should be displayed. */
	uint8 bForceDisplayOutputConnectors: 1;
	/** If the owning category should be expanded. */
	uint8 bExpandCategory: 1;
	/** True if interacting, used to prevent recursive interaction. */
	uint8 bInteractionGuard: 1;

	/** True if this property was selected externally by another manager. */
	TWeakObjectPtr<USMGraphNode_StateNodeBase> ExternalSelector;

	/** If an external manager has selected us. */
	bool IsSelectedExternally() const
	{
		return ExternalSelector.IsValid();
	}
	
	/** If any value here implies an interaction is present. */
	bool HasInteraction() const
	{
		return bHovered || bSelected || bForceDisplayInputConnectors || bForceDisplayOutputConnectors;
	}
	
	bool operator==(const FSMPropertyInteractionData& PropertyData) const
	{
		return bHovered == PropertyData.bHovered &&
			bSelected == PropertyData.bSelected &&
				bForceDisplayInputConnectors == PropertyData.bForceDisplayInputConnectors &&
					bForceDisplayOutputConnectors == PropertyData.bForceDisplayOutputConnectors &&
						ExternalSelector == PropertyData.ExternalSelector;
	}
};