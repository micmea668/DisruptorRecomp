@echo off
setlocal
set "DISRUPTOR_EXPERIMENT_MODE=exact-camera"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Play Disruptor.ps1" %*
if errorlevel 1 (
    echo.
    echo The exact-camera test did not complete. See README-FIRST.txt.
    pause
)
