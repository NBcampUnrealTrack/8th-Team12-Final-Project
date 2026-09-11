// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "MCP/MCPServer.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Misc/Guid.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Core/ToolRegistry.h"
#include "Chat/MCPTypes.h"
#include "Async/Async.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "MCP/IdempotencyLedger.h"
#include "MCP/OversizedResultStore.h"
#include "MCP/LogRedaction.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/Archive.h"
#include <cstdio>

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_UNIX || PLATFORM_MAC
#include <sys/stat.h>
#include <unistd.h>
#endif

DEFINE_LOG_CATEGORY(LogMCPServer);

TSharedPtr<FMCPServer> FMCPServer::Instance;

// MCP Protocol versions we support (newest first)
static const TArray<FString> SUPPORTED_PROTOCOL_VERSIONS = {
    TEXT("2025-11-25"),  // Latest spec
    TEXT("2025-06-18"),  // Mid-2025 spec
    TEXT("2024-11-05")   // For compatibility with older clients
};
static const FString MCP_SERVER_NAME = TEXT("Revvy");
static const FString MCP_SERVER_VERSION = TEXT("1.0.0");

namespace
{
    constexpr int32 DefaultEditorPort = 8088;
    constexpr int32 DefaultProxyPort = 8089;

    FString GetRevvyStateDir()
    {
        FString StateDir = FPlatformMisc::GetEnvironmentVariable(TEXT("REVVY_STATE_DIR"));
        if (!StateDir.IsEmpty())
        {
            return FPaths::ConvertRelativePathToFull(StateDir);
        }

        return FPaths::ConvertRelativePathToFull(
            FPaths::ProjectDir() / TEXT(".revvy"));
    }

    FString GetProxyRuntimeConfigPath()
    {
        FString OverridePath = FPlatformMisc::GetEnvironmentVariable(TEXT("REVVY_CONFIG"));
        if (!OverridePath.IsEmpty())
        {
            return FPaths::ConvertRelativePathToFull(OverridePath);
        }
        return GetRevvyStateDir() / TEXT("revvy-proxy.json");
    }

    FString GetCanonicalProjectPath()
    {
        FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        FPaths::NormalizeDirectoryName(ProjectPath);
        FPaths::CollapseRelativeDirectories(ProjectPath);
        return ProjectPath;
    }

    bool SaveStringToFileAtomically(const FString& Contents, const FString& Destination)
    {
        const FString TemporaryPath = FString::Printf(
            TEXT("%s.%s.tmp"),
            *Destination,
            *FGuid::NewGuid().ToString(EGuidFormats::Digits));
        if (!FFileHelper::SaveStringToFile(
                Contents,
                *TemporaryPath,
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            IFileManager::Get().Delete(*TemporaryPath, false, true);
            return false;
        }

        // FFileManager::Move deletes an existing destination before moving, which
        // creates a visibility gap. Use each platform's same-volume replace so
        // readers observe either the old file or the complete new JSON.
#if PLATFORM_WINDOWS
        const bool bReplaced = ::MoveFileExW(
            *TemporaryPath,
            *Destination,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        const bool bReplaced = std::rename(
            TCHAR_TO_UTF8(*TemporaryPath),
            TCHAR_TO_UTF8(*Destination)) == 0;
#endif
        if (!bReplaced)
        {
            IFileManager::Get().Delete(*TemporaryPath, false, true);
            return false;
        }
        return true;
    }

    bool IsValidTcpPort(int32 Port)
    {
        return Port >= 1 && Port <= 65535;
    }

    bool IsManagedLegacyMcpUrl(const FString& Url)
    {
        static const FString LoopbackPrefix = TEXT("http://127.0.0.1:");
        if (!Url.StartsWith(LoopbackPrefix, ESearchCase::CaseSensitive))
        {
            return false;
        }

        const int32 PathStart = Url.Find(
            TEXT("/"),
            ESearchCase::CaseSensitive,
            ESearchDir::FromStart,
            LoopbackPrefix.Len());
        if (PathStart == INDEX_NONE)
        {
            return false;
        }

        // Routed Reddy endpoints such as /projects/<routeId>/mcp/safe are
        // project-owned. Only the two legacy root paths are Revvy-managed.
        const FString Pathname = Url.Mid(PathStart);
        return Pathname == TEXT("/mcp") || Pathname == TEXT("/mcp/safe");
    }

    bool TryParseIntegratedReddyMcpUrl(
        const FString& Url,
        int32& OutPort,
        FString& OutSafeUrl)
    {
        OutPort = 0;
        OutSafeUrl.Empty();

        static const FString LoopbackPrefix = TEXT("http://127.0.0.1:");
        static const FString ProjectPrefix = TEXT("/projects/");
        static const FString FullSuffix = TEXT("/mcp");
        static const FString SafeSuffix = TEXT("/mcp/safe");
        constexpr int32 RouteIdLength = 24;

        if (!Url.StartsWith(LoopbackPrefix, ESearchCase::CaseSensitive))
        {
            return false;
        }

        const int32 PathStart = Url.Find(
            TEXT("/"),
            ESearchCase::CaseSensitive,
            ESearchDir::FromStart,
            LoopbackPrefix.Len());
        if (PathStart == INDEX_NONE)
        {
            return false;
        }

        const FString PortText = Url.Mid(
            LoopbackPrefix.Len(), PathStart - LoopbackPrefix.Len());
        if (PortText.IsEmpty() || PortText.Len() > 5)
        {
            return false;
        }
        for (const TCHAR Character : PortText)
        {
            if (Character < TEXT('0') || Character > TEXT('9'))
            {
                return false;
            }
        }

        const int64 ParsedPort = FCString::Atoi64(*PortText);
        if (ParsedPort < 1
            || ParsedPort > 65535
            || FString::FromInt(static_cast<int32>(ParsedPort)) != PortText)
        {
            return false;
        }

        const FString Pathname = Url.Mid(PathStart);
        if (!Pathname.StartsWith(ProjectPrefix, ESearchCase::CaseSensitive))
        {
            return false;
        }
        const FString RoutedPath = Pathname.Mid(ProjectPrefix.Len());
        const int32 RouteSeparator = RoutedPath.Find(TEXT("/"));
        if (RouteSeparator != RouteIdLength)
        {
            return false;
        }

        const FString RouteId = RoutedPath.Left(RouteSeparator);
        for (const TCHAR Character : RouteId)
        {
            const bool bDecimal = Character >= TEXT('0') && Character <= TEXT('9');
            const bool bLowerHex = Character >= TEXT('a') && Character <= TEXT('f');
            if (!bDecimal && !bLowerHex)
            {
                return false;
            }
        }

        const FString Endpoint = RoutedPath.Mid(RouteSeparator);
        if (Endpoint != FullSuffix && Endpoint != SafeSuffix)
        {
            return false;
        }

        OutPort = static_cast<int32>(ParsedPort);
        OutSafeUrl = Endpoint == SafeSuffix ? Url : Url + TEXT("/safe");
        return true;
    }

    bool TryReadConfiguredIntegratedReddyProxy(
        int32& OutPort,
        FString& OutSafeUrl)
    {
        OutPort = 0;
        OutSafeUrl.Empty();

        FString JsonString;
        const FString McpJsonPath = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectDir() / TEXT(".mcp.json"));
        if (!FFileHelper::LoadFileToString(JsonString, *McpJsonPath))
        {
            return false;
        }

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonObject>* Servers = nullptr;
        const TSharedPtr<FJsonObject>* Revvy = nullptr;
        FString Type;
        FString Url;
        return Root->TryGetObjectField(TEXT("mcpServers"), Servers)
            && Servers != nullptr
            && Servers->IsValid()
            && (*Servers)->TryGetObjectField(TEXT("Revvy"), Revvy)
            && Revvy != nullptr
            && Revvy->IsValid()
            && (*Revvy)->TryGetStringField(TEXT("type"), Type)
            && Type == TEXT("http")
            && (*Revvy)->TryGetStringField(TEXT("url"), Url)
            && TryParseIntegratedReddyMcpUrl(Url, OutPort, OutSafeUrl);
    }

    bool IsCurrentUnrealProjectPath(const FString& CandidatePath)
    {
        if (CandidatePath.IsEmpty() || FPaths::IsRelative(CandidatePath))
        {
            return false;
        }

        FString NormalizedCandidate = FPaths::ConvertRelativePathToFull(CandidatePath);
        FString NormalizedProject = GetCanonicalProjectPath();
        FPaths::NormalizeDirectoryName(NormalizedCandidate);
        FPaths::CollapseRelativeDirectories(NormalizedCandidate);
        return FPaths::IsSamePath(NormalizedCandidate, NormalizedProject);
    }

    bool RuntimeConfigMatchesCurrentUnrealProject(const TSharedPtr<FJsonObject>& JsonObject)
    {
        if (!JsonObject.IsValid())
        {
            return false;
        }

        FString PortScope;
        FString Engine;
        if (!JsonObject->TryGetStringField(TEXT("port_scope"), PortScope)
            || !PortScope.Equals(TEXT("project"), ESearchCase::IgnoreCase)
            || !JsonObject->TryGetStringField(TEXT("engine"), Engine)
            || !Engine.Equals(TEXT("unreal"), ESearchCase::IgnoreCase))
        {
            return false;
        }

        // register_project refreshes installed_project before the editor starts.
        // Keep the other identities for configs previously written by the editor.
        static const TCHAR* ProjectObjectFields[] = {
            TEXT("installed_project"),
            TEXT("active_project")
        };
        for (const TCHAR* ProjectObjectField : ProjectObjectFields)
        {
            const TSharedPtr<FJsonObject>* ProjectObject = nullptr;
            FString ProjectPath;
            if (JsonObject->TryGetObjectField(ProjectObjectField, ProjectObject)
                && ProjectObject != nullptr
                && ProjectObject->IsValid()
                && (*ProjectObject)->TryGetStringField(TEXT("path"), ProjectPath)
                && IsCurrentUnrealProjectPath(ProjectPath))
            {
                return true;
            }
        }

        FString ProjectPath;
        return JsonObject->TryGetStringField(TEXT("project_path"), ProjectPath)
            && IsCurrentUnrealProjectPath(ProjectPath);
    }

    bool TryReadProjectScopedRuntimePort(const TCHAR* FieldName, int32& OutPort)
    {
        FString ExistingJson;
        if (!FFileHelper::LoadFileToString(ExistingJson, *GetProxyRuntimeConfigPath()))
        {
            return false;
        }

        TSharedPtr<FJsonObject> JsonObject;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExistingJson);
        if (!FJsonSerializer::Deserialize(Reader, JsonObject)
            || !RuntimeConfigMatchesCurrentUnrealProject(JsonObject))
        {
            return false;
        }

        // UE's int32 JSON overload rounds fractional values, so validate the
        // double representation explicitly before accepting a listener port.
        double RawPort = 0.0;
        if (!JsonObject->TryGetNumberField(FieldName, RawPort)
            || !FMath::IsFinite(RawPort)
            || RawPort < 1.0
            || RawPort > 65535.0)
        {
            return false;
        }

        const int32 CandidatePort = static_cast<int32>(RawPort);
        if (static_cast<double>(CandidatePort) != RawPort)
        {
            return false;
        }

        OutPort = CandidatePort;
        return true;
    }

    int32 ResolveConfiguredPort(
        const TCHAR* IniSection,
        const TCHAR* RuntimeField,
        int32 DefaultPort)
    {
        int32 IniPort = DefaultPort;
        if (GConfig != nullptr)
        {
            GConfig->GetInt(IniSection, TEXT("Port"), IniPort, GEditorPerProjectIni);
        }

        // A valid non-default INI value is an explicit user choice. The legacy
        // defaults are replaceable by the project-local runtime assignment.
        if (IniPort != DefaultPort && IsValidTcpPort(IniPort))
        {
            return IniPort;
        }

        int32 RuntimePort = 0;
        if (TryReadProjectScopedRuntimePort(RuntimeField, RuntimePort))
        {
            return RuntimePort;
        }

        return IsValidTcpPort(IniPort) ? IniPort : DefaultPort;
    }

    FString ExpandUserSecurityPath(const FString& InputPath)
    {
        FString ExpandedPath = InputPath;
        if (ExpandedPath == TEXT("~")
            || ExpandedPath.StartsWith(TEXT("~/"))
            || ExpandedPath.StartsWith(TEXT("~\\")))
        {
#if PLATFORM_WINDOWS
            FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
#else
            FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
#endif
            if (UserProfile.IsEmpty())
            {
                UserProfile = FPlatformProcess::UserDir();
            }
            ExpandedPath = ExpandedPath.Len() == 1
                ? UserProfile
                : UserProfile / ExpandedPath.Mid(2);
        }
        return FPaths::ConvertRelativePathToFull(ExpandedPath);
    }

    FString GetRevvySecurityDir()
    {
        FString OverridePath = FPlatformMisc::GetEnvironmentVariable(TEXT("REVVY_SECURITY_DIR"));
        if (!OverridePath.IsEmpty())
        {
            return ExpandUserSecurityPath(OverridePath);
        }

#if PLATFORM_WINDOWS
        FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
        if (!LocalAppData.IsEmpty())
        {
            return ExpandUserSecurityPath(LocalAppData / TEXT("Revvy/Security"));
        }

        FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
        if (UserProfile.IsEmpty())
        {
            UserProfile = FPlatformProcess::UserDir();
        }
        return ExpandUserSecurityPath(UserProfile / TEXT("AppData/Local/Revvy/Security"));
#else
        FString XdgStateHome = FPlatformMisc::GetEnvironmentVariable(TEXT("XDG_STATE_HOME"));
        if (!XdgStateHome.IsEmpty())
        {
            return ExpandUserSecurityPath(XdgStateHome / TEXT("Revvy/Security"));
        }

        FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
        if (UserProfile.IsEmpty())
        {
            UserProfile = FPlatformProcess::UserDir();
        }
        return ExpandUserSecurityPath(UserProfile / TEXT(".local/state/Revvy/Security"));
#endif
    }

    FString GetBridgeAuthPath()
    {
        FString OverridePath = FPlatformMisc::GetEnvironmentVariable(TEXT("REVVY_BRIDGE_AUTH_FILE"));
        if (!OverridePath.IsEmpty())
        {
            return ExpandUserSecurityPath(OverridePath);
        }
        return GetRevvySecurityDir() / TEXT("bridge-auth.json");
    }

    FString GetLegacyBackendCapabilityPath()
    {
        FString OverridePath = FPlatformMisc::GetEnvironmentVariable(TEXT("REVVY_UE_CAPABILITY_FILE"));
        if (!OverridePath.IsEmpty())
        {
            return ExpandUserSecurityPath(OverridePath);
        }
        return GetRevvySecurityDir() / TEXT("ue-backend.json");
    }

    bool ApplyPrivateAcl(const FString& Path, bool bDirectory)
    {
#if PLATFORM_WINDOWS
        const DWORD Attributes = GetFileAttributesW(*Path);
        if (Attributes == INVALID_FILE_ATTRIBUTES
            || (Attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
            || (((Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != bDirectory))
        {
            UE_LOG(LogMCPServer, Error,
                TEXT("Refusing missing or linked Revvy Security path: %s"),
                *Path);
            return false;
        }

        HANDLE TokenHandle = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &TokenHandle))
        {
            UE_LOG(LogMCPServer, Error, TEXT("OpenProcessToken failed for Revvy Security ACL (%lu)"),
                static_cast<uint32>(GetLastError()));
            return false;
        }

        DWORD TokenInfoSize = 0;
        GetTokenInformation(TokenHandle, TokenUser, nullptr, 0, &TokenInfoSize);
        if (TokenInfoSize == 0)
        {
            CloseHandle(TokenHandle);
            UE_LOG(LogMCPServer, Error, TEXT("GetTokenInformation size failed for Revvy Security ACL"));
            return false;
        }

        TArray<uint8> TokenInfo;
        TokenInfo.SetNumUninitialized(static_cast<int32>(TokenInfoSize));
        if (!GetTokenInformation(
                TokenHandle,
                TokenUser,
                TokenInfo.GetData(),
                TokenInfoSize,
                &TokenInfoSize))
        {
            const DWORD Error = GetLastError();
            CloseHandle(TokenHandle);
            UE_LOG(LogMCPServer, Error, TEXT("GetTokenInformation failed for Revvy Security ACL (%lu)"),
                static_cast<uint32>(Error));
            return false;
        }
        CloseHandle(TokenHandle);

        const TOKEN_USER* CurrentUser =
            reinterpret_cast<const TOKEN_USER*>(TokenInfo.GetData());
        LPWSTR RawSid = nullptr;
        if (!ConvertSidToStringSidW(CurrentUser->User.Sid, &RawSid))
        {
            UE_LOG(LogMCPServer, Error, TEXT("ConvertSidToStringSid failed for Revvy Security ACL (%lu)"),
                static_cast<uint32>(GetLastError()));
            return false;
        }
        const FString UserSid(RawSid);
        LocalFree(RawSid);

        const FString InheritanceFlags = bDirectory ? TEXT("OICI") : TEXT("");
        const FString Sddl = FString::Printf(
            TEXT("D:P(A;%s;FA;;;SY)(A;%s;FA;;;BA)(A;%s;FA;;;%s)"),
            *InheritanceFlags,
            *InheritanceFlags,
            *InheritanceFlags,
            *UserSid);

        PSECURITY_DESCRIPTOR Descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                *Sddl,
                SDDL_REVISION_1,
                &Descriptor,
                nullptr))
        {
            UE_LOG(LogMCPServer, Error,
                TEXT("Could not build Revvy Security descriptor (%lu)"),
                static_cast<uint32>(GetLastError()));
            return false;
        }

        BOOL bDaclPresent = false;
        BOOL bDaclDefaulted = false;
        PACL Dacl = nullptr;
        const bool bHasDacl = GetSecurityDescriptorDacl(
            Descriptor,
            &bDaclPresent,
            &Dacl,
            &bDaclDefaulted) != 0
            && bDaclPresent
            && Dacl != nullptr;
        DWORD Result = ERROR_INVALID_SECURITY_DESCR;
        if (bHasDacl)
        {
            Result = SetNamedSecurityInfoW(
                const_cast<LPWSTR>(*Path),
                SE_FILE_OBJECT,
                OWNER_SECURITY_INFORMATION
                    | DACL_SECURITY_INFORMATION
                    | PROTECTED_DACL_SECURITY_INFORMATION,
                CurrentUser->User.Sid,
                nullptr,
                Dacl,
                nullptr);
        }
        LocalFree(Descriptor);
        if (!bHasDacl || Result != ERROR_SUCCESS)
        {
            UE_LOG(LogMCPServer, Error,
                TEXT("Could not protect Revvy Security path %s (%lu)"),
                *Path,
                static_cast<uint32>(Result));
            return false;
        }
        return true;
#elif PLATFORM_UNIX || PLATFORM_MAC
        const FTCHARToUTF8 NativePath(*Path);
        struct stat PathInfo = {};
        if (::lstat(NativePath.Get(), &PathInfo) != 0
            || S_ISLNK(PathInfo.st_mode)
            || PathInfo.st_uid != ::geteuid()
            || (bDirectory && !S_ISDIR(PathInfo.st_mode))
            || (!bDirectory && !S_ISREG(PathInfo.st_mode))
            || (!bDirectory && PathInfo.st_nlink != 1))
        {
            UE_LOG(LogMCPServer, Error,
                TEXT("Refusing missing, linked, or foreign-owned Revvy Security path: %s"),
                *Path);
            return false;
        }

        const mode_t RequiredMode = bDirectory ? 0700 : 0600;
        if (::chmod(NativePath.Get(), RequiredMode) != 0)
        {
            UE_LOG(LogMCPServer, Error,
                TEXT("Could not protect Revvy Security path: %s"),
                *Path);
            return false;
        }

