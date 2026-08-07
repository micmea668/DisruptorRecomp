@echo off
setlocal
rem Compatibility wrapper: Test 8B uses verified disc extraction because the
rem stable Release runtime does not start its debug server.
if "%~1"=="" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Extract-Frustum-Code-From-Disc.ps1"
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Extract-Frustum-Code-From-Disc.ps1" -DiscPath "%~1"
)
if errorlevel 1 (
    echo.
    echo Corrected Frustum Discovery did not complete. See frustum-capture-status.txt.
)
echo.
pause
