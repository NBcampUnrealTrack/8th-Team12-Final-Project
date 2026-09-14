param(
    [switch]$DesktopOnly,
    [string]$ProjectRoot
)

$ErrorActionPreference = "Stop"
$PluginRoot = (Resolve-Path $PSScriptRoot).Path
if ($ProjectRoot) {
    $ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
} else {
    $ProjectRoot = (Resolve-Path (Join-Path $PluginRoot "..\..")).Path
}
$PythonRoot = Join-Path $PluginRoot "Content\Python"
$Version = & python -c "import sys; sys.path.insert(0, r'$PythonRoot'); import revvy_app_core; print(revvy_app_core.APP_VERSION)"
if ($LASTEXITCODE -ne 0 -or -not $Version) {
    throw "Could not read APP_VERSION from revvy_app_core.py"
}
$Executable = Join-Path $PluginRoot "Binaries\RevvyApp\Revvy-$($Version.Trim()).exe"

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Revvy app executable not found. Run BuildRevvyApp.ps1 first: $Executable"
}

$ShortcutLocations = @(
    (Join-Path ([Environment]::GetFolderPath("Desktop")) "Revvy.lnk")
)

if (-not $DesktopOnly) {
    $Programs = [Environment]::GetFolderPath("Programs")
    $ShortcutLocations += Join-Path $Programs "Revvy.lnk"
}

$Shell = New-Object -ComObject WScript.Shell
foreach ($ShortcutPath in $ShortcutLocations) {
    $Parent = Split-Path -Parent $ShortcutPath
    New-Item -ItemType Directory -Force -Path $Parent | Out-Null

    $Shortcut = $Shell.CreateShortcut($ShortcutPath)
    $Shortcut.TargetPath = $Executable
    $Shortcut.WorkingDirectory = $ProjectRoot
    $Shortcut.IconLocation = "$Executable,0"
    $Shortcut.Description = "Revvy Unreal Copilot"
    $Shortcut.Save()

    Write-Host "Created: $ShortcutPath"
}
