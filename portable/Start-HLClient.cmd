@echo off
setlocal
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Start-HLClient.ps1" %*
set "hlc_exit=%errorlevel%"
if not "%hlc_exit%"=="0" (
    echo.
    echo HL Client did not start or ended with an error. Press any key to close.
    pause >nul
)
exit /b %hlc_exit%
