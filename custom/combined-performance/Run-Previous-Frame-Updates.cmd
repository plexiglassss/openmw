@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Combined.ps1" -Variant PreviousFrameUpdates
if errorlevel 1 pause
