#requires -Version 5.1

<#
.SYNOPSIS
Runs or validates the fail-closed stock-usercmd clean-room capture boundary.

.DESCRIPTION
Without an explicit research root this script validates the current
evidence-pending state. It starts no process, injects no input, creates no raw
artifact directory, and does not create tracked stock evidence.

The ResearchHalfLifeRoot/Game/Map form is an operational, bounded capture
harness. It accepts only a marked, non-reparse, non-hard-linked research copy
outside every configured Steam library. It snapshots the complete inventory,
backs up the explicitly mutable stock state, launches only the validated Valve
client/server, and owns a private IPv4-loopback byte-preserving UDP relay. The
relay learns exactly one client endpoint, uses one connected upstream socket,
and permits only baseline forwarding or a named whole-datagram mutation. It
stores bounded structural metadata but never stores raw datagram bytes.

The harness performs no Windows input injection. The client process and its
top-level window are nevertheless validated before capture. Cleanup is always
attempted in finally: owned processes are terminated, relay sockets disposed,
new research-root entries removed, protected state restored, directory
metadata restored, and a complete SHA-256/size/timestamp/inventory snapshot
compared. A capture is reported as accepted only after that comparison proves
external-file-drift=none. Capture acceptance does not promote stock wire
evidence; exact app/engine/protocol identity, move classification, envelope,
and checksum remain pending until independently reviewed projections exist.

.PARAMETER ValidateEvidencePending
Validate the zero-active-stock-run state. This is the default parameter set.

.PARAMETER ResearchHalfLifeRoot
Explicit isolated Half-Life root containing the exact isolation marker.

.PARAMETER Game
The bounded stock game token. Only valve is accepted.

.PARAMETER Map
The bounded stock map launched by the guided scenario.

.PARAMETER Scenario
Baseline byte-preserving forwarding, or one bounded whole-datagram mutation.
Mutation selection is based only on connectionless/sequenced framing metadata;
it does not inspect or rewrite a message payload.

.PARAMETER ServerPort
Exact private-loopback UDP port assigned to the owned stock server.

.PARAMETER RelayPort
Distinct private-loopback UDP port assigned to the client-facing relay socket.

.PARAMETER TimeoutSeconds
Hard relay-capture lifetime. Process/window readiness has its own bound.

.PARAMETER MaximumPackets
Maximum number of ingress datagrams observed in both directions.

.PARAMETER MaximumBytes
Maximum aggregate ingress datagram bytes observed in both directions.

.PARAMETER MaximumDatagramBytes
Per-datagram byte bound. UDP payloads larger than this fail the run.

.PARAMETER MinimumClientSequencedPackets
Minimum candidate-sequenced client datagrams required before completion. These
are not counted as verified move packets until independent review.

.PARAMETER MutationAfterClientSequencedPacket
One-based candidate-sequenced client packet at which a client mutation begins.

.PARAMETER MutationAfterServerSequencedPacket
One-based candidate-sequenced server packet selected by the server-drop case.

.PARAMETER MinimumCaptureSeconds
Minimum relay duration before a complete capture may stop.

.PARAMETER ProcessReadyTimeoutSeconds
Bound for owned server-port and client-window readiness checks.

.EXAMPLE
.\scripts\verify_stock_usercmd.ps1 -ValidateEvidencePending

.EXAMPLE
.\scripts\verify_stock_usercmd.ps1 `
  -ResearchHalfLifeRoot "D:\isolated\Half-Life" `
  -Game valve -Map boot_camp -Scenario Baseline
#>

[CmdletBinding(DefaultParameterSetName = 'Pending')]
param(
    [Parameter(ParameterSetName = 'Pending')]
    [switch]$ValidateEvidencePending,

    [Parameter(Mandatory = $true, ParameterSetName = 'Guided')]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Guided')]
    [ValidateSet('valve')]
    [string]$Game,

    [Parameter(Mandatory = $true, ParameterSetName = 'Guided')]
    [ValidateSet('boot_camp', 'crossfire', 'stalkyard')]
    [string]$Map,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateSet(
        'Baseline',
        'DropOneClientSequenced',
        'DropTwoConsecutiveClientSequenced',
        'DropOneServerSequenced',
        'DuplicateOldClientSequenced',
        'ReorderTwoClientSequenced')]
    [string]$Scenario = 'Baseline',

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1024, 65534)]
    [int]$ServerPort = 27016,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1024, 65534)]
    [int]$RelayPort = 27017,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(5, 300)]
    [int]$TimeoutSeconds = 45,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1, 8192)]
    [int]$MaximumPackets = 8192,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1, 67108864)]
    [Int64]$MaximumBytes = 33554432,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1, 65507)]
    [int]$MaximumDatagramBytes = 65507,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1, 4096)]
    [int]$MinimumClientSequencedPackets = 100,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1, 4096)]
    [int]$MutationAfterClientSequencedPacket = 20,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1, 4096)]
    [int]$MutationAfterServerSequencedPacket = 20,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1, 60)]
    [int]$MinimumCaptureSeconds = 5,

    [Parameter(ParameterSetName = 'Guided')]
    [ValidateRange(1, 30)]
    [int]$ProcessReadyTimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptPath = [IO.Path]::GetFullPath($MyInvocation.MyCommand.Path)
$repositoryRoot = [IO.Path]::GetFullPath(
    (Join-Path (Split-Path -Parent $scriptPath) '..')).TrimEnd('\', '/')
$manualRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts')).TrimEnd('\', '/')
$captureRoot = [IO.Path]::GetFullPath(
    (Join-Path $manualRoot 'usercmd-captures')).TrimEnd('\', '/')
$projectionPath = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'docs/evidence/GOLDSRC_USERCMD_STOCK.json'))

$isolationMarkerName = '.hlclient-research-isolated'
$isolationMarkerText = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
$maximumResearchEntries = 200000
$maximumResearchBytes = [Int64]17179869184
$maximumSteamLibraryManifestBytes = 1048576
$maximumMetadataBytes = 8388608
$loopbackAddressText = '127.0.0.1'
$metadataSchema = 'hlclient-stock-usercmd-capture-v1'
$protectedRelativeRoots = @(
    'config.cfg',
    'userconfig.cfg',
    'autoexec.cfg',
    'custom.hpk',
    'qconsole.log',
    'hlds.log',
    'logs',
    'screenshots',
    'save',
    'config',
    'valve/config.cfg',
    'valve/userconfig.cfg',
    'valve/autoexec.cfg',
    'valve/custom.hpk',
    'valve/qconsole.log',
    'valve/hlds.log',
    'valve/logs',
    'valve/screenshots',
    'valve/save',
    'valve/config',
    'platform/config')

function Get-Sha256Hex {
    param([byte[]]$Bytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '')
    }
    finally { $sha.Dispose() }
}

function Get-FileSha256Hex {
    param([string]$Path)
    $stream = [IO.File]::Open(
        $Path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Get-TextSha256Hex {
    param([string]$Text)
    $encoding = New-Object Text.UTF8Encoding($false)
    return Get-Sha256Hex -Bytes $encoding.GetBytes($Text)
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

function Get-BoundedDescendantItems {
    param(
        [string]$Path,
        [string]$Label,
        [int]$MaximumEntries)
    if ($MaximumEntries -lt 0 -or
        $MaximumEntries -gt $maximumResearchEntries) {
        throw "$Label has an invalid enumeration bound."
    }
    $items = [Collections.Generic.List[object]]::new()
    $pending = [Collections.Generic.Queue[IO.DirectoryInfo]]::new()
    $rootItem = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not [bool](
            $rootItem.Attributes -band [IO.FileAttributes]::Directory)) {
        throw "$Label must be a directory."
    }
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label contains a reparse point."
    }
    $pending.Enqueue($rootItem)
    while ($pending.Count -ne 0) {
        $directory = $pending.Dequeue()
        Assert-NoReparsePoint -Path $directory.FullName -Label $Label
        foreach ($item in @($directory.GetFileSystemInfos())) {
            if ($items.Count -ge $MaximumEntries) {
                throw "$Label exceeds the descendant entry bound."
            }
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Label contains a reparse point."
            }
            [void]$items.Add($item)
            if ([bool]($item.Attributes -band [IO.FileAttributes]::Directory)) {
                $pending.Enqueue([IO.DirectoryInfo]$item)
            }
        }
    }
    return @($items)
}

function Assert-NoDescendantReparsePoint {
    param([string]$Path, [string]$Label)
    [void](Get-BoundedDescendantItems -Path $Path -Label $Label `
        -MaximumEntries $maximumResearchEntries)
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    try {
        $streams = @(Get-Item -LiteralPath $Path -Stream * -ErrorAction Stop)
    }
    catch {
        throw "$Label data streams could not be inspected."
    }
    if (@($streams | Where-Object { $_.Stream -cne ':$DATA' }).Count -ne 0) {
        throw "$Label must contain only the default data stream."
    }
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -and
        @($streams | Where-Object { $_.Stream -ceq ':$DATA' }).Count -ne 1) {
        throw "$Label default data stream could not be established exactly."
    }
}

function Assert-NoHardLink {
    param([string]$Path, [string]$Label)
    try {
        $item = Microsoft.PowerShell.Management\Get-Item `
            -LiteralPath $Path -Force -ErrorAction Stop
        if ($item.PSIsContainer) {
            throw "$Label must be a filesystem file."
        }
        $linkTypeProperty = $item.PSObject.Properties['LinkType']
        if ($null -eq $linkTypeProperty) {
            throw "$Label hard-link state could not be established."
        }
        $linkType = [string]$linkTypeProperty.Value
    }
    catch {
        throw "$Label hard-link state could not be inspected: $($_.Exception.Message)"
    }
    if ($linkType -ceq 'HardLink') {
        throw "$Label must not be hard-linked."
    }
    if (-not [string]::IsNullOrEmpty($linkType)) {
        throw "$Label has an unsupported link type."
    }
}

