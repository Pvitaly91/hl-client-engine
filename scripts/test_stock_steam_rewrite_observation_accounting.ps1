#requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True {
    param([bool]$Value, [string]$Message)
    if (-not $Value) { throw $Message }
}

function Get-VerifiedRewriteAccounting {
    param([object[]]$Observations)
    $runIds = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    $verified = 0
    $safeNoRewrite = 0
    $conflicts = 0
    $unverifiable = 0
    foreach ($observation in $Observations) {
        if ([string]$observation.outcome -ceq 'safe_no_rewrite') {
            ++$safeNoRewrite
            continue
        }
        if ([string]$observation.outcome -ceq 'conflict') {
            ++$conflicts
            continue
        }
        $valid = [string]$observation.run_id -cmatch '^[0-9a-f]{32}$' -and
            $runIds.Add([string]$observation.run_id) -and
            [bool]$observation.provenance_complete -and
            [bool]$observation.before_after_complete -and
            [bool]$observation.exact_changed_path_set -and
            [bool]$observation.protected_projection_match -and
            [int]$observation.unknown_changes -eq 0 -and
            [int]$observation.fatal_changes -eq 0 -and
            [int]$observation.non_monotonic_changes -eq 0 -and
            [bool]$observation.restoration_exact -and
            [bool]$observation.cleanup_exact -and
            [bool]$observation.ps7_replay_agrees -and
            [bool]$observation.ps51_replay_agrees
        if ($valid) { ++$verified } else { ++$unverifiable }
    }
    $thresholdReached = $verified -ge 5
    [pscustomobject]@{
        verified = $verified
        safe_no_rewrite = $safeNoRewrite
        conflicts = $conflicts
        unverifiable = $unverifiable
        threshold_reached = $thresholdReached
        promotion_allowed = $thresholdReached -and $conflicts -eq 0
    }
}

function New-VerifiedObservation {
    param([string]$RunId)
    [pscustomobject]@{
        run_id = $RunId
        outcome = 'matching_rewrite'
        provenance_complete = $true
        before_after_complete = $true
        exact_changed_path_set = $true
        protected_projection_match = $true
        unknown_changes = 0
        fatal_changes = 0
        non_monotonic_changes = 0
        restoration_exact = $true
        cleanup_exact = $true
        ps7_replay_agrees = $true
        ps51_replay_agrees = $true
    }
}

$three = @(
    (New-VerifiedObservation ('1' * 32)),
    (New-VerifiedObservation ('2' * 32)),
    (New-VerifiedObservation ('3' * 32)))
$pending = Get-VerifiedRewriteAccounting $three
Assert-True ($pending.verified -eq 3 -and -not $pending.threshold_reached -and
    -not $pending.promotion_allowed) `
    'Pending threshold activated production policy.'

$five = @($three) + @(
    (New-VerifiedObservation ('4' * 32)),
    (New-VerifiedObservation ('5' * 32)))
$reached = Get-VerifiedRewriteAccounting $five
Assert-True ($reached.verified -eq 5 -and $reached.threshold_reached -and
    $reached.promotion_allowed) `
    'Three existing plus two distinct verified rewrites did not reach threshold.'

$duplicateReplay = Get-VerifiedRewriteAccounting (@($five) + @(
    (New-VerifiedObservation ('5' * 32))))
Assert-True ($duplicateReplay.verified -eq 5 -and
    $duplicateReplay.unverifiable -eq 1) `
    'Repeated replay increased the distinct observation count.'

$safe = [pscustomobject]@{ outcome = 'safe_no_rewrite'; run_id = ('6' * 32) }
$withSafe = Get-VerifiedRewriteAccounting (@($three) + @($safe))
Assert-True ($withSafe.verified -eq 3 -and $withSafe.safe_no_rewrite -eq 1) `
    'Safe no-rewrite changed or reset the verified count.'

$aggregateOnly = New-VerifiedObservation ('7' * 32)
$aggregateOnly.before_after_complete = $false
$unverifiable = Get-VerifiedRewriteAccounting (@($three) + @($aggregateOnly))
Assert-True ($unverifiable.verified -eq 3 -and
    $unverifiable.unverifiable -eq 1) `
    'Unverifiable aggregate was counted as an observation.'

$conflict = [pscustomobject]@{ outcome = 'conflict'; run_id = ('8' * 32) }
$blocked = Get-VerifiedRewriteAccounting (@($five) + @($conflict))
Assert-True ($blocked.threshold_reached -and -not $blocked.promotion_allowed -and
    $blocked.conflicts -eq 1) 'Conflict did not block promotion.'

$public = @(
    "verified=$($blocked.verified)",
    "safe-no-rewrite=$($withSafe.safe_no_rewrite)",
    "conflicts=$($blocked.conflicts)",
    "promotion=$($blocked.promotion_allowed.ToString().ToLowerInvariant())")
Assert-True (($public -join "`n") -cnotmatch
    '(?i)(steamid|account|token|cookie|localconfig|userdata|[A-F0-9]{64})') `
    'Public accounting output contains private replay material.'

Write-Output '[steam-rewrite-accounting-test] initial-verified=3'
Write-Output '[steam-rewrite-accounting-test] distinct-new-verified=2'
Write-Output '[steam-rewrite-accounting-test] duplicate-replay-increment=0'
Write-Output '[steam-rewrite-accounting-test] safe-no-rewrite-preserves-count=true'
Write-Output '[steam-rewrite-accounting-test] conflict-blocks-promotion=true'
Write-Output '[steam-rewrite-accounting-test] private-material=absent'
Write-Output '[steam-rewrite-accounting-test] result=success'
