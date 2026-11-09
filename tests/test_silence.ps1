Remove-Item lumn_debug.log -ErrorAction SilentlyContinue

$proc = Start-Process -FilePath 'build\LumnOverlay.exe' -ArgumentList @('--test-exit-after', '2.5') -Wait -PassThru

Write-Host "Process Exit Code:" $proc.ExitCode
Write-Host "--- SILENCE TEST LOG OUTPUT ---"
Get-Content lumn_debug.log

