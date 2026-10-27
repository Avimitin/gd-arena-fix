@echo off
setlocal
cd /d "%~dp0"
set "GD_VS="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GD_VS=%%i"
if not defined GD_VS exit /b 1
call "%GD_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if defined GD_SDK_HEADERS set "INCLUDE=%GD_SDK_HEADERS%\ucrt;%GD_SDK_HEADERS%\shared;%GD_SDK_HEADERS%\um;%GD_SDK_HEADERS%\winrt;%INCLUDE%"
if defined GD_SDK_LIBROOT set "LIB=%GD_SDK_LIBROOT%\ucrt\x64;%GD_SDK_LIBROOT%\um\x64;%LIB%"
if defined GD_SDK_BIN set "PATH=%GD_SDK_BIN%;%PATH%"
cmake --preset windows-release
if errorlevel 1 exit /b 1
cmake --build --preset windows-release
exit /b %ERRORLEVEL%
