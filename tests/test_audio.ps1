Remove-Item lumn_debug.log -ErrorAction SilentlyContinue

$player = New-Object System.Media.SoundPlayer('scratch\click120.wav')
$player.Play()

Start-Sleep -Milliseconds 200

$proc = Start-Process -FilePath 'build\LumnOverlay.exe' -ArgumentList @('--test-exit-after', '3.5') -Wait -PassThru

$player.Stop()

Write-Host "Process Exit Code:" $proc.ExitCode
Write-Host "--- LOG OUTPUT ---"
Get-Content lumn_debug.log

