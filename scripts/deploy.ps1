<#
.SYNOPSIS
  Deploys the built mod into The Quarry's UE4SS Mods folder.
.DESCRIPTION
  Copies build\src\main.dll to Mods\QuarryAccess\dlls\main.dll, the mod\ folder
  (enabled.txt, QuarryAccess.ini, lang\*) to Mods\QuarryAccess\, makes sure
  mods.txt enables QuarryAccess (and disables the old QuarryTestMod), and copies
  the Tolk runtime next to the game exe if it is missing.
  Refuses to run while the game is running (main.dll would be locked).
.PARAMETER GameDir
  The Quarry install directory.
.PARAMETER ResetConfig
  Overwrite an existing QuarryAccess.ini with the repository default.
#>
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\The Quarry",
    [switch]$ResetConfig
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$win64 = Join-Path $GameDir "SMG026\Binaries\Win64"
$modDir = Join-Path $win64 "Mods\QuarryAccess"

if (Get-Process -Name "TheQuarry-Win64-Shipping" -ErrorAction SilentlyContinue) {
    throw "The Quarry is running. Close it before deploying (main.dll is locked while the game runs)."
}
if (-not (Test-Path (Join-Path $win64 "TheQuarry-Win64-Shipping.exe"))) {
    throw "Game exe not found under $win64"
}
$dll = Join-Path $root "build\src\main.dll"
if (-not (Test-Path $dll)) { throw "Build output not found: $dll (run scripts\build.cmd first)" }

New-Item -ItemType Directory -Force (Join-Path $modDir "dlls") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $modDir "lang") | Out-Null
Copy-Item $dll (Join-Path $modDir "dlls\main.dll") -Force
Copy-Item (Join-Path $root "mod\enabled.txt") (Join-Path $modDir "enabled.txt") -Force
Copy-Item (Join-Path $root "mod\lang\*.ini") (Join-Path $modDir "lang") -Force
$ini = Join-Path $modDir "QuarryAccess.ini"
if ($ResetConfig -or -not (Test-Path $ini)) {
    Copy-Item (Join-Path $root "mod\QuarryAccess.ini") $ini -Force
}

# mods.txt: enable QuarryAccess, disable the toolchain test mod.
$modsTxt = Join-Path $win64 "Mods\mods.txt"
if (Test-Path $modsTxt) {
    $lines = Get-Content $modsTxt
    $lines = $lines | ForEach-Object {
        if ($_ -match '^\s*QuarryTestMod\s*:') { 'QuarryTestMod : 0' }
        elseif ($_ -match '^\s*QuarryProbe\s*:') { 'QuarryProbe : 0' }
        else { $_ }
    }
    if (-not ($lines | Where-Object { $_ -match '^\s*QuarryAccess\s*:' })) {
        $idx = [array]::IndexOf($lines, ($lines | Where-Object { $_ -match 'Built-in keybinds' } | Select-Object -First 1))
        if ($idx -ge 0) { $lines = $lines[0..($idx-1)] + @('QuarryAccess : 1', '') + $lines[$idx..($lines.Count-1)] }
        else { $lines += 'QuarryAccess : 1' }
    } else {
        $lines = $lines | ForEach-Object { if ($_ -match '^\s*QuarryAccess\s*:') { 'QuarryAccess : 1' } else { $_ } }
    }
    Set-Content $modsTxt $lines
}

# Tolk runtime next to the exe.
foreach ($f in 'Tolk.dll', 'nvdaControllerClient64.dll', 'SAAPI64.dll') {
    $dst = Join-Path $win64 $f
    if (-not (Test-Path $dst)) { Copy-Item (Join-Path $root "third_party\tolk\$f") $dst }
}

Write-Host "Deployed to $modDir"
Get-ChildItem $modDir -Recurse -File | Select-Object FullName, Length, LastWriteTime | Format-Table -AutoSize
