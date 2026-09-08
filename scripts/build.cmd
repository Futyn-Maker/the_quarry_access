@echo off
rem Builds QuarryAccess (main.dll) with the MSVC toolchain, CMake and Ninja.
rem Usage: scripts\build.cmd [configure]
rem   - first run (or "configure") runs the CMake configure step
rem   - QA_UE4SS_SOURCE_DIR may point to an existing RE-UE4SS checkout to reuse it
setlocal
set "ROOT=%~dp0.."
if "%QA_UE4SS_SOURCE_DIR%"=="" set "QA_UE4SS_SOURCE_DIR=D:/QuarryTools/RE-UE4SS"
if "%QA_VSWHERE%"=="" set "QA_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

for /f "usebackq tokens=*" %%i in (`"%QA_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if "%VSDIR%"=="" (
    echo Visual Studio with the C++ workload was not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
set "PATH=C:\Program Files\CMake\bin;%USERPROFILE%\.cargo\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

pushd "%ROOT%"
if not exist build\build.ninja (
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DQA_UE4SS_SOURCE_DIR="%QA_UE4SS_SOURCE_DIR%" || goto :fail
) else if "%~1"=="configure" (
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DQA_UE4SS_SOURCE_DIR="%QA_UE4SS_SOURCE_DIR%" || goto :fail
)
cmake --build build --target QuarryAccess || goto :fail
popd
echo BUILD_OK
exit /b 0

:fail
popd
echo BUILD_FAILED
exit /b 1
