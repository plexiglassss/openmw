@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Comparison.ps1" -Variant LegacyLayout
if errorlevel 1 pause
