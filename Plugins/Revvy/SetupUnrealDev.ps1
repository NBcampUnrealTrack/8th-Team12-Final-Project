<#
.SYNOPSIS
    Revvy one-click Unreal C++ development environment setup.

.DESCRIPTION
    Designed for artists: double-click SetupUnrealDev.bat and everything is handled.
      1. Locates the .uproject (walks up from the plugin folder).
      2. Resolves the Unreal Engine install (EngineAssociation -> registry -> scan).
      3. Reads toolchain requirements from the engine's own Windows_SDK.json
         (minimum / banned MSVC versions, Windows SDK range) - future-proof.
      4. Detects Visual Studio / MSVC toolchain via vswhere and validates versions.
      5. Detects the Windows SDK from the registry.
      6. If anything is missing, silently installs VS Build Tools (one UAC prompt).
      7. Generates Visual Studio project files (installed-engine UBT path).
      8. Builds the editor target so the project opens immediately.

    No system .NET install is needed: UE ships its own dotnet under
    Engine\Binaries\ThirdParty\DotNet and Build.bat uses it via GetDotnetPath.bat.

.PARAMETER ProjectPath
    Explicit path to a .uproject. Default: walk up from this script's folder.

.PARAMETER EnginePath
    Explicit engine root (folder containing Engine\). Default: auto-resolve.

.PARAMETER CheckOnly
    Detect and report only. No install, no generate, no build.

.PARAMETER SkipInstall
    Never install Visual Studio components (report what is missing instead).

.PARAMETER SkipBuild
    Stop after generating project files.

.PARAMETER NoPrompt
    Fully unattended: no confirmation pauses (UAC may still appear once).

.EXAMPLE
    .\SetupUnrealDev.ps1              # full one-click run
    .\SetupUnrealDev.ps1 -CheckOnly   # diagnose only
#>
[CmdletBinding()]
param(
    [string]$ProjectPath = "",
    [string]$EnginePath  = "",
    [switch]$CheckOnly,
    [switch]$SkipInstall,
    [switch]$SkipBuild,
    [switch]$NoPrompt
)

$ErrorActionPreference = 'Stop'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch {}
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

# ---------------------------------------------------------------- logging ---
$LogDir = Join-Path $env:LOCALAPPDATA 'Revvy\SetupLogs'
if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Force -Path $LogDir | Out-Null }
$Stamp   = Get-Date -Format 'yyyyMMdd-HHmmss'
$LogFile = Join-Path $LogDir "setup-$Stamp.log"
# Keep the 30 newest logs; prune the rest so the folder never grows unbounded.
try {
    Get-ChildItem $LogDir -Filter '*.log' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -Skip 30 |
        Remove-Item -Force -ErrorAction SilentlyContinue
} catch {}
try { Start-Transcript -Path $LogFile -ErrorAction SilentlyContinue | Out-Null } catch {}

function Write-Step([string]$Msg) { Write-Host ""; Write-Host "==> $Msg" -ForegroundColor Cyan }
function Write-Ok  ([string]$Msg) { Write-Host "  [OK] $Msg"   -ForegroundColor Green }
function Write-Warn2([string]$Msg){ Write-Host "  [!!] $Msg"   -ForegroundColor Yellow }
function Write-Bad ([string]$Msg) { Write-Host "  [XX] $Msg"   -ForegroundColor Red }

function Stop-Setup([int]$Code, [string]$Msg) {
    if ($Msg) { if ($Code -eq 0) { Write-Ok $Msg } else { Write-Bad $Msg } }
    Write-Host ""
    Write-Host ("로그 파일: {0}" -f $LogFile) -ForegroundColor DarkGray
    try { Stop-Transcript | Out-Null } catch {}
    exit $Code
}

# ---------------------------------------------------------- version helpers ---
function ConvertTo-VerArray([string]$V) {
    # "14.38.33130" -> @(14,38,33130). Tolerates 2..4 fields.
    $parts = @()
    foreach ($p in ($V -split '\.')) {
        $n = 0
        if ([int]::TryParse($p, [ref]$n)) { $parts += $n } else { $parts += 0 }
    }
    while ($parts.Count -lt 4) { $parts += 0 }
    return $parts
}

