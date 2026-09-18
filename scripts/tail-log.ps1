<#
.SYNOPSIS
  Prints the speech transcript (SAY lines) and errors from the mod log.
.PARAMETER GameDir
  The game's folder, the one with TheQuarry.exe. Without it: the QA_GAME_DIR environment
  variable, else the Steam library that has the game.
.PARAMETER Last
  Number of lines from the end of the log to scan.
.PARAMETER All
  Print all mod log lines instead of only SAY/ERROR lines.
#>
param(
    [string]$GameDir,
    [int]$Last = 400,
    [switch]$All
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")
$GameDir = Resolve-QuarryGameDir $GameDir
$log = Join-Path (Get-QuarryUE4SSDir (Get-QuarryWin64Dir $GameDir)) "Mods\QuarryAccess\QuarryAccess.log"
if (-not (Test-Path $log)) { throw "log not found: $log" }
$lines = Get-Content $log -Tail $Last -Encoding UTF8
if ($All) { $lines } else { $lines | Where-Object { $_ -match 'SAY |ERROR|self-check|ready|hotkey' } }
