param(
    [string]$Image,
    [uint32]$AppBase = 0x00004000,
    [uint32]$SectorSize = 0x00002000,
    [uint32]$PageSize = 0x00000200,
    [int]$AdapterSpeedKHz = 100,
    [string]$StageDir,
    [string]$OpenOcd,
    [string]$OpenOcdScripts,
    [string]$FlmBlob,
    [switch]$NoReset,
    [switch]$ConnectUnderReset,
    [switch]$InitialHaltOnly
)

$ErrorActionPreference = "Stop"

function Resolve-ProjectRoot {
    $scriptName = Split-Path -Leaf $PSScriptRoot
    $scriptParent = Split-Path -Parent $PSScriptRoot
    $parentName = Split-Path -Leaf $scriptParent

    if ($scriptName -eq "06_scripts") {
        return $scriptParent
    }
    if ($scriptName -eq "scripts" -and $parentName -eq "开发环境") {
        return (Split-Path -Parent $scriptParent)
    }
    return (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}

$Root = Resolve-ProjectRoot
$IsDeliveryPackage = (Split-Path -Leaf $PSScriptRoot) -eq "06_scripts"
$MdkHex = Join-Path $Root "mqttv5(1)\mdk\Objects\mqttv5.hex"
$EideHex = Join-Path $Root "mqttv5(1)\eide\build\Debug\mqttv5.hex"
$PackageHex = Join-Path $Root "02_firmware\real_board_public_emqx\mqttv5_real_board.hex"
if ($IsDeliveryPackage) {
    $GeneratedDir = Join-Path $PSScriptRoot "generated"
    $BackupDir = Join-Path $PSScriptRoot "backups"
} else {
    $GeneratedDir = Join-Path $Root "开发环境\generated"
    $BackupDir = Join-Path $Root "开发环境\backups"
}
if (-not $StageDir) {
    $StageDir = Join-Path ([System.IO.Path]::GetTempPath()) "hc32-openocd-work"
}

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

function Align-Up {
    param([uint32]$Value, [uint32]$Alignment)

    return [uint32]([math]::Floor(([uint64]$Value + [uint64]$Alignment - 1) / [uint64]$Alignment) * [uint64]$Alignment)
}

function Read-IntelHexAboveAddress {
    param([string]$Path, [uint32]$BaseAddress)

    $data = @{}
    $upper = [uint32]0
    $belowBaseBytes = 0

    foreach ($line in Get-Content -LiteralPath $Path) {
        $record = $line.Trim()
        if (-not $record) {
            continue
        }
        if ($record[0] -ne ":") {
            throw "Invalid Intel HEX record: $record"
        }

        $length = [Convert]::ToInt32($record.Substring(1, 2), 16)
        $address = [Convert]::ToUInt32($record.Substring(3, 4), 16)
        $type = [Convert]::ToInt32($record.Substring(7, 2), 16)

        if ($type -eq 0) {
            for ($index = 0; $index -lt $length; $index++) {
                $byte = [Convert]::ToByte($record.Substring(9 + ($index * 2), 2), 16)
                $absolute = [uint32]($upper + $address + $index)
                if ($absolute -ge $BaseAddress) {
                    $data[$absolute] = $byte
                } else {
                    $belowBaseBytes++
                }
            }
        } elseif ($type -eq 1) {
            break
        } elseif ($type -eq 4) {
            $upper = [uint32]([Convert]::ToUInt32($record.Substring(9, 4), 16) -shl 16)
        } elseif ($type -eq 2) {
            $upper = [uint32]([Convert]::ToUInt32($record.Substring(9, 4), 16) -shl 4)
        }
    }

    if ($data.Count -eq 0) {
        throw "No image data found at or above $(Format-Hex32 $BaseAddress): $Path"
    }

    $keys = [uint32[]]$data.Keys
    $min = ($keys | Measure-Object -Minimum).Minimum
    $max = ($keys | Measure-Object -Maximum).Maximum

    return [pscustomobject]@{
        Data = $data
        MinAddress = [uint32]$min
        MaxAddress = [uint32]$max
        BelowBaseBytes = $belowBaseBytes
    }
}

function Convert-ImageToAppBin {
    param(
        [string]$InputImage,
        [string]$OutputBin,
        [uint32]$BaseAddress,
        [uint32]$FlashSectorSize
    )

    $extension = [System.IO.Path]::GetExtension($InputImage).ToLowerInvariant()
    if ($extension -eq ".hex") {
        $parsed = Read-IntelHexAboveAddress -Path $InputImage -BaseAddress $BaseAddress
        $endAddress = Align-Up -Value ([uint32]($parsed.MaxAddress + 1)) -Alignment $FlashSectorSize
        $length = [int]($endAddress - $BaseAddress)
        $bytes = [byte[]]::new($length)
        [Array]::Fill($bytes, [byte]0xFF)

        foreach ($entry in $parsed.Data.GetEnumerator()) {
            $offset = [int]([uint32]$entry.Key - $BaseAddress)
            if ($offset -ge 0 -and $offset -lt $bytes.Length) {
                $bytes[$offset] = [byte]$entry.Value
            }
        }

        [System.IO.File]::WriteAllBytes($OutputBin, $bytes)
        return [pscustomobject]@{
            SourceType = "Intel HEX"
            Start = $BaseAddress
            End = $endAddress
            Length = $bytes.Length
            BelowBaseBytes = $parsed.BelowBaseBytes
            Bytes = $bytes
        }
    }

    if ($extension -eq ".bin") {
        $sourceBytes = [System.IO.File]::ReadAllBytes($InputImage)
        $endAddress = Align-Up -Value ([uint32]($BaseAddress + $sourceBytes.Length)) -Alignment $FlashSectorSize
        $length = [int]($endAddress - $BaseAddress)
        $bytes = [byte[]]::new($length)
        [Array]::Fill($bytes, [byte]0xFF)
        [Array]::Copy($sourceBytes, 0, $bytes, 0, $sourceBytes.Length)
        [System.IO.File]::WriteAllBytes($OutputBin, $bytes)
        return [pscustomobject]@{
            SourceType = "raw BIN"
            Start = $BaseAddress
            End = $endAddress
            Length = $bytes.Length
            BelowBaseBytes = 0
            Bytes = $bytes
        }
    }

    throw "Only .hex and .bin images are supported by the old ST-LINKV2 OpenOCD/FLM path: $InputImage"
}

function Get-NonBlankPages {
    param([byte[]]$Bytes, [uint32]$PageBytes)

    $pages = New-Object "System.Collections.Generic.List[int]"
    for ($offset = 0; $offset -lt $Bytes.Length; $offset += $PageBytes) {
        $hasData = $false
        for ($index = $offset; $index -lt ($offset + $PageBytes) -and $index -lt $Bytes.Length; $index++) {
            if ($Bytes[$index] -ne 0xFF) {
                $hasData = $true
                break
            }
        }
        if ($hasData) {
            [void]$pages.Add($offset)
        }
    }
    return $pages
}

if (-not $Image) {
    if ($IsDeliveryPackage -and (Test-Path -LiteralPath $PackageHex)) {
        $Image = $PackageHex
    } elseif (Test-Path -LiteralPath $MdkHex) {
        $Image = $MdkHex
    } elseif (Test-Path -LiteralPath $EideHex) {
        $Image = $EideHex
    }
}

if (-not $Image -or -not (Test-Path -LiteralPath $Image)) {
    throw "Image not found. Expected default image: $MdkHex"
}

if (-not $OpenOcd) {
    $bundledOpenOcd = Join-Path $PSScriptRoot "tools\openocd\xpack-openocd-0.12.0-7\bin\openocd.exe"
    if (Test-Path -LiteralPath $bundledOpenOcd) {
        $OpenOcd = $bundledOpenOcd
    }
    if (-not $OpenOcd) {
        $OpenOcd = Find-CommandPath @("openocd.exe", "openocd")
    }
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

if (-not $FlmBlob) {
    $bundledFlmBlob = Join-Path $PSScriptRoot "tools\hdsc_flm\HC32F460_512K_prg.bin"
    if (Test-Path -LiteralPath $bundledFlmBlob) {
        $FlmBlob = $bundledFlmBlob
    } else {
        $FlmBlob = "D:\Tools\hdsc-pack-inspect\HC32F460_512K_prg.bin"
    }
}

if (-not (Test-Path -LiteralPath $FlmBlob)) {
    throw "HDSC FLM RAM blob not found: $FlmBlob"
}

New-Item -ItemType Directory -Force -Path $GeneratedDir, $BackupDir, $StageDir | Out-Null

$appBaseText = "0x{0:X4}" -f $AppBase
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$appBin = Join-Path $StageDir ("mqttv5_app_from_{0}.bin" -f $appBaseText)
$projectAppBin = Join-Path $GeneratedDir ("mqttv5_app_from_{0}.bin" -f $appBaseText)
$openOcdTcl = Join-Path $StageDir ("flash_mqttv5_app_{0}.generated.tcl" -f $appBaseText)
$preBoot = Join-Path $StageDir ("hc32f460_boot_0x0_0x4000_before_{0}.bin" -f $timestamp)
$postBoot = Join-Path $StageDir ("hc32f460_boot_0x0_0x4000_after_{0}.bin" -f $timestamp)
$postApp = Join-Path $StageDir ("hc32f460_app_{0}_after_{1}.bin" -f $appBaseText, $timestamp)
$pageStageDir = Join-Path $StageDir ("mqttv5_app_pages_{0}" -f $timestamp)
$projectPreBoot = Join-Path $BackupDir ("hc32f460_boot_0x0_0x4000_before_{0}.bin" -f $timestamp)
$projectPostBoot = Join-Path $BackupDir ("hc32f460_boot_0x0_0x4000_after_{0}.bin" -f $timestamp)
$projectPostApp = Join-Path $BackupDir ("hc32f460_app_{0}_after_{1}.bin" -f $appBaseText, $timestamp)

$imageInfo = Convert-ImageToAppBin -InputImage $Image -OutputBin $appBin -BaseAddress $AppBase -FlashSectorSize $SectorSize
$programPages = Get-NonBlankPages -Bytes $imageInfo.Bytes -PageBytes $PageSize
$programPageFiles = @{}

New-Item -ItemType Directory -Force -Path $pageStageDir | Out-Null
foreach ($offset in $programPages) {
    $pageBytes = [byte[]]::new([int]$PageSize)
    [Array]::Copy($imageInfo.Bytes, [int]$offset, $pageBytes, 0, [int]$PageSize)
    $pagePath = Join-Path $pageStageDir ("page_0x{0:X6}.bin" -f $offset)
    [System.IO.File]::WriteAllBytes($pagePath, $pageBytes)
    $programPageFiles[[int]$offset] = $pagePath
}

if ($imageInfo.BelowBaseBytes -gt 0) {
    Write-Host ("Skipped {0} byte(s) below {1}; bootloader/low flash will not be programmed." -f $imageInfo.BelowBaseBytes, (Format-Hex32 $AppBase)) -ForegroundColor Yellow
}

$sectorStarts = New-Object "System.Collections.Generic.List[uint32]"
for ($address = $imageInfo.Start; $address -lt $imageInfo.End; $address += $SectorSize) {
    [void]$sectorStarts.Add([uint32]$address)
}

$flmBase = [uint32]0x20000000
$bufferBase = [uint32]0x20004000
$returnAddress = [uint32]0x20001000
$stackPointer = [uint32]0x20026000
$stackReserveBytes = [uint32]0x00004000
$bufferEnd = [uint32]([uint64]$bufferBase + [uint64]$PageSize)
if ($bufferEnd -gt [uint32]($stackPointer - $stackReserveBytes)) {
    throw ("APP page staging buffer would overlap FLM stack reserve: buffer={0}-{1}, stack_reserve={2}-{3}." -f (Format-Hex32 $bufferBase), (Format-Hex32 $bufferEnd), (Format-Hex32 ([uint32]($stackPointer - $stackReserveBytes))), (Format-Hex32 $stackPointer))
}
$initEntry = [uint32]0x20000441
$eraseSectorEntry = [uint32]0x20000107
$programPageEntry = [uint32]0x20000451
$unInitEntry = [uint32]0x2000047D

$lines = New-Object "System.Collections.Generic.List[string]"
$lines.Add("set RET_ADDR $(Format-Hex32 $returnAddress)")
$lines.Add("set STACK_PTR $(Format-Hex32 $stackPointer)")
$lines.Add("proc feed_wdt {} {")
$lines.Add("    mww 0xE0042000 0x00000003")
$lines.Add("    mww 0x40049008 0x00000123")
$lines.Add("    mww 0x40049008 0x00003210")
$lines.Add("    mww 0x40049408 0x00000123")
$lines.Add("    mww 0x40049408 0x00003210")
$lines.Add("}")
$lines.Add("proc call_flm {label entry arg0 arg1 arg2 arg3} {")
$lines.Add("    global RET_ADDR STACK_PTR")
$lines.Add("    echo [format {FLM %s} `$label]")
$lines.Add("    feed_wdt")
$lines.Add("    reg primask 1")
$lines.Add("    reg sp `$STACK_PTR")
$lines.Add("    reg lr [expr {`$RET_ADDR | 1}]")
$lines.Add("    reg r0 `$arg0")
$lines.Add("    reg r1 `$arg1")
$lines.Add("    reg r2 `$arg2")
$lines.Add("    reg r3 `$arg3")
$lines.Add("    resume `$entry")
$lines.Add("    wait_halt 15000")
$lines.Add("    set out [capture {reg r0}]")
$lines.Add("    if {![regexp {0x[0-9A-Fa-f]+} `$out retText]} {")
$lines.Add("        error [format {Cannot parse r0 after FLM %s: %s} `$label `$out]")
$lines.Add("    }")
$lines.Add("    set ret [expr {`$retText}]")
$lines.Add("    if {`$ret != 0} {")
$lines.Add("        error [format {FLM %s returned %s} `$label `$retText]")
$lines.Add("    }")
$lines.Add("}")
$lines.Add("init")
if ($InitialHaltOnly) {
    $lines.Add("halt 3000")
} else {
    $lines.Add("reset halt")
    $lines.Add("halt")
}
$lines.Add("reg primask 1")
$lines.Add("feed_wdt")
$lines.Add("echo {Dumping bootloader before flashing}")
$lines.Add("dump_image {$(ConvertTo-TclPath $preBoot)} 0x00000000 0x00004000")
$lines.Add("load_image {$(ConvertTo-TclPath $FlmBlob)} $(Format-Hex32 $flmBase) bin")
$lines.Add("mwh $(Format-Hex32 $returnAddress) 0xbe00")
$lines.Add("call_flm {Init erase} $(Format-Hex32 $initEntry) $(Format-Hex32 $imageInfo.Start) 0x00000000 0x00000001 0x00000000")

foreach ($sector in $sectorStarts) {
    $lines.Add("call_flm {EraseSector $(Format-Hex32 $sector)} $(Format-Hex32 $eraseSectorEntry) $(Format-Hex32 $sector) 0x00000000 0x00000000 0x00000000")
}

$lines.Add("call_flm {UnInit erase} $(Format-Hex32 $unInitEntry) 0x00000001 0x00000000 0x00000000 0x00000000")
$lines.Add("call_flm {Init program} $(Format-Hex32 $initEntry) $(Format-Hex32 $imageInfo.Start) 0x00000000 0x00000002 0x00000000")

foreach ($offset in $programPages) {
    $flashAddress = [uint32]($imageInfo.Start + $offset)
    $pagePath = $programPageFiles[[int]$offset]
    $lines.Add("load_image {$(ConvertTo-TclPath $pagePath)} $(Format-Hex32 $bufferBase) bin")
    $lines.Add("call_flm {ProgramPage $(Format-Hex32 $flashAddress)} $(Format-Hex32 $programPageEntry) $(Format-Hex32 $flashAddress) $(Format-Hex32 $PageSize) $(Format-Hex32 $bufferBase) 0x00000000")
}

$lines.Add("call_flm {UnInit program} $(Format-Hex32 $unInitEntry) 0x00000002 0x00000000 0x00000000 0x00000000")
$lines.Add("echo {Dumping APP image for host-side verify}")
$lines.Add("dump_image {$(ConvertTo-TclPath $postApp)} $(Format-Hex32 $imageInfo.Start) $(Format-Hex32 $imageInfo.Length)")
$lines.Add("echo {Dumping bootloader after flashing}")
$lines.Add("dump_image {$(ConvertTo-TclPath $postBoot)} 0x00000000 0x00004000")

if ($NoReset) {
    $lines.Add("halt")
} else {
    $lines.Add("reset run")
}

$lines.Add("shutdown")

[System.IO.File]::WriteAllLines($openOcdTcl, $lines, [System.Text.UTF8Encoding]::new($false))

Write-Host "OpenOCD: $OpenOcd"
Write-Host "Image:   $Image"
Write-Host "APP BIN: $appBin"
Write-Host ("Range:   {0} - {1} ({2} bytes), sectors={3}, program_pages={4}" -f (Format-Hex32 $imageInfo.Start), (Format-Hex32 $imageInfo.End), $imageInfo.Length, $sectorStarts.Count, $programPages.Count)
Write-Host "TCL:     $openOcdTcl"
Write-Host "Mode:    ST-LINK HLA SWD at $AdapterSpeedKHz kHz; no ST-LINK firmware update"

$openOcdArgs = @(
    "-s", $OpenOcdScripts,
    "-f", "interface/stlink-hla.cfg",
    "-c", "transport select hla_swd",
    "-c", "set CHIPNAME hc32f460",
    "-c", "source [find target/stm32f4x.cfg]",
    "-c", "adapter speed $AdapterSpeedKHz"
)
if ($ConnectUnderReset) {
    $openOcdArgs += @("-c", "reset_config srst_only srst_nogate connect_assert_srst")
}
$openOcdArgs += @("-f", $openOcdTcl)

& $OpenOcd @openOcdArgs
if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD flashing failed with exit code $LASTEXITCODE"
}

$preBytes = [System.IO.File]::ReadAllBytes($preBoot)
$postBytes = [System.IO.File]::ReadAllBytes($postBoot)
$expectedAppBytes = [System.IO.File]::ReadAllBytes($appBin)
$actualAppBytes = [System.IO.File]::ReadAllBytes($postApp)

if ($expectedAppBytes.Length -ne $actualAppBytes.Length) {
    throw "APP compare failed: expected length $($expectedAppBytes.Length), actual length $($actualAppBytes.Length)"
}

for ($index = 0; $index -lt $expectedAppBytes.Length; $index++) {
    if ($expectedAppBytes[$index] -ne $actualAppBytes[$index]) {
        throw ("APP compare failed at flash address 0x{0:X8}: expected=0x{1:X2}, actual=0x{2:X2}" -f ($AppBase + $index), $expectedAppBytes[$index], $actualAppBytes[$index])
    }
}

if ($preBytes.Length -ne $postBytes.Length) {
    throw "Bootloader compare failed: before length $($preBytes.Length), after length $($postBytes.Length)"
}

for ($index = 0; $index -lt $preBytes.Length; $index++) {
    if ($preBytes[$index] -ne $postBytes[$index]) {
        throw ("Bootloader compare failed at offset 0x{0:X}: before=0x{1:X2}, after=0x{2:X2}" -f $index, $preBytes[$index], $postBytes[$index])
    }
}

Copy-Item -LiteralPath $appBin -Destination $projectAppBin -Force
Copy-Item -LiteralPath $preBoot -Destination $projectPreBoot -Force
Copy-Item -LiteralPath $postBoot -Destination $projectPostBoot -Force
Copy-Item -LiteralPath $postApp -Destination $projectPostApp -Force

Write-Host ""
Write-Host ("APP verify passed at {0}; bootloader region 0x00000000-0x00003FFF unchanged." -f (Format-Hex32 $AppBase)) -ForegroundColor Green
Write-Host "Project APP BIN: $projectAppBin"
Write-Host "Pre-boot dump:   $projectPreBoot"
Write-Host "Post-boot dump:  $projectPostBoot"
Write-Host "Post-APP dump:   $projectPostApp"
