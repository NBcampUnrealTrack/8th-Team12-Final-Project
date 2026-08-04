// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Nodes/States/SMConduitInstance.h"
#include "Nodes/States/SMStateInstance.h"
#include "Nodes/States/SMStateMachineInstance.h"
#include "Properties/SMTextGraphProperty.h"
#include "SMInstance.h"

#include "SMOutputVariablesTestClasses.generated.h"

UCLASS(Blueprintable)
class USMOutputVariablesStateInstance_Base : public USMStateInstance
{
	GENERATED_BODY()

	virtual void OnStateBegin_Implementation() override
	{
		// Allows test to set output properties after input is read, but before output is written.
		OnTestStateBeginEvent.ExecuteIfBound();
	}

public:
	DECLARE_DELEGATE(FOnOutputStateBegin);
	FOnOutputStateBegin OnTestStateBeginEvent;
};

UCLASS(Blueprintable)
class USMOutputVariablesStateInstance : public USMOutputVariablesStateInstance_Base
{
public:
	GENERATED_BODY()

	USMOutputVariablesStateInstance(): InputInt(0), OutputInt(0), BothInt(0), InputFloat(0), OutputFloat(0),
	                                   InputDouble(0),
	                                   OutputDouble(0)
	{
		InputOutputIntArray.AddDefaulted();
		InputOutputIntArray.AddDefaulted();
	}

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	int32 InputInt;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	int32 OutputInt;
	// Tests "Both"
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Both"))
	int32 BothInt;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	FString InputStr;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	FString OutputStr;
	// Tests "Input,Output" instead of "Both"
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input,Output"))
	FString InputOutputStr;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	float InputFloat;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	float OutputFloat;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	double InputDouble;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	double OutputDouble;
	
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	FText InputText;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	FText OutputText;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Both"))
	TArray<int32> InputOutputIntArray;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	FTransform InputTransform;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	FTransform OutputTransform;

	UPROPERTY()
	FSMTextGraphProperty TextGraph;
};

UCLASS(Blueprintable)
class USMOutputVariablesStateInstance_NO_OUTPUT : public USMOutputVariablesStateInstance_Base
{
public:
	GENERATED_BODY()

	USMOutputVariablesStateInstance_NO_OUTPUT(): InputInt(0), OutputInt(0), BothInt(0), InputFloat(0), OutputFloat(0),
									   InputDouble(0),
									   OutputDouble(0)
	{
		InputOutputIntArray.AddDefaulted();
		InputOutputIntArray.AddDefaulted();
	}

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	int32 InputInt;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	int32 OutputInt;
	// Tests "Both"
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	int32 BothInt;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	FString InputStr;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	FString OutputStr;
	// Tests "Input,Output" instead of "Both"
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	FString InputOutputStr;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	float InputFloat;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	float OutputFloat;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	double InputDouble;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	double OutputDouble;
	
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	FText InputText;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	FText OutputText;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	TArray<int32> InputOutputIntArray;
	
	UPROPERTY()
	FSMTextGraphProperty TextGraph;
};

UCLASS(Blueprintable)
class USMOutputVariablesSMInstance : public USMInstance
{
public:
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	int32 IntVariable = 0;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	float FloatVariable = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	FString StringVariable;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests")
	FText TextVariable;
};

UCLASS(Blueprintable)
class USMOutputVariablesConduitInstance : public USMConduitInstance
{
public:
	GENERATED_BODY()

	USMOutputVariablesConduitInstance(): InputInt(0), OutputInt(0), BothInt(0), InputFloat(0), OutputFloat(0),
	                                    InputDouble(0), OutputDouble(0)
	{
		InputOutputIntArray.AddDefaulted();
		InputOutputIntArray.AddDefaulted();
	}

	virtual bool CanEnterTransition_Implementation() const override
	{
		return true;
	}

	virtual void OnConduitEntered_Implementation() override
	{
		OnTestConduitEnteredEvent.ExecuteIfBound();
	}

	DECLARE_DELEGATE(FOnOutputConduitEntered);
	FOnOutputConduitEntered OnTestConduitEnteredEvent;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	int32 InputInt;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	int32 OutputInt;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Both"))
	int32 BothInt;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	FString InputStr;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	FString OutputStr;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input,Output"))
	FString InputOutputStr;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	float InputFloat;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	float OutputFloat;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	double InputDouble;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	double OutputDouble;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	FText InputText;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	FText OutputText;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Both"))
	TArray<int32> InputOutputIntArray;

	UPROPERTY()
	FSMTextGraphProperty TextGraph;
};

UCLASS(Blueprintable)
class USMOutputVariablesStateMachineInstance : public USMStateMachineInstance
{
public:
	GENERATED_BODY()

	USMOutputVariablesStateMachineInstance(): InputInt(0), OutputInt(0), BothInt(0), InputFloat(0), OutputFloat(0),
	                                         InputDouble(0), OutputDouble(0)
	{
		InputOutputIntArray.AddDefaulted();
		InputOutputIntArray.AddDefaulted();
	}

	virtual void OnStateBegin_Implementation() override
	{
		OnTestStateBeginEvent.ExecuteIfBound();
	}

	DECLARE_DELEGATE(FOnOutputStateBegin);
	FOnOutputStateBegin OnTestStateBeginEvent;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	int32 InputInt;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	int32 OutputInt;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Both"))
	int32 BothInt;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	FString InputStr;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	FString OutputStr;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input,Output"))
	FString InputOutputStr;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	float InputFloat;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	float OutputFloat;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	double InputDouble;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	double OutputDouble;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Input"))
	FText InputText;
	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Output"))
	FText OutputText;

	UPROPERTY(BlueprintReadWrite, Category = "State Machine Tests", meta = (LD_PropertyDirection = "Both"))
	TArray<int32> InputOutputIntArray;

	UPROPERTY()
	FSMTextGraphProperty TextGraph;
};