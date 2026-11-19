@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Fix.ps1" -RestoreLatest %*
set "OpenmwSaneMagicRestoreExit=%ERRORLEVEL%"
echo.
pause
exit /b %OpenmwSaneMagicRestoreExit%
