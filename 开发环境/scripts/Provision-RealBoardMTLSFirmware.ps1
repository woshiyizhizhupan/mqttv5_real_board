param(
    [string]$BoardIp,
    [Parameter(Mandatory = $true)]
    [string]$ServerIp,
    [string[]]$ServerDns = @("localhost"),
    [string]$DeviceId,
    [int]$MqttPort = 8884,
    [string]$Username = "",
    [string]$Password = "",
    [string]$CityId = "tjw",
    [string]$PoleId = "pole001",
    [uint32]$ConfigVersion = [uint32](Get-Date -Format "MMddHHmm"),
    [string]$OutputDir,
    [string]$Gcc = "D:\Tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-gcc.exe",
    [int]$PostFlashConfigRetries = 10,
    [int]$PostFlashConfigRetryDelaySeconds = 2,
    [switch]$SkipBoardProbe,
    [switch]$SkipFlash,
    [switch]$SkipPostFlashConfig,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptRoot "..\..")).Path
$HostToolRoot = Join-Path $RepoRoot "开发环境\host_mqtt_tester"
$HostConfigProbeProject = Join-Path $RepoRoot "开发环境\host_tool_config_probe\host_tool_config_probe.csproj"
$HostConfigProbeOut = Join-Path $RepoRoot "开发环境\generated\host_config_probe_build"
$CertGenerator = Join-Path $RepoRoot "开发环境\emqx_local\generate_local_mtls_certs.py"
$BuildScript = Join-Path $ScriptRoot "Build-RealBoard-Firmware.ps1"
$FlashScript = Join-Path $ScriptRoot "Flash-MQTTv5-STLink.ps1"
$FirmwareHex = Join-Path $RepoRoot "mqttv5_real_board\eide\build\LocalEMQX\mqttv5_real_board.hex"

function Assert-IpAddress {
    param(
        [string]$Name,
        [string]$Value
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "$Name is required."
    }
    [System.Net.IPAddress]::Parse($Value) | Out-Null
}

function ConvertTo-CStringDefine {
    param(
        [string]$Name,
        [string]$Value
    )

    $escaped = $Value.Replace("\", "\\").Replace('"', '\"')
    return ('{0}="{1}"' -f $Name, $escaped)
}

function Resolve-ToolPython {
    $venv = Join-Path $HostToolRoot ".venv"
    $python = Join-Path $venv "Scripts\python.exe"
    if (-not (Test-Path -LiteralPath $python)) {
        python -m venv $venv
    }
    if (-not (Test-Path -LiteralPath $python)) {
        throw "Python venv was not created: $python"
    }

    & $python -m pip install -q -r (Join-Path $HostToolRoot "requirements.txt")
    if ($LASTEXITCODE -ne 0) {
        throw "pip install requirements failed with exit code $LASTEXITCODE"
    }
    & $python -m pip install -q cryptography==46.0.3
    if ($LASTEXITCODE -ne 0) {
        throw "pip install cryptography failed with exit code $LASTEXITCODE"
    }
    return $python
}

function Resolve-HostConfigProbe {
    dotnet build $HostConfigProbeProject -o $HostConfigProbeOut -v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "host_tool_config_probe build failed with exit code $LASTEXITCODE"
    }

    $probe = Join-Path $HostConfigProbeOut "host_tool_config_probe.exe"
    if (-not (Test-Path -LiteralPath $probe)) {
        throw "host_tool_config_probe.exe not found: $probe"
    }
    return $probe
}

function Resolve-DeviceIdFromBoard {
    param([string]$TargetIp)

    $probe = Resolve-HostConfigProbe
    $output = & $probe read $TargetIp 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "read board config failed for ${TargetIp}: $($output -join [Environment]::NewLine)"
    }

    foreach ($line in $output) {
        if ($line -match "^DeviceId=(.+)$") {
            $value = $Matches[1].Trim()
            if ($value) {
                return $value
            }
        }
    }
    throw "DeviceId not found in board config output from $TargetIp"
}

function Write-PostFlashMqttConfig {
    param(
        [string]$TargetIp,
        [string]$BrokerHost,
        [int]$BrokerPort,
        [string]$MqttUsername,
        [string]$MqttPassword,
        [string]$TargetCityId,
        [string]$TargetPoleId
    )

    $probe = Resolve-HostConfigProbe
    $args = @(
        "write-remote-emqx",
        $TargetIp,
        $BrokerHost,
        [string]$BrokerPort,
        $MqttUsername,
        $MqttPassword,
        "2",
        "1",
        $TargetCityId,
        $TargetPoleId
    )

    $output = & $probe @args 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "post-flash MQTT mTLS config write failed for ${TargetIp}: $($output -join [Environment]::NewLine)"
    }
    return $output
}

Assert-IpAddress -Name "ServerIp" -Value $ServerIp
if ($BoardIp) {
    Assert-IpAddress -Name "BoardIp" -Value $BoardIp
}
if ($MqttPort -le 0 -or $MqttPort -gt 65535) {
    throw "MqttPort must be 1-65535."
}
if ($PostFlashConfigRetries -lt 1) {
    throw "PostFlashConfigRetries must be at least 1."
}
if ($PostFlashConfigRetryDelaySeconds -lt 1) {
    throw "PostFlashConfigRetryDelaySeconds must be at least 1."
}
if (-not (Test-Path -LiteralPath $CertGenerator)) {
    throw "certificate generator not found: $CertGenerator"
}
if (-not (Test-Path -LiteralPath $BuildScript)) {
    throw "real-board build script not found: $BuildScript"
}
if (-not (Test-Path -LiteralPath $FlashScript)) {
    throw "safe flash script not found: $FlashScript"
}

