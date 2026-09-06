[CmdletBinding()]
param([Parameter(ValueFromRemainingArguments = $true)][object[]]$Arguments)
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "../snow_shot/scripts/configure-msvc-perf.ps1") @Arguments
exit $LASTEXITCODE
