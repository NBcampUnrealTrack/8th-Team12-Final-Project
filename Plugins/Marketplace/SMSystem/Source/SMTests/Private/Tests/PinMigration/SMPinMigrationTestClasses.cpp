// Copyright Recursoft LLC. All Rights Reserved.

#include "SMPinMigrationTestClasses.h"

USMPinMigrationTestState::USMPinMigrationTestState()
	: IntVar(0)
	, FloatVar(0.0f)
	, bBoolVar(false)
	, VectorVar(FVector::ZeroVector)
	, ObjectVar(nullptr)
{
	IntArrayOne.AddDefaulted(1);
	IntArrayThree.AddDefaulted(3);
	FloatArrayFive.AddDefaulted(5);
}
