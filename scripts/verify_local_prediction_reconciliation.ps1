[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CheckerPath,

    [Parameter(Mandatory = $true)]
    [string]$ViewerPath,

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
    [string[]]$Scenarios,

    [ValidateRange(2, 100)]
    [int]$Iterations = 2,

    [ValidateRange(1000, 1000000)]
    [int]$Frames = 1000
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
            -not $virtualMap.EndsWith(
                '.bsp', [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe virtual map: $virtualMap"
        }
        $relative = $virtualMap.Replace(
            '/', [System.IO.Path]::DirectorySeparatorChar)
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
            ('{0}|{1}|{2}' -f
                $item.Length, $item.LastWriteTimeUtc.Ticks, $hash))
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
    $prefix = "[prediction] $Name="
    $matches = @($Lines | Where-Object {
        ([string]$_).StartsWith(
            $prefix, [System.StringComparison]::Ordinal)
    })
    if ($matches.Count -ne 1) {
        throw "Expected one $Name summary, received $($matches.Count)"
    }
    return $matches[0].Substring($prefix.Length)
}

function Get-SummaryUInt64([string[]]$Lines, [string]$Name) {
    $text = Get-SummaryValue $Lines $Name
    [UInt64]$value = 0
    if (-not [UInt64]::TryParse(
            $text,
            [System.Globalization.NumberStyles]::None,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [ref]$value)) {
        throw "Prediction checker reported invalid numeric $Name"
    }
    return $value
}

function Get-SummaryHash([string[]]$Lines, [string]$Name) {
    $value = Get-SummaryValue $Lines $Name
    if ($value -cnotmatch '^[0-9a-f]{64}$') {
        throw "Prediction checker reported invalid SHA-256 $Name"
    }
    return $value
}