        PathInfo = {};
        if (::lstat(NativePath.Get(), &PathInfo) != 0
            || S_ISLNK(PathInfo.st_mode)
            || PathInfo.st_uid != ::geteuid()
            || (bDirectory && !S_ISDIR(PathInfo.st_mode))
            || (!bDirectory && (!S_ISREG(PathInfo.st_mode) || PathInfo.st_nlink != 1))
            || (PathInfo.st_mode & 0777) != RequiredMode)
        {
            UE_LOG(LogMCPServer, Error,
                TEXT("Revvy Security path did not retain owner-only permissions: %s"),
                *Path);
            return false;
        }
        return true;
#else
        UE_LOG(LogMCPServer, Error,
            TEXT("Revvy Security owner-only permission enforcement is unavailable; refusing bridge authentication storage"));
        return false;
#endif
    }

    bool TryLoadBridgeHmacKey(FString& OutKey)
    {
        OutKey.Empty();
        const FString Path = GetBridgeAuthPath();
        if (!FPaths::FileExists(Path)
            || !ApplyPrivateAcl(FPaths::GetPath(Path), true)
            || !ApplyPrivateAcl(Path, false))
        {
            return false;
        }

        FString JsonString;
        if (!FFileHelper::LoadFileToString(JsonString, *Path))
        {
            return false;
        }
        TSharedPtr<FJsonObject> JsonObject;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
        if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
        {
            return false;
        }

        double Version = 0.0;
        FString Algorithm;
        const bool bValidDocument = JsonObject->TryGetNumberField(TEXT("version"), Version)
            && Version == 2.0
            && JsonObject->TryGetStringField(TEXT("algorithm"), Algorithm)
            && Algorithm == TEXT("hmac-sha1-exact-v2")
            && JsonObject->TryGetStringField(TEXT("bridge_hmac_key"), OutKey);
        if (!bValidDocument)
        {
            OutKey.Empty();
            return false;
        }
        OutKey.TrimStartAndEndInline();
        return OutKey.Len() >= 32;
    }

    bool WriteBridgeHmacKey(const FString& Key)
    {
        if (Key.Len() < 32)
        {
            return false;
        }
        const FString Path = GetBridgeAuthPath();
        const FString Directory = FPaths::GetPath(Path);
        IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);
        if (!FPaths::DirectoryExists(Directory) || !ApplyPrivateAcl(Directory, true))
        {
            return false;
        }
        if (FPaths::FileExists(Path) && !ApplyPrivateAcl(Path, false))
        {
            return false;
        }

        TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
        JsonObject->SetNumberField(TEXT("version"), 2.0);
        JsonObject->SetStringField(TEXT("algorithm"), TEXT("hmac-sha1-exact-v2"));
        JsonObject->SetStringField(TEXT("bridge_hmac_key"), Key);
        FString JsonString;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
        FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

        const FString TemporaryPath = FString::Printf(
            TEXT("%s.%s.tmp"),
            *Path,
            *FGuid::NewGuid().ToString(EGuidFormats::Digits));
        if (!FFileHelper::SaveStringToFile(
                JsonString,
                *TemporaryPath,
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            || !ApplyPrivateAcl(TemporaryPath, false))
        {
            IFileManager::Get().Delete(*TemporaryPath, false, true);
            return false;
        }

#if PLATFORM_WINDOWS
        const bool bReplaced = ::MoveFileExW(
            *TemporaryPath,
            *Path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        const bool bReplaced = std::rename(
            TCHAR_TO_UTF8(*TemporaryPath),
            TCHAR_TO_UTF8(*Path)) == 0;
#endif
        if (!bReplaced)
        {
            IFileManager::Get().Delete(*TemporaryPath, false, true);
            return false;
        }
        if (!ApplyPrivateAcl(Path, false))
        {
            IFileManager::Get().Delete(*Path, false, true);
            return false;
        }
        return true;
    }

    bool EnsureBridgeHmacKey(FString& OutKey)
    {
        if (TryLoadBridgeHmacKey(OutKey))
        {
            return true;
        }

        const FString AuthPath = GetBridgeAuthPath();
        const FString Directory = FPaths::GetPath(AuthPath);
        IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);
        if (!FPaths::DirectoryExists(Directory) || !ApplyPrivateAcl(Directory, true))
        {
            return false;
        }

        const FString LockPath = AuthPath + TEXT(".lock");
        const double Deadline = FPlatformTime::Seconds() + 5.0;
        bool bOwnsLock = false;
        while (!bOwnsLock && FPlatformTime::Seconds() < Deadline)
        {
            TUniquePtr<FArchive> LockFile(IFileManager::Get().CreateFileWriter(
                *LockPath,
                FILEWRITE_NoReplaceExisting | FILEWRITE_Silent));
            if (LockFile.IsValid())
            {
                if (!ApplyPrivateAcl(LockPath, false))
                {
                    LockFile.Reset();
                    IFileManager::Get().Delete(*LockPath, false, true);
                    return false;
                }

                const FString LockDescription = FString::Printf(
                    TEXT("%u %s\n"),
                    FPlatformProcess::GetCurrentProcessId(),
                    *FDateTime::UtcNow().ToIso8601());
                const FTCHARToUTF8 LockUtf8(*LockDescription);
                LockFile->Serialize(
                    const_cast<ANSICHAR*>(LockUtf8.Get()),
                    LockUtf8.Length());
                const bool bLockWriteFailed = LockFile->IsError();
                LockFile->Close();
                LockFile.Reset();
                if (bLockWriteFailed)
                {
                    IFileManager::Get().Delete(*LockPath, false, true);
                    return false;
                }
                bOwnsLock = true;
                break;
            }

            if (FPaths::FileExists(LockPath))
            {
                const FDateTime LockTimestamp = IFileManager::Get().GetTimeStamp(*LockPath);
                const FTimespan LockAge = FDateTime::UtcNow() - LockTimestamp;
                if (LockTimestamp.GetTicks() > 0
                    && LockAge.GetTotalSeconds() >= 30.0
                    && ApplyPrivateAcl(LockPath, false))
                {
                    IFileManager::Get().Delete(*LockPath, false, true);
                    continue;
                }
            }
            FPlatformProcess::Sleep(0.05f);
        }

        if (!bOwnsLock)
        {
            UE_LOG(LogMCPServer, Error,
                TEXT("Timed out waiting for the Revvy bridge authentication lock"));
            return false;
        }

        // A peer may have provisioned the key while this process waited.
        bool bSucceeded = TryLoadBridgeHmacKey(OutKey);
        if (!bSucceeded)
        {
            const FString Candidate =
                FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower()
                + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
            bSucceeded = WriteBridgeHmacKey(Candidate)
                && TryLoadBridgeHmacKey(OutKey)
                && OutKey == Candidate;
        }

        IFileManager::Get().Delete(*LockPath, false, true);
        return bSucceeded;
    }

    void RetireLegacyBackendCapability()
    {
        TArray<FString> LegacyPaths = {
            GetRevvySecurityDir() / TEXT("ue-backend.json")
        };
        const FString ConfiguredLegacyPath = GetLegacyBackendCapabilityPath();
        if (!FPaths::IsSamePath(ConfiguredLegacyPath, LegacyPaths[0]))
        {
            LegacyPaths.Add(ConfiguredLegacyPath);
        }

        for (const FString& LegacyPath : LegacyPaths)
        {
            if (FPaths::IsSamePath(LegacyPath, GetBridgeAuthPath()))
            {
                continue;
            }

            FString JsonString;
            TSharedPtr<FJsonObject> JsonObject;
            if (!FFileHelper::LoadFileToString(JsonString, *LegacyPath))
            {
                continue;
            }
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
            FString LegacyToken;
            if (FJsonSerializer::Deserialize(Reader, JsonObject)
                && JsonObject.IsValid()
                && JsonObject->TryGetStringField(TEXT("ue_backend_token"), LegacyToken))
            {
                IFileManager::Get().Delete(*LegacyPath, false, true);
            }
        }
    }

    void ScrubLegacyBearerToken(const FString& JsonPath)
    {
        FString JsonString;
        if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
        {
            return;
        }
        TSharedPtr<FJsonObject> JsonObject;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
        if (!FJsonSerializer::Deserialize(Reader, JsonObject)
            || !JsonObject.IsValid()
            || (!JsonObject->HasField(TEXT("bearer_token"))
                && !JsonObject->HasField(TEXT("full_access_token"))))
        {
            return;
        }
        JsonObject->RemoveField(TEXT("bearer_token"));
        JsonObject->RemoveField(TEXT("full_access_token"));
        FString Sanitized;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Sanitized);
        FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
        SaveStringToFileAtomically(Sanitized, JsonPath);
    }

    FString JsonRpcIdToString(const TSharedPtr<FJsonValue>& RequestId)
    {
        if (!RequestId.IsValid())
        {
            return TEXT("<notification>");
        }
        switch (RequestId->Type)
        {
            case EJson::String: return RequestId->AsString();
            case EJson::Number: return FString::SanitizeFloat(RequestId->AsNumber());
            case EJson::Null:   return TEXT("null");
            default:            return TEXT("<invalid>");
        }
    }
}

FMCPServer::FMCPServer()
    : BridgeInstanceId(FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower())
    , NextEventId(0)
{
}

FMCPServer::~FMCPServer()
{
    Shutdown();
}

FMCPServer& FMCPServer::Get()
{
    if (!Instance.IsValid())
    {
        Instance = MakeShared<FMCPServer>();
    }
    return *Instance;
}

void FMCPServer::Initialize()
{
    LoadConfig();

    // The private-hop HMAC key is intentionally independent from every former
    // wire bearer. Never migrate ue_backend_token or a public runtime bearer.
    if (!EnsureBridgeHmacKey(Config.BridgeHmacKey))
    {
        Config.BridgeHmacKey.Empty();
        UE_LOG(LogMCPServer, Error,
            TEXT("SECURITY: bridge HMAC provisioning failed; editor backend will remain offline"));
    }
    else
    {
        RetireLegacyBackendCapability();
        UE_LOG(LogMCPServer, Log,
            TEXT("Protected exact-byte bridge authentication is ready"));
    }

    GConfig->RemoveKey(TEXT("Revvy.MCPServer"), TEXT("ApiKey"), GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
    if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Revvy")))
    {
        ScrubLegacyBearerToken(
            FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("revvy-proxy.json")));
    }
    ScrubLegacyBearerToken(GetProxyRuntimeConfigPath());

    // Public config carries only an ephemeral Safe/read-only capability.
    Config.ReadOnlyApiKey =
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower()
        + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();

    // Before anything spawns the proxy, so it and the CLI sessions share one bearer.
    EnsureEditorFullAccessToken();

    EnsureProjectMcpConfig();

    UE_LOG(LogMCPServer, Log, TEXT("MCP Server initialized - Enabled: %s, Port: %d, Bridge auth: %s"),
        Config.bEnabled ? TEXT("Yes") : TEXT("No"),
        Config.Port,
        Config.BridgeHmacKey.IsEmpty() ? TEXT("unavailable") : TEXT("ready"));

    // Auto-start MCP server if enabled
    if (Config.bEnabled)
    {
        Start();
    }
    else
    {
        // Do not touch ue_port while offline; only clear any cleanly stale
        // bridge_running marker left by a previous enabled session.
        WriteProxyConfigJson();
    }

    // Auto-start proxy if both proxy-enabled and auto-start are configured
    if (GetProxyEnabledFromConfig() && GetProxyAutoStartFromConfig())
    {
        StartProxy();
    }
}

void FMCPServer::Shutdown()
{
    StopServer();
    // The proxy is an editor-independent user process. Keeping it alive preserves
    // external MCP sessions while Unreal Editor restarts.
    UE_LOG(LogMCPServer, Log, TEXT("MCP Server shutdown; standalone proxy left running"));
}

bool FMCPServer::Start()
{
    if (bIsRunning)
    {
        UE_LOG(LogMCPServer, Warning, TEXT("MCP Server already running"));
        return true;
    }

    // Re-validate Revvy API key each time the server starts (picks up any key changes from settings)
    ValidateRevvyApiKeyAsync();

    if (Config.BridgeHmacKey.Len() < 32 || Config.ReadOnlyApiKey.Len() < 32)
    {
        UE_LOG(LogMCPServer, Error,
            TEXT("SECURITY: Revvy MCP server refused to start because bridge authentication is unavailable"));
        WriteProxyConfigJson();
        return false;
    }

    UE_LOG(LogMCPServer, Log, TEXT("Starting MCP Server on port %d..."), Config.Port);
    
    // Create TCP listener socket
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogMCPServer, Error, TEXT("Failed to get socket subsystem"));
        WriteProxyConfigJson();
        return false;
    }
    
    // Bind to localhost only for security (prevent DNS rebinding attacks)
    FIPv4Address LocalAddress;
    FIPv4Address::Parse(TEXT("127.0.0.1"), LocalAddress);
    
    TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
    Addr->SetIp(LocalAddress.Value);
    Addr->SetPort(Config.Port);
    
    ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MCP Server"), false);
    if (!ListenerSocket)
    {
        UE_LOG(LogMCPServer, Error, TEXT("Failed to create listener socket"));
        WriteProxyConfigJson();
        return false;
    }
    
    // Set socket options
    ListenerSocket->SetReuseAddr(true);
    ListenerSocket->SetNonBlocking(true);
    
    // Bind and listen
    if (!ListenerSocket->Bind(*Addr))
    {
        UE_LOG(LogMCPServer, Error, TEXT("Failed to bind to port %d - is another process using it?"), Config.Port);
        SocketSubsystem->DestroySocket(ListenerSocket);
        ListenerSocket = nullptr;
        WriteProxyConfigJson();
        return false;
    }
    
    if (!ListenerSocket->Listen(8))
    {
        UE_LOG(LogMCPServer, Error, TEXT("Failed to listen on socket"));
        SocketSubsystem->DestroySocket(ListenerSocket);
        ListenerSocket = nullptr;
        WriteProxyConfigJson();
        return false;
    }

    // A nonce and signature for a previous listener lifetime must never be
    // reusable after a restart on the same port.
    BridgeInstanceId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    BridgeReplayCache.Reset();
    
    bIsRunning = true;
    bShouldStop = false;
    
    // Start server thread
    ServerThread.Reset(FRunnableThread::Create(this, TEXT("MCPServerThread"), 0, TPri_Normal));
    if (!ServerThread)
    {
        UE_LOG(LogMCPServer, Error, TEXT("Failed to create server thread"));
        SocketSubsystem->DestroySocket(ListenerSocket);
        ListenerSocket = nullptr;
        bIsRunning = false;
        WriteProxyConfigJson();
        return false;
    }
    
    // Register tick delegate for processing requests on game thread
    TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
        {
            ProcessPendingRequests();
            return true; // Keep ticking
        }),
        0.016f // ~60 Hz
    );
    
    UE_LOG(LogMCPServer, Log, TEXT("MCP Server started at %s"), *GetServerUrl());

    // Publish the selected editor port only after bind, listen, and thread
    // creation all succeeded. A failed start can therefore never advertise an
    // unbound port as a live bridge.
    WriteProxyConfigJson();

    // Export manifest so the proxy can serve tool definitions when UE is not running
    ExportToolManifest();

    return true;
}

void FMCPServer::StopServer()
{
    if (!bIsRunning)
    {
        return;
    }
    
    UE_LOG(LogMCPServer, Log, TEXT("Stopping MCP Server..."));
    
    bShouldStop = true;
    bIsRunning = false;
    
    // Remove tick delegate
    if (TickDelegateHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
        TickDelegateHandle.Reset();
    }
    
    // Close listener socket FIRST to unblock the WaitForPendingConnection call in the thread,
    // but do NOT destroy it yet — the server thread may still be touching it.
    if (ListenerSocket)
    {
        ListenerSocket->Close();
    }

    // Wake socket-thread requests before waiting for the thread. The game thread
    // owns tool execution and cannot service the queue while Shutdown is blocking.
    TSharedPtr<FMCPPendingRequest, ESPMode::ThreadSafe> PendingRequest;
    while (PendingRequests.Dequeue(PendingRequest))
    {
        if (PendingRequest.IsValid())
        {
            PendingRequest->bCancelled = true;
            PendingRequest->CompletionEvent->Trigger();
        }
    }

    // Now wait for thread to finish (it should exit quickly since socket is closed)
    if (ServerThread)
    {
        ServerThread->WaitForCompletion();
        ServerThread.Reset();
    }

    // Thread is gone — safe to destroy the socket
    if (ListenerSocket)
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket);
        ListenerSocket = nullptr;
    }

    // Clear sessions
    FScopeLock Lock(&SessionLock);
    ActiveSessions.Empty();
    
    // Clean up SSE connections
    {
        FScopeLock SSELockGuard(&SSELock);
        for (auto& Connection : SSEConnections)
        {
            if (Connection->ClientSocket)
            {
                Connection->ClientSocket->Close();
                ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Connection->ClientSocket);
            }
        }
        SSEConnections.Empty();
    }
    
    // Keep the persistent proxy's routing document honest after the listener is gone.
    WriteProxyConfigJson();

    UE_LOG(LogMCPServer, Log, TEXT("MCP Server stopped"));
}

FString FMCPServer::GetServerUrl() const
{
    return FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Config.Port);
}

// ============ FRunnable Interface ============

bool FMCPServer::Init()
{
    return true;
}

uint32 FMCPServer::Run()
{
    UE_LOG(LogMCPServer, Log, TEXT("MCP Server thread started"));
    
    while (!bShouldStop)
    {
        // Check if socket is still valid (may have been closed during shutdown)
        if (!ListenerSocket)
        {
            break;
        }
        
        bool bHasPendingConnection = false;
        if (ListenerSocket->WaitForPendingConnection(bHasPendingConnection, FTimespan::FromMilliseconds(100)))
        {
            if (bHasPendingConnection && !bShouldStop)
            {
                FSocket* ClientSocket = ListenerSocket->Accept(TEXT("MCP Client"));
                if (ClientSocket)
                {
                    UE_LOG(LogMCPServer, Log, TEXT("MCP: New connection accepted"));
                    HandleConnection(ClientSocket);
                    UE_LOG(LogMCPServer, Verbose, TEXT("MCP: Connection handling completed"));
                }
            }
        }
        
        // Small sleep to prevent busy-waiting
        FPlatformProcess::Sleep(0.001f);
    }
    
    UE_LOG(LogMCPServer, Log, TEXT("MCP Server thread exiting"));
    return 0;
}

void FMCPServer::Exit()
{
    // Nothing to cleanup here - Stop() handles it
}

// ============ HTTP Handling ============

namespace
{
// Owns one client socket for the span of a single HTTP exchange. Every
// return path in HandleConnection used to carry its own Close+DestroySocket
// pair - nine of them - and a new path that forgot the pair leaked the
// socket. The SSE stream is the one legitimate handoff; it takes Release().
class FScopedClientSocket
{
public:
    explicit FScopedClientSocket(FSocket* InSocket) : Socket(InSocket) {}
    ~FScopedClientSocket()
    {
        if (Socket)
        {
            Socket->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
        }
    }
    FScopedClientSocket(const FScopedClientSocket&) = delete;
    FScopedClientSocket& operator=(const FScopedClientSocket&) = delete;
    FSocket* Get() const { return Socket; }
    /** Hand the socket to a longer-lived owner without closing it. */
    FSocket* Release() { FSocket* Out = Socket; Socket = nullptr; return Out; }
private:
    FSocket* Socket = nullptr;
};
}