$resolvedDeviceId = $DeviceId
if (-not $resolvedDeviceId) {
    if (-not $BoardIp -or $SkipBoardProbe) {
        throw "DeviceId is required when BoardIp probing is unavailable."
    }
    $resolvedDeviceId = Resolve-DeviceIdFromBoard -TargetIp $BoardIp
}

if (-not $OutputDir) {
    $safeDeviceId = $resolvedDeviceId -replace "[^A-Za-z0-9_.-]", "_"
    $OutputDir = Join-Path $RepoRoot ("开发环境\generated\real_board_mtls_stage1_{0}_{1}" -f $safeDeviceId, (Get-Date -Format "yyyyMMdd-HHmmss"))
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$CertDir = Join-Path $OutputDir "certs"
New-Item -ItemType Directory -Force -Path $CertDir | Out-Null

$python = Resolve-ToolPython
$certArgs = @(
    "--out-dir", $CertDir,
    "--client-common-name", $resolvedDeviceId,
    "--server-ip", $ServerIp
)
foreach ($dns in $ServerDns) {
    if (-not [string]::IsNullOrWhiteSpace($dns)) {
        $certArgs += @("--server-dns", $dns.Trim())
    }
}
& $python $CertGenerator @certArgs
if ($LASTEXITCODE -ne 0) {
    throw "certificate generation failed with exit code $LASTEXITCODE"
}

$defines = @(
    (ConvertTo-CStringDefine -Name "REAL_BOARD_DEFAULT_MQTT_HOST" -Value $ServerIp),
    ("REAL_BOARD_DEFAULT_MQTT_PORT={0}" -f $MqttPort),
    "REAL_BOARD_DEFAULT_TLS_MODE=2",
    "REAL_BOARD_DEFAULT_TLS_VERIFY_PEER=1",
    ("SYSTEM_CONFIG_VERSION={0}" -f $ConfigVersion)
)

& $BuildScript -Gcc $Gcc -MtlsCertDir $CertDir -AdditionalDefine $defines -Clean:$Clean
if ($LASTEXITCODE -ne 0) {
    throw "real-board firmware build failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $FirmwareHex)) {
    throw "built firmware HEX not found: $FirmwareHex"
}

$artifactHex = Join-Path $OutputDir "mqttv5_real_board_mtls.hex"
$artifactBin = Join-Path $OutputDir "mqttv5_real_board_mtls.bin"
$firmwareBin = [System.IO.Path]::ChangeExtension($FirmwareHex, ".bin")
Copy-Item -LiteralPath $FirmwareHex -Destination $artifactHex -Force
if (Test-Path -LiteralPath $firmwareBin) {
    Copy-Item -LiteralPath $firmwareBin -Destination $artifactBin -Force
}

$flashed = $false
if (-not $SkipFlash) {
    & $FlashScript -Image $FirmwareHex
    if ($LASTEXITCODE -ne 0) {
        throw "safe flash script failed with exit code $LASTEXITCODE"
    }
    $flashed = $true
}

$postFlashConfigWritten = $false
if ($BoardIp -and -not $SkipFlash -and -not $SkipPostFlashConfig) {
    $lastPostFlashError = $null
    for ($attempt = 1; $attempt -le $PostFlashConfigRetries; $attempt++) {
        Start-Sleep -Seconds $PostFlashConfigRetryDelaySeconds
        try {
            Write-PostFlashMqttConfig `
                -TargetIp $BoardIp `
                -BrokerHost $ServerIp `
                -BrokerPort $MqttPort `
                -MqttUsername $Username `
                -MqttPassword $Password `
                -TargetCityId $CityId `
                -TargetPoleId $PoleId | Out-Host
            $postFlashConfigWritten = $true
            break
        }
        catch {
            $lastPostFlashError = $_
            Write-Warning ("post-flash config attempt {0}/{1} failed: {2}" -f $attempt, $PostFlashConfigRetries, $_.Exception.Message)
        }
    }
    if (-not $postFlashConfigWritten) {
        throw $lastPostFlashError
    }
}

$summary = [ordered]@{
    stage = "real_board_mtls_stage1"
    board_ip = $BoardIp
    server_ip = $ServerIp
    server_dns = $ServerDns
    mqtt_port = $MqttPort
    device_id = $resolvedDeviceId
    tls_mode = 2
    tls_verify_peer = 1
    config_version = $ConfigVersion
    output_dir = $OutputDir
    cert_dir = $CertDir
    ca = (Join-Path $CertDir "ca.pem")
    server_cert = (Join-Path $CertDir "server.pem")
    server_key = (Join-Path $CertDir "server.key")
    client_cert = (Join-Path $CertDir "client.pem")
    client_key = (Join-Path $CertDir "client.key")
    firmware_hex = $FirmwareHex
    artifact_hex = $artifactHex
    flashed = $flashed
    post_flash_config_written = $postFlashConfigWritten
}
$summaryPath = Join-Path $OutputDir "provision_summary.json"
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host ("MTLS_STAGE1_SUMMARY={0}" -f $summaryPath)
