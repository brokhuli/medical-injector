@echo off
setlocal
set "REPO=%~dp0.."
cd /d "%REPO%"
"%REPO%\build\frontend\Release\injector-frontend.exe" %*
exit /b %ERRORLEVEL%
