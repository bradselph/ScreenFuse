@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "OUT=%ROOT%build"
set "SF_VS="

where cl.exe >nul 2>&1
if not errorlevel 1 goto :have_compiler

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_toolset

for /f "usebackq tokens=*" %%i in (`""!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"`) do set "SF_VS=%%i"
if not defined SF_VS goto :no_toolset
if not exist "%SF_VS%\VC\Auxiliary\Build\vcvars64.bat" goto :no_toolset

call "%SF_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 goto :no_toolset

:have_compiler
if not exist "%OUT%" mkdir "%OUT%"

echo [*] Compiling screenfuse_agent.exe ...
cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W4 /permissive- ^
    /Fe"%OUT%\screenfuse_agent.exe" /Fo"%OUT%\agent_" ^
    "%ROOT%src\agent_main.cpp"
if errorlevel 1 goto :build_failed

echo [*] Compiling screenfuse.exe ...
cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W4 /permissive- ^
    /Fe"%OUT%\screenfuse.exe" /Fo"%OUT%\controller_" ^
    "%ROOT%src\controller_main.cpp"
if errorlevel 1 goto :build_failed

echo.
echo [+] Built %OUT%\screenfuse_agent.exe   (copy to the PC being captured)
echo [+] Built %OUT%\screenfuse.exe         (runs on this PC)
endlocal
exit /b 0

:no_toolset
echo error: no Visual Studio C++ toolset found.
echo        Install the "Desktop development with C++" workload, then open a
echo        "x64 Native Tools Command Prompt for VS" and re-run this script.
endlocal
exit /b 1

:build_failed
echo [-] Build failed.
endlocal
exit /b 1
