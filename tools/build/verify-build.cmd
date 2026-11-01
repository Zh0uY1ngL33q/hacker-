@echo off
rem Double-click friendly wrapper for verify-build.ps1.
rem
rem Two problems this solves, both of which look like "it just closed":
rem   1. Windows blocks .ps1 by default (execution policy Restricted), so a
rem      plain .ps1 refuses to start at all.
rem   2. A console window closes the moment the script ends, taking the error
rem      message with it.
rem This runs PowerShell with -ExecutionPolicy Bypass for this one process only
rem (no machine setting is changed) and always pauses at the end.
rem
rem Usage:
rem   verify-build.cmd
rem   verify-build.cmd -Source G:\hacker\solstice\solstice-src\solstice-master-main\solstice-master-main
rem   verify-build.cmd -Fresh

setlocal
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=powershell.exe"

"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-build.ps1" -NoPause %*
set "RC=%ERRORLEVEL%"

echo.
echo --- verify-build finished with exit code %RC% ---
if "%RC%"=="0" (
    echo BUILD OK
) else (
    echo BUILD or CONFIGURE failed. Logs are in the build-verify folder next to this file.
)
echo.
pause
exit /b %RC%
