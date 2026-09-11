---
name: ue-online-subsystem-eos
description: "Use this skill when integrating Online Subsystem and EOS: subsystem layering, sessions, identity login flows, Lyra game sessions, voice and RTC, achievements and stats, artifact configuration, SDK encryption and redistribution, packaging, and OSS diagnostics."
revvy_classes:
  - OnlineSessionService
metadata:
  version: 1.0.1
---

# UE Online Subsystem + EOS

Use Unreal's Online Subsystem layering and the Epic Online Services backend. Inspect the
project's target and config rather than assuming Lyra or an EOS-specific target exists.

> **UE 5.8 note:** The separate `OnlineSubsystemEOSPlus` plugin was removed in UE 5.5+; its Steam/PSN/Xbox aggregation and the `EOSPLUS_SUBSYSTEM` name no longer exist. Set `DefaultPlatformService=EOS` and use `OnlineSubsystemEOS` directly (its login flow no longer relies on EOSPlus). The EOSPlus-specific config/interfaces below are retained only as historical context for projects still on ≤5.4.

## Context Check

Read `.agents/ue-project-context.md` first. Confirm:

- `OnlineSubsystemEOS`, `EOSShared`, `EOSVoiceChat` plugins enabled (`OnlineSubsystemEOSPlus` no longer exists in 5.8)
- `DefaultEngine.ini → [OnlineSubsystem] DefaultPlatformService=` value
- Product ID / Sandbox ID / Deployment ID present in `DefaultEngine.ini → [/Script/OnlineSubsystemEOS.EOSSettings]`
- Encryption key set (required for sealed builds)
- Existing `ULyraGameSession` and any session-creation flow
- Whether running PIE-with-EOS-Logged-In (requires DevAuthTool or dev login)

## Information to Gather

1. Which feature? — sessions, parties, friends, voice, achievements, stats, leaderboards
2. Server architecture — dedicated server, listen server, or P2P?
3. Login method — AccountPortal (interactive), PersistentAuth (silent refresh), Developer (dev tool), ExchangeCode (launcher), DeviceID (mobile/guest)?
4. Is platform-account aggregation required, and what UE 5.8-supported integration owns it?
5. PIE testing or only packaged builds?

---

## Subsystem Layering

```
Game Code
   ↓
IOnlineSubsystem (legacy "OSSv1" interface — IOnlineSession, IOnlineFriends, ...)
   ↓
OnlineSubsystemEOSPlus  ──► aggregates ──► OnlineSubsystemEOS + Platform OSS (Steam/PSN/...)
                                                ↓
                                            EOS SDK (C SDK)
                                                ↓
                                          EOS Backend (Epic servers)

OR

UE 5.4+: Online Services (OSSv2) — IOnlineServicesEngineUtils + UE::Online::*
         New API; coexists with OSSv1. Lyra primarily uses OSSv1 still.
```

### OSSv1 vs OSSv2

- **OSSv1** (`IOnlineSubsystem`): mature, Lyra/DuckSoul use this. Interfaces:
  `IOnlineSession`, `IOnlineIdentity`, `IOnlineFriends`, `IOnlineVoice`,
  `IOnlineAchievements`, `IOnlineStats`, `IOnlineLeaderboards`, `IOnlinePurchase`.
- **OSSv2** (`UE::Online::IOnlineServices`): UE5.x rewrite, parallel API.
  Cleaner async, but smaller backend support. Migrate per-system, not all-at-once.

This skill covers OSSv1 + EOS. See `Online/OnlineSubsystem/Public/OnlineSubsystem.h`.

---

## DefaultEngine.ini Configuration

