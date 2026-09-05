$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Start-Process -FilePath (Join-Path $root 'build/dist/AgentDeck.exe') -WindowStyle Hidden
$deadline = (Get-Date).AddSeconds(30)
$installedBridge = Join-Path $env:LOCALAPPDATA 'AgentDeck/bridge/target/release/agentdeck-bridge.exe'
do {
    Start-Sleep -Seconds 1
    $bridge = Get-Process agentdeck-bridge -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $installedBridge }
    $listener = Get-NetTCPConnection -State Listen -LocalPort 1884 -ErrorAction SilentlyContinue
} until (($bridge -and $listener) -or (Get-Date) -gt $deadline)
if (-not $bridge -or -not $listener) { throw 'Packaged EXE did not start broker and bridge' }
& 'C:\Program Files\mosquitto\mosquitto_sub.exe' -h 127.0.0.1 -p 1884 -t pc/status -C 1 -W 15
if ($LASTEXITCODE) { throw 'Packaged Bridge did not publish PC telemetry' }
$oldId = @($bridge)[0].Id
Stop-Process -Id $oldId
Start-Sleep -Seconds 8
$recovered = Get-Process agentdeck-bridge -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $installedBridge -and $_.Id -ne $oldId }
if (-not $recovered) { throw 'Bridge recovery failed' }
Write-Output 'EXE extraction, telemetry and process recovery passed.'
