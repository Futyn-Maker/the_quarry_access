<#
.SYNOPSIS
  Verifies that every mod\lang\*.ini contains exactly the keys of en_US.ini.
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
$master = Keys (Join-Path $langDir "en_US.ini")
$expected = @("da_DK","de_DE","en_US","es_ES","es_MX","fi_FI","fr_FR","it_IT","ja_JP","ko_KR","nl_NL","nn_NO","pl_PL","pt_BR","pt_PT","ru_RU","sv_SE","tr_TR","zh_CHS","zh_CHT")
$failed = $false
foreach ($code in $expected) {
    $file = Join-Path $langDir "$code.ini"
    if (-not (Test-Path $file)) { Write-Host "MISSING FILE: $code.ini"; $failed = $true; continue }
    $keys = Keys $file
    $missing = $master | Where-Object { $keys -notcontains $_ }
    $extra = $keys | Where-Object { $master -notcontains $_ }
    if ($missing.Count -gt 0) { Write-Host "$code missing: $($missing -join ', ')"; $failed = $true }
    if ($extra.Count -gt 0) { Write-Host "$code extra: $($extra -join ', ')"; $failed = $true }
}
if ($failed) { Write-Host "LANG CHECK FAILED"; exit 1 } else { Write-Host "LANG CHECK OK ($($master.Count) keys, $($expected.Count) languages)"; exit 0 }
