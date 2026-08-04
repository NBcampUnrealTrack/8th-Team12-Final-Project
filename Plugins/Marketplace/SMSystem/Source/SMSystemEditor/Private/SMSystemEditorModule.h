// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "ISMSystemEditorModule.h"
#include "Blueprints/SMBlueprintAssetTypeActions.h"
#include "Compilers/SMKismetCompiler.h"

#include "EdGraphUtilities.h"

class FExtensibilityManager;
class FSMBlueprintDebuggerCommands;

class FSMSystemEditorModule : public ISMSystemEditorModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() const override { return MenuExtensibilityManager; }
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() const override { return ToolBarExtensibilityManager; }

	virtual FExtendNodeInstanceDetails& GetExtendNodeInstanceDetails() override { return ExtendNodeInstanceDetails; }
	virtual FExtendGraphNodeContextMenu& GetExtendGraphNodeContextMenu() override { return ExtendGraphNodeContextMenu; }

	virtual TSharedPtr<FWorkspaceItem> GetToolsWorkspaceGroup() const override;

	/** If the user has pressed play in editor. */
	virtual bool IsPlayingInEditor() const override { return bPlayingInEditor; }

	virtual void RegisterBlueprintVariableCustomization() override;
	virtual void UnregisterBlueprintVariableCustomization() override;

	/** Either register or unregister the debugger commands based on the user's settings. */
	void RegisterOrUnregisterDebuggerCommands();
	
	/** Register a visual pin factory and store it so it can be automatically unregistered. */
	template<typename T>
	void RegisterPinFactory()
	{
		TSharedPtr<FGraphPanelPinFactory> PinNodeFactory = MakeShared<T>();
		FEdGraphUtilities::RegisterVisualPinFactory(PinNodeFactory);
		PinFactories.Add(MoveTemp(PinNodeFactory));
	}
	
private:
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action);
	static TSharedPtr<FKismetCompilerContext> GetCompilerForStateMachineBP(UBlueprint* BP, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions);
	static TSharedPtr<FKismetCompilerContext> GetCompilerForNodeBP(UBlueprint* BP, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions);
	
	void RegisterSettings();
	void UnregisterSettings();

	void RegisterPinFactories();
	void UnregisterPinFactories();

	void BeginPIE(bool bValue);
	void EndPie(bool bValue);

	void OnFilesLoaded();
	void OnAssetAdded(const FAssetData& InAssetData);
	
	void OnEditorInitialized(double Duration);

	void CheckForNewInstalledVersion();
	void DisplayUpdateNotification(const struct FPluginDescriptor& Descriptor, bool bIsUpdate);
	void OnViewNewReleaseNotesClicked(FString Version);
	void OnDismissUpdateNotificationClicked();
	
	void ClearUpdateNotification();

	void HandleModuleChanged(FName ModuleName, EModuleChangeReason ChangeReason);
	
private:
	TArray<TSharedPtr<IAssetTypeActions>> CreatedAssetTypeActions;

	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	mutable TSharedPtr<FWorkspaceItem> LogicDriverToolsWorkspaceGroup;

	TSharedPtr<FGraphPanelNodeFactory> SMGraphPanelNodeFactory;

	/** Registered pin factories. */
	TArray<TSharedPtr<FGraphPanelPinFactory>> PinFactories;
	
	FSMKismetCompiler SMBlueprintCompiler;
	FSMNodeKismetCompiler SMNodeBlueprintCompiler;
	
	FDelegateHandle RefreshAllNodesDelegateHandle;
	FDelegateHandle RenameVariableDelegateHandle;
	FDelegateHandle ModuleChangedHandle;
	/** For variable customization in UE 5.1+ only. */
	FDelegateHandle BlueprintVariableCustomizationHandle;

	FDelegateHandle BeginPieHandle;
	FDelegateHandle EndPieHandle;

	FDelegateHandle AssetAddedHandle;
	FDelegateHandle FilesLoadedHandle;
	
	FDelegateHandle EditorInitializedHandle;

	FExtendNodeInstanceDetails ExtendNodeInstanceDetails;
	FExtendGraphNodeContextMenu ExtendGraphNodeContextMenu;

	/** Debugger command manager. */
	TUniquePtr<FSMBlueprintDebuggerCommands> DebuggerCommands;
	
	/** Notification popup that the plugin has updated. */
	TWeakPtr<SNotificationItem> NewVersionNotification;
	
	/** If the user has pressed play in editor. */
	bool bPlayingInEditor = false;
};
