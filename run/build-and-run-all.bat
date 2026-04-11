@echo off
setlocal
set "REPO=%~dp0.."
set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

echo === Building backend ===
"%CMAKE%" --build "%REPO%\build" --config Release --target injector-backend
if errorlevel 1 (
    echo Backend build failed.
    exit /b %ERRORLEVEL%
)

echo === Building frontend ===
"%CMAKE%" --build "%REPO%\build" --config Release --target injector-frontend
if errorlevel 1 (
    echo Frontend build failed.
    exit /b %ERRORLEVEL%
)

echo === Launching backend ===
cd /d "%REPO%"
start "injector-backend" "%REPO%\build\backend\Release\injector-backend.exe" "%REPO%\config.json"

echo Waiting for backend to come up...
timeout /t 2 /nobreak >nul

echo === Launching frontend ===
start "injector-frontend" "%REPO%\build\frontend\Release\injector-frontend.exe"

endlocal
exit /b 0
