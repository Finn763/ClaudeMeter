@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL ( echo [ERROR] VS C++ tools not found & exit /b 1 )
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /EHsc /std:c++17 /DUNICODE /D_UNICODE test_host.cpp /Fe:test_host.exe || exit /b 1
echo --- running test_host.exe ---
"%~dp0test_host.exe"
endlocal
