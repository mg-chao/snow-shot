#Requires -Version 7.2
[CmdletBinding()]
param([string]$PackageScript = (Join-Path $PSScriptRoot 'package-snow-shot.ps1'))

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'snow-shot-ocr-release-runtime.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $repoRoot ('build/package-dependency-tests-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testRoot

# Run the packaging entry point's actual import audit without configuring,
# building, staging application artifacts, or executing a target binary.
$parseErrors = $null
$packageAst = [Management.Automation.Language.Parser]::ParseFile(
    $PackageScript, [ref]$null, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'The package script must parse successfully.' }
$statements = @($packageAst.EndBlock.Statements)
$auditText = @()
foreach ($variable in @('$allowedSystemImports', '$allowedLocalImports')) {
    $assignment = @($statements | Where-Object {
        $_ -is [Management.Automation.Language.AssignmentStatementAst] -and
        $_.Left.Extent.Text -ceq $variable
    })
    if ($assignment.Count -ne 1) { throw "Package dependency contract is missing: $variable" }
    $auditText += $assignment[0].Extent.Text
}
$auditIndex = -1
for ($index = 0; $index -lt $statements.Count; $index++) {
    $statement = $statements[$index]
    if ($statement -is [Management.Automation.Language.ForEachStatementAst] -and
        $statement.Variable.Extent.Text -ceq '$binary' -and
        $statement.Condition.Extent.Text -ceq '$stagedBinaries') {
        if ($auditIndex -ne -1) { throw 'Package PE dependency audit must be unique.' }
        $auditIndex = $index
    }
}
if ($auditIndex -eq -1) { throw 'Package PE dependency audit was not found.' }
$auditText += $statements[$auditIndex].Extent.Text
for ($index = $auditIndex + 1; $index -lt $statements.Count -and
    $statements[$index] -is [Management.Automation.Language.IfStatementAst]; $index++) {
    $auditText += $statements[$index].Extent.Text
}
$audit = [scriptblock]::Create($auditText -join "`n")

# Exercise the channel-specific required files and binary allowlist from the
# same packaging script, including their actual failure checks.
$stageAuditText = @()
$stagePolicyText = @()
foreach ($variable in @('$requiredStageFiles', '$missingStageFiles', '$expectedBinaryPaths', '$unexpectedBinaries')) {
    $assignmentIndexes = @(for ($index = 0; $index -lt $statements.Count; $index++) {
        if ($statements[$index] -is [Management.Automation.Language.AssignmentStatementAst] -and
            $statements[$index].Left.Extent.Text -ceq $variable) { $index }
    })
    if ($assignmentIndexes.Count -ne 1) { throw "Package staging contract is missing: $variable" }
    $index = $assignmentIndexes[0]
    $contract = @($statements[$index].Extent.Text)
    if ($statements[$index + 1] -is [Management.Automation.Language.IfStatementAst]) {
        $contract += $statements[$index + 1].Extent.Text
    }
    $stageAuditText += $contract
    if ($variable -in @('$requiredStageFiles', '$expectedBinaryPaths')) { $stagePolicyText += $contract }
}
$stageAudit = [scriptblock]::Create($stageAuditText -join "`n")
$stagePolicy = [scriptblock]::Create(($stagePolicyText -join "`n") +
    "`n[pscustomobject]@{ Required = `$requiredStageFiles; Binaries = `$expectedBinaryPaths }")

