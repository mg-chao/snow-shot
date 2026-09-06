[CmdletBinding()]
param(
    [switch]$Fix
)

$ErrorActionPreference = "Stop"
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$rustWorkspaces = @(
    (Join-Path $workspaceRoot "snow-crates"),
    (Join-Path $workspaceRoot "snow_draw_engine_qt")
)

foreach ($rustWorkspace in $rustWorkspaces) {
    Push-Location $rustWorkspace
    try {
        if ($Fix) {
            cargo fmt --all
        } else {
            cargo fmt --all -- --check
        }
        if ($LASTEXITCODE -ne 0) {
            throw "rustfmt failed in $rustWorkspace"
        }

        cargo check --workspace --all-targets --all-features
        if ($LASTEXITCODE -ne 0) {
            throw "cargo check failed in $rustWorkspace"
        }

        cargo clippy --workspace --all-targets --all-features -- -D warnings
        if ($LASTEXITCODE -ne 0) {
            throw "Clippy failed in $rustWorkspace"
        }
    } finally {
        Pop-Location
    }
}
