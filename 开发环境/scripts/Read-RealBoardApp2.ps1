param(
    [Parameter(Mandatory = $true)]
    [uint32]$Length,
    [string]$Output,
    [uint32]$App2Base = 0x00042000,
    [uint32]$App2End = 0x00076000,
    [int]$AdapterSpeedKHz = 100,
    [string]$OpenOcd,
    [string]$OpenOcdScripts
)

$ErrorActionPreference = "Stop"
$StageDir = "D:\Tools\hc32-openocd-work"

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

function ConvertTo-TclPath {
    param([string]$Path)
    return ([System.IO.Path]::GetFullPath($Path) -replace "\\", "/")
}

function Format-Hex32 {
    param([uint32]$Value)
    return ("0x{0:X8}" -f $Value)
}

if ($Length -eq 0) {
    throw "Length must be greater than zero."
}
if ($App2Base -lt 0x00042000 -or ([uint64]$App2Base + [uint64]$Length) -gt [uint64]$App2End) {
    throw ("Readback window must stay inside APP2: base={0}, length={1}, end={2}." -f (Format-Hex32 $App2Base), $Length, (Format-Hex32 $App2End))
}

if (-not $OpenOcd) {
    $OpenOcd = Find-CommandPath @("openocd.exe", "openocd")
    if (-not $OpenOcd -and (Test-Path -LiteralPath "D:\Tools\xpack-openocd-0.12.0-7\bin\openocd.exe")) {
        $OpenOcd = "D:\Tools\xpack-openocd-0.12.0-7\bin\openocd.exe"
    }
}
if (-not $OpenOcd -or -not (Test-Path -LiteralPath $OpenOcd)) {
    throw "OpenOCD not found. Expected: D:\Tools\xpack-openocd-0.12.0-7\bin\openocd.exe"
}
if (-not $OpenOcdScripts) {
    $OpenOcdScripts = Join-Path (Split-Path -Parent (Split-Path -Parent $OpenOcd)) "openocd\scripts"
}
if (-not (Test-Path -LiteralPath $OpenOcdScripts)) {
    throw "OpenOCD scripts directory not found: $OpenOcdScripts"
}

New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if (-not $Output) {
    $Output = Join-Path $StageDir ("real_board_app2_readback_{0}.bin" -f $timestamp)
}
$Output = [System.IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($Output)) | Out-Null
$openOcdOutput = Join-Path $StageDir ("real_board_app2_readback_openocd_{0}.bin" -f $timestamp)

$tcl = Join-Path $StageDir ("read_real_board_app2_{0}.tcl" -f $timestamp)
$lines = @(
    "init",
    "halt",
    "echo {Dumping real-board APP2 readback window}",
    "dump_image {$(ConvertTo-TclPath $openOcdOutput)} $(Format-Hex32 $App2Base) $(Format-Hex32 $Length)",
    "reset run",
    "shutdown"
)
[System.IO.File]::WriteAllLines($tcl, $lines, [System.Text.UTF8Encoding]::new($false))

$openOcdArgs = @(
    "-s", $OpenOcdScripts,
    "-f", "interface/stlink-hla.cfg",
    "-c", "transport select hla_swd",
    "-c", "set CHIPNAME hc32f460",
    "-c", "source [find target/stm32f4x.cfg]",
    "-c", "adapter speed $AdapterSpeedKHz",
    "-f", $tcl
)

& $OpenOcd @openOcdArgs
if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD APP2 readback failed with exit code $LASTEXITCODE"
}

Copy-Item -LiteralPath $openOcdOutput -Destination $Output -Force
Write-Host ("APP2_READBACK={0}" -f $Output)
Write-Host ("APP2_RANGE={0}+{1}" -f (Format-Hex32 $App2Base), $Length)
