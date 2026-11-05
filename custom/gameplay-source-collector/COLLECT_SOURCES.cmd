@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-Sources.ps1" %*
set "SOURCE_CHECK_EXIT=%ERRORLEVEL%"
echo.
if not "%SOURCE_CHECK_EXIT%"=="0" echo The source check did not finish. Please send the displayed error.
pause
exit /b %SOURCE_CHECK_EXIT%
