@echo off
setlocal
set "REVVY_CLI=%~dp0Content\Python\revvy.py"
set "PYTHONUTF8=1"

where py >nul 2>&1
if errorlevel 1 goto use_python
py -3 "%REVVY_CLI%" %*
exit /b %ERRORLEVEL%

:use_python
where python >nul 2>&1
if errorlevel 1 goto python_missing
python "%REVVY_CLI%" %*
exit /b %ERRORLEVEL%

:python_missing
echo Python 3 was not found on PATH. 1>&2
exit /b 2
