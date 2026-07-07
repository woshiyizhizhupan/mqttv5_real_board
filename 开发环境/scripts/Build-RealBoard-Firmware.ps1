param(
    [string]$Gcc = "D:\Tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-gcc.exe",
    [string]$MtlsCertDir,
    [switch]$TestVersion,
    [switch]$EnableLegacyFrame,
    [switch]$DisableLegacyFrame,
    [string]$FirmwareVersion = "real-board-mtls-ota-test",
    [string[]]$AdditionalDefine = @(),
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptRoot "..\..")).Path
$ProjectRoot = Join-Path $RepoRoot "mqttv5_real_board"

if ($EnableLegacyFrame -and $DisableLegacyFrame) {
    throw "EnableLegacyFrame and DisableLegacyFrame cannot be used together."
}

if ($MtlsCertDir) {
    python (Join-Path $ScriptRoot "embed_real_board_mtls_certs.py") `
        --cert-dir $MtlsCertDir `
        --out (Join-Path $ProjectRoot "src\real_board_tls_certs.c")
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$ArgsList = @(
    "--gcc", $Gcc,
    "--project-root", $ProjectRoot,
    "--output-name", "mqttv5_real_board"
)
$LegacyDefine = if ($DisableLegacyFrame) { "REAL_BOARD_ENABLE_LEGACY_FRAME=0" } else { "REAL_BOARD_ENABLE_LEGACY_FRAME=1" }
$ArgsList += @("--define", $LegacyDefine)
if ($TestVersion) {
    $ArgsList += @("--define", "REAL_BOARD_OTA_REBOOT_AFTER_END=0")
    $ArgsList += @("--define", ('REAL_BOARD_FW_VERSION="{0}"' -f $FirmwareVersion))
}
foreach ($Define in $AdditionalDefine) {
    $ArgsList += @("--define", $Define)
}
if ($Clean) {
    $ArgsList += "--clean"
}

python (Join-Path $ScriptRoot "build_local_emqx_firmware.py") @ArgsList
exit $LASTEXITCODE