```ini
[/Script/Engine.GameEngine]
; UNetDriverEOSBase was deprecated in 5.6 — use UNetDriverEOS (SocketSubsystemEOS module).
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="SocketSubsystemEOS.NetDriverEOS",DriverClassNameFallback="OnlineSubsystemUtils.IpNetDriver")

[OnlineSubsystem]
DefaultPlatformService=EOS

[OnlineSubsystemEOS]
bEnabled=true

[/Script/OnlineSubsystemEOS.EOSSettings]
DefaultArtifactName=DuckSoul
+Artifacts=(ArtifactName="DuckSoul",
    ClientId="xxxxxxxxxxxxxxxxxxxxxxxx",
    ClientSecret="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    ProductId="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    SandboxId="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    DeploymentId="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    ClientEncryptionKey="0000000000000000000000000000000000000000000000000000000000000000")
TickBudgetInMilliseconds=1

; REMOVED IN 5.8 — the EOSPlus module/section below no longer exists. Kept for
; reference only; on ≤5.4 it toggled Epic Account Services + cross-platform mirroring.
; [/Script/OnlineSubsystemEOSPlus.OnlineSubsystemEOSPlusModule]
; bUseEAS=true     ; Epic Account Services (cross-play)
; bUseEOSConnect=true
; bMirrorAchievementsToEOS=true
; bMirrorStatsToEOS=true
; bMirrorPresenceToEAS=true
```

- `DefaultPlatformService=EOS` makes `IOnlineSubsystem::Get()` return the EOS
  implementation. Call `IOnlineSubsystem::Get(EOS_SUBSYSTEM)` for it explicitly.
  (The `EOSPLUS_SUBSYSTEM` aggregator name was removed with the EOSPlus plugin in 5.5+.)
- The artifact encryption key is mandatory for sealed/encrypted EOS data — 64 hex
  chars. The ini config key is `ClientEncryptionKey` (renamed from `EncryptionKey`,
  which is stripped from packaged builds via `IniKeyDenylist` in BaseGame.ini).
  In source control: keep separate per-environment ini, not committed.

---

## IOnlineSession Flow

Source: `Online/OnlineSubsystem/Public/Interfaces/OnlineSessionInterface.h`

### Create

```cpp
IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
IOnlineSessionPtr Sessions = OSS->GetSessionInterface();

FOnlineSessionSettings Settings;
Settings.NumPublicConnections = 4;
Settings.bShouldAdvertise     = true;
Settings.bUsesPresence        = true;
Settings.bAllowJoinViaPresence = true;
Settings.bUseLobbiesIfAvailable = true;  // EOS Lobbies (P2P)
Settings.Set(SEARCH_KEYWORDS, FString("DuckSoulPrototype"),
             EOnlineDataAdvertisementType::ViaOnlineService);

CreateCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
    FOnCreateSessionCompleteDelegate::CreateUObject(this, &UMyComponent::OnCreateComplete));

Sessions->CreateSession(0, NAME_GameSession, Settings);
```

### Find

```cpp
TSharedRef<FOnlineSessionSearch> Search = MakeShared<FOnlineSessionSearch>();
Search->MaxSearchResults = 20;
Search->bIsLanQuery = false;
Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);

Sessions->AddOnFindSessionsCompleteDelegate_Handle(
    FOnFindSessionsCompleteDelegate::CreateUObject(this, &UMyComponent::OnFindComplete));

Sessions->FindSessions(0, Search);
```

### Join

```cpp
Sessions->AddOnJoinSessionCompleteDelegate_Handle(
    FOnJoinSessionCompleteDelegate::CreateUObject(this, &UMyComponent::OnJoinComplete));

Sessions->JoinSession(0, NAME_GameSession, Search->SearchResults[0]);

void UMyComponent::OnJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    if (Result != EOnJoinSessionCompleteResult::Success) return;
    FString ConnectString;
    Sessions->GetResolvedConnectString(SessionName, ConnectString);
    GetPlayerController()->ClientTravel(ConnectString, TRAVEL_Absolute);
}
```

### Destroy

```cpp
Sessions->DestroySession(NAME_GameSession);
```

