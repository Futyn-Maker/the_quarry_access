<#
.SYNOPSIS
  Verifies that every mod\lang\*.ini contains exactly the keys of en_US.ini, and the same for
  the tarot tables in mod\lang\tarot.
#>
$root = Split-Path -Parent $PSScriptRoot
$langDir = Join-Path $root "mod\lang"
function Keys($file) {
    $keys = @()
    foreach ($line in Get-Content $file -Encoding UTF8) {
        $t = $line.Trim()
        if ($t -eq '' -or $t.StartsWith(';') -or $t.StartsWith('[')) { continue }
        $eq = $t.IndexOf('=')
        if ($eq -gt 0) { $keys += $t.Substring(0, $eq).Trim().ToLowerInvariant() }
    }
    return $keys
}
$expected = @("da_DK","de_DE","en_US","es_ES","es_MX","fi_FI","fr_FR","it_IT","ja_JP","ko_KR","nl_NL","nn_NO","pl_PL","pt_BR","pt_PT","ru_RU","sv_SE","tr_TR","zh_CHS","zh_CHT")
$failed = $false
$total = 0
# The main table and the tarot table of each language must both carry every key of the English one.
foreach ($dir in @($langDir, (Join-Path $langDir "tarot"))) {
    $master = Keys (Join-Path $dir "en_US.ini")
    $total += $master.Count
    foreach ($code in $expected) {
        $file = Join-Path $dir "$code.ini"
        $label = if ($dir -eq $langDir) { "$code" } else { "tarot\$code" }
        if (-not (Test-Path $file)) { Write-Host "MISSING FILE: $label.ini"; $failed = $true; continue }
        $keys = Keys $file
        $missing = $master | Where-Object { $keys -notcontains $_ }
        $extra = $keys | Where-Object { $master -notcontains $_ }
        if ($missing.Count -gt 0) { Write-Host "$label missing: $($missing -join ', ')"; $failed = $true }
        if ($extra.Count -gt 0) { Write-Host "$label extra: $($extra -join ', ')"; $failed = $true }
    }
}
if ($failed) { Write-Host "LANG CHECK FAILED"; exit 1 } else { Write-Host "LANG CHECK OK ($total keys, $($expected.Count) languages)"; exit 0 }
