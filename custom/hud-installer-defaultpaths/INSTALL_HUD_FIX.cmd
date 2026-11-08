@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0AttendMe_Pixel_Updates\Install-Fix.ps1" %*
set "TASK_EXIT=%ERRORLEVEL%"
echo.
pause
exit /b %TASK_EXIT%
