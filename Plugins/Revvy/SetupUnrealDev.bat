@echo off
rem ============================================================================
rem  Revvy One-Click Unreal Dev Setup (for artists)
rem  Double-click this file. It will:
rem    1) find your .uproject and Unreal Engine install
rem    2) install the Visual Studio C++ toolchain if missing (one UAC prompt)
rem    3) generate Visual Studio project files
rem    4) build the editor target
rem  Pass-through args: -CheckOnly -SkipInstall -SkipBuild -NoPrompt
rem ============================================================================
chcp 65001 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0SetupUnrealDev.ps1" %*
set EXITCODE=%ERRORLEVEL%
echo.
if %EXITCODE%==0 (
    echo [Revvy] Setup finished successfully.
) else (
    echo [Revvy] Setup ended with code %EXITCODE%. See the log path printed above.
)
pause
exit /b %EXITCODE%
