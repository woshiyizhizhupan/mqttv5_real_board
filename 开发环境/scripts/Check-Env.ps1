param(
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$DataDirName = "$([char]0x8D44)$([char]0x6599)"
$EnvDirName = "$([char]0x5F00)$([char]0x53D1)$([char]0x73AF)$([char]0x5883)"
$Project = Join-Path $Root "$DataDirName\user\MDK\spi_three_wire_tx_and_rx_interrupt.uvprojx"
$ReleaseTarget = "spi_three_wire_tx_and_rx_interrupt_Release"

function Find-FirstExisting {
    param([string[]]$Paths)
    foreach ($path in $Paths) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }
    return $null
}

function Find-CommandPath {
    param([string]$Name, [string]$RejectRegex = "")
    $commands = Get-Command $Name -ErrorAction SilentlyContinue
    foreach ($cmd in $commands) {
        if ($cmd.Source -and (($RejectRegex -eq "") -or ($cmd.Source -notmatch $RejectRegex))) {
            return $cmd.Source
        }
    }
    return $null
}

function Add-Check {
    param(
        [string]$Name,
        [bool]$Ok,
        [string]$Value,
        [string]$Fix
    )
    [pscustomobject]@{
        Item = $Name
        OK = if ($Ok) { "YES" } else { "NO" }
        Value = if ($Value) { $Value } else { "" }
        Fix = if ($Ok) { "" } else { $Fix }
    }
}

$uv4 = Find-FirstExisting @(
    "D:\Keil_v5\UV4\UV4.exe",
    "C:\Keil_v5\UV4\UV4.exe",
    "C:\Keil\UV4\UV4.exe"
)
if (-not $uv4) {
    $uv4 = Find-CommandPath "UV4.exe"
}

$armcc = Find-FirstExisting @(
    "D:\Keil_v5\ARM\ARMCC\bin\armcc.exe",
    "C:\Keil_v5\ARM\ARMCC\bin\armcc.exe",
    "C:\Keil\ARM\ARMCC\bin\armcc.exe"
)
if (-not $armcc) {
    $armcc = Find-CommandPath "armcc.exe"
}

$fromelf = Find-FirstExisting @(
    "D:\Keil_v5\ARM\ARMCC\bin\fromelf.exe",
    "C:\Keil_v5\ARM\ARMCC\bin\fromelf.exe",
    "C:\Keil\ARM\ARMCC\bin\fromelf.exe"
)
if (-not $fromelf) {
    $fromelf = Find-CommandPath "fromelf.exe"
}

$jlink = Find-FirstExisting @(
    "C:\Program Files\SEGGER\JLink\JLink.exe",
    "C:\Program Files (x86)\SEGGER\JLink\JLink.exe",
    "$env:USERPROFILE\SEGGER\JLink\JLink.exe"
)
if (-not $jlink) {
    $jlink = Find-CommandPath "JLink.exe" "Microsoft\\jdk|Java|OpenJDK"
}

$stProgrammer = Find-FirstExisting @(
    "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    "C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
)
if (-not $stProgrammer) {
    $stProgrammer = Find-CommandPath "STM32_Programmer_CLI.exe"
}

$packRoots = @(
    "D:\Keil_v5\ARM\PACK",
    "D:\Keil_v5\Packs",
    "$env:LOCALAPPDATA\Arm\Packs",
    "$env:LOCALAPPDATA\Keil_v5\Packs",
    "C:\Keil_v5\ARM\PACK",
    "C:\Keil_v5\Packs"
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }

$installedHdscPack = $null
foreach ($root in $packRoots) {
    $found = Get-ChildItem -LiteralPath $root -Recurse -File -Filter "*.pdsc" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "HDSC.*HC32F46|HC32F460" } |
        Select-Object -First 1
    if ($found) {
        $installedHdscPack = $found.FullName
        break
    }
}

$localPack = Join-Path $Root "$EnvDirName\packs\HDSC.HC32F460.1.0.11.pack"
$localFlm = Join-Path $Root "$EnvDirName\flash_algo\HC32F460_512K.FLM"

$checks = @()
$checks += Add-Check "Keil uVision5 UV4.exe" ([bool]$uv4) $uv4 "Install Keil MDK-ARM 5."
$checks += Add-Check "ARM Compiler 5 armcc.exe" ([bool]$armcc) $armcc "Install ARM Compiler 5.06 from Keil MDK legacy compiler support."
$checks += Add-Check "fromelf.exe" ([bool]$fromelf) $fromelf "Install ARM Compiler 5.06; fromelf is used to generate .bin."
$checks += Add-Check "HDSC HC32F460 Pack installed" ([bool]$installedHdscPack) $installedHdscPack "Double-click the local .pack file or install it in Keil Pack Installer."
$checks += Add-Check "Local HDSC Pack file" (Test-Path -LiteralPath $localPack) $localPack "Download HDSC.HC32F460.1.0.11.pack into the project env packs folder."
$checks += Add-Check "Local HC32F460 FLM" (Test-Path -LiteralPath $localFlm) $localFlm "Keep the customer-provided FLM files in the project env flash_algo folder."
$checks += Add-Check "SEGGER J-Link tools" ([bool]$jlink) $jlink "Install SEGGER J-Link Software and Documentation Pack for SWD debug."
$checks += Add-Check "STM32/ST-Link programmer optional" ([bool]$stProgrammer) $stProgrammer "Optional only; J-Link is preferred for this HC32 project."
$checks += Add-Check "Keil project file" (Test-Path -LiteralPath $Project) $Project "Keep the original customer project under the data folder user\MDK."

if (-not $Quiet) {
    Write-Host ""
    Write-Host "Project root: $Root"
    Write-Host "Project file: $Project"
    Write-Host "Release target: $ReleaseTarget"
    Write-Host ""
    $checks | Format-Table -AutoSize
}

$failedRequired = $checks | Where-Object {
    $_.Item -in @(
        "Keil uVision5 UV4.exe",
        "ARM Compiler 5 armcc.exe",
        "fromelf.exe",
        "HDSC HC32F460 Pack installed",
        "SEGGER J-Link tools",
        "Keil project file"
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
    Write-Host "Environment looks ready for Keil build/debug." -ForegroundColor Green
}
exit 0
