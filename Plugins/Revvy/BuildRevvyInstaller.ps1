<#
.SYNOPSIS
    Build the Revvy desktop installer and the update feed entry it publishes.

.DESCRIPTION
    Runs BuildRevvyApp.ps1 for the executables, compiles Installer\Revvy.iss with
    Inno Setup, then writes latest.json describing the result.

    The feed entry is generated here rather than by hand because it carries the
    SHA-256 the updater verifies against. A hash typed by a person is a hash that
    is eventually wrong, and a wrong hash means every client refuses the update.

    -FeedDir publishes the installer and latest.json somewhere the app can reach:
    a shared folder, a synced drive, or a directory that is later uploaded. The
    updater takes any https: or file: URL, so where it lands is a deployment
    choice and not a code change.

.PARAMETER SkipAppBuild
    Reuse the executables already in Binaries\RevvyApp. Useful when iterating on
    the installer script itself.

.PARAMETER FeedDir
    Copy the installer here and write latest.json beside it.

.EXAMPLE
    .\BuildRevvyInstaller.ps1
    .\BuildRevvyInstaller.ps1 -FeedDir "\\nas\revvy\releases"
#>
[CmdletBinding()]
param(
    [switch]$SkipAppBuild,
    [string]$FeedDir = "",
    [string]$Notes = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path $PSScriptRoot).Path
$PythonRoot = Join-Path $Root "Content\Python"
$OutputDir = Join-Path $Root "Binaries\Installer"

$Version = & python -c "import sys; sys.path.insert(0, r'$PythonRoot'); import revvy_app_core; print(revvy_app_core.APP_VERSION)"
if ($LASTEXITCODE -ne 0 -or -not $Version) {
    throw "Could not read APP_VERSION from revvy_app_core.py"
}
$Version = $Version.Trim()
Write-Host "Revvy $Version"

if (-not $SkipAppBuild) {
    & (Join-Path $Root "BuildRevvyApp.ps1")
    if ($LASTEXITCODE -ne 0) { throw "App build failed." }
}

$AppExe = Join-Path $Root "Binaries\RevvyApp\Revvy-$Version.exe"
$ProxyExe = Join-Path $Root "Binaries\RevvyApp\RevvyProxy-$Version.exe"
foreach ($required in @($AppExe, $ProxyExe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing build output: $required. Run without -SkipAppBuild."
    }
}

# Inno Setup is not on PATH by default even when installed.
$Iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $Iscc) {
    foreach ($candidate in @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $Iscc = $candidate; break }
    }
}
if (-not $Iscc) {
    throw "Inno Setup 6 not found. Install it: winget install -e --id JRSoftware.InnoSetup"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
& $Iscc "/DMyAppVersion=$Version" (Join-Path $Root "Installer\Revvy.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed." }

$Setup = Join-Path $OutputDir "Revvy-$Version-setup.exe"
if (-not (Test-Path -LiteralPath $Setup -PathType Leaf)) {
    throw "Installer was not produced: $Setup"
}

$Hash = (Get-FileHash -LiteralPath $Setup -Algorithm SHA256).Hash.ToLower()
$Size = (Get-Item -LiteralPath $Setup).Length
$Feed = [ordered]@{
    version = $Version
    url     = [System.IO.Path]::GetFileName($Setup)
    sha256  = $Hash
    size    = $Size
    channel = "stable"
    notes   = $Notes
}
$FeedJson = ($Feed | ConvertTo-Json -Depth 3)
$FeedPath = Join-Path $OutputDir "latest.json"
$FeedJson | Out-File -FilePath $FeedPath -Encoding utf8
Write-Host "Installer : $Setup"
Write-Host "SHA-256   : $Hash"
Write-Host "Feed      : $FeedPath"

if ($FeedDir) {
    New-Item -ItemType Directory -Force -Path $FeedDir | Out-Null
    Copy-Item -LiteralPath $Setup -Destination $FeedDir -Force
    # latest.json goes last: until it names the new version, clients keep seeing
    # the old one, so a half-copied release is never announced.
    Copy-Item -LiteralPath $FeedPath -Destination $FeedDir -Force
    Write-Host "Published : $FeedDir"
}
