@echo off
setlocal
set "PYTHON_EXE=%LOCALAPPDATA%\Programs\VSMConductorTrainer\Python\python.exe"
if not exist "%PYTHON_EXE%" (
    echo Portable Python was not found: %PYTHON_EXE%
    pause
    exit /b 1
)
cd /d "%~dp0"
"%PYTHON_EXE%" "Backend\run_local.py"
echo Backend stopped.
pause