function Assert-ScenarioSummary(
    [string[]]$Lines,
    [string]$Scenario,
    [UInt64]$ExpectedCommands,
    [UInt64]$AuthorityDelay
) {
    $summary = [ordered]@{
        Commands = Get-SummaryUInt64 $Lines 'commands'
        AuthorityUpdates = Get-SummaryUInt64 $Lines 'authority-updates'
        Acknowledgements = Get-SummaryUInt64 $Lines 'acknowledgements'
        Reconciliations = Get-SummaryUInt64 $Lines 'reconciliations'
        Exact = Get-SummaryUInt64 $Lines 'exact'
        Replays = Get-SummaryUInt64 $Lines 'replays'
        ReplayedCommands = Get-SummaryUInt64 $Lines 'replayed-commands'
        ReplayDepth = Get-SummaryUInt64 $Lines 'maximum-replay-depth'
        SmallCorrections = Get-SummaryUInt64 $Lines 'small-corrections'
        Snaps = Get-SummaryUInt64 $Lines 'snaps'
        Stale = Get-SummaryUInt64 $Lines 'stale'
        Duplicates = Get-SummaryUInt64 $Lines 'duplicates'
        HistoryBackpressure =
            Get-SummaryUInt64 $Lines 'history-backpressure'
        HardResets = Get-SummaryUInt64 $Lines 'hard-resets'
        ResetDiscardedCommands =
            Get-SummaryUInt64 $Lines 'reset-discarded-commands'
        HistoryHighWater = Get-SummaryUInt64 $Lines 'history-high-water'
        Jumps = Get-SummaryUInt64 $Lines 'jumps'
        DuckEnters = Get-SummaryUInt64 $Lines 'duck-enters'
        DuckExits = Get-SummaryUInt64 $Lines 'duck-exits'
    }
    if ($summary.Commands -ne $ExpectedCommands -or
        $summary.ReplayDepth -gt $AuthorityDelay -or
        $summary.HistoryHighWater -gt 256 -or
        $summary.AuthorityUpdates -ne
            ($summary.Reconciliations + $summary.Stale +
                $summary.Duplicates)) {
        throw "Prediction checker reported invalid common bounds for $Scenario"
    }
    if ($Scenario -cne 'hard-reset') {
        if ($summary.Acknowledgements -ne $summary.Commands -or
            $summary.Reconciliations -ne $summary.Commands -or
            $summary.HardResets -ne 0 -or
            $summary.ResetDiscardedCommands -ne 0) {
            throw "Prediction checker reported invalid authority accounting for $Scenario"
        }
    }
    if ($Scenario -cne 'history-backpressure' -and
        $summary.HistoryBackpressure -ne 0) {
        throw "Prediction checker reported unexpected history backpressure for $Scenario"
    }
    if ($Scenario -cne 'stale-duplicate' -and
        ($summary.Stale -ne 0 -or $summary.Duplicates -ne 0)) {
        throw "Prediction checker reported unexpected stale/duplicate authority for $Scenario"
    }

    switch -CaseSensitive ($Scenario) {
        'exact-authority' {
            if ($summary.Exact -ne $summary.Commands -or
                $summary.Replays -ne 0 -or
                $summary.ReplayedCommands -ne 0 -or
                $summary.SmallCorrections -ne 0 -or
                $summary.Snaps -ne 0) {
                throw 'Exact-authority route did not remain exact'
            }
            break
        }
        'delayed-authority' {
            if ($summary.Exact -ne $summary.Commands -or
                $summary.Replays -ne 0 -or
                $summary.HistoryHighWater -ne ($AuthorityDelay + 1)) {
                throw 'Delayed-authority route did not retain the exact delayed window'
            }
            break
        }
        'small-correction' {
            if ($summary.SmallCorrections -eq 0 -or $summary.Snaps -ne 0 -or
                $summary.Replays -eq 0 -or
                $summary.ReplayDepth -ne $AuthorityDelay) {
                throw 'Small-correction route did not exercise smoothing classification'
            }
            break
        }
        'velocity-correction' {
            if (($summary.SmallCorrections + $summary.Snaps) -eq 0 -or
                $summary.Replays -eq 0 -or
                $summary.ReplayDepth -ne $AuthorityDelay) {
                throw 'Velocity-correction route did not exercise correction classification'
            }
            break
        }
        'large-correction' {
            if ($summary.Snaps -eq 0 -or $summary.Replays -eq 0 -or
                $summary.ReplayDepth -ne $AuthorityDelay) {
                throw 'Large-correction route did not exercise snap classification'
            }
            break
        }
        'teleport' {
            if ($summary.Snaps -eq 0 -or $summary.Replays -eq 0 -or
                $summary.ReplayDepth -ne $AuthorityDelay) {
                throw 'Teleport route did not exercise snap classification'
            }
            break
        }
        'stale-duplicate' {
            if ($summary.Stale -eq 0 -or $summary.Duplicates -eq 0 -or
                $summary.AuthorityUpdates -le $summary.Commands) {
                throw 'Stale/duplicate route did not exercise both ignored-update paths'
            }
            break
        }
        'wall-replay' {
            if ($summary.Replays -eq 0 -or
                $summary.ReplayedCommands -eq 0 -or
                $summary.ReplayDepth -ne $AuthorityDelay) {
                throw "$Scenario did not exercise bounded command replay"
            }
            break
        }
        'jump-replay' {
            if ($summary.Replays -eq 0 -or
                $summary.ReplayedCommands -eq 0 -or
                $summary.ReplayDepth -ne $AuthorityDelay -or
                $summary.Jumps -ne $summary.Replays) {
                throw 'Jump replay did not retain each one-shot exactly once'
            }
            break
        }
        'duck-replay' {
            if ($summary.Replays -eq 0 -or
                $summary.ReplayedCommands -eq 0 -or
                $summary.ReplayDepth -ne $AuthorityDelay -or
                ($summary.DuckEnters + $summary.DuckExits) -ne
                    ($summary.Replays + 1) -or
                $summary.DuckEnters -ne ($summary.DuckExits + 1)) {
                throw 'Duck replay did not retain each transition exactly once'
            }
            break
        }
        'deterministic-route' {
            if ($summary.Exact -ne $summary.Commands -or
                $summary.Replays -ne 0 -or
                $summary.HistoryHighWater -ne ($AuthorityDelay + 1)) {
                throw 'Deterministic route did not retain the exact delayed window'
            }
            break
        }
        'history-backpressure' {
            if ($summary.HistoryBackpressure -ne
                    ($summary.Commands - $AuthorityDelay) -or
                $summary.HistoryHighWater -ne $AuthorityDelay -or
                $summary.Exact -ne $summary.Commands -or
                $summary.Replays -ne 0) {
                throw 'History-backpressure route did not stop and resume at the exact bound'
            }
            break
        }
        'hard-reset' {
            if ($summary.HardResets -ne 1 -or $summary.Snaps -eq 0 -or
                $summary.ResetDiscardedCommands -eq 0 -or
                ($summary.Acknowledgements +
                    $summary.ResetDiscardedCommands) -ne $summary.Commands -or
                $summary.Reconciliations -ne
                    ($summary.Acknowledgements + $summary.HardResets) -or
                $summary.HistoryBackpressure -ne 0) {
                throw 'Hard-reset route did not replace its generation transactionally'
            }
            break
        }
        'mixed' {
            if ($summary.SmallCorrections -eq 0 -or
                $summary.Snaps -eq 0 -or $summary.Replays -eq 0) {
                throw 'Mixed route omitted correction smoothing, replay, or teleport snap'
            }
            break
        }
        default {
            throw "Unsupported prediction verification scenario: $Scenario"
        }
    }
    return $summary
}

