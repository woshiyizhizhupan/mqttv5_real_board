param(
    [string]$Gcc = "D:\Tools\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin\arm-none-eabi-gcc.exe",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ArgsList = @("--gcc", $Gcc)
if ($Clean) {
    $ArgsList += "--clean"
}
python (Join-Path $ScriptRoot "build_local_emqx_firmware.py") @ArgsList
exit $LASTEXITCODE
