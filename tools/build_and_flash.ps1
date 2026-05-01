param(
    [string]$Uv4Path = "C:\Keil_v5\UV4\UV4.exe",
    [string]$ProjectPath = "F:\CODE_STUDY\MCU\H7Lttit\H7Lttit\MDK-ARM\H7Lttit.uvprojx",
    [string]$TargetName = "H7Lttit",
    [string]$OutputDir = "F:\CODE_STUDY\MCU\H7Lttit\H7Lttit\MDK-ARM\H7Lttit",
    [string]$JLinkPath = "C:\Keil_v5\ARM\Segger\JLink.exe",
    [string]$CommandFile = "F:\CODE_STUDY\MCU\H7Lttit\tools\flash_stm32h750.jlink"
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

& (Join-Path $scriptDir "build.ps1") `
    -Uv4Path $Uv4Path `
    -ProjectPath $ProjectPath `
    -TargetName $TargetName `
    -OutputDir $OutputDir

& (Join-Path $scriptDir "flash.ps1") `
    -JLinkPath $JLinkPath `
    -CommandFile $CommandFile

Write-Host "[build_and_flash] done"
