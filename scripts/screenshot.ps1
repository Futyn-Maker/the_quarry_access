<#
.SYNOPSIS
  Captures the primary screen to a PNG file (for visual cross-checks by the developer).
.PARAMETER Path
  The file to write; by default screenshots\screen-<date>-<time>.png in the repository.
#>
param([string]$Path)
if (-not $Path) {
    $folder = Join-Path (Split-Path -Parent $PSScriptRoot) "screenshots"
    New-Item -ItemType Directory -Force $folder | Out-Null
    $Path = Join-Path $folder ("screen-{0:yyyyMMdd-HHmmss}.png" -f (Get-Date))
}
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
$bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
# The path goes to the pipeline, so a caller can take it: $file = scripts\screenshot.ps1
$Path
