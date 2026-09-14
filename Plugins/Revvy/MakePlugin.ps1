# Revvy Plugin Packaging Script for Fab Marketplace Submission
# Creates a clean plugin package excluding build artifacts and development files
# Usage: .\MakePlugin.ps1 [-ExpectedVersion <version>] [-OutputRoot <directory>]

param(
    [Alias("Version")]
    [string]$ExpectedVersion = "",
    [string]$PackageName = "Revvy-Package",
    [string]$ZipName = "Revvy.zip",
    [string]$OutputRoot = ""
)

# Script configuration
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceDir = [System.IO.Path]::GetFullPath($ScriptDir)
$ParentDir = Split-Path -Parent $ScriptDir
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = $ParentDir
}

if ($PackageName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$' -or
    $PackageName -in @(".", "..")) {
    throw "PackageName must be a simple 1-64 character leaf name."
}
if ($ZipName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}\.zip$') {
    throw "ZipName must be a simple .zip leaf name."
}

$OutputDir = [System.IO.Path]::GetFullPath($OutputRoot)
$sourcePrefix = $SourceDir.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar,
    [System.IO.Path]::AltDirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
if ($OutputDir.Equals($SourceDir, [System.StringComparison]::OrdinalIgnoreCase) -or
    $OutputDir.StartsWith($sourcePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be outside the Revvy source directory."
}

$PackageDir = [System.IO.Path]::GetFullPath((Join-Path $OutputDir $PackageName))
$ZipPath = [System.IO.Path]::GetFullPath((Join-Path $OutputDir $ZipName))
if (-not [System.IO.Path]::GetDirectoryName($PackageDir).Equals(
    $OutputDir,
    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "PackageDir must be a direct child of OutputRoot."
}
if (-not [System.IO.Path]::GetDirectoryName($ZipPath).Equals(
    $OutputDir,
    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "ZipPath must be a direct child of OutputRoot."
}

$SourcePluginPath = Join-Path $SourceDir "Revvy.uplugin"
if (-not (Test-Path -LiteralPath $SourcePluginPath)) {
    throw "Revvy.uplugin was not found at $SourcePluginPath"
}
$SourcePlugin = Get-Content -LiteralPath $SourcePluginPath -Raw | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace([string]$SourcePlugin.VersionName)) {
    throw "Revvy.uplugin must define VersionName."
}
if ([string]::IsNullOrWhiteSpace([string]$SourcePlugin.Version)) {
    throw "Revvy.uplugin must define the monotonically increasing integer Version."
}
if (-not [string]::IsNullOrWhiteSpace($ExpectedVersion) -and
    $ExpectedVersion -ne [string]$SourcePlugin.VersionName) {
    throw "Expected VersionName '$ExpectedVersion', found '$($SourcePlugin.VersionName)' in Revvy.uplugin."
}

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

Write-Host "=== Revvy Plugin Packaging Script ===" -ForegroundColor Cyan
Write-Host "Source Directory: $SourceDir" -ForegroundColor Gray
Write-Host "Descriptor Version: $($SourcePlugin.VersionName) ($($SourcePlugin.Version))" -ForegroundColor Gray
Write-Host "Package Directory: $PackageDir" -ForegroundColor Gray
Write-Host "Zip Output: $ZipPath" -ForegroundColor Gray
Write-Host ""

# Always clean previous package and zip file at startup
Write-Host "Cleaning previous package and zip file..." -ForegroundColor Yellow
if (Test-Path $PackageDir) {
    Remove-Item $PackageDir -Recurse -Force
    Write-Host "  Removed existing package directory" -ForegroundColor Gray
}
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath -Force
    Write-Host "  Removed existing zip file" -ForegroundColor Gray
}

# Create package directory
Write-Host "Creating package directory..." -ForegroundColor Green
New-Item -ItemType Directory -Path $PackageDir -Force | Out-Null

# Define files and directories to exclude for Fab submission
$ExcludeDirectories = @(
    "Binaries",           # Build artifacts
    "Intermediate",       # Build artifacts  
    "Packaged",          # Packaged builds
    "Saved",             # Local editor state, QA captures, tool-call history
    "Tests",             # Development-only test suite
    "Scripts",           # CI/development scripts
    "ThirdParty",        # Unlicensed/non-redistributable local helper binaries
    ".git",              # Git repository
    ".github",           # GitHub workflows and config (development only)
    ".claude",           # Local agent configuration/state
    ".vs",               # Visual Studio
    ".vscode",           # VS Code
    ".cursor",           # Cursor IDE
    "__pycache__",       # Python cache
    ".pytest_cache",     # Python test cache
    ".venv",             # Python virtual environment (users should create their own)
    "venv",              # Alternative virtual environment naming
    "build",             # Python build directories
    "dist",              # Python distribution directories
    "*.egg-info",        # Python package metadata
    "docs",              # Development documentation (user docs moved to Resources)
    "node_modules"       # Node.js modules
)

$ExcludeFiles = @(
    "*.log",             # Log files
    "*.tmp",             # Temporary files
    "*.pdb",             # Program database files
    "*.lib",             # Library files (will be rebuilt)
    "*.exp",             # Export files
    "*.ilk",             # Incremental linking files
    "*.exe",             # Executable files (not allowed in FAB)
    "*.dll",             # Dynamic libraries (will be rebuilt)
    "*~",                # Backup files
    "*.pyc",             # Python compiled files
    "*.pyo",             # Python optimized files
    ".DS_Store",         # macOS system files
    "Thumbs.db",         # Windows thumbnail cache
    "Desktop.ini",       # Windows folder settings
    "revvy_ue.log"        # Specific log file
)

# Development-only files that shouldn't be in marketplace submission
$ExcludeDevFiles = @(
    "CLAUDE.md",             # Claude AI context (development only)
    "DEAD_HANDLERS_DELETED.md",
    "HANDLER_AUDIT.md", 
    "HANDLER_AUDIT_COMPLETE.md",
    "ISSUE_SUMMARY.md",
    "BUILD_PLUGIN.md",       # Development documentation not needed by end users
    "BuildPlugin.bat",
    "MCP-Inspector.bat",
    "MakePlugin.ps1",        # Build script not needed by end users
    "BuildAndLaunchGame.ps1", # Game-specific launch script not needed by end users
    "AddCopyrights.ps1",     # Development script not needed by end users
    "FAB-DESCRIPTION.md",    # Development file not needed by end users
    "FAB_Tech_Details.md",   # FAB submission details not needed by end users
    "FAB-Checklist.md",      # Internal checklist not needed by end users
    ".gitignore",            # Git-specific file not needed by end users
    # --- Local machine state and the process-spawning convenience launcher are
    #     excluded. Runtime credentials are generated under the user's state
    #     directory. revvy-proxy.py itself is required runtime code for external
    #     MCP, image generation, and typed Blender upstream routing. ---
    "revvy-proxy.json",     # Local proxy config with bearer token (auto-generated at runtime)
    "start-revvy-proxy.bat" # Proxy launcher (kills/spawns background process, Windows startup helper)
)

# Note: test_prompts folder is now included for user reference and examples

Write-Host "Copying plugin files (excluding build artifacts)..." -ForegroundColor Green

# Use robocopy for efficient copying with exclusions
$RobocopyArgs = @(
    $SourceDir,
    $PackageDir,
    "/E",                # Copy subdirectories including empty ones
    "/XD"                # Exclude directories
) + $ExcludeDirectories + @(
    "/XF"                # Exclude files
) + $ExcludeFiles + $ExcludeDevFiles

Write-Host "Running robocopy with exclusions..." -ForegroundColor Gray
& robocopy @RobocopyArgs | Out-Null

# Robocopy exit codes: 0=no files copied, 1=files copied successfully, 2=extra files/folders detected
# Exit codes 0-7 are considered successful
$RobocopyExitCode = $LASTEXITCODE
if ($RobocopyExitCode -le 7) {
    Write-Host "  Files copied successfully" -ForegroundColor Gray
} else {
    Write-Host "ERROR: Robocopy failed (exit code: $RobocopyExitCode)" -ForegroundColor Red
    exit 1
}

# Verify essential files are present
Write-Host "Verifying package contents..." -ForegroundColor Green

$RequiredFiles = @(
    "Revvy.uplugin",
    "README.md",
    "Content\Python\revvy.py",
    "Content\Python\revvy-proxy.py",
    "Content\Python\revvy_animation_bridge.py",
    "Content\Python\revvy_app_core.py",
    "Content\Python\revvy_blender_mcp.py",
    "Content\Python\revvy_gen_texture.py",
    "Content\Python\revvy_mcp_upstreams.py",
    "Content\Python\revvy_native_toolset.py",
    "Content\Python\revvy_secure_store.py",
    "Content\Python\revvy_vfx_core.py",
    "Content\Blender\install_revvy_blender_bridge.py",
    "Content\Blender\revvy_blender_bridge\__init__.py",
    "Content\Blender\revvy_blender_bridge\common.py",
    "Content\Blender\revvy_blender_bridge\modeling.py",
    "Content\Blender\revvy_blender_bridge\security.py",
    "Content\Skills\shared\gpt-sol-image-generation\SKILL.md",
    "Content\Skills\shared\gpt-sol-image-generation\qa-checklist.md",
    "Content\Skills\shared\blender-3d-modeling\SKILL.md",
    "Content\Skills\shared\blender-animation-roundtrip\SKILL.md",
    "Content\Skills\revvy-capabilities.json"
)

$RequiredDirs = @(
    "Source",
    "Content", 
    "Config"
)

$MissingItems = @()

foreach ($file in $RequiredFiles) {
    $filePath = Join-Path $PackageDir $file
    if (-not (Test-Path $filePath)) {
        $MissingItems += "File: $file"
    }
}

foreach ($dir in $RequiredDirs) {
    $dirPath = Join-Path $PackageDir $dir
    if (-not (Test-Path $dirPath)) {
        $MissingItems += "Directory: $dir"
    }
}

if ($MissingItems.Count -gt 0) {
    Write-Host "ERROR: Missing required items:" -ForegroundColor Red
    foreach ($item in $MissingItems) {
        Write-Host "  - $item" -ForegroundColor Red
    }
    exit 1
}

# Verify excluded items are not present
$ExcludedItems = @()
foreach ($dir in $ExcludeDirectories) {
    $dirPath = Join-Path $PackageDir $dir
    if (Test-Path $dirPath) {
        $ExcludedItems += "Directory: $dir"
    }
}

if ($ExcludedItems.Count -gt 0) {
    Write-Host "ERROR: Found excluded items in the package:" -ForegroundColor Red
    foreach ($item in $ExcludedItems) {
        Write-Host "  - $item" -ForegroundColor Red
    }
    exit 1
}

# Calculate package size
Write-Host "Calculating package size..." -ForegroundColor Green
$PackageStats = Get-ChildItem $PackageDir -Recurse | Measure-Object -Property Length -Sum
$PackageSizeMB = [math]::Round($PackageStats.Sum / 1MB, 2)
$FileCount = $PackageStats.Count

Write-Host "  Files: $FileCount" -ForegroundColor Gray
Write-Host "  Size: $PackageSizeMB MB" -ForegroundColor Gray

# Create ZIP archive
Write-Host "Creating ZIP archive..." -ForegroundColor Green
try {
    # Use .NET compression for better control
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    
    if (Test-Path $ZipPath) {
        Remove-Item $ZipPath -Force
    }
    
    [System.IO.Compression.ZipFile]::CreateFromDirectory($PackageDir, $ZipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    
    $ZipStats = Get-Item $ZipPath
    $ZipSizeMB = [math]::Round($ZipStats.Length / 1MB, 2)
    
    Write-Host "  ZIP created successfully" -ForegroundColor Gray
    Write-Host "  ZIP size: $ZipSizeMB MB" -ForegroundColor Gray
}
catch {
    Write-Host "ERROR: Failed to create ZIP archive: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# Delete the package directory to prevent build conflicts
Write-Host "Cleaning up package directory..." -ForegroundColor Yellow
try {
    if (Test-Path $PackageDir) {
        Remove-Item $PackageDir -Recurse -Force
        Write-Host "  Package directory removed" -ForegroundColor Gray
    }
}
catch {
    Write-Host "WARNING: Failed to delete package directory: $($_.Exception.Message)" -ForegroundColor Yellow
}

# Final summary
Write-Host ""
Write-Host "=== Package Creation Complete ===" -ForegroundColor Green
Write-Host "ZIP Archive: $ZipPath" -ForegroundColor White
Write-Host "ZIP Size: $ZipSizeMB MB" -ForegroundColor White
Write-Host ""
Write-Host "Ready for Fab Marketplace submission!" -ForegroundColor Cyan
exit 0