bool FMCPServer::HandleConnection(FSocket* InClientSocket)
{
    if (!InClientSocket)
    {
        return false;
    }
    FScopedClientSocket Client(InClientSocket);
    FSocket* const ClientSocket = Client.Get();
    
    FString Method, Path;
    TMap<FString, FString> Headers;
    FString Body;
    TArray<uint8> BodyBytes;
    
    if (!ParseHttpRequest(ClientSocket, Method, Path, Headers, Body, BodyBytes))
    {
        SendHttpResponse(ClientSocket, 400, TEXT("Bad Request"), TEXT("text/plain"), TEXT("Invalid HTTP request"));
        return false;
    }
    
    UE_LOG(LogMCPServer, Verbose, TEXT("MCP Request: %s %s"), *Method, *Path);
    
    FRevvyBridgeAuthContext SignedAuthContext;
    const FRevvyBridgeAuthContext* ResponseAuthContext = nullptr;
    EMCPAccessScope AccessScope = EMCPAccessScope::None;

    if (Method == TEXT("POST") && FRevvyBridgeAuth::HasAnySignedRequestHeader(Headers))
    {
        FString AuthError;
        if (!FRevvyBridgeAuth::TryAuthenticateRequest(
                Headers,
                Config.BridgeHmacKey,
                BridgeInstanceId,
                Config.Port,
                BodyBytes,
                FDateTime::UtcNow().ToUnixTimestamp(),
                SignedAuthContext,
                AuthError))
        {
            UE_LOG(LogMCPServer, Warning,
                TEXT("Rejected unauthenticated signed bridge POST: %s"),
                *AuthError);
            SendHttpResponse(
                ClientSocket,
                401,
                TEXT("Unauthorized"),
                TEXT("text/plain"),
                TEXT("Unauthorized editor bridge request"));
            return false;
        }

        // The HMAC is valid at this point, so even the replay rejection can be
        // authenticated by the proxy as a genuine response from this listener.
        ResponseAuthContext = &SignedAuthContext;
        if (!BridgeReplayCache.TryConsume(
                SignedAuthContext.BridgeInstance,
                SignedAuthContext.Nonce,
                FDateTime::UtcNow().ToUnixTimestamp()))
        {
            SendHttpResponse(
                ClientSocket,
                401,
                TEXT("Unauthorized"),
                TEXT("text/plain"),
                TEXT("Proxy request nonce was already used"),
                TMap<FString, FString>(),
                ResponseAuthContext);
            return false;
        }

        AccessScope = SignedAuthContext.Scope == ERevvyBridgeAuthScope::Full
            ? EMCPAccessScope::Full
            : EMCPAccessScope::ReadOnly;
    }
    else
    {
        if (Method == TEXT("POST"))
        {
            // Ping is the sole intentionally public POST. It reveals only the
            // listener identity needed before the proxy can bind a signature.
            if (!FRevvyBridgeAuth::IsPublicPing(Body))
            {
                SendHttpResponse(
                    ClientSocket,
                    401,
                    TEXT("Unauthorized"),
                    TEXT("text/plain"),
                    TEXT("Signed proxy authentication is required"));
                return false;
            }
            AccessScope = EMCPAccessScope::ReadOnly;
        }
        else
        {
            AccessScope = ValidateApiKey(Headers);
        }
    }

    // Authenticate first so every response to a valid signed request, including
    // a routing error, can be verified by the proxy.
    if (!Path.StartsWith(TEXT("/mcp")))
    {
        SendHttpResponse(
            ClientSocket,
            404,
            TEXT("Not Found"),
            TEXT("text/plain"),
            TEXT("Not Found"),
            TMap<FString, FString>(),
            ResponseAuthContext);
        return false;
    }
    
    // Rate limit per client IP before any JSON-RPC or editor processing.
    {
        FString ClientKey = TEXT("unknown");
        TSharedRef<FInternetAddr> PeerAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        if (ClientSocket->GetPeerAddress(*PeerAddr))
        {
            ClientKey = PeerAddr->ToString(/*bAppendPort=*/false);
        }
        if (!CheckRateLimit(ClientKey))
        {
            UE_LOG(LogMCPServer, Warning, TEXT("Rate limit exceeded for %s"), *ClientKey);
            SendHttpResponse(
                ClientSocket,
                429,
                TEXT("Too Many Requests"),
                TEXT("text/plain"),
                TEXT("Rate limit exceeded (600 requests/min)"),
                TMap<FString, FString>(),
                ResponseAuthContext);
            return false;
        }
    }

    // Validate Origin header for security
    if (!ValidateOrigin(Headers))
    {
        SendHttpResponse(
            ClientSocket,
            403,
            TEXT("Forbidden"),
            TEXT("text/plain"),
            TEXT("Invalid Origin"),
            TMap<FString, FString>(),
            ResponseAuthContext);
        return false;
    }

    if (AccessScope == EMCPAccessScope::None)
    {
        SendHttpResponse(ClientSocket, 401, TEXT("Unauthorized"), TEXT("text/plain"), TEXT("Invalid or missing API key"));
        return false;
    }
    
    // Validate MCP-Protocol-Version header (for non-initialize requests)
    FString SessionId = Headers.FindRef(TEXT("mcp-session-id"));
    if (!SessionId.IsEmpty() && !ValidateProtocolVersion(Headers))
    {
        SendHttpResponse(
            ClientSocket,
            400,
            TEXT("Bad Request"),
            TEXT("text/plain"),
            TEXT("Invalid or unsupported MCP-Protocol-Version"),
            TMap<FString, FString>(),
            ResponseAuthContext);
        return false;
    }
    
    // Handle different HTTP methods per MCP spec
    if (Method == TEXT("POST"))
    {
        UE_LOG(LogMCPServer, Log, TEXT("MCP POST: Processing JSON-RPC request"));

        // Nudge direct connections toward the proxy (fires once, on first initialize)
        if (!Headers.Contains(TEXT("x-revvy-proxy")) && Body.Contains(TEXT("\"initialize\"")))
        {
            ShowProxyNudgeIfNeeded();
        }

        // Route the complete request through the game-thread ticker. This keeps
        // ToolRegistry, session state, and all editor APIs on their owning thread.
        TSharedPtr<FMCPPendingRequest, ESPMode::ThreadSafe> PendingRequest =
            MakeShared<FMCPPendingRequest, ESPMode::ThreadSafe>();
        PendingRequest->Body = Body;
        PendingRequest->SessionId = SessionId;
        PendingRequest->AccessScope = AccessScope;
        PendingRequests.Enqueue(PendingRequest);

        bool bCompleted = false;
        while (!bShouldStop)
        {
            if (PendingRequest->CompletionEvent->Wait(50))
            {
                bCompleted = true;
                break;
            }
        }

        bool bIsNotification = false;
        FString Response;
        if (bCompleted && !PendingRequest->bCancelled)
        {
            SessionId = PendingRequest->SessionId;
            bIsNotification = PendingRequest->bIsNotification;
            Response = MoveTemp(PendingRequest->Response);
        }
        else
        {
            PendingRequest->bCancelled = true;
            Response = BuildJsonRpcError(nullptr, -32000,
                TEXT("Unreal Editor is shutting down; inspect current state after Revvy reconnects"));
        }
        
        UE_LOG(LogMCPServer, Log, TEXT("MCP POST: Request processed, notification=%d, response length=%d"), 
            bIsNotification, Response.Len());
        
        if (bIsNotification)
        {
            // Notifications return 202 Accepted with no body
            SendHttpResponse(
                ClientSocket,
                202,
                TEXT("Accepted"),
                TEXT(""),
                TEXT(""),
                TMap<FString, FString>(),
                ResponseAuthContext);
        }
        else
        {
            // Build response headers
            TMap<FString, FString> ResponseHeaders;
            
            // If this was an initialize request and we generated a session ID, include it
            if (!SessionId.IsEmpty())
            {
                ResponseHeaders.Add(TEXT("Mcp-Session-Id"), SessionId);
            }
            
            // Add CORS headers (echoes validated Origin — never "*")
            AppendCorsHeaders(Headers, ResponseHeaders);
            
            // Check if client wants SSE response
            if (AcceptsSSE(Headers) && ResponseAuthContext == nullptr)
            {
                // Send as SSE stream (single event then close for POST)
                SendSSEResponse(ClientSocket, SessionId);
                int32 EventId = ++NextEventId;
                SendSSEEvent(ClientSocket, Response, EventId);
                // Note: Don't close socket here - let client close or we send more events
                // For POST, we close after sending the response
                return true;
            }
            else
            {
                UE_LOG(LogMCPServer, Verbose, TEXT("MCP POST: Sending HTTP response"));
                SendHttpResponse(
                    ClientSocket,
                    200,
                    TEXT("OK"),
                    TEXT("application/json"),
                    Response,
                    ResponseHeaders,
                    ResponseAuthContext);
                UE_LOG(LogMCPServer, Verbose, TEXT("MCP POST: Response sent"));
            }
        }
    }
    else if (Method == TEXT("GET"))
    {
        // GET opens SSE stream for server-to-client messages
        if (!AcceptsSSE(Headers))
        {
            SendHttpResponse(ClientSocket, 406, TEXT("Not Acceptable"), TEXT("text/plain"), 
                TEXT("GET requests must accept text/event-stream"));
            return false;
        }
        
        // Handle SSE stream request
        return HandleSSERequest(Client.Release(), Headers);
    }
    else if (Method == TEXT("DELETE"))
    {
        // Session termination (SessionId already retrieved above)
        if (!SessionId.IsEmpty())
        {
            FScopeLock Lock(&SessionLock);
            ActiveSessions.Remove(SessionId);
            UE_LOG(LogMCPServer, Log, TEXT("Session terminated: %s"), *SessionId);
        }
        SendHttpResponse(ClientSocket, 200, TEXT("OK"), TEXT("text/plain"), TEXT("Session terminated"));
    }
    else if (Method == TEXT("OPTIONS"))
    {
        // CORS preflight (echoes validated Origin — never "*")
        TMap<FString, FString> CorsHeaders;
        AppendCorsHeaders(Headers, CorsHeaders);
        CorsHeaders.Add(TEXT("Access-Control-Allow-Methods"), TEXT("GET, POST, DELETE, OPTIONS"));
        CorsHeaders.Add(TEXT("Access-Control-Max-Age"), TEXT("86400"));
        SendHttpResponse(ClientSocket, 204, TEXT("No Content"), TEXT(""), TEXT(""), CorsHeaders);
    }
    else
    {
        SendHttpResponse(ClientSocket, 405, TEXT("Method Not Allowed"), TEXT("text/plain"), TEXT("Method not allowed"));
    }
    
    return true;
}

bool FMCPServer::ParseHttpRequest(FSocket* Socket, FString& OutMethod, FString& OutPath,
                                   TMap<FString, FString>& OutHeaders, FString& OutBody,
                                   TArray<uint8>& OutBodyBytes)
{
    if (!Socket)
    {
        return false;
    }
    
    TArray<uint8> Buffer;
    Buffer.SetNumUninitialized(8192);
    TArray<uint8> RawRequest;
    RawRequest.Reserve(8192);

    Socket->SetNonBlocking(false);
    int32 ActualReceiveBufferSize = 0;
    Socket->SetReceiveBufferSize(8192, ActualReceiveBufferSize);

    const int32 MaxSize = 1024 * 1024; // 1MB max request
    int32 EmptyReadCount = 0;
    const int32 MaxEmptyReads = 3; // Give up after 3 empty reads (15 seconds total)

    int32 HeaderEndByte = INDEX_NONE;
    int32 ContentLength = 0;
    int32 ExpectedTotalBytes = INDEX_NONE;

    auto FindHeaderEnd = [](const TArray<uint8>& Data) -> int32
    {
        for (int32 Index = 0; Index + 3 < Data.Num(); ++Index)
        {
            if (Data[Index] == '\r' && Data[Index + 1] == '\n'
                && Data[Index + 2] == '\r' && Data[Index + 3] == '\n')
            {
                return Index;
            }
        }
        return INDEX_NONE;
    };

    while (RawRequest.Num() < MaxSize && EmptyReadCount < MaxEmptyReads)
    {
        bool bHadData = Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0));
        
        if (!bHadData)
        {
            EmptyReadCount++;
            UE_LOG(LogMCPServer, Warning, TEXT("ParseHttpRequest: No data after wait (attempt %d/%d)"), EmptyReadCount, MaxEmptyReads);
            continue;
        }
        
        int32 BytesRead = 0;
        if (Socket->Recv(Buffer.GetData(), Buffer.Num() - 1, BytesRead))
        {
            if (BytesRead > 0)
            {
                EmptyReadCount = 0;
                RawRequest.Append(Buffer.GetData(), BytesRead);
                if (RawRequest.Num() > MaxSize)
                {
                    return false;
                }

                if (HeaderEndByte == INDEX_NONE)
                {
                    HeaderEndByte = FindHeaderEnd(RawRequest);
                }

                if (HeaderEndByte != INDEX_NONE && ExpectedTotalBytes == INDEX_NONE)
                {
                    FUTF8ToTCHAR HeaderConverter(
                        reinterpret_cast<const ANSICHAR*>(RawRequest.GetData()),
                        HeaderEndByte);
                    FString HeaderPart(HeaderConverter.Length(), HeaderConverter.Get());
                    TArray<FString> HeaderLines;
                    HeaderPart.ParseIntoArray(HeaderLines, TEXT("\r\n"));
                    for (const FString& Line : HeaderLines)
                    {
                        if (Line.StartsWith(TEXT("Content-Length:"), ESearchCase::IgnoreCase))
                        {
                            FString LengthStr = Line.Mid(15).TrimStartAndEnd();
                            if (!LexTryParseString(ContentLength, *LengthStr)
                                || ContentLength < 0
                                || ContentLength > MaxSize)
                            {
                                return false;
                            }
                            break;
                        }
                    }

                    const int64 ExpectedBytes64 = static_cast<int64>(HeaderEndByte)
                        + 4
                        + static_cast<int64>(ContentLength);
                    if (ExpectedBytes64 > MaxSize || ExpectedBytes64 > MAX_int32)
                    {
                        return false;
                    }
                    ExpectedTotalBytes = static_cast<int32>(ExpectedBytes64);
                }

                if (ExpectedTotalBytes != INDEX_NONE && RawRequest.Num() >= ExpectedTotalBytes)
                {
                    break;
                }
            }
            else
            {
                // Connection closed or no more data
                UE_LOG(LogMCPServer, Log, TEXT("ParseHttpRequest: Connection closed (0 bytes read)"));
                break;
            }
        }
        else
        {
            UE_LOG(LogMCPServer, Warning, TEXT("ParseHttpRequest: Recv() failed"));
            break;
        }
    }
    
    if (EmptyReadCount >= MaxEmptyReads)
    {
        UE_LOG(LogMCPServer, Warning, TEXT("ParseHttpRequest: Timeout waiting for data"));
    }
    
    if (RawRequest.IsEmpty() || HeaderEndByte == INDEX_NONE
        || ExpectedTotalBytes == INDEX_NONE || RawRequest.Num() < ExpectedTotalBytes)
    {
        UE_LOG(LogMCPServer, Warning, TEXT("ParseHttpRequest: Incomplete request data received"));
        return false;
    }

    FUTF8ToTCHAR HeaderConverter(
        reinterpret_cast<const ANSICHAR*>(RawRequest.GetData()),
        HeaderEndByte);
    FString HeaderData(HeaderConverter.Length(), HeaderConverter.Get());

    // Parse request line
    int32 FirstLineEnd = HeaderData.Find(TEXT("\r\n"));
    if (FirstLineEnd == INDEX_NONE)
    {
        return false;
    }
    
    FString RequestLine = HeaderData.Left(FirstLineEnd);
    TArray<FString> RequestParts;
    RequestLine.ParseIntoArrayWS(RequestParts);
    
    if (RequestParts.Num() < 2)
    {
        return false;
    }
    
    OutMethod = RequestParts[0];
    OutPath = RequestParts[1];
    
    // Parse headers
    FString HeaderSection = HeaderData.Mid(FirstLineEnd + 2);
    TArray<FString> HeaderLines;
    HeaderSection.ParseIntoArray(HeaderLines, TEXT("\r\n"));
    
    for (const FString& Line : HeaderLines)
    {
        int32 ColonPos;
        if (Line.FindChar(':', ColonPos))
        {
            FString Key = Line.Left(ColonPos).TrimStartAndEnd().ToLower();
            FString Value = Line.Mid(ColonPos + 1).TrimStartAndEnd();
            OutHeaders.Add(Key, Value);
        }
    }
    
    // Convert the complete body once. This preserves UTF-8 code points split
    // across socket chunks and compares Content-Length in bytes, as HTTP requires.
    OutBody.Empty();
    OutBodyBytes.Reset();
    if (ContentLength > 0)
    {
        const uint8* BodyData = RawRequest.GetData() + HeaderEndByte + 4;
        OutBodyBytes.Append(BodyData, ContentLength);
        FUTF8ToTCHAR BodyConverter(reinterpret_cast<const ANSICHAR*>(BodyData), ContentLength);
        OutBody = FString(BodyConverter.Length(), BodyConverter.Get());
    }
    
    return true;
}

void FMCPServer::SendHttpResponse(FSocket* Socket, int32 StatusCode, const FString& StatusText,
                                   const FString& ContentType, const FString& Body,
                                   const TMap<FString, FString>& ExtraHeaders,
                                   const FRevvyBridgeAuthContext* AuthContext)
{
    if (!Socket)
    {
        UE_LOG(LogMCPServer, Warning, TEXT("SendHttpResponse called with null socket"));
        return;
    }
    
    // Convert body to UTF-8 FIRST to get accurate byte length for Content-Length header
    // Using Body.Len() is incorrect because UTF-8 can have more bytes than characters
    FTCHARToUTF8 BodyConverter(*Body);
    int32 BodyByteLength = BodyConverter.Length();
    TArray<uint8> BodyBytes;
    if (BodyByteLength > 0)
    {
        BodyBytes.Append(
            reinterpret_cast<const uint8*>(BodyConverter.Get()),
            BodyByteLength);
    }

    TMap<FString, FString> ResponseHeaders = ExtraHeaders;
    if (AuthContext != nullptr && AuthContext->IsAuthenticated())
    {
        ResponseHeaders.Add(
            TEXT("X-Revvy-Response-Signature"),
            FRevvyBridgeAuth::SignResponse(
                Config.BridgeHmacKey,
                *AuthContext,
                StatusCode,
                BodyBytes));
    }
    
    FString Response = FString::Printf(TEXT("HTTP/1.1 %d %s\r\n"), StatusCode, *StatusText);
    
    if (!ContentType.IsEmpty())
    {
        Response += FString::Printf(TEXT("Content-Type: %s; charset=utf-8\r\n"), *ContentType);
    }
    
    Response += FString::Printf(TEXT("Content-Length: %d\r\n"), BodyByteLength);
    Response += TEXT("Connection: close\r\n");
    
    // Add extra headers
    for (const auto& Header : ResponseHeaders)
    {
        Response += FString::Printf(TEXT("%s: %s\r\n"), *Header.Key, *Header.Value);
    }
    
    Response += TEXT("\r\n");
    
    // Convert headers to UTF-8 and send separately, then send body bytes
    FTCHARToUTF8 HeaderConverter(*Response);
    
    // Combine headers and body into one buffer for atomic send
    TArray<uint8> SendBuffer;
    SendBuffer.SetNumUninitialized(HeaderConverter.Length() + BodyByteLength);
    FMemory::Memcpy(SendBuffer.GetData(), HeaderConverter.Get(), HeaderConverter.Length());
    FMemory::Memcpy(SendBuffer.GetData() + HeaderConverter.Length(), BodyConverter.Get(), BodyByteLength);
    
    int32 TotalBytesSent = 0;
    int32 ConsecutiveFailures = 0;
    while (TotalBytesSent < SendBuffer.Num() && ConsecutiveFailures < 3)
    {
        int32 BytesSent = 0;
        const bool bSendSuccess = Socket->Send(
            SendBuffer.GetData() + TotalBytesSent,
            SendBuffer.Num() - TotalBytesSent,
            BytesSent);
        if (bSendSuccess && BytesSent > 0)
        {
            TotalBytesSent += BytesSent;
            ConsecutiveFailures = 0;
        }
        else
        {
            ++ConsecutiveFailures;
            Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(100));
        }
    }

    if (TotalBytesSent != SendBuffer.Num())
    {
        UE_LOG(LogMCPServer, Warning, TEXT("SendHttpResponse: Send incomplete/failed - sent %d of %d bytes"), 
            TotalBytesSent, SendBuffer.Num());
    }
    else
    {
        UE_LOG(LogMCPServer, Verbose, TEXT("SendHttpResponse: %d %s - %d bytes (body: %d bytes)"), 
            StatusCode, *StatusText, TotalBytesSent, BodyByteLength);
    }
}

// ============ MCP Protocol Handling ============

