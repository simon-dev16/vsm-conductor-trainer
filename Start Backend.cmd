@echo off
cd /d "%~dp0"
python -B -X utf8 Backend\server.py
if errorlevel 1 pause
