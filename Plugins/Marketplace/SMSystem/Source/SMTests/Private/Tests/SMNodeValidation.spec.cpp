// Copyright Recursoft LLC. All Rights Reserved.

#include "Helpers/SMTestBoilerplate.h"
#include "Helpers/SMTestHelpers.h"
#include "SMTestContext.h"

#include "Configuration/SMProjectEditorSettings.h"
#include "Graph/Nodes/SMGraphK2Node_StateMachineNode.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/SMGraph.h"
#include "Utilities/SMBlueprintEditorUtils.h"
#include "Utilities/SMNodeInstanceUtils.h"

#include "Blueprints/SMBlueprint.h"

#include "Kismet2/KismetEditorUtilities.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FNodeInstanceValidationSpec,
                 "LogicDriver.NodeInstance.Validation",
                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
    SETUP_SPEC_TEST_DEFINES()
    USMProjectEditorSettings* Settings;
    ESMEditorConstructionScriptProjectSetting OriginalCSSetting;
    USMTestPreCompileState* NodeTemplate;
END_DEFINE_SPEC(FNodeInstanceValidationSpec)

void FNodeInstanceValidationSpec::Define()
{
    BeforeEach([this]()
    {
        // ——— Setup new state machine ———
    	SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST(1)

        Settings = FSMBlueprintEditorUtils::GetMutableProjectEditorSettings();
        OriginalCSSetting = Settings->EditorNodeConstructionScriptSetting;
        Settings->EditorNodeConstructionScriptSetting = ESMEditorConstructionScriptProjectSetting::SM_Standard;

        UEdGraphPin* LastStatePin = nullptr;
        TestHelpers::BuildLinearStateMachine(this, StateMachineGraph, TotalStates, &LastStatePin, USMTestPreCompileState::StaticClass());
        const USMGraphNode_StateNode* StateNode =
            FSMBlueprintEditorUtils::GetFirstNodeOfClassNested<USMGraphNode_StateNode>(StateMachineGraph);
        NodeTemplate = StateNode->GetNodeTemplateAs<USMTestPreCompileState>();
        check(NodeTemplate);
    	return true;
    });

    Describe("Pre-compile validation logging", [this]()
    {
        It("should report an Error log as expected", [this]()
        {
            NodeTemplate->LogType    = ESMCompilerLogType::Error;
            NodeTemplate->LogMessage = TEXT("An error message!");
            AddExpectedError(NodeTemplate->LogMessage);
            FKismetEditorUtilities::CompileBlueprint(NewBP);
        });

        It("should report a Warning log as expected", [this]()
        {
            NodeTemplate->LogType = ESMCompilerLogType::Warning;
            NodeTemplate->LogMessage = TEXT("A warning message!");
            AddExpectedError(NodeTemplate->LogMessage);
            FKismetEditorUtilities::CompileBlueprint(NewBP);
        });

        It("should not report a Note log as an error", [this]()
        {
            NodeTemplate->LogType    = ESMCompilerLogType::Note;
            NodeTemplate->LogMessage = TEXT("A note message!");
            FKismetEditorUtilities::CompileBlueprint(NewBP);
        });

        It("should skip all validation when validation is disabled", [this]()
        {
            NodeTemplate->LogType    = ESMCompilerLogType::Error;
            NodeTemplate->LogMessage = TEXT("An error message!");
        	
            NewBP->bEnableNodeValidation = false;
            FKismetEditorUtilities::CompileBlueprint(NewBP);
            NewBP->bEnableNodeValidation = true;
        });
    });

    Describe("Property-based validation logging", [this]()
    {
        It("should report an Error when property-logging is enabled", [this]()
        {
            NodeTemplate->bLogProperty       = true;
            NodeTemplate->bLogPropertySilent = false;
            NodeTemplate->LogType            = ESMCompilerLogType::Error;
            NodeTemplate->LogMessage         = TEXT("A Property error message!");
            AddExpectedError(NodeTemplate->LogMessage);
            FKismetEditorUtilities::CompileBlueprint(NewBP);
        });

        It("should remain silent when the silent flag is on", [this]()
        {
            NodeTemplate->bLogProperty       = true;
            NodeTemplate->bLogPropertySilent = true;
            NodeTemplate->LogType            = ESMCompilerLogType::Error;
            NodeTemplate->LogMessage         = TEXT("A Property error message!");
            FKismetEditorUtilities::CompileBlueprint(NewBP);
        });
    });

    AfterEach([this]()
    {
        Settings->EditorNodeConstructionScriptSetting = OriginalCSSetting;
        NewAsset.DeleteAsset(this);
    });
}

#endif

#endif //WITH_DEV_AUTOMATION_TESTS
