@echo off
rem Makes a clone ready to build: checks the build tools, then fetches the RE-UE4SS submodule
rem together with the submodules it has of its own, and the Prism speech library into
rem third_party\prism. scripts\build.cmd runs it by itself when the submodules are missing.
rem Usage: scripts\setup.cmd
rem RE-UE4SS names its own submodules by SSH address; they are fetched over HTTPS here, so no SSH
rem key is needed. One of them, UEPseudo, is visible only to a GitHub account linked to an Epic
rem Games account, and Git asks to sign in to GitHub when it has no sign-in stored.
setlocal
call "%~dp0env.cmd" || exit /b 1
where git >nul 2>nul || goto :no_git

git -C "%~dp0.." -c "url.https://github.com/.insteadOf=git@github.com:" submodule update --init --recursive
if errorlevel 1 goto :fetch_failed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch-prism.ps1" || exit /b 1
echo SETUP_OK
exit /b 0

:no_git
echo Git was not found. Install Git for Windows from https://git-scm.com.
exit /b 1

:fetch_failed
echo Fetching the submodules failed. UEPseudo is fetched only by a GitHub account that is linked
echo to an Epic Games account and has accepted the invitation to the EpicGames organization.
exit /b 1