FString FMCPServer::HandleMCPRequest(
    const FString& JsonBody,
    FString& InOutSessionId,
    bool& bOutIsNotification,
    EMCPAccessScope AccessScope)
{
    bOutIsNotification = false;
    
    TSharedPtr<FJsonObject> RequestObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonBody);
    
    if (!FJsonSerializer::Deserialize(Reader, RequestObj) || !RequestObj.IsValid())
    {
        return BuildJsonRpcError(nullptr, -32700, TEXT("Parse error"));
    }
    
    // Check JSON-RPC version
    FString JsonRpc;
    if (!RequestObj->TryGetStringField(TEXT("jsonrpc"), JsonRpc) || JsonRpc != TEXT("2.0"))
    {
        return BuildJsonRpcError(nullptr, -32600, TEXT("Invalid Request - missing or invalid jsonrpc version"));
    }
    
    // Get method
    FString Method;
    if (!RequestObj->TryGetStringField(TEXT("method"), Method))
    {
        return BuildJsonRpcError(nullptr, -32600, TEXT("Invalid Request - missing method"));
    }
    
    // Preserve the JSON-RPC ID type. Missing means notification; explicit null
    // remains a request with a null ID. Reject numbers that JSON's double-backed
    // representation cannot preserve exactly.
    TSharedPtr<FJsonValue> RequestId;
    const TSharedPtr<FJsonValue>* IdValue = RequestObj->Values.Find(TEXT("id"));
    if (IdValue)
    {
        if (!IdValue->IsValid()
            || ((*IdValue)->Type != EJson::String
                && (*IdValue)->Type != EJson::Number
                && (*IdValue)->Type != EJson::Null))
        {
            return BuildJsonRpcError(nullptr, -32600, TEXT("Invalid Request - id must be a string, number, or null"));
        }
        if ((*IdValue)->Type == EJson::Number)
        {
            constexpr double MaxSafeJsonInteger = 9007199254740991.0;
            const double NumericId = (*IdValue)->AsNumber();
            if (!FMath::IsFinite(NumericId) || FMath::Abs(NumericId) > MaxSafeJsonInteger)
            {
                return BuildJsonRpcError(nullptr, -32600,
                    TEXT("Invalid Request - numeric id exceeds the JSON safe integer range; use a string id"));
            }
        }
        RequestId = *IdValue;
    }
    else
    {
        bOutIsNotification = true;
    }
    
    // Get params
    TSharedPtr<FJsonObject> Params;
    const TSharedPtr<FJsonObject>* ParamsPtr;
    if (RequestObj->TryGetObjectField(TEXT("params"), ParamsPtr))
    {
        Params = *ParamsPtr;
    }
    
    const FString RequestIdLog = JsonRpcIdToString(RequestId);
    UE_LOG(LogMCPServer, Log, TEXT("MCP Method: %s (id: %s)"), *Method, *RequestIdLog);
    
    // Route to handler
    if (Method == TEXT("initialize"))
    {
        FString Response = HandleInitialize(Params, RequestId);
        // Extract session ID from the initialized session for caller
        InOutSessionId = GenerateSessionId();
        {
            FScopeLock Lock(&SessionLock);
            ActiveSessions.Add(InOutSessionId, FDateTime::UtcNow());
        }
        return Response;
    }
    else if (Method == TEXT("initialized"))
    {
        // Client acknowledgment - nothing to return
        bOutIsNotification = true;
        return TEXT("");
    }
    else if (Method == TEXT("tools/list"))
    {
        return HandleToolsList(Params, RequestId, AccessScope);
    }
    else if (Method == TEXT("tools/call"))
    {
        return HandleToolsCall(Params, RequestId, AccessScope);
    }
    else if (Method == TEXT("ping"))
    {
        return HandlePing(RequestId);
    }
    else if (Method == TEXT("notifications/cancelled"))
    {
        // Cancellation notification - just acknowledge
        bOutIsNotification = true;
        return TEXT("");
    }
    else
    {
        return BuildJsonRpcError(RequestId, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
    }
}

FString FMCPServer::HandleInitialize(TSharedPtr<FJsonObject> Params, const TSharedPtr<FJsonValue>& RequestId)
{
    // Get client's requested protocol version
    FString RequestedVersion;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("protocolVersion"), RequestedVersion);
    }
    
    // Negotiate protocol version
    FString NegotiatedVersion = SUPPORTED_PROTOCOL_VERSIONS[0]; // Default to latest
    
    if (!RequestedVersion.IsEmpty())
    {
        // Check if we support the client's requested version
        if (SUPPORTED_PROTOCOL_VERSIONS.Contains(RequestedVersion))
        {
            NegotiatedVersion = RequestedVersion;
            UE_LOG(LogMCPServer, Log, TEXT("MCP Initialize: Client requested %s, using exact match"), *RequestedVersion);
        }
        else
        {
            // Client requested unsupported version, use our latest
            UE_LOG(LogMCPServer, Warning, TEXT("MCP Initialize: Client requested unsupported version %s, using %s"), 
                   *RequestedVersion, *NegotiatedVersion);
        }
    }
    else
    {
        UE_LOG(LogMCPServer, Log, TEXT("MCP Initialize: No version requested, using latest %s"), *NegotiatedVersion);
    }
    
    // Build capabilities
    TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
    
    // We support tools
    TSharedPtr<FJsonObject> ToolsCap = MakeShared<FJsonObject>();
    Capabilities->SetObjectField(TEXT("tools"), ToolsCap);
    
    // Build server info
    TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
    ServerInfo->SetStringField(TEXT("name"), MCP_SERVER_NAME);
    ServerInfo->SetStringField(TEXT("version"), MCP_SERVER_VERSION);
    
    // Build result
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("protocolVersion"), NegotiatedVersion);
    Result->SetObjectField(TEXT("capabilities"), Capabilities);
    Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
    
    // Build response - session ID is added as header by caller
    return BuildJsonRpcResponse(RequestId, Result);
}

#include "Core/ProgressContext.h"

namespace
{
    /** Build MCP "annotations" sub-object per spec 2025-03-26+. Emitted only when set explicitly. */
    static TSharedPtr<FJsonObject> BuildAnnotationsJson(const FMCPToolAnnotations& Ann)
    {
        if (!Ann.bAnnotated)
        {
            return nullptr;
        }
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        if (!Ann.Title.IsEmpty())
        {
            Obj->SetStringField(TEXT("title"), Ann.Title);
        }
        Obj->SetBoolField(TEXT("readOnlyHint"), Ann.bReadOnlyHint);
        Obj->SetBoolField(TEXT("destructiveHint"), Ann.bDestructiveHint);
        Obj->SetBoolField(TEXT("idempotentHint"), Ann.bIdempotentHint);
        Obj->SetBoolField(TEXT("openWorldHint"), Ann.bOpenWorldHint);
        if (Ann.ReadOnlyActions.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> Actions;
            for (const FString& Action : Ann.ReadOnlyActions)
            {
                Actions.Add(MakeShared<FJsonValueString>(Action));
            }
            Obj->SetArrayField(TEXT("readOnlyActions"), Actions);
        }
        return Obj;
    }

    static bool IsSafeReadOnlyTool(const FMCPTool& Tool)
    {
        return Tool.Annotations.bAnnotated
            && Tool.Annotations.bReadOnlyHint
            && !Tool.Annotations.bDestructiveHint;
    }

    /**
     * The official UE 5.8 MCP endpoint can run without Revvy authentication.
     * Its surface must therefore be narrower than an authenticated Revvy Safe
     * session. Keep this as an explicit allow-list and enforce it again at
     * execution time; annotations alone do not make local data safe to expose.
     */
    static bool IsAllowedForUnauthenticatedNativeToolset(const FMCPTool& Tool)
    {
        static const TSet<FString> AllowedTools = {
            TEXT("discover_python_class"),
            TEXT("discover_python_function"),
            TEXT("discover_python_module"),
            TEXT("list_python_subsystems"),
            // Same exposure class as discover_python_class: reflection over
            // loaded types, no project file contents, no logs, read-only.
            TEXT("lookup_class")
        };
        return AllowedTools.Contains(Tool.Name)
            && IsSafeReadOnlyTool(Tool)
            && !Tool.Annotations.bOpenWorldHint;
    }

    /** A mixed-verb tool that still has query actions a read-only session may call. */
    static bool HasSafeReadOnlyActions(const FMCPTool& Tool)
    {
        return Tool.Annotations.bAnnotated && Tool.Annotations.ReadOnlyActions.Num() > 0;
    }

    static bool IsVisibleToReadOnlyScope(const FMCPTool& Tool)
    {
        return IsSafeReadOnlyTool(Tool) || HasSafeReadOnlyActions(Tool);
    }

    /** Tell the model which verbs survive, so it does not spend a call discovering it. */
    static FString DescribeReadOnlyActions(const FMCPTool& Tool)
    {
        return FString::Printf(
            TEXT(" Safe (read-only) session: only these actions are available — %s."),
            *FString::Join(Tool.Annotations.ReadOnlyActions, TEXT(", ")));
    }
}

FString FMCPServer::HandleToolsList(
    TSharedPtr<FJsonObject> Params,
    const TSharedPtr<FJsonValue>& RequestId,
    EMCPAccessScope AccessScope)
{
    // Get internal tools
    TArray<FMCPTool> Tools = GetInternalTools();

    // Build tools array
    TArray<TSharedPtr<FJsonValue>> ToolsArray;

    for (const FMCPTool& Tool : Tools)
    {
        const bool bActionGated =
            AccessScope == EMCPAccessScope::ReadOnly && !IsSafeReadOnlyTool(Tool);
        if (AccessScope == EMCPAccessScope::ReadOnly && !IsVisibleToReadOnlyScope(Tool))
        {
            continue;
        }
        TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
        ToolObj->SetStringField(TEXT("name"), Tool.Name);
        ToolObj->SetStringField(
            TEXT("description"),
            bActionGated ? Tool.Description + DescribeReadOnlyActions(Tool) : Tool.Description);

        // Convert input schema
        if (Tool.InputSchema.IsValid())
        {
            ToolObj->SetObjectField(TEXT("inputSchema"), Tool.InputSchema);
        }
        else
        {
            // Default empty schema
            TSharedPtr<FJsonObject> EmptySchema = MakeShared<FJsonObject>();
            EmptySchema->SetStringField(TEXT("type"), TEXT("object"));
            EmptySchema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
            ToolObj->SetObjectField(TEXT("inputSchema"), EmptySchema);
        }

        // MCP annotations (spec 2025-03-26+). Emitted only if marked explicitly.
        if (TSharedPtr<FJsonObject> AnnObj = BuildAnnotationsJson(Tool.Annotations))
        {
            ToolObj->SetObjectField(TEXT("annotations"), AnnObj);
        }

        // Structured output schema (spec 2025-06-18). Emitted only if set.
        if (Tool.OutputSchema.IsValid())
        {
            ToolObj->SetObjectField(TEXT("outputSchema"), Tool.OutputSchema);
        }

        ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObj));
    }

    // The result-page reader, served by the server rather than the registry
    // (see HandleResultPageCall). Advertised to every scope: any tool's reply can
    // overshoot, and a caller that cannot see this tool cannot finish reading one.
    {
        TSharedPtr<FJsonObject> HandleProp = MakeShared<FJsonObject>();
        HandleProp->SetStringField(TEXT("type"), TEXT("string"));
        HandleProp->SetStringField(
            TEXT("description"), TEXT("Handle printed in the bounded reply you are continuing."));

        TSharedPtr<FJsonObject> OffsetProp = MakeShared<FJsonObject>();
        OffsetProp->SetStringField(TEXT("type"), TEXT("integer"));
        OffsetProp->SetStringField(
            TEXT("description"), TEXT("Character offset to read from, as printed in the previous reply."));

        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        Properties->SetObjectField(TEXT("handle"), HandleProp);
        Properties->SetObjectField(TEXT("offset"), OffsetProp);

        TArray<TSharedPtr<FJsonValue>> RequiredFields;
        RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("handle")));
        RequiredFields.Add(MakeShared<FJsonValueString>(TEXT("offset")));

        TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
        Schema->SetStringField(TEXT("type"), TEXT("object"));
        Schema->SetObjectField(TEXT("properties"), Properties);
        Schema->SetArrayField(TEXT("required"), RequiredFields);

        TSharedPtr<FJsonObject> Annotations = MakeShared<FJsonObject>();
        Annotations->SetStringField(TEXT("title"), TEXT("Read the rest of a large result"));
        Annotations->SetBoolField(TEXT("readOnlyHint"), true);
        Annotations->SetBoolField(TEXT("idempotentHint"), true);
        Annotations->SetBoolField(TEXT("openWorldHint"), false);

        TSharedPtr<FJsonObject> PageTool = MakeShared<FJsonObject>();
        PageTool->SetStringField(TEXT("name"), RevvyOversizedResult::GetPageToolName());
        PageTool->SetStringField(TEXT("description"), RevvyOversizedResult::GetPageToolDescription());
        PageTool->SetObjectField(TEXT("inputSchema"), Schema);
        PageTool->SetObjectField(TEXT("annotations"), Annotations);

        ToolsArray.Add(MakeShared<FJsonValueObject>(PageTool));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("tools"), ToolsArray);

    UE_LOG(LogMCPServer, Log, TEXT("MCP tools/list - Returning %d tools"), ToolsArray.Num());
    
    return BuildJsonRpcResponse(RequestId, Result);
}

