// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Chat/AIChatCommands.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"
#include "LevelEditor.h"
#include "Editor.h"
#include "Widgets/Docking/SDockTab.h"
#include "StatusBarSubsystem.h"

#define LOCTEXT_NAMESPACE "AIChatCommands"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyChat, Log, All);

// Static members
TSharedPtr<FUICommandList> FAIChatCommands::CommandList;
FDelegateHandle FAIChatCommands::MenuExtensionHandle;
FDelegateHandle FAIChatCommands::PanelDrawerSummonHandle;
const FName FAIChatCommands::AIChatTabName("RevvyAIChat");

FAIChatCommands::FAIChatCommands()
    : TCommands<FAIChatCommands>(
        TEXT("AIChatCommands"),
        LOCTEXT("AIChatCommands", "AI Chat Commands"),
        NAME_None,
        FAppStyle::GetAppStyleSetName())
{
}

void FAIChatCommands::RegisterCommands()
{
    UI_COMMAND(
        OpenAIChat,
        "Open Reddy",
        "Open Reddy, the desktop IDE that carries the chat",
        EUserInterfaceActionType::Button,
        FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::V)
    );
}

void FAIChatCommands::Initialize()
{
    // Register commands
    FAIChatCommands::Register();
    
    // Create command list
    CommandList = MakeShared<FUICommandList>();
    
    // Map commands to actions
    CommandList->MapAction(
        Get().OpenAIChat,
        FExecuteAction::CreateStatic(&FAIChatCommands::HandleOpenAIChat),
        FCanExecuteAction::CreateStatic(&FAIChatCommands::CanOpenAIChat)
    );
    
    // Bind to Level Editor's global actions so keyboard shortcuts work
    FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
    LevelEditorModule.GetGlobalLevelEditorActions()->Append(CommandList.ToSharedRef());
    
    // Register tab spawner
    RegisterTabSpawner();
    
    // Register menus via startup callback to ensure UToolMenus is fully initialized
    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateStatic(&FAIChatCommands::RegisterMenus));
    
    // Register status bar panel drawer (after editor is ready)
    if (GEditor)
    {
        RegisterStatusBarPanelDrawer();
    }
    else
    {
        FCoreDelegates::GetOnPostEngineInit().AddStatic(&FAIChatCommands::RegisterStatusBarPanelDrawer);
    }
    
    UE_LOG(LogRevvyChat, Log, TEXT("AI Chat commands initialized with panel drawer support"));
}

void FAIChatCommands::Shutdown()
{
    UnregisterStatusBarPanelDrawer();
    UnregisterTabSpawner();
    UnregisterMenus();
    
    CommandList.Reset();
    
    FAIChatCommands::Unregister();
    
    UE_LOG(LogRevvyChat, Log, TEXT("AI Chat commands shutdown"));
}

void FAIChatCommands::RegisterTabSpawner()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        AIChatTabName,
        FOnSpawnTab::CreateStatic(&FAIChatCommands::SpawnAIChatTab))
        .SetDisplayName(LOCTEXT("AIChatTabTitle", "레비"))
        .SetMenuType(ETabSpawnerMenuType::Hidden)
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"))
        .SetCanSidebarTab(false);  // Panel drawer tabs don't work well as sidebar tabs

    UE_LOG(LogRevvyChat, Log, TEXT("AI Chat tab spawner registered"));
}

void FAIChatCommands::UnregisterTabSpawner()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AIChatTabName);
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FAIChatCommands::RegisterStatusBarPanelDrawer()
{
    if (GEditor)
    {
        // Make sure StatusBar module is loaded
        FModuleManager::Get().LoadModuleChecked(TEXT("StatusBar"));
        
        if (UStatusBarSubsystem* StatusBarSubsystem = GEditor->GetEditorSubsystem<UStatusBarSubsystem>())
        {
            PanelDrawerSummonHandle = StatusBarSubsystem->RegisterPanelDrawerSummon(
                UStatusBarSubsystem::FRegisterPanelDrawerSummonDelegate::FDelegate::CreateStatic(
                    &FAIChatCommands::GeneratePanelDrawerSummon)
            );
            
            UE_LOG(LogRevvyChat, Log, TEXT("AI Chat panel drawer registered in status bar"));
        }
    }
}

