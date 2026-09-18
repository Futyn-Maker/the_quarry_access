# Shared by the PowerShell scripts: where the repository, its build and the game are, and which
# files make up an installed copy of the mod. Dot-source it:
#   . (Join-Path $PSScriptRoot "common.ps1")
# Runs in Windows PowerShell 5.1 and in PowerShell 7.

$QaRoot = Split-Path -Parent $PSScriptRoot
$QaSteamAppId = 1577120
$QaExeName = "TheQuarry-Win64-Shipping"

# The mod's version: the first line of the VERSION file.
function Get-QaVersion {
    $version = (Get-Content (Join-Path $QaRoot "VERSION") -TotalCount 1).Trim()
    if (-not $version) { throw "The VERSION file is empty." }
    return $version
}

# The RE-UE4SS tree the last build used (scripts\build.cmd notes it), else the one it would use.
function Get-QaUE4SSSourceDir {
    $note = Join-Path $QaRoot "build\ue4ss-source.txt"
    if (Test-Path $note) {
        $dir = (Get-Content $note -TotalCount 1).Trim()
        if ($dir) { return $dir.Replace('/', '\') }
    }
    if ($env:QA_UE4SS_SOURCE_DIR) { return $env:QA_UE4SS_SOURCE_DIR }
    return (Join-Path $QaRoot "third_party\RE-UE4SS")
}

# The game's folder in the Steam library that has it, from Steam's own records; $null when Steam
# or the game is not installed.
function Find-QuarrySteamDir {
    $steam = $null
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        $values = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if (-not $values) { continue }
        if ($values.PSObject.Properties['SteamPath']) { $steam = $values.SteamPath }
        elseif ($values.PSObject.Properties['InstallPath']) { $steam = $values.InstallPath }
        if ($steam) { break }
    }
    if (-not $steam) { return $null }
    $libraries = @($steam)
    $folders = Join-Path $steam "steamapps\libraryfolders.vdf"
    if (Test-Path $folders) {
        foreach ($match in [regex]::Matches((Get-Content $folders -Raw), '"path"\s+"([^"]+)"')) {
            $libraries += $match.Groups[1].Value.Replace('\\', '\')
        }
    }
    foreach ($library in ($libraries | Select-Object -Unique)) {
        $manifest = Join-Path $library "steamapps\appmanifest_$QaSteamAppId.acf"
        if (-not (Test-Path $manifest)) { continue }
        $installDir = [regex]::Match((Get-Content $manifest -Raw), '"installdir"\s+"([^"]+)"').Groups[1].Value
        if (-not $installDir) { continue }
        $dir = Join-Path $library "steamapps\common\$installDir"
        if (Test-Path $dir) { return (Resolve-Path $dir).Path }
    }
    return $null
}

# The game's folder (the one with TheQuarry.exe): the given one, else QA_GAME_DIR, else the
# Steam library that has the game.
function Resolve-QuarryGameDir([string]$GameDir) {
    $from = "-GameDir"
    if (-not $GameDir) { $GameDir = $env:QA_GAME_DIR; $from = "QA_GAME_DIR" }
    if (-not $GameDir) { $GameDir = Find-QuarrySteamDir; $from = "Steam" }
    if (-not $GameDir) {
        throw "The Quarry was not found in Steam's libraries. Pass -GameDir with the game's folder (the one with TheQuarry.exe), or set QA_GAME_DIR to it."
    }
    $exe = Join-Path $GameDir "SMG026\Binaries\Win64\$QaExeName.exe"
    if (-not (Test-Path $exe)) { throw "$GameDir (from $from) is not The Quarry's folder: $exe is missing." }
    return (Resolve-Path $GameDir).Path
}

function Get-QuarryWin64Dir([string]$GameDir) {
    return (Join-Path $GameDir "SMG026\Binaries\Win64")
}

# Where UE4SS lives in the game: Binaries\Win64\ue4ss, as UE4SS's releases and this mod's archive
# install it, or Binaries\Win64 itself for an older install. A game without UE4SS gets the
# ue4ss folder.
function Get-QuarryUE4SSDir([string]$Win64) {
    $sub = Join-Path $Win64 "ue4ss"
    if (Test-Path (Join-Path $sub "UE4SS.dll")) { return $sub }
    if (Test-Path (Join-Path $Win64 "UE4SS.dll")) { return $Win64 }
    return $sub
}

# Every file of an installed copy of the mod: its source, its place under the game's
# Binaries\Win64 folder, UE4SS's files under "ue4ss\", and its kind. Program files are replaced by
# a newer build; Settings files are the player's once installed. Tolk sits next to the game's
# exe, where the mod loads it from.
function Get-QaInstallFiles {
    $bin = Join-Path $QaRoot "build\Game__Shipping__Win64\bin"
    $ue4ss = Get-QaUE4SSSourceDir
    $modDest = "ue4ss\Mods\QuarryAccess"
    $files = New-Object System.Collections.Generic.List[object]
    $add = {
        param($Source, $Dest, $Kind)
        $files.Add([pscustomobject]@{ Source = $Source; Dest = $Dest; Kind = $Kind })
    }

    & $add (Join-Path $bin "dwmapi.dll") "dwmapi.dll" Program
    foreach ($name in 'Tolk.dll', 'nvdaControllerClient64.dll', 'SAAPI64.dll') {
        & $add (Join-Path $QaRoot "third_party\tolk\$name") $name Program
    }
    & $add (Join-Path $bin "UE4SS.dll") "ue4ss\UE4SS.dll" Program
    # UE4SS's configuration for this game: its settings, the vtable layout and the signature
    # of StaticConstructObject.
    $config = Join-Path $ue4ss "assets\CustomGameConfigs\The Quarry"
    if (-not (Test-Path $config)) { throw "UE4SS's configuration for The Quarry is missing: $config" }
    $config = (Resolve-Path $config).Path
    foreach ($file in Get-ChildItem $config -Recurse -File) {
        & $add $file.FullName ("ue4ss\" + $file.FullName.Substring($config.Length + 1)) Settings
    }

    & $add (Join-Path $QaRoot "build\src\main.dll") "$modDest\dlls\main.dll" Program
    $mod = (Resolve-Path (Join-Path $QaRoot "mod")).Path
    foreach ($file in Get-ChildItem $mod -Recurse -File) {
        $relative = $file.FullName.Substring($mod.Length + 1)
        $kind = "Program"
        if ($relative -eq "QuarryAccess.ini") { $kind = "Settings" }
        & $add $file.FullName "$modDest\$relative" $kind
    }

    & $add (Join-Path $QaRoot "LICENSE") "$modDest\licenses\QuarryAccess.txt" Program
    & $add (Join-Path $ue4ss "LICENSE") "$modDest\licenses\UE4SS.txt" Program
    & $add (Join-Path $QaRoot "third_party\tolk\LICENSE.txt") "$modDest\licenses\Tolk.txt" Program
    & $add (Join-Path $QaRoot "third_party\tolk\LICENSE-NVDA.txt") "$modDest\licenses\NVDAControllerClient.txt" Program

    $missing = @($files | Where-Object { -not (Test-Path $_.Source) } | ForEach-Object { $_.Source })
    if ($missing.Count -gt 0) {
        throw "Missing (run scripts\build.cmd first): $($missing -join ', ')"
    }
    return $files
}
