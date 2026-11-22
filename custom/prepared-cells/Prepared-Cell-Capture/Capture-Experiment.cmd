@echo off
setlocal
set "OPENMW_STAGED_ARRIVAL=1"
set "OPENMW_PREPARED_CELLS=1"
echo Recording with prepared cells ENABLED. Quit OpenMW normally to finish the ZIP.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Capture.ps1" -OpenMWExe "%~dp0..\openmw.exe" -Mode Detailed -RunLabel prepared-on -NoPrompt
set "capture_exit=%errorlevel%"
if not "%capture_exit%"=="0" echo Capture ended with code %capture_exit%.
pause
exit /b %capture_exit%
