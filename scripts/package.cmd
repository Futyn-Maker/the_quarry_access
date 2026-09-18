@echo off
rem Builds the mod and lays the release out in dist\TheQuarryAccess-<version>: runs
rem scripts\package.ps1.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1"
exit /b %ERRORLEVEL%
