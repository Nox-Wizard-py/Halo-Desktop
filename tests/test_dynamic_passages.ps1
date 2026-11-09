Remove-Item lumn_debug.log -ErrorAction SilentlyContinue

$player = New-Object System.Media.SoundPlayer('scratch\dynamic_track.wav')
$player.Play()

$currDir = (Get-Location).Path
$proc = Start-Process -FilePath 'build\LumnOverlay.exe' -ArgumentList @('--test-exit-after', '7.0') -PassThru -WorkingDirectory $currDir -Wait

$player.Stop()

Write-Host "Process Exit Code:" $proc.ExitCode
Write-Host "--- LOG OUTPUT ---"
Get-Content lumn_debug.log

