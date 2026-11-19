@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Fix.ps1" -RestoreLatest %*
set "ExitCode=%ERRORLEVEL%"
echo.
pause
exit /b %ExitCode%
