// Copyright Recursoft LLC. All Rights Reserved.

#include "SMUtilityLauncherCommands.h"

#include "SMUnrealTypeDefs.h"

#define LOCTEXT_NAMESPACE "SMUtilityLauncherCommands"

FSMUtilityLauncherCommands::FSMUtilityLauncherCommands(): TCommands<FSMUtilityLauncherCommands>(TEXT("SMUtilityLauncher"), NSLOCTEXT("Contexts", "SMUtilityLauncher", "Logic Driver Tools"),
	NAME_None, FSMUnrealAppStyle::Get().GetStyleSetName())
{
}

void FSMUtilityLauncherCommands::RegisterCommands()
{
	UI_COMMAND(OpenDocs, "Documentation", "View the Logic Driver documentation site.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenReleaseNotes, "Release Notes", "View the release notes for the current version.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenDiscord, "Discord", "Access the Logic Driver Discord community.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AboutLogicDriver, "About Logic Driver", "View version and system information about Logic Driver.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CreateStateMachineClass, "New State Machine Class...", "Create a new State Machine Blueprint Class.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CreateNodeClass, "New Node Class...", "Create a new Node Blueprint Class.", EUserInterfaceActionType::Button, FInputChord());
}

const FSMUtilityLauncherCommands& FSMUtilityLauncherCommands::Get()
{
	return TCommands<FSMUtilityLauncherCommands>::Get();
}

#undef LOCTEXT_NAMESPACE
