param([string]$SourceDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$configPath = Join-Path $root 'bridge/config.json'
if (-not (Test-Path -LiteralPath $configPath)) {
    $config = Get-Content (Join-Path $root 'bridge/config.example.json') -Raw | ConvertFrom-Json
    $config.port = 1884
    # Import an existing project configuration only at runtime, never into the release.
    $candidate = $SourceDirectory
    for ($i = 0; $i -lt 4 -and $candidate; $i++) {
        $existing = Join-Path $candidate 'bridge/config.json'
        if (Test-Path -LiteralPath $existing) {
            $config = Get-Content -LiteralPath $existing -Raw | ConvertFrom-Json
            $config.state_dir = [IO.Path]::GetFullPath((Join-Path (Split-Path $existing) $config.state_dir))
            break
        }
        $candidate = Split-Path $candidate -Parent
    }
    $config | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $configPath -Encoding UTF8
}
# Resolve the real executable, including VS Code extension upgrades.
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if ($config.codex_usage_command -and -not (Test-Path $config.codex_usage_command[0])) {
    $codex = Get-Command codex.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
    if (-not $codex) {
        $codex = Get-ChildItem "$env:USERPROFILE/.vscode/extensions/openai.chatgpt-*/bin/windows-x86_64/codex.exe" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    if ($codex) { $config.codex_usage_command = @($codex, 'app-server') }
}
$json = $config | ConvertTo-Json -Depth 20
[IO.File]::WriteAllText($configPath, $json, (New-Object Text.UTF8Encoding($false)))
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
New-Item -Path $runKey -Force | Out-Null
New-ItemProperty -Path $runKey -Name AgentDeckBackground -Value ('"{0}"' -f (Join-Path $root 'AgentDeck.exe')) -PropertyType String -Force | Out-Null
$env:AGENTDECK_MOSQUITTO = Join-Path $root 'broker/mosquitto.exe'
& (Join-Path $PSScriptRoot 'agentdeck-background.ps1')
