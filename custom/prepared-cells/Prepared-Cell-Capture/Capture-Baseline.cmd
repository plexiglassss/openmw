@echo off
setlocal
set "OPENMW_STAGED_ARRIVAL=0"
set "OPENMW_PREPARED_CELLS=0"
echo Recording with prepared cells DISABLED. Quit OpenMW normally to finish the ZIP.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Capture.ps1" -OpenMWExe "%~dp0..\openmw.exe" -Mode Detailed -RunLabel prepared-off -NoPrompt
set "capture_exit=%errorlevel%"
if not "%capture_exit%"=="0" echo Capture ended with code %capture_exit%.
pause
exit /b %capture_exit%
