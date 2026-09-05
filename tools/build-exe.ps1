param([string]$MosquittoDirectory = 'C:\Program Files\mosquitto')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
cargo build --release --locked --manifest-path bridge/Cargo.toml
if ($LASTEXITCODE) { throw 'Bridge build failed' }
$payload = Join-Path $root ('build/exe-payload-' + [guid]::NewGuid().ToString('N'))
foreach ($dir in @('bridge/target/release', 'broker', 'tools')) { New-Item -ItemType Directory -Path (Join-Path $payload $dir) -Force | Out-Null }
Copy-Item bridge/target/release/agentdeck-bridge.exe "$payload/bridge/target/release/"
Copy-Item bridge/config.example.json "$payload/bridge/"
@('listener 1884 0.0.0.0', 'allow_anonymous true', 'persistence false', 'log_type error', 'log_type warning', 'log_type notice', 'connection_messages true') | Set-Content "$payload/bridge/mosquitto.local.conf" -Encoding ASCII
Copy-Item tools/agentdeck-background.ps1,tools/desktop-start.ps1 "$payload/tools/"
Copy-Item "$MosquittoDirectory/mosquitto.exe" "$payload/broker/"
Get-ChildItem $MosquittoDirectory -File | Where-Object { $_.Extension -eq '.dll' -or $_.Name -match '^(LICENSE|NOTICE|COPYING|epl|edl|README)' } | Copy-Item -Destination "$payload/broker/"
$env:AGENTDECK_PAYLOAD = $payload
cargo build --release --manifest-path launcher/Cargo.toml
if ($LASTEXITCODE) { throw 'Launcher build failed' }
New-Item -ItemType Directory -Path build/dist -Force | Out-Null
Copy-Item launcher/target/release/agentdeck.exe build/dist/AgentDeck.exe -Force
Get-FileHash build/dist/AgentDeck.exe -Algorithm SHA256 | Format-List