FString FMCPServer::HandleToolsCall(
    TSharedPtr<FJsonObject> Params,
    const TSharedPtr<FJsonValue>& RequestId,
    EMCPAccessScope AccessScope)
{
    // NOTE: Upstream VibeUE gated tool execution behind a hosted-API license
    // check here. This fork is fully local \u2014 no license gate.

    if (!Params.IsValid())
    {
        return BuildJsonRpcError(RequestId, -32602, TEXT("Invalid params"));
    }
    
    FString ToolName;
    if (!Params->TryGetStringField(TEXT("name"), ToolName))
    {
        return BuildJsonRpcError(RequestId, -32602, TEXT("Missing tool name"));
    }

    // Ahead of the scope gate below, which resolves names through the tool
    // registry: this one is served by the server itself, and refusing it as
    // unknown would strand a Safe session halfway through reading its own result.
    if (ToolName == RevvyOversizedResult::GetPageToolName())
    {
        return HandleResultPageCall(Params, RequestId, AccessScope);
    }

    if (AccessScope == EMCPAccessScope::ReadOnly)
    {
        const TArray<FMCPTool> Tools = GetInternalTools();
        const FMCPTool* Tool = Tools.FindByPredicate(
            [&ToolName](const FMCPTool& Candidate)
            {
                return Candidate.Name == ToolName;
            });
        if (Tool == nullptr || !IsVisibleToReadOnlyScope(*Tool))
        {
            UE_LOG(LogMCPServer, Warning,
                TEXT("MCP ReadOnly scope blocked tool call: %s"),
                *ToolName);
            return BuildJsonRpcError(
                RequestId,
                -32003,
                TEXT("This tool requires a Revvy Full access session"));
        }

        // Mixed-verb tools are admitted for their query actions only. The handlers
        // normalise the argument the same way, so compare it the same way.
        if (!IsSafeReadOnlyTool(*Tool))
        {
            FString Action;
            const TSharedPtr<FJsonObject>* ScopeArgs = nullptr;
            if (Params->TryGetObjectField(TEXT("arguments"), ScopeArgs) && ScopeArgs)
            {
                (*ScopeArgs)->TryGetStringField(TEXT("action"), Action);
            }
            const FString NormalizedAction = Action.TrimStartAndEnd().ToLower();
            if (!Tool->Annotations.ReadOnlyActions.Contains(NormalizedAction))
            {
                UE_LOG(LogMCPServer, Warning,
                    TEXT("MCP ReadOnly scope blocked %s action: '%s'"),
                    *ToolName, *NormalizedAction);
                return BuildJsonRpcError(
                    RequestId,
                    -32003,
                    FString::Printf(
                        TEXT("Action '%s' requires a Revvy Full access session. Available here: %s"),
                        *NormalizedAction,
                        *FString::Join(Tool->Annotations.ReadOnlyActions, TEXT(", "))));
            }
        }
    }
    
    // Get arguments
    TMap<FString, FString> Arguments;
    const TSharedPtr<FJsonObject>* ArgsObj;
    if (Params->TryGetObjectField(TEXT("arguments"), ArgsObj))
    {
        for (const auto& Pair : (*ArgsObj)->Values)
        {
            if (Pair.Value->Type == EJson::String)
            {
                Arguments.Add(FString(Pair.Key), Pair.Value->AsString());
            }
            else if (Pair.Value->Type == EJson::Number)
            {
                // Serialize numbers directly — FJsonSerializer::Serialize with an identifier
                // requires an open object context and produces empty output otherwise.
                Arguments.Add(FString(Pair.Key), FString::Printf(TEXT("%.10g"), Pair.Value->AsNumber()));
            }
            else if (Pair.Value->Type == EJson::Boolean)
            {
                Arguments.Add(FString(Pair.Key), Pair.Value->AsBool() ? TEXT("true") : TEXT("false"));
            }
            else
            {
                // Arrays/objects — serialize as condensed JSON
                FString JsonStr;
                TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
                    TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonStr);
                if (Pair.Value->Type == EJson::Array)
                {
                    FJsonSerializer::Serialize(Pair.Value->AsArray(), Writer);
                }
                else if (Pair.Value->Type == EJson::Object && Pair.Value->AsObject().IsValid())
                {
                    FJsonSerializer::Serialize(Pair.Value->AsObject().ToSharedRef(), Writer);
                }
                else
                {
                    JsonStr = TEXT("null");
                }
                Arguments.Add(FString(Pair.Key), JsonStr);
            }
        }
    }
    
    // Idempotency key (opt-in): '_idempotency_key' argument or _meta.idempotencyKey.
    // Stripped here so the tool never sees it and the action/ParamsJson transform
    // below never folds it into the payload.
    FString IdempotencyKey;
    if (Arguments.Contains(TEXT("_idempotency_key")))
    {
        IdempotencyKey = Arguments.FindRef(TEXT("_idempotency_key")).TrimStartAndEnd();
        Arguments.Remove(TEXT("_idempotency_key"));
    }
    if (IdempotencyKey.IsEmpty())
    {
        const TSharedPtr<FJsonObject>* MetaForKey = nullptr;
        if (Params->TryGetObjectField(TEXT("_meta"), MetaForKey) && MetaForKey && MetaForKey->IsValid())
        {
            (*MetaForKey)->TryGetStringField(TEXT("idempotencyKey"), IdempotencyKey);
            IdempotencyKey.TrimStartAndEndInline();
        }
    }

    UE_LOG(LogMCPServer, Log, TEXT("MCP tools/call - Tool: %s (RequestId: %s), Arguments received: %d"),
        *ToolName, *JsonRpcIdToString(RequestId), Arguments.Num());
    for (const auto& Pair : Arguments)
    {
        // Fail-closed: secret-shaped arguments never reach the log verbatim
        // (see MCP/LogRedaction.h), and oversized values are elided.
        UE_LOG(LogMCPServer, Log, TEXT("  Arg: %s = %s"), *Pair.Key,
            *RevvyLogRedaction::RedactArgumentForLog(Pair.Key, Pair.Value));
    }
    double ToolStartTime = FPlatformTime::Seconds();
    
    // Transform flat arguments into Action + ParamsJson format for tools that expect it
    // This handles the case where MCP clients send {"action": "list", "blueprint_name": "..."} 
    // but the tool expects {"Action": "list", "ParamsJson": "{\"blueprint_name\":\"...\"}"}
    if (Arguments.Contains(TEXT("action")) || Arguments.Contains(TEXT("Action")))
    {
        FString ActionValue;
        if (Arguments.Contains(TEXT("action")))
        {
            ActionValue = Arguments.FindRef(TEXT("action"));
            Arguments.Remove(TEXT("action"));
        }
        else if (Arguments.Contains(TEXT("Action")))
        {
            ActionValue = Arguments.FindRef(TEXT("Action"));
            Arguments.Remove(TEXT("Action"));
        }
        
        // Check if this is NOT using the ParamsJson pattern already
        if (!Arguments.Contains(TEXT("ParamsJson")))
        {
            // Build ParamsJson from remaining arguments
            TSharedPtr<FJsonObject> ParamsObj = MakeShared<FJsonObject>();
            for (const auto& Pair : Arguments)
            {
                // Try to parse as JSON first (for nested objects/arrays)
                TSharedPtr<FJsonValue> JsonValue;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Pair.Value);
                if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid())
                {
                    ParamsObj->SetField(Pair.Key, JsonValue);
                }
                else
                {
                    ParamsObj->SetStringField(Pair.Key, Pair.Value);
                }
            }
            
            FString ParamsJsonStr;
            TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ParamsJsonStr);
            FJsonSerializer::Serialize(ParamsObj.ToSharedRef(), Writer);
            
            // Replace arguments with Action + ParamsJson
            Arguments.Empty();
            Arguments.Add(TEXT("Action"), ActionValue);
            Arguments.Add(TEXT("ParamsJson"), ParamsJsonStr);
        }
        else
        {
            // Already has ParamsJson, just ensure Action is capitalized
            Arguments.Add(TEXT("Action"), ActionValue);
        }
    }
    
    // Check if tool exists (can be done on any thread)
    FToolRegistry& Registry = FToolRegistry::Get();
    const FToolMetadata* ToolMeta = Registry.FindTool(ToolName);
    if (!ToolMeta)
    {
        return BuildJsonRpcError(RequestId, -32602, FString::Printf(TEXT("Unknown tool: %s"), *ToolName));
    }

    // Idempotency ledger (after ChiR24/Unreal_mcp): with a key supplied, an
    // identical retry replays the stored result instead of re-executing the
    // mutation; the same key with a different payload is refused - the HTTP-409
    // analogue. Keys are scoped per access level so a Safe-session key can never
    // replay a Full-session result.
    static FRevvyIdempotencyLedger GToolCallIdempotencyLedger;
    FString LedgerKey;
    FString LedgerStoredResult;
    bool bReplayFromLedger = false;
    if (!IdempotencyKey.IsEmpty())
    {
        LedgerKey = FString::Printf(TEXT("%d|%s"), static_cast<int32>(AccessScope), *IdempotencyKey);
        TArray<FString> HashParts;
        for (const TPair<FString, FString>& Pair : Arguments)
        {
            HashParts.Add(Pair.Key + TEXT("=") + Pair.Value);
        }
        HashParts.Sort();
        const FString PayloadHash = FMD5::HashAnsiString(
            *(ToolName + TEXT("\n") + FString::Join(HashParts, TEXT("\n"))));

        switch (GToolCallIdempotencyLedger.Begin(LedgerKey, PayloadHash, LedgerStoredResult))
        {
        case ERevvyIdempotencyBegin::Execute:
            break;
        case ERevvyIdempotencyBegin::ReplayCompleted:
            bReplayFromLedger = true;
            UE_LOG(LogMCPServer, Log, TEXT("Idempotent replay for tool %s (key=%s)"), *ToolName, *IdempotencyKey);
            break;
        case ERevvyIdempotencyBegin::ConflictPending:
            return BuildJsonRpcError(RequestId, -32009,
                TEXT("A call with this idempotency key is still executing. Wait, then retry to replay its result."));
        case ERevvyIdempotencyBegin::ConflictPayload:
            return BuildJsonRpcError(RequestId, -32009,
                TEXT("This idempotency key was already used with a DIFFERENT payload. Use a fresh key per logical operation."));
        }
    }

    // Extract progressToken from the request `_meta` object (spec: notifications/progress).
    // Fall back to a synthetic ID so the response's _meta.progress carries a stable
    // correlation key even when the client did not subscribe.
    FString ProgressToken;
    bool bHasClientProgressToken = false;
    {
        const TSharedPtr<FJsonObject>* MetaObj = nullptr;
        if (Params->TryGetObjectField(TEXT("_meta"), MetaObj) && MetaObj && MetaObj->IsValid())
        {
            bHasClientProgressToken = (*MetaObj)->TryGetStringField(TEXT("progressToken"), ProgressToken)
                && !ProgressToken.IsEmpty();
        }
    }
    if (ProgressToken.IsEmpty())
    {
        ProgressToken = FString::Printf(TEXT("synthetic-%s"), *JsonRpcIdToString(RequestId));
    }

    // Per-request progress event buffer. Populated on the thread that runs the tool
    // (game thread) via a per-thread emit sink; drained on the socket thread after
    // ExecuteTool completes and attached to result._meta.progress.
    //
    // Rationale (approach B): the current SSE transport wraps the single JSON-RPC
    // response in one SSE event and closes — there's no intermediate streaming
    // channel from ExecuteTool back to the socket. Buffering per request and
    // injecting into result._meta.progress delivers the trail predictably to any
    // client, streaming or not, without holding the socket open mid-execution.
    struct FProgressEvent
    {
        float Progress = 0.f;
        float Total = 1.f;
        FString Message;
        double TimestampSeconds = 0.0;
    };
    struct FProgressBuffer
    {
        FCriticalSection Lock;
        TArray<FProgressEvent> Events;
        TAtomic<int32> DroppedCount{0};
        enum { MaxEvents = 1000 }; // cap unbounded growth per request (local class: enum, not static member)

        void Append(float Progress, float Total, const FString& Message)
        {
            FScopeLock ScopeLock(&Lock);
            if (Events.Num() >= MaxEvents)
            {
                // TAtomic supports operator++ in UE, matching NextEventId's usage.
                ++DroppedCount;
                return;
            }
            FProgressEvent Ev;
            Ev.Progress = Progress;
            Ev.Total = Total;
            Ev.Message = Message;
            Ev.TimestampSeconds = FPlatformTime::Seconds();
            Events.Add(MoveTemp(Ev));
        }

        TArray<FProgressEvent> Drain()
        {
            FScopeLock ScopeLock(&Lock);
            TArray<FProgressEvent> Out = MoveTemp(Events);
            Events.Reset();
            return Out;
        }
    };
    TSharedPtr<FProgressBuffer, ESPMode::ThreadSafe> ProgressBuffer =
        MakeShared<FProgressBuffer, ESPMode::ThreadSafe>();

    // Reporter for the legacy FRevvyProgressScope path — preserves the existing
    // UE_LOG stub for local debugging and also appends to the shared buffer so
    // any callers using the older API still land in the response.
    auto MakeReporter = [ProgressBuffer]()
    {
        return [ProgressBuffer](const FString& Token, float Progress, float Total, const FString& Message)
        {
            UE_LOG(LogMCPServer, Log, TEXT("[progress token=%s] %.0f/%.0f %s"),
                *Token, Progress, Total, *Message);
            if (ProgressBuffer.IsValid())
            {
                ProgressBuffer->Append(Progress, Total, Message);
            }
        };
    };
    // Newer per-thread emit sink used by FRevvyProgress::SetActiveEmitSink. Same
    // UE_LOG stub, same shared buffer — total defaults to 1.0 since the sink
    // signature is (progress, message).
    auto MakeSink = [ProgressBuffer, ProgressToken]()
    {
        return [ProgressBuffer, ProgressToken](float Progress, const FString& Message)
        {
            UE_LOG(LogMCPServer, Log, TEXT("[progress token=%s] %.3f %s"),
                *ProgressToken, Progress, *Message);
            if (ProgressBuffer.IsValid())
            {
                ProgressBuffer->Append(Progress, 1.0f, Message);
            }
        };
    };

    // Execute tool on game thread - REQUIRED because many UE operations are not thread-safe
    FString ToolResult;

    if (bReplayFromLedger)
    {
        // The stored result stands in for execution; response shaping below is shared.
        ToolResult = MoveTemp(LedgerStoredResult);
    }
    else if (IsInGameThread())
    {
        // Execute directly - no need to defer. Install the progress scope + sink
        // on THIS thread so FRevvyProgress::Report from inside the tool routes
        // into ProgressBuffer.
        UE_LOG(LogMCPServer, Log, TEXT("Executing tool %s directly on game thread"), *ToolName);
        FRevvyProgressScope ProgressScope(ProgressToken, MakeReporter());
        FRevvyProgressEmitSinkGuard SinkGuard(MakeSink());
        ToolResult = FToolRegistry::Get().ExecuteTool(ToolName, Arguments);
        if (!LedgerKey.IsEmpty())
        {
            GToolCallIdempotencyLedger.Complete(LedgerKey, ToolResult);
        }
    }
    else
    {
        // We're on a background thread, need to marshal to game thread.
        // Use shared state so the lambda never touches dangling stack references
        // if the caller times out and its stack frame is destroyed.
        UE_LOG(LogMCPServer, Log, TEXT("Marshaling tool %s to game thread from socket thread"), *ToolName);

        struct FToolExecState
        {
            FString Result;
            TAtomic<bool> bStarted{false};
            TAtomic<bool> bTimedOut{false};
            FEvent* Event = nullptr;

            ~FToolExecState()
            {
                if (Event)
                {
                    FPlatformProcess::ReturnSynchEventToPool(Event);
                    Event = nullptr;
                }
            }
        };

        TSharedPtr<FToolExecState> State = MakeShared<FToolExecState>();
        State->Event = FPlatformProcess::GetSynchEventFromPool(false);

        // CRITICAL: the progress scope+sink must be installed on the GAME thread
        // (which owns the thread_local slot ExecuteTool will read) — not on this
        // socket thread. Install them inside the AsyncTask lambda; RAII cleanup
        // guarantees they never leak between tool calls.
        AsyncTask(ENamedThreads::GameThread,
            [ToolName, Arguments, State, ProgressToken, ProgressBuffer, LedgerKey,
             Reporter = MakeReporter(), Sink = MakeSink()]() mutable
        {
            State->bStarted = true;

            if (State->bTimedOut)
            {
                UE_LOG(LogMCPServer, Warning, TEXT("Tool %s: caller already timed out before execution, skipping"), *ToolName);
                if (!LedgerKey.IsEmpty())
                {
                    // Nothing executed - free the key so a retry can run for real.
                    GToolCallIdempotencyLedger.Abandon(LedgerKey);
                }
                return;
            }

            UE_LOG(LogMCPServer, Log, TEXT("Tool %s execution starting on game thread"), *ToolName);
            {
                FRevvyProgressScope ProgressScope(ProgressToken, MoveTemp(Reporter));
                FRevvyProgressEmitSinkGuard SinkGuard(MoveTemp(Sink));
                State->Result = FToolRegistry::Get().ExecuteTool(ToolName, Arguments);
            }
            if (!LedgerKey.IsEmpty())
            {
                // Completes even when the socket side already timed out: the
                // mutation DID happen, so the retry must replay, not re-execute.
                GToolCallIdempotencyLedger.Complete(LedgerKey, State->Result);
            }
            UE_LOG(LogMCPServer, Log, TEXT("Tool %s execution completed on game thread"), *ToolName);

            if (!State->bTimedOut && State->Event)
            {
                State->Event->Trigger();
            }
        });
        
        // Wait for completion with timeout (60 seconds for MCP tool execution)
        // Python scripts and complex operations may need more time
        const double TimeoutSeconds = 60.0;
        bool bCompleted = State->Event->Wait(FTimespan::FromSeconds(TimeoutSeconds));
        
        if (bCompleted)
        {
            ToolResult = MoveTemp(State->Result);
        }
        else
        {
            // Mark timeout so the lambda won't touch the event after we clean up
            State->bTimedOut = true;
            
            if (!State->bStarted)
            {
                UE_LOG(LogMCPServer, Error, TEXT("Tool %s execution never started (game thread blocked?) - timed out after %.1fs"), 
                    *ToolName, TimeoutSeconds);
                return BuildJsonRpcError(RequestId, -32000, 
                    TEXT("Tool execution timed out - game thread may be blocked. Try again or restart the editor."));
            }
            else
            {
                UE_LOG(LogMCPServer, Error, TEXT("Tool %s execution started but didn't complete - timed out after %.1fs"), 
                    *ToolName, TimeoutSeconds);
                return BuildJsonRpcError(RequestId, -32000, 
                    TEXT("Tool execution timed out while running. The operation may still be in progress."));
            }
        }
    }
    
    double ToolEndTime = FPlatformTime::Seconds();
    UE_LOG(LogMCPServer, Log, TEXT("Tool %s completed in %.2fms"), *ToolName, (ToolEndTime - ToolStartTime) * 1000.0);
    
    // Classify the result BEFORE any bounding below, so a reply that gets sliced
    // is still reported with the success/error the tool actually returned.
    bool bIsError = false;
    TSharedPtr<FJsonObject> ResultJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ToolResult);
    if (FJsonSerializer::Deserialize(Reader, ResultJson) && ResultJson.IsValid())
    {
        // Check if success field is false, or if error field has content.
        // Use TryGet variants to avoid LogJson warnings when fields are absent.
        bool bSuccess = true;
        ResultJson->TryGetBoolField(TEXT("success"), bSuccess);
        FString ErrorMsg;
        ResultJson->TryGetStringField(TEXT("error"), ErrorMsg);
        bIsError = !bSuccess || !ErrorMsg.IsEmpty();
    }
    else
    {
        // If we can't parse JSON, fall back to string check for actual error content
        bIsError = ToolResult.Contains(TEXT("\"error\":")) &&
                   !ToolResult.Contains(TEXT("\"error\":\"\"")) &&
                   !ToolResult.Contains(TEXT("\"error\": \"\""));
    }

    // Bound the reply if the tool overshot what a client will accept. Individual
    // tools budget their own output where they can (see PythonTools), but that
    // cannot cover every tool or every project's data, and the failure mode when
    // one overshoots is total: the client rejects the whole reply and the caller
    // gets boilerplate instead of the answer it already paid to compute.
    const FString BoundedToolResult =
        RevvyOversizedResult::BoundToolReply(ToolResult, static_cast<int32>(AccessScope));
    const bool bResultWasBounded = BoundedToolResult.Len() != ToolResult.Len();
    if (bResultWasBounded)
    {
        UE_LOG(LogMCPServer, Warning,
            TEXT("Tool %s returned %d characters - sent a bounded slice with a handle to read the rest"),
            *ToolName, ToolResult.Len());
    }

    // Build MCP result
    TArray<TSharedPtr<FJsonValue>> ContentArray;

    TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
    TextContent->SetStringField(TEXT("type"), TEXT("text"));
    TextContent->SetStringField(TEXT("text"), BoundedToolResult);
    ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("content"), ContentArray);
    Result->SetBoolField(TEXT("isError"), bIsError);

    // Structured output (spec 2025-06-18): if the tool has an OutputSchema attached
    // and the result string parses as a JSON object, pass it through under
    // `structuredContent`. The text content block remains as a fallback for
    // clients that haven't adopted the spec yet.
    //
    // Skipped for a bounded reply: structuredContent is a promise that the object
    // satisfies the declared schema, and a first slice does not. Sending it
    // anyway would hand a schema-trusting client a half object with no sign
    // anything is missing - worse than the oversize error this replaces.
    if (ToolMeta && ToolMeta->OutputSchema.IsValid() && ResultJson.IsValid() && !bResultWasBounded)
    {
        Result->SetObjectField(TEXT("structuredContent"), ResultJson);
    }

    // Drain the per-request progress buffer and attach as result._meta.progress.
    // Non-standard extension (not part of MCP notifications/progress SSE stream)
    // but predictable — clients can inspect the trail after the fact. The token
    // is included so clients can correlate with progressToken they subscribed to.
    {
        // Implicit atomic load (matches ++NextEventId pattern used elsewhere in this file).
        const int32 DroppedCount = ProgressBuffer.IsValid()
            ? static_cast<int32>(ProgressBuffer->DroppedCount)
            : 0;
        TArray<FProgressEvent> Events = ProgressBuffer.IsValid()
            ? ProgressBuffer->Drain()
            : TArray<FProgressEvent>();

        if (Events.Num() > 0 || DroppedCount > 0)
        {
            TArray<TSharedPtr<FJsonValue>> ProgressArray;
            ProgressArray.Reserve(Events.Num());
            for (const FProgressEvent& Ev : Events)
            {
                TSharedPtr<FJsonObject> EvObj = MakeShared<FJsonObject>();
                EvObj->SetNumberField(TEXT("progress"), Ev.Progress);
                EvObj->SetNumberField(TEXT("total"), Ev.Total);
                if (!Ev.Message.IsEmpty())
                {
                    EvObj->SetStringField(TEXT("message"), Ev.Message);
                }
                EvObj->SetNumberField(TEXT("t"), Ev.TimestampSeconds);
                ProgressArray.Add(MakeShared<FJsonValueObject>(EvObj));
            }

            TSharedPtr<FJsonObject> MetaObj = MakeShared<FJsonObject>();
            MetaObj->SetArrayField(TEXT("progress"), ProgressArray);
            if (bHasClientProgressToken)
            {
                MetaObj->SetStringField(TEXT("progressToken"), ProgressToken);
            }
            if (DroppedCount > 0)
            {
                MetaObj->SetNumberField(TEXT("droppedProgressEvents"), DroppedCount);
            }
            Result->SetObjectField(TEXT("_meta"), MetaObj);

            UE_LOG(LogMCPServer, Log, TEXT("Attached %d progress events (%d dropped) for tool %s"),
                Events.Num(), DroppedCount, *ToolName);
        }
    }

    if (bReplayFromLedger)
    {
        const TSharedPtr<FJsonObject>* ExistingMeta = nullptr;
        TSharedPtr<FJsonObject> MetaObj =
            (Result->TryGetObjectField(TEXT("_meta"), ExistingMeta) && ExistingMeta && ExistingMeta->IsValid())
                ? *ExistingMeta
                : MakeShared<FJsonObject>();
        MetaObj->SetBoolField(TEXT("idempotentReplay"), true);
        Result->SetObjectField(TEXT("_meta"), MetaObj);
    }

    return BuildJsonRpcResponse(RequestId, Result);
}

FString FMCPServer::ExecuteUnauthenticatedNativeToolInProcess(
    const FString& ToolName,
    const FString& ArgumentsJson)
{
    auto BuildBridgeError = [](const FString& Code, const FString& Message)
    {
        TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetBoolField(TEXT("success"), false);
        Error->SetStringField(TEXT("error_code"), Code);
        Error->SetStringField(TEXT("error"), Message);

        FString Output;
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
        FJsonSerializer::Serialize(Error.ToSharedRef(), Writer);
        return Output;
    };

    if (!IsInGameThread())
    {
        return BuildBridgeError(
            TEXT("GAME_THREAD_REQUIRED"),
            TEXT("Native read-only tool execution must run on the Unreal game thread"));
    }

    const FString NormalizedToolName = ToolName.TrimStartAndEnd();
    if (NormalizedToolName.IsEmpty() || NormalizedToolName.Len() > 200)
    {
        return BuildBridgeError(
            TEXT("INVALID_TOOL_NAME"),
            TEXT("ToolName must contain between 1 and 200 characters"));
    }

    const TArray<FMCPTool> AvailableTools = GetInternalTools();
    const FMCPTool* NativeTool = AvailableTools.FindByPredicate(
        [&NormalizedToolName](const FMCPTool& Candidate)
        {
            return Candidate.Name == NormalizedToolName
                && IsAllowedForUnauthenticatedNativeToolset(Candidate);
        });
    if (!NativeTool)
    {
        return BuildBridgeError(
            TEXT("NATIVE_SCOPE_DENIED"),
            TEXT("Tool is not available to the unauthenticated native Toolset"));
    }

    if (ArgumentsJson.Len() > 256 * 1024)
    {
        return BuildBridgeError(
            TEXT("ARGUMENTS_TOO_LARGE"),
            TEXT("ArgumentsJson exceeds the 256 KiB bridge limit"));
    }

    const FString EffectiveArguments =
        ArgumentsJson.IsEmpty() ? FString(TEXT("{}")) : ArgumentsJson;
    TSharedPtr<FJsonObject> Arguments;
    TSharedRef<TJsonReader<>> ArgumentsReader =
        TJsonReaderFactory<>::Create(EffectiveArguments);
    if (!FJsonSerializer::Deserialize(ArgumentsReader, Arguments) || !Arguments.IsValid())
    {
        return BuildBridgeError(
            TEXT("INVALID_ARGUMENTS"),
            TEXT("ArgumentsJson must be a valid JSON object"));
    }

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("name"), NormalizedToolName);
    Params->SetObjectField(TEXT("arguments"), Arguments);

    const TSharedPtr<FJsonValue> RequestId =
        MakeShared<FJsonValueString>(TEXT("revvy-native-read-only"));
    const FString RpcResponse =
        HandleToolsCall(Params, RequestId, EMCPAccessScope::ReadOnly);
    if (RpcResponse.Len() > 4 * 1024 * 1024)
    {
        return BuildBridgeError(
            TEXT("RESULT_TOO_LARGE"),
            TEXT("Revvy's internal MCP response exceeds the 4 MiB bridge limit"));
    }

    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<>> ResponseReader =
        TJsonReaderFactory<>::Create(RpcResponse);
    if (!FJsonSerializer::Deserialize(ResponseReader, ResponseObject)
        || !ResponseObject.IsValid())
    {
        return BuildBridgeError(
            TEXT("INVALID_INTERNAL_RESPONSE"),
            TEXT("Revvy produced an invalid internal MCP response"));
    }

    const TSharedPtr<FJsonObject>* RpcError = nullptr;
    if (ResponseObject->TryGetObjectField(TEXT("error"), RpcError)
        && RpcError && RpcError->IsValid())
    {
        FString Message;
        (*RpcError)->TryGetStringField(TEXT("message"), Message);
        double NumericCode = 0.0;
        (*RpcError)->TryGetNumberField(TEXT("code"), NumericCode);
        return BuildBridgeError(
            FString::Printf(TEXT("MCP_%.0f"), NumericCode),
            Message.IsEmpty() ? TEXT("Read-only tool execution was rejected") : Message);
    }

    const TSharedPtr<FJsonObject>* Result = nullptr;
    if (!ResponseObject->TryGetObjectField(TEXT("result"), Result)
        || !Result || !Result->IsValid())
    {
        return BuildBridgeError(
            TEXT("MISSING_INTERNAL_RESULT"),
            TEXT("Revvy's internal MCP response did not contain a result"));
    }

    const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
    bool bIsError = false;
    (*Result)->TryGetBoolField(TEXT("isError"), bIsError);
    if ((*Result)->TryGetArrayField(TEXT("content"), Content) && Content)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Content)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object)
            {
                continue;
            }

            const TSharedPtr<FJsonObject> ContentObject = Value->AsObject();
            FString Type;
            FString Text;
            if (ContentObject.IsValid()
                && ContentObject->TryGetStringField(TEXT("type"), Type)
                && Type == TEXT("text")
                && ContentObject->TryGetStringField(TEXT("text"), Text))
            {
                if (bIsError)
                {
                    return BuildBridgeError(
                        TEXT("MCP_TOOL_ERROR"),
                        Text.IsEmpty()
                            ? TEXT("Read-only tool execution failed")
                            : Text);
                }
                return Text;
            }
        }
    }

    FString ResultJson;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> ResultWriter =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResultJson);
    FJsonSerializer::Serialize((*Result).ToSharedRef(), ResultWriter);
    return ResultJson;
}

