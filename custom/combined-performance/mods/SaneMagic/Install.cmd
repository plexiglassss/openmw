@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Fix.ps1" %*
set "OpenmwSaneMagicInstallExit=%ERRORLEVEL%"
echo.
pause
exit /b %OpenmwSaneMagicInstallExit%
