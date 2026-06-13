@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL ( echo [ERROR] VS C++ tools not found & exit /b 1 )
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /MT /LD /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE /DNDEBUG ClaudeMeterPlugin.cpp /Fe:ClaudeMeter.dll /link /DEF:ClaudeMeter.def || exit /b 1
echo [OK] built ClaudeMeter.dll
echo --- verifying export name ---
dumpbin /nologo /exports ClaudeMeter.dll | findstr /C:"TMPluginGetInstance" || ( echo [ERROR] export missing & exit /b 1 )
endlocal
