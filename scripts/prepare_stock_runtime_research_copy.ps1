#requires -Version 5.1

<#
.SYNOPSIS
Inspects or safely materializes a Half-Life research tree.

.DESCRIPTION
The project-owned Windows helper performs handle-based topology inspection and
copy-by-verified-handle materialization. InspectSourceTopology is strictly
read-only and never resolves, creates, or writes the requested destination.
Materialization publishes a new destination atomically and retains the v1
isolation marker while writing the stricter preparation manifest v2.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SourceHalfLifeRoot,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$DestinationHalfLifeRoot,

    [Parameter()]
    [switch]$InspectSourceTopology,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchCopyToolPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-ResearchCopyTool {
    if (-not [string]::IsNullOrWhiteSpace($ResearchCopyToolPath)) {
        if (-not (Test-Path -LiteralPath $ResearchCopyToolPath -PathType Leaf)) {
            throw 'The explicitly selected research-copy helper is unavailable.'
        }
        return [IO.Path]::GetFullPath($ResearchCopyToolPath)
    }

    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    foreach ($candidate in @(
            (Join-Path $repositoryRoot 'build\bin\Debug\hlclient_stock_research_copy.exe'),
            (Join-Path $repositoryRoot 'build\bin\Release\hlclient_stock_research_copy.exe'),
            (Join-Path $repositoryRoot 'build-asan\bin\Release\hlclient_stock_research_copy.exe'))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw 'Build hlclient_stock_research_copy before running the preparation helper.'
}

function Invoke-BoundedResearchCopyTool {
    param([string[]]$Arguments)

    $tool = Resolve-ResearchCopyTool
    $lines = @(& $tool @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    if ($lines.Count -gt 64) {
        throw 'The research-copy helper exceeded its public output line bound.'
    }
    foreach ($line in $lines) {
        if ($line.Length -gt 512 -or
            $line -cnotmatch '^\[(?:research-copy|stock-runtime-prepare)\] [a-z0-9-]+=[A-Za-z0-9_.-]+$') {
            throw 'The research-copy helper emitted non-bounded public output.'
        }
        Write-Output $line
    }
    if ($exitCode -ne 0) {
        throw "The research-copy helper failed with exit code $exitCode."
    }
}

$source = [IO.Path]::GetFullPath($SourceHalfLifeRoot).TrimEnd('\', '/')
if ($InspectSourceTopology) {
    # Deliberately do not resolve, inspect, create, or otherwise touch the
    # destination in diagnostic mode. The parameter remains accepted so the
    # original two-path manual command is backwards compatible.
    Invoke-BoundedResearchCopyTool -Arguments @(
        '--inspect-source-topology', '--source-root', $source)
    return
}

if ([string]::IsNullOrWhiteSpace($DestinationHalfLifeRoot)) {
    throw 'DestinationHalfLifeRoot is required for materialization.'
}
$destination =
    [IO.Path]::GetFullPath($DestinationHalfLifeRoot).TrimEnd('\', '/')
Invoke-BoundedResearchCopyTool -Arguments @(
    '--materialize', '--source-root', $source,
    '--destination-root', $destination)
