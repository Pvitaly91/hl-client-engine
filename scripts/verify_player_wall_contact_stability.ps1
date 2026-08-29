[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ToolPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ViewerPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Basedir,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Game,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]]$Maps,

    [Parameter(Mandatory = $true)]
    [ValidateRange(2, 1000)]
    [int]$Iterations,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1000, 8192)]
    [int]$Frames
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Scenarios = @(
    'wall-contact-stress',
    'wall-glance-stress',
    'corner-contact-stress',
    'jump-wall-stress',
    'duck-wall-stress'
)

$MaximumChildOutputCharacters = 256 * 1024
$MaximumChildOutputLines = 4096
$CheckerTimeoutMilliseconds = 180000L
$ViewerMaximumTimeoutMilliseconds = 600000L

function Resolve-Leaf([string]$Path, [string]$Label) {
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
        throw "$Label must identify an existing file"
    }
    return $resolved.Path
}

function Resolve-Container([string]$Path, [string]$Label) {
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not (Test-Path -LiteralPath $resolved.Path -PathType Container)) {
        throw "$Label must identify an existing directory"
    }
    return $resolved.Path.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
}

function Assert-SafeMap([string]$VirtualMap) {
    if ([System.IO.Path]::IsPathRooted($VirtualMap) -or
        $VirtualMap.Contains('\') -or $VirtualMap.Contains(':') -or
        $VirtualMap.Contains('..') -or $VirtualMap.Contains('//') -or
        -not $VirtualMap.StartsWith('maps/',
            [System.StringComparison]::Ordinal) -or
        -not $VirtualMap.EndsWith('.bsp',
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe virtual map: $VirtualMap"
    }
}

function Get-MapPath(
    [string]$Root,
    [string]$GameDirectory,
    [string]$VirtualMap
) {
    $relative = $VirtualMap.Replace(
        '/', [System.IO.Path]::DirectorySeparatorChar)
    foreach ($rootName in @($GameDirectory, 'valve') | Select-Object -Unique) {
        $searchRoot = Join-Path $Root $rootName
        if (-not (Test-Path -LiteralPath $searchRoot -PathType Container)) {
            continue
        }
        $candidate = [System.IO.Path]::GetFullPath(
            (Join-Path $searchRoot $relative))
        $prefix = [System.IO.Path]::GetFullPath($searchRoot).TrimEnd(
            [System.IO.Path]::DirectorySeparatorChar) +
            [System.IO.Path]::DirectorySeparatorChar
        if (-not $candidate.StartsWith(
                $prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Resolved map escaped its resource root: $VirtualMap"
        }
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    throw "Map is unavailable: $VirtualMap"
}

function Get-MapSnapshot([string[]]$Paths) {
    $snapshot = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $Paths) {
        $item = Get-Item -LiteralPath $path -Force
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        $snapshot.Add($path,
            ('{0}|{1}|{2}' -f $item.Length,
                $item.LastWriteTimeUtc.Ticks, $hash))
    }
    return $snapshot
}

function Get-RootInventory([string]$Root, [string]$GameDirectory) {
    $snapshot = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($rootName in @($GameDirectory, 'valve') | Select-Object -Unique) {
        $resourceRoot = Join-Path $Root $rootName
        if (-not (Test-Path -LiteralPath $resourceRoot -PathType Container)) {
            continue
        }
        foreach ($file in Get-ChildItem -LiteralPath $resourceRoot -File `
                -Recurse -Force) {
            $relative = [System.IO.Path]::GetRelativePath(
                $resourceRoot, $file.FullName)
            $key = "$rootName/$relative"
            $snapshot.Add($key,
                ('{0}|{1}' -f $file.Length, $file.LastWriteTimeUtc.Ticks))
        }
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
    $matches = @($Lines | Where-Object {
        $_.StartsWith($prefix, [System.StringComparison]::Ordinal)
    })
    if ($matches.Count -ne 1) {
        throw "Expected one $Name summary, received $($matches.Count)"
    }
    return $matches[0].Substring($prefix.Length)
}

function Get-PlainValue([string[]]$Lines, [string]$Name) {
    $prefix = "$Name="
    $matches = @($Lines | Where-Object {
        $_.StartsWith($prefix, [System.StringComparison]::Ordinal)
    })
    if ($matches.Count -ne 1) {
        throw "Expected one $Name value, received $($matches.Count)"
    }
    return $matches[0].Substring($prefix.Length)
}

function Assert-UnsignedDecimal([string]$Value, [string]$Label) {
    if ($Value -cnotmatch '^(0|[1-9][0-9]*)$') {
        throw "$Label is not a canonical unsigned decimal: $Value"
    }
}

function Assert-Sha256([string]$Value, [string]$Label) {
    if ($Value -cnotmatch '^[0-9a-fA-F]{64}$') {
        throw "$Label is not a 64-hex SHA-256 value: $Value"
    }
}

function Stop-ChildProcessTree([System.Diagnostics.Process]$Process) {
    try {
        if (-not $Process.HasExited) {
            $Process.Kill($true)
        }
    }
    catch {
        try {
            if (-not $Process.HasExited) {
                $Process.Kill()
            }
        }
        catch {
        }
    }
    try {
        if (-not $Process.HasExited) {
            [void]$Process.WaitForExit(5000)
        }
    }
    catch {
    }
}

function Invoke-BoundedChildProcess(
    [string]$Executable,
    [string[]]$Arguments,
    [hashtable]$EnvironmentOverrides,
    [long]$TimeoutMilliseconds,
    [string]$Label
) {
    if ($TimeoutMilliseconds -le 0) {
        throw [System.ArgumentOutOfRangeException]::new(
            'TimeoutMilliseconds')
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.WorkingDirectory = [Environment]::CurrentDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        [void]$startInfo.ArgumentList.Add($argument)
    }
    foreach ($entry in $EnvironmentOverrides.GetEnumerator()) {
        $startInfo.Environment[$entry.Key] = [string]$entry.Value
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $started = $false
    try {
        if (-not $process.Start()) {
            throw [System.InvalidOperationException]::new(
                "process_start_failed label=$Label")
        }
        $started = $true

        $stdout = [System.Text.StringBuilder]::new()
        $stderr = [System.Text.StringBuilder]::new()
        $stdoutBuffer = [char[]]::new(4096)
        $stderrBuffer = [char[]]::new(4096)
        $stdoutDone = $false
        $stderrDone = $false
        $stdoutTask = $process.StandardOutput.ReadAsync(
            $stdoutBuffer, 0, $stdoutBuffer.Length)
        $stderrTask = $process.StandardError.ReadAsync(
            $stderrBuffer, 0, $stderrBuffer.Length)
        [long]$characterCount = 0
        [long]$lineCount = 0
        $timer = [System.Diagnostics.Stopwatch]::StartNew()

        while ($true) {
            $madeProgress = $false
            if (-not $stdoutDone -and $stdoutTask.IsCompleted) {
                $read = [int]$stdoutTask.GetAwaiter().GetResult()
                if ($read -eq 0) {
                    $stdoutDone = $true
                }
                else {
                    $chunk = [string]::new($stdoutBuffer, 0, $read)
                    [void]$stdout.Append($chunk)
                    $characterCount += $read
                    for ($index = 0; $index -lt $read; ++$index) {
                        if ($stdoutBuffer[$index] -eq "`n") {
                            ++$lineCount
                        }
                    }
                    $stdoutTask = $process.StandardOutput.ReadAsync(
                        $stdoutBuffer, 0, $stdoutBuffer.Length)
                }
                $madeProgress = $true
            }
            if (-not $stderrDone -and $stderrTask.IsCompleted) {
                $read = [int]$stderrTask.GetAwaiter().GetResult()
                if ($read -eq 0) {
                    $stderrDone = $true
                }
                else {
                    $chunk = [string]::new($stderrBuffer, 0, $read)
                    [void]$stderr.Append($chunk)
                    $characterCount += $read
                    for ($index = 0; $index -lt $read; ++$index) {
                        if ($stderrBuffer[$index] -eq "`n") {
                            ++$lineCount
                        }
                    }
                    $stderrTask = $process.StandardError.ReadAsync(
                        $stderrBuffer, 0, $stderrBuffer.Length)
                }
                $madeProgress = $true
            }

            if ($characterCount -gt $MaximumChildOutputCharacters -or
                $lineCount -gt $MaximumChildOutputLines) {
                Stop-ChildProcessTree $process
                throw [System.IO.InvalidDataException]::new(
                    ('process_output_limit_exceeded label={0} characters={1} lines={2}' -f
                        $Label, $characterCount, $lineCount))
            }
            if ($process.HasExited -and $stdoutDone -and $stderrDone) {
                break
            }
            if ($timer.ElapsedMilliseconds -gt $TimeoutMilliseconds) {
                Stop-ChildProcessTree $process
                throw [System.TimeoutException]::new(
                    ('process_timeout label={0} timeout-ms={1}' -f
                        $Label, $TimeoutMilliseconds))
            }
            if (-not $madeProgress) {
                [System.Threading.Thread]::Sleep(10)
            }
        }

        $lines = [System.Collections.Generic.List[string]]::new()
        foreach ($text in @($stdout.ToString(), $stderr.ToString())) {
            if ($text.Length -eq 0) {
                continue
            }
            [string[]]$split = [regex]::Split($text, "`r`n|`n|`r")
            $count = $split.Length
            if ($count -ne 0 -and $split[$count - 1].Length -eq 0) {
                --$count
            }
            for ($index = 0; $index -lt $count; ++$index) {
                $lines.Add($split[$index])
            }
        }
        return [pscustomobject]@{
            Lines = [string[]]$lines.ToArray()
            ExitCode = $process.ExitCode
        }
    }
    finally {
        if ($started) {
            try {
                if (-not $process.HasExited) {
                    Stop-ChildProcessTree $process
                }
            }
            catch {
            }
        }
        $process.Dispose()
    }
}