function Compare-Ver([string]$LeftVer, [string]$RightVer) {
    # -1 / 0 / 1. NOTE: locals must not collide with params — PowerShell vars are
    # case-insensitive, and assigning to a [string]-typed param coerces arrays.
    $va = ConvertTo-VerArray $LeftVer; $vb = ConvertTo-VerArray $RightVer
    for ($i = 0; $i -lt 4; $i++) {
        if ($va[$i] -lt $vb[$i]) { return -1 }
        if ($va[$i] -gt $vb[$i]) { return 1 }
    }
    return 0
}

function Test-VerInRange([string]$V, [string]$Range) {
    # Range "14.50.0-14.50.35722" inclusive
    $ends = $Range -split '-'
    if ($ends.Count -ne 2) { return $false }
    return ((Compare-Ver $V $ends[0]) -ge 0 -and (Compare-Ver $V $ends[1]) -le 0)
}

# ------------------------------------------------------------ find project ---
Write-Step "프로젝트(.uproject) 찾기"
$UProject = $null
if ($ProjectPath) {
    # -LiteralPath: '[' ']' in paths are wildcard syntax to -Path and break real folders.
    if (-not (Test-Path -LiteralPath $ProjectPath)) { Stop-Setup 1 ".uproject 경로가 존재하지 않습니다: $ProjectPath" }
    $UProject = (Resolve-Path -LiteralPath $ProjectPath).Path
} else {
    # Script lives in <Project>/Plugins/Revvy/ -> walk up.
    $dir = Split-Path -Parent $MyInvocation.MyCommand.Path
    while ($dir) {
        $found = Get-ChildItem -LiteralPath $dir -Filter '*.uproject' -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { $UProject = $found.FullName; break }
        $parent = Split-Path -Parent $dir
        if ($parent -eq $dir) { break }
        $dir = $parent
    }
}
if (-not $UProject) { Stop-Setup 1 ".uproject 를 찾지 못했습니다. -ProjectPath 로 직접 지정하세요." }
$ProjectDir  = Split-Path -Parent $UProject
$ProjectName = [IO.Path]::GetFileNameWithoutExtension($UProject)
Write-Ok "프로젝트: $UProject"

# Long-path sanity: UBT + MSVC start failing near MAX_PATH.
if ($ProjectDir.Length -gt 140) {
    Write-Warn2 "프로젝트 경로가 깁니다 ($($ProjectDir.Length)자). 빌드가 260자 제한으로 실패하면 더 짧은 경로로 옮기세요."
}

