@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Combined.ps1" -Variant WithoutAsyncDistances
if errorlevel 1 pause
