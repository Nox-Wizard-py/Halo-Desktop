Remove-Item lumn_debug.log -ErrorAction SilentlyContinue

$player = New-Object System.Media.SoundPlayer('scratch\click120.wav')
$player.Play()

$currDir = (Get-Location).Path
$proc = Start-Process -FilePath 'build\LumnOverlay.exe' -ArgumentList @('--test-exit-after', '4.5') -PassThru -WorkingDirectory $currDir

Start-Sleep -Milliseconds 1800
$player.Stop()
Write-Host "Audio paused after 1.8 seconds."

$proc.WaitForExit()

Write-Host "Process Exit Code:" $proc.ExitCode
Write-Host "--- PAUSE TRANSITION LOG OUTPUT ---"
Get-Content lumn_debug.log
