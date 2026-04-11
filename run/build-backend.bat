@echo off
setlocal
set "REPO=%~dp0.."
set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

"%CMAKE%" --build "%REPO%\build" --config Release --target injector-backend
exit /b %ERRORLEVEL%
