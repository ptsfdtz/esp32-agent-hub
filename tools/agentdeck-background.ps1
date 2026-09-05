$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$bridgeExe = Join-Path $projectRoot 'bridge\target\release\agentdeck-bridge.exe'
$brokerConfig = Join-Path $projectRoot 'bridge\mosquitto.local.conf'
$bridgeConfig = Join-Path $projectRoot 'bridge\config.json'
$brokerExe = if ($env:AGENTDECK_MOSQUITTO) { $env:AGENTDECK_MOSQUITTO } else { 'C:\Program Files\mosquitto\mosquitto.exe' }
$logDir = Join-Path $projectRoot 'bridge\.state'
$mutex = New-Object System.Threading.Mutex($false, 'Local\AgentDeckBackground')
if (-not $mutex.WaitOne(0)) { exit }
try {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    Set-Location -LiteralPath $projectRoot
    while ($true) {
        try {
            $listener = Get-NetTCPConnection -State Listen -LocalPort 1884 -ErrorAction SilentlyContinue
            if (-not $listener) {
                Start-Process -FilePath $brokerExe -ArgumentList ('-c "{0}"' -f $brokerConfig) -WindowStyle Hidden -RedirectStandardOutput "$logDir\mosquitto.stdout.log" -RedirectStandardError "$logDir\mosquitto.stderr.log"
            }
            $running = Get-CimInstance Win32_Process -Filter "Name='agentdeck-bridge.exe'" | Where-Object { $_.ExecutablePath -eq $bridgeExe -and $_.CommandLine -match '\bservice\b' }
            if (-not $running) {
                Start-Process -FilePath $bridgeExe -ArgumentList ('service --config "{0}"' -f $bridgeConfig) -WorkingDirectory $projectRoot -WindowStyle Hidden -RedirectStandardOutput "$logDir\bridge.stdout.log" -RedirectStandardError "$logDir\bridge.stderr.log"
            }
        } catch {
            Add-Content -LiteralPath "$logDir\background.log" -Value ("{0:o} {1}" -f (Get-Date), $_.Exception.Message)
        }
        Start-Sleep -Seconds 5
    }
} finally {
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
