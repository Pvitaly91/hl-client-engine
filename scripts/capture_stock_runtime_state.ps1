#requires -Version 5.1

<#
.SYNOPSIS
Validates the stock-runtime research preflight and restoration guard.

.DESCRIPTION
Active capture is currently evidence-pending and fails before process launch or
output creation because OS-level outbound isolation and exact app/engine/
protocol/build observation are not implemented. The retained orchestration and
loopback relay are research scaffolding only.

ValidateResearchRoot is read-only with respect to the repository and research
tree and starts no stock/game process. It invokes the read-only Windows
`subst.exe` listing to reject substituted drive aliases.
#>
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'RestorationSelfTest')]
    [switch]$ValidateRestorationGuard,

    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [switch]$ValidateResearchRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [ValidateNotNullOrEmpty()]
    [string]$ClientPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [ValidateNotNullOrEmpty()]
    [string]$HldsPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureToolPath,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedCaptureToolSha256,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateSet('valve')]
    [string]$Game,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateSet('boot_camp', 'crossfire', 'stalkyard')]
    [string]$Map,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateSet(
        'baseline', 'idle-runtime', 'forward', 'backward', 'left', 'right',
        'forward-right', 'jump', 'duck', 'duck-stand', 'yaw-positive',
        'yaw-negative', 'pitch-positive', 'pitch-negative', 'second-client',
        'reconnect', 'map-change', 'server-restart', 'respawn',
        'low-updaterate', 'high-updaterate', 'low-cmdrate', 'high-cmdrate',
        'drop-server-runtime', 'drop-two-server-runtime',
        'duplicate-server-runtime', 'reorder-server-runtime',
        'drop-client-move', 'delay-client-move')]
    [string]$Scenario,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1024, 65534)]
    [int]$RelayPort = 27140,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1024, 65534)]
    [int]$ServerPort = 27141,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$OutputRoot = '.\manual-artifacts\stock-runtime',

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(5, 300)]
    [int]$MaximumDurationSeconds = 45,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65536)]
    [int]$MaximumDatagrams = 8192,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 536870912)]
    [Int64]$MaximumTotalRawBytes = 67108864,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65507)]
    [int]$MaximumPayloadBytes = 65507,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 67108864)]
    [int]$MaximumReassembledBytes = 8388608,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 268435456)]
    [int]$MaximumDecompressedBytes = 33554432,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65536)]
    [int]$MaximumMessageCount = 8192,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 32768)]
    [int]$MaximumRuntimeFrames = 4096,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65536)]
    [int]$MaximumClientPackets = 4096,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65536)]
    [int]$MaximumServerPackets = 4096,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65536)]
    [int]$MutationAfterClientPackets = 20,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65536)]
    [int]$MutationAfterServerPackets = 20
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$manualRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'manual-artifacts')).TrimEnd('\', '/')
$requiredOutputRoot = [IO.Path]::GetFullPath((Join-Path $manualRoot 'stock-runtime')).TrimEnd('\', '/')
$markerName = '.hlclient-research-isolated'
$markerText = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
$maximumEntries = 199999
$maximumResearchBytes = [Int64]17179869184
$maximumSteamManifestBytes = 1048576
$protectedRoots = @(
    'config.cfg', 'userconfig.cfg', 'autoexec.cfg', 'custom.hpk',
    'qconsole.log', 'hlds.log', 'logs', 'screenshots', 'save', 'demo', 'demos',
    'valve/config.cfg', 'valve/userconfig.cfg', 'valve/autoexec.cfg',
    'valve/custom.hpk', 'valve/qconsole.log', 'valve/hlds.log', 'valve/logs',
    'valve/screenshots', 'valve/save', 'valve/demo', 'valve/demos',
    'valve/config', 'platform/config')

function Test-PathAtOrBelow {
    param([string]$Path, [string]$Root)
    $pathValue = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $rootValue = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    return $pathValue.Equals($rootValue, [StringComparison]::OrdinalIgnoreCase) -or
        $pathValue.StartsWith(
            $rootValue + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)
}

function Assert-PathBelowRoot {
    param([string]$Path, [string]$Root, [string]$Label)
    $pathValue = [IO.Path]::GetFullPath($Path)
    $rootValue = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not $pathValue.StartsWith(
            $rootValue + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be a canonical descendant of its exact root."
    }
}

function Assert-NoReparsePoint {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a reparse point."
    }
}

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $pathRoot = [IO.Path]::GetPathRoot($fullPath)
    $current = $pathRoot
    foreach ($component in @($fullPath.Substring($pathRoot.Length) -split '[\\/]' |
            Where-Object { $_ })) {
        $current = [IO.Path]::Combine($current, $component)
        if (Test-Path -LiteralPath $current) {
            Assert-NoReparsePoint -Path $current -Label $Label
        }
    }
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    $streams = @(Get-Item -LiteralPath $Path -Stream * -ErrorAction Stop)
    if (@($streams | Where-Object { $_.Stream -cne ':$DATA' }).Count -ne 0) {
        throw "$Label must contain only its default data stream."
    }
}

function Assert-NoHardLink {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) { return }
    $property = $item.PSObject.Properties['LinkType']
    if ($null -eq $property) {
        throw "$Label hard-link state could not be established."
    }
    if (-not [string]::IsNullOrEmpty([string]$property.Value)) {
        throw "$Label must not be linked."
    }
}

