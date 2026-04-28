@echo off
setlocal
set "REPO=%~dp0.."
set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"

call "%VSDEVCMD%" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %ERRORLEVEL%

"%CMAKE%" --build "%REPO%\build" --config Release --target injector-frontend
if errorlevel 1 exit /b %ERRORLEVEL%

set "WINDEPLOYQT=C:\Qt\6.7.3\msvc2019_64\bin\windeployqt.exe"
"%WINDEPLOYQT%" --qmldir "%REPO%\frontend\qml" "%REPO%\build\frontend\Release\injector-frontend.exe"
exit /b %ERRORLEVEL%
