<#
.SYNOPSIS
  Installs the last build into the game: the loader, UE4SS, the mod and Tolk.
.DESCRIPTION
  Copies the files of the release (scripts\package.ps1), taken from the last build
  (scripts\build.cmd), into the game. UE4SS's files go where the game already has UE4SS
  (Binaries\Win64\ue4ss, or Binaries\Win64 itself for an older install), or into
  Binaries\Win64\ue4ss when it has none. Program files are replaced; settings files that already
  exist are kept, the mod's QuarryAccess.ini too unless -ResetConfig is given.
  Refuses to run while the game is running, since its DLLs are locked then.
.PARAMETER GameDir
  The game's folder, the one with TheQuarry.exe. Without it: the QA_GAME_DIR environment
  variable, else the Steam library that has the game.
.PARAMETER ResetConfig
  Replaces an existing QuarryAccess.ini with the repository's default.
#>
param(
    [string]$GameDir,
    [switch]$ResetConfig
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$GameDir = Resolve-QuarryGameDir $GameDir
if (Get-Process -Name $QaExeName -ErrorAction SilentlyContinue) {
    throw "The Quarry is running. Close it before deploying (its DLLs are locked while the game runs)."
}
$win64 = Get-QuarryWin64Dir $GameDir
$ue4ssDir = Get-QuarryUE4SSDir $win64

$copied = 0
foreach ($file in Get-QaInstallFiles) {
    if ($file.Dest.StartsWith("ue4ss\")) { $dest = Join-Path $ue4ssDir $file.Dest.Substring(6) }
    else { $dest = Join-Path $win64 $file.Dest }
    if ($file.Kind -eq "Settings" -and (Test-Path $dest)) {
        $isModConfig = $file.Dest -eq "ue4ss\Mods\QuarryAccess\QuarryAccess.ini"
        if (-not ($ResetConfig -and $isModConfig)) {
            Write-Host "kept     $dest"
            continue
        }
    }
    New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
    Copy-Item $file.Source $dest -Force
    $copied++
}
Write-Host "Deployed $copied files; UE4SS in $ue4ssDir"