**Delegate handle hygiene**: store every `Add*CompleteDelegate_Handle` return
value and call `Clear*CompleteDelegate_Handle` after the callback fires.
Forgetting causes duplicate callbacks on retry.

---

## IOnlineIdentity — Login

```cpp
IOnlineIdentityPtr Identity = OSS->GetIdentityInterface();

Identity->AddOnLoginCompleteDelegate_Handle(0,
    FOnLoginCompleteDelegate::CreateUObject(this, &UMyComponent::OnLoginComplete));

FOnlineAccountCredentials Creds;
Creds.Type  = TEXT("AccountPortal");  // see table below
Creds.Id    = TEXT("");
Creds.Token = TEXT("");

Identity->Login(0, Creds);
```

| Type | Id | Token | Use case |
|---|---|---|---|
| `AccountPortal` | "" | "" | Interactive browser login |
| `PersistentAuth` | "" | "" | Silent re-login from saved refresh token |
| `Developer` | DevAuthTool URL | Credential name | PIE / editor testing |
| `ExchangeCode` | "" | exchange code | Epic Launcher start |
| `DeviceID` | display name | "" | Guest / mobile no-account |
| `AccountID` | UserId hex | Token | Auto-relogin |

### PIE login — Developer auth flow

1. Install `DevAuthTool.exe` (in EOSSDK `Tools/`).
2. Run: `DevAuthTool.exe --url=localhost:6300 --serverName=DuckSoul`
3. Log in via browser inside the tool.
4. In `DefaultEngine.ini` or command line: `-AUTH_TYPE=Developer -AUTH_LOGIN=localhost:6300 -AUTH_PASSWORD=DuckSoulCred1`
5. PIE launches authenticated.

---

## ULyraGameSession Integration

Lyra has `ALyraGameSession` (overrides `AGameSession`) — game-mode-level hook.

```cpp
// ALyraGameMode::PreLogin / InitGame call into ALyraGameSession.
// Override in DuckSoul if you need custom session settings per Experience.

ALyraGameSession::ALyraGameSession()
{
    MaxPlayers = 4;
    bWantsToBeDedicated = false;
}
```

The match-assignment flow:
1. GameMode picks Experience.
2. Experience triggers session create.
3. Session created → server is reachable.
4. Clients use `IOnlineSession::JoinSession` → `ClientTravel`.

---

## Voice Chat (IVoiceChat + EOS RTC)

```cpp
IVoiceChat* VoiceChat = IVoiceChat::Get();
VoiceChat->Initialize();
VoiceChat->Login(0, "PlayerName", "PlayerToken",
    FOnVoiceChatLoginCompleteDelegate::CreateUObject(this, &UMyVC::OnVoiceLogin));

// EOS RTC channel = Session ID by convention
VoiceChat->JoinChannel(ChannelName, ChannelCredentials,
    EVoiceChatChannelType::NonPositional, ...);
```

EOS RTC handles voice transport — separate from `IOnlineVoice` (legacy). For
Lyra projects, use `EOSVoiceChat` plugin's `IVoiceChat` implementation.

---

## Achievements + Stats

```cpp
IOnlineAchievementsPtr Achievements = OSS->GetAchievementsInterface();
TArray<FOnlineAchievement> List;
Achievements->QueryAchievements(*UserId,
    FOnQueryAchievementsCompleteDelegate::CreateLambda(
        [Achievements](const FUniqueNetId& U, bool bSuccess) { /* ... */ }));

// Stats — write
IOnlineStatsPtr Stats = OSS->GetStatsInterface();
TArray<FOnlineStatsUserUpdatedStats> Updates;
FOnlineStatsUserUpdatedStats Update(UserId.AsShared());
Update.Stats.Add("ENEMIES_KILLED",
    FOnlineStatUpdate(1, FOnlineStatUpdate::EOnlineStatModificationType::Sum));
Updates.Add(Update);
Stats->UpdateStats(LocalUserId, Updates, FOnlineStatsUpdateStatsComplete());
```

