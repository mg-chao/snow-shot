[CmdletBinding()]
param([Parameter(ValueFromRemainingArguments = $true)][object[]]$Arguments)
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "../snow_shot/scripts/run-scrolling-perf.ps1") @Arguments
exit $LASTEXITCODE
