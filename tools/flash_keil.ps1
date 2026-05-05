param(
    [string]$Uv4Path = "C:\Keil_v5\UV4\UV4.exe",
    [string]$JLinkPath = "C:\Keil_v5\ARM\Segger\JLink.exe",
    [string]$ProjectPath = "F:\CODE_STUDY\MCU\H7Lttit\H7Lttit\MDK-ARM\H7Lttit.uvprojx",
    [string]$TargetName = "H7Lttit",
    [string]$OutputDir = "F:\CODE_STUDY\MCU\H7Lttit\H7Lttit\MDK-ARM\H7Lttit"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Uv4Path)) {
    throw "UV4.exe not found: $Uv4Path"
}
if (-not (Test-Path -LiteralPath $ProjectPath)) {
    throw "Project not found: $ProjectPath"
}
if (-not (Test-Path -LiteralPath $JLinkPath)) {
    throw "JLink.exe not found: $JLinkPath"
}

$bootHexPath = Join-Path $OutputDir "hex_regions\ER_BOOT"
if (-not (Test-Path -LiteralPath $bootHexPath)) {
    throw "Missing internal boot HEX: $bootHexPath"
}

Write-Host "[flash_keil] project: $ProjectPath"
$process = Start-Process `
    -FilePath $Uv4Path `
    -ArgumentList @("-f", $ProjectPath, "-t", $TargetName, "-j0") `
    -Wait `
    -PassThru `
    -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    throw "MDK flash failed with exit code $($process.ExitCode)"
}

$bootHexTempPath = Join-Path $env:TEMP "h7lttit_boot.hex"
$commandFile = Join-Path $env:TEMP "h7lttit_flash_boot.jlink"
Copy-Item -LiteralPath $bootHexPath -Destination $bootHexTempPath -Force
@"
device STM32H750VB
si SWD
speed 4000
connect
r
h
loadfile $bootHexTempPath
r
g
qc
"@ | Set-Content -LiteralPath $commandFile -Encoding ASCII

try {
    Write-Host "[flash_keil] internal boot: $bootHexPath"
    $jlinkOutput = & $JLinkPath -CommandFile $commandFile 2>&1
    $jlinkOutput | Write-Host
    if ($LASTEXITCODE -ne 0) {
        throw "J-Link internal boot flash failed with exit code $LASTEXITCODE"
    }
    if (($jlinkOutput -match "\*\*\*\*\*\* Error") -or
        ($jlinkOutput -match "Unknown command") -or
        ($jlinkOutput -match "unknown / supported format")) {
        throw "J-Link internal boot flash reported an error"
    }
}
finally {
    Remove-Item -LiteralPath $commandFile -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $bootHexTempPath -Force -ErrorAction SilentlyContinue
}

Write-Host "[flash_keil] ok"
