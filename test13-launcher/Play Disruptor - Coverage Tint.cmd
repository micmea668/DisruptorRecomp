@echo off
setlocal
set "DISRUPTOR_EXPERIMENT_MODE=coverage-tint"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Play Disruptor.ps1" %*
if errorlevel 1 pause
