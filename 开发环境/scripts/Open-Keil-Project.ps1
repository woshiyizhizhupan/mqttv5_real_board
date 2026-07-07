param(
    [string]$Uv4Path
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$DataDirName = "$([char]0x8D44)$([char]0x6599)"
$Project = Join-Path $Root "$DataDirName\user\MDK\spi_three_wire_tx_and_rx_interrupt.uvprojx"

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

Start-Process -FilePath $Uv4Path -ArgumentList "`"$Project`"" -WorkingDirectory (Split-Path -Parent $Project)
