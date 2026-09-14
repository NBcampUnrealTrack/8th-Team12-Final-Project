@echo off
setlocal
call "%~dp0..\..\revvy.cmd" start
exit /b %ERRORLEVEL%
