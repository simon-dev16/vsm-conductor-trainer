@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\Launch.ps1" -Portrait %*
if errorlevel 1 pause
