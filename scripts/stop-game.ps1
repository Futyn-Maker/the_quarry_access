# Stops The Quarry.
Stop-Process -Name "TheQuarry-Win64-Shipping" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "TheQuarry" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
if (Get-Process -Name "TheQuarry-Win64-Shipping" -ErrorAction SilentlyContinue) { Write-Host "still running" } else { Write-Host "stopped" }
