param(
    [string]$JLinkPath = "C:\Keil_v5\ARM\Segger\JLink.exe",
    [string]$CommandFile = "F:\CODE_STUDY\MCU\H7Lttit\tools\flash_stm32h750.jlink"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $JLinkPath)) {
    throw "JLink.exe not found: $JLinkPath"
}
if (-not (Test-Path -LiteralPath $CommandFile)) {
    throw "J-Link command file not found: $CommandFile"
}

Write-Host "[flash] command file: $CommandFile"
& $JLinkPath -CommandFile $CommandFile
if ($LASTEXITCODE -ne 0) {
    throw "J-Link flash failed with exit code $LASTEXITCODE"
}

Write-Host "[flash] ok"
