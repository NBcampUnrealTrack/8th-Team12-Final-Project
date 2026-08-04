// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Misc/Guid.h"
#include "UObject/ObjectPtr.h"

#include "SMReplicationContainers.generated.h"

class USMNodeInstance;
class USMInstance;

USTRUCT()
struct FSMReplicationContainer
{
	GENERATED_BODY()
	
	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	FGuid PathGuid;
};

USTRUCT()
struct FSMReferenceContainer : public FSMReplicationContainer
{
	GENERATED_BODY()

	FSMReferenceContainer() : Reference(nullptr) {}
	
	UPROPERTY()
	TObjectPtr<USMInstance> Reference;
};

USTRUCT()
struct FSMNodeStackInstanceContainer
{
	GENERATED_BODY()
	
	/** The index of this node instance. 0 is the primary node instance. All stack instances are offset by 1. */
	UPROPERTY()
	uint8 StackIndex = 0;
	
	UPROPERTY()
	TObjectPtr<USMNodeInstance> NodeInstance;
};

USTRUCT()
struct FSMNodeInstanceContainer : public FSMReplicationContainer
{
	GENERATED_BODY()
	
	/** All instances, including the main node instance and any stack instances.*/
	UPROPERTY()
	TArray<FSMNodeStackInstanceContainer> StackInstances;
};