function Get-BoundedItems {
    param([string]$Root)
    $items = [Collections.Generic.List[object]]::new()
    $queue = [Collections.Generic.Queue[IO.DirectoryInfo]]::new()
    $queue.Enqueue([IO.DirectoryInfo](Get-Item -LiteralPath $Root -Force))
    while ($queue.Count -ne 0) {
        $directory = $queue.Dequeue()
        Assert-NoReparsePoint -Path $directory.FullName -Label 'research tree'
        foreach ($item in @($directory.GetFileSystemInfos())) {
            if ($items.Count -ge $maximumEntries) {
                throw 'Research tree exceeds its entry bound.'
            }
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Research tree contains a reparse point.'
            }
            [void]$items.Add($item)
            if (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                $queue.Enqueue([IO.DirectoryInfo]$item)
            }
        }
    }
    return @($items)
}

function Get-FileSha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Read-BoundedAsciiMarker {
    param([string]$Path, [int]$MaximumBytes = 128)
    $stream = [IO.File]::Open(
        $Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 1 -or $stream.Length -gt $MaximumBytes) {
            throw 'Research marker length is outside its bound.'
        }
        $bytes = [byte[]]::new([int]$stream.Length)
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) { throw 'Research marker ended before its declared length.' }
            $offset += $read
        }
        if (@($bytes | Where-Object { $_ -gt 0x7F }).Count -ne 0) {
            throw 'Research marker must contain ASCII only.'
        }
        return [Text.Encoding]::ASCII.GetString($bytes)
    } finally {
        $stream.Dispose()
    }
}

function Get-RelativePath {
    param([string]$Path, [string]$Root)
    $prefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Research entry escaped its root.'
    }
    return $full.Substring($prefix.Length).Replace('\', '/')
}

function Get-ResearchSnapshot {
    param([string]$Root)
    $entries = [Collections.Generic.List[object]]::new()
    $rootItem = Get-Item -LiteralPath $Root -Force
    [void]$entries.Add([pscustomobject]@{
        RelativePath = '.'; Kind = 'directory'; Length = [Int64]0; Sha256 = ''
        CreationTicks = $rootItem.CreationTimeUtc.Ticks
        WriteTicks = $rootItem.LastWriteTimeUtc.Ticks
        Attributes = [Int64]$rootItem.Attributes
    })
    [Int64]$totalBytes = 0
    foreach ($item in @(Get-BoundedItems -Root $Root | Sort-Object FullName)) {
        $relative = Get-RelativePath -Path $item.FullName -Root $Root
        Assert-OnlyDefaultDataStream -Path $item.FullName -Label 'research entry'
        $isDirectory = ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0
        if (-not $isDirectory) { Assert-NoHardLink -Path $item.FullName -Label 'research file' }
        [Int64]$length = if ($isDirectory) { 0 } else { $item.Length }
        if ($length -lt 0 -or $totalBytes -gt ($maximumResearchBytes - $length)) {
            throw 'Research tree exceeds its byte bound.'
        }
        $totalBytes += $length
        [void]$entries.Add([pscustomobject]@{
            RelativePath = $relative
            Kind = $(if ($isDirectory) { 'directory' } else { 'file' })
            Length = $length
            Sha256 = $(if ($isDirectory) { '' } else { Get-FileSha256 $item.FullName })
            CreationTicks = $item.CreationTimeUtc.Ticks
            WriteTicks = $item.LastWriteTimeUtc.Ticks
            Attributes = [Int64]$item.Attributes
        })
    }
    $canonical = @($entries | ForEach-Object {
        '{0}|{1}|{2}|{3}|{4}|{5}|{6}' -f $_.RelativePath, $_.Kind,
            $_.Length, $_.Sha256, $_.CreationTicks, $_.WriteTicks, $_.Attributes
    }) -join "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($canonical)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $manifest = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
    return [pscustomobject]@{
        Entries = @($entries); EntryCount = $entries.Count
        TotalBytes = $totalBytes; ManifestSha256 = $manifest
    }
}

