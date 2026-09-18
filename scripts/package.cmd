@echo off
rem Builds the mod and packs the release archive: runs scripts\package.ps1 with the arguments
rem given, for example "scripts\package.cmd -Clean" to build everything from scratch first.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
exit /b %ERRORLEVEL%
