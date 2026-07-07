param(
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Workspace = Join-Path $Root "mqttv5(1)\eide\mqttv5.code-workspace"
$Pack = Join-Path $Root "mqttv5(1)\HDSC.HC32F460.1.0.12.pack"
$Hex = Join-Path $Root "mqttv5(1)\eide\build\Debug\mqttv5.hex"

function Find-CommandPath {
    param([string[]]$Names)
    foreach ($name in $Names) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($cmd) {
            return $cmd.Source
        }
    }
    return $null
}

function Add-Check {
    param([string]$Item, [bool]$OK, [string]$Value, [string]$Fix)
    [pscustomobject]@{
        Item = $Item
        OK = if ($OK) { "YES" } else { "NO" }
        Value = if ($Value) { $Value } else { "" }
        Fix = if ($OK) { "" } else { $Fix }
    }
}

$code = Find-CommandPath @("code.cmd", "code")
if (-not $code -and (Test-Path -LiteralPath "D:\Tools\VSCode\bin\code.cmd")) {
    $code = "D:\Tools\VSCode\bin\code.cmd"
}

$pyocd = Find-CommandPath @("pyocd.exe", "pyocd")
if (-not $pyocd -and (Test-Path -LiteralPath "$env:USERPROFILE\AppData\Local\Programs\Python\Python312\Scripts\pyocd.exe")) {
    $pyocd = "$env:USERPROFILE\AppData\Local\Programs\Python\Python312\Scripts\pyocd.exe"
}

$openocd = Find-CommandPath @("openocd.exe", "openocd")
if (-not $openocd -and (Test-Path -LiteralPath "D:\Tools\xpack-openocd-0.12.0-7\bin\openocd.exe")) {
    $openocd = "D:\Tools\xpack-openocd-0.12.0-7\bin\openocd.exe"
}

$ninja = Find-CommandPath @("ninja.exe", "ninja")
if (-not $ninja -and (Test-Path -LiteralPath "D:\Tools\ninja\ninja.exe")) {
    $ninja = "D:\Tools\ninja\ninja.exe"
}

$gcc = Find-CommandPath @("arm-none-eabi-gcc.exe", "arm-none-eabi-gcc")
$gdb = Find-CommandPath @("arm-none-eabi-gdb.exe", "arm-none-eabi-gdb")

$stlink = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -match "VID_0483&PID_3748" } |
    Select-Object -First 1

$probeText = ""
$probeOk = $false
if ($pyocd) {
    $probeText = (& $pyocd list --probes 2>&1) -join "`n"
    $probeOk = $LASTEXITCODE -eq 0 -and $probeText -notmatch "No available debug probes"
}

$checks = @()
$checks += Add-Check "Portable VS Code" ([bool]$code) $code "Install or keep D:\Tools\VSCode."
$checks += Add-Check "EIDE workspace" (Test-Path -LiteralPath $Workspace) $Workspace "Keep mqttv5(1)\eide\mqttv5.code-workspace."
$checks += Add-Check "pyOCD" ([bool]$pyocd) $pyocd "Install with python -m pip install pyocd."
$checks += Add-Check "HDSC CMSIS-Pack" (Test-Path -LiteralPath $Pack) $Pack "Keep HDSC.HC32F460.1.0.12.pack in mqttv5(1)."
$checks += Add-Check "MQTTv5 HEX image" (Test-Path -LiteralPath $Hex) $Hex "Build in EIDE or keep the provided Debug hex."
$checks += Add-Check "ST-LINKV2 USB device" ([bool]$stlink) ($(if ($stlink) { "$($stlink.Status): $($stlink.FriendlyName) $($stlink.InstanceId)" })) "Plug in ST-LINKV2."
$checks += Add-Check "pyOCD sees probe" $probeOk $probeText "Bind ST-LINKV2 to WinUSB/libusb driver with Zadig, then replug it."
$checks += Add-Check "Ninja optional" ([bool]$ninja) $ninja "Optional; extracted to D:\Tools\ninja."
$checks += Add-Check "OpenOCD optional" ([bool]$openocd) $openocd "Optional; HC32F460 flashing uses pyOCD."
$checks += Add-Check "Arm GCC optional for rebuild" ([bool]$gcc) $gcc "Install via EIDE toolchain manager or an admin-installed Arm GNU Toolchain."
$checks += Add-Check "Arm GDB optional for debug" ([bool]$gdb) $gdb "Install via EIDE toolchain manager or an admin-installed Arm GNU Toolchain."

if (-not $Quiet) {
    $checks | Format-Table -AutoSize -Wrap
}

$failedRequired = $checks | Where-Object {
    $_.Item -in @(
        "Portable VS Code",
        "EIDE workspace",
        "pyOCD",
        "HDSC CMSIS-Pack",
        "MQTTv5 HEX image",
        "ST-LINKV2 USB device",
        "pyOCD sees probe"
    ) -and $_.OK -ne "YES"
}

if ($failedRequired) {
    if (-not $Quiet) {
        Write-Host ""
        Write-Host "Required items still missing:" -ForegroundColor Yellow
        $failedRequired | ForEach-Object { Write-Host "- $($_.Item): $($_.Fix)" }
    }
    exit 1
}

if (-not $Quiet) {
    Write-Host ""
    Write-Host "VS Code + ST-LINKV2 pyOCD environment looks ready." -ForegroundColor Green
}
