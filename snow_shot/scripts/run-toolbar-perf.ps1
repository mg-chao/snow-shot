param(
    [string]$QtBin = "",
    [string]$OutputDirectory = "",
    [string]$Baseline = "",
    [string]$Scenario = "*",
    [int]$ScreenIndex = 0,
    [int]$Warmups = 8,
    [int]$Samples = 40,
    [int]$CountdownSeconds = 5,
    [switch]$ForceCompare,
    [switch]$NoGate,
    [switch]$ListScenarios,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

$shot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$workspace = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDirectory = Join-Path $shot "build\toolbar-perf\$stamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

if ($Warmups -lt 0 -or $Samples -le 0) {
    throw "Warmups must be nonnegative and Samples must be positive"
}
if ($ScreenIndex -lt 0) {
    throw "ScreenIndex must be nonnegative"
}
if (!(Test-Path (Join-Path $QtBin "Qt6Core.dll"))) {
    throw "Qt runtime not found in QtBin: $QtBin"
}
if (![string]::IsNullOrWhiteSpace($Baseline)) {
    $Baseline = (Resolve-Path $Baseline).Path
}

Push-Location $workspace
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File `
        (Join-Path $shot "scripts\configure-msvc-perf.ps1") -Fresh
    if ($LASTEXITCODE -ne 0) {
        throw "The msvc-perf configuration failed"
    }

    & cmake --build build/windows-msvc-performance --config Release --target `
        snow-shot-screenshot-toolbar-render-benchmark --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "The toolbar benchmark target failed to build"
    }

    $release = Join-Path $workspace "build\windows-msvc-performance\snow_shot\test-bin\Release"
    $executable = Join-Path $release "snow-shot-screenshot-toolbar-render-benchmark.exe"
    if (!(Test-Path $executable)) {
        throw "Benchmark executable was not produced: $executable"
    }

    $qtRoot = Split-Path $QtBin -Parent
    $savedPath = $env:PATH
    $savedPlatform = $env:QT_QPA_PLATFORM
    $savedPluginPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
    $savedCommit = $env:SNOW_SHOT_PERF_GIT_COMMIT
    $savedDirty = $env:SNOW_SHOT_PERF_GIT_DIRTY
    $savedGpuDriver = $env:SNOW_SHOT_PERF_GPU_DRIVER
    $savedPowerPlan = $env:SNOW_SHOT_PERF_POWER_PLAN
    $env:PATH = "$QtBin;$env:PATH"
    $env:QT_QPA_PLATFORM = "windows"
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $qtRoot "plugins\platforms"
    $env:SNOW_SHOT_PERF_GIT_COMMIT = (& git rev-parse HEAD).Trim()
    $env:SNOW_SHOT_PERF_GIT_DIRTY = if ([string]::IsNullOrWhiteSpace((& git status --porcelain))) { "0" } else { "1" }
    try {
        $gpuDrivers = Get-CimInstance Win32_VideoController |
            ForEach-Object { "$($_.Name):$($_.DriverVersion)" }
        $env:SNOW_SHOT_PERF_GPU_DRIVER = $gpuDrivers -join " | "
    }
    catch {
        $env:SNOW_SHOT_PERF_GPU_DRIVER = "unavailable"
    }
    try {
        $env:SNOW_SHOT_PERF_POWER_PLAN = (& powercfg /getactivescheme) -join " "
    }
    catch {
        $env:SNOW_SHOT_PERF_POWER_PLAN = "unavailable"
    }

    Add-Type -AssemblyName System.Windows.Forms
    $cursorPosition = [System.Windows.Forms.Cursor]::Position
    try {
        $arguments = @(
            "--output", $OutputDirectory,
            "--scenario", $Scenario,
            "--screen-index", $ScreenIndex.ToString(),
            "--warmups", $Warmups.ToString(),
            "--samples", $Samples.ToString()
        )
        if (![string]::IsNullOrWhiteSpace($Baseline)) {
            $arguments += @("--baseline", $Baseline)
        }
        if ($ForceCompare) { $arguments += "--force-compare" }
        if ($NoGate) { $arguments += "--no-gate" }
        if ($ListScenarios) { $arguments += "--list-scenarios" }
        if ($SelfTest) { $arguments += "--self-test" }

        if (!$ListScenarios -and !$SelfTest) {
            Write-Warning "This native benchmark creates always-on-top windows and controls the real mouse pointer; do not use the workstation until it finishes"
            for ($remaining = $CountdownSeconds; $remaining -gt 0; --$remaining) {
                Write-Host "Starting toolbar benchmark in $remaining"
                Start-Sleep -Seconds 1
            }
        }

        New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
        & $executable @arguments
        $benchmarkExitCode = $LASTEXITCODE
    }
    finally {
        [System.Windows.Forms.Cursor]::Position = $cursorPosition
        $env:PATH = $savedPath
        $env:QT_QPA_PLATFORM = $savedPlatform
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedPluginPath
        $env:SNOW_SHOT_PERF_GIT_COMMIT = $savedCommit
        $env:SNOW_SHOT_PERF_GIT_DIRTY = $savedDirty
        $env:SNOW_SHOT_PERF_GPU_DRIVER = $savedGpuDriver
        $env:SNOW_SHOT_PERF_POWER_PLAN = $savedPowerPlan
    }
}
finally {
    Pop-Location
}

if (!$ListScenarios -and !$SelfTest) {
    Write-Host "Toolbar performance JSON: $(Join-Path $OutputDirectory 'report.json')"
    Write-Host "Toolbar performance HTML: $(Join-Path $OutputDirectory 'report.html')"
}
exit $benchmarkExitCode
