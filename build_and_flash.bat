@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_and_flash.ps1" %*
exit /b %ERRORLEVEL%
