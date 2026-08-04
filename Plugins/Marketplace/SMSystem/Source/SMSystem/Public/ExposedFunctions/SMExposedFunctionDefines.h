// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "ExposedFunctions/SMExposedFunctionHelpers.h"

#define INITIALIZE_NODE_FUNCTION_HANDLER() \
	if (FSMExposedNodeFunctions* ExposedNodeFunctions = LD::ExposedFunctions::FindExposedNodeFunctions(this)) \
	{ \
		check(ExposedNodeFunctions->LOGICDRIVER_FUNCTION_HANDLER_TYPE.Num() > 0); \
		const auto& LocalRuntimeData = GetRuntimeData(); \
		LocalRuntimeData->FunctionHandlers = &ExposedNodeFunctions->LOGICDRIVER_FUNCTION_HANDLER_TYPE[0]; \
		LocalRuntimeData->FunctionHandlers->ExposedFunctionsOwner = ExposedNodeFunctions; \
	}

#define INITIALIZE_EXPOSED_FUNCTIONS(Handler) \
	if (FSMNode_FunctionHandlers* FunctionHandlers = GetFunctionHandlers()) \
	{ \
		LD::ExposedFunctions::InitializeGraphFunctions(*(&static_cast<LOGICDRIVER_FUNCTION_HANDLER_TYPE*>(FunctionHandlers)->Handler), GetOwningInstance(), GetNodeInstance()); \
	} \

#define EXECUTE_EXPOSED_FUNCTIONS(Handler, ...) \
	if (FSMNode_FunctionHandlers* FunctionHandlers = GetFunctionHandlers()) \
	{ \
		LD::ExposedFunctions::ExecuteGraphFunctions(*&static_cast<LOGICDRIVER_FUNCTION_HANDLER_TYPE*>(FunctionHandlers)->Handler, GetOwningInstance(), GetNodeInstance(), ##__VA_ARGS__); \
	} \