$resolvedChecker = Resolve-ExistingLeaf $CheckerPath 'CheckerPath'
$resolvedViewer = Resolve-ExistingLeaf $ViewerPath 'ViewerPath'
$resolvedBasedir = Resolve-ExistingContainer $Basedir 'Basedir'
$beforeInventory = Get-RootInventory $resolvedBasedir
$beforeMaps = Get-MapSnapshot $resolvedBasedir $Game $Maps

$runOrdinal = 0
foreach ($virtualMap in $Maps) {
    foreach ($scenario in $Scenarios) {
        $authorityDelay = if ($scenario -ceq 'exact-authority') { 0 } else { 8 }
        $reference = $null
        for ($pass = 0; $pass -lt $Iterations; ++$pass) {
            $lines = @(& $resolvedChecker `
                --basedir $resolvedBasedir `
                --game $Game `
                --map $virtualMap `
                --scenario $scenario `
                --authority-delay-commands $authorityDelay `
                --commands 1000 2>&1)
            if ($LASTEXITCODE -ne 0) {
                throw "Prediction checker failed at route $runOrdinal pass $pass`n$($lines -join "`n")"
            }
            if (@($lines | Where-Object {
                    ([string]$_).StartsWith(
                        '[prediction-error]',
                        [System.StringComparison]::Ordinal)
                }).Count -ne 0) {
                throw "Prediction checker reported an error at route $runOrdinal"
            }
            foreach ($zeroField in @(
                    'startsolid',
                    'allsolid',
                    'history-overflow',
                    'network-operations',
                    'writes-performed')) {
                if ((Get-SummaryValue $lines $zeroField) -cne '0') {
                    throw "Prediction checker reported nonzero $zeroField"
                }
            }
            if ((Get-SummaryValue $lines 'result') -cne 'success') {
                throw 'Prediction checker did not report success'
            }
            $summary = Assert-ScenarioSummary $lines $scenario 1000 `
                ([UInt64]$authorityDelay)
            $sample = @{
                StateHash = Get-SummaryHash $lines 'final-state-hash'
                HistoryReplayHash =
                    Get-SummaryHash $lines 'history-replay-hash'
                Commands = $summary.Commands.ToString(
                    [System.Globalization.CultureInfo]::InvariantCulture)
                Summary = (($summary.GetEnumerator() | ForEach-Object {
                    '{0}={1}' -f $_.Key, $_.Value
                }) -join ';')
            }
            if ($null -eq $reference) {
                $reference = $sample
            } elseif ($sample.StateHash -cne $reference.StateHash -or
                $sample.HistoryReplayHash -cne
                    $reference.HistoryReplayHash -or
                $sample.Commands -cne $reference.Commands -or
                $sample.Summary -cne $reference.Summary) {
                throw "Deterministic prediction mismatch at route $runOrdinal"
            }
        }
        Write-Output (
            '[prediction-verify] route={0} commands={1} state-hash={2} history-replay-hash={3} result=success' -f
                $runOrdinal,
                $reference.Commands,
                $reference.StateHash,
                $reference.HistoryReplayHash)
        ++$runOrdinal
    }
}

# The viewer's bounded mixed route contains the explicit one-sample teleport
# required by the OpenGL acceptance campaign; it intentionally exposes no
# standalone arbitrary teleport input.
$viewerScenarios = @('small-correction', 'wall-replay', 'mixed')
$previousFrameLimit = $env:HLCLIENT_SMOKE_TEST_FRAMES
try {
    $env:HLCLIENT_SMOKE_TEST_FRAMES = $Frames.ToString(
        [System.Globalization.CultureInfo]::InvariantCulture)
    foreach ($scenario in $viewerScenarios) {
        $lines = @(& $resolvedViewer `
            --basedir $resolvedBasedir `
            --game $Game `
            --map $Maps[0] `
            --scenario $scenario `
            --authority-delay-commands 8 `
            --prediction-diagnostics summary `
            --visibility all `
            --brush-submodels static `
            --cull none 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "Prediction viewer failed for $scenario`n$($lines -join "`n")"
        }
        if (@($lines | Where-Object {
                ([string]$_).StartsWith(
                    '[prediction-error]',
                    [System.StringComparison]::Ordinal)
            }).Count -ne 0) {
            throw "Prediction viewer reported an error for $scenario"
        }
        $capabilitySkip = @($lines | Where-Object {
            ([string]$_) -ceq '[prediction-opengl] capability=unavailable'
        }).Count -eq 1
        if ($capabilitySkip) {
            Write-Output '[prediction-verify] opengl=capability-unavailable'
            break
        }
        if (@($lines | Where-Object {
                ([string]$_) -ceq '[prediction-opengl] result=success'
            }).Count -ne 1) {
            throw "Prediction viewer omitted its success summary for $scenario"
        }
        foreach ($requiredLine in @(
                'world-upload-count=1',
                'scene-upload-count=1',
                'brush-upload-count=1',
                'failed-upload-count=0',
                'world-resource-release-count=0',
                'active-world-resources=1',
                'studio-upload-count=0',
                'sprite-upload-count=0',
                'entity-resource-release-count=0',
                'gl-error=none',
                'network-operations=0',
                'writes-performed=0')) {
            if (@($lines | Where-Object {
                    ([string]$_) -ceq $requiredLine
                }).Count -ne 1) {
                throw "Prediction viewer resource invariant '$requiredLine' failed for $scenario"
            }
        }
        $expectedFrames = 'rendered-frames={0}' -f $Frames
        if (@($lines | Where-Object {
                ([string]$_) -ceq $expectedFrames
            }).Count -ne 1) {
            throw "Prediction viewer did not render exactly $Frames frames for $scenario"
        }
        foreach ($zeroField in @('startsolid', 'allsolid', 'failures')) {
            if ((Get-SummaryValue $lines $zeroField) -cne '0') {
                throw "Prediction viewer reported nonzero $zeroField for $scenario"
            }
        }
        if ($scenario -ceq 'small-correction' -and
            (Get-SummaryUInt64 $lines 'small-corrections') -eq 0) {
            throw 'Prediction viewer omitted the required small correction'
        }
        if ($scenario -ceq 'wall-replay' -and
            (Get-SummaryUInt64 $lines 'replayed-commands') -eq 0) {
            throw 'Prediction viewer omitted the required wall replay'
        }
        if ($scenario -ceq 'mixed' -and
            ((Get-SummaryUInt64 $lines 'snaps') -eq 0 -or
                (Get-SummaryUInt64 $lines 'teleport-one-sample') -ne 1)) {
            throw 'Prediction viewer did not publish exactly one first-sample teleport snap'
        }
    }
} finally {
    $env:HLCLIENT_SMOKE_TEST_FRAMES = $previousFrameLimit
}

$afterMaps = Get-MapSnapshot $resolvedBasedir $Game $Maps
$afterInventory = Get-RootInventory $resolvedBasedir
Assert-DictionaryEqual $beforeMaps $afterMaps 'BSP snapshot'
Assert-DictionaryEqual $beforeInventory $afterInventory 'Root inventory'

Write-Output "[prediction-verify] runs=$runOrdinal"
Write-Output '[prediction-verify] created-files=0'
Write-Output '[prediction-verify] deleted-files=0'
Write-Output '[prediction-verify] network-operations=0'
Write-Output '[prediction-verify] external-file-drift=none'
Write-Output '[prediction-verify] result=success'
