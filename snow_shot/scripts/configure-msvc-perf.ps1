[CmdletBinding()]
param([switch]$Fresh)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

Push-Location $repoRoot
try {
    $arguments = @("--preset", "windows-msvc-performance")
    if ($Fresh) {
        $arguments = @("--fresh") + $arguments
    }
    & cmake @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "The windows-msvc-performance configuration failed."
    }
}
finally {
    Pop-Location
}
