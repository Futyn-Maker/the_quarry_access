<#
.SYNOPSIS
  Fetches the Prism speech library the mod is built and shipped with.
.DESCRIPTION
  Downloads the Windows x64 release of Prism (https://github.com/ethindp/prism) that
  third_party\prism.version names and lays out third_party\prism, which is not part of the
  repository:
    include\prism.h, include\prism_version.h  the API the mod compiles against
    bin\prism.dll                             the library, shipped beside the mod's main.dll
    licenses\                                 Prism's licence and those of the code it bundles
  A run that finds the wanted version already there does nothing. scripts\build.cmd runs this
  by itself whenever what is on disk is not what the repository pins.
.PARAMETER Version
  A release tag to fetch instead of the pinned one, such as v0.18.2. The pin stays as it is.
.PARAMETER Force
  Fetches again even when the wanted version is already there.
.EXAMPLE
  scripts\fetch-prism.ps1
#>
param(
    [string]$Version,
    [switch]$Force
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

if (-not $Version) { $Version = Get-QaPrismVersion }
$dir = Join-Path $QaRoot "third_party\prism"
$stamp = Join-Path $dir "VERSION"

$have = ""
if (Test-Path $stamp) { $have = (Get-Content $stamp -TotalCount 1).Trim() }
$complete = (Test-Path (Join-Path $dir "include\prism.h")) -and (Test-Path (Join-Path $dir "bin\prism.dll"))
if ($have -eq $Version -and $complete -and -not $Force) {
    Write-Host "Prism $Version is already in third_party\prism."
    Write-Host "FETCH_PRISM_OK"
    exit 0
}

# The release holds both linkages and both configurations; the mod takes the release build of
# the shared library, the header that goes with it and the licences.
$wanted = [ordered]@{
    "include/prism.h"               = "include\prism.h"
    "include/prism_version.h"       = "include\prism_version.h"
    "dynamic/release/bin/prism.dll" = "bin\prism.dll"
    "NOTICE"                        = "licenses\NOTICE.txt"
}
$url = "https://github.com/ethindp/prism/releases/download/$Version/prism-windows-x64.zip"
$archive = Join-Path ([System.IO.Path]::GetTempPath()) ("prism-" + [guid]::NewGuid().ToString("N") + ".zip")

Write-Host "Fetching Prism $Version from $url"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
# Windows PowerShell draws a progress bar for every block it reads, which makes the download
# many times slower than the connection.
$progress = $ProgressPreference
$ProgressPreference = "SilentlyContinue"
try {
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
}
catch {
    throw "Prism $Version could not be downloaded from $url : $($_.Exception.Message)"
}
finally {
    $ProgressPreference = $progress
}

try {
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Force $dir | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($entry in $zip.Entries) {
            if (-not $entry.Name) { continue }
            $dest = $null
            if ($wanted.Contains($entry.FullName)) { $dest = $wanted[$entry.FullName] }
            elseif ($entry.FullName.StartsWith("LICENSES/")) {
                $dest = "licenses\" + $entry.FullName.Substring(9).Replace("/", "\")
            }
            if (-not $dest) { continue }
            $path = Join-Path $dir $dest
            New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path, $true)
        }
    }
    finally {
        $zip.Dispose()
    }
}
finally {
    Remove-Item $archive -Force -ErrorAction SilentlyContinue
}

foreach ($name in $wanted.Values) {
    if (-not (Test-Path (Join-Path $dir $name))) {
        throw "The Prism $Version release has no $name. Check third_party\prism.version."
    }
}
Set-Content -Path $stamp -Value $Version -Encoding ASCII

$size = (Get-ChildItem $dir -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ("Prism {0} in third_party\prism ({1:N1} MB)" -f $Version, ($size / 1MB))
Write-Host "FETCH_PRISM_OK"
