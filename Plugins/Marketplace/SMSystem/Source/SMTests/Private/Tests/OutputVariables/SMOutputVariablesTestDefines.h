// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

// Build our state machine given a state class.
#define OUR_SPEC_TEST_BEFORE_EACH_BUILD_SM(StateClass) \
    UEdGraphPin* LastStatePin = nullptr; \
    TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin, StateClass); \
    StateNodes.Empty(); \
    FSMBlueprintEditorUtils::GetAllNodesOfClassNested(NewBP, StateNodes); \
    check(StateNodes.Num() == TotalStates);

#define CONNECT_BP_PROPS_BASE(StateOutputPropName, InstancePropName, bExpected, ArrayIdx, TemplateIdx) \
    const bool bOK = ConnectProps( \
        GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName), \
        GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName), \
        0, \
        bExpected, \
		ArrayIdx, \
		TemplateIdx); \
    TestEqual(TEXT("Connection result"), bOK, bExpected);

// Create and test owning bp connection.
#define CONNECT_BP_PROPS(StateOutputPropName, InstancePropName, bExpected) \
		CONNECT_BP_PROPS_BASE(StateOutputPropName, InstancePropName, bExpected, 0, INDEX_NONE)

// Full IT run testing owning bp connection.
#define IT_CONNECT_BP_PROPS(StateOutputPropName, InstancePropName, bExpected) \
    It(FString::Printf(TEXT("connect %s→%s should %s"), \
    TEXT(#StateOutputPropName), TEXT(#InstancePropName), (bExpected) ? TEXT("succeed") : TEXT("fail")), \
    [this]() \
    { \
        CONNECT_BP_PROPS(StateOutputPropName, InstancePropName, bExpected) \
    })

// Full IT test owning bp connection and runtime.
#define IT_CONNECT_BP_PROPS_TEST_RUNTIME(StateOutputPropName, InstancePropName, OutputValue, bExpected) \
    It(FString::Printf(TEXT("connect %s→%s should %s"), \
    TEXT(#StateOutputPropName), TEXT(#InstancePropName), (bExpected) ? TEXT("succeed") : TEXT("fail")), \
    [this]() \
    { \
        CONNECT_BP_PROPS(StateOutputPropName, InstancePropName, bExpected) \
        if (bOK) \
        { \
            const FString StrValue = TestHelpers::ConvertToString(OutputValue); \
            TestOwningBPConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName), \
            GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName), StrValue); \
        } \
    })

#define IT_CONNECT_BP_PROPS_TEST_RUNTIME_STATE_STACK(StateOutputPropName, InstancePropName, OutputValue, bExpected, TemplateIdx) \
    It(FString::Printf(TEXT("connect %s%s→%s should %s"), \
    TEXT(#TemplateIdx), TEXT(#StateOutputPropName), TEXT(#InstancePropName), (bExpected) ? TEXT("succeed") : TEXT("fail")), \
    [this]() \
    { \
        CONNECT_BP_PROPS_BASE(StateOutputPropName, InstancePropName, bExpected, 0, TemplateIdx) \
        if (bOK) \
        { \
            const FString StrValue = TestHelpers::ConvertToString(OutputValue); \
            TestOwningBPConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName), \
            GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName), StrValue, TemplateIdx); \
        } \
    })

// Full IT test two owning bp connections and runtime.
#define IT_CONNECT_TWO_BP_PROPS_TEST_RUNTIME(StateOutputPropName1, InstancePropName1, OutputValue1, StateOutputPropName2, InstancePropName2, OutputValue2, bExpected) \
    It(FString::Printf(TEXT("connect %s→%s and %s→%s should %s"), \
    TEXT(#StateOutputPropName1), TEXT(#InstancePropName1), TEXT(#StateOutputPropName2), TEXT(#InstancePropName2), (bExpected) ? TEXT("succeed") : TEXT("fail")), \
    [this]() \
    { \
        { \
            CONNECT_BP_PROPS(StateOutputPropName1, InstancePropName1, true) \
            TestEqual(TEXT("Connection result 1"), bOK, bExpected); \
        } \
        { \
            CONNECT_BP_PROPS(StateOutputPropName2, InstancePropName2, true) \
            TestEqual(TEXT("Connection result 2"), bOK, bExpected); \
        } \
        { \
            const FString StrValue1 = TestHelpers::ConvertToString(OutputValue1); \
            TestOwningBPConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName1), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName1), StrValue1); \
            const FString StrValue2 = TestHelpers::ConvertToString(OutputValue2); \
            TestOwningBPConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName2), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName2), StrValue2); \
        } \
    })

