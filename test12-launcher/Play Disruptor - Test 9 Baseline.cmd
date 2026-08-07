@echo off
setlocal
set "DISRUPTOR_EXPERIMENT_MODE=baseline"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Play Disruptor.ps1" %*
if errorlevel 1 (
    echo.
    echo The Test 9 baseline comparison did not complete. See README-FIRST.txt.
    pause
)