FString FMCPServer::HandleResultPageCall(
    TSharedPtr<FJsonObject> Params,
    const TSharedPtr<FJsonValue>& RequestId,
    EMCPAccessScope AccessScope)
{
    FString Handle;
    int32 Offset = 0;
    const TSharedPtr<FJsonObject>* Arguments = nullptr;
    if (Params->TryGetObjectField(TEXT("arguments"), Arguments) && Arguments)
    {
        (*Arguments)->TryGetStringField(TEXT("handle"), Handle);
        // Accepted as a number by schema, but a client that sends "23600" should
        // not be told its handle expired.
        if (!(*Arguments)->TryGetNumberField(TEXT("offset"), Offset))
        {
            FString OffsetText;
            if ((*Arguments)->TryGetStringField(TEXT("offset"), OffsetText))
            {
                Offset = FCString::Atoi(*OffsetText);
            }
        }
    }

    if (Handle.IsEmpty())
    {
        return BuildJsonRpcError(RequestId, -32602,
            TEXT("Missing 'handle'. Use the handle from the bounded reply you are continuing."));
    }

    const FString PageText =
        RevvyOversizedResult::BuildPageReply(static_cast<int32>(AccessScope), Handle, Offset);
    // The only failure this can report is an unreadable handle, and BuildPageReply
    // renders that as the JSON error object the classifier below recognises.
    const bool bIsError = PageText.StartsWith(TEXT("{\"success\":false"));

    TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
    TextContent->SetStringField(TEXT("type"), TEXT("text"));
    TextContent->SetStringField(TEXT("text"), PageText);

    TArray<TSharedPtr<FJsonValue>> ContentArray;
    ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("content"), ContentArray);
    Result->SetBoolField(TEXT("isError"), bIsError);
    return BuildJsonRpcResponse(RequestId, Result);
}

FString FMCPServer::HandlePing(const TSharedPtr<FJsonValue>& RequestId)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("engine"), TEXT("unreal"));
    Result->SetStringField(TEXT("project_path"), GetCanonicalProjectPath());
    Result->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    Result->SetNumberField(TEXT("editor_port"), static_cast<double>(Config.Port));
    Result->SetStringField(TEXT("backend_auth"), TEXT("hmac-sha1-exact-v2"));
    return BuildJsonRpcResponse(RequestId, Result);
}

// ============ JSON-RPC Helpers ============

FString FMCPServer::BuildJsonRpcResponse(const TSharedPtr<FJsonValue>& RequestId, TSharedPtr<FJsonObject> Result)
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    
    if (RequestId.IsValid())
    {
        Response->SetField(TEXT("id"), RequestId);
    }
    else
    {
        Response->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
    }
    
    Response->SetObjectField(TEXT("result"), Result);
    
    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    
    return Output;
}

FString FMCPServer::BuildJsonRpcError(const TSharedPtr<FJsonValue>& RequestId, int32 Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    
    if (RequestId.IsValid())
    {
        Response->SetField(TEXT("id"), RequestId);
    }
    else
    {
        Response->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
    }
    
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetNumberField(TEXT("code"), Code);
    Error->SetStringField(TEXT("message"), Message);
    Response->SetObjectField(TEXT("error"), Error);
    
    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
    
    return Output;
}

// ============ Proxy Nudge ============

void FMCPServer::ShowProxyNudgeIfNeeded()
{
    // Prevent duplicate toasts within the same session
    static bool bShownThisSession = false;
    if (bShownThisSession)
    {
        return;
    }

    // Check if user has permanently dismissed it
    bool bDismissed = false;
    GConfig->GetBool(TEXT("Revvy.MCPServer"), TEXT("ProxyNudgeDismissed"), bDismissed, GEditorPerProjectIni);
    if (bDismissed)
    {
        return;
    }

    bShownThisSession = true;

    // Must run on game thread — we're on the socket thread here
    AsyncTask(ENamedThreads::GameThread, []()
    {
        TSharedPtr<TWeakPtr<SNotificationItem>> WeakItemHolder = MakeShared<TWeakPtr<SNotificationItem>>();

        FNotificationInfo Info(FText::FromString(
            TEXT("Your AI client is connected directly to Revvy (port 8088).\n\n")
            TEXT("The Revvy proxy keeps your tools available even when Unreal Editor ")
            TEXT("is closed — so you can open your AI tool first without losing MCP tools. ")
            TEXT("See the plugin docs to set it up.")));
        Info.bFireAndForget = false;
        Info.bUseThrobber = false;
        Info.ExpireDuration = 0.0f;
        Info.FadeOutDuration = 1.0f;

        Info.ButtonDetails.Add(FNotificationButtonInfo(
            FText::FromString(TEXT("Got it, don't show again")),
            FText::FromString(TEXT("Dismiss permanently")),
            FSimpleDelegate::CreateLambda([WeakItemHolder]()
            {
                GConfig->SetBool(TEXT("Revvy.MCPServer"), TEXT("ProxyNudgeDismissed"), true, GEditorPerProjectIni);
                GConfig->Flush(false, GEditorPerProjectIni);
                if (TSharedPtr<SNotificationItem> Item = WeakItemHolder->Pin())
                {
                    Item->SetCompletionState(SNotificationItem::CS_None);
                    Item->ExpireAndFadeout();
                }
            })
        ));

        Info.ButtonDetails.Add(FNotificationButtonInfo(
            FText::FromString(TEXT("Maybe later")),
            FText::FromString(TEXT("Close for now, remind me next session")),
            FSimpleDelegate::CreateLambda([WeakItemHolder]()
            {
                if (TSharedPtr<SNotificationItem> Item = WeakItemHolder->Pin())
                {
                    Item->SetCompletionState(SNotificationItem::CS_None);
                    Item->ExpireAndFadeout();
                }
            })
        ));

        TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
        if (Item.IsValid())
        {
            *WeakItemHolder = Item;
            Item->SetCompletionState(SNotificationItem::CS_Pending);
        }
    });
}

// ============ Security ============

namespace
{
    /** Constant-time string comparison — avoids timing side-channel on key checks. */
    bool ConstantTimeEquals(const FString& A, const FString& B)
    {
        const int32 LenA = A.Len();
        const int32 LenB = B.Len();
        uint32 Diff = static_cast<uint32>(LenA) ^ static_cast<uint32>(LenB);
        const int32 MaxLen = FMath::Max(LenA, LenB);
        for (int32 i = 0; i < MaxLen; ++i)
        {
            const TCHAR CharA = i < LenA ? A[i] : 0;
            const TCHAR CharB = i < LenB ? B[i] : 0;
            Diff |= static_cast<uint32>(CharA) ^ static_cast<uint32>(CharB);
        }
        return Diff == 0;
    }
}

EMCPAccessScope FMCPServer::ValidateApiKey(const TMap<FString, FString>& Headers) const
{
    if (Config.ReadOnlyApiKey.Len() < 32)
    {
        return EMCPAccessScope::None;
    }

    // Check Authorization header
    const FString* AuthHeader = Headers.Find(TEXT("authorization"));
    if (!AuthHeader)
    {
        return EMCPAccessScope::None;
    }

    // Support "Bearer <key>" format; a raw API key is also accepted.
    FString ProvidedKey = *AuthHeader;
    if (AuthHeader->StartsWith(TEXT("Bearer "), ESearchCase::IgnoreCase))
    {
        ProvidedKey = AuthHeader->Mid(7);
    }

    // Raw bearer credentials are public Safe/read-only capabilities only.
    // Full access requires an exact-body proxy HMAC below the HTTP layer.
    const bool bIsReadOnlyKey = ConstantTimeEquals(ProvidedKey, Config.ReadOnlyApiKey);

    if (bIsReadOnlyKey)
    {
        return EMCPAccessScope::ReadOnly;
    }
    return EMCPAccessScope::None;
}

bool FMCPServer::CheckRateLimit(const FString& ClientKey)
{
    // Sliding 60-second window, 600 requests/min per client (ChiR24 defaults).
    constexpr double WindowSeconds = 60.0;
    constexpr int32 MaxRequestsPerWindow = 600;

    const double Now = FPlatformTime::Seconds();

    FScopeLock Lock(&RateLimitLock);
    TArray<double>& Times = ClientRequestTimes.FindOrAdd(ClientKey);
    Times.RemoveAll([Now](double T) { return Now - T > WindowSeconds; });
    if (Times.Num() >= MaxRequestsPerWindow)
    {
        return false;
    }
    Times.Add(Now);

    // Bound the map so a churn of ephemeral ports can't grow it unbounded
    if (ClientRequestTimes.Num() > 64)
    {
        for (auto It = ClientRequestTimes.CreateIterator(); It; ++It)
        {
            It->Value.RemoveAll([Now](double T) { return Now - T > WindowSeconds; });
            if (It->Value.Num() == 0)
            {
                It.RemoveCurrent();
            }
        }
    }
    return true;
}

void FMCPServer::AppendCorsHeaders(const TMap<FString, FString>& RequestHeaders, TMap<FString, FString>& ResponseHeaders) const
{
    // Only browsers send Origin; only they need CORS headers. Echo the exact
    // validated origin — never "*", which would let any page read responses.
    const FString* Origin = RequestHeaders.Find(TEXT("origin"));
    if (Origin && !Origin->IsEmpty())
    {
        ResponseHeaders.Add(TEXT("Access-Control-Allow-Origin"), Origin->TrimStartAndEnd());
        ResponseHeaders.Add(TEXT("Vary"), TEXT("Origin"));
    }
    ResponseHeaders.Add(
        TEXT("Access-Control-Allow-Headers"),
        TEXT("Content-Type, Authorization, Mcp-Session-Id, MCP-Protocol-Version, Accept, Last-Event-ID, X-Revvy-Bridge-Instance, X-Revvy-Access-Scope, X-Revvy-Request-Timestamp, X-Revvy-Request-Nonce, X-Revvy-Request-Signature"));
    ResponseHeaders.Add(
        TEXT("Access-Control-Expose-Headers"),
        TEXT("Mcp-Session-Id, X-Revvy-Response-Signature"));
}

void FMCPServer::ValidateRevvyApiKeyAsync()
{
    // Upstream VibeUE phoned home to llm.vibeue.com to validate a paid API key
    // and disabled all MCP tools without one. This fork runs entirely locally:
    // no external validation, tools are always available.
    bIsRevvyApiKeyValid = true;
    LastValidationDate = FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
}
bool FMCPServer::ValidateOrigin(const TMap<FString, FString>& Headers) const
{
    const FString* Origin = Headers.Find(TEXT("origin"));

    if (!Origin)
    {
        // No origin header - likely a non-browser client like curl or an IDE
        return true;
    }

    // Strict parse-and-allowlist (DNS-rebinding defense). The previous substring
    // check let "http://localhost.evil.com" and "http://127.0.0.1.attacker.com"
    // through — the host must now be EXACTLY a loopback name.
    const FString Trimmed = Origin->TrimStartAndEnd();
    if (Trimmed.IsEmpty() ||
        Trimmed.Contains(TEXT("\r")) || Trimmed.Contains(TEXT("\n")) ||
        Trimmed.Equals(TEXT("null"), ESearchCase::IgnoreCase))
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Rejected request with malformed Origin"));
        return false;
    }

    // IDE webviews are trusted local surfaces. NOTE: "file://" was accepted on the
    // local branch but is deliberately NOT reinstated here — it is opaque-origin
    // territory (browsers send it as "null", which is rejected above) and would
    // reopen the hole this allowlist closes.
    if (Trimmed.StartsWith(TEXT("vscode-webview://")))
    {
        return true;
    }

    FString Scheme, HostPort;
    if (!Trimmed.Split(TEXT("://"), &Scheme, &HostPort))
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Rejected request with Origin: %s"), *Trimmed);
        return false;
    }

    const FString SchemeLower = Scheme.ToLower();
    if (SchemeLower != TEXT("http") && SchemeLower != TEXT("https"))
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Rejected request with Origin scheme: %s"), *Trimmed);
        return false;
    }

    // Origin is scheme://host[:port] — any path component is malformed
    if (HostPort.Contains(TEXT("/")))
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Rejected request with Origin: %s"), *Trimmed);
        return false;
    }

    FString Host = HostPort;
    FString PortStr;
    if (HostPort.StartsWith(TEXT("[")))
    {
        // IPv6 literal: [::1] or [::1]:port
        FString Bracketed, After;
        if (!HostPort.Split(TEXT("]"), &Bracketed, &After))
        {
            return false;
        }
        Host = Bracketed.Mid(1);
        if (After.StartsWith(TEXT(":")))
        {
            PortStr = After.Mid(1);
        }
        else if (!After.IsEmpty())
        {
            return false;
        }
    }
    else if (HostPort.Contains(TEXT(":")))
    {
        HostPort.Split(TEXT(":"), &Host, &PortStr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    }

    const FString HostLower = Host.ToLower();
    const bool bLoopbackHost =
        HostLower == TEXT("localhost") ||
        HostLower == TEXT("127.0.0.1") ||
        HostLower == TEXT("::1");
    if (!bLoopbackHost)
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Rejected request with Origin: %s"), *Trimmed);
        return false;
    }

    if (!PortStr.IsEmpty())
    {
        if (!PortStr.IsNumeric())
        {
            return false;
        }
        const int32 Port = FCString::Atoi(*PortStr);
        if (Port < 1 || Port > 65535)
        {
            return false;
        }
    }

    return true;
}

FString FMCPServer::GenerateSessionId() const
{
    return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
}

// ============ Tool Access ============

TArray<FMCPTool> FMCPServer::GetInternalTools() const
{
    TArray<FMCPTool> Result;
    
    FToolRegistry& Registry = FToolRegistry::Get();
    if (!Registry.IsInitialized())
    {
        UE_LOG(LogMCPServer, Warning, TEXT("ToolRegistry not initialized"));
        return Result;
    }
    
    TArray<FToolMetadata> EnabledTools = Registry.GetEnabledTools();
    Result.Reserve(EnabledTools.Num());
    
    for (const FToolMetadata& Tool : EnabledTools)
    {
        // Skip internal-only tools - they are not exposed via MCP to external clients
        if (Tool.bInternalOnly)
        {
            UE_LOG(LogMCPServer, Verbose, TEXT("Skipping internal-only tool for MCP: %s"), *Tool.Name);
            continue;
        }
        
        FMCPTool MCPTool;
        MCPTool.Name = Tool.Name;
        MCPTool.Description = Tool.Description;
        MCPTool.ServerName = TEXT("Revvy-Internal");

        // Carry annotations through to the MCP layer (spec 2025-03-26+).
        MCPTool.Annotations.Title = Tool.Annotations.Title;
        MCPTool.Annotations.bReadOnlyHint = Tool.Annotations.bReadOnlyHint;
        MCPTool.Annotations.bDestructiveHint = Tool.Annotations.bDestructiveHint;
        MCPTool.Annotations.bIdempotentHint = Tool.Annotations.bIdempotentHint;
        MCPTool.Annotations.bOpenWorldHint = Tool.Annotations.bOpenWorldHint;
        MCPTool.Annotations.bAnnotated = Tool.Annotations.bAnnotated;
        MCPTool.Annotations.ReadOnlyActions = Tool.Annotations.ReadOnlyActions;

        // Carry structured output schema (spec 2025-06-18).
        MCPTool.OutputSchema = Tool.OutputSchema;

        // Build input schema
        TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
        InputSchema->SetStringField(TEXT("type"), TEXT("object"));
        
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> RequiredArray;
        
        for (const FToolParameter& Param : Tool.Parameters)
        {
            TSharedPtr<FJsonObject> ParamSchema = MakeShared<FJsonObject>();
            const FString SchemaType = FToolParameter::NormalizeJsonSchemaType(Param.Type);
            ParamSchema->SetStringField(TEXT("type"), SchemaType);
            ParamSchema->SetStringField(TEXT("description"), Param.Description);
            
            // For array types, add items schema (required by JSON Schema spec)
            if (SchemaType == TEXT("array"))
            {
                TSharedPtr<FJsonObject> ItemsSchema = MakeShared<FJsonObject>();
                ItemsSchema->SetStringField(
                    TEXT("type"),
                    FToolParameter::NormalizeJsonSchemaType(Param.ArrayItemType));
                ParamSchema->SetObjectField(TEXT("items"), ItemsSchema);
            }
            
            Properties->SetObjectField(Param.Name, ParamSchema);
            
            if (Param.bRequired)
            {
                RequiredArray.Add(MakeShared<FJsonValueString>(Param.Name));
            }
        }
        
        InputSchema->SetObjectField(TEXT("properties"), Properties);
        if (RequiredArray.Num() > 0)
        {
            InputSchema->SetArrayField(TEXT("required"), RequiredArray);
        }
        
        MCPTool.InputSchema = InputSchema;
        Result.Add(MCPTool);
    }
    
    return Result;
}

FString FMCPServer::GetUnauthenticatedNativeToolCatalogJson() const
{
    TArray<TSharedPtr<FJsonValue>> ToolsArray;
    for (const FMCPTool& Tool : GetInternalTools())
    {
        if (!IsAllowedForUnauthenticatedNativeToolset(Tool))
        {
            continue;
        }

        TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
        ToolObj->SetStringField(TEXT("name"), Tool.Name);
        ToolObj->SetStringField(TEXT("description"), Tool.Description);
        ToolObj->SetObjectField(
            TEXT("inputSchema"),
            Tool.InputSchema.IsValid()
                ? Tool.InputSchema
                : MakeShared<FJsonObject>());

        if (TSharedPtr<FJsonObject> AnnObj = BuildAnnotationsJson(Tool.Annotations))
        {
            ToolObj->SetObjectField(TEXT("annotations"), AnnObj);
        }
        if (Tool.OutputSchema.IsValid())
        {
            ToolObj->SetObjectField(TEXT("outputSchema"), Tool.OutputSchema);
        }
        ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObj));
    }

    FString JsonString;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
            &JsonString);
    FJsonSerializer::Serialize(ToolsArray, Writer);
    return JsonString;
}

void FMCPServer::ExportToolManifest() const
{
    TArray<FMCPTool> Tools = GetInternalTools();

    // Build JSON array matching the tools/list schema
    TArray<TSharedPtr<FJsonValue>> ToolsArray;
    for (const FMCPTool& Tool : Tools)
    {
        TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
        ToolObj->SetStringField(TEXT("name"), Tool.Name);
        ToolObj->SetStringField(TEXT("description"), Tool.Description);

        if (Tool.InputSchema.IsValid())
        {
            ToolObj->SetObjectField(TEXT("inputSchema"), Tool.InputSchema);
        }
        else
        {
            TSharedPtr<FJsonObject> EmptySchema = MakeShared<FJsonObject>();
            EmptySchema->SetStringField(TEXT("type"), TEXT("object"));
            EmptySchema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
            ToolObj->SetObjectField(TEXT("inputSchema"), EmptySchema);
        }

        // MCP annotations (spec 2025-03-26+). Emitted only if marked explicitly.
        if (TSharedPtr<FJsonObject> AnnObj = BuildAnnotationsJson(Tool.Annotations))
        {
            ToolObj->SetObjectField(TEXT("annotations"), AnnObj);
        }

        if (Tool.OutputSchema.IsValid())
        {
            ToolObj->SetObjectField(TEXT("outputSchema"), Tool.OutputSchema);
        }

        ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObj));
    }

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(ToolsArray, Writer);

    // Write beside the standalone proxy config so every external client sees
    // the latest tool catalogue even while the editor is offline.
    FString ManifestDir = GetRevvyStateDir();
    FString ManifestPath = ManifestDir / TEXT("tools-manifest.json");

    IFileManager::Get().MakeDirectory(*ManifestDir, /*Tree=*/true);

    if (FFileHelper::SaveStringToFile(JsonString, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogMCPServer, Log, TEXT("Exported %d tools to %s"), Tools.Num(), *ManifestPath);
    }
    else
    {
        UE_LOG(LogMCPServer, Warning, TEXT("ExportToolManifest: Failed to write %s"), *ManifestPath);
    }
}

