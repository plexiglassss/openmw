@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Fix.ps1" %*
set "result=%errorlevel%"
echo.
pause
exit /b %result%
