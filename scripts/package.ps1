<#
.SYNOPSIS
  Builds the mod and packs the release archive dist\TheQuarryAccess-<version>.zip.
.DESCRIPTION
  The archive holds everything the game needs to run the mod, laid out from the game's own
  folder (the one with TheQuarry.exe), so unpacking it there installs the mod:
    SMG026\Binaries\Win64\dwmapi.dll             the loader that starts UE4SS with the game
    SMG026\Binaries\Win64\Tolk.dll ...           Tolk and the screen reader libraries it uses
    SMG026\Binaries\Win64\ue4ss\                 UE4SS with its configuration for The Quarry
    SMG026\Binaries\Win64\ue4ss\Mods\QuarryAccess\  the mod, its settings, language tables and licenses
  dist\TheQuarryAccess-<version>\ keeps the same files unpacked, and
  dist\TheQuarryAccess-<version>-symbols.zip the symbol files and the linker map of this very
  build, which crash dumps from players of this version are read with; it is not for players.
  The version is the first line of the VERSION file.
.PARAMETER SkipBuild
  Packs the files of the last build without building first.
.PARAMETER Clean
  Deletes the build directory first, so that everything is built from scratch.
.EXAMPLE
  scripts\package.cmd
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Clean
#>
param(
    [switch]$SkipBuild,
    [switch]$Clean
)
. (Join-Path $PSScriptRoot "common.ps1")

function New-QaZip([string]$SourceDir, [string]$ZipPath) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $base = (Resolve-Path $SourceDir).Path.TrimEnd('\') + '\'
    $archive = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in (Get-ChildItem $SourceDir -Recurse -File | Sort-Object FullName)) {
            # Entry names use forward slashes, the separator every unpacker understands.
            $entry = $file.FullName.Substring($base.Length).Replace('\', '/')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $entry, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    }
    finally {
        $archive.Dispose()
    }
}

function Remove-QaPath([string]$Path) {
    if (Test-Path $Path) { Remove-Item $Path -Recurse -Force -ErrorAction Stop }
}

if (-not $SkipBuild) {
    if ($Clean) {
        Write-Host "Deleting the build directory."
        Remove-QaPath (Join-Path $QaRoot "build")
    }
    & (Join-Path $PSScriptRoot "build.cmd")
    if ($LASTEXITCODE -ne 0) { throw "The build failed." }
}
# Only now: the build's tools write their progress to stderr, which Windows PowerShell can
# take for errors.
$ErrorActionPreference = "Stop"

$version = Get-QaVersion
$name = "TheQuarryAccess-$version"
$dist = Join-Path $QaRoot "dist"
$stage = Join-Path $dist $name
$zip = Join-Path $dist "$name.zip"
$symbolsStage = Join-Path $dist "$name-symbols"
$symbolsZip = Join-Path $dist "$name-symbols.zip"
foreach ($path in $stage, $zip, $symbolsStage, $symbolsZip) { Remove-QaPath $path }

$files = Get-QaInstallFiles
$win64 = Join-Path $stage "SMG026\Binaries\Win64"
foreach ($file in $files) {
    $dest = Join-Path $win64 $file.Dest
    New-Item -ItemType Directory -Force (Split-Path -Parent $dest) | Out-Null
    Copy-Item $file.Source $dest
}
New-QaZip $stage $zip

$build = Join-Path $QaRoot "build"
New-Item -ItemType Directory -Force $symbolsStage | Out-Null
foreach ($symbol in "src\main.pdb", "src\main.map", "Game__Shipping__Win64\bin\UE4SS.pdb", "Game__Shipping__Win64\bin\dwmapi.pdb") {
    $path = Join-Path $build $symbol
    if (Test-Path $path) { Copy-Item $path $symbolsStage }
    else { Write-Warning "No $symbol in the build; the symbols archive goes without it." }
}
New-QaZip $symbolsStage $symbolsZip
Remove-QaPath $symbolsStage

$size = (Get-Item $zip).Length
Write-Host ""
Write-Host ("{0} files, {1:N1} MB packed:" -f @($files).Count, ($size / 1MB))
foreach ($file in ($files | Sort-Object Dest)) { Write-Host ("  SMG026\Binaries\Win64\" + $file.Dest) }
Write-Host ""
Write-Host "Release archive: $zip"
Write-Host "Unpacked copy:   $stage"
Write-Host "Symbols of this build, for its crash dumps (not for players): $symbolsZip"
Write-Host "PACKAGE_OK"
