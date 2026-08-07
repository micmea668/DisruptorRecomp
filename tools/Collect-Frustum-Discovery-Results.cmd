@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-Frustum-Discovery-Results.ps1"
echo.
pause