void FAIChatCommands::UnregisterStatusBarPanelDrawer()
{
    if (GEditor && PanelDrawerSummonHandle.IsValid())
    {
        if (UStatusBarSubsystem* StatusBarSubsystem = GEditor->GetEditorSubsystem<UStatusBarSubsystem>())
        {
            StatusBarSubsystem->UnregisterPanelDrawerSummon(PanelDrawerSummonHandle);
        }
        PanelDrawerSummonHandle.Reset();
    }
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

// The in-editor Slate chat was retired: Reddy (the desktop IDE) carries the same
// chat against the same engine tools, and maintaining both meant every stream
// parser and skill change twice. The tab survives as a launcher so the muscle
// memory (Ctrl+Shift+V, status-bar button) still lands somewhere useful.
//
// Finding it is the fiddly part: the install folder is named after the app's
// package name, not its product name, so a per-user install lands in
// `Programs/reddy-ide` while the binary inside is `Reddy.exe`. Looking only
// where the product name suggests meant the button reported "not installed" on
// every machine that actually had it.
static const TCHAR* const ReddyInstallFolders[] = {TEXT("reddy-ide"), TEXT("Reddy")};

static FString FindReddyExecutable()
{
    TArray<FString> Roots;
    const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
    if (!LocalAppData.IsEmpty())
    {
        Roots.Add(FPaths::Combine(LocalAppData, TEXT("Programs")));
    }
    // Per-machine installs are not the default, but an admin can choose one.
    const FString ProgramFiles = FPlatformMisc::GetEnvironmentVariable(TEXT("PROGRAMFILES"));
    if (!ProgramFiles.IsEmpty())
    {
        Roots.Add(ProgramFiles);
    }

    for (const FString& Root : Roots)
    {
        for (const TCHAR* const Folder : ReddyInstallFolders)
        {
            const FString Candidate = FPaths::Combine(Root, Folder, TEXT("Reddy.exe"));
            if (FPaths::FileExists(Candidate))
            {
                return Candidate;
            }
        }
    }
    return FString();
}

// Why cached: the tab paints every frame and this hits the filesystem. Why not
// resolved once at spawn: someone who installs Reddy while the editor is open
// would otherwise stare at a dead button until they close and reopen the tab.
static bool IsReddyInstalledCached()
{
    static double LastCheckedSeconds = -1000.0;
    static bool bLastResult = false;
    const double Now = FPlatformTime::Seconds();
    if (Now - LastCheckedSeconds > 2.0)
    {
        bLastResult = !FindReddyExecutable().IsEmpty();
        LastCheckedSeconds = Now;
    }
    return bLastResult;
}

static void LaunchReddy()
{
    const FString Executable = FindReddyExecutable();
    if (Executable.IsEmpty())
    {
        UE_LOG(LogRevvyChat, Warning,
            TEXT("Reddy is not installed (looked for Reddy.exe under "
                 "%%LOCALAPPDATA%%/Programs and %%PROGRAMFILES%%, in reddy-ide/ and Reddy/). "
                 "Install it from https://github.com/RevStudio/RevvyIDE/releases"));
        return;
    }
    FPlatformProcess::CreateProc(*Executable, TEXT(""), true, false, false, nullptr, 0, nullptr, nullptr);
}

TSharedRef<SDockTab> FAIChatCommands::SpawnAIChatTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        .Label(LOCTEXT("AIChatTabLabel", "레디"))
        [
            SNew(SBox)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Center)
                .Padding(0.f, 0.f, 0.f, 12.f)
                [
                    SNew(STextBlock)
                    .Text_Lambda([]() {
                        return IsReddyInstalledCached()
                            ? LOCTEXT("ReddyLaunchHint",
                                "채팅은 Reddy에서 진행됩니다. 엔진 툴은 그대로 연결됩니다.")
                            : LOCTEXT("ReddyInstallHint",
                                "Reddy가 설치되어 있지 않습니다. github.com/RevStudio/RevvyIDE 에서 설치하세요.");
                    })
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Center)
                [
                    SNew(SButton)
                    .IsEnabled_Lambda([]() { return IsReddyInstalledCached(); })
                    .Text(LOCTEXT("OpenReddyButton", "Reddy 열기"))
                    .OnClicked_Lambda([]() { LaunchReddy(); return FReply::Handled(); })
                ]
            ]
        ];
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FAIChatCommands::GeneratePanelDrawerSummon(
    TArray<UStatusBarSubsystem::FTabIdAndButtonLabel>& OutTabIdsAndLabels,
    const TSharedRef<SDockTab>& InParentTab)
{
    // Add the "레비" button to the status bar drawer (Mixamo is a top tab inside the panel)
    OutTabIdsAndLabels.Emplace(AIChatTabName, LOCTEXT("StatusBarRevvy", "레비"));
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FAIChatCommands::RegisterMenus()
{
    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus)
    {
        return;
    }

    // Owner will be used for cleanup in call to UToolMenus::UnregisterOwner()
    FToolMenuOwnerScoped OwnerScoped("AIChatCommands");

    // Add to Window menu under Assistance section (alongside Epic AI Assistant)
    {
        UToolMenu* WindowMenu = ToolMenus->ExtendMenu("MainFrame.MainMenu.Window");

        FToolMenuSection& Section = WindowMenu->FindOrAddSection("Assistance");

        Section.AddEntry(FToolMenuEntry::InitMenuEntry(
            "RevvyAIChat",
            LOCTEXT("OpenAIChatLabel", "레비"),
            LOCTEXT("OpenAIChatTooltip", "레비 AI 채팅 패널 열기 (Ctrl+Shift+V)"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
            FUIAction(
                FExecuteAction::CreateStatic(&FAIChatCommands::HandleOpenAIChat),
                FCanExecuteAction::CreateStatic(&FAIChatCommands::CanOpenAIChat)
            )
        ));
    }

    ToolMenus->RefreshAllWidgets();
}

void FAIChatCommands::UnregisterMenus()
{
    // Use UnregisterOwner to remove only our entries, not the entire section
    // (the Assistance section is shared with Epic's AI Assistant)
    UToolMenus::UnregisterOwner("AIChatCommands");
}

void FAIChatCommands::HandleOpenAIChat()
{
    // Try to toggle the tab in the panel drawer (right-side slide-in panel)
    // This matches how Unreal's AI Assistant behaves
    
    // Get the widget path under the cursor to find the appropriate window
    FWidgetPath WidgetPath = FSlateApplication::Get().LocateWindowUnderMouse(
        FSlateApplication::Get().GetCursorPos(),
        FSlateApplication::Get().GetInteractiveTopLevelWindows());
    
    TSharedPtr<FTabManager> TabManager;
    if (WidgetPath.IsValid())
    {
        TabManager = FGlobalTabmanager::Get()->GetSubTabManagerForWindow(WidgetPath.GetWindow());
    }
    
    if (TabManager)
    {
        // Toggle the tab in the panel drawer
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        TabManager->TryToggleTabInPanelDrawer(AIChatTabName, {});
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }
    else
    {
        // Fallback: just invoke the tab normally
        FGlobalTabmanager::Get()->TryInvokeTab(AIChatTabName);
    }
}

bool FAIChatCommands::CanOpenAIChat()
{
    return true;
}

#undef LOCTEXT_NAMESPACE
