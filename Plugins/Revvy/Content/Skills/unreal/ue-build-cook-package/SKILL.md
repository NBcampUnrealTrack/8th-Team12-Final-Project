---
name: ue-build-cook-package
description: "Use this skill when building, cooking, or packaging an Unreal project: Target.cs anatomy, RunUAT BuildCookRun, BuildGraph pipelines, Unreal Insights boot tracing, cook-error triage, packaging settings, GameFeature packaging, EOS encryption, signing, and delivery validation."
metadata:
  version: 1.0.1
---

# UE Build / Cook / Package

You are an expert in the UE build → cook → package pipeline: UBT, UAT, BuildCookRun, BuildGraph, IoStore, pak, and DRM/signing.

## Context Check

Read `.agents/ue-project-context.md` first. Confirm:

- UE 5.8 (`UE_5.8` engine path)
- `.Target.cs` files present (Editor, Game, Client, Server)
- Existing game/client/server targets and any EOS-specific target configuration
- `Plugins/GameFeatures/` discovery (GFPs cook differently)
- `DefaultGame.ini` Packaging section
- CI runner OS (Win64 host vs Linux)

## Information to Gather

1. Target platform (Win64, Linux, Mac, Android, iOS, consoles)?
2. Configuration (DebugGame, Development, Test, Shipping)?
3. Client + Server split or monolithic Game target?
4. EOS encryption + signing required (Shipping)?
5. IoStore (recommended) or legacy Pak?
6. GameFeature plugins included or only DLC-mode?
7. CI pipeline — BuildGraph already configured?

---

## .Target.cs Anatomy

Source: `Source/<ProjectName><TargetType>.Target.cs`

```csharp
// LyraGame.Target.cs
public class LyraGameTarget : TargetRules
{
    public LyraGameTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.Add("LyraGame");
        LyraGameTarget.ApplySharedLyraTargetSettings(this);
    }
}
```

### TargetType values

| Type | Use | Includes Editor? | Cooked content? |
|---|---|---|---|
| `Editor` | Open in editor | Yes | No (loads raw) |
| `Game` | Standalone client + server in one | No | Yes |
| `Client` | Client-only (multiplayer) | No | Yes (client subset) |
| `Server` | Dedicated server | No | Yes (server subset) |
| `Program` | Standalone tools | No | N/A |

### Cooked content distinction

`Client` excludes server-only assets; `Server` excludes client-only (textures
high-mip, UMG widgets). Set via `bIsClientOnly` / `bIsServerOnly` in
`DefaultEngine.ini` per-platform.

### Lyra dual targets

`LyraGame.Target.cs` and `LyraGameEOS.Target.cs` — same code, different
default plugin set. Use EOS target for Epic-distributed builds.

---

## RunUAT BuildCookRun (Standard Recipe)

```bash
RunUAT.bat BuildCookRun
    -project="C:/Path/DuckSoul.uproject"
    -platform=Win64
    -clientconfig=Development
    -serverconfig=Development
    -cook -stage -pak -iostore -compressed
    -archive -archivedirectory="C:/Path/Out"
    -build -nop4 -utf8output
```

### Flag breakdown

| Flag | Effect |
|---|---|
| `-platform=Win64` | Build target platform |
| `-clientconfig=Development` | Optimization level (Debug, DebugGame, Development, Test, Shipping) |
| `-cook` | Cook content to platform-specific format |
| `-stage` | Lay out final directory structure |
| `-pak` | Package content into .pak files |
| `-iostore` | Use IoStore container (preferred over pak alone) |
| `-compressed` | Compress pak/iostore |
| `-archive` | Copy staged build to `-archivedirectory` |
| `-build` | Compile target binaries |
| `-nop4` | Skip Perforce ops |
| `-utf8output` | UTF-8 log encoding (matters in CI) |
| `-skipcook` | Reuse last cook (iterate faster) |
| `-skipstage` | Reuse last stage |
| `-prereqs` | Include redistributable prerequisites |
| `-distribution` | Shipping config marker — disables some dev features |

### IoStore vs Pak

- **Pak**: monolithic archive of cooked files (legacy).
- **IoStore**: chunk-based container with on-disk DDC-style lookup. Faster
  load, better mem profile. Preferred for 5.x.

Both can co-exist; `-iostore` produces `.utoc/.ucas` files, `-pak` produces
`.pak`. Modern projects use both flags; iostore is primary.

---

## DefaultGame.ini Packaging Section

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
BuildConfiguration=PPBC_Development
BuildTarget=DuckSoul
UsePakFile=True
bUseIoStore=True
bShareMaterialShaderCode=True

+MapsToCook=(FilePath="/Game/Maps/MainMenu")
+MapsToCook=(FilePath="/Game/Maps/ShopHub")
+MapsToCook=(FilePath="/Game/Maps/CombatArena")

