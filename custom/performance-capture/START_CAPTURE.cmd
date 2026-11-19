@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Capture.ps1" %*
if errorlevel 1 (
  echo.
  echo Capture could not finish. Read the error above; no game files were replaced.
)
echo.
pause
