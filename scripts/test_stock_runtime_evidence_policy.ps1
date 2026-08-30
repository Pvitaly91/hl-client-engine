#requires -Version 5.1

<#
.SYNOPSIS
Runs the in-memory stock-runtime committed-evidence policy gate.

.DESCRIPTION
The production verifier owns this validation mode, so this regression proves
the same exact schema and cursor/prefix-width checks reject synthetic malicious
metadata. It creates no capture, evidence, temporary file, socket or process.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$verifier = Join-Path $PSScriptRoot 'verify_stock_runtime_first_observations.ps1'
$output = @(& $verifier -ValidateEvidencePolicy |
    ForEach-Object { $_.ToString() })
$required = @(
    '[stock-runtime-evidence-policy] forbidden-key-rejections=2',
    '[stock-runtime-evidence-policy] cursor-width-rejections=3',
    '[stock-runtime-evidence-policy] candidate-alignment-rejections=1',
    '[stock-runtime-evidence-policy] scenario-binding-rejections=2',
    '[stock-runtime-evidence-policy] map-binding-rejections=1',
    '[stock-runtime-evidence-policy] manifest-counter-binding-rejections=7',
    '[stock-runtime-evidence-policy] source-counter-binding-rejections=3',
    '[stock-runtime-evidence-policy] replay-suppression-binding-acceptances=2',
    '[stock-runtime-evidence-policy] replay-counter-bound-rejections=1',
    '[stock-runtime-evidence-policy] replay-accounting-rejections=2',
    '[stock-runtime-evidence-policy] hash-binding-rejections=4',
    '[stock-runtime-evidence-policy] timestamp-binding-rejections=2',
    '[stock-runtime-evidence-policy] candidate-stability-binding-rejections=1',
    '[stock-runtime-evidence-policy] rejected-overflow-candidate-binding-rejections=1',
    '[stock-runtime-evidence-policy] canary-mutation-rejections=4',
    '[stock-runtime-evidence-policy] fatal-resume-category-rejections=5',
    '[stock-runtime-evidence-policy] fatal-resume-state-rejections=1',
    '[stock-runtime-evidence-policy] canary-walker-invocations=2',
    '[stock-runtime-evidence-policy] canary-walker-gate-rejections=3',
    '[stock-runtime-evidence-policy] implementation-commit-chain=exact-message-and-ancestor',
    '[stock-runtime-evidence-policy] failure-publication-mutations=3',
    '[stock-runtime-evidence-policy] campaign-identity-rejections=2',
    '[stock-runtime-evidence-policy] files-written=0',
    '[stock-runtime-evidence-policy] result=success')
foreach ($line in $required) {
    if ($output -cnotcontains $line) {
        throw "Evidence policy gate lacks '$line'."
    }
}
if ($output.Count -ne $required.Count) {
    throw 'Evidence policy gate emitted an unexpected line.'
}

Write-Output '[stock-runtime-evidence-policy-test] result=success'