// Full IT test three owning bp connections and runtime.
#define IT_CONNECT_THREE_BP_PROPS_TEST_RUNTIME(StateOutputPropName1, InstancePropName1, OutputValue1, StateOutputPropName2, InstancePropName2, OutputValue2, StateOutputPropName3, InstancePropName3, OutputValue3, bExpected) \
    It(FString::Printf(TEXT("connect %s→%s, %s→%s and %s→%s should %s"), \
    TEXT(#StateOutputPropName1), TEXT(#InstancePropName1), TEXT(#StateOutputPropName2), TEXT(#InstancePropName2), TEXT(#StateOutputPropName3), TEXT(#InstancePropName3), (bExpected) ? TEXT("succeed") : TEXT("fail")), \
    [this]() \
    { \
        { \
            const bool bOK = ConnectProps( \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName1), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName1), \
                0, \
                bExpected); \
            TestEqual(TEXT("Connection result 1"), bOK, bExpected); \
        } \
        { \
            const bool bOK = ConnectProps( \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName2), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName2), \
                0, \
                bExpected); \
            TestEqual(TEXT("Connection result 2"), bOK, bExpected); \
        } \
        { \
            const bool bOK = ConnectProps( \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName3), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName3), \
                0, \
                bExpected); \
            TestEqual(TEXT("Connection result 3"), bOK, bExpected); \
        } \
        { \
            const FString StrValue1 = TestHelpers::ConvertToString(OutputValue1); \
            TestOwningBPConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName1), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName1), StrValue1); \
            const FString StrValue2 = TestHelpers::ConvertToString(OutputValue2); \
            TestOwningBPConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName2), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName2), StrValue2); \
            const FString StrValue3 = TestHelpers::ConvertToString(OutputValue3); \
            TestOwningBPConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, StateOutputPropName3), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesSMInstance, InstancePropName3), StrValue3); \
        } \
    })

#define CONNECT_STATE_PROPS_ARRAY(SrcIdx, SrcName, SrcArrayIdx, SrcTemplateIdx, DstIdx, DstName, DstArrayIdx, DstTemplateIdx, bExpected) \
    const bool bOK = ConnectProps( \
        SrcIdx, \
        GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, SrcName), \
		SrcArrayIdx, \
		SrcTemplateIdx, \
        DstIdx, \
        GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, DstName), \
        DstArrayIdx, \
        DstTemplateIdx); \
    TestEqual(TEXT("Connection result"), bOK, bExpected);

// Create and test state connection.
#define CONNECT_STATE_PROPS(SrcIdx, SrcName, DstIdx, DstName, bExpected) \
    CONNECT_STATE_PROPS_ARRAY(SrcIdx, SrcName, 0, INDEX_NONE, DstIdx, DstName, 0, INDEX_NONE, bExpected)

