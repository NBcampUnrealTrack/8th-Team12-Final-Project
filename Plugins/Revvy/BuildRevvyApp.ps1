param(
    [switch]$NoClean,
    [switch]$SkipBlenderMCP
)

$ErrorActionPreference = "Stop"
$PluginRoot = (Resolve-Path $PSScriptRoot).Path
$PythonRoot = Join-Path $PluginRoot "Content\Python"
$OutputRoot = Join-Path $PluginRoot "Binaries\RevvyApp"
$WorkRoot = Join-Path $PluginRoot "Intermediate\RevvyApp"
$SpecRoot = Join-Path $WorkRoot "Spec"
$Icon = Join-Path $PluginRoot "Resources\Icon128.png"
$Mascot = Join-Path $PluginRoot "Resources\Revvy_256.png"
$Voice = Join-Path $PluginRoot "Content\instructions\revvy-voice.json"
$Skills = Join-Path $PluginRoot "Content\Skills"
$BlenderAddon = Join-Path $PluginRoot "Content\Blender\revvy_blender_bridge"
$Requirements = Join-Path $PythonRoot "requirements-revvy-app.txt"
# One source of truth. revvy_app_core.APP_VERSION is what the running app
# reports and what find_support_proxy_executable builds the proxy name from, so
# a version typed separately here would silently produce an app that cannot find
# its own proxy.
$Version = & python -c "import sys; sys.path.insert(0, r'$PythonRoot'); import revvy_app_core; print(revvy_app_core.APP_VERSION)"
if ($LASTEXITCODE -ne 0 -or -not $Version) {
    throw "Could not read APP_VERSION from revvy_app_core.py"
}
$Version = $Version.Trim()
$AppName = "Revvy-$Version"
$ProxyName = "RevvyProxy-$Version"

& python -c "import PIL, PyInstaller, tkinterdnd2"
if ($LASTEXITCODE -ne 0) {
    throw "Missing desktop app dependencies. Run: python -m pip install -r `"$Requirements`""
}

New-Item -ItemType Directory -Force -Path $OutputRoot, $WorkRoot, $SpecRoot | Out-Null

$Common = @("--noconfirm", "--onefile")
if (-not $NoClean) {
    $Common += "--clean"
}

Write-Host "Building $ProxyName.exe..."
& python -m PyInstaller @Common `
    --name $ProxyName `
    --windowed `
    --distpath $OutputRoot `
    --workpath (Join-Path $WorkRoot "Proxy") `
    --specpath $SpecRoot `
    --paths $PythonRoot `
    --add-data "$(Join-Path $PythonRoot 'revvy-proxy.py');Content/Python" `
    --add-data "$(Join-Path $PythonRoot 'revvy_secure_store.py');Content/Python" `
    --add-data "$(Join-Path $PythonRoot 'revvy_processes.py');Content/Python" `
    --add-data "$(Join-Path $PythonRoot 'revvy_mcp_upstreams.py');Content/Python" `
    --add-data "$(Join-Path $PythonRoot 'revvy_gen_texture.py');Content/Python" `
    --add-data "$(Join-Path $PythonRoot 'revvy_vfx_core.py');Content/Python" `
    --add-data "$Skills;Content/Skills" `
    (Join-Path $PythonRoot "revvy_proxy_host.py")
if ($LASTEXITCODE -ne 0) { throw "RevvyProxy build failed." }

if ($SkipBlenderMCP) {
    Write-Host "Keeping the existing RevvyBlenderMCP.exe."
} else {
    Write-Host "Building RevvyBlenderMCP.exe..."
    & python -m PyInstaller @Common `
        --name "RevvyBlenderMCP" `
        --console `
        --distpath $OutputRoot `
        --workpath (Join-Path $WorkRoot "BlenderMCP") `
        --specpath $SpecRoot `
        --paths $PythonRoot `
        --paths (Join-Path $PluginRoot "Content\Blender") `
        (Join-Path $PythonRoot "revvy_blender_mcp.py")
    if ($LASTEXITCODE -ne 0) { throw "Revvy Blender MCP build failed." }
}

Write-Host "Building $AppName.exe..."
& python -m PyInstaller @Common `
    --name $AppName `
    --windowed `
    --distpath $OutputRoot `
    --workpath (Join-Path $WorkRoot "App") `
    --specpath $SpecRoot `
    --paths $PythonRoot `
    --icon $Icon `
    --hidden-import revvy_updater `
    --hidden-import revvy_files `
    --hidden-import revvy_diff `
    --hidden-import revvy_processes `
    --add-data "$Icon;Resources" `
    --add-data "$Mascot;Resources" `
    --add-data "$Voice;Content/instructions" `
    --add-data "$BlenderAddon;Content/Blender/revvy_blender_bridge" `
    (Join-Path $PythonRoot "revvy_app.pyw")
if ($LASTEXITCODE -ne 0) { throw "Revvy app build failed." }

Write-Host "Built: $(Join-Path $OutputRoot ($AppName + '.exe'))"
