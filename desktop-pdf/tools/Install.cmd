@echo off
rem Double-click to install Generic Print to PDF (sideload build) for the current user.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Sideload.ps1" %*
echo.
pause
