#requires -Version 5.1

<#
.SYNOPSIS
Verifies stock BSP geometry compatibility without network, graphics, or writes.

.DESCRIPTION
Runs the CPU-only BSP compatibility checker twice for every safe virtual map,
requires identical successful spatial-scene summaries, and proves that the
selected BSP files, relevant WAD files, and game-root inventory did not drift.
Success output contains only bounded counts, map ordinals, a fixed category,
and deterministic summary hashes. Native paths and asset-derived names are
never printed.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ToolPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Basedir,

    [ValidateNotNullOrEmpty()]
    [string]$Game = 'valve',

    [ValidateNotNullOrEmpty()]
    [string[]]$Maps = @(
        'maps/boot_camp.bsp',
        'maps/crossfire.bsp',
        'maps/stalkyard.bsp'
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$MaximumMaps = 64
$MaximumInventoryEntries = 200000
$MaximumSnapshotFileBytes = 67108864
$MaximumCheckerOutputLines = 32
$MaximumCheckerOutputBytes = 16384
$MaximumCounter = [uint64]1000000000

function Throw-VerificationFailure {
    throw [InvalidOperationException]::new(
        'Stock BSP geometry compatibility verification failed.')
}

function Get-Sha256Text {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Text
    )

    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $encoding = New-Object Text.UTF8Encoding($false)
        $digest = $algorithm.ComputeHash($encoding.GetBytes($Text))
        return ([BitConverter]::ToString($digest)).Replace(
            '-', '').ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

function Assert-FixedLocalPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][bool]$RequireDirectory
    )

    if ($Path.IndexOf([char]0) -ge 0 -or
        $Path -notmatch '^[A-Za-z]:[\\/]' -or
        $Path -match '^[\\/]{2}') {
        Throw-VerificationFailure
    }

    $full = [IO.Path]::GetFullPath($Path)
    $driveRoot = [IO.Path]::GetPathRoot($full)
    $drive = [IO.DriveInfo]::new($driveRoot)
    if (-not $drive.IsReady -or
        $drive.DriveType -ne [IO.DriveType]::Fixed) {
        Throw-VerificationFailure
    }

    $current = $driveRoot
    $components = @($full.Substring($driveRoot.Length).Split(
        [char[]]@('\', '/'),
        [StringSplitOptions]::RemoveEmptyEntries))
    if ($components.Count -eq 0) {
        Throw-VerificationFailure
    }
    for ($index = 0; $index -lt $components.Count; ++$index) {
        $component = $components[$index]
        if ($component -eq '.' -or $component -eq '..') {
            Throw-VerificationFailure
        }
        $current = Join-Path -Path $current -ChildPath $component
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Throw-VerificationFailure
        }
        $isFinal = $index -eq $components.Count - 1
        if ((-not $isFinal -and -not $item.PSIsContainer) -or
            ($isFinal -and $item.PSIsContainer -ne $RequireDirectory)) {
            Throw-VerificationFailure
        }
    }
    return $full.TrimEnd([char[]]@('\', '/'))
}

function Assert-SafeGameDirectory {
    if ($Game.Length -gt 64 -or
        $Game -notmatch '^[A-Za-z0-9_-]+$') {
        Throw-VerificationFailure
    }
}

function Assert-SafeVirtualMap {
    param([Parameter(Mandatory = $true)][string]$VirtualMap)

    if ([string]::IsNullOrEmpty($VirtualMap) -or
        $VirtualMap.Length -gt 1024 -or
        $VirtualMap -notmatch '^[\x21-\x7E]+$' -or
        $VirtualMap.Contains('\') -or $VirtualMap.Contains(':') -or
        $VirtualMap.StartsWith('/') -or $VirtualMap.EndsWith('/') -or
        $VirtualMap.Contains('//') -or
        $VirtualMap.IndexOfAny([char[]]@('*', '?', '"', '<', '>', '|')) -ge 0) {
        Throw-VerificationFailure
    }

    $segments = @($VirtualMap.Split('/'))
    if ($segments.Count -lt 2 -or $segments[0] -cne 'maps' -or
        $segments[-1] -notmatch '(?i)\.bsp$') {
        Throw-VerificationFailure
    }
    foreach ($segment in $segments) {
        if ([string]::IsNullOrEmpty($segment) -or
            $segment -eq '.' -or $segment -eq '..' -or
            $segment.Length -gt 255 -or
            $segment.EndsWith('.') -or $segment.EndsWith(' ')) {
            Throw-VerificationFailure
        }
    }
}

function Get-FileSnapshot {
    param([Parameter(Mandatory = $true)][string]$Path)

    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or
        (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) -or
        $item.Length -lt 1 -or $item.Length -gt $MaximumSnapshotFileBytes) {
        Throw-VerificationFailure
    }

    $stream = [IO.File]::Open(
        $item.FullName,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    try {
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try {
            $digest = $algorithm.ComputeHash($stream)
        }
        finally {
            $algorithm.Dispose()
        }
        $item.Refresh()
        return [pscustomobject]@{
            Content = ([BitConverter]::ToString($digest)).Replace(
                '-', '').ToLowerInvariant()
            Length = [int64]$item.Length
            WriteTime = [int64]$item.LastWriteTimeUtc.Ticks
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Get-InventorySnapshot {
    param([Parameter(Mandatory = $true)][string[]]$Roots)

    $rows = [Collections.Generic.List[string]]::new()
    for ($rootIndex = 0; $rootIndex -lt $Roots.Count; ++$rootIndex) {
        $rootItem = Get-Item -LiteralPath $Roots[$rootIndex] `
            -Force -ErrorAction Stop
        foreach ($entry in Get-ChildItem -LiteralPath $rootItem.FullName `
                -Force -Recurse -ErrorAction Stop) {
            if ($rows.Count -ge $MaximumInventoryEntries -or
                (($entry.Attributes -band
                    [IO.FileAttributes]::ReparsePoint) -ne 0)) {
                Throw-VerificationFailure
            }
            $relative = $entry.FullName.Substring($rootItem.FullName.Length).
                TrimStart([char[]]@('\', '/'))
            $length = if ($entry.PSIsContainer) {
                -1
            } else {
                [int64]$entry.Length
            }
            $rows.Add(('{0}|{1}|{2}|{3}|{4}' -f @(
                $rootIndex,
                $relative,
                [int]$entry.Attributes,
                $length,
                $entry.LastWriteTimeUtc.Ticks)))
        }
    }
    $rows.Sort([StringComparer]::Ordinal)
    return [pscustomobject]@{
        Count = $rows.Count
        Digest = Get-Sha256Text -Text ($rows.ToArray() -join "`n")
    }
}

function Get-WadSnapshot {
    param([Parameter(Mandatory = $true)][string[]]$Roots)

    $rows = [Collections.Generic.List[string]]::new()
    for ($rootIndex = 0; $rootIndex -lt $Roots.Count; ++$rootIndex) {
        $root = Get-Item -LiteralPath $Roots[$rootIndex] `
            -Force -ErrorAction Stop
        foreach ($wad in Get-ChildItem -LiteralPath $root.FullName `
                -Filter '*.wad' -File -Force -Recurse -ErrorAction Stop) {
            if ($rows.Count -ge $MaximumInventoryEntries -or
                (($wad.Attributes -band
                    [IO.FileAttributes]::ReparsePoint) -ne 0)) {
                Throw-VerificationFailure
            }
            $snapshot = Get-FileSnapshot -Path $wad.FullName
            $relative = $wad.FullName.Substring($root.FullName.Length).
                TrimStart([char[]]@('\', '/'))
            $rows.Add(('{0}|{1}|{2}|{3}|{4}' -f @(
                $rootIndex,
                $relative,
                $snapshot.Content,
                $snapshot.Length,
                $snapshot.WriteTime)))
        }
    }
    $rows.Sort([StringComparer]::Ordinal)
    return [pscustomobject]@{
        Count = $rows.Count
        Digest = Get-Sha256Text -Text ($rows.ToArray() -join "`n")
    }
}

function Resolve-MapTarget {
    param(
        [Parameter(Mandatory = $true)][string]$VirtualMap,
        [Parameter(Mandatory = $true)][string[]]$Roots
    )

    $segments = @($VirtualMap.Split('/'))
    foreach ($root in $Roots) {
        $current = $root
        $validCandidate = $true
        for ($index = 0; $index -lt $segments.Count; ++$index) {
            $current = Join-Path -Path $current -ChildPath $segments[$index]
            if (-not (Test-Path -LiteralPath $current)) {
                $validCandidate = $false
                break
            }
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if (($item.Attributes -band
                    [IO.FileAttributes]::ReparsePoint) -ne 0 -or
                ($index -lt $segments.Count - 1 -and
                    -not $item.PSIsContainer) -or
                ($index -eq $segments.Count - 1 -and
                    $item.PSIsContainer)) {
                Throw-VerificationFailure
            }
        }
        if ($validCandidate) {
            $full = [IO.Path]::GetFullPath($current)
            $prefix = $root.TrimEnd([char[]]@('\', '/')) +
                [IO.Path]::DirectorySeparatorChar
            if (-not $full.StartsWith(
                    $prefix, [StringComparison]::OrdinalIgnoreCase)) {
                Throw-VerificationFailure
            }
            return $full
        }
    }
    Throw-VerificationFailure
}

function Convert-ExactCounter {
    param([Parameter(Mandatory = $true)][string]$Text)

    if ($Text -notmatch '^(0|[1-9][0-9]*)$') {
        Throw-VerificationFailure
    }
    $value = [uint64]0
    if (-not [uint64]::TryParse(
            $Text,
            [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$value) -or
        $value -gt $MaximumCounter) {
        Throw-VerificationFailure
    }
    return $value
}

function Convert-ExactFiniteNumber {
    param([Parameter(Mandatory = $true)][string]$Text)

    if ($Text.Length -gt 64 -or
        $Text -notmatch '^-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?$') {
        Throw-VerificationFailure
    }
    $value = [double]0.0
    if (-not [double]::TryParse(
            $Text,
            [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture,
            [ref]$value) -or
        [double]::IsNaN($value) -or [double]::IsInfinity($value)) {
        Throw-VerificationFailure
    }
    return $value
}

function Read-CheckerSummary {
    param([Parameter(Mandatory = $true)][string[]]$Output)

    $fieldNames = @(
        'bsp-version',
        'models',
        'world-faces',
        'brush-faces',
        'canonicalized-faces',
        'removed-collinear-corners',
        'min-winding-dot',
        'max-planarity-error',
        'vertices',
        'triangles',
        'textures',
        'lightmap-pages',
        'pvs-rows',
        'brush-models',
        'supported-instances',
        'result'
    )
    if ($Output.Count -ne $fieldNames.Count -or
        $Output.Count -gt $MaximumCheckerOutputLines) {
        Throw-VerificationFailure
    }

    $normalized = $Output -join "`n"
    if ([Text.Encoding]::UTF8.GetByteCount($normalized) -gt
        $MaximumCheckerOutputBytes) {
        Throw-VerificationFailure
    }

    $values = [ordered]@{}
    for ($index = 0; $index -lt $fieldNames.Count; ++$index) {
        $name = $fieldNames[$index]
        $pattern = '^\[compat\] ' + [regex]::Escape($name) + '=([^\r\n]+)$'
        if ($Output[$index] -notmatch $pattern) {
            Throw-VerificationFailure
        }
        $values[$name] = $Matches[1]
    }

    $bspVersion = Convert-ExactCounter -Text $values['bsp-version']
    $models = Convert-ExactCounter -Text $values['models']
    $worldFaces = Convert-ExactCounter -Text $values['world-faces']
    $brushFaces = Convert-ExactCounter -Text $values['brush-faces']
    $canonicalizedFaces = Convert-ExactCounter `
        -Text $values['canonicalized-faces']
    $removedCorners = Convert-ExactCounter `
        -Text $values['removed-collinear-corners']
    $minimumWinding = Convert-ExactFiniteNumber `
        -Text $values['min-winding-dot']
    $maximumPlanarity = Convert-ExactFiniteNumber `
        -Text $values['max-planarity-error']
    $vertices = Convert-ExactCounter -Text $values['vertices']
    $triangles = Convert-ExactCounter -Text $values['triangles']
    $textures = Convert-ExactCounter -Text $values['textures']
    $lightmapPages = Convert-ExactCounter -Text $values['lightmap-pages']
    $pvsRows = Convert-ExactCounter -Text $values['pvs-rows']
    $brushModels = Convert-ExactCounter -Text $values['brush-models']
    $supportedInstances = Convert-ExactCounter `
        -Text $values['supported-instances']

    if ($bspVersion -ne [uint64]30 -or $models -lt [uint64]1 -or
        $worldFaces -lt [uint64]1 -or
        $canonicalizedFaces -ne ($worldFaces + $brushFaces) -or
        ($brushModels + [uint64]1) -ne $models -or
        $minimumWinding -le 0.0 -or
        $maximumPlanarity -lt 0.0 -or $maximumPlanarity -gt 0.02 -or
        $vertices -lt [uint64]3 -or $triangles -lt [uint64]1 -or
        $textures -lt [uint64]1 -or
        $lightmapPages -lt [uint64]1 -or
        $pvsRows -lt [uint64]1 -or
        $values['result'] -cne 'success') {
        Throw-VerificationFailure
    }

    return [pscustomobject]@{
        Normalized = $normalized
        Hash = Get-Sha256Text -Text $normalized
        Models = $models
        WorldFaces = $worldFaces
        BrushFaces = $brushFaces
        CanonicalizedFaces = $canonicalizedFaces
        RemovedCorners = $removedCorners
        Vertices = $vertices
        Triangles = $triangles
        Textures = $textures
        LightmapPages = $lightmapPages
        PvsRows = $pvsRows
        BrushModels = $brushModels
        SupportedInstances = $supportedInstances
    }
}

function Invoke-CompatibilityChecker {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$GameDirectory,
        [Parameter(Mandatory = $true)][string]$VirtualMap
    )

    [string[]]$output = @(& $Executable --basedir $Root `
        --game $GameDirectory --map $VirtualMap `
        --validate-through spatial-scene 2>&1 |
        ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) {
        Throw-VerificationFailure
    }
    return Read-CheckerSummary -Output $output
}

try {
    Assert-SafeGameDirectory
    if ($Maps.Count -lt 1 -or $Maps.Count -gt $MaximumMaps) {
        Throw-VerificationFailure
    }

    $mapNames = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($virtualMap in $Maps) {
        Assert-SafeVirtualMap -VirtualMap $virtualMap
        if (-not $mapNames.Add($virtualMap)) {
            Throw-VerificationFailure
        }
    }

    $toolFullPath = [IO.Path]::GetFullPath($ToolPath)
    $tool = Assert-FixedLocalPath -Path $toolFullPath `
        -RequireDirectory $false
    $toolItem = Get-Item -LiteralPath $tool -Force -ErrorAction Stop
    if ($toolItem.Extension -ine '.exe' -or $toolItem.Length -lt 1) {
        Throw-VerificationFailure
    }

    $base = Assert-FixedLocalPath -Path $Basedir -RequireDirectory $true
    $roots = [Collections.Generic.List[string]]::new()
    $seenRoots = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($rootName in @($Game, 'valve')) {
        $root = Assert-FixedLocalPath `
            -Path (Join-Path -Path $base -ChildPath $rootName) `
            -RequireDirectory $true
        if ($seenRoots.Add($root)) {
            $roots.Add($root)
        }
    }
    $rootArray = $roots.ToArray()

    $selectedMaps = [Collections.Generic.List[object]]::new()
    foreach ($virtualMap in $Maps) {
        $target = Resolve-MapTarget -VirtualMap $virtualMap -Roots $rootArray
        $selectedMaps.Add([pscustomobject]@{
            VirtualMap = $virtualMap
            Target = $target
            Before = Get-FileSnapshot -Path $target
        })
    }

    $inventoryBefore = Get-InventorySnapshot -Roots $rootArray
    $wadsBefore = Get-WadSnapshot -Roots $rootArray
    $summaries = [Collections.Generic.List[object]]::new()
    $ordinal = 0
    foreach ($selectedMap in $selectedMaps) {
        ++$ordinal
        $first = Invoke-CompatibilityChecker `
            -Executable $toolItem.FullName -Root $base `
            -GameDirectory $Game -VirtualMap $selectedMap.VirtualMap
        $second = Invoke-CompatibilityChecker `
            -Executable $toolItem.FullName -Root $base `
            -GameDirectory $Game -VirtualMap $selectedMap.VirtualMap
        if ($first.Normalized -cne $second.Normalized -or
            $first.Hash -cne $second.Hash) {
            Throw-VerificationFailure
        }
        $summaries.Add([pscustomobject]@{
            Ordinal = $ordinal
            Summary = $first
        })
    }

    $inventoryAfter = Get-InventorySnapshot -Roots $rootArray
    $wadsAfter = Get-WadSnapshot -Roots $rootArray
    if ($inventoryAfter.Count -ne $inventoryBefore.Count -or
        $inventoryAfter.Digest -cne $inventoryBefore.Digest -or
        $wadsAfter.Count -ne $wadsBefore.Count -or
        $wadsAfter.Digest -cne $wadsBefore.Digest) {
        Throw-VerificationFailure
    }
    foreach ($selectedMap in $selectedMaps) {
        $after = Get-FileSnapshot -Path $selectedMap.Target
        if ($after.Content -cne $selectedMap.Before.Content -or
            $after.Length -ne $selectedMap.Before.Length -or
            $after.WriteTime -ne $selectedMap.Before.WriteTime) {
            Throw-VerificationFailure
        }
    }

    $aggregateRows = [Collections.Generic.List[string]]::new()
    Write-Output 'stock-bsp-geometry-compatibility-verification=passed'
    Write-Output ('maps-verified=' + $summaries.Count)
    Write-Output ('checker-runs=' + ($summaries.Count * 2))
    foreach ($entry in $summaries) {
        $summary = $entry.Summary
        $aggregateRows.Add(('{0}|{1}' -f $entry.Ordinal, $summary.Hash))
        Write-Output (('map-ordinal={0};map-category=stock-bsp-v30;' +
            'models={1};world-faces={2};brush-faces={3};' +
            'canonicalized-faces={4};removed-collinear-corners={5};' +
            'vertices={6};triangles={7};textures={8};' +
            'lightmap-pages={9};pvs-rows={10};brush-models={11};' +
            'supported-instances={12};summary-sha256={13}') -f @(
                $entry.Ordinal,
                $summary.Models,
                $summary.WorldFaces,
                $summary.BrushFaces,
                $summary.CanonicalizedFaces,
                $summary.RemovedCorners,
                $summary.Vertices,
                $summary.Triangles,
                $summary.Textures,
                $summary.LightmapPages,
                $summary.PvsRows,
                $summary.BrushModels,
                $summary.SupportedInstances,
                $summary.Hash))
    }
    Write-Output ('summary-sha256=' + (Get-Sha256Text -Text (
        $aggregateRows.ToArray() -join "`n")))
    Write-Output ('wad-files-snapshotted=' + $wadsBefore.Count)
    Write-Output 'network-operations=0'
    Write-Output 'writes-performed=0'
    Write-Output 'created-files=0'
    Write-Output 'deleted-files=0'
    Write-Output 'external-file-drift=none'
}
catch {
    [Console]::Error.WriteLine(
        'stock-bsp-geometry-compatibility-verification=failed')
    exit 1
}
