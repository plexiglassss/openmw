@echo off
setlocal DisableDelayedExpansion
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0OpenMW-CrashCapture.ps1" -Mode Restore
set "capture_result=%ERRORLEVEL%"
echo.
pause
exit /b %capture_result%