+DirectoriesToAlwaysCook=(Path="/Game/Experiences")
+DirectoriesToAlwaysCook=(Path="/Game/UI/Styles")
+DirectoriesToAlwaysCook=(Path="/Game/Characters/DuckCharacter")

+DirectoriesToNeverCook=(Path="/Game/__Drafts__")
+DirectoriesToNeverCook=(Path="/Game/_TestOnly")

+IniSectionBlacklist=BlueprintNativization
+IniSectionBlacklist=GameAnalytics

ApplocalPrerequisitesDirectory=(Path="")
```

- `MapsToCook` — explicit maps (besides default map) to include
- `DirectoriesToAlwaysCook` — assets not referenced by maps but needed at runtime (Experience DataAssets, UI styles, GFP-referenced softrefs)
- `DirectoriesToNeverCook` — exclude WIP/test content from shipping builds

---

## GameFeature Plugin Packaging

GFPs require explicit handling:

```json
// In Plugins/GameFeatures/DuckSoulShop/DuckSoulShop.uplugin
{
    "Type": "GameFeature",
    "BuiltInInitialFeatureState": "Active",
    "Modules": [{
        "Name": "DuckSoulShopRuntime",
        "Type": "Runtime",
        "LoadingPhase": "Default"
    }],
    "EnabledByDefault": true,
    "CanContainContent": true
}
```

- `CanContainContent: true` — without this, content under `Plugins/.../Content/` is **not cooked**
- `EnabledByDefault: true` — included in packaged build
- `BuiltInInitialFeatureState`: `Active` / `Registered` / `Installed` — see ue-lyra-experience for runtime implications

Discovery: UAT scans `Plugins/GameFeatures/` automatically. Plugins outside
this folder need explicit `-AdditionalPluginContent` flags.

---

## BuildGraph XML Pipelines

`Engine/Build/Graph/Examples/BuildAndTestProject.xml` is the reference. Use
to compose multi-step pipelines.

```xml
<?xml version="1.0" encoding="utf-8" ?>
<BuildGraph xmlns="http://www.epicgames.com/BuildGraph"
            xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
            xsi:schemaLocation="http://www.epicgames.com/BuildGraph
                                ../Schema.xsd">
  <Option Name="Platforms" DefaultValue="Win64" />
  <Option Name="Configuration" DefaultValue="Development" />
  <Property Name="ProjectPath" Value="$(RootDir)/DuckSoul.uproject" />

  <Agent Name="Build DuckSoul" Type="Win64">
    <Node Name="Compile Editor">
      <Compile Target="DuckSoulEditor" Platform="Win64"
               Configuration="Development" Project="$(ProjectPath)" />
    </Node>

    <Node Name="Compile Game" Requires="Compile Editor">
      <Compile Target="DuckSoul" Platform="$(Platforms)"
               Configuration="$(Configuration)" Project="$(ProjectPath)" />
    </Node>

    <Node Name="Cook" Requires="Compile Editor">
      <Cook Project="$(ProjectPath)" Platform="$(Platforms)"
            Versioned="true" Compressed="true" />
    </Node>

    <Node Name="Stage" Requires="Cook;Compile Game">
      <Stage Project="$(ProjectPath)" Platform="$(Platforms)"
             Configuration="$(Configuration)"
             ArchiveDirectory="$(RootDir)/Out" />
    </Node>
  </Agent>
</BuildGraph>
```

Run:

```bash
RunUAT.bat BuildGraph -Script="Build/DuckSoul.xml" -Target="Stage" -set:Platforms=Win64
```

CI workflows wrap this with Jenkins / GitHub Actions / Buildkite.

---

## UnrealInsights — Boot Trace

```bash
DuckSoul.exe -trace=default,cpu,memory,loadtime,assetloadtime -tracehost=127.0.0.1
```

Then `UnrealInsights.exe` connects and shows:
- LoadTime track — asset load sequence (target: < 5s to first interactive frame on dev hardware)
- AssetLoading track — per-package load times (find heavy assets)
- CPU track — startup hot paths

Use to chase 5-30s boot-time regressions after adding GFPs or large soft refs.

---

## Cook Error Triage

```
LogCook: Error: Missing import: ...
```

| Error pattern | Cause | Fix |
|---|---|---|
| "Missing import" | Soft ref to unstaged asset | Add containing folder to `DirectoriesToAlwaysCook` or fix the reference |
| "Redirector found" | Renamed asset still referenced by old path | Run `FixUpRedirectors` commandlet, re-cook |
| "Asset not in cook list" | GFP plugin missing `CanContainContent: true` | Edit .uplugin |
| "Cooker timeout" | Single asset takes too long (huge texture import) | Investigate; split or downsize |
| "Unsupported platform feature" | Platform whitelist mismatch on plugin | Check `WhitelistPlatforms` in .uplugin |
| "Out of memory during cook" | Cooker process exceeds RAM | Set `-CookProcessCount=N` smaller; use `-cookcultures=en` only |

Run `FixUpRedirectors` after every asset rename:

```bash
UnrealEditor-Cmd.exe DuckSoul.uproject -run=ResavePackages -fixupredirects -nosourcecontrol -autocheckout
```

---

## EOS Shipping Concerns

Shipping builds with EOS need:

1. **EncryptionKey** set in `DefaultEngine.ini → [/Script/OnlineSubsystemEOS.EOSSettings]`
2. **Signing certificate** for OS-level distribution (notarization on Mac, code-sign on Win for Smart App Control)
3. **EOSSDK.dll** redistributed in `Engine/Binaries/ThirdParty/EOSSDK/Win64/`
4. **Overlay support** — `EOSOverlay` DLL alongside game exe

Without encryption key, EOS Auth fails silently at first login → user sees
black screen.

---

## DuckSoul Recipe — Win64 Development Client (Prototype)

For the 2026-04-30 milestone:

```bash
cd "C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles"
RunUAT.bat BuildCookRun
    -project="C:/Users/Ndus_072/Desktop/DuckSoul/DuckSoul.uproject"
    -platform=Win64
    -clientconfig=Development
    -cook -stage -pak -iostore -compressed
    -archive -archivedirectory="C:/Users/Ndus_072/Desktop/DuckSoul/Out"
    -build -nop4 -utf8output -prereqs