$script:DumpbinPath = Join-Path $testRoot 'host-import-inspector.ps1'
[IO.File]::WriteAllText($script:DumpbinPath, @'
param([string]$Nologo, [string]$Dependents, [string]$Image)
Get-Content -LiteralPath ($Image + '.imports')
exit 0
'@ + "`n", [Text.UTF8Encoding]::new($false))

function Test-PackageDependencyCase {
    param([string]$Name, [string]$Architecture, [int]$Machine,
        [string[]]$Imports, [string]$ExpectedFailure, [switch]$MissingSystemFile,
        [string]$BinaryName = 'snow_shot.exe')

    $caseRoot = Join-Path $testRoot $Name
    $null = New-Item -ItemType Directory -Path $caseRoot
    $image = Join-Path $caseRoot $BinaryName
    $bytes = [byte[]]::new(128)
    [BitConverter]::GetBytes([uint16]0x5a4d).CopyTo($bytes, 0)
    [BitConverter]::GetBytes([uint32]64).CopyTo($bytes, 0x3c)
    [BitConverter]::GetBytes([uint32]0x4550).CopyTo($bytes, 64)
    [BitConverter]::GetBytes([uint16]$Machine).CopyTo($bytes, 68)
    [IO.File]::WriteAllBytes($image, $bytes)
    ($Imports | ForEach-Object { "    $_" }) | Set-Content -LiteralPath ($image + '.imports') -Encoding utf8NoBOM
    $stagedBinaries = @(Get-Item -LiteralPath $image)
    $stagedBinDirectory = $caseRoot
    $windowsSystemDirectory = if ($MissingSystemFile) { $caseRoot } else { Join-Path $env:SystemRoot 'System32' }
    $debugRuntimeImports = [Collections.Generic.List[string]]::new()
    $unresolvedImports = [Collections.Generic.List[string]]::new()
    $unexpectedImports = [Collections.Generic.List[string]]::new()
    $failure = $null
    try { & $audit } catch { $failure = $_.Exception.Message }
    if ($ExpectedFailure) {
        if ($null -eq $failure -or $failure -notlike $ExpectedFailure) {
            throw "Package audit failed to reject $Name for the required reason: $failure"
        }
    } elseif ($null -ne $failure) {
        throw "Package audit rejected the valid $Name dependency: $failure"
    }
    Write-Output "PASS: $Name"
}

function Test-PackageStageCase {
    param([string]$Name, [bool]$Store, [string]$ExpectedFailure,
        [switch]$AddExtension, [switch]$MissingExtension)

    $installDirectory = Join-Path $testRoot $Name
    $policy = & $stagePolicy
    foreach ($relative in @($policy.Required) + @($policy.Binaries)) {
        $path = Join-Path $installDirectory $relative
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        [IO.File]::WriteAllText($path, 'staged fixture')
    }
    $extension = Join-Path $installDirectory 'bin\SnowShotExplorerCommand.dll'
    if ($AddExtension) { [IO.File]::WriteAllText($extension, 'extension fixture') }
    if ($MissingExtension -and (Test-Path -LiteralPath $extension -PathType Leaf)) {
        Remove-Item -LiteralPath $extension -Force
    }
    $stagedBinaries = @(Get-ChildItem -LiteralPath $installDirectory -Recurse -File |
        Where-Object { $_.Extension -in @('.exe', '.dll') })
    $failure = $null
    try { & $stageAudit } catch { $failure = $_.Exception.Message }
    if ($ExpectedFailure) {
        if ($null -eq $failure -or $failure -notlike $ExpectedFailure) {
            throw "Package staging audit failed to reject $Name for the required reason: $failure"
        }
    } elseif ($null -ne $failure) {
        throw "Package staging audit rejected the valid $Name payload: $failure"
    }
    Write-Output "PASS: $Name"
}

try {
    Test-PackageStageCase standalone-stage $false
    Test-PackageStageCase store-stage $true
    Test-PackageStageCase standalone-missing-extension $false 'Release staging is missing required runtime files*SnowShotExplorerCommand.dll*' -MissingExtension
    Test-PackageStageCase store-missing-extension $true 'Release staging is missing required runtime files*SnowShotExplorerCommand.dll*' -MissingExtension
    Test-PackageDependencyCase x64-color-management x64 0x8664 @('MSCMS.dll')
    Test-PackageDependencyCase arm64-color-management arm64 0xaa64 @('mscms.dll')
    Test-PackageDependencyCase x64-media-foundation x64 0x8664 @('MF.dll', 'MFPlat.dll', 'MFReadWrite.dll')
    Test-PackageDependencyCase arm64-media-foundation arm64 0xaa64 @('mf.dll', 'mfplat.dll', 'mfreadwrite.dll')
    Test-PackageDependencyCase x64-explorer-command x64 0x8664 @('KERNEL32.dll', 'ole32.dll', 'SHELL32.dll', 'SHLWAPI.dll', 'USER32.dll') -BinaryName 'SnowShotExplorerCommand.dll'
    Test-PackageDependencyCase arm64-explorer-command arm64 0xaa64 @('kernel32.dll', 'OLE32.dll', 'shell32.dll', 'shlwapi.dll', 'user32.dll') -BinaryName 'SnowShotExplorerCommand.dll'
    Test-PackageDependencyCase explorer-command-dynamic-runtime x64 0x8664 @('VCRUNTIME140.dll') 'Release staging imports non-system or disallowed libraries*' -BinaryName 'SnowShotExplorerCommand.dll'
    Test-PackageDependencyCase opposite-architecture arm64 0x8664 @('mscms.dll') 'Expected arm64 PE machine*'
    Test-PackageDependencyCase non-system-runtime arm64 0xaa64 @('snow-private-runtime.dll') 'Release staging imports non-system or disallowed libraries*'
    Test-PackageDependencyCase debug-runtime arm64 0xaa64 @('VCRUNTIME140D.dll') 'Release staging imports debug runtime libraries*'
    Test-PackageDependencyCase missing-system-file arm64 0xaa64 @('mscms.dll') 'Release staging has unresolved PE dependencies*' -MissingSystemFile
    Test-PackageDependencyCase missing-media-foundation-file arm64 0xaa64 @('mfplat.dll') 'Release staging has unresolved PE dependencies*' -MissingSystemFile
} finally {
    $resolvedRoot = [IO.Path]::GetFullPath($testRoot)
    $expectedParent = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build')).TrimEnd('\') + '\'
    if (-not $resolvedRoot.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Unsafe package dependency fixture cleanup.'
    }
    Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
}
