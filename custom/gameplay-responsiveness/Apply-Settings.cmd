@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Apply-Settings.ps1" %*
set result=%errorlevel%
echo.
pause
exit /b %result%
