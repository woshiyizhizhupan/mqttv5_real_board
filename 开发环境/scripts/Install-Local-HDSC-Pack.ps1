param(
    [string]$KeilRoot = "D:\Keil_v5"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$EnvDirName = "$([char]0x5F00)$([char]0x53D1)$([char]0x73AF)$([char]0x5883)"
$PackFile = Join-Path $Root "$EnvDirName\packs\HDSC.HC32F460.1.0.11.pack"

if (-not (Test-Path -LiteralPath $PackFile)) {
    throw "Local HDSC pack not found: $PackFile"
}

$packInstaller = Join-Path $KeilRoot "UV4\PackInstaller.exe"
if (-not (Test-Path -LiteralPath $packInstaller)) {
    $packInstaller = Join-Path $KeilRoot "UV4\PackUnzip.exe"
}

if (Test-Path -LiteralPath $packInstaller) {
    Start-Process -FilePath $packInstaller -ArgumentList "`"$PackFile`"" -Wait
    Write-Host "Pack installer launched for: $PackFile"
} else {
    Write-Host "Keil pack installer not found under $KeilRoot."
    Write-Host "Please install manually: double-click $PackFile or open it from Keil Pack Installer."
}
