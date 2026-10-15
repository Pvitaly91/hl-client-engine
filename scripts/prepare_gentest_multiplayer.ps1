#requires -Version 7.0
# User-requested local multiplayer QA copy; original gentest is never changed.
[CmdletBinding()]
param([string]$ResearchHalfLifeRoot = 'D:\DEV\HLCLIENT-RESEARCH\Half-Life',
      [switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'gentest_multiplayer_map.psm1') -Force
$result = New-GentestMultiplayerMap -ResearchRoot $ResearchHalfLifeRoot `
    -RepositoryRoot (Split-Path -Parent $PSScriptRoot) -CheckOnly:$CheckOnly
Write-Output "[gentest-mp] status=$($result.Status) map=$($result.Map) dm_spawns=$($result.DeathmatchSpawnCount)"
Write-Output "[gentest-mp] source_sha256=$($result.SourceSha256) sha256=$($result.Sha256) original_unchanged=$($result.OriginalUnchanged)"
Write-Output '[gentest-mp] game_processes_started=0; Steam/WFP/sockets=not_started'
