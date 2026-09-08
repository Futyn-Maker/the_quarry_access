<#
.SYNOPSIS
  Launches The Quarry through Steam and waits until the mod reports readiness.
  Detects a crash (process gone, UE4SS fatal error, or the engine's crash dialog)
  and, with -AutoDismissCrash, closes the dialog/process so nothing stays blocked.
.PARAMETER TimeoutSec
  Seconds to wait for "[QuarryAccess] ready" in UE4SS.log.
#>
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\The Quarry",
    [int]$TimeoutSec = 120,
    [switch]$AutoDismissCrash
)

Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class QaWin {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    public static List<string> Titles = new List<string>();
    public static IntPtr Found = IntPtr.Zero;
    public static IntPtr FindContaining(string part) {
        Found = IntPtr.Zero; Titles.Clear();
        EnumWindows((h, l) => {
            if (!IsWindowVisible(h)) return true;
            var sb = new StringBuilder(512); GetWindowText(h, sb, 512);
            var t = sb.ToString();
            if (t.Length > 0) Titles.Add(t);
            if (t.IndexOf(part, StringComparison.OrdinalIgnoreCase) >= 0) { Found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return Found;
    }
}
"@

$win64 = Join-Path $GameDir "SMG026\Binaries\Win64"
$log = Join-Path $win64 "UE4SS.log"
if (Get-Process -Name "TheQuarry-Win64-Shipping" -ErrorAction SilentlyContinue) {
    Write-Host "Game already running."
} else {
    Start-Process "steam://rungameid/1577120"
}
$launchedAt = Get-Date
$deadline = $launchedAt.AddSeconds($TimeoutSec)
$sawProcess = $false
$result = "timeout"
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    $proc = Get-Process -Name "TheQuarry-Win64-Shipping" -ErrorAction SilentlyContinue
    if ($proc) { $sawProcess = $true }
    $crashWnd = [QaWin]::FindContaining("has crashed")
    if ($crashWnd -ne [IntPtr]::Zero) {
        $result = "crash-dialog"
        Write-Host "CRASH: the engine crash dialog is showing."
        if ($AutoDismissCrash) {
            [QaWin]::PostMessage($crashWnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_CLOSE
            Start-Sleep -Seconds 2
            Stop-Process -Name "TheQuarry-Win64-Shipping" -Force -ErrorAction SilentlyContinue
        }
        break
    }
    if ($sawProcess -and -not $proc) { $result = "exited"; Write-Host "CRASH: game process exited."; break }
    # Only trust the log once this launch has rewritten it (UE4SS truncates it at start).
    if ((Test-Path $log) -and ((Get-Item $log).LastWriteTime -gt $launchedAt)) {
        $content = Get-Content $log -Raw -ErrorAction SilentlyContinue
        if ($content -match '\[QuarryAccess\] ready') { $result = "ready"; break }
        if ($content -match 'Fatal Error') { $result = "ue4ss-fatal"; Write-Host "UE4SS fatal error:"; ($content -split "`n") | Select-String 'Fatal' | Select-Object -First 3; break }
    }
}
switch ($result) {
    "ready"   { Write-Host "QuarryAccess ready." }
    "timeout" { Write-Host "Timed out waiting for QuarryAccess ready." }
}
# Last mod log lines help to locate a crash.
$modLog = Join-Path $win64 "Mods\QuarryAccess\QuarryAccess.log"
if (($result -ne "ready") -and (Test-Path $modLog)) {
    Write-Host "--- last mod log lines ---"
    Get-Content $modLog -Tail 8 -Encoding UTF8
}
exit ($(if ($result -eq "ready") { 0 } else { 1 }))
