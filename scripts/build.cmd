@echo off
rem Builds the mod (main.dll) together with UE4SS (UE4SS.dll) and the loader that starts UE4SS
rem with the game (dwmapi.dll), in the CMake preset "shipping" (Game__Shipping__Win64, Ninja).
rem Fetches the submodules first when they are missing (scripts\setup.cmd).
rem Usage: scripts\build.cmd [configure]
rem   configure  runs the CMake configure step even when the build directory is set up
rem Environment: QA_UE4SS_SOURCE_DIR  another RE-UE4SS checkout of the pinned commit to build
rem                                   instead of the submodule third_party\RE-UE4SS
rem Output: build\src\main.dll, build\Game__Shipping__Win64\bin\UE4SS.dll and dwmapi.dll
setlocal
set "ROOT=%~dp0.."
call "%~dp0env.cmd" || goto :fail

if defined QA_UE4SS_SOURCE_DIR goto :have_source
if not exist "%ROOT%\third_party\RE-UE4SS\deps\first\Unreal\CMakeLists.txt" call "%~dp0setup.cmd" || goto :fail
set "QA_UE4SS_SOURCE_DIR=%ROOT%\third_party\RE-UE4SS"
:have_source
for %%i in ("%QA_UE4SS_SOURCE_DIR%") do set "QA_UE4SS_SOURCE_DIR=%%~fi"
set "QA_UE4SS_SOURCE_DIR=%QA_UE4SS_SOURCE_DIR:\=/%"

pushd "%ROOT%"
rem The build directory is configured when it is new or when asked, and again when UE4SS is to
rem come from another tree than last time or something else (an IDE) configured it another way.
set "CONFIGURE="
set "LAST_SOURCE="
if /i "%~1"=="configure" set "CONFIGURE=1"
if not exist build\build.ninja set "CONFIGURE=1"
if exist build\CMakeCache.txt findstr /b /c:"CMAKE_BUILD_TYPE:STRING=Game__Shipping__Win64" build\CMakeCache.txt >nul || set "CONFIGURE=1"
if exist build\ue4ss-source.txt set /p LAST_SOURCE=<build\ue4ss-source.txt
if /i not "%LAST_SOURCE%"=="%QA_UE4SS_SOURCE_DIR%" set "CONFIGURE=1"
if not defined CONFIGURE goto :build
cmake --preset shipping "-DQA_UE4SS_SOURCE_DIR=%QA_UE4SS_SOURCE_DIR%" || goto :fail_popd
<nul set /p "=%QA_UE4SS_SOURCE_DIR%" > build\ue4ss-source.txt
:build
cmake --build --preset shipping || goto :fail_popd
for %%f in (build\src\main.dll build\Game__Shipping__Win64\bin\UE4SS.dll build\Game__Shipping__Win64\bin\dwmapi.dll) do (
    if not exist "%%f" (
        echo Missing after the build: %%f
        goto :fail_popd
    )
)
popd
echo BUILD_OK
exit /b 0

:fail_popd
popd
:fail
echo BUILD_FAILED
exit /b 1
