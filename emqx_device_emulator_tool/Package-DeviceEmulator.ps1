param(
    [string]$Configuration = "Release",
    [string]$PackageRoot = "D:\code\唐家湾嵌入式兼职\交付包\public_emqx_board_materials_20260630\01_emqx_gui_client\emqx_device_emulator_csharp"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$appProject = Join-Path $projectRoot "EmqxDeviceEmulator.App\EmqxDeviceEmulator.App.csproj"
$appOutput = Join-Path $projectRoot "EmqxDeviceEmulator.App\bin\$Configuration\net48"

dotnet build $appProject -c $Configuration -v:minimal

$resolvedPackageRoot = [System.IO.Path]::GetFullPath($PackageRoot)
$allowedRoot = [System.IO.Path]::GetFullPath("D:\code\唐家湾嵌入式兼职\交付包")
if (-not $resolvedPackageRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "PackageRoot must stay under $allowedRoot"
}

if (Test-Path -LiteralPath $resolvedPackageRoot) {
    Remove-Item -LiteralPath $resolvedPackageRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $resolvedPackageRoot | Out-Null

Copy-Item -Path (Join-Path $appOutput "*") -Destination $resolvedPackageRoot -Recurse -Force
Copy-Item -LiteralPath (Join-Path $projectRoot "README.md") -Destination (Join-Path $resolvedPackageRoot "README.md") -Force

$settings = @"
host=39.103.154.108
port=1883
client_id=GM400-67890-device-emulator
username=GM400-67890
password=public
qos=2
tls=false
connect_timeout_seconds=6
device_id=GM400-67890
firmware_version=csharp-device-emulator-20260701
topic_telemetry_up=city/zhxm01/pole/zhxm002/device/67890/
topic_cmd_down=city/zhxm01/pole/zhxm002/device/67890/get
topic_event_up=city/zhxm01/pole/zhxm002/device/67890/event
topic_ota_down=city/zhxm01/pole/zhxm002/device/67890/ota
topic_debug_up=city/zhxm01/pole/zhxm002/device/67890/debug
extra_subscriptions=
auto_follow_latest=false
"@
Set-Content -LiteralPath (Join-Path $resolvedPackageRoot "device_emulator_settings.ini") -Value $settings -Encoding UTF8

$startAuto = @"
@echo off
cd /d "%~dp0"
start "" "%~dp0EmqxDeviceEmulator.App.exe" --autoconnect
"@
Set-Content -LiteralPath (Join-Path $resolvedPackageRoot "启动并自动连接.cmd") -Value $startAuto -Encoding Default

$startManual = @"
@echo off
cd /d "%~dp0"
start "" "%~dp0EmqxDeviceEmulator.App.exe"
"@
Set-Content -LiteralPath (Join-Path $resolvedPackageRoot "启动但不自动连接.cmd") -Value $startManual -Encoding Default

$report = "D:\code\唐家湾嵌入式兼职\docs\EMQX_DEVICE_EMULATOR_TEST_REPORT_2026-07-01.md"
if (Test-Path -LiteralPath $report) {
    Copy-Item -LiteralPath $report -Destination (Join-Path $resolvedPackageRoot "EMQX_DEVICE_EMULATOR_TEST_REPORT_2026-07-01.md") -Force
}

Write-Host "Package created: $resolvedPackageRoot"