EOS Achievements/Stats configured in Developer Portal first — IDs must match.

---

## Packaging Considerations

### EOSSDK redist

`OnlineSubsystemEOS` brings `EOSSDK.dll` (Win64) / `libEOSSDK.so` (Linux) /
`EOSSDK.framework` (Mac). Verify presence in
`Plugins/Online/OnlineSubsystemEOS/Source/ThirdParty/EOSSDK/Bin/`.

### Encryption key

Sealed builds without `EncryptionKey` fail EOS Auth → no login. Set per-env
ini.

### Overlay

EOS Overlay (in-game UI for friends/notifications) requires hook DLLs. Disabled
in PIE by default; enable via `-EOSOverlay=true`. Often broken under PIE —
test in packaged build.

---

## Debugging

### Console commands

```
OSS.DumpSessionState
OSS.DumpVoiceState
OSS.DumpIdentityState
EOSSDK.LogLevel All Verbose
```

### Log categories

`LogOnline`, `LogOnlineSession`, `LogEOSSDK`, `LogEOSAuth`.

In `DefaultEngine.ini`:

```ini
[Core.Log]
LogOnline=Verbose
LogOnlineSession=Verbose
LogEOS=Verbose
LogEOSSDK=Verbose
```

### Network insights

`NetTrace` captures the underlying NetDriver layer — useful when EOS sockets
are involved.

---

## Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| Login fails: "Token expired" | PersistentAuth refresh token age | Fall back to AccountPortal interactive |
| `JoinSession` returns `Success` but client gets disconnected | NAT type / port mismatch | Use `bUseLobbiesIfAvailable=true` (EOS lobbies handle NAT via P2P relay) |
| EOS Overlay broken in PIE | Hook DLL not loaded in editor | Test in packaged Development build |
| Delegate fires twice | Forgot `Clear*CompleteDelegate_Handle` after success | Pair Add with Clear in every callback |
| `IOnlineSubsystem::Get()` returns nullptr | Plugins disabled or `DefaultPlatformService` empty | Verify .uproject + DefaultEngine.ini |
| Achievements unlock locally but not on EOS | Achievement ID mismatch with Dev Portal | Re-check the unlock ID; case sensitive |
| Stats writes silently dropped | Stat name not defined in Dev Portal | Define stats in EOS Dashboard first |
| Sealed build crashes on first login | Missing `EncryptionKey` | Add 64-char hex to EOSSettings |
| Two players join same session and see "host disconnect" | Listen-server NAT punch failed | Move to dedicated server or EOS Lobby relay |

---

## Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new[] {
    "Core", "CoreUObject", "Engine",
    "OnlineSubsystem", "OnlineSubsystemUtils"
});
PrivateDependencyModuleNames.AddRange(new[] {
    "OnlineSubsystemEOS",
    "EOSShared",
    "EOSVoiceChat"
});
// If using ULyraGameSession extension:
PrivateDependencyModuleNames.Add("LyraGame");
```

`.uproject` plugins:

```json
{ "Name": "OnlineSubsystemEOS", "Enabled": true },
{ "Name": "SocketSubsystemEOS", "Enabled": true },
{ "Name": "EOSShared", "Enabled": true },
{ "Name": "EOSVoiceChat", "Enabled": true }
```

---

## Related Skills

- `ue-networking-replication` — NetDriver, RPCs, connection lifecycle. EOS
  sits below the replication layer.
- `ue-iris-replication` — modern replication path; EOS is the transport
  underneath either system.
- `ue-gameplay-framework` — `AGameSession`, `AGameMode`, `APlayerController`
  travel hooks.
- `ue-lyra-experience` — Lyra session lifecycle is Experience-aware;
  session-create happens around Experience load.
- `ue-build-cook-package` — packaging EOS SDK redist + encryption key per
  shipping environment.
