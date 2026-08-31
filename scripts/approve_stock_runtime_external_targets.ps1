#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ReviewRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ConfirmExternalMaterialization,

    [Parameter()]
    [ValidateRange(1, 168)]
    [int]$LifetimeHours,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$ReviewToolPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$approvalPhrase = 'HLCLIENT_APPROVE_REVIEWED_EXTERNAL_TARGETS_V1'
if ($ConfirmExternalMaterialization -cne $approvalPhrase) {
    throw 'ConfirmExternalMaterialization does not exactly match the required approval phrase.'
}

function Resolve-ExternalTargetReviewTool {
    if (-not [string]::IsNullOrWhiteSpace($ReviewToolPath)) {
        if (-not (Test-Path -LiteralPath $ReviewToolPath -PathType Leaf)) {
            throw 'The explicitly selected external-target review helper is unavailable.'
        }
        return [IO.Path]::GetFullPath($ReviewToolPath)
    }

    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    foreach ($candidate in @(
            (Join-Path $repositoryRoot 'build\bin\Debug\hlclient_stock_external_target_review.exe'),
            (Join-Path $repositoryRoot 'build\bin\Release\hlclient_stock_external_target_review.exe'),
            (Join-Path $repositoryRoot 'build-asan\bin\Release\hlclient_stock_external_target_review.exe'))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'Build hlclient_stock_external_target_review before approving a review.'
}

$review = [IO.Path]::GetFullPath($ReviewRoot).TrimEnd('\', '/')
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$requiredReviewParent = [IO.Path]::GetFullPath((Join-Path `
        $repositoryRoot 'manual-artifacts\stock-runtime-source-review')).TrimEnd('\', '/')
if ((Split-Path -Leaf $review) -cnotmatch '^[0-9a-f]{32}$' -or
    -not [string]::Equals(
        (Split-Path -Parent $review), $requiredReviewParent,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'ReviewRoot must be an exact 32-hex child of the repository-local review directory.'
}
$arguments = [Collections.Generic.List[string]]::new()
foreach ($argument in @(
        '--approve', '--review-root', $review,
        '--approval-phrase', $approvalPhrase)) {
    $arguments.Add($argument)
}
if ($PSBoundParameters.ContainsKey('LifetimeHours')) {
    $arguments.Add('--lifetime-hours')
    $arguments.Add($LifetimeHours.ToString(
            [Globalization.CultureInfo]::InvariantCulture))
}

$tool = Resolve-ExternalTargetReviewTool
$lines = @(& $tool @arguments 2>&1 | ForEach-Object { $_.ToString() })
$exitCode = $LASTEXITCODE
if ($lines.Count -gt 64) {
    throw 'The external-target approval helper exceeded its public output line bound.'
}
foreach ($line in $lines) {
    if ($line.Length -gt 512 -or
        $line -cnotmatch '^\[source-review\] [a-z0-9-]+=[A-Za-z0-9_.-]+$') {
        throw 'The external-target approval helper emitted non-bounded public output.'
    }
}
if ($exitCode -ne 0) {
    throw "The external-target approval helper failed with exit code $exitCode."
}

$requiredRecords = @('schema', 'lifetime-hours', 'private-handoff', 'result')
$records = @{}
foreach ($line in $lines) {
    if ($line -cnotmatch '^\[source-review\] ([a-z0-9-]+)=([A-Za-z0-9_.-]+)$') {
        throw 'The external-target approval helper emitted a malformed success record.'
    }
    $name = $Matches[1]
    $value = $Matches[2]
    if ($requiredRecords -cnotcontains $name) {
        throw 'The external-target approval helper emitted a success record outside the approval contract.'
    }
    if ($records.ContainsKey($name)) {
        throw 'The external-target approval helper duplicated a success record.'
    }
    $records[$name] = $value
}
foreach ($name in $requiredRecords) {
    if (-not $records.ContainsKey($name)) {
        throw 'The external-target approval helper omitted a required success record.'
    }
}
if ($records.Count -ne $requiredRecords.Count) {
    throw 'The external-target approval helper returned an inexact success record set.'
}
$expectedLifetime = if ($PSBoundParameters.ContainsKey('LifetimeHours')) {
    $LifetimeHours
} else {
    24
}
if ($records['schema'] -cne
        'hlclient.stock-runtime-external-target-approval.v1' -or
    $records['lifetime-hours'] -cne $expectedLifetime.ToString(
        [Globalization.CultureInfo]::InvariantCulture) -or
    $records['private-handoff'] -cne 'local-only' -or
    $records['result'] -cne 'success') {
    throw 'The external-target approval helper returned an inconsistent approval success contract.'
}
$approvalManifest = Join-Path $review 'external-target-approval.json'
if (-not (Test-Path -LiteralPath $approvalManifest -PathType Leaf)) {
    throw 'The external-target approval helper did not publish its exact local manifest.'
}
$approvalItem = Get-Item -LiteralPath $approvalManifest -Force
if (($approvalItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
    $approvalItem.Length -lt 1 -or $approvalItem.Length -gt 65536) {
    throw 'The external-target approval manifest is not a bounded ordinary file.'
}
Write-Output $lines