# ------------------------------------------------------------ find engine ---
Write-Step "Unreal Engine 찾기"
$EngineRoot = $null
if ($EnginePath) {
    if (Test-Path -LiteralPath (Join-Path $EnginePath 'Engine\Build\BatchFiles\Build.bat')) { $EngineRoot = (Resolve-Path -LiteralPath $EnginePath).Path }
    else { Stop-Setup 1 "지정한 EnginePath 에 Engine\Build\BatchFiles\Build.bat 이 없습니다: $EnginePath" }
} else {
    $assoc = $null
    try { $assoc = (Get-Content $UProject -Raw | ConvertFrom-Json).EngineAssociation } catch {}
    if ($assoc) {
        # Launcher installs: HKLM\SOFTWARE\EpicGames\Unreal Engine\<ver>\InstalledDirectory
        try {
            $reg = Get-ItemProperty -Path "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$assoc" -ErrorAction SilentlyContinue
            if ($reg -and $reg.InstalledDirectory -and (Test-Path $reg.InstalledDirectory)) { $EngineRoot = $reg.InstalledDirectory }
        } catch {}
        # Source builds: HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds -> GUID = path
        if (-not $EngineRoot) {
            try {
                $builds = Get-ItemProperty -Path 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
                if ($builds -and $builds.PSObject.Properties[$assoc]) {
                    $cand = $builds.PSObject.Properties[$assoc].Value
                    if ($cand -and (Test-Path $cand)) { $EngineRoot = $cand }
                }
            } catch {}
        }
    }
    # Track: association declared but unresolvable on this PC (e.g. source-build GUID
    # registered only in the original machine's HKCU) — substitution must be loud.
    $AssocUnresolved = [bool]($assoc -and -not $EngineRoot)
    if (-not $EngineRoot) {
        # Fallback: newest UE_* under Program Files. NUMERIC version order —
        # lexical sort puts UE_5.9 above UE_5.10.
        $scan = Get-ChildItem 'C:\Program Files\Epic Games' -Directory -Filter 'UE_*' -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^UE_\d+(\.\d+){1,3}$' } |
                Sort-Object { [version]$_.Name.Substring(3) } -Descending | Select-Object -First 1
        if ($scan) { $EngineRoot = $scan.FullName }
        if ($EngineRoot -and $AssocUnresolved) {
            Write-Warn2 ".uproject 의 EngineAssociation '$assoc' 을(를) 이 PC에서 찾지 못해 $EngineRoot 로 대체합니다. 팀의 실제 엔진과 다르면 빌드 결과물이 맞지 않을 수 있습니다."
        }
    }
}
if (-not $EngineRoot -or -not (Test-Path (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'))) {
    Stop-Setup 1 "Unreal Engine 설치를 찾지 못했습니다. Epic Games Launcher 에서 엔진을 먼저 설치하거나 -EnginePath 를 지정하세요."
}
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
Write-Ok "엔진: $EngineRoot"

# ------------------------------------- toolchain requirements (engine JSON) ---
# Truth source: the engine's own SDK config. Falls back to UE 5.8 constants.
$MinMsvc    = '14.38.33130'
$BannedMsvc = @('14.50.0-14.50.35722','14.44.0-14.44.35210','14.40.0-14.43.99999','14.39.0-14.39.99999')
$MinWinSdk  = '10.0.19041.0'
$MainWinSdk = '10.0.22621.0'
$SdkJsonPath = Join-Path $EngineRoot 'Engine\Config\Windows\Windows_SDK.json'
if (Test-Path $SdkJsonPath) {
    try {
        # The file may contain // comment lines - strip before parsing.
        $raw  = (Get-Content $SdkJsonPath -Raw) -replace '(?m)^\s*//.*$', ''
        $json = $raw | ConvertFrom-Json
        if ($json.MinimumVisualCppVersion) { $MinMsvc = $json.MinimumVisualCppVersion }
        if ($json.BannedVisualCppVersions) { $BannedMsvc = @($json.BannedVisualCppVersions) }
        if ($json.MinVersion)              { $MinWinSdk = $json.MinVersion }
        if ($json.MainVersion)             { $MainWinSdk = $json.MainVersion }
        Write-Ok "엔진 요구사항 로드: MSVC >= $MinMsvc, Windows SDK >= $MinWinSdk (Windows_SDK.json)"
    } catch {
        Write-Warn2 "Windows_SDK.json 파싱 실패 - 내장 기본값(UE 5.8) 사용"
    }
} else {
    Write-Warn2 "Windows_SDK.json 없음 - 내장 기본값(UE 5.8) 사용"
}

# ------------------------------------------------------- detect VS + MSVC ---
Write-Step "Visual Studio C++ 툴체인 확인"
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$ValidToolchains = @()
$VsInstalls = @()
if (Test-Path $VsWhere) {
    try {
        # vswhere emits multi-line JSON; join lines before parsing (PS 5.1 pipes line-by-line).
        $vsJson = (& $VsWhere -products * -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8 2>$null) -join "`n"
        if ($vsJson) {
            # PS 5.1: ConvertFrom-Json emits the array as ONE pipeline item; unwrap via variable.
            $parsedVs = $vsJson | ConvertFrom-Json
            $VsInstalls = @($parsedVs)
        }
    } catch {}
    foreach ($vs in $VsInstalls) {
        $msvcRoot = Join-Path $vs.installationPath 'VC\Tools\MSVC'
        if (-not (Test-Path $msvcRoot)) { continue }
        foreach ($tsDir in (Get-ChildItem $msvcRoot -Directory -ErrorAction SilentlyContinue)) {
            $tv = $tsDir.Name
            if ((Compare-Ver $tv $MinMsvc) -lt 0) { continue }
            $banned = $false
            foreach ($r in $BannedMsvc) { if (Test-VerInRange $tv $r) { $banned = $true; break } }
            if ($banned) { Write-Warn2 "MSVC $tv ($($vs.displayName)) - 엔진 금지 버전(컴파일러 버그), 무시"; continue }
            # cl.exe actually present?
            if (Test-Path (Join-Path $tsDir.FullName 'bin\Hostx64\x64\cl.exe')) {
                $ValidToolchains += [pscustomobject]@{ Version = $tv; Product = $vs.displayName; Path = $tsDir.FullName }
            }
        }
    }
}
$ToolchainOk = ($ValidToolchains.Count -gt 0)
if ($ToolchainOk) {
    $best = $ValidToolchains | Sort-Object { [version]$_.Version } | Select-Object -Last 1
    Write-Ok "유효한 MSVC 툴체인: $($best.Version) ($($best.Product))"
} else {
    Write-Warn2 "유효한 MSVC C++ 툴체인이 없습니다."
}

# --------------------------------------------------------- detect Win SDK ---
Write-Step "Windows SDK 확인"
$SdkOk = $false
$SdkVersions = @()
foreach ($regPath in @('HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots',
                        'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots')) {
    try {
        $roots = Get-ItemProperty -Path $regPath -ErrorAction SilentlyContinue
        if ($roots -and $roots.KitsRoot10 -and (Test-Path $roots.KitsRoot10)) {
            $incDir = Join-Path $roots.KitsRoot10 'Include'
            foreach ($d in (Get-ChildItem $incDir -Directory -Filter '10.0.*' -ErrorAction SilentlyContinue)) {
                # A real SDK has um\windows.h
                if (Test-Path (Join-Path $d.FullName 'um\windows.h')) { $SdkVersions += $d.Name }
            }
            break
        }
    } catch {}
}
foreach ($sv in $SdkVersions) {
    if ((Compare-Ver $sv $MinWinSdk) -ge 0) { $SdkOk = $true }
}
if ($SdkOk) { Write-Ok ("Windows SDK: " + (($SdkVersions | Sort-Object) -join ', ')) }
else        { Write-Warn2 "Windows SDK $MinWinSdk 이상이 없습니다." }

# ------------------------------------------------------------- check-only ---
if ($CheckOnly) {
    Write-Step "진단 결과 (CheckOnly)"
    Write-Host ("  프로젝트     : {0}" -f $UProject)
    Write-Host ("  엔진         : {0}" -f $EngineRoot)
    Write-Host ("  MSVC 툴체인  : {0}" -f $(if ($ToolchainOk) { 'OK' } else { '없음/무효' }))
    Write-Host ("  Windows SDK  : {0}" -f $(if ($SdkOk) { 'OK' } else { '없음' }))
    Write-Host ("  Source 폴더  : {0}" -f $(if (Test-Path -LiteralPath (Join-Path $ProjectDir 'Source')) { '있음 (C++ 프로젝트)' } else { '없음 (콘텐츠 전용)' }))
    if ($AssocUnresolved) { Write-Warn2 "EngineAssociation 불일치 - 선언된 엔진을 이 PC에서 찾지 못해 대체했습니다 (위 경고 참고)" }
    if ($ToolchainOk -and $SdkOk) { Stop-Setup 0 "환경 정상 - 설치 필요 없음" } else { Stop-Setup 2 "환경 미비 - SetupUnrealDev.bat 을 실행하면 자동 설치됩니다" }
}

# --------------------------------------------------- install VS Build Tools ---
if (-not ($ToolchainOk -and $SdkOk)) {
    if ($SkipInstall) { Stop-Setup 2 "툴체인 미비 + SkipInstall 지정 - 설치를 건너뛰고 종료합니다." }

    Write-Step "Visual Studio Build Tools 자동 설치 (관리자 권한 창이 한 번 뜹니다)"
    # Component set follows the engine's own suggestions (Windows_SDK.json).
    # NOTE: the Build Tools SKU has no 'Game development with C++' workload -
    # Workload.VCTools + explicit components is the correct headless equivalent.
    $AddArgs = @(
        '--add Microsoft.VisualStudio.Workload.VCTools',
        '--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '--add Microsoft.VisualStudio.Component.Windows11SDK.22621',
        '--includeRecommended'
    ) -join ' '
    $InstallerArgs = "--quiet --wait --norestart $AddArgs"

    $Bootstrapper = Join-Path $env:TEMP ("vs_buildtools_{0}.exe" -f [guid]::NewGuid())
    $Installed = $false
    $ElevationDeclined = $false

    # Primary path: official bootstrapper (aka.ms/vs/stable = current VS 2026 channel).
    try {
        Write-Host "  다운로드 중: https://aka.ms/vs/stable/vs_buildtools.exe"
        Invoke-WebRequest -Uri 'https://aka.ms/vs/stable/vs_buildtools.exe' -OutFile $Bootstrapper -UseBasicParsing

        # Authenticode gate BEFORE elevated execution — rejects corrupted downloads
        # and trusted-CA MITM/redirect hijack. Tampered files read UnknownError,
        # so the gate must be Status -eq 'Valid' specifically.
        $sig = Get-AuthenticodeSignature -FilePath $Bootstrapper
        if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
            throw "설치기 서명 검증 실패 (Status=$($sig.Status)) - 네트워크 문제일 수 있으니 다시 시도하세요."
        }

        Write-Host "  설치 실행 중 (10~30분 소요, 조용히 진행됩니다)..."
        # .NET launch instead of Start-Process: UAC 거부가 Win32Exception(1223)으로
        # 정확히 잡힘 (Start-Process 는 원인 정보 없는 예외를 던짐).
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $Bootstrapper
        $psi.Arguments = $InstallerArgs
        $psi.Verb = 'runas'
        $psi.UseShellExecute = $true
        try {
            $p = [System.Diagnostics.Process]::Start($psi)
            $p.WaitForExit()
            $ec = $p.ExitCode
            if ($ec -eq 0)        { Write-Ok "Build Tools 설치 완료"; $Installed = $true }
            elseif ($ec -eq 3010) { Write-Warn2 "설치 완료 - 재부팅 필요 (일단 계속 진행)"; $Installed = $true }
            else                  { Write-Warn2 "설치기가 코드 $ec 로 종료했습니다." }
        } catch [System.ComponentModel.Win32Exception] {
            if ($_.Exception.NativeErrorCode -eq 1223) {
                Write-Bad "관리자 권한 요청이 거부되었습니다 - 설치하려면 UAC 창에서 '예'를 눌러야 합니다."
                $ElevationDeclined = $true
            } else {
                Write-Warn2 "부트스트래퍼 실행 실패: $($_.Exception.Message)"
            }
        }
    } catch {
        Write-Warn2 "부트스트래퍼 다운로드/검증 실패: $($_.Exception.Message)"
    } finally {
        Remove-Item $Bootstrapper -Force -ErrorAction SilentlyContinue
    }

    # UAC declined once → stop here; do NOT surprise the user with a second
    # elevation prompt from the winget fallback.
    if ($ElevationDeclined) {
        Stop-Setup 1 "관리자 승인이 거부되어 설치를 중단했습니다. 다시 실행한 뒤 UAC 창에서 '예'를 누르세요."
    }

    # Fallback: winget (also elevates via UAC).
    if (-not $Installed) {
        $winget = Get-Command winget -ErrorAction SilentlyContinue
        if ($winget) {
            Write-Host "  winget 으로 재시도..."
            & winget install -e --id Microsoft.VisualStudio.BuildTools --accept-source-agreements --accept-package-agreements --override "$InstallerArgs"
            if ($LASTEXITCODE -eq 0) { $Installed = $true }
        }
    }
    if (-not $Installed) {
        Stop-Setup 1 "Visual Studio Build Tools 설치에 실패했습니다. 로그: $LogFile / VS 설치 로그: %TEMP%\dd_*.log"
    }

    # Re-detect after install.
    $ValidToolchains = @()
    if (Test-Path $VsWhere) {
        $vsJson = (& $VsWhere -products * -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8 2>$null) -join "`n"
        if ($vsJson) {
            $parsedVs2 = $vsJson | ConvertFrom-Json
            foreach ($vs in @($parsedVs2)) {
                $msvcRoot = Join-Path $vs.installationPath 'VC\Tools\MSVC'
                if (-not (Test-Path $msvcRoot)) { continue }
                foreach ($tsDir in (Get-ChildItem $msvcRoot -Directory -ErrorAction SilentlyContinue)) {
                    $tv = $tsDir.Name
                    if ((Compare-Ver $tv $MinMsvc) -lt 0) { continue }
                    $banned = $false
                    foreach ($r in $BannedMsvc) { if (Test-VerInRange $tv $r) { $banned = $true; break } }
                    if (-not $banned -and (Test-Path (Join-Path $tsDir.FullName 'bin\Hostx64\x64\cl.exe'))) {
                        $ValidToolchains += [pscustomobject]@{ Version = $tv; Product = $vs.displayName }
                    }
                }
            }
        }
    }
    if ($ValidToolchains.Count -eq 0) {
        Stop-Setup 1 "설치 후에도 유효한 MSVC 툴체인을 찾지 못했습니다. 재부팅 후 다시 실행해 보세요."
    }
    Write-Ok "설치 후 툴체인 확인 완료"
}

# ------------------------------------------------- editor running warning ---
$EditorProc = Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue
if ($EditorProc) {
    Write-Warn2 "Unreal 에디터가 실행 중입니다. 빌드 충돌을 피하려면 에디터를 닫는 것이 좋습니다."
    if (-not $NoPrompt) {
        $ans = Read-Host "  계속할까요? (y/N)"
        if ($ans -notmatch '^[yY]') { Stop-Setup 1 "사용자 취소" }
    }
}

# ---------------------------------------------------- generate project files ---
$HasSource = Test-Path -LiteralPath (Join-Path $ProjectDir 'Source')
if ($HasSource) {
    Write-Step "Visual Studio 프로젝트 파일 생성"
    # Installed (launcher) engines have no GenerateProjectFiles.bat -
    # UBT's -projectfiles mode is the canonical path.
    $genLog = Join-Path $LogDir "genproj-$Stamp.log"
    cmd /s /c "`"$BuildBat`" -projectfiles -project=`"$UProject`" -game -waitmutex > `"$genLog`" 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Get-Content $genLog -Tail 25 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
        Stop-Setup 1 "프로젝트 파일 생성 실패 (전체 로그: $genLog)"
    }
    Write-Ok "프로젝트 파일 생성 완료 ($ProjectName.sln)"
} else {
    Write-Warn2 "Source 폴더가 없는 콘텐츠 전용 프로젝트 - 프로젝트 파일 생성을 건너뜁니다."
}

