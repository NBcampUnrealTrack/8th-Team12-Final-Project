# Building Revvy for Unreal Engine 5.8

Revvy is a C++ Editor plugin. A source-only installation must be compiled for
the exact Unreal Engine and project toolchain in use.

## Requirements

- Unreal Engine 5.8
- Visual Studio 2022 with **Desktop development with C++** and
  **Game development with C++**
- A Windows SDK supported by the installed UE 5.8 build

## Project installation

1. Place the plugin at `YourProject/Plugins/Revvy`.
2. Close Unreal Editor.
3. Regenerate the project's IDE files.
4. Build the project's `Editor` target in the `Development Editor`
   configuration.
5. Reopen the project and enable Revvy if prompted.

If the project has no C++ target yet, add an empty C++ class once so Unreal can
generate a build target.

## Standalone plugin validation

From a source checkout, Epic's Automation Tool can build an isolated plugin
package:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" `
  BuildPlugin `
  "-Plugin=C:\Path\To\Revvy\Revvy.uplugin" `
  "-Package=C:\Temp\RevvyPackage" `
  "-TargetPlatforms=Win64" `
  -UTF8Output
```

Repository maintainers can run the checked-in validation wrapper:

```powershell
.\Scripts\CI\Invoke-UE58Validation.ps1 -SkipAutomation
```

Omit `-SkipAutomation` to build an isolated host project and run the `Revvy`
Automation test filter. The wrapper requires an empty output directory when one
is supplied.

## Troubleshooting

### Missing or incompatible modules

Confirm the descriptor targets UE 5.8, close the Editor, and rebuild the
project's Editor target. Do not copy binaries produced by a different engine
minor version.

### Toolchain not found

Open Visual Studio Installer and add the C++ workloads and Windows SDK, then
rerun Unreal's project-file generation.

### Optional native AI Toolset is unavailable

Epic's UE 5.8 `ToolsetRegistry` and `ModelContextProtocol` plugins are optional
and marked `NoRedist`. Revvy does not package or hard-depend on them. Revvy's
authenticated MCP server remains available when those engine plugins are
disabled.

## Platform status

Win64 is covered by Revvy's checked-in BuildPlugin and Automation gate. The
descriptor retains Mac and Linux module eligibility, but those targets require
their own native toolchain and platform CI before a marketplace release can
claim equivalent support.
