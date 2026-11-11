@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Combined.ps1" -Variant PreviousSpellReads
if errorlevel 1 pause