function Invoke-Checker(
    [string]$Executable,
    [string]$Root,
    [string]$GameDirectory,
    [string]$VirtualMap,
    [string]$Scenario
) {
    $arguments = @(
        '--basedir', $Root,
        '--game', $GameDirectory,
        '--map', $VirtualMap,
        '--scenario', $Scenario
    )
    $run = Invoke-BoundedChildProcess $Executable $arguments @{} `
        $CheckerTimeoutMilliseconds ('checker:{0}:{1}' -f
            $VirtualMap, $Scenario)
    [string[]]$lines = $run.Lines
    if ($run.ExitCode -ne 0 -or
        @($lines | Where-Object { $_.StartsWith(
            '[movement-error]', [System.StringComparison]::Ordinal) }).Count `
            -ne 0) {
        throw "Movement checker failed for $VirtualMap / $Scenario`n$($lines -join "`n")"
    }
    return $lines
}

function Invoke-Viewer(
    [string]$Executable,
    [string]$Root,
    [string]$GameDirectory,
    [string]$VirtualMap,
    [int]$FrameCount
) {
    $arguments = @(
        '--basedir', $Root,
        '--game', $GameDirectory,
        '--map', $VirtualMap,
        '--camera', 'player-walk',
        '--visibility', 'pvs-frustum',
        '--brush-submodels', 'static',
        '--cull', 'back'
    )
    $environment = @{
        HLCLIENT_SMOKE_TEST_FRAMES = $FrameCount.ToString(
            [System.Globalization.CultureInfo]::InvariantCulture)
        HLCLIENT_SMOKE_TEST_INPUT = 'player-wall-contact-v1'
    }
    $timeout = [Math]::Min($ViewerMaximumTimeoutMilliseconds,
        120000L + [long]$FrameCount * 50L)
    return Invoke-BoundedChildProcess $Executable $arguments $environment `
        $timeout "viewer:$VirtualMap"
}