// Full IT run testing state connection.
#define IT_CONNECT_STATE_PROPS(SrcIdx, SrcName, DstIdx, DstName, bExpected) \
    It(FString::Printf(TEXT("connect %s→%s should %s"), \
            TEXT(#SrcName), TEXT(#DstName), (bExpected) ? TEXT("succeed") : TEXT("fail")), \
       [this]() \
    { \
        CONNECT_STATE_PROPS(SrcIdx, SrcName, DstIdx, DstName, bExpected) \
    })

#define IT_CONNECT_STATE_PROPS_TEST_RUNTIME_BASE(SrcIdx, SrcName, SrcArrIdx, SrcTemplateIdx, DstIdx, DstName, DstArrIdx, DstTemplateIdx, \
	OutputValue, bExpected, Func, bIsArray, bIsStateStack) \
	It(bIsArray ? FString::Printf( \
		TEXT("connect %s:%s[%s]→%s:%s[%s] should %s"), \
		bIsStateStack ? TEXT(#SrcIdx #SrcTemplateIdx) : TEXT(#SrcIdx), TEXT(#SrcName), TEXT(#SrcArrIdx), bIsStateStack ? TEXT(#DstIdx #DstTemplateIdx) : TEXT(#DstIdx), TEXT(#DstName), \
		TEXT(#DstArrIdx), (bExpected) ? TEXT("succeed") : TEXT("fail")) \
		: FString::Printf( \
		TEXT("connect %s:%s→%s:%s should %s"), \
		bIsStateStack ? TEXT(#SrcIdx #SrcTemplateIdx) : TEXT(#SrcIdx), TEXT(#SrcName), bIsStateStack ? TEXT(#DstIdx #DstTemplateIdx) : TEXT(#DstIdx), TEXT(#DstName), (bExpected) ? TEXT("succeed") : TEXT("fail")), \
	[this]() \
	{ \
		CONNECT_STATE_PROPS_ARRAY(SrcIdx, SrcName, SrcArrIdx, SrcTemplateIdx, DstIdx, DstName, DstArrIdx, DstTemplateIdx, true) \
		if (bOK) \
		{ \
			TFunction<void()> LocalCallback = Func; \
			if (LocalCallback) \
			{ \
				LocalCallback(); \
			} \
			const FString StrValue = TestHelpers::ConvertToString(OutputValue); \
			TestStateConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, SrcName), \
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, DstName), StrValue, SrcIdx, \
				DstIdx, bExpected, SrcArrIdx, DstArrIdx, SrcTemplateIdx, DstTemplateIdx); \
		} \
	})

// Full IT test state connection and runtime. bExpected is used if the runtime output values should match.
#define IT_CONNECT_STATE_PROPS_TEST_RUNTIME(SrcIdx, SrcName, DstIdx, DstName, OutputValue, bExpected) \
    IT_CONNECT_STATE_PROPS_TEST_RUNTIME_BASE(SrcIdx, SrcName, 0, INDEX_NONE, DstIdx, DstName, 0, INDEX_NONE, OutputValue, bExpected, nullptr, false, false)

#define IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK(SrcIdx, SrcName, SrcTemplateIdx, DstIdx, DstName, DstTemplateIdx, OutputValue, bExpected) \
    IT_CONNECT_STATE_PROPS_TEST_RUNTIME_BASE(SrcIdx, SrcName, 0, SrcTemplateIdx, DstIdx, DstName, 0, DstTemplateIdx, OutputValue, bExpected, nullptr, true, true)

#define IT_CONNECT_STATE_PROPS_TEST_RUNTIME_ARRAY(SrcIdx, SrcName, SrcArrayIdx, DstIdx, DstName, DstArrayIdx, OutputValue, bExpected) \
    IT_CONNECT_STATE_PROPS_TEST_RUNTIME_BASE(SrcIdx, SrcName, SrcArrayIdx, INDEX_NONE, DstIdx, DstName, DstArrayIdx, INDEX_NONE, OutputValue, bExpected, nullptr, true, false)

#define IT_CONNECT_STATE_PROPS_TEST_RUNTIME_STATE_STACK_ARRAY(SrcIdx, SrcName, SrcArrayIdx, SrcTemplateIdx, DstIdx, DstName, DstArrayIdx, DstTemplateIdx, OutputValue, bExpected) \
    IT_CONNECT_STATE_PROPS_TEST_RUNTIME_BASE(SrcIdx, SrcName, SrcArrayIdx, SrcTemplateIdx, DstIdx, DstName, DstArrayIdx, DstTemplateIdx, OutputValue, bExpected, nullptr, true, true)

// Full It test with callback prior runtime connection.
#define IT_CONNECT_STATE_PROPS_TEST_RUNTIME_CALLBACK(SrcIdx, SrcName, DstIdx, DstName, OutputValue, bExpected, Func) \
	IT_CONNECT_STATE_PROPS_TEST_RUNTIME_BASE(SrcIdx, SrcName, 0, INDEX_NONE, DstIdx, DstName, 0, INDEX_NONE, OutputValue, bExpected, Func, false, false)

#define IT_CONNECT_TWO_STATE_PROPS_TEST_RUNTIME(SrcIdx, SrcName, DstIdx, DstName, OutputValue, SrcIdx2, SrcName2, DstIdx2, DstName2, OutputValue2) \
    It(FString::Printf(TEXT("connect %s:%s→%s:%s and %s:%s→%s:%s should succeed"), \
     TEXT(#SrcIdx), TEXT(#SrcName), TEXT(#DstIdx), TEXT(#DstName), TEXT(#SrcIdx2), TEXT(#SrcName2), TEXT(#DstIdx2), TEXT(#DstName2)), \
    [this]() \
    { \
        { \
            CONNECT_STATE_PROPS(SrcIdx, SrcName, DstIdx, DstName, true) \
            check (bOK); \
        } \
        { \
            CONNECT_STATE_PROPS(SrcIdx2, SrcName2, DstIdx2, DstName2, true) \
            check (bOK); \
        } \
        { \
            const FString StrValue = TestHelpers::ConvertToString(OutputValue); \
            TestStateConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, SrcName), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, DstName), StrValue, SrcIdx, DstIdx); \
        } \
        { \
            const FString StrValue = TestHelpers::ConvertToString(OutputValue2); \
            TestStateConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, SrcName2), \
                GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, DstName2), StrValue, SrcIdx2, DstIdx2); \
        } \
    })

