@echo off
rem Builds the mod (main.dll) together with UE4SS (UE4SS.dll) and the loader that starts UE4SS
rem with the game (dwmapi.dll), in the CMake preset "shipping" (Game__Shipping__Win64, Ninja).
rem Fetches the submodules first when they are missing (scripts\setup.cmd), and the Prism speech
rem library when third_party\prism holds another release than the repository pins
rem (scripts\fetch-prism.ps1).
rem Usage: scripts\build.cmd [configure]
rem   configure  runs the CMake configure step even when the build directory is set up
rem Environment: QA_UE4SS_SOURCE_DIR  another RE-UE4SS checkout of the pinned commit to build
rem                                   instead of the submodule third_party\RE-UE4SS
rem Output: build\src\main.dll, build\Game__Shipping__Win64\bin\UE4SS.dll and dwmapi.dll
setlocal
set "ROOT=%~dp0.."
call "%~dp0env.cmd" || goto :fail

if defined QA_UE4SS_SOURCE_DIR goto :have_source
rem Missing submodules are fetched. One checked out at another commit than the repository pins
rem is built as it is, since that is how a newer UE4SS gets tried, and said.
set "MISSING="
set "MOVED="
if not exist "%ROOT%\third_party\RE-UE4SS\deps\first\Unreal\CMakeLists.txt" set "MISSING=1"
git -C "%ROOT%" submodule status --recursive 2>nul | findstr /b /c:"-" >nul && set "MISSING=1"
if defined MISSING call "%~dp0setup.cmd" || goto :fail
git -C "%ROOT%" submodule status --recursive 2>nul | findstr /b /c:"+" >nul && set "MOVED=1"
if defined MOVED echo Note: a submodule is checked out at another commit than the repository pins, and is built as it is. scripts\setup.cmd checks out the pinned commits.
set "QA_UE4SS_SOURCE_DIR=%ROOT%\third_party\RE-UE4SS"
:have_source
for %%i in ("%QA_UE4SS_SOURCE_DIR%") do set "QA_UE4SS_SOURCE_DIR=%%~fi"
set "QA_UE4SS_SOURCE_DIR=%QA_UE4SS_SOURCE_DIR:\=/%"

rem The Prism speech library is not in the repository: it is fetched into third_party\prism
rem whenever what is there is not the release third_party\prism.version pins.
set "PRISM_WANT="
set "PRISM_HAVE="
for /f "usebackq delims=" %%v in ("%ROOT%\third_party\prism.version") do if not defined PRISM_WANT set "PRISM_WANT=%%v"
if exist "%ROOT%\third_party\prism\VERSION" for /f "usebackq delims=" %%v in ("%ROOT%\third_party\prism\VERSION") do if not defined PRISM_HAVE set "PRISM_HAVE=%%v"
if not exist "%ROOT%\third_party\prism\include\prism.h" set "PRISM_HAVE="
if not exist "%ROOT%\third_party\prism\bin\prism.dll" set "PRISM_HAVE="
if /i not "%PRISM_WANT%"=="%PRISM_HAVE%" powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch-prism.ps1" || goto :fail

pushd "%ROOT%"
rem The build directory is configured when it is new or when asked, and again when UE4SS is to
rem come from another tree than last time, when the preset changed, or when something else (an
rem IDE) configured it another way. Otherwise Ninja itself reruns CMake when a CMakeLists changed.
set "CONFIGURE="
set "LAST_SOURCE="
if /i "%~1"=="configure" set "CONFIGURE=1"
if not exist build\build.ninja set "CONFIGURE=1"
if exist build\CMakeCache.txt findstr /b /c:"CMAKE_BUILD_TYPE:STRING=Game__Shipping__Win64" build\CMakeCache.txt >nul || set "CONFIGURE=1"
if exist build\ue4ss-source.txt set /p LAST_SOURCE=<build\ue4ss-source.txt
if /i not "%LAST_SOURCE%"=="%QA_UE4SS_SOURCE_DIR%" set "CONFIGURE=1"
fc /b CMakePresets.json build\presets-used.json >nul 2>nul || set "CONFIGURE=1"
if not defined CONFIGURE goto :build
cmake --preset shipping "-DQA_UE4SS_SOURCE_DIR=%QA_UE4SS_SOURCE_DIR%" || goto :fail_popd
<nul set /p "=%QA_UE4SS_SOURCE_DIR%" > build\ue4ss-source.txt
copy /y CMakePresets.json build\presets-used.json >nul
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
