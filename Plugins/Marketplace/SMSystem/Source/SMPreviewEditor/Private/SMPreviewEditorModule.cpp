// Copyright Recursoft LLC. All Rights Reserved.

#include "SMPreviewEditorModule.h"

#include "SMPreviewEditorCommands.h"
#include "SMPreviewObject.h"
#include "Utilities/SMPreviewUtils.h"
#include "Views/Editor/SSMPreviewModeEditorView.h"
#include "Views/Viewport/SMPreviewModeViewportClient.h"
#include "Views/Viewport/SSMPreviewModeViewportView.h"

#include "Blueprints/SMBlueprint.h"
#include "Blueprints/SMBlueprintEditor.h"

#include "SAdvancedPreviewDetailsTab.h"
#include "Toolkits/AssetEditorToolkit.h"

#define LOCTEXT_NAMESPACE "SMPreviewEditorModule"

void FSMPreviewEditorModule::StartupModule()
{
	FSMPreviewEditorCommands::Register();
	
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	FSMPreviewUtils::BindDelegates();
}

void FSMPreviewEditorModule::ShutdownModule()
{
	FSMPreviewUtils::UnbindDelegates();
	FSMPreviewEditorCommands::Unregister();
}

USMPreviewObject* FSMPreviewEditorModule::CreatePreviewObject(UObject* Outer)
{
	return NewObject<USMPreviewObject>(Outer);
}

USMPreviewObject* FSMPreviewEditorModule::RecreatePreviewObject(USMPreviewObject* OriginalPreviewObject)
{
	check(OriginalPreviewObject);
	return NewObject<USMPreviewObject>(OriginalPreviewObject->GetOuter(), NAME_None, RF_NoFlags, OriginalPreviewObject);
}

void FSMPreviewEditorModule::StartPreviewSimulation(USMBlueprint* StateMachineBlueprint)
{
	FSMPreviewUtils::StartSimulation(StateMachineBlueprint);
}

bool FSMPreviewEditorModule::CanStartPreviewSimulation(USMBlueprint* StateMachineBlueprint)
{
	if (GEditor && GEditor->IsPlaySessionInProgress())
	{
		// Don't allow during PIE.
		return false;
	}

	if (StateMachineBlueprint)
	{
		if (const USMPreviewObject* PreviewObject = StateMachineBlueprint->GetPreviewObject(false))
		{
			// Can't play without a context.
			return PreviewObject->GetContextObject() != nullptr;
		}
	}

	return true;
}

void FSMPreviewEditorModule::StopPreviewSimulation(USMBlueprint* StateMachineBlueprint)
{
	FSMPreviewUtils::StopSimulation(StateMachineBlueprint);
}

bool FSMPreviewEditorModule::IsPreviewRunning(USMBlueprint* StateMachineBlueprint)
{
	check(StateMachineBlueprint);
	return StateMachineBlueprint->GetPreviewObject()->IsSimulationRunning();
}

void FSMPreviewEditorModule::DeleteSelection(TWeakPtr<FSMStateMachineBlueprintEditor> InBlueprintEditor)
{
	if (!InBlueprintEditor.IsValid())
	{
		return;
	}

	const TWeakPtr<FSMPreviewModeViewportClient> PreviewClient = StaticCastSharedPtr<FSMPreviewModeViewportClient>(InBlueprintEditor.Pin()->GetPreviewClient().Pin());
	if (PreviewClient.IsValid())
	{
		if (AActor* Actor = PreviewClient.Pin()->GetSelectedActor())
		{
			if (FSMAdvancedPreviewScene* AdvPreviewScene = PreviewClient.Pin()->GetOurPreviewScene())
			{
				if (USMPreviewObject* PreviewObject = AdvPreviewScene->GetPreviewObject())
				{
					PreviewObject->RemovePreviewActor(Actor);
				}
			}
		}
	}
}

TSharedRef<SWidget> FSMPreviewEditorModule::CreatePreviewEditorWidget(TWeakPtr<FSMStateMachineBlueprintEditor> InBlueprintEditor, const FName& InTabID)
{
	check(InBlueprintEditor.IsValid());
	return SNew(SSMPreviewModeEditorView, InBlueprintEditor.Pin().ToSharedRef(), InTabID);
}

TSharedRef<SWidget> FSMPreviewEditorModule::CreatePreviewViewportWidget(TWeakPtr<FSMStateMachineBlueprintEditor> InBlueprintEditor)
{
	check(InBlueprintEditor.IsValid());
	return SNew(SSMPreviewModeViewportView, InBlueprintEditor.Pin().ToSharedRef());
}

TSharedRef<SWidget> FSMPreviewEditorModule::CreateAdvancedSceneDetailsWidget(TWeakPtr<FSMStateMachineBlueprintEditor> InBlueprintEditor, TSharedPtr<SWidget> InViewportWidget)
{
	check(InBlueprintEditor.IsValid());
	TSharedPtr<SSMPreviewModeViewportView> Viewport = StaticCastSharedPtr<SSMPreviewModeViewportView>(InViewportWidget);
	return SNew(SAdvancedPreviewDetailsTab, Viewport->GetAdvancedPreviewScene().ToSharedRef());
}

IMPLEMENT_MODULE(FSMPreviewEditorModule, SMPreviewEditor)

#undef LOCTEXT_NAMESPACE
