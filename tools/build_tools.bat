@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "OUT=%ROOT%\build"
set "SF_VS="

where cl.exe >nul 2>&1
if not errorlevel 1 goto :have_compiler

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_toolset

for /f "usebackq tokens=*" %%i in (`""!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"`) do set "SF_VS=%%i"
if not defined SF_VS goto :no_toolset

call "%SF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 goto :no_toolset

:have_compiler
if not exist "%OUT%" mkdir "%OUT%"

echo [*] Compiling make_test_frames.exe ...
cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W4 /permissive- ^
    /Fe"%OUT%\make_test_frames.exe" /Fo"%OUT%\tools_" ^
    "%ROOT%\tools\make_test_frames.cpp"
if errorlevel 1 goto :build_failed

echo [+] Built %OUT%\make_test_frames.exe
endlocal
exit /b 0

:no_toolset
echo error: no Visual Studio C++ toolset found.
endlocal
exit /b 1

:build_failed
echo [-] Build failed.
endlocal
exit /b 1
