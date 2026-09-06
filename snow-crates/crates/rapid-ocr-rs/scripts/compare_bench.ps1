param(
    [Parameter(Mandatory = $true)]
    [string]$Before,
    [Parameter(Mandatory = $true)]
    [string]$After,
    [double]$ThresholdPercent = 5.0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-Json([string]$Path) {
    if (-not (Test-Path $Path)) {
        throw "missing file: $Path"
    }
    return Get-Content $Path -Raw | ConvertFrom-Json
}

function Get-Metric([object]$Doc, [string]$Metric, [string]$Percentile) {
    $value = $Doc.stats.$Metric.$Percentile
    if ($null -eq $value) {
        throw "missing metric: stats.$Metric.$Percentile"
    }
    return [double]$value
}

function Percent-Delta([double]$Before, [double]$After) {
    if ($Before -eq 0) {
        return 0.0
    }
    return (($After - $Before) / $Before) * 100.0
}

$beforeDoc = Read-Json $Before
$afterDoc = Read-Json $After

$targets = @(
    @{ Metric = "wall_ms"; Percentile = "p50" },
    @{ Metric = "wall_ms"; Percentile = "p90" },
    @{ Metric = "e2e_ms"; Percentile = "p50" },
    @{ Metric = "e2e_ms"; Percentile = "p90" }
)

$failed = $false
foreach ($target in $targets) {
    $metric = $target.Metric
    $pct = $target.Percentile
    $beforeVal = Get-Metric $beforeDoc $metric $pct
    $afterVal = Get-Metric $afterDoc $metric $pct
    $delta = Percent-Delta $beforeVal $afterVal
    $deltaText = "{0:N2}" -f $delta

    Write-Host "${metric}.${pct}: before=$beforeVal after=$afterVal delta=$deltaText%"
    if ($delta -gt $ThresholdPercent) {
        Write-Error "$metric.$pct regressed by $deltaText% (> $ThresholdPercent%)"
        $failed = $true
    }
}

if ($failed) {
    exit 1
}

Write-Host "benchmark comparison passed (threshold: $ThresholdPercent%)"
