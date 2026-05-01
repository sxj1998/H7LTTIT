param(
    [string]$Uv4Path = "C:\Keil_v5\UV4\UV4.exe",
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

Write-Host "[build] project: $ProjectPath"
$process = Start-Process `
    -FilePath $Uv4Path `
    -ArgumentList @("-b", $ProjectPath, "-t", $TargetName, "-j0") `
    -Wait `
    -PassThru `
    -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    throw "MDK build failed with exit code $($process.ExitCode)"
}

$axfPath = Join-Path $OutputDir "$TargetName.axf"
$hexPath = Join-Path $OutputDir "$TargetName.hex"

if (-not (Test-Path -LiteralPath $axfPath)) {
    throw "Missing AXF output: $axfPath"
}
if (-not (Test-Path -LiteralPath $hexPath)) {
    throw "Missing HEX output: $hexPath"
}

Write-Host "[build] ok"
Write-Host "[build] axf: $axfPath"
Write-Host "[build] hex: $hexPath"
