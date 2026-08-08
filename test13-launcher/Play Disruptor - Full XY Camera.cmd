@echo off
setlocal
set "DISRUPTOR_EXPERIMENT_MODE=full-xy-camera"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Play Disruptor.ps1" %*
if errorlevel 1 pause
