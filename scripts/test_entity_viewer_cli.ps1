[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ToolPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string] $ForbiddenOption
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$arguments = @(
    '--basedir', 'parser-only',
    '--game', 'valve',
    '--map', 'maps/parser_only.bsp',
    '--sprite', 'sprites/parser_only.spr',
    '--fixture', 'sprite',
    '--camera', 'static',
    '--' + $ForbiddenOption, 'rejected'
)
$lines = @(& $ToolPath @arguments 2>&1 | ForEach-Object { $_.ToString() })
if ($LASTEXITCODE -ne 2) {
    throw "Forbidden option '$ForbiddenOption' returned exit $LASTEXITCODE; expected parser rejection 2."
}
if ($lines -notmatch '^Usage: hlclient_entity_viewer ') {
    throw "Forbidden option '$ForbiddenOption' did not produce usage-only rejection."
}
if (($lines -join "`n") -match 'parser_only') {
    throw 'Entity viewer parser rejection disclosed an untrusted argument.'
}