void FMCPServer::ProcessPendingRequests()
{
    check(IsInGameThread());

    TSharedPtr<FMCPPendingRequest, ESPMode::ThreadSafe> PendingRequest;
    while (PendingRequests.Dequeue(PendingRequest))
    {
        if (!PendingRequest.IsValid())
        {
            continue;
        }
        if (!PendingRequest->bCancelled)
        {
            PendingRequest->Response = HandleMCPRequest(
                PendingRequest->Body,
                PendingRequest->SessionId,
                PendingRequest->bIsNotification,
                PendingRequest->AccessScope);
        }
        PendingRequest->CompletionEvent->Trigger();
    }

    // Re-validate the API key when the calendar day rolls over so each day gets a usage log entry.
    // Throttled to one date check per minute to avoid overhead at 60 Hz.
    double Now = FPlatformTime::Seconds();
    if (Now - LastDayCheckTime >= 60.0)
    {
        LastDayCheckTime = Now;
        FString TodayStr = FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
        if (!LastValidationDate.IsEmpty() && TodayStr != LastValidationDate)
        {
            UE_LOG(LogMCPServer, Log, TEXT("New day detected (%s → %s), re-validating Revvy API key"), *LastValidationDate, *TodayStr);
            ValidateRevvyApiKeyAsync();
        }
    }

    // Clean up stale SSE connections
    FScopeLock Lock(&SSELock);
    SSEConnections.RemoveAll([](const TSharedPtr<FMCPSSEConnection>& Conn) {
        return !Conn->bIsActive;
    });
}

// ============ SSE Streaming Support ============

bool FMCPServer::HandleSSERequest(FSocket* ClientSocket, const TMap<FString, FString>& Headers)
{
    FString SessionId = Headers.FindRef(TEXT("mcp-session-id"));
    
    // Validate session exists if provided
    if (!SessionId.IsEmpty())
    {
        FScopeLock Lock(&SessionLock);
        if (!ActiveSessions.Contains(SessionId))
        {
            SendHttpResponse(ClientSocket, 404, TEXT("Not Found"), TEXT("text/plain"), TEXT("Session not found"));
            ClientSocket->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
            return false;
        }
    }
    
    // Check for Last-Event-ID for resumption
    FString LastEventIdStr = Headers.FindRef(TEXT("last-event-id"));
    int32 LastEventId = 0;
    if (!LastEventIdStr.IsEmpty())
    {
        LastEventId = FCString::Atoi(*LastEventIdStr);
        UE_LOG(LogMCPServer, Log, TEXT("SSE stream resuming from event ID: %d"), LastEventId);
    }
    
    // Send SSE response headers
    SendSSEResponse(ClientSocket, SessionId);
    
    // Create connection tracking
    TSharedPtr<FMCPSSEConnection> Connection = MakeShared<FMCPSSEConnection>();
    Connection->ClientSocket = ClientSocket;
    Connection->SessionId = SessionId;
    Connection->ConnectedAt = FDateTime::UtcNow();
    Connection->LastEventId = LastEventId;
    Connection->bIsActive = true;
    
    {
        FScopeLock Lock(&SSELock);
        SSEConnections.Add(Connection);
    }
    
    // Send initial empty event to prime reconnection (per MCP spec)
    int32 EventId = ++NextEventId;
    SendSSEEvent(ClientSocket, TEXT(""), EventId);
    
    // Send retry hint (1 second recommended by spec)
    FString RetryHint = TEXT("retry: 1000\n\n");
    FTCHARToUTF8 Converter(*RetryHint);
    int32 BytesSent = 0;
    ClientSocket->Send(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length(), BytesSent);
    
    UE_LOG(LogMCPServer, Log, TEXT("SSE stream opened for session: %s"), SessionId.IsEmpty() ? TEXT("<none>") : *SessionId);
    
    // Note: We keep the connection open for server-initiated messages
    // The connection will be cleaned up when the client disconnects or on shutdown
    // For now, we don't have server-initiated messages, so we just keep it open
    // The client can close the connection when done
    
    return true;
}

void FMCPServer::SendSSEResponse(FSocket* Socket, const FString& SessionId)
{
    if (!Socket)
    {
        return;
    }
    
    FString Response = TEXT("HTTP/1.1 200 OK\r\n");
    Response += TEXT("Content-Type: text/event-stream\r\n");
    Response += TEXT("Cache-Control: no-cache\r\n");
    Response += TEXT("Connection: keep-alive\r\n");
    Response += TEXT("Access-Control-Allow-Origin: *\r\n");
    Response += TEXT("Access-Control-Allow-Headers: Content-Type, Authorization, Mcp-Session-Id, MCP-Protocol-Version, Accept, Last-Event-ID, X-Revvy-Bridge-Instance, X-Revvy-Access-Scope, X-Revvy-Request-Timestamp, X-Revvy-Request-Nonce, X-Revvy-Request-Signature\r\n");
    Response += TEXT("Access-Control-Expose-Headers: Mcp-Session-Id, X-Revvy-Response-Signature\r\n");
    
    if (!SessionId.IsEmpty())
    {
        Response += FString::Printf(TEXT("Mcp-Session-Id: %s\r\n"), *SessionId);
    }
    
    Response += TEXT("\r\n");
    
    FTCHARToUTF8 Converter(*Response);
    int32 BytesSent = 0;
    Socket->Send(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length(), BytesSent);
}

bool FMCPServer::SendSSEEvent(FSocket* ClientSocket, const FString& Data, int32 EventId)
{
    if (!ClientSocket)
    {
        return false;
    }
    
    FString Event;
    
    // Add event ID for resumability
    if (EventId >= 0)
    {
        Event += FString::Printf(TEXT("id: %d\n"), EventId);
    }
    
    // Data field - handle multi-line data
    if (Data.IsEmpty())
    {
        Event += TEXT("data: \n");
    }
    else
    {
        // SSE requires each line of data to be prefixed with "data: "
        TArray<FString> Lines;
        Data.ParseIntoArray(Lines, TEXT("\n"));
        for (const FString& Line : Lines)
        {
            Event += FString::Printf(TEXT("data: %s\n"), *Line);
        }
    }
    
    Event += TEXT("\n"); // Empty line to signal end of event
    
    FTCHARToUTF8 Converter(*Event);
    int32 BytesSent = 0;
    bool bSuccess = ClientSocket->Send(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length(), BytesSent);
    
    if (!bSuccess)
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Failed to send SSE event"));
    }
    
    return bSuccess;
}

bool FMCPServer::ValidateProtocolVersion(const TMap<FString, FString>& Headers) const
{
    const FString* VersionHeader = Headers.Find(TEXT("mcp-protocol-version"));
    
    if (!VersionHeader)
    {
        // Per spec: if no header and no other way to identify version, assume 2025-03-26
        // We'll be permissive and allow missing header
        return true;
    }
    
    // Check if it's a version we support
    if (SUPPORTED_PROTOCOL_VERSIONS.Contains(*VersionHeader))
    {
        return true;
    }
    
    UE_LOG(LogMCPServer, Warning, TEXT("Unsupported MCP-Protocol-Version: %s"), **VersionHeader);
    return false;
}

bool FMCPServer::AcceptsSSE(const TMap<FString, FString>& Headers) const
{
    const FString* AcceptHeader = Headers.Find(TEXT("accept"));
    if (!AcceptHeader)
    {
        return false;
    }
    
    // Check if text/event-stream is in the Accept header
    return AcceptHeader->Contains(TEXT("text/event-stream"));
}

// ============ Config Persistence ============

void FMCPServer::LoadConfig()
{
    Config.bEnabled = GetEnabledFromConfig();
    Config.Port = GetPortFromConfig();
    Config.BridgeHmacKey = GetApiKeyFromConfig();
}

void FMCPServer::SaveConfig()
{
    SaveEnabledToConfig(Config.bEnabled);
    SavePortToConfig(Config.Port);
    SaveApiKeyToConfig(Config.BridgeHmacKey);
}

bool FMCPServer::GetEnabledFromConfig()
{
    bool bEnabled = true; // Default to enabled
    GConfig->GetBool(TEXT("Revvy.MCPServer"), TEXT("Enabled"), bEnabled, GEditorPerProjectIni);
    return bEnabled;
}