$tool = Resolve-Leaf $ToolPath 'ToolPath'
$viewer = Resolve-Leaf $ViewerPath 'ViewerPath'
$root = Resolve-Container $Basedir 'Basedir'
$mapPaths = @()
foreach ($map in $Maps) {
    Assert-SafeMap $map
    $mapPaths += Get-MapPath $root $Game $map
}

$beforeMaps = Get-MapSnapshot $mapPaths
$beforeInventory = Get-RootInventory $root $Game
$references = @{}
$cpuRuns = 0
$glRuns = 0
$glSkips = 0
$primaryError = $null
$driftError = $null
try {
    foreach ($map in $Maps) {
        foreach ($scenario in $Scenarios) {
            for ($iteration = 0; $iteration -lt $Iterations; ++$iteration) {
                $lines = Invoke-Checker $tool $root $Game $map $scenario
                $values = [ordered]@{
                    Profile = Get-SummaryValue $lines 'profile'
                    Collision = Get-SummaryValue $lines 'collision'
                    BrushSolidity = Get-SummaryValue $lines 'brush-solidity'
                    Scenario = Get-SummaryValue $lines 'scenario'
                    SpawnCandidates = Get-SummaryValue $lines 'spawn-candidates'
                    SpawnSelected = Get-SummaryValue $lines 'spawn-selected'
                    BootstrapCommands = Get-SummaryValue $lines 'bootstrap-commands'
                    StressCommands = Get-SummaryValue $lines 'stress-commands'
                    WallFound = Get-SummaryValue $lines 'wall-found'
                    CornerFound = Get-SummaryValue $lines 'corner-found'
                    WallDirection = Get-SummaryValue $lines `
                        'wall-direction-ordinal'
                    WallSourcePlane = Get-SummaryValue $lines `
                        'wall-source-plane-index'
                    SelectionHash = Get-SummaryValue $lines `
                        'wall-selection-hash'
                    Commands = Get-SummaryValue $lines 'commands'
                    GroundedCommands = Get-SummaryValue $lines `
                        'grounded-commands'
                    AirborneCommands = Get-SummaryValue $lines `
                        'airborne-commands'
                    CollisionHits = Get-SummaryValue $lines 'collision-hits'
                    ContactTouches = Get-SummaryValue $lines 'contact-touches'
                    SelectedWallTouches = Get-SummaryValue $lines `
                        'selected-wall-contact-touches'
                    CornerWallTouches = Get-SummaryValue $lines `
                        'corner-wall-contact-touches'
                    ContactEpochs = Get-SummaryValue $lines `
                        'selected-wall-contact-epochs'
                    RecontactEpochs = Get-SummaryValue $lines `
                        'release-recontact-epochs'
                    PositiveTangentCommands = Get-SummaryValue $lines `
                        'positive-tangent-contact-commands'
                    NegativeTangentCommands = Get-SummaryValue $lines `
                        'negative-tangent-contact-commands'
                    AirborneWallTouches = Get-SummaryValue $lines `
                        'airborne-wall-contact-touches'
                    DuckedWallTouches = Get-SummaryValue $lines `
                        'ducked-wall-contact-touches'
                    RestoredStandingWallTouches = Get-SummaryValue $lines `
                        'restored-standing-wall-contact-touches'
                    PositionChecks = Get-SummaryValue $lines `
                        'nonpenetrating-checks'
                    MaximumTouches = Get-SummaryValue $lines `
                        'maximum-command-touches'
                    TouchLimit = Get-SummaryValue $lines 'touch-limit'
                    TouchHardLimit = Get-SummaryValue $lines 'touch-hard-limit'
                    Scratch = Get-SummaryValue $lines 'scratch-retained-bytes'
                    ScratchPrimary = Get-SummaryValue $lines `
                        'scratch-primary-retained-bytes'
                    ScratchDirect = Get-SummaryValue $lines `
                        'scratch-direct-candidate-retained-bytes'
                    ScratchStep = Get-SummaryValue $lines `
                        'scratch-step-candidate-retained-bytes'
                    ScratchHardLimit = Get-SummaryValue $lines `
                        'scratch-hard-limit-bytes'
                    DiagnosticCapacity = Get-SummaryValue $lines `
                        'diagnostic-ring-capacity'
                    DiagnosticMaximum = Get-SummaryValue $lines `
                        'diagnostic-ring-maximum-records'
                    DiagnosticFinal = Get-SummaryValue $lines `
                        'diagnostic-ring-final-records'
                    DiagnosticMaximumOverwrites = Get-SummaryValue $lines `
                        'diagnostic-ring-maximum-overwrites'
                    DiagnosticFinalOverwrites = Get-SummaryValue $lines `
                        'diagnostic-ring-final-overwrites'
                    StepSuccesses = Get-SummaryValue $lines 'step-successes'
                    JumpCount = Get-SummaryValue $lines 'jump-count'
                    DuckEnterCount = Get-SummaryValue $lines 'duck-enter-count'
                    DuckExitCount = Get-SummaryValue $lines 'duck-exit-count'
                    DuckTransitions = Get-SummaryValue $lines 'duck-transitions'
                    Grounded = Get-SummaryValue $lines 'grounded'
                    Hull = Get-SummaryValue $lines 'hull'
                    StartSolid = Get-SummaryValue $lines 'startsolid'
                    AllSolid = Get-SummaryValue $lines 'allsolid'
                    FinalHash = Get-SummaryValue $lines 'final-state-hash'
                    RouteHash = Get-SummaryValue $lines 'route-hash'
                    Network = Get-SummaryValue $lines 'network-operations'
                    Writes = Get-SummaryValue $lines 'writes-performed'
                    Result = Get-SummaryValue $lines 'result'
                }
                foreach ($field in @(
                        'SpawnCandidates', 'BootstrapCommands',
                        'StressCommands', 'WallDirection', 'WallSourcePlane',
                        'Commands', 'GroundedCommands', 'AirborneCommands',
                        'CollisionHits', 'ContactTouches',
                        'SelectedWallTouches', 'CornerWallTouches',
                        'ContactEpochs', 'RecontactEpochs',
                        'PositiveTangentCommands', 'NegativeTangentCommands',
                        'AirborneWallTouches', 'DuckedWallTouches',
                        'RestoredStandingWallTouches',
                        'PositionChecks', 'MaximumTouches', 'TouchLimit',
                        'TouchHardLimit', 'Scratch', 'ScratchPrimary',
                        'ScratchDirect', 'ScratchStep', 'ScratchHardLimit',
                        'DiagnosticCapacity', 'DiagnosticMaximum',
                        'DiagnosticFinal', 'DiagnosticMaximumOverwrites',
                        'DiagnosticFinalOverwrites', 'StepSuccesses',
                        'JumpCount', 'DuckEnterCount', 'DuckExitCount',
                        'DuckTransitions', 'StartSolid',
                        'AllSolid', 'Network', 'Writes')) {
                    Assert-UnsignedDecimal $values[$field] $field
                }
                Assert-Sha256 $values.SelectionHash 'wall-selection-hash'
                Assert-Sha256 $values.FinalHash 'final-state-hash'
                Assert-Sha256 $values.RouteHash 'route-hash'

                $scratchTotal = [uint64]$values.ScratchPrimary +
                    [uint64]$values.ScratchDirect + [uint64]$values.ScratchStep
                $cornerExpected = $scenario -ceq 'corner-contact-stress'
                $wallContactExpected = $scenario -ceq 'wall-contact-stress'
                $glanceExpected = $scenario -ceq 'wall-glance-stress'
                $jumpExpected = $scenario -ceq 'jump-wall-stress'
                $duckExpected = $scenario -ceq 'duck-wall-stress'
                $duckTransitionTotal = [uint64]$values.DuckEnterCount +
                    [uint64]$values.DuckExitCount
                if ($values.Profile -cne
                        'public_valve_pm_shared_dry_walk_subset_v1' -or
                    $values.Collision -cne 'world-only' -or
                    $values.BrushSolidity -cne 'stock-evidence-pending' -or
                    $values.Scenario -cne $scenario -or
                    $values.SpawnSelected -cne 'true' -or
                    [uint64]$values.StressCommands -lt 10000 -or
                    $values.Commands -cne $values.StressCommands -or
                    $values.WallFound -cne 'true' -or
                    ($values.CornerFound -cne
                        $(if ($cornerExpected) { 'true' } else { 'false' })) -or
                    [uint64]$values.ContactTouches -eq 0 -or
                    [uint64]$values.SelectedWallTouches -eq 0 -or
                    [uint64]$values.SelectedWallTouches -gt
                        [uint64]$values.ContactTouches -or
                    ($cornerExpected -and
                        [uint64]$values.CornerWallTouches -eq 0) -or
                    (-not $cornerExpected -and
                        [uint64]$values.CornerWallTouches -ne 0) -or
                    [uint64]$values.CornerWallTouches -gt
                        [uint64]$values.ContactTouches -or
                    [uint64]$values.ContactEpochs -eq 0 -or
                    [uint64]$values.ContactEpochs -gt
                        [uint64]$values.Commands -or
                    [uint64]$values.RecontactEpochs -ge
                        [uint64]$values.ContactEpochs -or
                    ($wallContactExpected -and
                        ([uint64]$values.ContactEpochs -lt 2 -or
                            [uint64]$values.RecontactEpochs -eq 0)) -or
                    (-not $wallContactExpected -and
                        [uint64]$values.RecontactEpochs -ne 0) -or
                    [uint64]$values.PositiveTangentCommands -gt
                        [uint64]$values.Commands -or
                    [uint64]$values.NegativeTangentCommands -gt
                        [uint64]$values.Commands -or
                    ($glanceExpected -and
                        ([uint64]$values.PositiveTangentCommands -eq 0 -or
                            [uint64]$values.NegativeTangentCommands -eq 0)) -or
                    (-not $glanceExpected -and
                        ([uint64]$values.PositiveTangentCommands -ne 0 -or
                            [uint64]$values.NegativeTangentCommands -ne 0)) -or
                    [uint64]$values.AirborneWallTouches -gt
                        [uint64]$values.SelectedWallTouches -or
                    [uint64]$values.JumpCount -gt
                        [uint64]$values.Commands -or
                    ($jumpExpected -and
                        ([uint64]$values.JumpCount -eq 0 -or
                            [uint64]$values.AirborneCommands -eq 0 -or
                            [uint64]$values.AirborneWallTouches -eq 0)) -or
                    (-not $jumpExpected -and
                        ([uint64]$values.JumpCount -ne 0 -or
                            [uint64]$values.AirborneWallTouches -ne 0)) -or
                    [uint64]$values.DuckedWallTouches -gt
                        [uint64]$values.SelectedWallTouches -or
                    [uint64]$values.RestoredStandingWallTouches -gt
                        [uint64]$values.SelectedWallTouches -or
                    [uint64]$values.DuckEnterCount -gt
                        [uint64]$values.Commands -or
                    [uint64]$values.DuckExitCount -gt
                        [uint64]$values.Commands -or
                    [uint64]$values.DuckTransitions -ne $duckTransitionTotal -or
                    ($duckExpected -and
                        ([uint64]$values.DuckEnterCount -eq 0 -or
                            [uint64]$values.DuckExitCount -eq 0 -or
                            [uint64]$values.DuckedWallTouches -eq 0 -or
                            [uint64]$values.RestoredStandingWallTouches -eq 0)) -or
                    (-not $duckExpected -and
                        ([uint64]$values.DuckEnterCount -ne 0 -or
                            [uint64]$values.DuckExitCount -ne 0 -or
                            [uint64]$values.DuckedWallTouches -ne 0 -or
                            [uint64]$values.RestoredStandingWallTouches -ne 0)) -or
                    $values.PositionChecks -cne $values.StressCommands -or
                    [uint64]$values.TouchLimit -ne 256 -or
                    [uint64]$values.TouchHardLimit -ne 4096 -or
                    [uint64]$values.MaximumTouches -gt
                        [uint64]$values.TouchLimit -or
                    [uint64]$values.Scratch -ne $scratchTotal -or
                    [uint64]$values.ScratchHardLimit -ne
                        [uint64](3 * 64 * 1024 * 1024) -or
                    [uint64]$values.Scratch -gt
                        [uint64]$values.ScratchHardLimit -or
                    [uint64]$values.DiagnosticCapacity -ne 256 -or
                    [uint64]$values.DiagnosticMaximum -gt
                        [uint64]$values.DiagnosticCapacity -or
                    [uint64]$values.DiagnosticFinal -gt
                        [uint64]$values.DiagnosticCapacity -or
                    [uint64]$values.DiagnosticFinalOverwrites -gt
                        [uint64]$values.DiagnosticMaximumOverwrites -or
                    $values.StartSolid -cne '0' -or
                    $values.AllSolid -cne '0' -or
                    $values.Network -cne '0' -or
                    $values.Writes -cne '0' -or
                    $values.Result -cne 'success') {
                    throw "Invalid stress summary for $map / $scenario"
                }

                $stable = (@($lines | Where-Object {
                    $_.StartsWith('[movement] ',
                        [System.StringComparison]::Ordinal)
                }) -join "`n")
                $key = "$map|$scenario"
                if (-not $references.ContainsKey($key)) {
                    $references[$key] = $stable
                } elseif ($references[$key] -cne $stable) {
                    throw "Nondeterministic stress result for $map / $scenario"
                }
                ++$cpuRuns
            }
            Write-Output (
                '[wall-contact-verify] map={0} scenario={1} campaigns={2} result=success' -f
                    $map, $scenario, $Iterations)
        }
    }

    $smokePattern = '^\[movement-smoke\] profile=player-wall-contact-v1 ' +
        'wall-found=true rendered-frames=([0-9]+) commands=([0-9]+) ' +
        'contacts=([0-9]+) startsolid=0 allsolid=0 fatal=false ' +
        'gl-error=none non-clear=true world-uploads=1 scene-uploads=1 ' +
        'brush-uploads=1$'
    foreach ($map in $Maps) {
        $run = Invoke-Viewer $viewer $root $Game $map $Frames
        $capabilitySkip = @($run.Lines | Where-Object {
            $_ -ceq 'wall-contact-opengl=capability-unavailable'
        })
        if ($capabilitySkip.Count -eq 1 -and $run.ExitCode -eq 0) {
            ++$glSkips
            Write-Output (
                '[wall-contact-verify] map={0} opengl=capability-skip' -f $map)
            continue
        }
        $smoke = @($run.Lines | Where-Object { $_ -match $smokePattern })
        if ($run.ExitCode -ne 0 -or $smoke.Count -ne 1) {
            throw "Viewer wall-contact smoke failed for $map`n$($run.Lines -join "`n")"
        }
        $matched = [regex]::Match($smoke[0], $smokePattern)
        $renderedFrames = Get-PlainValue $run.Lines 'rendered-frames'
        $visibilityUpdates = Get-PlainValue $run.Lines 'visibility-updates'
        $networkOperations = Get-PlainValue $run.Lines 'network-operations'
        $writesPerformed = Get-PlainValue $run.Lines 'writes-performed'
        Assert-UnsignedDecimal $renderedFrames 'viewer rendered-frames'
        Assert-UnsignedDecimal $visibilityUpdates 'viewer visibility-updates'
        Assert-UnsignedDecimal $networkOperations 'viewer network-operations'
        Assert-UnsignedDecimal $writesPerformed 'viewer writes-performed'
        if ([uint64]$matched.Groups[1].Value -ne [uint64]$Frames -or
            [uint64]$matched.Groups[2].Value -eq 0 -or
            [uint64]$matched.Groups[3].Value -eq 0 -or
            [uint64]$renderedFrames -ne [uint64]$Frames -or
            [uint64]$visibilityUpdates -eq 0 -or
            [uint64]$visibilityUpdates -gt [uint64]$Frames -or
            $networkOperations -cne '0' -or $writesPerformed -cne '0') {
            throw "Viewer wall-contact counters are invalid for $map"
        }
        ++$glRuns
        Write-Output (
            '[wall-contact-verify] map={0} rendered-frames={1} opengl=success' -f
                $map, $Frames)
    }
}
catch {
    $primaryError = $_
}
finally {
    try {
        $afterMaps = Get-MapSnapshot $mapPaths
        $afterInventory = Get-RootInventory $root $Game
        Assert-DictionaryEqual $beforeMaps $afterMaps 'BSP snapshot'
        Assert-DictionaryEqual $beforeInventory $afterInventory 'Root inventory'
    }
    catch {
        $driftError = $_
    }
}

if ($null -ne $primaryError) {
    if ($null -ne $driftError) {
        $message = 'Verification failed: {0}{1}Drift audit also failed: {2}' -f
            $primaryError.Exception.Message, [Environment]::NewLine,
            $driftError.Exception.Message
        throw [System.InvalidOperationException]::new(
            $message, $primaryError.Exception)
    }
    throw $primaryError
}
if ($null -ne $driftError) {
    throw $driftError
}

Write-Output "[wall-contact-verify] cpu-runs=$cpuRuns"
Write-Output "[wall-contact-verify] opengl-runs=$glRuns"
Write-Output "[wall-contact-verify] opengl-capability-skips=$glSkips"
Write-Output '[wall-contact-verify] created-files=0'
Write-Output '[wall-contact-verify] deleted-files=0'
Write-Output '[wall-contact-verify] network-operations=0'
Write-Output '[wall-contact-verify] external-file-drift=none'
Write-Output '[wall-contact-verify] result=success'
