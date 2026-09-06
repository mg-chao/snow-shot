param(
    [int]$WarmupFrames = 50,
    [int]$MeasureFrames = 420,
    [int]$Rounds = 4,
    [double]$SampleIntervalMs = 8.0,
    [double]$MaxRegressionPct = 8.0,
    [double]$MinImprovementPct = 3.0,
    [double]$MaxDuplicatePct = 35.0,
    [string]$GuardBaselinePath = "",
    [string]$OutputDir = "target/perf",
    [int]$WindowX = 180,
    [int]$WindowY = 160,
    [int]$WindowWidth = 960,
    [int]$WindowHeight = 540,
    [int]$WindowIntervalMs = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$workloadScript = Join-Path $PSScriptRoot "start_animated_region_window.ps1"
if (-not (Test-Path $workloadScript)) {
    throw "missing workload script: $workloadScript"
}

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $PathValue))
}

$outputPath = Resolve-RepoPath -PathValue $OutputDir
if (-not (Test-Path $outputPath)) {
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
}

$workloadInfoPath = Join-Path $outputPath "gdi_span_single_scan_workload.json"
$optimizedCsv = Join-Path $outputPath "gdi_span_single_scan_opt.csv"
$baselineCsv = Join-Path $outputPath "gdi_span_single_scan_baseline.csv"

foreach ($path in @($workloadInfoPath, $optimizedCsv, $baselineCsv)) {
    if (Test-Path $path) {
        Remove-Item $path -Force
    }
}

$label = "gdi-region-span-single-scan"
$commonBenchArgs = @(
    "--backends", "gdi",
    "--target-label", $label,
    "--warmup", "$WarmupFrames",
    "--frames", "$MeasureFrames",
    "--rounds", "$Rounds",
    "--sample-interval-ms", "$SampleIntervalMs",
    "--regression-metrics", "avg,p50,p95",
    "--max-duplicate-pct", "$MaxDuplicatePct"
)

function Invoke-Benchmark {
    param(
        [Parameter(Mandatory = $true)][string]$RegionCsv,
        [Parameter(Mandatory = $true)][string]$SavePath,
        [string]$BaselinePath = ""
    )

    $args = @($commonBenchArgs + @("--region", $RegionCsv, "--save-baseline", $SavePath))
    if (-not [string]::IsNullOrWhiteSpace($BaselinePath)) {
        $args += @("--baseline", $BaselinePath, "--max-regression-pct", "$MaxRegressionPct")
    }

    Write-Host ("Running: cargo run --release --example benchmark -- {0}" -f ($args -join " "))
    & cargo run --release --example benchmark -- @args
    if ($LASTEXITCODE -ne 0) {
        throw "benchmark command failed with exit code $LASTEXITCODE"
    }
}

function Read-ResultRow {
    param([Parameter(Mandatory = $true)][string]$CsvPath)
    if (-not (Test-Path $CsvPath)) {
        throw "benchmark CSV not found: $CsvPath"
    }
    $rows = Import-Csv -Path $CsvPath
    $row = $rows | Where-Object { $_.backend -eq "gdi" -and $_.target -eq $label } | Select-Object -First 1
    if (-not $row) {
        throw "missing gdi row in $CsvPath for target label '$label'"
    }
    return $row
}

