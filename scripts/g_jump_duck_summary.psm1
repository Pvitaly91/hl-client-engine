#requires -Version 5.1
Set-StrictMode -Version Latest

function Get-GJumpDuckNativeStatusKeys {
    @('jump-duck-result', 'jump-observed', 'descent-observed',
      'duck-observed', 'release-response-observed',
      'jump-new-submitted', 'duck-new-submitted')
}

function Assert-GJumpDuckNativeSummary {
    param([Parameter(Mandatory = $true)]
          [Collections.Generic.Dictionary[string, string]]$Values)
    foreach ($entry in ([ordered]@{
            'jump-duck-result' = 'verified'
            'jump-observed' = 'true'
            'descent-observed' = 'true'
            'duck-observed' = 'true'
            'release-response-observed' = 'true'
        }).GetEnumerator()) {
        if (-not $Values.ContainsKey($entry.Key) -or
            [string]$Values[$entry.Key] -cne $entry.Value) {
            throw "G native summary disagrees at $($entry.Key)."
        }
    }
    foreach ($key in @('jump-new-submitted', 'duck-new-submitted')) {
        [Int64]$count = 0
        if (-not $Values.ContainsKey($key) -or
            -not [Int64]::TryParse([string]$Values[$key], [ref]$count) -or
            $count -le 0) {
            throw "G native summary has no positive $key."
        }
    }
}

function Assert-GJumpDuckStagedSummary {
    param([Parameter(Mandatory = $true)][object]$Summary)
    if ([string]$Summary.jump_duck_result -cne 'verified' -or
        $Summary.jump_observed -ne $true -or
        $Summary.descent_observed -ne $true -or
        $Summary.duck_observed -ne $true -or
        $Summary.release_response_observed -ne $true -or
        [Int64]$Summary.jump_new_submitted -le 0 -or
        [Int64]$Summary.duck_new_submitted -le 0) {
        throw 'G staged server-observation summary is incomplete.'
    }
}

function Assert-GJumpDuckStagedContract {
    param([Parameter(Mandatory = $true)][object]$Summary)
    $outcome = [string]$Summary.jump_duck_result
    if (@('not_evaluated', 'verified', 'jump_verified_duck_pending',
          'buttons_transmitted_server_effect_unverified',
          'observation_context_blocked', 'environment_limited') -cnotcontains
        $outcome) {
        throw 'G staged result is outside the declared outcome set.'
    }
    foreach ($key in @('jump_new_submitted', 'duck_new_submitted')) {
        if ($null -ne $Summary.$key -and [Int64]$Summary.$key -lt 0) {
            throw "G staged $key is negative."
        }
    }
    if ($outcome -ceq 'verified') {
        Assert-GJumpDuckStagedSummary -Summary $Summary
    }
}

Export-ModuleMember -Function Get-GJumpDuckNativeStatusKeys, `
    Assert-GJumpDuckNativeSummary, Assert-GJumpDuckStagedContract, `
    Assert-GJumpDuckStagedSummary
