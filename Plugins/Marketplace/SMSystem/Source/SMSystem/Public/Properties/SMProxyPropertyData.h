// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/PropertyBag.h"

#include "SMProxyPropertyData.generated.h"

/** Represents a path to a proxy property. */
USTRUCT(BlueprintInternalUseOnly)
struct FSMProxyPropertyPath
{
	GENERATED_BODY()

	/** PathGuid of the node containing the property. */
	UPROPERTY(BlueprintReadWrite, Category = Default)
	FGuid PathGuid;

	/** A template guid, such as for a stack instance. */
	UPROPERTY(BlueprintReadWrite, Category = Default)
	FGuid TemplateGuid;

	/** The name of the property. */
	UPROPERTY(BlueprintReadWrite, Category = Default)
	FName PropertyName;

	/** Array index of the property. */
	UPROPERTY(BlueprintReadWrite, Category = Default)
	int32 ArrayIndex = INDEX_NONE;
};

/** Proxy property data. */
USTRUCT(BlueprintInternalUseOnly)
struct FSMProxyPropertyData
{
	GENERATED_BODY()

	/** Instanced properties. */
	UPROPERTY()
	FInstancedPropertyBag PropertyBag;
};

/** Proxy properties within a node. */
USTRUCT(BlueprintInternalUseOnly)
struct FSMNodeProxyPropertyData
{
	GENERATED_BODY()

	/** Stack instances (template guid) to properties. */
	UPROPERTY()
	TMap<FGuid, FSMProxyPropertyData> TemplateProperties;
};

/** Proxy properties for storage on an instance. */
USTRUCT(BlueprintInternalUseOnly)
struct FSMInstanceProxyPropertyData
{
	GENERATED_BODY()

	/** PathGuid to stored node properties. */
	UPROPERTY()
	TMap<FGuid, FSMNodeProxyPropertyData> NodeProperties;

	/** Attempt to add proxy property data. */
	FORCEINLINE FSMProxyPropertyData* FindOrAddProxyPropertyData(const FSMProxyPropertyPath& InArgs)
	{
		FSMNodeProxyPropertyData& NodePropertyData = NodeProperties.FindOrAdd(InArgs.PathGuid);
		return &NodePropertyData.TemplateProperties.FindOrAdd(InArgs.PathGuid);
	}

	/** Find proxy property data. */
	FORCEINLINE FSMProxyPropertyData* FindProxyPropertyData(const FSMProxyPropertyPath& InArgs)
	{
		if (FSMNodeProxyPropertyData* NodePropertyData = NodeProperties.Find(InArgs.PathGuid))
		{
			return NodePropertyData->TemplateProperties.Find(InArgs.PathGuid);
		}
		return nullptr;
	}
};
