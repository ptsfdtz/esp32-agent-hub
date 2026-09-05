$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'agentdeck-background.ps1'
$powershellExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$arguments = '-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "{0}"' -f $scriptPath
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
New-Item -Path $runKey -Force | Out-Null
New-ItemProperty -Path $runKey -Name 'AgentDeckBackground' -Value ('"{0}" {1}' -f $powershellExe, $arguments) -PropertyType String -Force | Out-Null
Start-Process -FilePath $powershellExe -ArgumentList $arguments -WindowStyle Hidden
Write-Output 'Agent Deck background supervisor installed for this Windows user and started.'