# --------------------------------------------------------- build editor ---
if ($SkipBuild) { Stop-Setup 0 "SkipBuild 지정 - 여기까지 완료" }

Write-Step "에디터 타깃 빌드"
$TargetName = $null
if ($HasSource) {
    $t = Get-ChildItem -LiteralPath (Join-Path $ProjectDir 'Source') -Filter '*Editor.Target.cs' -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($t) { $TargetName = $t.BaseName -replace '\.Target$', '' }
    if (-not $TargetName) { $TargetName = "${ProjectName}Editor" }
} else {
    # Content-only project with C++ plugins (e.g. Revvy): building the stock
    # UnrealEditor target with -project compiles the project's plugin modules.
    $TargetName = 'UnrealEditor'
}
# UBT target names are C# identifiers. Anything else = a stray/backup Target.cs
# matched first by the glob — reject it instead of splicing a broken cmd line.
if ($TargetName -notmatch '^[A-Za-z0-9_]+$') {
    Stop-Setup 1 "타깃 이름 '$TargetName' 이 유효하지 않습니다 - Source 폴더의 *Editor.Target.cs 백업/복사본 파일을 정리하세요."
}
Write-Host "  타깃: $TargetName Win64 Development"
$buildLog = Join-Path $LogDir "build-$Stamp.log"
cmd /s /c "`"$BuildBat`" $TargetName Win64 Development -project=`"$UProject`" -waitmutex > `"$buildLog`" 2>&1"
if ($LASTEXITCODE -ne 0) {
    Get-Content $buildLog -Tail 40 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    Stop-Setup 1 "빌드 실패 (전체 로그: $buildLog)"
}
Write-Ok "빌드 성공"

Write-Step "완료"
Write-Host "  이제 $ProjectName.uproject 를 더블클릭해 에디터를 열면 됩니다." -ForegroundColor Green
Stop-Setup 0 "환경 셋업 전체 완료"
