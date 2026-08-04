// Copyright Recursoft LLC. All Rights Reserved.

#include "SMProjectEditorSettings.h"

#include "SMSystemEditorModule.h"

#include "SMConduitInstance.h"
#include "SMInstance.h"
#include "SMStateInstance.h"
#include "SMStateMachineInstance.h"
#include "SMTransitionInstance.h"

USMProjectEditorSettings::USMProjectEditorSettings()
{
	InstalledVersion = "";
	bUpdateAssetsOnStartup = true;
	bDisplayAssetUpdateProgress = true;
	bDisplayUpdateNotification = true;
	
	bDisplayMemoryLimitsOnCompile = true;
	bAlwaysDisplayStructMemoryUsage = false;
	StructMemoryLimitWarningThreshold = 0.95f;
	
	bDisplayInputEventNotes = true;
	
	bRestrictInvalidCharacters = true;
	bWarnIfChildrenAreOutOfDate = true;
	bCalculateGuidsOnCompile = true;
	bLinkerLoadHandling = true;
	
	bDefaultNewTransitionsToTrue = false;
	bDefaultNewConduitsToTrue = false;
	bConfigureNewConduitsAsTransitions = true;

	DefaultStateMachineBlueprintParentClass = USMInstance::StaticClass();
	DefaultStateMachineBlueprintNamePrefix = TEXT("BP_");
	DefaultNodeBlueprintNamePrefix = TEXT("BP_");
	
	EditorNodeConstructionScriptSetting = ESMEditorConstructionScriptProjectSetting::SM_Standard;
	DefaultStateClass = USMStateInstance::StaticClass();
	DefaultStateMachineClass = USMStateMachineInstance::StaticClass();
	DefaultConduitClass = USMConduitInstance::StaticClass();
	DefaultTransitionClass = USMTransitionInstance::StaticClass();
	bEnableReferenceTemplatesByDefault = false;
	bRunConstructionScriptsOnLoad = true;

	OverrideActorSoftReferencePins = ESMPinOverride::LogicDriverOnly;
	OverrideObjectPins = ESMPinOverride::LogicDriverOnly;
	bDisplayThumbnailsByDefault = true;
	
	bEnableDebugCommandHooks = true;
	
	bEnablePreviewMode = true;
}

void USMProjectEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (DefaultStateMachineBlueprintParentClass.IsNull())
	{
		DefaultStateMachineBlueprintParentClass = USMInstance::StaticClass();
	}
	
	if (DefaultStateClass.IsNull())
	{
		DefaultStateClass = USMStateInstance::StaticClass();
	}

	if (DefaultStateMachineClass.IsNull())
	{
		DefaultStateMachineClass = USMStateMachineInstance::StaticClass();
	}

	if (DefaultConduitClass.IsNull())
	{
		DefaultConduitClass = USMConduitInstance::StaticClass();
	}

	if (DefaultTransitionClass.IsNull())
	{
		DefaultTransitionClass = USMTransitionInstance::StaticClass();
	}
	
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(USMProjectEditorSettings, bEnableDebugCommandHooks))
	{
		FSMSystemEditorModule& SMBlueprintEditorModule = FModuleManager::GetModuleChecked<FSMSystemEditorModule>(LOGICDRIVER_EDITOR_MODULE_NAME);
		SMBlueprintEditorModule.RegisterOrUnregisterDebuggerCommands();
	}
	
	SaveConfig();
}
