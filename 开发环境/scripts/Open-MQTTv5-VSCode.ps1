param(
    [string]$CodePath = "D:\Tools\VSCode\bin\code.cmd"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Workspace = Join-Path $Root "mqttv5(1)\eide\mqttv5.code-workspace"

if (-not (Test-Path -LiteralPath $CodePath)) {
    $cmd = Get-Command "code.cmd" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) {
        $CodePath = $cmd.Source
    }
}

if (-not (Test-Path -LiteralPath $CodePath)) {
    throw "VS Code command not found. Expected: $CodePath"
}

if (-not (Test-Path -LiteralPath $Workspace)) {
    throw "Workspace file not found: $Workspace"
}

& $CodePath $Workspace