function Test-ProtectedPath {
    param([string]$RelativePath)
    $normalized = $RelativePath.Replace('\', '/').TrimStart('/')
    if ($normalized.EndsWith('.dem', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    foreach ($root in $protectedRoots) {
        if ($normalized.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
            $normalized.StartsWith($root + '/', [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function New-RestorationGuard {
    param([string]$Root, [object]$Snapshot)
    $temporary = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) (
        'hlclient-stock-runtime-restore-' + [Guid]::NewGuid().ToString('N'))))
    if ((Test-PathAtOrBelow $temporary $Root) -or
        (Test-PathAtOrBelow $temporary $repositoryRoot)) {
        throw 'Restoration backup root is not disjoint.'
    }
    [Int64]$headroom = 67108864
    if ([Int64]$Snapshot.TotalBytes -gt ([Int64]::MaxValue - $headroom)) {
        throw 'Restoration backup size overflowed.'
    }
    $temporaryDrive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($temporary))
    if (-not $temporaryDrive.IsReady -or
        $temporaryDrive.AvailableFreeSpace -lt ([Int64]$Snapshot.TotalBytes + $headroom)) {
        throw 'Temporary storage lacks space for a full transactional research backup.'
    }
    Assert-NoReparsePointInExistingPath $temporary 'restoration backup path'
    [IO.Directory]::CreateDirectory($temporary) | Out-Null
    Assert-NoReparsePointInExistingPath $temporary 'restoration backup path'
    Assert-NoReparsePoint $temporary 'restoration backup root'
    $data = Join-Path $temporary 'data'
    [IO.Directory]::CreateDirectory($data) | Out-Null
    Assert-NoReparsePoint $data 'restoration backup data root'
    $backed = [Collections.Generic.List[object]]::new()
    try {
        # Back up the complete bounded tree. A whitelist-only backup can detect
        # drift outside known mutable paths but cannot restore it transactionally.
        foreach ($entry in @($Snapshot.Entries | Where-Object {
                    $_.RelativePath -ne '.'
                } | Sort-Object RelativePath)) {
            $source = Join-Path $Root $entry.RelativePath.Replace('/', '\')
            $destination = Join-Path $data $entry.RelativePath.Replace('/', '\')
            Assert-PathBelowRoot $source $Root 'restoration source'
            Assert-PathBelowRoot $destination $data 'restoration destination'
            if ($entry.Kind -eq 'directory') {
                [IO.Directory]::CreateDirectory($destination) | Out-Null
            } else {
                [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
                [IO.File]::Copy($source, $destination, $false)
                if ((Get-FileSha256 $destination) -cne $entry.Sha256) {
                    throw 'Restoration backup digest mismatch.'
                }
            }
            [void]$backed.Add($entry)
        }
        return [pscustomobject]@{
            Root = $Root; TemporaryRoot = $temporary; DataRoot = $data
            Before = $Snapshot; BackedEntries = @($backed)
        }
    } catch {
        throw "Restoration backup failed; inspect '$temporary': $($_.Exception.Message)"
    }
}

function Remove-SafeEntry {
    param([string]$Path, [string]$Root)
    Assert-PathBelowRoot $Path $Root 'restoration removal target'
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return }
    $isDirectory = ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0
    if ($isDirectory -and
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 -and
        @($item.GetFileSystemInfos()).Count -ne 0) {
        throw 'Refusing to remove a non-empty restoration directory.'
    }
    $isReparse = ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    if ($isReparse) {
        if ($isDirectory) { [IO.Directory]::Delete($Path, $false) }
        else { [IO.File]::Delete($Path) }
        return
    }
    if (-not $isDirectory) {
        $linkProperty = $item.PSObject.Properties['LinkType']
        if ($null -eq $linkProperty) {
            throw 'Restoration removal could not establish file link state.'
        }
        if ([string]::IsNullOrEmpty([string]$linkProperty.Value)) {
            $item.Attributes = [IO.FileAttributes]::Normal
        }
        # File.Delete removes one directory entry. In particular, it does not
        # open and rewrite a hostile hard-link target before unlinking it.
        [IO.File]::Delete($Path)
        return
    }
    $item.Attributes = [IO.FileAttributes]::Normal
    Remove-Item -LiteralPath $Path -Force
}

function Remove-SafeTree {
    param([string]$Path, [string]$Root)
    Assert-PathBelowRoot $Path $Root 'restoration tree target'
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return }
    if ((($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) -and
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        foreach ($descendant in @(Get-CurrentItemsNoTraversalThroughLinks $Path |
                Sort-Object { $_.FullName.Split([IO.Path]::DirectorySeparatorChar).Count } `
                    -Descending)) {
            Remove-SafeEntry $descendant.FullName $Root
        }
    }
    Remove-SafeEntry $Path $Root
}

function Get-CurrentItemsNoTraversalThroughLinks {
    param([string]$Root)
    $items = [Collections.Generic.List[object]]::new()
    $queue = [Collections.Generic.Queue[IO.DirectoryInfo]]::new()
    $queue.Enqueue([IO.DirectoryInfo](Get-Item -LiteralPath $Root -Force))
    while ($queue.Count -ne 0) {
        foreach ($item in @($queue.Dequeue().GetFileSystemInfos())) {
            if ($items.Count -ge $maximumEntries) { throw 'Restoration enumeration bound exceeded.' }
            [void]$items.Add($item)
            if ((($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) -and
                ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
                $queue.Enqueue([IO.DirectoryInfo]$item)
            }
        }
    }
    return @($items)
}

function Restore-ResearchState {
    param([object]$Guard)
    $initial = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $initialKinds = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $Guard.Before.Entries) {
        [void]$initial.Add($entry.RelativePath)
        $initialKinds.Add($entry.RelativePath, $entry.Kind)
    }

    # Never traverse a replaced research root or one reached through a replaced
    # ancestor. Revalidate the backup chain before using any retained byte.
    Assert-NoReparsePointInExistingPath $Guard.Root 'research restoration root'
    Assert-PathBelowRoot $Guard.DataRoot $Guard.TemporaryRoot `
        'restoration backup data root'
    Assert-NoReparsePointInExistingPath $Guard.TemporaryRoot `
        'restoration backup root'
    Assert-NoReparsePointInExistingPath $Guard.DataRoot `
        'restoration backup data root'

    # Remove new entries deepest-first, without traversing any new reparse point.
    foreach ($item in @(Get-CurrentItemsNoTraversalThroughLinks $Guard.Root | Sort-Object {
                (Get-RelativePath $_.FullName $Guard.Root).Split('/').Count
            } -Descending)) {
        $relative = Get-RelativePath $item.FullName $Guard.Root
        if (-not $initial.Contains($relative)) {
            Remove-SafeEntry $item.FullName $Guard.Root
        }
    }

    # Use one no-follow inventory and remove hostile original-path reparses or
    # type conflicts shallowest-first. Descendants of a reparse point were not
    # enumerated, so no descendant operation can escape through that link.
    foreach ($item in @(Get-CurrentItemsNoTraversalThroughLinks $Guard.Root |
            Sort-Object { (Get-RelativePath $_.FullName $Guard.Root).Split('/').Count })) {
        $relative = Get-RelativePath $item.FullName $Guard.Root
        if ($initial.Contains($relative)) {
            $isReparse = ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
            $actualKind = if (-not $isReparse -and
                ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                'directory'
            } else { 'file' }
            if ($isReparse -or $actualKind -cne $initialKinds[$relative]) {
                Remove-SafeTree $item.FullName $Guard.Root
            }
        }
    }
    foreach ($entry in @($Guard.BackedEntries | Where-Object Kind -eq 'directory' |
            Sort-Object { $_.RelativePath.Split('/').Count })) {
        $target = Join-Path $Guard.Root $entry.RelativePath.Replace('/', '\')
        $parent = Split-Path -Parent $target
        Assert-NoReparsePointInExistingPath $parent 'restoration directory parent'
        [IO.Directory]::CreateDirectory($target) | Out-Null
        Assert-NoReparsePointInExistingPath $target 'restoration directory'
    }
    foreach ($entry in @($Guard.BackedEntries | Where-Object Kind -eq 'file' | Sort-Object RelativePath)) {
        $source = Join-Path $Guard.DataRoot $entry.RelativePath.Replace('/', '\')
        $target = Join-Path $Guard.Root $entry.RelativePath.Replace('/', '\')
        Assert-PathBelowRoot $source $Guard.DataRoot 'restoration backup file'
        Assert-NoReparsePointInExistingPath $source 'restoration backup file'
        Assert-OnlyDefaultDataStream $source 'restoration backup file'
        Assert-NoHardLink $source 'restoration backup file'
        if ((Get-FileSha256 $source) -cne $entry.Sha256) {
            throw 'Restoration backup file digest changed before restore.'
        }
        Assert-PathBelowRoot $target $Guard.Root 'restoration file'
        $parent = Split-Path -Parent $target
        Assert-NoReparsePointInExistingPath $parent 'restoration file parent'
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        $existing = Get-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
        if ($null -ne $existing) { Remove-SafeTree $target $Guard.Root }
        Assert-NoReparsePointInExistingPath $parent 'restoration file parent'
        # Never overwrite: a raced-in link or file makes Copy fail closed.
        [IO.File]::Copy($source, $target, $false)
        Assert-NoReparsePoint $target 'restored file'
        Assert-NoHardLink $target 'restored file'
        $item = Get-Item -LiteralPath $target -Force
        $item.CreationTimeUtc = [DateTime]::new([Int64]$entry.CreationTicks, [DateTimeKind]::Utc)
        $item.LastWriteTimeUtc = [DateTime]::new([Int64]$entry.WriteTicks, [DateTimeKind]::Utc)
        $item.Attributes = [IO.FileAttributes]([Int64]$entry.Attributes)
    }
    foreach ($entry in @($Guard.Before.Entries | Where-Object Kind -eq 'directory' |
            Sort-Object { $_.RelativePath.Split('/').Count } -Descending)) {
        $target = if ($entry.RelativePath -eq '.') { $Guard.Root } else {
            Join-Path $Guard.Root $entry.RelativePath.Replace('/', '\')
        }
        Assert-NoReparsePointInExistingPath $target 'restored directory'
        $item = Get-Item -LiteralPath $target -Force
        $item.CreationTimeUtc = [DateTime]::new([Int64]$entry.CreationTicks, [DateTimeKind]::Utc)
        $item.LastWriteTimeUtc = [DateTime]::new([Int64]$entry.WriteTicks, [DateTimeKind]::Utc)
        $item.Attributes = [IO.FileAttributes]([Int64]$entry.Attributes)
    }
    $after = Get-ResearchSnapshot $Guard.Root
    if ($after.EntryCount -ne $Guard.Before.EntryCount -or
        $after.TotalBytes -ne $Guard.Before.TotalBytes -or
        $after.ManifestSha256 -cne $Guard.Before.ManifestSha256) {
        throw 'Research restoration detected external-file drift.'
    }
    return $after
}

function Get-KnownSteamRoots {
    $roots = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($registryPath in @('HKCU:\Software\Valve\Steam',
            'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        if (-not (Test-Path -LiteralPath $registryPath)) { continue }
        $record = Get-ItemProperty -LiteralPath $registryPath
        foreach ($name in @('SteamPath', 'InstallPath')) {
            $property = $record.PSObject.Properties[$name]
            if ($null -ne $property -and -not [string]::IsNullOrWhiteSpace([string]$property.Value)) {
                [void]$roots.Add([IO.Path]::GetFullPath([string]$property.Value))
            }
        }
    }
    foreach ($root in @($roots)) {
        $libraries = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $libraries -PathType Leaf)) { continue }
        if ((Get-Item -LiteralPath $libraries).Length -gt $maximumSteamManifestBytes) {
            throw 'Steam library manifest exceeds its bound.'
        }
        foreach ($match in [regex]::Matches((Get-Content -Raw -LiteralPath $libraries),
                '"path"\s+"(?<path>[^"]+)"')) {
            [void]$roots.Add([IO.Path]::GetFullPath($match.Groups['path'].Value.Replace('\\', '\')))
        }
    }
    $canonicalRoots =
        [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($root in @($roots)) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        # Get-Item expands DOS/8.3 aliases (unlike Path.GetFullPath), so a
        # differently-spelled alias cannot evade the Steam-library comparison.
        $canonical = [IO.Path]::GetFullPath(
            (Get-Item -LiteralPath $root -Force -ErrorAction Stop).FullName
        ).TrimEnd('\', '/')
        [void]$canonicalRoots.Add($canonical)
    }
    return @($canonicalRoots)
}

function Assert-ApprovedLocalDriveRoot {
    param([string]$Path, [string]$Label)
    if ($Path -cnotmatch '^(?<drive>[A-Za-z]):\\') {
        throw "$Label must use a local drive-letter path; UNC and volume aliases are rejected."
    }
    $driveName = $Matches.drive.ToUpperInvariant()
    $drive = [IO.DriveInfo]::new($driveName + ':\')
    if (-not $drive.IsReady -or $drive.DriveType -ne [IO.DriveType]::Fixed) {
        throw "$Label must reside on a ready fixed local drive."
    }
    $subst = Join-Path $env:SystemRoot 'System32\subst.exe'
    if (-not (Test-Path -LiteralPath $subst -PathType Leaf)) {
        throw 'Cannot prove research-root isolation because subst.exe is absent.'
    }
    $mappings = @(& $subst 2>$null | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) {
        throw 'Cannot prove research-root isolation because subst.exe failed.'
    }
    $prefix = '^\s*' + [regex]::Escape($driveName + ':\:') + '\s*=>'
    if (@($mappings | Where-Object { $_ -match $prefix }).Count -ne 0) {
        throw "$Label must not use a substituted drive alias."
    }
}

function Resolve-IsolatedResearchRoot {
    $requestedRoot = [IO.Path]::GetFullPath($ResearchHalfLifeRoot).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $requestedRoot -PathType Container)) {
        throw 'ResearchHalfLifeRoot must be an existing directory.'
    }
    Assert-ApprovedLocalDriveRoot $requestedRoot 'research root'
    # DirectoryInfo.FullName expands every existing DOS/8.3 path component.
    # Reparse ancestors, UNC/network roots, volume aliases, and substituted
    # drives are rejected separately so alternate spellings cannot bypass the
    # Steam-library comparison.
    $root = [IO.Path]::GetFullPath(
        (Get-Item -LiteralPath $requestedRoot -Force -ErrorAction Stop).FullName
    ).TrimEnd('\', '/')
    Assert-NoReparsePointInExistingPath $root 'research root'
    $canonicalRepositoryRoot = [IO.Path]::GetFullPath(
        (Get-Item -LiteralPath $repositoryRoot -Force -ErrorAction Stop).FullName
    ).TrimEnd('\', '/')
    if ((Test-PathAtOrBelow $root $canonicalRepositoryRoot) -or
        (Test-PathAtOrBelow $canonicalRepositoryRoot $root) -or
        $root -match '(?i)(?:^|[\\/])steamapps(?:[\\/]|$)') {
        throw 'Research root must be disjoint from repository and Steam libraries.'
    }
    foreach ($steamRoot in @(Get-KnownSteamRoots)) {
        if ((Test-PathAtOrBelow $root $steamRoot) -or (Test-PathAtOrBelow $steamRoot $root)) {
            throw 'Research root overlaps a configured Steam library.'
        }
    }
    [void](Get-BoundedItems $root)
    $marker = [IO.Path]::GetFullPath((Join-Path $root $markerName))
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw 'Research root lacks the exact isolation marker.'
    }
    Assert-NoReparsePointInExistingPath $marker 'research marker'
    Assert-OnlyDefaultDataStream $marker 'research marker'
    Assert-NoHardLink $marker 'research marker'
    $markerValue = Read-BoundedAsciiMarker $marker
    if ($markerValue -cne $markerText -and
        $markerValue -cne ($markerText + "`n") -and
        $markerValue -cne ($markerText + "`r`n")) {
        throw 'Research root lacks the exact isolation marker.'
    }
    $client = [IO.Path]::GetFullPath($ClientPath)
    $server = [IO.Path]::GetFullPath($HldsPath)
    if ($client -ine (Join-Path $root 'hl.exe') -or $server -ine (Join-Path $root 'hlds.exe')) {
        throw 'ClientPath and HldsPath must be the canonical root launchers.'
    }
    foreach ($pair in @(@($client, '1.1.1.1', 'stock client'), @($server, '4.1.1.1', 'stock server'))) {
        Assert-NoReparsePointInExistingPath $pair[0] $pair[2]
        Assert-OnlyDefaultDataStream $pair[0] $pair[2]
        Assert-NoHardLink $pair[0] $pair[2]
        $item = Get-Item -LiteralPath $pair[0] -Force
        $version = '{0}.{1}.{2}.{3}' -f $item.VersionInfo.FileMajorPart,
            $item.VersionInfo.FileMinorPart, $item.VersionInfo.FileBuildPart,
            $item.VersionInfo.FilePrivatePart
        if ($version -cne $pair[1]) { throw "$($pair[2]) version is not accepted." }
        $signature = Get-AuthenticodeSignature -LiteralPath $pair[0]
        if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
            $signature.SignerCertificate.Subject -cnotmatch '^CN=Valve Corp\.(?:,|$)') {
            throw "$($pair[2]) is not validly Valve-signed."
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $root 'valve') -PathType Container)) {
        throw 'Research root lacks the valve directory.'
    }
    return [pscustomobject]@{ Root = $root; Client = $client; Server = $server }
}

function New-OwnedProcessRecord {
    param([Diagnostics.Process]$Process, [string]$ExpectedPath, [string]$Role)
    $Process.Refresh()
    return [pscustomobject]@{
        Id = $Process.Id; StartTime = $Process.StartTime.ToUniversalTime()
        ExpectedPath = [IO.Path]::GetFullPath($ExpectedPath); Role = $Role
    }
}

function Test-OwnedProcess {
    param([object]$Record)
    $process = Get-Process -Id $Record.Id -ErrorAction SilentlyContinue
    if ($null -eq $process) { return $false }
    try {
        return ([IO.Path]::GetFullPath($process.Path) -ieq $Record.ExpectedPath) -and
            [Math]::Abs(($process.StartTime.ToUniversalTime() - $Record.StartTime).TotalMilliseconds) -le 2
    } catch { return $false }
}

function Stop-OwnedProcess {
    param([object]$Record)
    if ($null -eq $Record) { return }
    $process = Get-Process -Id $Record.Id -ErrorAction SilentlyContinue
    if ($null -eq $process) { return }
    if (-not (Test-OwnedProcess $Record)) {
        throw "Refusing to terminate unverified $($Record.Role) PID."
    }
    $process.Kill()
    if (-not $process.WaitForExit(5000)) { throw "$($Record.Role) did not stop." }
}

function Wait-ForUdpOwner {
    param([int]$Port, [object]$Record)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if (-not (Test-OwnedProcess $Record)) { throw "$($Record.Role) exited during readiness." }
        $row = Get-NetUDPEndpoint -LocalAddress '127.0.0.1' -LocalPort $Port -ErrorAction SilentlyContinue |
            Where-Object OwningProcess -eq $Record.Id
        if ($null -ne $row) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "$($Record.Role) did not own its expected loopback port."
}

function Write-RestorationAttestation {
    param([string]$RunRoot, [object]$Before, [object]$After, [int]$ExitCode)
    $value = [ordered]@{
        schema = 'hlclient.stock-runtime-restoration.v1'
        external_file_drift = 'none'
        snapshot_entry_count = $Before.EntryCount
        pre_manifest_sha256 = $Before.ManifestSha256
        post_manifest_sha256 = $After.ManifestSha256
        created_files_removed = $true
        owned_processes_stopped = $true
        input_automation_used = $false
        input_events_injected = 0
        capture_process_exit_code = $ExitCode
    }
    $path = Join-Path $RunRoot 'research-restoration-attestation.json'
    if (Test-Path -LiteralPath $path) { throw 'Restoration attestation already exists.' }
    $json = $value | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText($path, $json + "`r`n", [Text.UTF8Encoding]::new($false))
}

function Get-RestorationSelfTestExternalObservation {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer) {
        return 'directory|{0}|{1}|{2}' -f $item.CreationTimeUtc.Ticks,
            $item.LastWriteTimeUtc.Ticks, [Int64]$item.Attributes
    }
    return 'file|{0}|{1}|{2}|{3}|{4}' -f $item.Length,
        $item.CreationTimeUtc.Ticks, $item.LastWriteTimeUtc.Ticks,
        [Int64]$item.Attributes, (Get-FileSha256 $Path)
}

if ($PSCmdlet.ParameterSetName -eq 'RestorationSelfTest') {
    $systemTemporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $selfTestRoot = [IO.Path]::GetFullPath((Join-Path $systemTemporaryRoot (
        'hlclient-stock-runtime-selftest-' + [Guid]::NewGuid().ToString('N'))))
    $guard = $null
    try {
        Assert-PathBelowRoot $selfTestRoot $systemTemporaryRoot 'restoration self-test root'
        Assert-NoReparsePointInExistingPath $selfTestRoot 'restoration self-test path'
        [IO.Directory]::CreateDirectory($selfTestRoot) | Out-Null
        Assert-NoReparsePointInExistingPath $selfTestRoot 'restoration self-test path'

        $testResearch = Join-Path $selfTestRoot 'research'
        [IO.Directory]::CreateDirectory($testResearch) | Out-Null
        $original = Join-Path $testResearch 'original.bin'
        $external = Join-Path $selfTestRoot 'external-sentinel.bin'
        $originalDirectory = Join-Path $testResearch 'original-directory'
        $externalDirectory = Join-Path $selfTestRoot 'external-directory'
        [IO.Directory]::CreateDirectory($originalDirectory) | Out-Null
        [IO.Directory]::CreateDirectory($externalDirectory) | Out-Null
        $originalNested = Join-Path $originalDirectory 'nested.bin'
        $externalNested = Join-Path $externalDirectory 'external-nested.bin'
        [IO.File]::WriteAllBytes($original, [byte[]](0x10, 0x20, 0x30, 0x40))
        [IO.File]::WriteAllBytes($external, [byte[]](0xA1, 0xB2, 0xC3, 0xD4))
        [IO.File]::WriteAllBytes($originalNested, [byte[]](0x41, 0x42, 0x43))
        [IO.File]::WriteAllBytes($externalNested, [byte[]](0xE1, 0xE2, 0xE3))
        $externalBefore = Get-RestorationSelfTestExternalObservation $external
        $externalDirectoryBefore =
            Get-RestorationSelfTestExternalObservation $externalDirectory
        $externalNestedBefore =
            Get-RestorationSelfTestExternalObservation $externalNested

        $before = Get-ResearchSnapshot $testResearch
        $guard = New-RestorationGuard $testResearch $before

        [IO.File]::Delete($original)
        [void](New-Item -ItemType HardLink -Path $original -Target $external -ErrorAction Stop)
        [IO.Directory]::Delete($originalDirectory, $true)
        [void](New-Item -ItemType Junction -Path $originalDirectory `
            -Target $externalDirectory -ErrorAction Stop)
        [IO.File]::WriteAllBytes((Join-Path $testResearch 'created.bin'), [byte[]](0x55, 0x66))

        $after = Restore-ResearchState $guard
        if ((Get-RestorationSelfTestExternalObservation $external) -cne
            $externalBefore) {
            throw 'Restoration self-test changed an external hard-link target.'
        }
        if ((Get-RestorationSelfTestExternalObservation $externalDirectory) -cne
                $externalDirectoryBefore -or
            (Get-RestorationSelfTestExternalObservation $externalNested) -cne
                $externalNestedBefore) {
            throw 'Restoration self-test traversed an external junction target.'
        }
        if ($after.ManifestSha256 -cne $before.ManifestSha256 -or
            $after.EntryCount -ne $before.EntryCount -or
            $after.TotalBytes -ne $before.TotalBytes) {
            throw 'Restoration self-test did not recover the exact research snapshot.'
        }
        Write-Output '[stock-runtime-capture] hardlink-overwrite=blocked'
        Write-Output '[stock-runtime-capture] junction-traversal=blocked'
        Write-Output '[stock-runtime-capture] external-sentinel-metadata=unchanged'
        Write-Output '[stock-runtime-capture] junction-target-metadata=unchanged'
        Write-Output '[stock-runtime-capture] restoration=exact'
        Write-Output '[stock-runtime-capture] result=restoration-self-test-success'
    } finally {
        if ($null -ne $guard -and (Test-Path -LiteralPath $guard.TemporaryRoot)) {
            if ([IO.Path]::GetFileName($guard.TemporaryRoot) -notmatch
                '^hlclient-stock-runtime-restore-[0-9a-f]{32}$') {
                throw 'Restoration self-test backup identity is invalid.'
            }
            Assert-PathBelowRoot $guard.TemporaryRoot $systemTemporaryRoot `
                'restoration self-test backup cleanup'
            Assert-NoReparsePointInExistingPath $guard.TemporaryRoot `
                'restoration self-test backup cleanup'
            Remove-SafeTree $guard.TemporaryRoot $systemTemporaryRoot
        }
        if (Test-Path -LiteralPath $selfTestRoot) {
            if ([IO.Path]::GetFileName($selfTestRoot) -notmatch
                '^hlclient-stock-runtime-selftest-[0-9a-f]{32}$') {
                throw 'Restoration self-test root identity is invalid.'
            }
            Assert-PathBelowRoot $selfTestRoot $systemTemporaryRoot `
                'restoration self-test cleanup'
            Assert-NoReparsePointInExistingPath $selfTestRoot `
                'restoration self-test cleanup'
            Remove-SafeTree $selfTestRoot $systemTemporaryRoot
        }
    }
    return
}

if ($PSCmdlet.ParameterSetName -eq 'Capture') {
    Write-Output '[stock-runtime-capture] active-capture=evidence_pending'
    Write-Output '[stock-runtime-capture] os-outbound-isolation=not-implemented'
    Write-Output '[stock-runtime-capture] app-engine-protocol-build=not-observed'
    Write-Output '[stock-runtime-capture] owned-processes-started=0'
    Write-Output '[stock-runtime-capture] files-written=0'
    throw 'Active stock-runtime capture is evidence_pending until OS-level outbound isolation and exact app/engine/protocol/build observation are implemented; no run was started.'
}

$research = Resolve-IsolatedResearchRoot
if ($PSCmdlet.ParameterSetName -eq 'Preflight') {
    [void](Get-ResearchSnapshot $research.Root)
    Write-Output '[stock-runtime-capture] research-root=policy-screened-copy-physical-identity-pending'
    Write-Output '[stock-runtime-capture] client-version=1.1.1.1'
    Write-Output '[stock-runtime-capture] server-launcher-version=4.1.1.1'
    Write-Output '[stock-runtime-capture] stock-processes-started=0'
    Write-Output '[stock-runtime-capture] read-only-helper-processes-started=1'
    Write-Output '[stock-runtime-capture] files-written=0'
    Write-Output '[stock-runtime-capture] result=preflight-structural-success-isolation-evidence-pending'
    return
}

if ($RelayPort -eq $ServerPort -or $MaximumPayloadBytes -gt $MaximumTotalRawBytes -or
    $MaximumDecompressedBytes -lt $MaximumReassembledBytes -or
    $MaximumRuntimeFrames -gt $MaximumMessageCount -or
    $MaximumClientPackets -gt $MaximumDatagrams -or
    $MaximumServerPackets -gt $MaximumDatagrams) {
    throw 'Capture limits violate cross-field policy.'
}
$pendingOrchestrationScenarios = @(
    'second-client', 'reconnect', 'map-change', 'server-restart', 'respawn',
    'low-updaterate', 'high-updaterate', 'low-cmdrate', 'high-cmdrate')
if ($pendingOrchestrationScenarios -ccontains $Scenario) {
    throw 'The requested lifecycle/rate scenario orchestration is evidence_pending; no run was started.'
}
if (@(Get-Process -Name @('hl', 'hlds') -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'Active capture requires no pre-existing hl.exe or hlds.exe process.'
}
foreach ($port in @($RelayPort, $ServerPort)) {
    if (Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue) {
        throw "Selected UDP port $port is already occupied."
    }
}
$output = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
if ($output -ine $requiredOutputRoot) {
    throw 'OutputRoot must be the exact repository manual-artifacts/stock-runtime root.'
}
Assert-NoReparsePointInExistingPath $output 'stock runtime output root'
[IO.Directory]::CreateDirectory($output) | Out-Null
Assert-NoReparsePoint $output 'stock runtime output root'

$tool = [IO.Path]::GetFullPath($CaptureToolPath)
Assert-PathBelowRoot $tool $repositoryRoot 'capture tool'
Assert-NoReparsePointInExistingPath $tool 'capture tool'
if (-not (Test-Path -LiteralPath $tool -PathType Leaf) -or
    [IO.Path]::GetFileName($tool) -cne 'hlclient_stock_runtime_capture.exe') {
    throw 'CaptureToolPath must name the canonical repository-built capture executable.'
}
Assert-OnlyDefaultDataStream $tool 'capture tool'
Assert-NoHardLink $tool 'capture tool'
if ($ExpectedCaptureToolSha256 -and
    (Get-FileSha256 $tool) -cne $ExpectedCaptureToolSha256.ToUpperInvariant()) {
    throw 'Capture tool SHA-256 does not match the reviewed value.'
}

$before = Get-ResearchSnapshot $research.Root
$guard = New-RestorationGuard $research.Root $before
$runId = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $output $runId
$serverRecord = $null
$relayRecord = $null
$clientRecord = $null
$captureExitCode = 255
$primaryError = $null
$cleanupErrors = [Collections.Generic.List[string]]::new()
$after = $null
try {
    $server = Start-Process -FilePath $research.Server -ArgumentList @(
        '-console', '-game', $Game, '-noipx', '-insecure', '-ip', '127.0.0.1',
        '-port', [string]$ServerPort, '+sv_lan', '1', '+maxplayers', '2', '+map', $Map
    ) -WorkingDirectory $research.Root -WindowStyle Hidden -PassThru
    $serverRecord = New-OwnedProcessRecord $server $research.Server 'stock server'
    Wait-ForUdpOwner $ServerPort $serverRecord

    $arguments = @(
        '--listen-port', [string]$RelayPort, '--server-port', [string]$ServerPort,
        '--output-run-root', ('"' + $runRoot + '"'), '--scenario', $Scenario,
        '--max-duration-ms', [string]($MaximumDurationSeconds * 1000),
        '--max-datagrams', [string]$MaximumDatagrams,
        '--max-total-raw-bytes', [string]$MaximumTotalRawBytes,
        '--max-payload-bytes', [string]$MaximumPayloadBytes,
        '--max-reassembled-bytes', [string]$MaximumReassembledBytes,
        '--max-decompressed-bytes', [string]$MaximumDecompressedBytes,
        '--max-message-count', [string]$MaximumMessageCount,
        '--max-runtime-frames', [string]$MaximumRuntimeFrames,
        '--max-client-packets', [string]$MaximumClientPackets,
        '--max-server-packets', [string]$MaximumServerPackets,
        '--mutation-after-client-packets', [string]$MutationAfterClientPackets,
        '--mutation-after-server-packets', [string]$MutationAfterServerPackets,
        '--private-ipv4-loopback-only', '--one-upstream-socket',
        '--byte-preserving', '--no-payload-rewrite')
    $relay = Start-Process -FilePath $tool -ArgumentList $arguments `
        -WorkingDirectory $repositoryRoot -WindowStyle Hidden -PassThru
    $relayRecord = New-OwnedProcessRecord $relay $tool 'stock runtime relay'
    Wait-ForUdpOwner $RelayPort $relayRecord

    $client = Start-Process -FilePath $research.Client -ArgumentList @(
        '-game', $Game, '-console', '-novid', '-nojoy', '-windowed', '-w', '640',
        '-h', '480', '+connect', "127.0.0.1:$RelayPort"
    ) -WorkingDirectory $research.Root -PassThru
    $clientRecord = New-OwnedProcessRecord $client $research.Client 'stock client'

    if (-not $relay.WaitForExit(($MaximumDurationSeconds + 10) * 1000)) {
        throw 'Capture executable exceeded its hard duration plus grace period.'
    }
    $captureExitCode = $relay.ExitCode
    if ($captureExitCode -ne 0) { throw "Capture executable exited $captureExitCode." }
} catch {
    $primaryError = $_
} finally {
    foreach ($record in @($clientRecord, $relayRecord, $serverRecord)) {
        try { Stop-OwnedProcess $record }
        catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    }
    if (@(Get-Process -Name @('hl', 'hlds') -ErrorAction SilentlyContinue).Count -ne 0) {
        [void]$cleanupErrors.Add('A GoldSrc process remains after owned cleanup.')
    }
    if ($cleanupErrors.Count -eq 0) {
        try { $after = Restore-ResearchState $guard }
        catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    }
    if ($null -ne $after) {
        try {
            if ([IO.Path]::GetFileName($guard.TemporaryRoot) -notmatch
                '^hlclient-stock-runtime-restore-[0-9a-f]{32}$') {
                throw 'Restoration backup identity is invalid.'
            }
            $systemTemporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
            Assert-PathBelowRoot $guard.TemporaryRoot $systemTemporaryRoot 'restoration backup cleanup'
            Assert-NoReparsePointInExistingPath $guard.TemporaryRoot 'restoration backup cleanup'
            Remove-SafeTree $guard.TemporaryRoot $systemTemporaryRoot
        } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    } else {
        Write-Warning "Restoration backup retained for recovery at '$($guard.TemporaryRoot)'."
    }
}

if ($cleanupErrors.Count -ne 0) {
    $prefix = if ($null -ne $primaryError) { $primaryError.Exception.Message + '; ' } else { '' }
    throw ($prefix + ($cleanupErrors -join '; '))
}
if ($null -ne $primaryError) { throw $primaryError }
if ($null -eq $after -or -not (Test-Path -LiteralPath $runRoot -PathType Container)) {
    throw 'Capture lacks a successful restoration or run directory.'
}
Write-RestorationAttestation $runRoot $before $after $captureExitCode
Write-Output '[stock-runtime-capture] research-root=policy-screened-copy-physical-identity-pending'
Write-Output '[stock-runtime-capture] stock-app-build=not-observed'
Write-Output '[stock-runtime-capture] server-engine-protocol-build=not-observed'
Write-Output '[stock-runtime-capture] accepted-evidence-runs=0'
Write-Output '[stock-runtime-capture] restoration=attested-external-file-drift-none'
Write-Output '[stock-runtime-capture] raw-output=manual-artifacts/stock-runtime'
Write-Output '[stock-runtime-capture] result=transport-captured-evidence-pending'