```

Expected output: `Out/Windows/DuckSoul.exe` + `Out/Windows/DuckSoul/Content/Paks/`.

Smoke-check:
1. Run exe; first frame within 10s.
2. Main menu appears.
3. Press start → ShopHub Experience loads.
4. Walk to combat door → CombatArena Experience loads.
5. EOS login (Developer auth in dev builds; AccountPortal in shipping).

If milestone slips on packaging, rotate to `-skipbuild -skipcook` for iterative
stage-only runs.

---

## Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| Cooked build crashes on first asset load | Asset not staged (missing cook directive) | Add path to `DirectoriesToAlwaysCook` |
| GFP plugin assets missing in build | `CanContainContent: false` in .uplugin | Set true |
| Shipping build hangs on EOS init | No `EncryptionKey` | Set 64-char hex key |
| Win64 build runs but Server target missing | Server compile step not in BuildGraph | Add `<Compile Target="DuckSoulServer">` node |
| IoStore + Pak duplication doubled package size | Both `-iostore -pak` produce content; keep only the one shipped to disk | Use `-iostore` only for new projects |
| Boot time regression after adding Experience | Soft refs from new Experience loaded eagerly | Move to bundle-state lazy loading; verify via Insights LoadTime track |
| Cook stuck on shader compile | Shader cache cold + many materials | Use shared shader code (`bShareMaterialShaderCode=true`); pre-build DDC |
| "Could not find target" | Project not regenerated after Build.cs change | Run `GenerateProjectFiles.bat` first |
| Cook works locally, fails in CI | Local DDC vs cold-CI difference | Warm shared DDC; CI step with `-ddc=DerivedDataBackendGraph` |
| Pak signing breaks in Test config | Signing key only set for Shipping | Set `bSignPak=True` for both Test and Shipping or neither |
| Plugin disabled in client but referenced by Experience | Per-target plugin gate (`TargetAllowList`) | Verify `.uplugin` Modules.TargetAllowList |

---

## Build Configurations Cheat-Sheet

| Config | Optimizations | Asserts | Editor tools? | Use case |
|---|---|---|---|---|
| `Debug` | None | All | Yes | Engine debugging |
| `DebugGame` | Engine optimized, game debug | All | Yes | Game-code debugging |
| `Development` | Yes | `check()` only | Yes | Daily dev / playtests |
| `Test` | Yes | None | Some (logging) | Pre-shipping QA |
| `Shipping` | Yes | None | No (stripped) | Production |

DuckSoul prototype milestone → `Development`. After milestone, validate
`Test` config for QA, then `Shipping` for store delivery.

---

## Build.cs (target-level deps live in Target.cs, not Module.Build.cs)

`Target.cs` controls global toggles:

```csharp
// In LyraGameTarget.ApplySharedLyraTargetSettings (or DuckSoul equivalent)
Target.bWithServerCode = true;
Target.bCompileAgainstEngine = true;
Target.bUsesSlate = true;
Target.bUseLoggingInShipping = true;  // diagnostics in shipping
```

---

## Related Skills

- `ue-module-build-system` — Build.cs anatomy, module dependencies, MinimalAPI.
  Load BEFORE this skill if cook errors trace to module configuration.
- `ue-game-features` — GFP packaging rules + .uplugin `CanContainContent`.
- `ue-online-subsystem-eos` — EOS shipping requirements (encryption key,
  redist DLL).
- `ue-project-context` — current project's plugin set, target list,
  packaging config.
- `ue-automation-functional-tests` — tests can run as part of BuildCookRun
  for build-gating.
