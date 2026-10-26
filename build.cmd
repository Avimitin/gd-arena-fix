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
if not exist build mkdir build
if not exist dist mkdir dist
pushd build
cl /nologo /c /O2 /MT /W3 /TC ..\third_party\minhook\src\buffer.c ..\third_party\minhook\src\hook.c ..\third_party\minhook\src\trampoline.c ..\third_party\minhook\src\hde\hde64.c
if errorlevel 1 exit /b 1
lib /nologo /out:minhook.lib buffer.obj hook.obj trampoline.obj hde64.obj
if errorlevel 1 exit /b 1
rc /nologo /fo licenses.res ..\src\licenses.rc
if errorlevel 1 exit /b 1
cl /nologo /LD /std:c++20 /EHsc /O2 /MT /W4 /Zi ..\src\entry.cpp ..\src\platform.cpp ..\src\graphics.cpp ..\src\timing.cpp ..\src\note_diagnostics.cpp licenses.res minhook.lib bcrypt.lib user32.lib d3d9.lib /Fe:..\dist\gd-arena-fix.dll /link /DEBUG /PDB:..\dist\gd-arena-fix.pdb
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 ..\tests\smoke.cpp ..\src\platform.cpp ..\src\graphics.cpp ..\src\timing.cpp ..\src\note_diagnostics.cpp minhook.lib bcrypt.lib user32.lib d3d9.lib /Fe:..\dist\gd-arena-fix-test.exe
if errorlevel 1 exit /b 1
popd
copy /y gd-arena-fix.ini dist\gd-arena-fix.ini >nul
if errorlevel 1 exit /b 1
echo Build complete.
