[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ToolPath,

    [Parameter(Mandatory = $true)]
    [string]$Basedir,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Game,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]]$Maps,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]]$Scenarios
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-ExistingLeaf([string]$Path, [string]$Label) {
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
        throw "$Label must identify an existing file"
    }
    return $resolved.Path
}

function Resolve-ExistingContainer([string]$Path, [string]$Label) {
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not (Test-Path -LiteralPath $resolved.Path -PathType Container)) {
        throw "$Label must identify an existing directory"
    }
    return $resolved.Path.TrimEnd([System.IO.Path]::DirectorySeparatorChar)
}

function Get-RootInventory([string]$Root) {
    $inventory = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($file in Get-ChildItem -LiteralPath $Root -File -Recurse -Force) {
        $relative = [System.IO.Path]::GetRelativePath($Root, $file.FullName)
        $value = '{0}|{1}' -f $file.Length, $file.LastWriteTimeUtc.Ticks
        $inventory.Add($relative, $value)
    }
    return $inventory
}

function Get-MapSnapshot(
    [string]$Root,
    [string]$GameDirectory,
    [string[]]$VirtualMaps
) {
    $snapshot = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::Ordinal)
    foreach ($virtualMap in $VirtualMaps) {
        if ([System.IO.Path]::IsPathRooted($virtualMap) -or
            $virtualMap.Contains('..') -or
            -not $virtualMap.EndsWith('.bsp', [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe virtual map: $virtualMap"
        }
        $relative = $virtualMap.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
        $mapPath = Join-Path (Join-Path $Root $GameDirectory) $relative
        $resolvedMap = Resolve-ExistingLeaf $mapPath 'Map'
        $rootPrefix = $Root + [System.IO.Path]::DirectorySeparatorChar
        if (-not $resolvedMap.StartsWith(
                $rootPrefix,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Resolved map escaped basedir: $virtualMap"
        }
        $item = Get-Item -LiteralPath $resolvedMap
        $hash = (Get-FileHash -LiteralPath $resolvedMap -Algorithm SHA256).Hash
        $snapshot.Add(
            $virtualMap,
            ('{0}|{1}|{2}' -f $item.Length, $item.LastWriteTimeUtc.Ticks, $hash))
    }
    return $snapshot
}

function Assert-DictionaryEqual($Before, $After, [string]$Label) {
    if ($Before.Count -ne $After.Count) {
        throw "$Label count changed: $($Before.Count) -> $($After.Count)"
    }
    foreach ($entry in $Before.GetEnumerator()) {
        if (-not $After.ContainsKey($entry.Key) -or
            $After[$entry.Key] -cne $entry.Value) {
            throw "$Label changed: $($entry.Key)"
        }
    }
}

function Get-SummaryValue([string[]]$Lines, [string]$Name) {
    $prefix = "[movement] $Name="
    $matches = @($Lines | Where-Object { $_.StartsWith($prefix, [System.StringComparison]::Ordinal) })
    if ($matches.Count -ne 1) {
        throw "Expected one $Name summary, received $($matches.Count)"
    }
    return $matches[0].Substring($prefix.Length)
}

function Test-RequiresGroundedFinalState([string]$Scenario) {
    return $Scenario -ceq 'spawn-settle' -or
        $Scenario -ceq 'walk-forward' -or
        $Scenario -ceq 'jump' -or
        $Scenario -ceq 'deterministic-route'
}

$resolvedTool = Resolve-ExistingLeaf $ToolPath 'ToolPath'
$resolvedBasedir = Resolve-ExistingContainer $Basedir 'Basedir'
$beforeInventory = Get-RootInventory $resolvedBasedir
$beforeMaps = Get-MapSnapshot $resolvedBasedir $Game $Maps

$runCount = 0
foreach ($virtualMap in $Maps) {
    foreach ($scenario in $Scenarios) {
        $outputs = @()
        for ($pass = 0; $pass -lt 2; ++$pass) {
            $lines = @(& $resolvedTool `
                --basedir $resolvedBasedir `
                --game $Game `
                --map $virtualMap `
                --scenario $scenario 2>&1)
            if ($LASTEXITCODE -ne 0) {
                throw "Movement checker failed for scenario ordinal $runCount pass $pass`n$($lines -join "`n")"
            }
            if ((Get-SummaryValue $lines 'result') -cne 'success') {
                throw "Movement checker did not report success"
            }
            if ((Get-SummaryValue $lines 'network-operations') -cne '0') {
                throw "Movement checker reported a network operation"
            }
            if ((Get-SummaryValue $lines 'startsolid') -cne '0' -or
                (Get-SummaryValue $lines 'allsolid') -cne '0') {
                throw "Successful route reported a solid-start state"
            }
            $grounded = Get-SummaryValue $lines 'grounded'
            if ((Test-RequiresGroundedFinalState $scenario) -and
                $grounded -cne 'true') {
                throw "Required movement scenario did not finish grounded"
            }
            $outputs += ,@{
                Hash = Get-SummaryValue $lines 'final-state-hash'
                Commands = Get-SummaryValue $lines 'commands'
                Grounded = $grounded
                Hull = Get-SummaryValue $lines 'hull'
            }
        }
        if ($outputs[0].Hash -cne $outputs[1].Hash -or
            $outputs[0].Commands -cne $outputs[1].Commands -or
            $outputs[0].Grounded -cne $outputs[1].Grounded -or
            $outputs[0].Hull -cne $outputs[1].Hull) {
            throw "Deterministic summary mismatch at scenario ordinal $runCount"
        }
        Write-Output (
            '[movement-verify] scenario-ordinal={0} commands={1} final-state-hash={2} result=success' -f
                $runCount, $outputs[0].Commands, $outputs[0].Hash)
        ++$runCount
    }
}

$afterMaps = Get-MapSnapshot $resolvedBasedir $Game $Maps
$afterInventory = Get-RootInventory $resolvedBasedir
Assert-DictionaryEqual $beforeMaps $afterMaps 'BSP snapshot'
Assert-DictionaryEqual $beforeInventory $afterInventory 'Root inventory'

Write-Output "[movement-verify] runs=$runCount"
Write-Output '[movement-verify] created-files=0'
Write-Output '[movement-verify] deleted-files=0'
Write-Output '[movement-verify] network-operations=0'
Write-Output '[movement-verify] external-file-drift=none'
Write-Output '[movement-verify] result=success'
