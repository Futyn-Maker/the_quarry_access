@echo off
rem Formats all C++ sources of the mod and of its tests with clang-format (the LLVM tools bundled
rem with Visual Studio).
rem Usage: scripts\format.cmd        - reformat in place
rem        scripts\format.cmd check  - only report files that would change (exit code 1 if any)
setlocal enabledelayedexpansion
set "ROOT=%~dp0.."
set "MODE=%~1"
if "%QA_VSWHERE%"=="" set "QA_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%QA_VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
set "CF=%VSDIR%\VC\Tools\Llvm\x64\bin\clang-format.exe"
if not exist "%CF%" (
    where clang-format >nul 2>nul && set "CF=clang-format"
)
if not exist "%CF%" if not "%CF%"=="clang-format" (
    echo clang-format not found. Install the "C++ Clang tools for Windows" component or put clang-format on PATH.
    exit /b 1
)
set "FAILED=0"
for /r "%ROOT%\src" %%f in (*.cpp *.hpp) do call :format "%%f"
for /r "%ROOT%\tests" %%f in (*.cpp *.hpp) do call :format "%%f"
if "%MODE%"=="check" (
    if "!FAILED!"=="1" (echo FORMAT CHECK FAILED & exit /b 1) else (echo FORMAT CHECK OK & exit /b 0)
)
echo formatted
exit /b 0

:format
if "%MODE%"=="check" (
    "%CF%" --dry-run --Werror "%~1" >nul 2>nul || (echo needs formatting: %~1 & set "FAILED=1")
) else (
    "%CF%" -i "%~1"
)
exit /b 0
