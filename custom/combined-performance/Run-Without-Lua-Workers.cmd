@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Combined.ps1" -Variant WithoutLuaWorkers
if errorlevel 1 pause
