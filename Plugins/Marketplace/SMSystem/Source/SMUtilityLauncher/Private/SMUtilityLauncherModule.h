// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

#include "ISMUtilityLauncherModule.h"

class FToolBarBuilder;
class FExtender;
class FExtensibilityManager;
class FMenuBuilder;
class FUICommandList;
class SWidget;

class FSMUtilityLauncherModule : public ISMUtilityLauncherModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Gets the extensibility managers for outside entities to extend this editor's menus and toolbars */
	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() const override { return MenuExtensibilityManager; }
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() const override { return ToolBarExtensibilityManager; }

private:
	void BindCommands();
	void ExtendLevelEditorToolbar(FToolBarBuilder& ToolbarBuilder);
	TSharedRef<SWidget> GenerateMenuContent() const;

	/** Retrieve the icon to use for the Logic Driver level toolbar. */
	FSlateIcon GetLevelToolbarIcon() const;

	/** Retrieve the icon to use for license information. */
	FSlateIcon GetLicenseIcon() const;
	
private:
	static void OpenDocs();
	static void OpenReleaseNotes();
	static void OpenDiscord();
	static void ViewAboutPage();
	static void CreateNewStateMachineClass();
	static void CreateNewNodeClass();
	static void MakeOpenBlueprintClassMenu(FMenuBuilder& InMenu);
	static void OpenLicenseInformation();
	
private:
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;
	TSharedPtr<FUICommandList> PluginCommands;
	TSharedPtr<FExtender> ToolbarExtender;
};