function Test-PathAtOrBelow {
    param([string]$Path, [string]$Root)
    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    return $fullPath.Equals(
        $fullRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith(
            $fullRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)
}

function Assert-PathBelowRoot {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not $fullPath.StartsWith(
            $fullRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be a canonical descendant of its exact root."
    }
}

function Resolve-CanonicalFile {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    Assert-PathBelowRoot -Path $fullPath -Root $Root -Label $Label
    Assert-NoReparsePointInExistingPath -Path $fullPath -Label $Label
    $resolved = Resolve-Path -LiteralPath $fullPath -ErrorAction Stop
    if ($resolved.Provider.Name -cne 'FileSystem' -or
        -not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
        throw "$Label must be a filesystem file."
    }
    $canonical = [IO.Path]::GetFullPath($resolved.Path)
    Assert-PathBelowRoot -Path $canonical -Root $Root -Label $Label
    Assert-NoReparsePoint -Path $canonical -Label $Label
    Assert-OnlyDefaultDataStream -Path $canonical -Label $Label
    Assert-NoHardLink -Path $canonical -Label $Label
    return $canonical
}

function Get-KnownSteamRoots {
    $roots = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($registryPath in @(
            'HKCU:\Software\Valve\Steam',
            'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
            'HKLM:\SOFTWARE\Valve\Steam')) {
        if (-not (Test-Path -LiteralPath $registryPath -PathType Container)) {
            continue
        }
        try { $value = Get-ItemProperty -LiteralPath $registryPath }
        catch { throw "Unable to inspect configured Steam root '$registryPath'." }
        foreach ($property in @('SteamPath', 'InstallPath')) {
            $entry = $value.PSObject.Properties[$property]
            if ($null -ne $entry -and
                -not [string]::IsNullOrWhiteSpace([string]$entry.Value)) {
                try {
                    [void]$roots.Add(
                        [IO.Path]::GetFullPath([string]$entry.Value))
                }
                catch { throw 'A configured Steam root is not a valid path.' }
            }
        }
    }
    foreach ($candidate in @(
            $(if (${env:ProgramFiles(x86)}) {
                Join-Path ${env:ProgramFiles(x86)} 'Steam'
            }),
            $(if ($env:ProgramFiles) {
                Join-Path $env:ProgramFiles 'Steam'
            }))) {
        if ($candidate -and
            (Test-Path -LiteralPath $candidate -PathType Container)) {
            [void]$roots.Add([IO.Path]::GetFullPath($candidate))
        }
    }
    foreach ($steamRoot in @($roots)) {
        $libraryFile = [IO.Path]::Combine(
            $steamRoot, 'steamapps', 'libraryfolders.vdf')
        if (-not (Test-Path -LiteralPath $libraryFile -PathType Leaf)) {
            continue
        }
        Assert-NoReparsePointInExistingPath -Path $libraryFile `
            -Label 'Steam library manifest'
        Assert-NoReparsePoint -Path $libraryFile `
            -Label 'Steam library manifest'
        Assert-OnlyDefaultDataStream -Path $libraryFile `
            -Label 'Steam library manifest'
        if ((Get-Item -LiteralPath $libraryFile).Length -gt
            $maximumSteamLibraryManifestBytes) {
            throw 'Steam library manifest exceeds its inspection bound.'
        }
        try {
            $text = Get-Content -Raw -LiteralPath $libraryFile
            foreach ($match in [regex]::Matches(
                    $text,
                    '"path"\s+"(?<path>[^"]+)"',
                    [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
                $libraryPath = $match.Groups['path'].Value.Replace('\\', '\')
                if (-not [string]::IsNullOrWhiteSpace($libraryPath)) {
                    [void]$roots.Add([IO.Path]::GetFullPath($libraryPath))
                }
            }
        }
        catch { throw 'Unable to establish the configured Steam-library set.' }
    }
    return @($roots)
}

function Assert-ValveSignedLauncherVersion {
    param(
        [string]$Path,
        [string]$ExpectedFileVersion,
        [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force
    $actualFileVersion = '{0}.{1}.{2}.{3}' -f
        $item.VersionInfo.FileMajorPart,
        $item.VersionInfo.FileMinorPart,
        $item.VersionInfo.FileBuildPart,
        $item.VersionInfo.FilePrivatePart
    if ($actualFileVersion -cne $ExpectedFileVersion) {
        throw "$Label does not match the accepted launcher file version."
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or
        $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -cnotmatch
            '^CN=Valve Corp\.(?:,|$)') {
        throw "$Label is not validly Valve-signed."
    }
}

function Assert-IsolatedResearchRoot {
    param([string]$Path)
    $inputPath = [IO.Path]::GetFullPath($Path)
    Assert-NoReparsePointInExistingPath -Path $inputPath `
        -Label 'ResearchHalfLifeRoot'
    $resolved = Resolve-Path -LiteralPath $inputPath -ErrorAction Stop
    if ($resolved.Provider.Name -cne 'FileSystem' -or
        -not (Test-Path -LiteralPath $resolved.Path -PathType Container)) {
        throw 'ResearchHalfLifeRoot must be a filesystem directory.'
    }
    $root = [IO.Path]::GetFullPath($resolved.Path).TrimEnd('\', '/')
    Assert-NoReparsePoint -Path $root -Label 'ResearchHalfLifeRoot'
    $volumeRoot = [IO.Path]::GetPathRoot($root).TrimEnd('\', '/')
    if ($root.Equals($volumeRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ResearchHalfLifeRoot must not be a filesystem volume root.'
    }
    if ((Test-PathAtOrBelow -Path $root -Root $repositoryRoot) -or
        (Test-PathAtOrBelow -Path $repositoryRoot -Root $root)) {
        throw 'ResearchHalfLifeRoot must be disjoint from the repository.'
    }
    if ($root -match '(?i)(?:^|[\\/])steamapps(?:[\\/]|$)') {
        throw 'Primary or managed Steam-library roots are never accepted.'
    }
    foreach ($steamRoot in @(Get-KnownSteamRoots)) {
        $normalizedSteam = [IO.Path]::GetFullPath($steamRoot).TrimEnd('\', '/')
        $primary = [IO.Path]::GetFullPath([IO.Path]::Combine(
            $normalizedSteam,
            'steamapps',
            'common',
            'Half-Life')).TrimEnd('\', '/')
        if ($root.Equals($primary, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The primary Half-Life installation is never accepted.'
        }
        if ((Test-PathAtOrBelow -Path $root -Root $normalizedSteam) -or
            (Test-PathAtOrBelow -Path $normalizedSteam -Root $root)) {
            throw 'ResearchHalfLifeRoot overlaps a configured Steam library.'
        }
    }
    Assert-NoDescendantReparsePoint -Path $root `
        -Label 'ResearchHalfLifeRoot'

    $marker = Resolve-CanonicalFile `
        -Path (Join-Path $root $isolationMarkerName) `
        -Root $root -Label 'isolation marker'
    if ((Get-Item -LiteralPath $marker).Length -gt 128 -or
        (Get-Content -Raw -LiteralPath $marker).Trim() -cne
            $isolationMarkerText) {
        throw 'Isolated research marker content is invalid.'
    }

    $client = Resolve-CanonicalFile -Path (Join-Path $root 'hl.exe') `
        -Root $root -Label 'client launcher'
    $server = Resolve-CanonicalFile -Path (Join-Path $root 'hlds.exe') `
        -Root $root -Label 'server launcher'
    $valveRoot = [IO.Path]::GetFullPath((Join-Path $root 'valve'))
    Assert-PathBelowRoot -Path $valveRoot -Root $root -Label 'valve directory'
    if (-not (Test-Path -LiteralPath $valveRoot -PathType Container)) {
        throw 'Isolated research copy lacks the valve directory.'
    }
    Assert-NoReparsePoint -Path $valveRoot -Label 'valve directory'
    Assert-ValveSignedLauncherVersion -Path $client `
        -ExpectedFileVersion '1.1.1.1' -Label 'client launcher'
    Assert-ValveSignedLauncherVersion -Path $server `
        -ExpectedFileVersion '4.1.1.1' -Label 'server launcher'
    return [pscustomobject]@{
        Root = $root
        Client = $client
        Server = $server
    }
}

function Get-LiveGoldSrcProcessCount {
    return @(Get-Process -Name @('hl', 'hlds') `
        -ErrorAction SilentlyContinue).Count
}

function Get-RelativeResearchPath {
    param([string]$Path, [string]$Root)
    $rootPrefix = $Root.TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    if (-not $Path.StartsWith(
            $rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Research snapshot entry escaped its root.'
    }
    return $Path.Substring($rootPrefix.Length).Replace('\', '/')
}

function Get-ResearchSnapshot {
    param([string]$Root)
    Assert-NoReparsePointInExistingPath -Path $Root `
        -Label 'research snapshot root'
    Assert-NoReparsePoint -Path $Root -Label 'research snapshot root'
    Assert-OnlyDefaultDataStream -Path $Root -Label 'research snapshot root'
    $snapshotItems = @(Get-BoundedDescendantItems -Path $Root `
        -Label 'research snapshot root' `
        -MaximumEntries ($maximumResearchEntries - 1))
    $entries = [Collections.Generic.List[object]]::new()
    $rootItem = Get-Item -LiteralPath $Root -Force
    [void]$entries.Add([pscustomobject]@{
        RelativePath = '.'
        Kind = 'directory'
        Length = [Int64]0
        CreationTimeUtcTicks = $rootItem.CreationTimeUtc.Ticks
        LastWriteTimeUtcTicks = $rootItem.LastWriteTimeUtc.Ticks
        Sha256 = ''
        Attributes = [Int64]$rootItem.Attributes
    })
    [Int64]$totalBytes = 0
    $seen = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    [void]$seen.Add('.')
    foreach ($item in @($snapshotItems | Sort-Object FullName)) {
        $entryPath = [IO.Path]::GetFullPath($item.FullName)
        Assert-PathBelowRoot -Path $entryPath -Root $Root `
            -Label 'research snapshot entry'
        Assert-NoReparsePointInExistingPath -Path $entryPath `
            -Label 'research snapshot entry'
        Assert-NoReparsePoint -Path $entryPath `
            -Label 'research snapshot entry'
        $relative = Get-RelativeResearchPath -Path $entryPath -Root $Root
        if (-not $seen.Add($relative)) {
            throw 'Research snapshot contains a path identity collision.'
        }
        $isDirectory = [bool](
            $item.Attributes -band [IO.FileAttributes]::Directory)
        if (-not $isDirectory) {
            Assert-NoHardLink -Path $entryPath `
                -Label 'research snapshot file'
        }
        Assert-OnlyDefaultDataStream -Path $entryPath `
            -Label $(if ($isDirectory) {
                'research snapshot directory'
            } else {
                'research snapshot file'
            })
        $item.Refresh()
        [Int64]$length = if ($isDirectory) { 0 } else { $item.Length }
        [Int64]$creationTimeUtcTicks = $item.CreationTimeUtc.Ticks
        [Int64]$lastWriteTimeUtcTicks = $item.LastWriteTimeUtc.Ticks
        [Int64]$attributes = $item.Attributes
        if ($length -lt 0 -or $totalBytes -gt
            ($maximumResearchBytes - $length)) {
            throw 'Research root exceeds the snapshot byte bound.'
        }
        $totalBytes += $length
        $sha256 = if ($isDirectory) { '' } else {
            Get-FileSha256Hex -Path $entryPath
        }
        Assert-NoReparsePointInExistingPath -Path $entryPath `
            -Label 'research snapshot entry after inspection'
        Assert-NoReparsePoint -Path $entryPath `
            -Label 'research snapshot entry after inspection'
        if (-not $isDirectory) {
            Assert-NoHardLink -Path $entryPath `
                -Label 'research snapshot file after inspection'
        }
        $finalItem = Get-Item -LiteralPath $entryPath -Force
        if ([bool]$finalItem.PSIsContainer -ne $isDirectory -or
            (-not $isDirectory -and $finalItem.Length -ne $length) -or
            $finalItem.CreationTimeUtc.Ticks -ne $creationTimeUtcTicks -or
            $finalItem.LastWriteTimeUtc.Ticks -ne $lastWriteTimeUtcTicks -or
            [Int64]$finalItem.Attributes -ne $attributes) {
            throw (
                "Research snapshot entry '$relative' changed while it was " +
                'inspected.')
        }
        [void]$entries.Add([pscustomobject]@{
            RelativePath = $relative
            Kind = $(if ($isDirectory) { 'directory' } else { 'file' })
            Length = $length
            CreationTimeUtcTicks = $creationTimeUtcTicks
            LastWriteTimeUtcTicks = $lastWriteTimeUtcTicks
            Sha256 = $sha256
            Attributes = $attributes
        })
    }
    $lines = @($entries | ForEach-Object {
        '{0}:{1}|{2}|{3}|{4}|{5}|{6}|{7}' -f
            $_.RelativePath.Length,
            $_.RelativePath,
            $_.Kind,
            $_.Length,
            $_.CreationTimeUtcTicks,
            $_.LastWriteTimeUtcTicks,
            $_.Sha256,
            $_.Attributes
    })
    return [pscustomobject]@{
        EntryCount = $entries.Count
        TotalBytes = $totalBytes
        ManifestSha256 = Get-TextSha256Hex -Text ($lines -join "`n")
        Entries = @($entries)
    }
}

function Assert-ResearchSnapshotMatch {
    param([object]$Before, [object]$After)
    if ($Before.EntryCount -ne $After.EntryCount -or
        $Before.TotalBytes -ne $After.TotalBytes -or
        $Before.ManifestSha256 -cne $After.ManifestSha256) {
        throw 'Read-only research preflight detected external-file drift.'
    }
}

function Test-ProtectedRelativePath {
    param([string]$RelativePath)
    $normalized = $RelativePath.Replace('\', '/').TrimStart('/')
    foreach ($protected in $protectedRelativeRoots) {
        if ($normalized.Equals(
                $protected, [StringComparison]::OrdinalIgnoreCase) -or
            $normalized.StartsWith(
                $protected + '/', [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Get-PathDepth {
    param([string]$RelativePath)
    if ($RelativePath -ceq '.') { return 0 }
    return @($RelativePath -split '/').Count
}

function Get-SafeRestorationItems {
    param([string]$Root, [int]$MaximumEntries)
    $items = [Collections.Generic.List[object]]::new()
    $pending = [Collections.Generic.Queue[IO.DirectoryInfo]]::new()
    $rootItem = Get-Item -LiteralPath $Root -Force -ErrorAction Stop
    $pending.Enqueue([IO.DirectoryInfo]$rootItem)
    while ($pending.Count -ne 0) {
        $directory = $pending.Dequeue()
        foreach ($item in @($directory.GetFileSystemInfos())) {
            if ($items.Count -ge $MaximumEntries) {
                throw 'Research restoration enumeration exceeds its bound.'
            }
            $fullPath = [IO.Path]::GetFullPath($item.FullName)
            Assert-PathBelowRoot -Path $fullPath -Root $Root `
                -Label 'research restoration entry'
            [void]$items.Add($item)
            $isDirectory = [bool](
                $item.Attributes -band [IO.FileAttributes]::Directory)
            $isReparse = [bool](
                $item.Attributes -band [IO.FileAttributes]::ReparsePoint)
            if ($isDirectory -and -not $isReparse) {
                $pending.Enqueue([IO.DirectoryInfo]$item)
            }
        }
    }
    return @($items)
}

function Remove-SafeRestorationEntry {
    param([string]$Path, [string]$Root)
    $fullPath = [IO.Path]::GetFullPath($Path)
    Assert-PathBelowRoot -Path $fullPath -Root $Root `
        -Label 'research restoration removal target'
    if (-not (Test-Path -LiteralPath $fullPath)) { return }
    $item = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
    $isDirectory = [bool](
        $item.Attributes -band [IO.FileAttributes]::Directory)
    $isReparse = [bool](
        $item.Attributes -band [IO.FileAttributes]::ReparsePoint)
    if ($isDirectory -and -not $isReparse -and
        @($item.GetFileSystemInfos()).Count -ne 0) {
        throw 'Research restoration attempted to remove a non-empty directory.'
    }
    Remove-Item -LiteralPath $fullPath -Force -ErrorAction Stop
}

function Remove-SafeRestorationTree {
    param([string]$Path, [string]$Root)
    $fullPath = [IO.Path]::GetFullPath($Path)
    Assert-PathBelowRoot -Path $fullPath -Root $Root `
        -Label 'protected restoration root'
    if (-not (Test-Path -LiteralPath $fullPath)) { return }
    $item = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or
        [bool]($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        Remove-SafeRestorationEntry -Path $fullPath -Root $Root
        return
    }
    $descendants = @(Get-SafeRestorationItems -Root $fullPath `
        -MaximumEntries $maximumResearchEntries)
    foreach ($descendant in @($descendants | Sort-Object `
            @{ Expression = {
                Get-PathDepth -RelativePath (
                    Get-RelativeResearchPath -Path $_.FullName -Root $Root)
            }; Descending = $true },
            @{ Expression = { $_.FullName }; Descending = $true })) {
        Remove-SafeRestorationEntry -Path $descendant.FullName -Root $Root
    }
    Remove-SafeRestorationEntry -Path $fullPath -Root $Root
}

function New-ResearchRestorationGuard {
    param([string]$Root, [object]$Snapshot)
    $temporaryRoot = [IO.Path]::GetFullPath((Join-Path `
        ([IO.Path]::GetTempPath()) `
        ('hlclient-usercmd-restore-' + [Guid]::NewGuid().ToString('N'))))
    if ((Test-PathAtOrBelow -Path $temporaryRoot -Root $Root) -or
        (Test-PathAtOrBelow -Path $Root -Root $temporaryRoot) -or
        (Test-PathAtOrBelow -Path $temporaryRoot -Root $repositoryRoot)) {
        throw 'Restoration backup root is not disjoint from protected roots.'
    }
    try {
        [IO.Directory]::CreateDirectory($temporaryRoot) | Out-Null
        Assert-NoReparsePointInExistingPath -Path $temporaryRoot `
            -Label 'restoration backup root'
        Assert-NoReparsePoint -Path $temporaryRoot `
            -Label 'restoration backup root'
        $backupDataRoot = Join-Path $temporaryRoot 'data'
        [IO.Directory]::CreateDirectory($backupDataRoot) | Out-Null

        $backedEntries = [Collections.Generic.List[object]]::new()
        foreach ($entry in @($Snapshot.Entries | Where-Object {
                    $_.RelativePath -cne '.' -and
                    (Test-ProtectedRelativePath -RelativePath $_.RelativePath)
                } | Sort-Object `
                    @{ Expression = {
                        Get-PathDepth -RelativePath $_.RelativePath
                    } }, RelativePath)) {
            $source = [IO.Path]::GetFullPath((Join-Path `
                $Root $entry.RelativePath.Replace('/', '\')))
            Assert-PathBelowRoot -Path $source -Root $Root `
                -Label 'restoration backup source'
            $destination = [IO.Path]::GetFullPath((Join-Path `
                $backupDataRoot $entry.RelativePath.Replace('/', '\')))
            Assert-PathBelowRoot -Path $destination -Root $backupDataRoot `
                -Label 'restoration backup destination'
            if ($entry.Kind -ceq 'directory') {
                [IO.Directory]::CreateDirectory($destination) | Out-Null
            }
            else {
                $parent = Split-Path -Parent $destination
                [IO.Directory]::CreateDirectory($parent) | Out-Null
                [IO.File]::Copy($source, $destination, $false)
                if ((Get-FileSha256Hex -Path $destination) -cne
                    $entry.Sha256) {
                    throw 'Restoration backup digest validation failed.'
                }
            }
            [void]$backedEntries.Add($entry)
        }
        return [pscustomobject]@{
            Root = $Root
            TemporaryRoot = $temporaryRoot
            BackupDataRoot = $backupDataRoot
            Before = $Snapshot
            BackedEntries = @($backedEntries)
        }
    }
    catch {
        if ((Test-Path -LiteralPath $temporaryRoot) -and
            [IO.Path]::GetFileName($temporaryRoot) -cmatch
                '^hlclient-usercmd-restore-[0-9a-f]{32}$') {
            Remove-Item -LiteralPath $temporaryRoot -Recurse -Force `
                -ErrorAction SilentlyContinue
        }
        throw
    }
}

function Restore-ResearchState {
    param([object]$Guard)
    $root = $Guard.Root
    $initialPaths = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($Guard.Before.Entries)) {
        [void]$initialPaths.Add([string]$entry.RelativePath)
    }

    # Protected mutable state is replaced from the byte-for-byte backup. The
    # roots are exact, non-overlapping descendants chosen above.
    foreach ($relativeRoot in $protectedRelativeRoots) {
        $target = [IO.Path]::GetFullPath((Join-Path `
            $root $relativeRoot.Replace('/', '\')))
        Assert-PathBelowRoot -Path $target -Root $root `
            -Label 'protected restoration target'
        if (Test-Path -LiteralPath $target) {
            Remove-SafeRestorationTree -Path $target -Root $root
        }
    }

    # Remove every entry that did not exist before launch. Reparse points are
    # never traversed and each removal is an already-resolved exact descendant.
    $currentItems = @(Get-SafeRestorationItems -Root $root `
        -MaximumEntries $maximumResearchEntries)
    foreach ($item in @($currentItems | Sort-Object `
            @{ Expression = {
                Get-PathDepth -RelativePath (
                    Get-RelativeResearchPath -Path $_.FullName -Root $root)
            }; Descending = $true },
            @{ Expression = { $_.FullName }; Descending = $true })) {
        $relative = Get-RelativeResearchPath -Path `
            ([IO.Path]::GetFullPath($item.FullName)) -Root $root
        if (-not $initialPaths.Contains($relative)) {
            Remove-SafeRestorationEntry -Path $item.FullName -Root $root
        }
    }

    foreach ($entry in @($Guard.BackedEntries | Where-Object {
                $_.Kind -ceq 'directory'
            } | Sort-Object `
                @{ Expression = {
                    Get-PathDepth -RelativePath $_.RelativePath
                } }, RelativePath)) {
        $target = [IO.Path]::GetFullPath((Join-Path `
            $root $entry.RelativePath.Replace('/', '\')))
        [IO.Directory]::CreateDirectory($target) | Out-Null
    }
    foreach ($entry in @($Guard.BackedEntries | Where-Object {
                $_.Kind -ceq 'file'
            } | Sort-Object RelativePath)) {
        $source = [IO.Path]::GetFullPath((Join-Path `
            $Guard.BackupDataRoot $entry.RelativePath.Replace('/', '\')))
        $target = [IO.Path]::GetFullPath((Join-Path `
            $root $entry.RelativePath.Replace('/', '\')))
        Assert-PathBelowRoot -Path $target -Root $root `
            -Label 'protected restoration file'
        [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) |
            Out-Null
        [IO.File]::Copy($source, $target, $false)
        $item = Get-Item -LiteralPath $target -Force
        $item.CreationTimeUtc = [DateTime]::new(
            [Int64]$entry.CreationTimeUtcTicks, [DateTimeKind]::Utc)
        $item.LastWriteTimeUtc = [DateTime]::new(
            [Int64]$entry.LastWriteTimeUtcTicks, [DateTimeKind]::Utc)
        $item.Attributes = [IO.FileAttributes]([Int64]$entry.Attributes)
        if ($item.Length -ne [Int64]$entry.Length -or
            (Get-FileSha256Hex -Path $target) -cne $entry.Sha256) {
            throw 'Restored protected file failed size/SHA-256 validation.'
        }
    }

    # Restoring file children changes directory timestamps. Apply every
    # original directory timestamp and attribute after all content operations.
    foreach ($entry in @($Guard.Before.Entries | Where-Object {
                $_.Kind -ceq 'directory'
            } | Sort-Object `
                @{ Expression = {
                    Get-PathDepth -RelativePath $_.RelativePath
                }; Descending = $true }, RelativePath)) {
        $target = if ($entry.RelativePath -ceq '.') {
            $root
        }
        else {
            [IO.Path]::GetFullPath((Join-Path `
                $root $entry.RelativePath.Replace('/', '\')))
        }
        if (-not (Test-Path -LiteralPath $target -PathType Container)) {
            throw 'An original research directory could not be restored.'
        }
        $item = Get-Item -LiteralPath $target -Force
        $item.CreationTimeUtc = [DateTime]::new(
            [Int64]$entry.CreationTimeUtcTicks, [DateTimeKind]::Utc)
        $item.LastWriteTimeUtc = [DateTime]::new(
            [Int64]$entry.LastWriteTimeUtcTicks, [DateTimeKind]::Utc)
        $item.Attributes = [IO.FileAttributes]([Int64]$entry.Attributes)
    }

    $after = Get-ResearchSnapshot -Root $root
    Assert-ResearchSnapshotMatch -Before $Guard.Before -After $after
    return $after
}

function Remove-RestorationBackup {
    param([object]$Guard)
    if ($null -eq $Guard -or
        -not (Test-Path -LiteralPath $Guard.TemporaryRoot)) {
        return
    }
    $temporaryRoot = [IO.Path]::GetFullPath($Guard.TemporaryRoot)
    $systemTemporaryRoot = [IO.Path]::GetFullPath(
        [IO.Path]::GetTempPath()).TrimEnd('\', '/')
    Assert-PathBelowRoot -Path $temporaryRoot -Root $systemTemporaryRoot `
        -Label 'restoration backup cleanup target'
    if ([IO.Path]::GetFileName($temporaryRoot) -cnotmatch
        '^hlclient-usercmd-restore-[0-9a-f]{32}$') {
        throw 'Restoration backup cleanup target has an invalid identity.'
    }
    Assert-NoReparsePointInExistingPath -Path $temporaryRoot `
        -Label 'restoration backup cleanup target'
    Assert-NoDescendantReparsePoint -Path $temporaryRoot `
        -Label 'restoration backup cleanup target'
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force `
        -ErrorAction Stop
}

function Add-ResearchWindowApi {
    if ('HlClient.UserCmdResearchWindowApi' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Net;
using System.Runtime.InteropServices;

namespace HlClient {
    public sealed class UserCmdResearchUdpOwnerRow {
        public string LocalAddress { get; set; }
        public int LocalPort { get; set; }
        public int OwningProcessId { get; set; }
    }

    public static class UserCmdResearchWindowApi {
        private const int AddressFamilyInterNetwork = 2;
        private const uint ErrorInsufficientBuffer = 122;
        private const int UdpTableOwnerPid = 1;
        private const int MaximumUdpTableBytes = 32 * 1024 * 1024;
        private const int MaximumUdpRows = 1000000;
        private const int UdpOwnerPidRowBytes = 12;

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool IsWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        public static extern uint GetWindowThreadProcessId(
            IntPtr hWnd,
            out uint processId);

        [DllImport("iphlpapi.dll", SetLastError = true)]
        private static extern uint GetExtendedUdpTable(
            IntPtr table,
            ref int tableBytes,
            [MarshalAs(UnmanagedType.Bool)] bool order,
            int addressFamily,
            int tableClass,
            uint reserved);

        public static UserCmdResearchUdpOwnerRow[] GetUdpOwnerRows(int port) {
            if (port < 1 || port > 65535) {
                throw new ArgumentOutOfRangeException("port");
            }
            int bytes = 0;
            uint status = GetExtendedUdpTable(
                IntPtr.Zero,
                ref bytes,
                false,
                AddressFamilyInterNetwork,
                UdpTableOwnerPid,
                0);
            if (status != ErrorInsufficientBuffer ||
                bytes < sizeof(uint) ||
                bytes > MaximumUdpTableBytes) {
                throw new InvalidOperationException(
                    "IPv4 UDP ownership table size query failed: " + status);
            }
            IntPtr buffer = Marshal.AllocHGlobal(bytes);
            try {
                status = GetExtendedUdpTable(
                    buffer,
                    ref bytes,
                    false,
                    AddressFamilyInterNetwork,
                    UdpTableOwnerPid,
                    0);
                if (status != 0) {
                    throw new InvalidOperationException(
                        "IPv4 UDP ownership table query failed: " + status);
                }
                int count = Marshal.ReadInt32(buffer, 0);
                if (count < 0 || count > MaximumUdpRows ||
                    ((long)count * UdpOwnerPidRowBytes) >
                        (bytes - sizeof(uint))) {
                    throw new InvalidOperationException(
                        "IPv4 UDP ownership table has invalid bounds.");
                }
                var matches = new List<UserCmdResearchUdpOwnerRow>();
                for (int index = 0; index < count; ++index) {
                    int offset = sizeof(uint) + index * UdpOwnerPidRowBytes;
                    uint addressWord = unchecked(
                        (uint)Marshal.ReadInt32(buffer, offset));
                    uint portWord = unchecked(
                        (uint)Marshal.ReadInt32(buffer, offset + 4));
                    int localPort =
                        ((int)(portWord & 0xff) << 8) |
                        (int)((portWord >> 8) & 0xff);
                    if (localPort != port) {
                        continue;
                    }
                    int owner = Marshal.ReadInt32(buffer, offset + 8);
                    string address = new IPAddress(
                        BitConverter.GetBytes(addressWord)).ToString();
                    matches.Add(new UserCmdResearchUdpOwnerRow {
                        LocalAddress = address,
                        LocalPort = localPort,
                        OwningProcessId = owner
                    });
                }
                return matches.ToArray();
            }
            finally {
                Marshal.FreeHGlobal(buffer);
            }
        }
    }
}
'@
}

function Get-ProcessExecutablePath {
    param([Diagnostics.Process]$Process)
    try {
        $Process.Refresh()
        return [IO.Path]::GetFullPath($Process.MainModule.FileName)
    }
    catch {
        throw "Owned process executable identity could not be read: $($_.Exception.Message)"
    }
}

function Get-BoundedUdpOwnerRows {
    param([int]$Port)
    Add-ResearchWindowApi
    try {
        return @(
            [HlClient.UserCmdResearchWindowApi]::GetUdpOwnerRows($Port))
    }
    catch {
        throw "Exact IPv4 UDP ownership query failed: $($_.Exception.Message)"
    }
}

function New-OwnedProcessRecord {
    param(
        [Diagnostics.Process]$Process,
        [string]$ExpectedExecutable,
        [string]$Role)
    if ($null -eq $Process -or $Process.HasExited) {
        throw "$Role process exited before its identity was recorded."
    }
    $expected = [IO.Path]::GetFullPath($ExpectedExecutable)
    $actual = Get-ProcessExecutablePath -Process $Process
    if (-not $actual.Equals(
            $expected, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Role process executable does not match the validated launcher."
    }
    return [pscustomobject]@{
        Process = $Process
        Id = [int]$Process.Id
        StartTimeUtcTicks = [Int64]$Process.StartTime.ToUniversalTime().Ticks
        ExpectedExecutable = $expected
        Role = $Role
    }
}

function Test-OwnedProcessIdentity {
    param([object]$Record)
    if ($null -eq $Record -or $null -eq $Record.Process) { return $false }
    try {
        $Record.Process.Refresh()
        if ($Record.Process.HasExited -or
            [int]$Record.Process.Id -ne [int]$Record.Id -or
            [Int64]$Record.Process.StartTime.ToUniversalTime().Ticks -ne
                [Int64]$Record.StartTimeUtcTicks) {
            return $false
        }
        $actual = Get-ProcessExecutablePath -Process $Record.Process
        return $actual.Equals(
            [string]$Record.ExpectedExecutable,
            [StringComparison]::OrdinalIgnoreCase)
    }
    catch { return $false }
}

function Assert-OwnedProcessIdentity {
    param([object]$Record)
    if (-not (Test-OwnedProcessIdentity -Record $Record)) {
        throw "$($Record.Role) process identity changed or exited."
    }
}

function Stop-VerifiedOwnedProcess {
    param([object]$Record)
    if ($null -eq $Record -or $null -eq $Record.Process) { return }
    $process = $Record.Process
    try { $process.Refresh() }
    catch { return }
    if ($process.HasExited) { return }
    if (-not (Test-OwnedProcessIdentity -Record $Record)) {
        throw "$($Record.Role) process cannot be terminated after identity drift."
    }
    try { [void]$process.CloseMainWindow() }
    catch { }
    if (-not $process.WaitForExit(1500)) {
        $process.Kill()
        if (-not $process.WaitForExit(5000)) {
            throw "$($Record.Role) process did not terminate within its cleanup bound."
        }
    }
}

function Assert-OwnedClientWindow {
    param([object]$Record, [int]$TimeoutSeconds)
    Add-ResearchWindowApi
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Assert-OwnedProcessIdentity -Record $Record
        $Record.Process.Refresh()
        $window = $Record.Process.MainWindowHandle
        if ($window -ne [IntPtr]::Zero -and
            [HlClient.UserCmdResearchWindowApi]::IsWindow($window)) {
            [uint32]$owner = 0
            [void][HlClient.UserCmdResearchWindowApi]::GetWindowThreadProcessId(
                $window, [ref]$owner)
            if ([int]$owner -ne [int]$Record.Id) {
                throw 'Stock client window is not owned by the validated client process.'
            }
            return [pscustomobject]@{
                Handle = $window
                OwnerProcessId = [int]$owner
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Validated stock client did not create an owned top-level window in time.'
}

function Assert-LoopbackUdpPortOwnedByProcess {
    param(
        [int]$Port,
        [object]$Record,
        [int]$TimeoutSeconds,
        [string]$Role)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        Assert-OwnedProcessIdentity -Record $Record
        $matches = @(Get-BoundedUdpOwnerRows -Port $Port | Where-Object {
                [int]$_.OwningProcessId -eq [int]$Record.Id -and
                (@('127.0.0.1', '0.0.0.0') -ccontains
                    [string]$_.LocalAddress)
            })
        if ($matches.Count -eq 1) { return $matches[0] }
        if ($matches.Count -gt 1) {
            throw "$Role owns an ambiguous set of UDP endpoints on the selected port."
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "$Role did not bind its exact bounded UDP port in time."
}

function Assert-LearnedClientEndpointOwnedByProcess {
    param([Net.IPEndPoint]$Endpoint, [object]$Record)
    if ($Endpoint.Address.ToString() -cne $loopbackAddressText) {
        throw 'Relay learned a non-loopback client endpoint.'
    }
    $matches = @(Get-BoundedUdpOwnerRows -Port $Endpoint.Port |
        Where-Object {
            [int]$_.OwningProcessId -eq [int]$Record.Id -and
            (@('127.0.0.1', '0.0.0.0') -ccontains
                [string]$_.LocalAddress)
        })
    if ($matches.Count -ne 1) {
        throw 'Learned relay endpoint is not owned uniquely by the validated client.'
    }
}

function Get-UnsignedDatagramWord {
    param([byte[]]$Bytes, [int]$Offset)
    if ($Offset -lt 0 -or $Bytes.Length -lt ($Offset + 4)) {
        throw 'Datagram word read exceeds its exact boundary.'
    }
    if (-not [BitConverter]::IsLittleEndian) {
        throw 'This bounded stock harness requires a little-endian Windows host.'
    }
    return [UInt64][BitConverter]::ToUInt32($Bytes, $Offset)
}

function Get-DatagramFramingMetadata {
    param([byte[]]$Bytes)
    if ($Bytes.Length -lt 4) {
        return [ordered]@{
            classification = 'short'
            header_bytes_available = $Bytes.Length
        }
    }
    [UInt64]$word0 = Get-UnsignedDatagramWord -Bytes $Bytes -Offset 0
    if ($word0 -eq [UInt64]4294967295) {
        return [ordered]@{
            classification = 'connectionless'
            header_bytes_available = [Math]::Min(4, $Bytes.Length)
            marker_unsigned = $word0
        }
    }
    if ($Bytes.Length -lt 8) {
        return [ordered]@{
            classification = 'candidate-sequenced-truncated'
            header_bytes_available = $Bytes.Length
            word0_unsigned = $word0
        }
    }
    [UInt64]$word1 = Get-UnsignedDatagramWord -Bytes $Bytes -Offset 4
    return [ordered]@{
        classification = 'candidate-sequenced'
        header_bytes_available = 8
        word0_unsigned = $word0
        word1_unsigned = $word1
        word0_low30 = ($word0 -band [UInt64]0x3FFFFFFF)
        word0_high2 = (($word0 -shr 30) -band [UInt64]3)
        word1_low30 = ($word1 -band [UInt64]0x3FFFFFFF)
        word1_high2 = (($word1 -shr 30) -band [UInt64]3)
    }
}

function New-RelayObservation {
    param(
        [int]$Order,
        [string]$Direction,
        [byte[]]$Bytes,
        [Net.IPEndPoint]$Source,
        [string]$SourceRole,
        [Net.IPEndPoint]$Destination,
        [string]$DestinationRole,
        [Diagnostics.Stopwatch]$Clock)
    return [pscustomobject][ordered]@{
        event_order = $Order
        elapsed_microseconds = [Int64](
            $Clock.ElapsedTicks * 1000000L / [Diagnostics.Stopwatch]::Frequency)
        direction = $Direction
        action = 'pending'
        pre_netchan = [ordered]@{
            source_role = $SourceRole
            source_ipv4 = $Source.Address.ToString()
            source_port = $Source.Port
            destination_role = $DestinationRole
            destination_ipv4 = $Destination.Address.ToString()
            destination_port = $Destination.Port
            datagram_bytes = $Bytes.Length
            sha256 = Get-Sha256Hex -Bytes $Bytes
            framing = Get-DatagramFramingMetadata -Bytes $Bytes
        }
        post_netchan = [Collections.Generic.List[object]]::new()
    }
}

function Add-RelayEmissionMetadata {
    param(
        [object]$Observation,
        [byte[]]$Bytes,
        [Net.IPEndPoint]$Source,
        [string]$SourceRole,
        [Net.IPEndPoint]$Destination,
        [string]$DestinationRole,
        [Diagnostics.Stopwatch]$Clock,
        [string]$Disposition)
    $postSha256 = Get-Sha256Hex -Bytes $Bytes
    if ($postSha256 -cne [string]$Observation.pre_netchan.sha256 -or
        $Bytes.Length -ne
            [int]$Observation.pre_netchan.datagram_bytes) {
        throw 'Relay emission differs from its exact ingress datagram.'
    }
    [void]$Observation.post_netchan.Add([ordered]@{
        elapsed_microseconds = [Int64](
            $Clock.ElapsedTicks * 1000000L / [Diagnostics.Stopwatch]::Frequency)
        disposition = $Disposition
        source_role = $SourceRole
        source_ipv4 = $Source.Address.ToString()
        source_port = $Source.Port
        destination_role = $DestinationRole
        destination_ipv4 = $Destination.Address.ToString()
        destination_port = $Destination.Port
        datagram_bytes = $Bytes.Length
        sha256 = $postSha256
        byte_preserved = $true
        framing = Get-DatagramFramingMetadata -Bytes $Bytes
    })
}

function Test-CandidateSequencedObservation {
    param([object]$Observation)
    return $Observation.pre_netchan.framing.classification -ceq
        'candidate-sequenced'
}

function Invoke-BoundedUserCmdRelay {
    param(
        [Net.Sockets.UdpClient]$ClientFacingSocket,
        [Net.Sockets.UdpClient]$UpstreamSocket,
        [Net.IPEndPoint]$RelayEndpoint,
        [Net.IPEndPoint]$ServerEndpoint,
        [object]$ClientRecord,
        [object]$ServerRecord,
        [string]$Scenario,
        [int]$TimeoutSeconds,
        [int]$MinimumCaptureSeconds,
        [int]$MaximumPackets,
        [Int64]$MaximumBytes,
        [int]$MaximumDatagramBytes,
        [int]$MinimumClientSequencedPackets,
        [int]$MutationAfterClientSequencedPacket,
        [int]$MutationAfterServerSequencedPacket)
    $events = [Collections.Generic.List[object]]::new()
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $clientEndpoint = $null
    $relayState = [pscustomobject]@{
        ObservedPackets = [int]0
        ObservedBytes = [Int64]0
        EmittedPackets = [int]0
        EmittedBytes = [Int64]0
    }
    [int]$clientSequenced = 0
    [int]$serverSequenced = 0
    [int]$mutationCount = 0
    $reorderObservation = $null
    [byte[]]$reorderBytes = $null
    $deadline = [TimeSpan]::FromSeconds($TimeoutSeconds)

    function Assert-RelayIngressBound {
        param([byte[]]$Bytes)
        if ($Bytes.Length -gt $MaximumDatagramBytes) {
            throw 'Relay received a datagram above the exact per-packet bound.'
        }
        if ($relayState.ObservedPackets -ge $MaximumPackets -or
            $relayState.ObservedBytes -gt ($MaximumBytes - $Bytes.Length)) {
            throw 'Relay ingress exceeded its packet or byte bound.'
        }
    }

    function Add-IngressAccounting {
        param([byte[]]$Bytes)
        $relayState.ObservedPackets++
        $relayState.ObservedBytes += $Bytes.Length
    }

    function Send-UpstreamDatagram {
        param([byte[]]$Bytes, [object]$Observation, [string]$Disposition)
        if ($relayState.EmittedPackets -ge ($MaximumPackets * 2) -or
            $relayState.EmittedBytes -gt
                (($MaximumBytes * 2) - $Bytes.Length)) {
            throw 'Relay egress exceeded its derived packet or byte bound.'
        }
        $sent = $UpstreamSocket.Send($Bytes, $Bytes.Length)
        if ($sent -ne $Bytes.Length) {
            throw 'Upstream UDP send was not byte-complete.'
        }
        $relayState.EmittedPackets++
        $relayState.EmittedBytes += $Bytes.Length
        Add-RelayEmissionMetadata -Observation $Observation -Bytes $Bytes `
            -Source ([Net.IPEndPoint]$UpstreamSocket.Client.LocalEndPoint) `
            -SourceRole 'relay-upstream-socket' `
            -Destination $ServerEndpoint -DestinationRole 'exact-stock-server' `
            -Clock $clock -Disposition $Disposition
    }

    function Send-ClientDatagram {
        param([byte[]]$Bytes, [object]$Observation, [string]$Disposition)
        if ($null -eq $clientEndpoint) {
            throw 'Relay cannot forward server traffic before learning the client.'
        }
        if ($relayState.EmittedPackets -ge ($MaximumPackets * 2) -or
            $relayState.EmittedBytes -gt
                (($MaximumBytes * 2) - $Bytes.Length)) {
            throw 'Relay egress exceeded its derived packet or byte bound.'
        }
        $sent = $ClientFacingSocket.Send(
            $Bytes, $Bytes.Length, $clientEndpoint)
        if ($sent -ne $Bytes.Length) {
            throw 'Client-facing UDP send was not byte-complete.'
        }
        $relayState.EmittedPackets++
        $relayState.EmittedBytes += $Bytes.Length
        Add-RelayEmissionMetadata -Observation $Observation -Bytes $Bytes `
            -Source $RelayEndpoint -SourceRole 'relay-client-facing-socket' `
            -Destination $clientEndpoint -DestinationRole 'learned-stock-client' `
            -Clock $clock -Disposition $Disposition
    }

    do {
        Assert-OwnedProcessIdentity -Record $ClientRecord
        Assert-OwnedProcessIdentity -Record $ServerRecord
        $didWork = $false

        if ($ClientFacingSocket.Client.Poll(
                1000, [Net.Sockets.SelectMode]::SelectRead)) {
            $remote = [Net.IPEndPoint]::new([Net.IPAddress]::Any, 0)
            [byte[]]$bytes = $ClientFacingSocket.Receive([ref]$remote)
            Assert-RelayIngressBound -Bytes $bytes
            Add-IngressAccounting -Bytes $bytes
            $didWork = $true
            if ($remote.Address.ToString() -cne $loopbackAddressText) {
                throw 'Relay rejected non-loopback client traffic.'
            }
            if ($null -eq $clientEndpoint) {
                Assert-LearnedClientEndpointOwnedByProcess `
                    -Endpoint $remote -Record $ClientRecord
                $clientEndpoint = [Net.IPEndPoint]::new(
                    $remote.Address, $remote.Port)
            }
            elseif (-not $remote.Equals($clientEndpoint)) {
                throw 'Relay observed a second client endpoint.'
            }
            $observation = New-RelayObservation `
                -Order ($events.Count + 1) -Direction 'client-to-server' `
                -Bytes $bytes -Source $remote -SourceRole 'learned-stock-client' `
                -Destination $RelayEndpoint `
                -DestinationRole 'relay-client-facing-socket' -Clock $clock
            [void]$events.Add($observation)
            $candidateSequenced = Test-CandidateSequencedObservation `
                -Observation $observation
            if ($candidateSequenced) { $clientSequenced++ }

            $drop = $false
            $duplicate = $false
            $bufferForReorder = $false
            if ($candidateSequenced) {
                if ($Scenario -eq 'DropOneClientSequenced' -and
                    $clientSequenced -eq $MutationAfterClientSequencedPacket) {
                    $drop = $true
                    $mutationCount++
                }
                elseif ($Scenario -eq 'DropTwoConsecutiveClientSequenced' -and
                    $clientSequenced -ge $MutationAfterClientSequencedPacket -and
                    $clientSequenced -lt
                        ($MutationAfterClientSequencedPacket + 2)) {
                    $drop = $true
                    $mutationCount++
                }
                elseif ($Scenario -eq 'DuplicateOldClientSequenced' -and
                    $clientSequenced -eq $MutationAfterClientSequencedPacket) {
                    $duplicate = $true
                    $mutationCount++
                }
                elseif ($Scenario -eq 'ReorderTwoClientSequenced' -and
                    $clientSequenced -eq $MutationAfterClientSequencedPacket) {
                    $bufferForReorder = $true
                    $mutationCount++
                }
            }

            if ($drop) {
                $observation.action = 'drop-whole-datagram'
            }
            elseif ($bufferForReorder) {
                $observation.action = 'buffer-whole-datagram-for-reorder'
                $reorderObservation = $observation
                $reorderBytes = $bytes
            }
            else {
                $observation.action = $(if ($duplicate) {
                    'forward-and-duplicate-whole-datagram'
                } else { 'forward-byte-preserved' })
                Send-UpstreamDatagram -Bytes $bytes `
                    -Observation $observation -Disposition 'forward'
                if ($duplicate) {
                    Send-UpstreamDatagram -Bytes $bytes `
                        -Observation $observation -Disposition 'duplicate'
                }
                if ($Scenario -eq 'ReorderTwoClientSequenced' -and
                    $candidateSequenced -and $null -ne $reorderBytes -and
                    $clientSequenced -eq
                        ($MutationAfterClientSequencedPacket + 1)) {
                    Send-UpstreamDatagram -Bytes $reorderBytes `
                        -Observation $reorderObservation `
                        -Disposition 'reordered-after-next-datagram'
                    $reorderObservation.action =
                        'buffer-then-forward-after-next-whole-datagram'
                    $reorderBytes = $null
                    $reorderObservation = $null
                }
            }
        }

        if ($UpstreamSocket.Client.Poll(
                1000, [Net.Sockets.SelectMode]::SelectRead)) {
            $remoteServer = [Net.IPEndPoint]::new([Net.IPAddress]::Any, 0)
            [byte[]]$bytes = $UpstreamSocket.Receive([ref]$remoteServer)
            Assert-RelayIngressBound -Bytes $bytes
            Add-IngressAccounting -Bytes $bytes
            $didWork = $true
            if (-not $remoteServer.Equals($ServerEndpoint)) {
                throw 'Connected upstream socket observed a non-server endpoint.'
            }
            if ($null -eq $clientEndpoint) {
                throw 'Server traffic arrived before one client endpoint was learned.'
            }
            $observation = New-RelayObservation `
                -Order ($events.Count + 1) -Direction 'server-to-client' `
                -Bytes $bytes -Source $remoteServer `
                -SourceRole 'exact-stock-server' `
                -Destination ([Net.IPEndPoint]$UpstreamSocket.Client.LocalEndPoint) `
                -DestinationRole 'relay-upstream-socket' -Clock $clock
            [void]$events.Add($observation)
            $candidateSequenced = Test-CandidateSequencedObservation `
                -Observation $observation
            if ($candidateSequenced) { $serverSequenced++ }
            if ($Scenario -eq 'DropOneServerSequenced' -and
                $candidateSequenced -and
                $serverSequenced -eq $MutationAfterServerSequencedPacket) {
                $observation.action = 'drop-whole-datagram'
                $mutationCount++
            }
            else {
                $observation.action = 'forward-byte-preserved'
                Send-ClientDatagram -Bytes $bytes `
                    -Observation $observation -Disposition 'forward'
            }
        }

        $expectedMutationCount = switch ($Scenario) {
            'Baseline' { 0 }
            'DropTwoConsecutiveClientSequenced' { 2 }
            default { 1 }
        }
        $mutationComplete =
            $mutationCount -eq $expectedMutationCount -and
            $null -eq $reorderBytes
        if ($clientSequenced -ge $MinimumClientSequencedPackets -and
            $serverSequenced -gt 0 -and $mutationComplete -and
            $clock.Elapsed.TotalSeconds -ge $MinimumCaptureSeconds) {
            break
        }
        if (-not $didWork) { Start-Sleep -Milliseconds 1 }
    } while ($clock.Elapsed -lt $deadline)

    $clock.Stop()
    if ($null -ne $reorderBytes) {
        throw 'Relay timed out with a buffered reorder datagram.'
    }
    if ($clientSequenced -lt $MinimumClientSequencedPackets -or
        $serverSequenced -eq 0) {
        throw 'Capture did not reach its bounded sequenced-packet minimum.'
    }
    $expectedMutationCount = switch ($Scenario) {
        'Baseline' { 0 }
        'DropTwoConsecutiveClientSequenced' { 2 }
        default { 1 }
    }
    if ($mutationCount -ne $expectedMutationCount) {
        throw 'Capture did not complete its exact named mutation.'
    }
    return [pscustomobject]@{
        Events = @($events)
        DurationMilliseconds = [Int64]$clock.ElapsedMilliseconds
        ObservedPackets = $relayState.ObservedPackets
        ObservedBytes = $relayState.ObservedBytes
        EmittedPackets = $relayState.EmittedPackets
        EmittedBytes = $relayState.EmittedBytes
        ClientSequencedCandidates = $clientSequenced
        ServerSequencedCandidates = $serverSequenced
        MutationCount = $mutationCount
        LearnedClientEndpoint = $clientEndpoint
        UpstreamLocalEndpoint = [Net.IPEndPoint]$UpstreamSocket.Client.LocalEndPoint
    }
}

function Write-BoundedCaptureMetadata {
    param([string]$RunRoot, [object]$Metadata)
    Assert-PathBelowRoot -Path $RunRoot -Root $captureRoot `
        -Label 'capture run root'
    $metadataPath = [IO.Path]::GetFullPath((Join-Path $RunRoot 'metadata.json'))
    Assert-PathBelowRoot -Path $metadataPath -Root $RunRoot `
        -Label 'capture metadata path'
    $json = $Metadata | ConvertTo-Json -Depth 12
    $encoding = New-Object Text.UTF8Encoding($false)
    [byte[]]$encoded = $encoding.GetBytes($json)
    if ($encoded.Length -le 0 -or $encoded.Length -gt $maximumMetadataBytes) {
        throw 'Capture metadata is empty or exceeds its exact byte bound.'
    }
    $stream = [IO.File]::Open(
        $metadataPath,
        [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write,
        [IO.FileShare]::None)
    try {
        $stream.Write($encoded, 0, $encoded.Length)
        $stream.Flush()
    }
    finally { $stream.Dispose() }
    Assert-NoReparsePoint -Path $metadataPath -Label 'capture metadata'
    Assert-OnlyDefaultDataStream -Path $metadataPath -Label 'capture metadata'
    Assert-NoHardLink -Path $metadataPath -Label 'capture metadata'
    return [pscustomobject]@{
        Path = $metadataPath
        Bytes = $encoded.Length
        Sha256 = Get-FileSha256Hex -Path $metadataPath
    }
}

$gitIgnorePath = Join-Path $repositoryRoot '.gitignore'
if (-not (Test-Path -LiteralPath $gitIgnorePath -PathType Leaf) -or
    (Get-Content -Raw -LiteralPath $gitIgnorePath) -cnotmatch
        '(?m)^/manual-artifacts/\s*$') {
    throw 'Verifier requires the repository-wide /manual-artifacts/ ignore rule.'
}
Assert-PathBelowRoot -Path $manualRoot -Root $repositoryRoot `
    -Label 'manual artifact root'
Assert-PathBelowRoot -Path $captureRoot -Root $manualRoot `
    -Label 'usercmd capture root'
Assert-NoReparsePointInExistingPath -Path $captureRoot `
    -Label 'usercmd capture root'
if (Test-Path -LiteralPath $captureRoot) {
    if (-not (Test-Path -LiteralPath $captureRoot -PathType Container)) {
        throw 'Usercmd capture root must be a directory when it exists.'
    }
    Assert-NoReparsePoint -Path $captureRoot -Label 'usercmd capture root'
    Assert-NoDescendantReparsePoint -Path $captureRoot `
        -Label 'usercmd capture root'
}

if (Test-Path -LiteralPath $projectionPath) {
    throw (
        'Tracked stock usercmd evidence exists, but this evidence-pending ' +
        'harness has no accepted projection validator.')
}

if ($PSCmdlet.ParameterSetName -ceq 'Pending') {
    Write-Output (
        'stock-usercmd-evidence=pending accepted-active-stock-runs=0 ' +
        'verified-move-packets=0 stock-wire-profile=pending ' +
        'checksum-profile=pending tracked-evidence-created=false ' +
        'research-root=not-supplied isolation=not-validated ' +
        'capture-mode=not-requested operational-guided=true ' +
        'restoration-attestation=not-created ' +
        'processes-started=0 input-events-injected=0 ' +
        'raw-packet-bytes-output=false external-file-drift=none')
    return
}

if ($PSCmdlet.ParameterSetName -ceq 'Guided') {
    if ($env:OS -cne 'Windows_NT') {
        throw 'Operational stock capture is supported only on Windows.'
    }
    $scenarioNames = @{
        'baseline' = 'Baseline'
        'droponeclientsequenced' = 'DropOneClientSequenced'
        'droptwoconsecutiveclientsequenced' =
            'DropTwoConsecutiveClientSequenced'
        'droponeserversequenced' = 'DropOneServerSequenced'
        'duplicateoldclientsequenced' = 'DuplicateOldClientSequenced'
        'reordertwoclientsequenced' = 'ReorderTwoClientSequenced'
    }
    $Scenario = [string]$scenarioNames[$Scenario.ToLowerInvariant()]
    $Game = $Game.ToLowerInvariant()
    $Map = $Map.ToLowerInvariant()
    if ($ServerPort -eq $RelayPort) {
        throw 'ServerPort and RelayPort must be distinct.'
    }
    if ($MinimumCaptureSeconds -gt $TimeoutSeconds) {
        throw 'MinimumCaptureSeconds must not exceed TimeoutSeconds.'
    }
    if ($MaximumBytes -lt $MaximumDatagramBytes) {
        throw 'MaximumBytes must cover at least one maximum-size datagram.'
    }
    $requiredMutationClientPackets = switch ($Scenario) {
        'DropTwoConsecutiveClientSequenced' {
            $MutationAfterClientSequencedPacket + 1
        }
        'ReorderTwoClientSequenced' {
            $MutationAfterClientSequencedPacket + 1
        }
        'DropOneClientSequenced' { $MutationAfterClientSequencedPacket }
        'DuplicateOldClientSequenced' { $MutationAfterClientSequencedPacket }
        default { 0 }
    }
    if ($requiredMutationClientPackets -gt $MinimumClientSequencedPackets) {
        throw (
            'MinimumClientSequencedPackets must include the complete named ' +
            'client mutation window.')
    }
    if ($MinimumClientSequencedPackets -ge $MaximumPackets) {
        throw 'MaximumPackets cannot accommodate the required client sample.'
    }

    $research = Assert-IsolatedResearchRoot -Path $ResearchHalfLifeRoot
    if ((Get-LiveGoldSrcProcessCount) -ne 0) {
        throw 'Guided capture requires no pre-existing hl.exe or hlds.exe process.'
    }
    $before = Get-ResearchSnapshot -Root $research.Root
    $guard = $null
    $serverProcess = $null
    $clientProcess = $null
    $serverRecord = $null
    $clientRecord = $null
    $clientFacingSocket = $null
    $upstreamSocket = $null
    $capture = $null
    $primaryFailure = $null
    $cleanupFailures = [Collections.Generic.List[string]]::new()
    $restorationSucceeded = $false
    $after = $null
    $runRoot = $null
    $metadataFile = $null
    $processesStarted = 0
    $inputState = [pscustomobject]@{
        AutomationUsed = $false
        HeldKeyCount = 0
        HeldMouseButtonCount = 0
        CursorModified = $false
        CaptureModified = $false
    }
    $serverEndpoint = [Net.IPEndPoint]::new(
        [Net.IPAddress]::Parse($loopbackAddressText), $ServerPort)
    $relayEndpoint = [Net.IPEndPoint]::new(
        [Net.IPAddress]::Parse($loopbackAddressText), $RelayPort)
    $clientLauncherSha256 = Get-FileSha256Hex -Path $research.Client
    $serverLauncherSha256 = Get-FileSha256Hex -Path $research.Server
    $guard = New-ResearchRestorationGuard -Root $research.Root `
        -Snapshot $before

    try {
        $existingEndpoints = @(
            @(Get-BoundedUdpOwnerRows -Port $ServerPort) +
            @(Get-BoundedUdpOwnerRows -Port $RelayPort))
        if ($existingEndpoints.Count -ne 0) {
            throw 'A selected bounded UDP port is already in use.'
        }

        [IO.Directory]::CreateDirectory($manualRoot) | Out-Null
        [IO.Directory]::CreateDirectory($captureRoot) | Out-Null
        Assert-NoReparsePointInExistingPath -Path $captureRoot `
            -Label 'usercmd capture root after creation'
        Assert-NoReparsePoint -Path $captureRoot `
            -Label 'usercmd capture root after creation'
        $runName = '{0}-{1}-{2}' -f
            [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'),
            $Scenario.ToLowerInvariant(),
            [Guid]::NewGuid().ToString('N')
        $runRoot = [IO.Path]::GetFullPath((Join-Path $captureRoot $runName))
        Assert-PathBelowRoot -Path $runRoot -Root $captureRoot `
            -Label 'capture run root'
        if (Test-Path -LiteralPath $runRoot) {
            throw 'Unique capture run root already exists.'
        }
        [IO.Directory]::CreateDirectory($runRoot) | Out-Null
        Assert-NoReparsePoint -Path $runRoot -Label 'capture run root'

        $serverArguments = @(
            '-console',
            '-game', $Game,
            '-noipx',
            '-insecure',
            '-ip', $loopbackAddressText,
            '-port', $ServerPort.ToString(
                [Globalization.CultureInfo]::InvariantCulture),
            '+sv_lan', '1',
            '+maxplayers', '1',
            '+map', $Map)
        $serverProcess = Start-Process -FilePath $research.Server `
            -ArgumentList $serverArguments -WorkingDirectory $research.Root `
            -WindowStyle Hidden -PassThru
        $processesStarted++
        try {
            $serverRecord = New-OwnedProcessRecord -Process $serverProcess `
                -ExpectedExecutable $research.Server -Role 'stock server'
        }
        catch {
            if (-not $serverProcess.HasExited) {
                try {
                    $serverProcess.Kill()
                    [void]$serverProcess.WaitForExit(5000)
                }
                catch { }
            }
            throw
        }
        [void](Assert-LoopbackUdpPortOwnedByProcess -Port $ServerPort `
            -Record $serverRecord -TimeoutSeconds $ProcessReadyTimeoutSeconds `
            -Role 'stock server')

        $clientFacingSocket = [Net.Sockets.UdpClient]::new(
            [Net.Sockets.AddressFamily]::InterNetwork)
        $clientFacingSocket.ExclusiveAddressUse = $true
        $clientFacingSocket.Client.ReceiveBufferSize = 1048576
        $clientFacingSocket.Client.SendBufferSize = 1048576
        $clientFacingSocket.Client.Bind($relayEndpoint)

        $upstreamSocket = [Net.Sockets.UdpClient]::new(
            [Net.Sockets.AddressFamily]::InterNetwork)
        $upstreamSocket.ExclusiveAddressUse = $true
        $upstreamSocket.Client.ReceiveBufferSize = 1048576
        $upstreamSocket.Client.SendBufferSize = 1048576
        $upstreamSocket.Client.Bind([Net.IPEndPoint]::new(
            [Net.IPAddress]::Parse($loopbackAddressText), 0))
        $upstreamSocket.Connect($serverEndpoint)

        $clientArguments = @(
            '-game', $Game,
            '-console',
            '-novid',
            '-nojoy',
            '-windowed',
            '-w', '640',
            '-h', '480',
            '+connect',
            ('{0}:{1}' -f $loopbackAddressText, $RelayPort))
        $clientProcess = Start-Process -FilePath $research.Client `
            -ArgumentList $clientArguments -WorkingDirectory $research.Root `
            -WindowStyle Hidden -PassThru
        $processesStarted++
        try {
            $clientRecord = New-OwnedProcessRecord -Process $clientProcess `
                -ExpectedExecutable $research.Client -Role 'stock client'
        }
        catch {
            if (-not $clientProcess.HasExited) {
                try {
                    $clientProcess.Kill()
                    [void]$clientProcess.WaitForExit(5000)
                }
                catch { }
            }
            throw
        }
        [void](Assert-OwnedClientWindow -Record $clientRecord `
            -TimeoutSeconds $ProcessReadyTimeoutSeconds)

        $capture = Invoke-BoundedUserCmdRelay `
            -ClientFacingSocket $clientFacingSocket `
            -UpstreamSocket $upstreamSocket `
            -RelayEndpoint $relayEndpoint `
            -ServerEndpoint $serverEndpoint `
            -ClientRecord $clientRecord `
            -ServerRecord $serverRecord `
            -Scenario $Scenario `
            -TimeoutSeconds $TimeoutSeconds `
            -MinimumCaptureSeconds $MinimumCaptureSeconds `
            -MaximumPackets $MaximumPackets `
            -MaximumBytes $MaximumBytes `
            -MaximumDatagramBytes $MaximumDatagramBytes `
            -MinimumClientSequencedPackets $MinimumClientSequencedPackets `
            -MutationAfterClientSequencedPacket `
                $MutationAfterClientSequencedPacket `
            -MutationAfterServerSequencedPacket `
                $MutationAfterServerSequencedPacket
        Assert-OwnedProcessIdentity -Record $clientRecord
        Assert-OwnedProcessIdentity -Record $serverRecord
    }
    catch {
        $primaryFailure = $_
    }
    finally {
        if ($inputState.AutomationUsed -or
            $inputState.HeldKeyCount -ne 0 -or
            $inputState.HeldMouseButtonCount -ne 0 -or
            $inputState.CursorModified -or
            $inputState.CaptureModified) {
            [void]$cleanupFailures.Add(
                'unsupported research input state remained at cleanup')
        }
        foreach ($socket in @($clientFacingSocket, $upstreamSocket)) {
            if ($null -ne $socket) {
                try { $socket.Dispose() }
                catch {
                    [void]$cleanupFailures.Add(
                        "relay socket cleanup failed: $($_.Exception.Message)")
                }
            }
        }
        foreach ($record in @($clientRecord, $serverRecord)) {
            if ($null -ne $record) {
                try { Stop-VerifiedOwnedProcess -Record $record }
                catch {
                    [void]$cleanupFailures.Add(
                        "$($record.Role) cleanup failed: $($_.Exception.Message)")
                }
            }
        }
        $liveGoldSrcAfterCleanup = Get-LiveGoldSrcProcessCount
        if ($liveGoldSrcAfterCleanup -ne 0) {
            [void]$cleanupFailures.Add(
                'one or more GoldSrc processes remain after owned cleanup')
        }
        if ($liveGoldSrcAfterCleanup -eq 0) {
            try {
                $after = Restore-ResearchState -Guard $guard
                $restorationSucceeded = $true
            }
            catch {
                [void]$cleanupFailures.Add(
                    "research restoration failed: $($_.Exception.Message); " +
                    "backup retained at '$($guard.TemporaryRoot)'")
            }
        }
        if ($restorationSucceeded) {
            try { Remove-RestorationBackup -Guard $guard }
            catch {
                [void]$cleanupFailures.Add(
                    "restoration backup cleanup failed: $($_.Exception.Message)")
            }
        }
    }

    if ($cleanupFailures.Count -ne 0) {
        $prefix = if ($null -ne $primaryFailure) {
            "capture failed: $($primaryFailure.Exception.Message); "
        } else { '' }
        throw ($prefix + ($cleanupFailures -join '; '))
    }
    if ($null -ne $primaryFailure) {
        throw $primaryFailure
    }
    if ($null -eq $capture -or -not $restorationSucceeded) {
        throw 'Capture cannot be accepted before successful restoration.'
    }

    $metadata = [ordered]@{
        schema = $metadataSchema
        generated_utc = [DateTime]::UtcNow.ToString('o')
        methodology = [ordered]@{
            isolated_marked_copy = $true
            configured_steam_overlap = $false
            no_reparse_points = $true
            no_hard_links = $true
            private_ipv4_loopback = $true
            byte_preserving_relay = $true
            one_learned_client_endpoint = $true
            one_connected_upstream_socket = $true
            exact_upstream_endpoint = $true
            whole_datagram_mutations_only = $true
            mutation_selection = 'candidate-sequenced-order-only'
            payload_rewriting = $false
            raw_datagram_bytes_persisted = $false
            input_automation_used = $false
            input_events_injected = 0
            held_keys_after_cleanup = $inputState.HeldKeyCount
            held_mouse_buttons_after_cleanup =
                $inputState.HeldMouseButtonCount
            cursor_or_capture_modified = $false
            input_release_result = 'not-required-no-input-injector'
            client_process_identity_validated = $true
            client_window_ownership_validated = $true
            server_process_identity_validated = $true
        }
        stock_launchers = [ordered]@{
            client_file_version = '1.1.1.1'
            client_sha256 = $clientLauncherSha256
            server_file_version = '4.1.1.1'
            server_sha256 = $serverLauncherSha256
            valve_authenticode_signatures = 'valid'
            exact_steam_app_build = 'pending'
            exact_engine_identity = 'pending'
            exact_protocol_build = 'pending'
            runtime_ready_detection = 'pending'
        }
        run = [ordered]@{
            game = $Game
            map = $Map
            scenario = $Scenario
            server_endpoint = $serverEndpoint.ToString()
            relay_endpoint = $relayEndpoint.ToString()
            learned_client_endpoint =
                $capture.LearnedClientEndpoint.ToString()
            upstream_local_endpoint =
                $capture.UpstreamLocalEndpoint.ToString()
            timeout_seconds = $TimeoutSeconds
            minimum_capture_seconds = $MinimumCaptureSeconds
            maximum_packets = $MaximumPackets
            maximum_bytes = $MaximumBytes
            maximum_datagram_bytes = $MaximumDatagramBytes
            observed_packets = $capture.ObservedPackets
            observed_bytes = $capture.ObservedBytes
            emitted_packets = $capture.EmittedPackets
            emitted_bytes = $capture.EmittedBytes
            client_sequenced_candidates =
                $capture.ClientSequencedCandidates
            server_sequenced_candidates =
                $capture.ServerSequencedCandidates
            mutation_count = $capture.MutationCount
            duration_milliseconds = $capture.DurationMilliseconds
            processes_started = $processesStarted
        }
        restoration = [ordered]@{
            protected_roots = $protectedRelativeRoots
            pre_entry_count = $before.EntryCount
            pre_total_bytes = $before.TotalBytes
            pre_manifest_sha256 = $before.ManifestSha256
            post_entry_count = $after.EntryCount
            post_total_bytes = $after.TotalBytes
            post_manifest_sha256 = $after.ManifestSha256
            sha256_size_timestamps_inventory_verified = $true
            external_file_drift = 'none'
        }
        acceptance = [ordered]@{
            capture_transport_run_accepted_after_restoration = $true
            accepted_stock_evidence_run = $false
            accepted_active_stock_runs = 0
            verified_move_packets = 0
            stock_usercmd_evidence = 'pending-independent-review'
            stock_wire_profile = 'pending'
            checksum_profile = 'pending'
            tracked_evidence_created = $false
        }
        events = $capture.Events
    }
    $metadataFile = Write-BoundedCaptureMetadata -RunRoot $runRoot `
        -Metadata $metadata

    Write-Output (
        'research-root-preflight=valid path-isolation=valid ' +
        'configured-steam-overlap=false reparse-points=none ' +
        'hard-link-check=passed ' +
        'client-launcher-version=1.1.1.1 server-launcher-version=4.1.1.1 ' +
        'launcher-valve-signatures=valid process-window-validation=passed ' +
        'relay=private-ipv4-loopback byte-preserving=true ' +
        'one-client-endpoint=true one-upstream-socket=true ' +
        'filesystem-snapshot=restored-and-matched ' +
        'exact-stock-app-build=pending exact-engine-identity=pending ' +
        'exact-protocol-build=pending stock-binary-digests=pending ' +
        "game=$Game map=$Map scenario=$Scenario " +
        'capture-transport-run=accepted-after-restoration ' +
        "observed-packets=$($capture.ObservedPackets) " +
        "client-sequenced-candidates=$($capture.ClientSequencedCandidates) " +
        'stock-usercmd-evidence=pending-independent-review ' +
        'accepted-active-stock-runs=0 verified-move-packets=0 ' +
        'stock-wire-profile=pending checksum-profile=pending ' +
        'capture-mode=operational restoration-attestation=verified ' +
        "metadata=manual-artifacts/usercmd-captures/$runName/metadata.json " +
        "metadata-bytes=$($metadataFile.Bytes) " +
        "metadata-sha256=$($metadataFile.Sha256) " +
        'tracked-evidence-created=false processes-started=2 ' +
        'input-events-injected=0 raw-packet-bytes-output=false ' +
        'external-file-drift=none preflight-only=false')
    return
}

throw 'Unsupported verifier parameter set.'
