param(
    [string]$Uv4Path,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$DataDirName = "$([char]0x8D44)$([char]0x6599)"
$ProjectDir = Join-Path $Root "$DataDirName\user\MDK"
$Project = Join-Path $ProjectDir "spi_three_wire_tx_and_rx_interrupt.uvprojx"
$Target = "spi_three_wire_tx_and_rx_interrupt_Release"
$Log = Join-Path $ProjectDir "output\release\build-release.log"

if (-not $Uv4Path) {
    $candidates = @(
        "D:\Keil_v5\UV4\UV4.exe",
        "C:\Keil_v5\UV4\UV4.exe",
        "C:\Keil\UV4\UV4.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            $Uv4Path = $candidate
            break
        }
    }
}

if (-not $Uv4Path) {
    $cmd = Get-Command "UV4.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) {
        $Uv4Path = $cmd.Source
    }
}

if (-not $Uv4Path -or -not (Test-Path -LiteralPath $Uv4Path)) {
    throw "UV4.exe not found. Install Keil MDK-ARM 5 first."
}

if (-not (Test-Path -LiteralPath $Project)) {
    throw "Project file not found: $Project"
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Log) | Out-Null

if ($Clean) {
    $args = @("-c", "`"$Project`"", "-t", "`"$Target`"", "-j0", "-o", "`"$Log`"")
    & $Uv4Path @args
}

$args = @("-b", "`"$Project`"", "-t", "`"$Target`"", "-j0", "-o", "`"$Log`"")
& $Uv4Path @args

$hex = Join-Path $ProjectDir "output\release\spi_three_wire_tx_and_rx_interrupt.hex"
$bin = Join-Path $ProjectDir "output\release\spi_three_wire_tx_and_rx_interrupt.bin"

Write-Host ""
Write-Host "Build log: $Log"
if (Test-Path -LiteralPath $hex) {
    Write-Host "HEX: $hex" -ForegroundColor Green
} else {
    Write-Host "HEX not generated." -ForegroundColor Yellow
}
if (Test-Path -LiteralPath $bin) {
    Write-Host "BIN: $bin" -ForegroundColor Green
} else {
    Write-Host "BIN not generated." -ForegroundColor Yellow
}

if (Test-Path -LiteralPath $Log) {
    Write-Host ""
    Write-Host "Last build log lines:"
    Get-Content -LiteralPath $Log -Tail 30
}
