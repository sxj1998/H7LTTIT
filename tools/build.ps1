param(
    [string]$Uv4Path = "C:\Keil_v5\UV4\UV4.exe",
    [string]$ArmLinkPath = "C:\Keil_v5\ARM\ARMCLANG\bin\armlink.exe",
    [string]$FromElfPath = "C:\Keil_v5\ARM\ARMCLANG\bin\fromelf.exe",
    [string]$ProjectPath = "F:\CODE_STUDY\MCU\H7Lttit\H7Lttit\MDK-ARM\H7Lttit.uvprojx",
    [string]$TargetName = "H7Lttit",
    [string]$OutputDir = "F:\CODE_STUDY\MCU\H7Lttit\H7Lttit\MDK-ARM\H7Lttit",
    [string]$ScatterPath = "F:\CODE_STUDY\MCU\H7Lttit\H7Lttit\MDK-ARM\H7Lttit_boot.sct"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Uv4Path)) {
    throw "UV4.exe not found: $Uv4Path"
}
if (-not (Test-Path -LiteralPath $ProjectPath)) {
    throw "Project not found: $ProjectPath"
}
if (-not (Test-Path -LiteralPath $ArmLinkPath)) {
    throw "armlink.exe not found: $ArmLinkPath"
}
if (-not (Test-Path -LiteralPath $FromElfPath)) {
    throw "fromelf.exe not found: $FromElfPath"
}
if (-not (Test-Path -LiteralPath $ScatterPath)) {
    throw "Scatter file not found: $ScatterPath"
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
$lnpPath = Join-Path $OutputDir "$TargetName.lnp"
$generatedScatterPath = Join-Path $OutputDir "$TargetName.sct"
$regionsDir = Join-Path $OutputDir "hex_regions"
$projectDir = Split-Path -Parent $ProjectPath

if (-not (Test-Path -LiteralPath $lnpPath)) {
    throw "Missing linker response file: $lnpPath"
}

Copy-Item -LiteralPath $ScatterPath -Destination $generatedScatterPath -Force

Write-Host "[build] relink scatter: $ScatterPath"
$linkProcess = Start-Process `
    -FilePath $ArmLinkPath `
    -ArgumentList @("--via", ".\$TargetName\$TargetName.lnp") `
    -WorkingDirectory $projectDir `
    -Wait `
    -PassThru `
    -WindowStyle Hidden
if ($linkProcess.ExitCode -ne 0) {
    throw "armlink failed with exit code $($linkProcess.ExitCode)"
}

Remove-Item -LiteralPath $hexPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $regionsDir -Recurse -Force -ErrorAction SilentlyContinue

$fromElfProcess = Start-Process `
    -FilePath $FromElfPath `
    -ArgumentList @("--i32", "--output", ".\$TargetName\hex_regions", ".\$TargetName\$TargetName.axf") `
    -WorkingDirectory $projectDir `
    -Wait `
    -PassThru `
    -WindowStyle Hidden
if ($fromElfProcess.ExitCode -ne 0) {
    throw "fromelf failed with exit code $($fromElfProcess.ExitCode)"
}

$hexFiles = Get-ChildItem -LiteralPath $regionsDir -File | Sort-Object Name
if ($hexFiles.Count -eq 0) {
    throw "fromelf generated no HEX region files: $regionsDir"
}

$combined = New-Object System.Collections.Generic.List[string]
foreach ($hexFile in $hexFiles) {
    foreach ($line in Get-Content -LiteralPath $hexFile.FullName) {
        if ($line -ne ":00000001FF") {
            $combined.Add($line)
        }
    }
}
$combined.Add(":00000001FF")
Set-Content -LiteralPath $hexPath -Value $combined -Encoding ASCII

if (-not (Test-Path -LiteralPath $axfPath)) {
    throw "Missing AXF output: $axfPath"
}
if (-not (Test-Path -LiteralPath $hexPath)) {
    throw "Missing HEX output: $hexPath"
}

Write-Host "[build] ok"
Write-Host "[build] axf: $axfPath"
Write-Host "[build] hex: $hexPath"
