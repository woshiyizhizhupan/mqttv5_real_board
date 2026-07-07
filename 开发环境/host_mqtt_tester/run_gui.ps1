param()

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$PackageExe = Join-Path $Root "emqx_gui_client_offline\mqtt_host_gui\mqtt_host_gui.exe"
$BuildExe = Join-Path $Root "dist\mqtt_host_gui\mqtt_host_gui.exe"
if (Test-Path -LiteralPath $PackageExe) {
    & $PackageExe
    Exit $LASTEXITCODE
}
if (Test-Path -LiteralPath $BuildExe) {
    & $BuildExe
    Exit $LASTEXITCODE
}

$Venv = Join-Path $Root ".venv"
$Python = Join-Path $Venv "Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    python -m venv $Venv
}

& $Python -m pip install -q -r (Join-Path $Root "requirements.txt")
& $Python (Join-Path $Root "mqtt_host_gui.py")
