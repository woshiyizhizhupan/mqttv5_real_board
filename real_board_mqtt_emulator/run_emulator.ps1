param(
    [string]$HostName = "39.103.154.108",
    [int]$Port = 1883,
    [string]$DeviceId = "GM400-452089",
    [string]$CityId = "tjw",
    [string]$PoleId = "pole001",
    [int]$Qos = 2,
    [double]$TelemetryInterval = 5.0,
    [double]$MaxRuntime = 0,
    [string]$LogDir = "",
    [string]$Username = "",
    [string]$Password = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$VenvPython = Join-Path $ProjectRoot ".venv\Scripts\python.exe"
if (Test-Path -LiteralPath $VenvPython) {
    $Python = $VenvPython
} else {
    $Python = "python"
}

$ArgsList = @(
    "-m", "real_board_emulator.cli",
    "--host", $HostName,
    "--port", "$Port",
    "--device-id", $DeviceId,
    "--city-id", $CityId,
    "--pole-id", $PoleId,
    "--qos", "$Qos"
)

if ($Username -ne "") {
    $ArgsList += @("--username", $Username)
}
if ($Password -ne "") {
    $ArgsList += @("--password", $Password)
}

$ArgsList += @("run", "--telemetry-interval", "$TelemetryInterval")

if ($MaxRuntime -gt 0) {
    $ArgsList += @("--max-runtime", "$MaxRuntime")
}
if ($LogDir -ne "") {
    $ArgsList += @("--log-dir", $LogDir)
}

Push-Location $ProjectRoot
try {
    & $Python @ArgsList
    exit $LASTEXITCODE
} finally {
    Pop-Location
}

