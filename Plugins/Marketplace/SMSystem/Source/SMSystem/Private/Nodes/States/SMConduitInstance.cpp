// Copyright Recursoft LLC. All Rights Reserved.

#include "SMConduitInstance.h"

#include "SMConduit.h"
#include "SMCustomVersion.h"

USMConduitInstance::USMConduitInstance() : Super(),
                                           bEvalWithTransitions(false), bCanEvaluate(true)
{
#if WITH_EDITORONLY_DATA
	bEvalGraphsOnInitialize_DEPRECATED = true;
	bEvalGraphsOnTransitionEval_DEPRECATED = true;
#endif
	
	EnumAddFlags(reinterpret_cast<ESMGraphPropertyEvalFlags&>(InputPropertyEvalFlags),
		ESMGraphPropertyEvalFlags::OnTransitionInitialize | ESMGraphPropertyEvalFlags::OnTransitionEval);
}

void USMConduitInstance::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	const int32 LinkerVersion = GetLinkerCustomVersion(FSMNodeInstanceCustomVersion::GUID);
	if (LinkerVersion < FSMNodeInstanceCustomVersion::OutputVariables)
	{
		ESMGraphPropertyEvalFlags& InputFlagsEnum = reinterpret_cast<ESMGraphPropertyEvalFlags&>(InputPropertyEvalFlags);
		if (bEvalGraphsOnInitialize_DEPRECATED)
		{
			EnumAddFlags(InputFlagsEnum, ESMGraphPropertyEvalFlags::OnTransitionInitialize);
		}
		if (bEvalGraphsOnTransitionEval_DEPRECATED)
		{
			EnumAddFlags(InputFlagsEnum, ESMGraphPropertyEvalFlags::OnTransitionEval);
		}
	}
#endif
}

void USMConduitInstance::SetCanEvaluate(const bool bValue)
{
	SET_NODE_DEFAULT_VALUE(FSMConduit, bCanEvaluate, bValue);
}

bool USMConduitInstance::GetCanEvaluate() const
{
	GET_NODE_DEFAULT_VALUE(FSMConduit, bCanEvaluate);
}

bool USMConduitInstance::GetEvalWithTransitions() const
{
	GET_NODE_DEFAULT_VALUE(FSMConduit, bEvalWithTransitions);
}

void USMConduitInstance::SetEvalWithTransitions(const bool bValue)
{
	SET_NODE_DEFAULT_VALUE(FSMConduit, bEvalWithTransitions, bValue);
}
