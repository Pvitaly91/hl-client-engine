#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'h1_speed_summary.psm1') -Force

function New-Values([string]$Outcome) {
    $values = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::Ordinal)
    if ($null -ne $Outcome) { $values.Add('speed-result', $Outcome) }
    return $values
}
function Assert-Rejected([scriptblock]$Check) {
    $rejected = $false
    try { & $Check } catch { $rejected = $true }
    if (-not $rejected) { throw 'Expected H1 summary rejection.' }
}

Assert-H1SpeedNativeSummary -Values (New-Values 'verified')
Assert-Rejected { Assert-H1SpeedNativeSummary -Values (
    New-Values 'server_motion_unverified') }
Assert-Rejected { Assert-H1SpeedNativeSummary -Values (
    New-Values $null) }

$success = [pscustomobject]@{
    speed_result = 'verified'
    usercmd_movement_verified = $true
    live_visual_verified = $true
}
Assert-H1SpeedStagedContract -Summary $success
$partial = [pscustomobject]@{
    speed_result = 'server_motion_unverified'
    usercmd_movement_verified = $false
    live_visual_verified = $false
}
Assert-H1SpeedStagedContract -Summary $partial
Assert-Rejected { Assert-H1SpeedStagedContract -Summary (
    [pscustomobject]@{
        speed_result = 'verified'
        usercmd_movement_verified = $false
        live_visual_verified = $true
    }) }
Assert-Rejected { Assert-H1SpeedStagedContract -Summary (
    [pscustomobject]@{
        speed_result = 'invented'
        usercmd_movement_verified = $false
        live_visual_verified = $false
    }) }
Write-Output 'H1 speed producer/consumer summary tests passed.'
