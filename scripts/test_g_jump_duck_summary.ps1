#requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'g_jump_duck_summary.psm1') `
    -ErrorAction Stop

$gKeys = @(Get-GJumpDuckNativeStatusKeys)
$expectedGKeys = @('jump-duck-result', 'jump-observed',
    'descent-observed', 'duck-observed', 'release-response-observed',
    'jump-new-submitted', 'duck-new-submitted')
if ($gKeys.Count -ne $expectedGKeys.Count -or
    @($expectedGKeys | Where-Object { $gKeys -cnotcontains $_ }).Count -ne 0 -or
    @($gKeys | Select-Object -Unique).Count -ne $gKeys.Count) {
    throw 'G native status allowlist is incomplete or duplicated.'
}

$values = [Collections.Generic.Dictionary[string, string]]::new(
    [StringComparer]::Ordinal)
$values['jump-duck-result'] = 'verified'
foreach ($key in @('jump-observed', 'descent-observed', 'duck-observed',
        'release-response-observed')) {
    $values[$key] = 'true'
}
$values['jump-new-submitted'] = '60'
$values['duck-new-submitted'] = '75'
$nativeLines = foreach ($key in $gKeys) {
    "[stock-runtime-orchestrator] $key=$($values[$key])"
}
$parsed = [Collections.Generic.Dictionary[string, string]]::new(
    [StringComparer]::Ordinal)
foreach ($line in $nativeLines) {
    if ($line -cnotmatch '^\[stock-runtime-orchestrator\] (?<key>[a-z0-9-]+)=(?<value>[A-Za-z0-9_.:/-]{1,128})$' -or
        $gKeys -cnotcontains $Matches.key -or
        $parsed.ContainsKey($Matches.key)) {
        throw 'G producer status line is not admitted by native parser contract.'
    }
    $parsed.Add($Matches.key, $Matches.value)
}
$values = $parsed
$staged = '{"jump_duck_result":"verified","jump_observed":true,' +
    '"descent_observed":true,"duck_observed":true,' +
    '"release_response_observed":true,"jump_new_submitted":60,' +
    '"duck_new_submitted":75}' | ConvertFrom-Json
Assert-GJumpDuckNativeSummary -Values $values
Assert-GJumpDuckStagedSummary -Summary $staged
Assert-GJumpDuckStagedContract -Summary $staged

function Expect-Rejected {
    param([scriptblock]$Check)
    $rejected = $false
    try { & $Check } catch { $rejected = $true }
    if (-not $rejected) { throw 'G summary accepted a partial or failed case.' }
}

$values['jump-duck-result'] = 'jump_verified_duck_pending'
$values['duck-observed'] = 'false'
$staged.jump_duck_result = 'jump_verified_duck_pending'
$staged.duck_observed = $false
Expect-Rejected { Assert-GJumpDuckNativeSummary -Values $values }
Expect-Rejected { Assert-GJumpDuckStagedSummary -Summary $staged }
Assert-GJumpDuckStagedContract -Summary $staged
if ($values['jump-observed'] -cne 'true' -or
    $staged.jump_observed -ne $true) {
    throw 'G partial result lost retained jump observation.'
}

[void]$values.Remove('jump-duck-result')
$staged.jump_duck_result = $null
Expect-Rejected { Assert-GJumpDuckNativeSummary -Values $values }
Expect-Rejected { Assert-GJumpDuckStagedSummary -Summary $staged }
$staged.jump_duck_result = 'not_evaluated'
Assert-GJumpDuckStagedContract -Summary $staged
$staged.jump_duck_result = 'fabricated_success'
Expect-Rejected { Assert-GJumpDuckStagedContract -Summary $staged }
Write-Output '[g-summary-test] native_staged_success_partial_failed=verified'