$workloadProcess = $null
try {
    $workloadArgs = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-STA",
        "-File", $workloadScript,
        "-OutputPath", $workloadInfoPath,
        "-X", "$WindowX",
        "-Y", "$WindowY",
        "-Width", "$WindowWidth",
        "-Height", "$WindowHeight",
        "-IntervalMs", "$WindowIntervalMs"
    )
    $workloadProcess = Start-Process -FilePath powershell.exe -ArgumentList $workloadArgs -PassThru

    $deadline = (Get-Date).AddSeconds(20)
    while (-not (Test-Path $workloadInfoPath)) {
        if ($workloadProcess.HasExited) {
            throw "workload process exited early with code $($workloadProcess.ExitCode)"
        }
        if ((Get-Date) -ge $deadline) {
            throw "timed out waiting for workload metadata file: $workloadInfoPath"
        }
        Start-Sleep -Milliseconds 50
    }

    $workloadInfo = Get-Content -Path $workloadInfoPath -Raw | ConvertFrom-Json
    $regionX = [int]$workloadInfo.x
    $regionY = [int]$workloadInfo.y
    $regionWidth = [int]$workloadInfo.width
    $regionHeight = [int]$workloadInfo.height
    if ($regionWidth -le 0 -or $regionHeight -le 0) {
        throw "workload metadata reported non-positive region size: width=$regionWidth height=$regionHeight"
    }
    $regionCsv = "{0},{1},{2},{3}" -f $regionX, $regionY, $regionWidth, $regionHeight
    Write-Host "Benchmark region: $regionCsv"

    $baselineForOptimized = ""
    if (-not [string]::IsNullOrWhiteSpace($GuardBaselinePath)) {
        $resolvedGuardBaseline = Resolve-RepoPath -PathValue $GuardBaselinePath
        if (-not (Test-Path $resolvedGuardBaseline)) {
            throw "guard baseline path does not exist: $resolvedGuardBaseline"
        }
        $baselineForOptimized = $resolvedGuardBaseline
        Write-Host "Using guard baseline: $resolvedGuardBaseline"
    }

    Invoke-Benchmark -RegionCsv $regionCsv -SavePath $optimizedCsv -BaselinePath $baselineForOptimized

    $env:SNOW_CAPTURE_DISABLE_GDI_SPAN_SINGLE_SCAN = "1"
    try {
        Invoke-Benchmark -RegionCsv $regionCsv -SavePath $baselineCsv
    }
    finally {
        Remove-Item Env:SNOW_CAPTURE_DISABLE_GDI_SPAN_SINGLE_SCAN -ErrorAction SilentlyContinue
    }

    $optimized = Read-ResultRow -CsvPath $optimizedCsv
    $baseline = Read-ResultRow -CsvPath $baselineCsv

    $optAvg = [double]$optimized.avg_ms
    $optP50 = [double]$optimized.p50_ms
    $optP95 = [double]$optimized.p95_ms
    $baselineAvg = [double]$baseline.avg_ms
    $baselineP50 = [double]$baseline.p50_ms
    $baselineP95 = [double]$baseline.p95_ms

    if ($baselineP50 -le 0.0) {
        throw "baseline p50 is non-positive, cannot compute improvement"
    }
    if ($baselineAvg -le 0.0) {
        throw "baseline avg is non-positive, cannot compute improvement"
    }

    $avgImprovementPct = (($baselineAvg - $optAvg) / $baselineAvg) * 100.0
    $p50ImprovementPct = (($baselineP50 - $optP50) / $baselineP50) * 100.0
    $p95ImprovementPct = if ($baselineP95 -gt 0.0) {
        (($baselineP95 - $optP95) / $baselineP95) * 100.0
    } else {
        0.0
    }

    Write-Host ("A/B result vs baseline (span-single-scan disabled): avg={0:N2}% p50={1:N2}% p95={2:N2}%" -f $avgImprovementPct, $p50ImprovementPct, $p95ImprovementPct)
    Write-Host "Optimized CSV: $optimizedCsv"
    Write-Host "Baseline CSV:    $baselineCsv"

    if ($p50ImprovementPct -lt $MinImprovementPct) {
        throw ("optimized p50 improvement {0:N2}% is below required threshold {1:N2}%" -f $p50ImprovementPct, $MinImprovementPct)
    }
}
finally {
    if ($workloadProcess -and -not $workloadProcess.HasExited) {
        $null = $workloadProcess.CloseMainWindow()
        Start-Sleep -Milliseconds 300
        if (-not $workloadProcess.HasExited) {
            Stop-Process -Id $workloadProcess.Id -Force
        }
    }
}

