param(
    [string]$Gcc = "D:\Tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-gcc.exe",
    [string]$FirmwareVersion = "sim-board-app1",
    [string]$OutputName = "mqttv5_sim_board",
    [string[]]$AdditionalDefine = @(),
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptRoot "..\..")).Path
$ProjectRoot = Join-Path $RepoRoot "mqttv5_sim_board"

$ArgsList = @(
    "--gcc", $Gcc,
    "--project-root", $ProjectRoot,
    "--output-name", $OutputName,
    "--define", "SIM_BOARD_FW_VERSION=`"$FirmwareVersion`""
)
foreach ($Define in $AdditionalDefine) {
    $ArgsList += @("--define", $Define)
}
if ($Clean) {
    $ArgsList += "--clean"
}

python (Join-Path $ScriptRoot "build_local_emqx_firmware.py") @ArgsList
exit $LASTEXITCODE
