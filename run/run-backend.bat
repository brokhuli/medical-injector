@echo off
setlocal
set "REPO=%~dp0.."
cd /d "%REPO%"
"%REPO%\build\backend\Release\injector-backend.exe" "%REPO%\config.json"
exit /b %ERRORLEVEL%
