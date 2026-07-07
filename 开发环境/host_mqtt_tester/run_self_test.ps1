param(
    [string]$HostName = "127.0.0.1",
    [int]$Port = 1883,
    [string]$ProductKey = "local_pk",
    [string]$DeviceName = "hc32f460_dev"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Venv = Join-Path $Root ".venv"
$Python = Join-Path $Venv "Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    python -m venv $Venv
}

& $Python -m pip install -q -r (Join-Path $Root "requirements.txt")
Push-Location $Root
try {
    & $Python -m mqtt_tester.mqtt_loopback --host $HostName --port $Port --product-key $ProductKey --device-name $DeviceName
    $ExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}
exit $ExitCode
