#requires -Version 5.1
Set-StrictMode -Version Latest

function Assert-H1SpeedNativeSummary {
    param([Parameter(Mandatory = $true)]
          [Collections.Generic.Dictionary[string, string]]$Values)
    if (-not $Values.ContainsKey('speed-result') -or
        $Values['speed-result'] -cne 'verified') {
        throw 'H1 native speed result is not verified.'
    }
}

function Assert-H1SpeedStagedContract {
    param([Parameter(Mandatory = $true)][object]$Summary)
    $outcome = [string]$Summary.speed_result
    if (@('not_evaluated', 'verified', 'server_motion_unverified',
          'observation_context_blocked') -cnotcontains $outcome) {
        throw 'H1 staged speed result is outside the declared outcome set.'
    }
    if ($outcome -ceq 'verified' -and
        ($Summary.usercmd_movement_verified -ne $true -or
         $Summary.live_visual_verified -ne $true)) {
        throw 'H1 staged success lacks movement and visual evidence.'
    }
}

Export-ModuleMember -Function Assert-H1SpeedNativeSummary, `
    Assert-H1SpeedStagedContract
