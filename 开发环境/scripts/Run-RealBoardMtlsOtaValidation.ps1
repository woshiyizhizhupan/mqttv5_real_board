param(
    [Alias("Host")]
    [string]$BrokerHost = "192.168.0.110",
    [int]$Port = 8884,
    [string]$CityId = "tjw",
    [string]$PoleId = "pole001",
    [string]$DeviceName = "GM400",
    [string]$DeviceId,
    [Parameter(Mandatory = $true)]
    [string]$Image,
    [string]$CertDir = "D:\Tools\emqx-5.2.0\etc\certs\local_mtls",
    [int]$ChunkSize = 1024,
    [int]$Timeout = 60,
    [double]$RequestInterval = 0.2,
    [int]$Retries = 2,
    [string]$OutputDir,
    [switch]$SkipReadback
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptRoot "..\..")).Path
$HostToolRoot = Join-Path $RepoRoot "开发环境\host_mqtt_tester"
$Python = Join-Path $HostToolRoot ".venv\Scripts\python.exe"

if (-not (Test-Path -LiteralPath $Python)) {
    python -m venv (Join-Path $HostToolRoot ".venv")
}
& $Python -m pip install -q -r (Join-Path $HostToolRoot "requirements.txt")

if (-not (Test-Path -LiteralPath $Image)) {
    throw "OTA image not found: $Image"
}
if (-not $OutputDir) {
    $OutputDir = Join-Path $RepoRoot ("开发环境\test_runs\mtls_ota_test_{0}" -f (Get-Date -Format "yyyyMMdd-HHmmss"))
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$baseByName = "city/$CityId/pole/$PoleId/device/$DeviceName"
$otaDevice = if ($DeviceId) { $DeviceId } else { $DeviceName }
$otaTopic = "city/$CityId/pole/$PoleId/device/$otaDevice/ota"
$responseTopic = "$baseByName/event"
$ca = Join-Path $CertDir "ca.pem"
$cert = Join-Path $CertDir "client.pem"
$key = Join-Path $CertDir "client.key"

$mqttArgs = @(
    "-m", "mqtt_tester.sim_board_server",
    "--host", $BrokerHost,
    "--port", $Port,
    "--tls",
    "--ca", $ca,
    "--cert", $cert,
    "--key", $key,
    "--city-id", $CityId,
    "--pole-id", $PoleId,
    "--device-name", $DeviceName,
    "ota-send",
    "--ota-topic", $otaTopic,
    "--response-topic", $responseTopic,
    "--image", $Image,
    "--chunk-size", $ChunkSize,
    "--timeout", $Timeout,
    "--request-interval", $RequestInterval,
    "--retries", $Retries,
    "--log-dir", $OutputDir
)
if ($DeviceId) {
    $mqttArgs = @(
        "-m", "mqtt_tester.sim_board_server",
        "--host", $BrokerHost,
        "--port", $Port,
        "--tls",
        "--ca", $ca,
        "--cert", $cert,
        "--key", $key,
        "--city-id", $CityId,
        "--pole-id", $PoleId,
        "--device-name", $DeviceName,
        "--device-id", $DeviceId,
        "ota-send",
        "--ota-topic", $otaTopic,
        "--response-topic", $responseTopic,
        "--image", $Image,
        "--chunk-size", $ChunkSize,
        "--timeout", $Timeout,
        "--request-interval", $RequestInterval,
        "--retries", $Retries,
        "--log-dir", $OutputDir
    )
}

Push-Location $HostToolRoot
try {
    & $Python @mqttArgs
    if ($LASTEXITCODE -ne 0) {
        throw "MQTT OTA send failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$imageBytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Image).Path)
$readbackPath = Join-Path $OutputDir "app2_readback.bin"
$compareOk = $false
if (-not $SkipReadback) {
    & (Join-Path $ScriptRoot "Read-RealBoardApp2.ps1") -Length $imageBytes.Length -Output $readbackPath
    if ($LASTEXITCODE -ne 0) {
        throw "APP2 readback script failed with exit code $LASTEXITCODE"
    }

    $actualBytes = [System.IO.File]::ReadAllBytes($readbackPath)
    $diff = Compare-Object -ReferenceObject $imageBytes -DifferenceObject $actualBytes -SyncWindow 0
    if ($imageBytes.Length -ne $actualBytes.Length -or $diff) {
        for ($i = 0; $i -lt [Math]::Min($imageBytes.Length, $actualBytes.Length); $i++) {
            if ($imageBytes[$i] -ne $actualBytes[$i]) {
                throw ("APP2 compare failed at offset 0x{0:X}: expected=0x{1:X2}, actual=0x{2:X2}" -f $i, $imageBytes[$i], $actualBytes[$i])
            }
        }
        throw "APP2 compare failed: length mismatch"
    }
    $compareOk = $true
}

$summary = [ordered]@{
    test = "mtls_ota_test"
    host = $BrokerHost
    port = $Port
    ota_topic = $otaTopic
    response_topic = $responseTopic
    image = (Resolve-Path -LiteralPath $Image).Path
    image_len = $imageBytes.Length
    timeout_s = $Timeout
    request_interval_s = $RequestInterval
    retries = $Retries
    cert_dir = $CertDir
    readback = if ($SkipReadback) { $null } else { $readbackPath }
    flash_compare_ok = $compareOk
}
$summaryPath = Join-Path $OutputDir "mtls_ota_test_summary.json"
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host ("MTLS_OTA_TEST_SUMMARY={0}" -f $summaryPath)
