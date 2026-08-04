// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Nodes/States/SMStateInstance.h"

#include "SMPinMigrationTestClasses.generated.h"

/**
 * State with a variety of exposed property types. Used by SMPinMigration.spec.cpp
 * to drive the orphan-pin data migration path on a rename. Each property gets its own
 * property graph with a single result pin, so the spec can mutate them independently.
 */
UCLASS(Blueprintable)
class USMPinMigrationTestState : public USMStateInstance
{
public:
	GENERATED_BODY()

	USMPinMigrationTestState();

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	int32 IntVar;

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	float FloatVar;

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	bool bBoolVar;

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	FString StringVar;

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	FName NameVar;

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	FVector VectorVar;

	UPROPERTY(BlueprintReadWrite, Category = "Test")
	TObjectPtr<UObject> ObjectVar;

	/** Single-element array, constructed with one entry. */
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	TArray<int32> IntArrayOne;

	/** Three-element array, constructed with three entries. */
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	TArray<int32> IntArrayThree;

	/** Five-element array of floats, constructed with five entries. */
	UPROPERTY(BlueprintReadWrite, Category = "Test")
	TArray<float> FloatArrayFive;
};
