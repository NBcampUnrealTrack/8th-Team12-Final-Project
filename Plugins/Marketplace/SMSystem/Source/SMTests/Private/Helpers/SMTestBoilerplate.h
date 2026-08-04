// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Helpers/SMTestHelpers.h"

#include "Graph/SMGraph.h"
#include "Graph/Nodes/SMGraphK2Node_StateMachineNode.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "Blueprints/SMBlueprint.h"

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

static bool SetupTest(FAutomationTestBase* InTest, FAssetHandler& OutNewAsset, USMBlueprint** OutNewBP,
	USMGraphK2Node_StateMachineNode** OutRootStateMachineNode, USMGraph** OutStateMachineGraph,
	const TSubclassOf<USMInstance>& StateMachineParentClass = nullptr, const TSubclassOf<USMInstance>& InTemplateClass = nullptr)
{
	if (InTemplateClass)
	{
		if (!TestHelpers::TryCreateNewStateMachineBlueprintAssetFromNativeTemplate(InTest,
			InTemplateClass, StateMachineParentClass, OutNewAsset, false))
		{
			return false;
		}
	}
	else if (!TestHelpers::TryCreateNewStateMachineAsset(InTest, OutNewAsset, false, StateMachineParentClass))
	{
		return false;
	}
	*OutNewBP = OutNewAsset.GetObjectAs<USMBlueprint>();
	*OutRootStateMachineNode = FSMBlueprintEditorUtils::GetRootStateMachineNode(*OutNewBP);
	*OutStateMachineGraph = (*OutRootStateMachineNode)->GetStateMachineGraph();
	return true;
}

/**
 * Additional arguments for the SETUP_NEW_STATE_MACHINE macros to avoid multiple macro definitions.
 * Anything here should be reset at the end of FSMTestScopeHelper.
 */
namespace LD::Tests::Setup
{
	/** The parent class to use when creating a new state machine.  */
	inline TSubclassOf<USMInstance> ParentAssetClass = nullptr;

	/** A template to use for creating a blueprint only class. */
	inline TSubclassOf<USMInstance> BlueprintTemplateClass = nullptr;
}

/** Run for duration of a test. */
struct FSMTestScopeHelper
{
	FSMTestScopeHelper()
	{
	}

	~FSMTestScopeHelper()
	{
		Reset();
	}

	void Start()
	{
		FSMNode_Base::bValidateGuids = true;
	}

	void Reset()
	{
		FSMNode_Base::bValidateGuids = false;
		LD::Tests::Setup::ParentAssetClass = nullptr;
		LD::Tests::Setup::BlueprintTemplateClass = nullptr;
	}
};

#define SETUP_NEW_STATE_MACHINE_FOR_TEST_NO_STATES() \
FSMTestScopeHelper SMTestScopeHelper; \
SMTestScopeHelper.Start();\
FAssetHandler NewAsset; \
USMBlueprint* NewBP = nullptr; \
USMGraphK2Node_StateMachineNode* RootStateMachineNode = nullptr; \
USMGraph* StateMachineGraph = nullptr; \
if (!SetupTest(this, NewAsset, &NewBP, &RootStateMachineNode, &StateMachineGraph, LD::Tests::Setup::ParentAssetClass)) \
{ \
	return false; \
} \

#define SETUP_NEW_STATE_MACHINE_FOR_TEST(num_states) \
SETUP_NEW_STATE_MACHINE_FOR_TEST_NO_STATES() \
int32 TotalStates = num_states; \

#define SETUP_SPEC_TEST_DEFINES() \
FSMTestScopeHelper SMTestScopeHelper; \
FAssetHandler NewAsset; \
USMBlueprint* NewBP; \
USMGraphK2Node_StateMachineNode* RootStateMachineNode; \
USMGraph* StateMachineGraph; \
int32 TotalStates; \

#define SPEC_TEST_BEFORE_EACH() \
SMTestScopeHelper.Start();

#define SPEC_TEST_AFTER_EACH() \
SMTestScopeHelper.Reset();

#define SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST_NO_STATES() \
NewBP = nullptr; \
RootStateMachineNode = nullptr; \
StateMachineGraph = nullptr; \
check(SetupTest(this, NewAsset, &NewBP, &RootStateMachineNode, &StateMachineGraph, LD::Tests::Setup::ParentAssetClass, \
	LD::Tests::Setup::BlueprintTemplateClass));

#define SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(num_states) \
SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST_NO_STATES() \
TotalStates = num_states; \

#endif
