<#
.SYNOPSIS
  Sends keystrokes to the foreground game window using SendInput with scan codes.
.EXAMPLE
  scripts\sendkeys.ps1 -Keys "Enter","Down","Down","F6" -DelayMs 800
.NOTES
  Key names: letters/digits, F1-F24, Enter, Escape, Space, Backspace, Tab, Up, Down, Left, Right,
  Home, End, PageUp, PageDown, and "Ctrl+F9" style combinations.
#>
param(
    [Parameter(Mandatory = $true)][string[]]$Keys,
    [int]$DelayMs = 700,
    [int]$HoldMs = 60,
    [switch]$NoFocus
)

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class QaInput {
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION { [FieldOffset(0)] public KEYBDINPUT ki; [FieldOffset(0)] public long pad1; [FieldOffset(8)] public long pad2; [FieldOffset(16)] public long pad3; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public INPUTUNION u; }
    [DllImport("user32.dll", SetLastError = true)] public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint uCode, uint uMapType);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    const uint KEYEVENTF_SCANCODE = 0x0008; const uint KEYEVENTF_KEYUP = 0x0002; const uint KEYEVENTF_EXTENDEDKEY = 0x0001;
    static bool IsExtended(ushort vk) { return vk == 0x25 || vk == 0x26 || vk == 0x27 || vk == 0x28 || vk == 0x2D || vk == 0x2E || vk == 0x24 || vk == 0x23 || vk == 0x21 || vk == 0x22; }
    public static void Key(ushort vk, bool down) {
        INPUT[] inp = new INPUT[1];
        inp[0].type = 1;
        inp[0].u.ki.wVk = 0;
        inp[0].u.ki.wScan = (ushort)MapVirtualKey(vk, 0);
        inp[0].u.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0u : KEYEVENTF_KEYUP) | (IsExtended(vk) ? KEYEVENTF_EXTENDEDKEY : 0u);
        SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@

$named = @{ enter=0x0D; return=0x0D; escape=0x1B; esc=0x1B; space=0x20; backspace=0x08; tab=0x09; up=0x26; down=0x28; left=0x25; right=0x27; home=0x24; end=0x23; pageup=0x21; pagedown=0x22; insert=0x2D; delete=0x2E; ctrl=0x11; control=0x11; alt=0x12; shift=0x10 }
function VkOf([string]$name) {
    $n = $name.ToLowerInvariant()
    if ($named.ContainsKey($n)) { return [int]$named[$n] }
    if ($n -match '^f(\d+)$') { return 0x70 + [int]$Matches[1] - 1 }
    if ($n.Length -eq 1) { return [int][char]$n.ToUpperInvariant() }
    throw "unknown key $name"
}

if (-not $NoFocus) {
    $proc = Get-Process -Name "TheQuarry-Win64-Shipping" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc -and $proc.MainWindowHandle -ne 0) { [QaInput]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 300 }
}

foreach ($combo in $Keys) {
    $parts = $combo -split '\+'
    $vks = @($parts | ForEach-Object { VkOf $_ })
    foreach ($vk in $vks) { [QaInput]::Key([uint16]$vk, $true); Start-Sleep -Milliseconds 20 }
    Start-Sleep -Milliseconds $HoldMs
    [array]::Reverse($vks)
    foreach ($vk in $vks) { [QaInput]::Key([uint16]$vk, $false); Start-Sleep -Milliseconds 20 }
    Start-Sleep -Milliseconds $DelayMs
}
Write-Host "sent: $($Keys -join ' ')"