void FMCPServer::SaveEnabledToConfig(bool bEnabled)
{
    GConfig->SetBool(TEXT("Revvy.MCPServer"), TEXT("Enabled"), bEnabled, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

int32 FMCPServer::GetPortFromConfig()
{
    return ResolveConfiguredPort(
        TEXT("Revvy.MCPServer"), TEXT("ue_port"), DefaultEditorPort);
}

void FMCPServer::SavePortToConfig(int32 Port)
{
    GConfig->SetInt(TEXT("Revvy.MCPServer"), TEXT("Port"), Port, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

FString FMCPServer::GetApiKeyFromConfig()
{
    FString BridgeHmacKey;
    TryLoadBridgeHmacKey(BridgeHmacKey);
    return BridgeHmacKey;
}

void FMCPServer::SaveApiKeyToConfig(const FString& ApiKey)
{
    if (ApiKey.Len() >= 32 && !WriteBridgeHmacKey(ApiKey))
    {
        UE_LOG(LogMCPServer, Error,
            TEXT("SECURITY: failed to save bridge HMAC key to protected storage"));
    }
    GConfig->RemoveKey(TEXT("Revvy.MCPServer"), TEXT("ApiKey"), GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

// ============ Proxy Config Persistence ============

bool FMCPServer::GetProxyEnabledFromConfig()
{
    bool bEnabled = true; // Default to enabled (zero-config setup)
    GConfig->GetBool(TEXT("Revvy.MCPProxy"), TEXT("Enabled"), bEnabled, GEditorPerProjectIni);
    return bEnabled;
}

void FMCPServer::SaveProxyEnabledToConfig(bool bEnabled)
{
    GConfig->SetBool(TEXT("Revvy.MCPProxy"), TEXT("Enabled"), bEnabled, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

bool FMCPServer::GetProxyAutoStartFromConfig()
{
    bool bAutoStart = true; // Default to auto-start (zero-config setup)
    GConfig->GetBool(TEXT("Revvy.MCPProxy"), TEXT("AutoStart"), bAutoStart, GEditorPerProjectIni);
    return bAutoStart;
}

void FMCPServer::SaveProxyAutoStartToConfig(bool bAutoStart)
{
    GConfig->SetBool(TEXT("Revvy.MCPProxy"), TEXT("AutoStart"), bAutoStart, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

int32 FMCPServer::GetProxyPortFromConfig()
{
    return ResolveConfiguredPort(
        TEXT("Revvy.MCPProxy"), TEXT("proxy_port"), DefaultProxyPort);
}

void FMCPServer::SaveProxyPortToConfig(int32 Port)
{
    GConfig->SetInt(TEXT("Revvy.MCPProxy"), TEXT("Port"), Port, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

FString FMCPServer::GetProxyPythonPathFromConfig()
{
    FString Path;
    GConfig->GetString(TEXT("Revvy.MCPProxy"), TEXT("PythonPath"), Path, GEditorPerProjectIni);
    return Path; // Empty = use system "python"
}

void FMCPServer::SaveProxyPythonPathToConfig(const FString& Path)
{
    GConfig->SetString(TEXT("Revvy.MCPProxy"), TEXT("PythonPath"), *Path, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
}

// ============ Proxy Control ============

/**
 * Whether the process Revvy recorded as its proxy is alive and owns this port.
 *
 * SECURITY: the only evidence that the listener on a loopback port is Revvy's
 * proxy rather than whatever else got there first. Callers that are about to
 * disclose the Full access bearer must gate on this — an open port is not an
 * identity, and a local process that squats the port would otherwise be handed
 * a token that unlocks every mutating tool.
 */
static bool RecordedRevvyProxyOwnsPort(int32 ProxyPort)
{
    FString StateJson;
    const FString StatePath = GetRevvyStateDir() / TEXT("proxy-process.json");
    if (!FFileHelper::LoadFileToString(StateJson, *StatePath))
    {
        return false;
    }
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StateJson);
    TSharedPtr<FJsonObject> StateObject;
    double StatePid = 0.0;
    double StatePort = 0.0;
    if (!FJsonSerializer::Deserialize(Reader, StateObject)
        || !StateObject.IsValid()
        || !StateObject->TryGetNumberField(TEXT("pid"), StatePid)
        || !StateObject->TryGetNumberField(TEXT("proxy_port"), StatePort))
    {
        return false;
    }
    const uint32 ProcessId = static_cast<uint32>(StatePid);
    return ProcessId > 0
        && static_cast<int32>(StatePort) == ProxyPort
        && FPlatformProcess::IsApplicationRunning(ProcessId);
}

static bool IsLoopbackListenerRunning(int32 Port)
{
    if (!IsValidTcpPort(Port))
    {
        return false;
    }

    ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSub)
    {
        return false;
    }

    TSharedRef<FInternetAddr> Addr = SocketSub->CreateInternetAddr();
    bool bIsValid = false;
    Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
    Addr->SetPort(Port);
    if (!bIsValid)
    {
        return false;
    }

    FSocket* TestSocket = SocketSub->CreateSocket(NAME_Stream, TEXT("ProxyCheck"), false);
    if (!TestSocket)
    {
        return false;
    }
    ON_SCOPE_EXIT { SocketSub->DestroySocket(TestSocket); };

    // Non-blocking connect + 100ms completion wait.
    TestSocket->SetNonBlocking(true);
    bool bConnectStarted = TestSocket->Connect(*Addr);
    if (!bConnectStarted)
    {
        const ESocketErrors LastErr = SocketSub->GetLastErrorCode();
        if (LastErr == SE_EINPROGRESS || LastErr == SE_EWOULDBLOCK)
        {
            bConnectStarted = true;
        }
    }

    // Connect() may report success while a non-blocking connection is still in
    // progress. WaitForWrite also completes on refusal, so verify socket state.
    return bConnectStarted
        && TestSocket->Wait(
            ESocketWaitConditions::WaitForWrite,
            FTimespan::FromMilliseconds(100))
        && TestSocket->GetConnectionState() == SCS_Connected;
}

static bool TryGetLiveIntegratedReddyProxy(int32& OutPort, FString& OutSafeUrl)
{
    return TryReadConfiguredIntegratedReddyProxy(OutPort, OutSafeUrl)
        && IsLoopbackListenerRunning(OutPort);
}

bool FMCPServer::IsProxyRunning() const
{
    // Throttle to at most one real check every 3 seconds — this is called from Slate
    // tick lambdas and a blocking socket connect would freeze the game thread.
    double Now = FPlatformTime::Seconds();
    if (Now - LastProxyCheckTime < 3.0)
    {
        return bCachedProxyRunning;
    }
    LastProxyCheckTime = Now;

    // Reddy owns its in-process proxy lifecycle and intentionally has no
    // proxy-process.json. A strict project route plus a live loopback listener is
    // enough to avoid starting a competing standalone proxy because this branch
    // is used only for lifecycle suppression and Safe access; no secret is sent.
    int32 IntegratedPort = 0;
    FString IntegratedSafeUrl;
    if (TryGetLiveIntegratedReddyProxy(IntegratedPort, IntegratedSafeUrl))
    {
        bCachedProxyRunning = true;
        return true;
    }

    const int32 ProxyPort = GetProxyPortFromConfig();
    // For a standalone proxy, an answering port is not an identity: the recorded
    // process must still be the one behind it. The bearer probe gates on the same
    // evidence before putting a Full access token on the wire.
    bCachedProxyRunning = IsLoopbackListenerRunning(ProxyPort)
        && RecordedRevvyProxyOwnsPort(ProxyPort);
    return bCachedProxyRunning;
}

FString FMCPServer::EditorFullAccessToken;
bool FMCPServer::bEditorFullAccessTokenPinned = false;
bool FMCPServer::bProxyAdoptedEditorFullAccessToken = false;

void FMCPServer::EnsureEditorFullAccessToken()
{
    if (!EditorFullAccessToken.IsEmpty())
    {
        return;
    }

    const FString Pinned = FPlatformMisc::GetEnvironmentVariable(TEXT("REVVY_FULL_ACCESS_TOKEN"));
    if (Pinned.Len() >= 32)
    {
        // The operator pinned it, so every Revvy process started from this user
        // session already shares the value — including a proxy someone else launched.
        EditorFullAccessToken = Pinned;
        bEditorFullAccessTokenPinned = true;
        UE_LOG(LogMCPServer, Log, TEXT("Adopted a pinned REVVY_FULL_ACCESS_TOKEN for editor sessions"));
        return;
    }

    if (!Pinned.IsEmpty())
    {
        UE_LOG(LogMCPServer, Warning,
            TEXT("REVVY_FULL_ACCESS_TOKEN is shorter than 32 characters and was ignored"));
    }

    EditorFullAccessToken =
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower()
        + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    bEditorFullAccessTokenPinned = false;
    // Published to this process so both the proxy and the CLI sessions inherit it.
    FPlatformMisc::SetEnvironmentVar(TEXT("REVVY_FULL_ACCESS_TOKEN"), *EditorFullAccessToken);
}

bool FMCPServer::IsEditorFullAccessAvailable()
{
    return EditorFullAccessToken.Len() >= 32
        && (bEditorFullAccessTokenPinned || bProxyAdoptedEditorFullAccessToken);
}

bool FMCPServer::ProbeProxyAcceptsEditorBearer()
{
    // A routed Reddy endpoint is an external integrated proxy. Its Full access
    // capability is derived and granted by Reddy itself, not by this editor's
    // standalone-proxy bearer. Refuse the probe before consulting cached bearer
    // state so even a stale adoption flag cannot put EditorFullAccessToken on
    // this configured route.
    int32 IntegratedPort = 0;
    FString IntegratedSafeUrl;
    if (TryReadConfiguredIntegratedReddyProxy(IntegratedPort, IntegratedSafeUrl))
    {
        UE_LOG(LogMCPServer, Log,
            TEXT("Reddy integrated proxy is project-routed; editor bearer probe disabled"));
        return false;
    }

    if (bProxyAdoptedEditorFullAccessToken)
    {
        return true;
    }
    EnsureEditorFullAccessToken();
    if (EditorFullAccessToken.Len() < 32)
    {
        return false;
    }

    const int32 ProxyPort = GetProxyPortFromConfig();
    // SECURITY: the probe puts the Full access bearer on the wire, so it goes only
    // to a listener Revvy recorded as its own proxy and that is still alive. A port
    // that merely answers proves nothing about who is behind it, and handing this
    // token to a local squatter would give it every mutating tool.
    if (!RecordedRevvyProxyOwnsPort(ProxyPort))
    {
        return false;
    }

    // GET /mcp without an SSE Accept: the proxy checks the bearer first and then
    // answers a one-line 200 for a non-stream request. Nothing is started, no tool
    // runs, and a rejection is a plain 401 — so the reply distinguishes exactly the
    // thing being asked and nothing else.
    ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSub)
    {
        return false;
    }

    TSharedRef<FInternetAddr> Addr = SocketSub->CreateInternetAddr();
    bool bIsValid = false;
    Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
    Addr->SetPort(ProxyPort);
    if (!bIsValid)
    {
        return false;
    }

    FSocket* Socket = SocketSub->CreateSocket(NAME_Stream, TEXT("ProxyBearerProbe"), false);
    if (!Socket)
    {
        return false;
    }
    // Bounded on every leg: this runs on the startup path, and a proxy that accepts
    // the connection but never answers must not hold the editor open.
    ON_SCOPE_EXIT { SocketSub->DestroySocket(Socket); };
    Socket->SetNonBlocking(true);

    if (!Socket->Connect(*Addr))
    {
        const ESocketErrors LastErr = SocketSub->GetLastErrorCode();
        if (LastErr != SE_EINPROGRESS && LastErr != SE_EWOULDBLOCK)
        {
            return false;
        }
    }
    if (!Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(300))
        || Socket->GetConnectionState() != SCS_Connected)
    {
        return false;
    }

    const FString Request = FString::Printf(
        TEXT("GET /mcp HTTP/1.1\r\n")
        TEXT("Host: 127.0.0.1:%d\r\n")
        TEXT("Authorization: Bearer %s\r\n")
        TEXT("Accept: text/plain\r\n")
        TEXT("Connection: close\r\n\r\n"),
        ProxyPort,
        *EditorFullAccessToken);
    const FTCHARToUTF8 Converter(*Request);
    int32 BytesSent = 0;
    if (!Socket->Send(
            reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length(), BytesSent)
        || BytesSent != Converter.Length())
    {
        return false;
    }

    if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(700)))
    {
        return false;
    }
    uint8 Buffer[256] = {};
    int32 BytesRead = 0;
    if (!Socket->Recv(Buffer, sizeof(Buffer) - 1, BytesRead) || BytesRead <= 0)
    {
        return false;
    }

    // Only the status line is read. A 200 means the bearer was accepted; a 401 means
    // this proxy answers to a different token, which is a valid state and not an error.
    // Buffer is zero-initialised and Recv is capped one short of its size, so it is
    // NUL-terminated whatever came back.
    const FString StatusLine =
        FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer))).TrimStartAndEnd();
    const bool bAccepted = StatusLine.StartsWith(TEXT("HTTP/1.1 200"))
        || StatusLine.StartsWith(TEXT("HTTP/1.0 200"));
    if (bAccepted)
    {
        bProxyAdoptedEditorFullAccessToken = true;
        UE_LOG(LogMCPServer, Log,
            TEXT("The proxy already on port %d accepts this editor's Full access bearer"),
            ProxyPort);
    }
    else
    {
        UE_LOG(LogMCPServer, Log,
            TEXT("The proxy already on port %d does not accept this editor's bearer; ")
            TEXT("editor sessions stay read-only. Pin REVVY_FULL_ACCESS_TOKEN before ")
            TEXT("starting both, or let the editor start the proxy."),
            ProxyPort);
    }
    return bAccepted;
}

FString FMCPServer::WriteEditorMcpConfig()
{
    int32 IntegratedPort = 0;
    FString IntegratedSafeUrl;
    const bool bIntegratedProxy = TryReadConfiguredIntegratedReddyProxy(
        IntegratedPort, IntegratedSafeUrl);

    bool bFullAccess = false;
    FString ServerUrl;
    if (bIntegratedProxy)
    {
        // Reddy routes access per project. Editor-owned sessions remain Safe and
        // never receive the standalone EditorFullAccessToken, even when the
        // project file happens to contain the route's Full (/mcp) form.
        ServerUrl = IntegratedSafeUrl;
    }
    else
    {
        EnsureEditorFullAccessToken();
        // Also asked here, not only on the start path: a session can be launched
        // into a standalone proxy this editor never tried to start. Ask whether
        // it accepts the bearer before defaulting that session to Safe.
        if (!IsEditorFullAccessAvailable())
        {
            ProbeProxyAcceptsEditorBearer();
        }
        bFullAccess = IsEditorFullAccessAvailable();
        const int32 ProxyPort = GetProxyPortFromConfig();
        ServerUrl = FString::Printf(
            TEXT("http://127.0.0.1:%d/mcp%s"),
            ProxyPort,
            bFullAccess ? TEXT("") : TEXT("/safe"));
    }

    TSharedPtr<FJsonObject> Server = MakeShared<FJsonObject>();
    Server->SetStringField(TEXT("type"), TEXT("http"));
    Server->SetStringField(TEXT("url"), ServerUrl);
    if (bFullAccess)
    {
        // The variable reference, never the secret: the CLI inherits the value from
        // this process, so the file itself stays harmless if it is ever copied.
        TSharedPtr<FJsonObject> Headers = MakeShared<FJsonObject>();
        Headers->SetStringField(TEXT("Authorization"), TEXT("Bearer ${REVVY_FULL_ACCESS_TOKEN}"));
        Server->SetObjectField(TEXT("headers"), Headers);
    }

    TSharedPtr<FJsonObject> Servers = MakeShared<FJsonObject>();
    Servers->SetObjectField(TEXT("Revvy"), Server);
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetObjectField(TEXT("mcpServers"), Servers);

    FString JsonString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

    const FString ConfigPath = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Revvy") / TEXT("mcp-editor.json"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), /*Tree=*/true);
    if (!FFileHelper::SaveStringToFile(
            JsonString, *ConfigPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Could not write the editor MCP config: %s"), *ConfigPath);
        return FString();
    }

    UE_LOG(LogMCPServer, Log,
        TEXT("Editor MCP config written (%s access): %s"),
        bFullAccess ? TEXT("Full") : TEXT("Safe"), *ConfigPath);
    return ConfigPath;
}

bool FMCPServer::StartProxy()
{
    int32 IntegratedPort = 0;
    FString IntegratedSafeUrl;
    if (TryReadConfiguredIntegratedReddyProxy(IntegratedPort, IntegratedSafeUrl))
    {
        // Reddy owns this route and its per-project capabilities. Never start a
        // competing standalone proxy while the route remains configured: doing so
        // would reallocate proxy_port/ue_port but leave clients on this route.
        // This branch also never probes Reddy with EditorFullAccessToken.
        const bool bIntegratedLive = IsLoopbackListenerRunning(IntegratedPort);
        bCachedProxyRunning = bIntegratedLive;
        LastProxyCheckTime = FPlatformTime::Seconds();
        if (bIntegratedLive)
        {
            UE_LOG(LogMCPServer, Log,
                TEXT("Reddy integrated proxy already running on port %d; ")
                TEXT("standalone proxy start skipped (Safe route: %s)"),
                IntegratedPort,
                *IntegratedSafeUrl);
            return true;
        }

        UE_LOG(LogMCPServer, Warning,
            TEXT("Reddy integrated route is configured but port %d is offline; ")
            TEXT("standalone proxy was not started to preserve the project route. ")
            TEXT("Start or restart Reddy and retry: %s"),
            IntegratedPort,
            *IntegratedSafeUrl);
        return false;
    }

    // The standalone child inherits this process's environment, so the bearer
    // must exist before that child is spawned.
    EnsureEditorFullAccessToken();
    // Discard a recent cached integrated-proxy result if Reddy disappeared between
    // lifecycle checks; the standalone ownership check below must be fresh.
    LastProxyCheckTime = -999.0;

    if (IsProxyRunning())
    {
        UE_LOG(LogMCPServer, Log, TEXT("MCP Proxy already running on port %d — skipping start"), GetProxyPortFromConfig());
        // Skipping the spawn is not the same as having no Full access. A proxy the
        // operator started from a shell that already carried the token answers to
        // this editor's bearer, and assuming otherwise pinned every in-editor
        // session to read-only. Ask the proxy instead of inferring from who
        // started it.
        ProbeProxyAcceptsEditorBearer();
        return true;
    }

    // Locate the proxy script relative to the plugin directory
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Revvy"));
    if (!Plugin.IsValid())
    {
        UE_LOG(LogMCPServer, Warning, TEXT("StartProxy: Revvy plugin not found via IPluginManager"));
        return false;
    }
    FString ScriptPath = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Content/Python/revvy-proxy.py"));

    FString PythonExe = GetProxyPythonPathFromConfig();
    if (PythonExe.IsEmpty()) PythonExe = TEXT("python");

    // Ensure the proxy config JSON is up to date before spawning
    WriteProxyConfigJson();

    FString Params = FString::Printf(TEXT("\"%s\""), *ScriptPath);
    uint32 OutProcessId = 0;
    FProcHandle SpawnedProxyHandle = FPlatformProcess::CreateProc(*PythonExe, *Params,
        /*bLaunchDetached=*/true, /*bLaunchHidden=*/true, /*bLaunchReallyHidden=*/true,
        &OutProcessId, 0, nullptr, nullptr);

    if (SpawnedProxyHandle.IsValid())
    {
        // Close only our process handle. The standalone proxy intentionally
        // outlives this editor process and is stopped through the revvy CLI.
        FPlatformProcess::CloseProc(SpawnedProxyHandle);
        bProxyOwnedByUs = false;
        bCachedProxyRunning = false;

        const double ReadinessDeadline = FPlatformTime::Seconds() + 5.0;
        while (FPlatformTime::Seconds() < ReadinessDeadline)
        {
            LastProxyCheckTime = -999.0;
            if (IsProxyRunning())
            {
                // It inherited our environment, so it accepts our Full bearer.
                bProxyAdoptedEditorFullAccessToken = true;
                UE_LOG(LogMCPServer, Log, TEXT("MCP Proxy started (PID %d) on port %d"), OutProcessId, GetProxyPortFromConfig());
                return true;
            }
            FPlatformProcess::Sleep(0.05f);
        }

        UE_LOG(LogMCPServer, Warning,
            TEXT("StartProxy: process PID %d launched but Revvy proxy did not become ready"),
            OutProcessId);
        return false;
    }

    UE_LOG(LogMCPServer, Warning, TEXT("StartProxy: Failed to spawn process — python='%s', script='%s'"), *PythonExe, *ScriptPath);
    return false;
}

void FMCPServer::StopProxy()
{
    if (!bProxyOwnedByUs || !ProxyProcHandle.IsValid()) return;

    FPlatformProcess::TerminateProc(ProxyProcHandle, /*bKillTree=*/true);
    FPlatformProcess::CloseProc(ProxyProcHandle);
    bProxyOwnedByUs = false;
    bCachedProxyRunning = false;
    LastProxyCheckTime = FPlatformTime::Seconds();
    UE_LOG(LogMCPServer, Log, TEXT("MCP Proxy stopped"));
}

void FMCPServer::WriteProxyConfigJson() const
{
    static FCriticalSection ProxyConfigWriteLock;
    FScopeLock WriteLock(&ProxyConfigWriteLock);

    const FString JsonPath = GetProxyRuntimeConfigPath();
    const int32 ResolvedProxyPort = GetProxyPortFromConfig();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(JsonPath), /*Tree=*/true);

    TSharedPtr<FJsonObject> JsonObj;
    FString ExistingJson;
    if (FFileHelper::LoadFileToString(ExistingJson, *JsonPath))
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExistingJson);
        FJsonSerializer::Deserialize(Reader, JsonObj);
    }
    if (!JsonObj.IsValid())
    {
        JsonObj = MakeShared<FJsonObject>();
    }
    const bool bUsesProjectScopedPorts =
        RuntimeConfigMatchesCurrentUnrealProject(JsonObj);

    const FString ProjectName = FApp::GetProjectName();
    const FString ProjectPath = GetCanonicalProjectPath();
    JsonObj->RemoveField(TEXT("full_access_token"));
    JsonObj->SetNumberField(TEXT("config_version"), 3.0);
    if (bUsesProjectScopedPorts)
    {
        JsonObj->SetStringField(TEXT("port_scope"), TEXT("project"));
    }
    else
    {
        // An editor-first default-port session must stay reachable on 8088,
        // but must not claim that legacy pair as this project's permanent
        // allocation. Revvy migrates it after the listener stops.
        JsonObj->RemoveField(TEXT("port_scope"));
    }
    JsonObj->SetStringField(TEXT("engine"), TEXT("unreal"));
    JsonObj->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    JsonObj->SetStringField(TEXT("bearer_token"), Config.ReadOnlyApiKey);
    JsonObj->SetStringField(TEXT("bearer_scope"), TEXT("read_only"));
    JsonObj->SetStringField(TEXT("backend_auth"), TEXT("hmac-sha1-exact-v2"));
    JsonObj->SetNumberField(TEXT("proxy_port"), static_cast<double>(ResolvedProxyPort));
    JsonObj->SetBoolField(TEXT("bridge_running"), bIsRunning.load());
    JsonObj->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    if (bIsRunning)
    {
        // Config.Port is the port that successfully bound. Re-reading a file or
        // INI here could publish a route different from the live socket.
        JsonObj->SetNumberField(TEXT("ue_port"), static_cast<double>(Config.Port));
    }
    JsonObj->SetStringField(TEXT("project_name"), ProjectName);
    JsonObj->SetStringField(TEXT("project_path"), ProjectPath);
    TSharedPtr<FJsonObject> ActiveProject = MakeShared<FJsonObject>();
    ActiveProject->SetStringField(TEXT("name"), ProjectName);
    ActiveProject->SetStringField(TEXT("path"), ProjectPath);
    JsonObj->SetObjectField(TEXT("active_project"), ActiveProject);
    if (!JsonObj->HasField(TEXT("installed_project")))
    {
        JsonObj->SetObjectField(TEXT("installed_project"), ActiveProject);
    }
    JsonObj->SetStringField(TEXT("updated_at"), FDateTime::UtcNow().ToIso8601());

    FString JsonStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
    FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

    if (SaveStringToFileAtomically(JsonStr, JsonPath))
    {
        UE_LOG(LogMCPServer, Log, TEXT("Wrote standalone proxy config to %s"), *JsonPath);
    }
    else
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Failed to write standalone proxy config to %s"), *JsonPath);
    }

    // Invalidate the cached proxy status so the UI re-checks against the new port on next tick
    LastProxyCheckTime = -999.0;
}

void FMCPServer::EnsureProjectMcpConfig() const
{
    const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    auto LoadJsonObject = [](const FString& Path, TSharedPtr<FJsonObject>& OutObject) -> bool
    {
        OutObject = MakeShared<FJsonObject>();
        if (!FPaths::FileExists(Path))
        {
            return true;
        }
        FString JsonString;
        if (!FFileHelper::LoadFileToString(JsonString, *Path))
        {
            return false;
        }
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
        return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
    };

    auto SaveJsonObject = [](const FString& Path, const TSharedPtr<FJsonObject>& Object) -> bool
    {
        FString JsonString;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        JsonString.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
        JsonString += TEXT("\n");
        return FFileHelper::SaveStringToFile(
            JsonString, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };

    // Merge Revvy into .mcp.json while preserving every other MCP server.
    const FString McpJsonPath = ProjectDir / TEXT(".mcp.json");
    TSharedPtr<FJsonObject> McpRoot;
    if (LoadJsonObject(McpJsonPath, McpRoot))
    {
        bool bMcpModified = false;
        TSharedPtr<FJsonObject> Servers;
        const TSharedPtr<FJsonObject>* ExistingServers = nullptr;
        if (McpRoot->TryGetObjectField(TEXT("mcpServers"), ExistingServers) && ExistingServers)
        {
            Servers = *ExistingServers;
        }
        else if (!McpRoot->HasField(TEXT("mcpServers")))
        {
            Servers = MakeShared<FJsonObject>();
            McpRoot->SetObjectField(TEXT("mcpServers"), Servers);
            bMcpModified = true;
        }

        if (Servers.IsValid())
        {
            TSharedPtr<FJsonObject> RevvyServer;
            const TSharedPtr<FJsonObject>* ExistingRevvy = nullptr;
            if (Servers->TryGetObjectField(TEXT("Revvy"), ExistingRevvy) && ExistingRevvy)
            {
                RevvyServer = *ExistingRevvy;
            }
            else
            {
                RevvyServer = MakeShared<FJsonObject>();
                Servers->SetObjectField(TEXT("Revvy"), RevvyServer);
                bMcpModified = true;
            }
            const FString ProxyUrl = FString::Printf(
                TEXT("http://127.0.0.1:%d/mcp/safe"), GetProxyPortFromConfig());
            FString ExistingType;
            FString ExistingUrl;
            const bool bHasUrl = RevvyServer->TryGetStringField(TEXT("url"), ExistingUrl);

            // Only repair entries Revvy itself writes. A user who wired Full access
            // adds an Authorization header, and a foreign URL belongs to whoever put
            // it there — overwriting either turned an intentional setup into a silent
            // downgrade on every editor start.
            const bool bUserManagedAuth = RevvyServer->HasField(TEXT("headers"));
            const bool bRevvyShapedUrl =
                !bHasUrl
                || ExistingUrl.IsEmpty()
                || IsManagedLegacyMcpUrl(ExistingUrl);
            const bool bMayRewriteUrl = !bUserManagedAuth && bRevvyShapedUrl;

            if (!RevvyServer->TryGetStringField(TEXT("type"), ExistingType)
                || ExistingType != TEXT("http"))
            {
                RevvyServer->SetStringField(TEXT("type"), TEXT("http"));
                bMcpModified = true;
            }
            if (bMayRewriteUrl)
            {
                if (!bHasUrl || ExistingUrl != ProxyUrl)
                {
                    RevvyServer->SetStringField(TEXT("url"), ProxyUrl);
                    bMcpModified = true;
                }
            }
            else
            {
                UE_LOG(LogMCPServer, Log,
                    TEXT("Left the Revvy MCP endpoint in %s as configured: %s"),
                    *McpJsonPath, *ExistingUrl);
            }

            if (bMcpModified && SaveJsonObject(McpJsonPath, McpRoot))
            {
                UE_LOG(LogMCPServer, Log, TEXT("Merged Revvy MCP config into %s"), *McpJsonPath);
            }
        }
        else
        {
            UE_LOG(LogMCPServer, Warning, TEXT("Cannot merge %s: mcpServers is not an object"), *McpJsonPath);
        }
    }
    else
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Cannot parse existing MCP config; left unchanged: %s"), *McpJsonPath);
    }

    // Merge the allow-list without replacing permissions or other user settings.
    const FString ClaudeDir = ProjectDir / TEXT(".claude");
    const FString SettingsPath = ClaudeDir / TEXT("settings.local.json");
    PlatformFile.CreateDirectoryTree(*ClaudeDir);
    TSharedPtr<FJsonObject> SettingsRoot;
    if (LoadJsonObject(SettingsPath, SettingsRoot))
    {
        bool bClaudeModified = false;
        TArray<TSharedPtr<FJsonValue>> EnabledServers;
        const TArray<TSharedPtr<FJsonValue>>* ExistingEnabled = nullptr;
        if (SettingsRoot->TryGetArrayField(TEXT("enabledMcpjsonServers"), ExistingEnabled) && ExistingEnabled)
        {
            EnabledServers = *ExistingEnabled;
        }
        else if (SettingsRoot->HasField(TEXT("enabledMcpjsonServers")))
        {
            UE_LOG(LogMCPServer, Warning,
                TEXT("Cannot merge %s: enabledMcpjsonServers is not an array"), *SettingsPath);
            return;
        }

        const bool bAlreadyEnabled = EnabledServers.ContainsByPredicate(
            [](const TSharedPtr<FJsonValue>& Value)
            {
                FString Name;
                return Value.IsValid() && Value->TryGetString(Name) && Name == TEXT("Revvy");
            });
        if (!bAlreadyEnabled)
        {
            EnabledServers.Add(MakeShared<FJsonValueString>(TEXT("Revvy")));
            bClaudeModified = true;
        }
        if (bClaudeModified)
        {
            SettingsRoot->SetArrayField(TEXT("enabledMcpjsonServers"), EnabledServers);
            if (SaveJsonObject(SettingsPath, SettingsRoot))
            {
                UE_LOG(LogMCPServer, Log, TEXT("Merged Revvy allow-list into %s"), *SettingsPath);
            }
        }
    }
    else
    {
        UE_LOG(LogMCPServer, Warning, TEXT("Cannot parse Claude settings; left unchanged: %s"), *SettingsPath);
    }

    // .kimi-code/mcp.json — same proxy for the Kimi Code CLI (project-level
    // mcp.json; entries with a "url" are HTTP servers). Only written when missing.
    const FString KimiDir = ProjectDir / TEXT(".kimi-code");
    const FString KimiMcpJsonPath = KimiDir / TEXT("mcp.json");
    if (!PlatformFile.FileExists(*KimiMcpJsonPath))
    {
        PlatformFile.CreateDirectoryTree(*KimiDir);
        // Kimi reads this from the project directory, so external shells share it.
        // Point it at the Full endpoint only when the operator pinned the bearer for
        // the whole user session — otherwise those shells would resolve the variable
        // to nothing and get a 401 instead of read-only access.
        const FString KimiMcpJson = bEditorFullAccessTokenPinned
            ? FString::Printf(
                TEXT("{\n  \"mcpServers\": {\n    \"Revvy\": {\n      \"url\": \"http://127.0.0.1:%d/mcp\",\n")
                TEXT("      \"bearerTokenEnvVar\": \"REVVY_FULL_ACCESS_TOKEN\"\n    }\n  }\n}\n"),
                GetProxyPortFromConfig())
            : FString::Printf(
                TEXT("{\n  \"mcpServers\": {\n    \"Revvy\": {\n      \"url\": \"http://127.0.0.1:%d/mcp/safe\"\n    }\n  }\n}\n"),
                GetProxyPortFromConfig());
        FFileHelper::SaveStringToFile(KimiMcpJson, *KimiMcpJsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogMCPServer, Log, TEXT("Created %s"), *KimiMcpJsonPath);
    }
}
