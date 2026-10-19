@echo off
rem Builds and runs the tests of the mod's engine-free modules (tests\, doctest) in build\tests,
rem with the same tools as the build (scripts\env.cmd). The first run fetches doctest, which
rem needs the network once; after that the build folder keeps it.
rem Usage: scripts\test.cmd
rem Output: build\tests\tests.exe, run; ends with TESTS_OK or TESTS_FAILED.
setlocal
set "ROOT=%~dp0.."
call "%~dp0env.cmd" || goto :fail
pushd "%ROOT%"
if not exist build\tests\build.ninja (
    cmake -S tests -B build\tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_UPDATES_DISCONNECTED=ON || goto :fail_popd
)
cmake --build build\tests || goto :fail_popd
build\tests\tests.exe || goto :fail_popd
popd
echo TESTS_OK
exit /b 0

:fail_popd
popd
:fail
echo TESTS_FAILED
exit /b 1
