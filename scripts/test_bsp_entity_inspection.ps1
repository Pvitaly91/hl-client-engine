#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ToolPath,
    [Parameter(Mandatory)][string]$FixtureWriterPath
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('bsp entity inspection ' + [guid]::NewGuid().ToString('N'))
# Existing project-owned fixture generator; no installed game required. Leave
# scratch data retained rather than deleting any user directory.
& $FixtureWriterPath $scratch
if ($LASTEXITCODE -ne 0) { throw 'Synthetic fixture writer failed.' }
$common = @('--basedir', $scratch, '--game', 'valve', '--map', 'maps/test_movement.bsp',
    '--validate-through', 'geometry')
$plain = @(& $ToolPath @common 2>&1)
if ($LASTEXITCODE -ne 0 -or ($plain -join "`n").Contains('[bsp-entity]')) {
    throw 'Default compatibility output changed.'
}
$first = @(& $ToolPath @common --inspect-entities 2>&1)
if ($LASTEXITCODE -ne 0) { throw 'Synthetic inspection failed.' }
$second = @(& $ToolPath @common --inspect-entities 2>&1)
if ($LASTEXITCODE -ne 0 -or ($first -join "`n") -cne ($second -join "`n")) {
    throw 'Inspection is not deterministic.'
}
foreach ($expected in @(
    '[bsp-entity] index=0 classname=worldspawn initial_origin=unavailable',
    '[bsp-entity] index=1 classname=func_wall initial_origin=-1,0,0 brush_model=1 initial_bounds_min=191,-2049,-513 initial_bounds_max=193,2049,513',
    '[bsp-entity] index=2 classname=info_player_start initial_origin=0,0,36',
    '[bsp-entities] count=3 result=success')) {
    if ($first -cnotcontains $expected) { throw "Missing independent fixture expectation: $expected" }
}
& $ToolPath @common --inspect-entities --inspect-entities 2>&1 | Out-Null
if ($LASTEXITCODE -ne 2) { throw 'Duplicate inspection flag accepted.' }
& $ToolPath --basedir $scratch --game valve --map ../escape.bsp --validate-through geometry --inspect-entities 2>&1 | Out-Null
if ($LASTEXITCODE -ne 1) { throw 'Unsafe virtual map accepted.' }
Write-Output "PASS inspector: unchanged default output, deterministic project-owned entities/origin/brush bounds, duplicate flag and unsafe map rejected. Retained fixture=$scratch. Stock launches=0."
