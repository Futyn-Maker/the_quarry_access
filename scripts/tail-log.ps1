<#
.SYNOPSIS
  Prints the speech transcript (SAY lines) and errors from the mod log.
.PARAMETER Last
  Number of lines from the end of the log to scan.
.PARAMETER All
  Print all mod log lines instead of only SAY/ERROR lines.
#>
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\The Quarry",
    [int]$Last = 400,
    [switch]$All
)
$log = Join-Path $GameDir "SMG026\Binaries\Win64\Mods\QuarryAccess\QuarryAccess.log"
if (-not (Test-Path $log)) { throw "log not found: $log" }
$lines = Get-Content $log -Tail $Last -Encoding UTF8
if ($All) { $lines } else { $lines | Where-Object { $_ -match 'SAY |ERROR|self-check|ready|hotkey' } }
