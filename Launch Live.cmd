@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Launch.ps1" -Live
if errorlevel 1 pause
