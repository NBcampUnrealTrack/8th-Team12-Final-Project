---
name: dev-environment-setup
description: One-click Unreal C++ dev environment setup and repair for artists —
  diagnose Visual Studio / MSVC toolchain, silently install VS Build Tools,
  regenerate Visual Studio project files, rebuild the editor target. Use when the
  user says "빌드가 안 돼요", "프로젝트가 안 열려요", "missing modules", "환경 세팅",
  "Visual Studio 없음", or a fresh artist machine needs setup.
  NOT for packaging/cooking (use ue-build-cook-package), NOT for Build.cs module
  wiring (use ue-module-build-system).
compatibility: Windows only. Requires Revvy plugin; VS install path needs one UAC approval.
revvy_classes:
  - DevEnvironmentService
keywords:
  - environment setup
  - visual studio
  - build tools
  - generate project files
  - toolchain
  - msvc
  - one-click
  - 환경 세팅
related_skills:
  - ue-build-cook-package
  - ue-module-build-system
  - revvy
---

# Dev Environment Setup — one-click artist onboarding

Three entry points, same engine-verified logic:

| Entry | When | Can install VS? |
|---|---|---|
| Revvy desktop app (`빌드 + 실행` gate) | Opening/launching a project from the Revvy IDE | YES (one consent dialog + one UAC prompt, then install → build → launch run automatically) |
| `SetupUnrealDev.bat` (plugin root, double-click) | Editor won't open / fresh machine | YES (one UAC prompt) |
| `unreal.DevEnvironmentService` (in-editor) | Editor is running, diagnose/repair | NO (launches the .bat for installs) |

Truth source for requirements = the engine's own `Engine/Config/Windows/Windows_SDK.json`
(min MSVC 14.38.33130, banned compiler ranges, min Windows SDK 10.0.19041.0 for UE 5.8) —
all entry points read it live, so checks survive engine upgrades.

The desktop app gate runs it twice: once shortly after the app window opens and again on
every `빌드 + 실행`. `_build_environment_problem` (`revvy_app.pyw`) calls
`revvy_app_core.check_unreal_build_environment(engine_root)`, which validates every
installed MSVC toolset against the min/banned ranges and checks the Windows SDK — so a
machine with only a banned MSVC (e.g. 14.44.35207 on UE 5.8) gets the install offer with
the reason spelled out, instead of failing the UBT build and needing chat diagnosis.

## Preflight — ALWAYS first

```python
import unreal, json
env = json.loads(unreal.DevEnvironmentService.check_environment())
# {engine:{root,version}, project:{path,name,has_source,project_files_exist},
#  requirements:{min_msvc,banned_msvc,min_windows_sdk},
#  toolchain:{ok,installations,valid_versions,rejected},
#  windows_sdk:{ok,versions}, bootstrap_script}
```

Decision table:

| State | Action |
|---|---|
| `toolchain.ok` + `windows_sdk.ok` + `project_files_exist` | Nothing to do — say so |
| `toolchain.ok` but `project_files_exist=false` | `generate_project_files()` then poll |
| `toolchain.ok=false` or `windows_sdk.ok=false` | ASK THE USER in chat first, then `run_setup_bootstrap(False)` — an in-editor Yes/No consent dialog also appears (install = UAC + 10-30 min). `user_declined` → respect it, don't retry |
| `rejected[].reason == "banned_compiler_bug"` | MSVC version has known codegen bugs (engine bans it). Bootstrap installs a valid one |
| `project.has_source=false` | Content-only project — gen skipped; builds use stock `UnrealEditor` target (compiles plugin modules) |

## Long operations — detached, poll status

`generate_project_files()` / `build_editor_target(configuration)` return immediately
(`{started:true, log_file}`); UBT runs detached with `-waitmutex` (queues instead of
ConflictingInstance). Poll:

```python
st = json.loads(unreal.DevEnvironmentService.get_setup_status(40))
# {task, running, exit_code, log_file, log_tail, log_readable}
# exit_code == -1 while running; log_tail streams live (shared-read open)
# log_readable=false + exit_code!=0 → child died before writing the log
```

- Report `log_tail` progress to the user; done when `running=false`. `exit_code==0` = success.
- Only ONE detached task at a time — a second call errors until the first finishes.
- `build_editor_target` while the editor is open can hit locked-DLL failures — for a
  full rebuild prefer `run_setup_bootstrap(False)` and tell the user to close the editor.

## The bootstrap (what SetupUnrealDev.bat actually does)

1. Walks up to find the `.uproject`; resolves the engine (EngineAssociation → registry → scan).
2. Reads `Windows_SDK.json` requirements.
3. `vswhere -products *` → validates every MSVC toolset against min + banned ranges.
4. Missing → downloads `aka.ms/vs/stable/vs_buildtools.exe`, runs
   `--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools
   --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64
   --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended`
   elevated (winget fallback). Build Tools SKU has NO "Game development with C++"
   workload — VCTools + components IS the correct headless equivalent.
5. `Build.bat -projectfiles -project=... -game` (launcher engines have no
   GenerateProjectFiles.bat — UBT mode is the canonical path).
6. `Build.bat <Target>Editor Win64 Development -project=...`.
7. No .NET install ever needed — UE bundles dotnet (`GetDotnetPath.bat`).

Flags: `-CheckOnly` `-SkipInstall` `-SkipBuild` `-NoPrompt` `-ProjectPath` `-EnginePath`.
Logs: `%LOCALAPPDATA%\Revvy\SetupLogs\` (bootstrap), `Saved/Revvy/SetupLogs/` (in-editor).

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| Installer exit 740 | UAC declined | Re-run, approve the admin prompt |
| Installer exit 3010 | Reboot pending | Toolchain usually usable now; reboot when convenient |
| "is still running — poll get_setup_status()" | Previous detached task active | Wait for `running=false` |
| ConflictingInstance in log | Another UBT instance | `-waitmutex` already queues; check for stuck UBT/editor processes |
| Build fails, path >260 chars | Long project path | Move project to a shorter path (e.g. `C:\Proj\`) |
| MSVC found but rejected | Banned range (engine `BannedVisualCppVersions`) | Bootstrap installs current valid toolset |
| Content-only project, "missing modules" on open | Plugin needs compile | `build_editor_target()` → builds `UnrealEditor` target with `-project` |
