@echo off
rem Puts the build tools on PATH for the script that calls it: the MSVC x64 environment of the
rem newest Visual Studio 2022 (or its Build Tools) with the C++ workload, CMake and Ninja (the
rem "C++ CMake tools for Windows" component of Visual Studio, or any on PATH), and Rust's cargo.
rem When a tool is missing it says which and ends with errorlevel 1.
rem Called by the other scripts: call "%~dp0env.cmd" || exit /b 1
rem Environment: QA_VSWHERE  vswhere.exe to ask instead of the one the Visual Studio installer keeps

if not defined QA_VSWHERE set "QA_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%QA_VSWHERE%" goto :no_vs
set "QA_VSDIR="
for /f "usebackq tokens=*" %%i in (`"%QA_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "QA_VSDIR=%%i"
if not defined QA_VSDIR goto :no_vs
rem vcvarsall.bat itself runs vswhere.exe by name.
for %%i in ("%QA_VSWHERE%") do set "PATH=%PATH%;%%~dpi"
call "%QA_VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
where cl >nul 2>nul || goto :no_vs

where cmake >nul 2>nul || goto :no_cmake
where ninja >nul 2>nul || goto :no_ninja

rem rustup installs cargo into %CARGO_HOME%\bin, %USERPROFILE%\.cargo\bin unless told otherwise,
rem and adds it to PATH for new sessions only.
where cargo >nul 2>nul && exit /b 0
if defined CARGO_HOME (set "QA_CARGO_BIN=%CARGO_HOME%\bin") else set "QA_CARGO_BIN=%USERPROFILE%\.cargo\bin"
if exist "%QA_CARGO_BIN%\cargo.exe" set "PATH=%QA_CARGO_BIN%;%PATH%"
where cargo >nul 2>nul && exit /b 0
echo Rust was not found. Install it with rustup from https://rustup.rs and keep its default
echo MSVC toolchain.
exit /b 1

:no_vs
echo Visual Studio 2022 or its Build Tools, version 17.13 or newer, with the "Desktop development
echo with C++" workload was not found.
exit /b 1

:no_cmake
echo CMake was not found. Add the "C++ CMake tools for Windows" component to Visual Studio, or
echo install CMake 3.22 or newer.
exit /b 1

:no_ninja
echo Ninja was not found. Add the "C++ CMake tools for Windows" component to Visual Studio, or
echo put ninja.exe on PATH.
exit /b 1
