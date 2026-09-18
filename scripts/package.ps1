<#
.SYNOPSIS
  Builds the mod and lays the release out in dist\TheQuarryAccess-<version>.
.DESCRIPTION
  The folder holds everything the game needs to run the mod, laid out from the game's own folder
  (the one with TheQuarry.exe): copying its contents there installs the mod, and zipping them
  makes the release archive.
    SMG026\Binaries\Win64\dwmapi.dll                the loader that starts UE4SS with the game
    SMG026\Binaries\Win64\Tolk.dll ...              Tolk and the screen reader libraries it uses
    SMG026\Binaries\Win64\ue4ss\                    UE4SS with its configuration for The Quarry
    SMG026\Binaries\Win64\ue4ss\Mods\QuarryAccess\  the mod, its settings, language tables and licenses
  dist\TheQuarryAccess-<version>-symbols gets the symbol files and the linker map of the same
  build, which crash dumps from players of this version are read with; they are not for players.
  The version is the first line of the VERSION file.
  Every run builds only what changed since the last one (scripts\build.cmd) and lays both folders
  out anew, so it can be run again after any change.
.EXAMPLE
  scripts\package.cmd
#>
. (Join-Path $PSScriptRoot "common.ps1")

function Remove-QaPath([string]$Path) {
    if (Test-Path $Path) { Remove-Item $Path -Recurse -Force -ErrorAction Stop }
}

& (Join-Path $PSScriptRoot "build.cmd")
if ($LASTEXITCODE -ne 0) { throw "The build failed." }
# Only now: the build's tools write their progress to stderr, which Windows PowerShell can
# take for errors.
$ErrorActionPreference = "Stop"

$version = Get-QaVersion
$name = "TheQuarryAccess-$version"
$dist = Join-Path $QaRoot "dist"
$release = Join-Path $dist $name
$symbols = Join-Path $dist "$name-symbols"
Remove-QaPath $release
Remove-QaPath $symbols

$files = Get-QaInstallFiles
$win64 = Join-Path $release "SMG026\Binaries\Win64"
foreach ($file in $files) {
    $dest = Join-Path $win64 $file.Dest
    New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
    Copy-Item $file.Source $dest
}

$build = Join-Path $QaRoot "build"
New-Item -ItemType Directory -Force $symbols | Out-Null
foreach ($symbol in "src\main.pdb", "src\main.map", "Game__Shipping__Win64\bin\UE4SS.pdb", "Game__Shipping__Win64\bin\dwmapi.pdb") {
    $path = Join-Path $build $symbol
    if (Test-Path $path) { Copy-Item $path $symbols }
    else { Write-Warning "No $symbol in the build; the symbols folder goes without it." }
}

$size = ($files | ForEach-Object { (Get-Item $_.Source).Length } | Measure-Object -Sum).Sum
Write-Host ""
Write-Host ("{0} files, {1:N1} MB:" -f @($files).Count, ($size / 1MB))
foreach ($file in ($files | Sort-Object Dest)) { Write-Host ("  SMG026\Binaries\Win64\" + $file.Dest) }
Write-Host ""
Write-Host "Release: $release"
Write-Host "Symbols of this build, for its crash dumps (not for players): $symbols"
Write-Host "PACKAGE_OK"