#define IT_CONNECT_THREE_STATE_PROPS_TEST_RUNTIME(SrcIdx, SrcName, DstIdx, DstName, \
OutputValue, SrcIdx2, SrcName2, DstIdx2, DstName2, OutputValue2, SrcIdx3, SrcName3, DstIdx3, DstName3, OutputValue3) \
	It(FString::Printf(TEXT("connect %s:%s→%s:%s, %s:%s→%s:%s and %s:%s→%s:%s should succeed"), \
	TEXT(#SrcIdx), TEXT(#SrcName), TEXT(#DstIdx), TEXT(#DstName), TEXT(#SrcIdx2), TEXT(#SrcName2), TEXT(#DstIdx2), TEXT(#DstName2), TEXT(#SrcIdx3), TEXT(#SrcName3), TEXT(#DstIdx3), TEXT(#DstName3)), \
	[this]() \
	{ \
		{ \
			CONNECT_STATE_PROPS(SrcIdx, SrcName, DstIdx, DstName, true) \
			check (bOK); \
		} \
		{ \
			CONNECT_STATE_PROPS(SrcIdx2, SrcName2, DstIdx2, DstName2, true) \
			check (bOK); \
		} \
		{ \
			CONNECT_STATE_PROPS(SrcIdx3, SrcName3, DstIdx3, DstName3, true) \
			check (bOK); \
		} \
		{ \
			const FString StrValue = TestHelpers::ConvertToString(OutputValue); \
			TestStateConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, SrcName), \
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, DstName), StrValue, SrcIdx, DstIdx); \
		} \
		{ \
			const FString StrValue = TestHelpers::ConvertToString(OutputValue2); \
			TestStateConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, SrcName2), \
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, DstName2), StrValue, SrcIdx2, DstIdx2); \
		} \
		{ \
			const FString StrValue = TestHelpers::ConvertToString(OutputValue3); \
			TestStateConnectionsRuntime(GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, SrcName3), \
				GET_MEMBER_NAME_CHECKED(USMOutputVariablesStateInstance, DstName3), StrValue, SrcIdx3, DstIdx3); \
		} \
	})
