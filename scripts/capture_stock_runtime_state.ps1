#requires -Version 5.1

<#
.SYNOPSIS
Validates or runs the isolated stock-runtime capture transaction.

.DESCRIPTION
Active capture is disabled by default. It requires an explicit, case-sensitive
confirmation token and remains fail-closed unless the project-owned Windows
orchestrator reports a valid binary profile and a successful dynamic-WFP
isolation canary. PowerShell owns the research/external snapshots, exact
restoration and one-time final run-manifest publication; C++ owns every stock
process, socket and WFP lifecycle.

ValidateResearchRoot is read-only with respect to the repository and research
tree and starts no stock/game process. It invokes the read-only Windows
`subst.exe` listing to reject substituted drive aliases.
#>
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'DirectoryCapabilityBootstrap')]
    [switch]$InitializeDirectoryCapability,

    [Parameter(Mandatory = $true, ParameterSetName = 'RestorationSelfTest')]
    [switch]$ValidateRestorationGuard,

    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [switch]$ValidateResearchRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [switch]$ValidateActiveCaptureEnvironment,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [ValidateNotNullOrEmpty()]
    [string]$ClientPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [ValidateNotNullOrEmpty()]
    [string]$HldsPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureToolPath,

    [Parameter(ParameterSetName = 'Capture')]
    [switch]$EnableActiveCapture,

    [Parameter(ParameterSetName = 'Capture')]
    [AllowEmptyString()]
    [string]$ConfirmActiveCapture,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [ValidateNotNullOrEmpty()]
    [string]$NetworkIsolationGuardPath,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [ValidateNotNullOrEmpty()]
    [string]$AppManifestPath,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'ActivePreflight')]
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
        'drop-server-to-client-transport-ordinal',
        'duplicate-server-to-client-transport-ordinal',
        'reorder-server-to-client-transport-ordinal',
        'drop-server-runtime', 'drop-two-server-runtime',
        'duplicate-server-runtime', 'reorder-server-runtime',
        'drop-client-move', 'delay-client-move')]
    [string]$Scenario,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'ActivePreflight')]
    [ValidateRange(1024, 65534)]
    [int]$RelayPort = 27140,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'ActivePreflight')]
    [ValidateRange(1024, 65534)]
    [int]$ServerPort = 27141,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateNotNullOrEmpty()]
    [string]$OutputRoot = '.\manual-artifacts\stock-runtime',

    [Parameter(ParameterSetName = 'Capture')]
    [switch]$PreCampaignCanary,

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
$requiredCanaryOutputRoot = [IO.Path]::GetFullPath(
    (Join-Path $manualRoot 'stock-runtime-canary')).TrimEnd('\', '/')
$markerName = '.hlclient-research-isolated'
$markerText = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
$pendingMarkerName = '.hlclient-research-pending'
$preparationManifestName = '.hlclient-research-preparation.json'
$externalApprovalName = 'external-target-approval.json'
$activeCaptureToken = 'HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1'
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

# This is intentionally the first Capture-mode action. It does not resolve or
# inspect any caller-supplied path and occurs before output, backup, socket,
# process or WFP mutation. The confirmation value has no environment/config
# fallback and Ordinal comparison is case-sensitive.
if ($PSCmdlet.ParameterSetName -eq 'Capture' -and
    (-not $EnableActiveCapture -or $ConfirmActiveCapture -cne $activeCaptureToken)) {
    Write-Output '[stock-runtime-capture] active-capture=explicit-opt-in-required'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] files-written=0'
    Write-Output '[stock-runtime-capture] network-operations=0'
    Write-Output '[stock-runtime-capture] wfp-sessions-started=0'
    Write-Output '[stock-runtime-capture] capture-runs-created=0'
    Write-Output '[stock-runtime-capture] restoration-backups-created=0'
    throw 'Active stock-runtime capture requires the exact explicit confirmation token; no input path was resolved and no mutation occurred.'
}

function Test-PathAtOrBelow {
    param([string]$Path, [string]$Root)
    $pathValue = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $rootValue = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    return $pathValue.Equals($rootValue, [StringComparison]::OrdinalIgnoreCase) -or
        $pathValue.StartsWith(
            $rootValue + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)
}

function Get-PhysicalPathIdentity {
    param([string]$Path, [string]$Label)
    $canonical = [IO.Path]::GetFullPath(
        (Get-Item -LiteralPath $Path -Force -ErrorAction Stop).FullName
    ).TrimEnd('\', '/')
    if ($canonical -cnotmatch '^(?<drive>[A-Za-z]):\\(?<suffix>.*)$') {
        throw "$Label physical identity requires a drive-letter path."
    }
    $driveName = $Matches.drive.ToUpperInvariant()
    $relativeSuffix = $Matches.suffix
    if ($null -eq ('Hlclient.StockRuntimePathIdentity' -as [type])) {
        Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using System.Text;

namespace Hlclient
{
    public static class StockRuntimePathIdentity
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetVolumeNameForVolumeMountPoint(
            string volumeMountPoint,
            StringBuilder volumeName,
            int bufferLength);
    }
}
'@
    }
    $volumeName = [Text.StringBuilder]::new(64)
    if (-not [Hlclient.StockRuntimePathIdentity]::GetVolumeNameForVolumeMountPoint(
            ($driveName + ':\'), $volumeName, $volumeName.Capacity)) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "$Label physical volume identity is unavailable (Win32 $errorCode)."
    }
    return [pscustomobject]@{
        Path = $canonical
        Volume = $volumeName.ToString().ToUpperInvariant()
        Relative = ([string]$relativeSuffix).TrimEnd('\', '/')
    }
}

function Test-PhysicalPathAtOrBelow {
    param([object]$PathIdentity, [object]$RootIdentity)
    if ($PathIdentity.Volume -cne $RootIdentity.Volume) { return $false }
    $path = $PathIdentity.Relative.TrimEnd('\', '/')
    $root = $RootIdentity.Relative.TrimEnd('\', '/')
    return $path.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
        $path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)
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
    Initialize-RestorationDirectoryCapabilityNative
    try {
        [Hlclient.StockRuntimeDirectoryCapability]::ValidateOnlyDefaultDataStream(
            [IO.Path]::GetFullPath($Path))
    } catch {
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

function Assert-ExternalApprovalDigestAvailable {
    param([string]$ExpectedSha256)

    $reviewParent = [IO.Path]::GetFullPath((Join-Path `
            $manualRoot 'stock-runtime-source-review')).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $reviewParent -PathType Container)) {
        throw 'Reviewed research copy lacks its local approval artifact.'
    }
    Assert-NoReparsePointInExistingPath $reviewParent `
        'external-target approval root'
    Assert-OnlyDefaultDataStream $reviewParent 'external-target approval root'

    $reviewRoots = @(Get-ChildItem -LiteralPath $reviewParent -Force `
            -Directory -ErrorAction Stop)
    if ($reviewRoots.Count -gt 1024) {
        throw 'External-target approval root exceeds its review bound.'
    }
    $digestMatchCount = 0
    foreach ($reviewRoot in $reviewRoots) {
        if ($reviewRoot.Name -cnotmatch '^[0-9a-f]{32}$') { continue }
        if (($reviewRoot.Attributes -band
                [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'External-target approval review root must not be a reparse point.'
        }
        $candidate = Join-Path $reviewRoot.FullName $externalApprovalName
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $item = Get-Item -LiteralPath $candidate -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
            $item.Length -lt 1 -or $item.Length -gt 65536) {
            throw 'External-target approval artifact is not a bounded ordinary file.'
        }
        Assert-OnlyDefaultDataStream $candidate `
            'external-target approval artifact'
        Assert-NoHardLink $candidate 'external-target approval artifact'
        $observed = (Get-FileSha256 $candidate).ToLowerInvariant()
        if ($observed -ceq $ExpectedSha256) { ++$digestMatchCount }
    }
    if ($digestMatchCount -ne 1) {
        throw 'Reviewed research copy approval digest is not backed by one exact local artifact.'
    }
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
    # The root directory is not returned by Get-BoundedItems. Reject a named
    # stream here independently so a stream added after preparation (or after
    # the structural preflight) cannot be omitted from restoration evidence.
    Assert-OnlyDefaultDataStream -Path $Root -Label 'research snapshot root'
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

function Initialize-RestorationDirectoryCapabilityNative {
    if ($null -ne ('Hlclient.StockRuntimeDirectoryCapability' -as [type])) {
        return
    }
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace Hlclient
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct StockRuntimeByHandleFileInformation
    {
        public uint FileAttributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct StockRuntimeIoStatusBlock
    {
        public IntPtr Status;
        public UIntPtr Information;
    }

    public sealed class StockRuntimeDirectoryCapability : IDisposable
    {
        private const uint GenericRead = 0x80000000;
        private const uint GenericWrite = 0x40000000;
        private const uint DeleteAccess = 0x00010000;
        private const uint FileReadAttributes = 0x80;
        private const uint FileShareRead = 0x1;
        private const uint FileShareWrite = 0x2;
        private const uint CreateNew = 1;
        private const uint OpenExisting = 3;
        private const uint FileAttributeTemporary = 0x100;
        private const uint FileFlagBackupSemantics = 0x02000000;
        private const uint FileFlagOpenReparsePoint = 0x00200000;
        private const uint FileFlagWriteThrough = 0x80000000;
        private const uint FileAttributeDirectory = 0x10;
        private const uint FileAttributeReparsePoint = 0x400;
        private const uint FileNameNormalized = 0x0;
        private const uint VolumeNameDos = 0x0;
        private const int FileRenameInfo = 3;
        private const int FileDispositionInfo = 4;
        private const int FileStreamInfo = 7;
        private const int NtFileStreamInformation = 22;
        private const int StatusNoMoreFiles = unchecked((int)0x80000006);
        private const uint FileBegin = 0;
        private const uint MoveFileReplaceExisting = 0x1;
        private const int MaximumPublicationBytes = 4 * 1024 * 1024;
        private const int MaximumStreamInformationBytes = 64 * 1024;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);

        private readonly List<IntPtr> handles = new List<IntPtr>();
        private readonly string canonicalPath;
        private readonly uint volumeSerial;
        private readonly ulong fileId;
        private bool disposed;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateFile(
            string fileName, uint desiredAccess, uint shareMode,
            IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandle(
            IntPtr handle, out StockRuntimeByHandleFileInformation information);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandleEx(
            IntPtr handle, int informationClass, IntPtr information,
            uint bufferSize);

        [DllImport("ntdll.dll")]
        private static extern int NtQueryInformationFile(
            IntPtr handle, out StockRuntimeIoStatusBlock ioStatus,
            IntPtr information, uint informationBytes,
            int informationClass);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandle(
            IntPtr handle, StringBuilder path, uint pathLength, uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WriteFile(
            IntPtr handle, byte[] buffer, uint bytesToWrite,
            out uint bytesWritten, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ReadFile(
            IntPtr handle, byte[] buffer, uint bytesToRead,
            out uint bytesRead, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FlushFileBuffers(IntPtr handle);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetFilePointerEx(
            IntPtr handle, long distance, out long newPointer, uint moveMethod);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetFileInformationByHandle(
            IntPtr handle, int informationClass,
            IntPtr information, uint bufferSize);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DeleteFile(string fileName);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool MoveFileEx(
            string existingFileName, string newFileName, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFileAttributes(string fileName);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);

        private StockRuntimeDirectoryCapability(string path, string anchorPath)
        {
            canonicalPath = Path.GetFullPath(path).TrimEnd('\\', '/');
            try
            {
                if (String.IsNullOrEmpty(Path.GetPathRoot(canonicalPath)))
                    throw new InvalidOperationException("Directory capability requires an absolute path.");
                string canonicalAnchor = Path.GetFullPath(anchorPath).TrimEnd('\\', '/');
                if (!canonicalAnchor.StartsWith(
                        canonicalPath + Path.DirectorySeparatorChar,
                        StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "Directory capability anchor must be a strict descendant.");
                OpenAndHold(canonicalPath, true);
                // Holding a descendant without FILE_SHARE_DELETE blocks
                // rename/replacement of the root and its ancestors on the
                // supported Windows profile. The self-test exercises both.
                OpenAndHold(canonicalAnchor, false);
                StockRuntimeByHandleFileInformation information = Information(RootHandle);
                volumeSerial = information.VolumeSerialNumber;
                fileId = ((ulong)information.FileIndexHigh << 32) | information.FileIndexLow;
                if (!Revalidate())
                    throw new InvalidOperationException("Directory capability identity changed during acquisition.");
            }
            catch
            {
                Dispose();
                throw;
            }
        }

        public static StockRuntimeDirectoryCapability Open(
            string path, string anchorPath)
        {
            StockRuntimeDirectoryCapability capability = null;
            try
            {
                capability = new StockRuntimeDirectoryCapability(path, anchorPath);
                return capability;
            }
            catch
            {
                if (capability != null) capability.Dispose();
                throw;
            }
        }

        // Windows PowerShell 5.1 does not reliably enumerate directory ADS
        // through Get-Item -Stream. Query FILE_STREAM_INFO through one exact,
        // retained no-follow handle so files and directories share the same
        // fail-closed stream gate on every supported PowerShell host.
        public static void ValidateOnlyDefaultDataStream(string path)
        {
            if (String.IsNullOrWhiteSpace(path))
                throw new InvalidOperationException(
                    "Default-stream validation path is invalid.");
            string canonical = Path.GetFullPath(path).TrimEnd('\\', '/');
            IntPtr handle = CreateFile(
                canonical, GenericRead | FileReadAttributes, FileShareRead,
                IntPtr.Zero, OpenExisting,
                FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Default-stream validation open failed.");
            try
            {
                StockRuntimeByHandleFileInformation before =
                    Information(handle);
                ulong size = ((ulong)before.FileSizeHigh << 32) |
                    before.FileSizeLow;
                if ((before.FileAttributes & FileAttributeReparsePoint) != 0 ||
                    !String.Equals(FinalPath(handle), canonical,
                        StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "Default-stream validation identity is invalid.");
                if ((before.FileAttributes & FileAttributeDirectory) != 0)
                    RequireOnlyDirectoryDataStreams(handle);
                else
                    RequireOnlyDefaultDataStream(handle, size);
                StockRuntimeByHandleFileInformation after =
                    Information(handle);
                if (before.FileAttributes != after.FileAttributes ||
                    before.VolumeSerialNumber != after.VolumeSerialNumber ||
                    before.FileSizeHigh != after.FileSizeHigh ||
                    before.FileSizeLow != after.FileSizeLow ||
                    before.NumberOfLinks != after.NumberOfLinks ||
                    before.FileIndexHigh != after.FileIndexHigh ||
                    before.FileIndexLow != after.FileIndexLow ||
                    before.CreationTime.dwHighDateTime !=
                        after.CreationTime.dwHighDateTime ||
                    before.CreationTime.dwLowDateTime !=
                        after.CreationTime.dwLowDateTime ||
                    before.LastWriteTime.dwHighDateTime !=
                        after.LastWriteTime.dwHighDateTime ||
                    before.LastWriteTime.dwLowDateTime !=
                        after.LastWriteTime.dwLowDateTime ||
                    !String.Equals(FinalPath(handle), canonical,
                        StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "Default-stream validation identity changed.");
                if ((after.FileAttributes & FileAttributeDirectory) != 0)
                    RequireOnlyDirectoryDataStreams(handle);
                else
                    RequireOnlyDefaultDataStream(handle, size);
            }
            finally
            {
                CloseHandle(handle);
            }
        }

        // Removes only one exact, empty, lowercase-GUID child directory from
        // an otherwise empty retained parent. Both identities are opened with
        // FILE_FLAG_OPEN_REPARSE_POINT and without FILE_SHARE_DELETE before
        // the child is marked delete-on-close. Any content, alternate stream,
        // reparse identity, sibling or inaccessible inventory fails closed.
        public static void DeleteExactEmptyChildDirectory(
            string parentPath, string childLeaf)
        {
            if (String.IsNullOrWhiteSpace(parentPath) ||
                !ValidLowerHexRunId(childLeaf))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup parameters are invalid.");
            string parent = Path.GetFullPath(parentPath).TrimEnd('\\', '/');
            if (String.IsNullOrEmpty(Path.GetPathRoot(parent)))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup parent must be absolute.");
            DriveInfo drive = new DriveInfo(Path.GetPathRoot(parent));
            if (drive.DriveType != DriveType.Fixed)
                throw new InvalidOperationException(
                    "Exact empty-child cleanup requires a fixed local drive.");
            string child = Path.Combine(parent, childLeaf);
            if (!String.Equals(Path.GetDirectoryName(child), parent,
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup child is not direct.");

            IntPtr parentHandle = InvalidHandle;
            IntPtr childHandle = InvalidHandle;
            try
            {
                parentHandle = CreateFile(
                    parent, GenericRead | FileReadAttributes,
                    FileShareRead | FileShareWrite,
                    IntPtr.Zero, OpenExisting,
                    FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                    IntPtr.Zero);
                if (parentHandle == InvalidHandle)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Exact empty-child cleanup parent open failed.");
                StockRuntimeByHandleFileInformation parentBefore =
                    Information(parentHandle);
                RequireOrdinaryExactDirectory(
                    parentHandle, parent, parentBefore);
                RequireOnlyDirectoryDataStreams(parentHandle);
                RequireExactSingleChild(parent, child);

                childHandle = CreateFile(
                    child,
                    GenericRead | DeleteAccess | FileReadAttributes,
                    FileShareRead | FileShareWrite,
                    IntPtr.Zero, OpenExisting,
                    FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                    IntPtr.Zero);
                if (childHandle == InvalidHandle)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Exact empty-child cleanup child open failed.");
                StockRuntimeByHandleFileInformation childBefore =
                    Information(childHandle);
                RequireOrdinaryExactDirectory(
                    childHandle, child, childBefore);
                RequireOnlyDirectoryDataStreams(childHandle);
                RequireEmptyDirectory(child);

                RequireOrdinaryExactDirectory(
                    parentHandle, parent, parentBefore);
                RequireOnlyDirectoryDataStreams(parentHandle);
                RequireExactSingleChild(parent, child);
                RequireOrdinaryExactDirectory(
                    childHandle, child, childBefore);
                RequireOnlyDirectoryDataStreams(childHandle);
                RequireEmptyDirectory(child);

                if (!MarkDeleteOnClose(childHandle))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Exact empty-child cleanup disposition failed.");
                if (!CloseHandle(childHandle))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Exact empty-child cleanup close failed.");
                childHandle = IntPtr.Zero;
                if (GetFileAttributes(child) != UInt32.MaxValue)
                    throw new InvalidOperationException(
                        "Exact empty-child cleanup target remains present.");
                RequireOrdinaryExactDirectory(
                    parentHandle, parent, parentBefore);
                RequireOnlyDirectoryDataStreams(parentHandle);
                RequireEmptyDirectory(parent);
            }
            finally
            {
                if (childHandle != IntPtr.Zero &&
                    childHandle != InvalidHandle)
                    CloseHandle(childHandle);
                if (parentHandle != IntPtr.Zero &&
                    parentHandle != InvalidHandle)
                    CloseHandle(parentHandle);
            }
        }

        public bool Revalidate()
        {
            if (disposed || handles.Count == 0) return false;
            StockRuntimeByHandleFileInformation information = Information(RootHandle);
            return information.VolumeSerialNumber == volumeSerial &&
                ((((ulong)information.FileIndexHigh << 32) | information.FileIndexLow) == fileId) &&
                String.Equals(FinalPath(RootHandle), canonicalPath,
                    StringComparison.OrdinalIgnoreCase);
        }

        public bool VerifyRootSubstitutionBlocked()
        {
            if (disposed || !Revalidate()) return false;
            string moved = canonicalPath + ".hlclient-root-swap-probe";
            if (GetFileAttributes(moved) != UInt32.MaxValue) return false;
            bool moveBlocked = !MoveFileEx(canonicalPath, moved, 0);
            if (!moveBlocked)
            {
                // Best-effort self-test recovery; production publication never
                // attempts a path rename of its retained root.
                MoveFileEx(moved, canonicalPath, 0);
            }
            return moveBlocked && Revalidate() &&
                GetFileAttributes(moved) == UInt32.MaxValue;
        }

        public string IdentityCategory
        {
            get { return "retained-volume-and-file-id"; }
        }

        public string CanonicalPath
        {
            get { return canonicalPath; }
        }

        // Reads an existing bounded artifact through a single no-share-write/
        // no-share-delete handle. The exact ordinary-file identity is checked
        // before and after the read, so callers can validate and later publish
        // these same bytes without reopening a mutable path by name.
        public byte[] ReadExistingFile(string leafName, int maximumBytes)
        {
            if (disposed || !Revalidate())
                throw new InvalidOperationException(
                    "Read directory capability is no longer valid.");
            if (!ValidLeafName(leafName) || maximumBytes < 1 ||
                maximumBytes > MaximumPublicationBytes)
                throw new InvalidOperationException(
                    "Bounded retained-handle read parameters are invalid.");
            string path = Path.Combine(canonicalPath, leafName);
            IntPtr handle = CreateFile(
                path, GenericRead | FileReadAttributes,
                FileShareRead, IntPtr.Zero, OpenExisting,
                FileFlagOpenReparsePoint, IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Bounded retained-handle read open failed.");
            try
            {
                StockRuntimeByHandleFileInformation information =
                    Information(handle);
                ulong size = ((ulong)information.FileSizeHigh << 32) |
                    information.FileSizeLow;
                if (size == 0 || size > (ulong)maximumBytes)
                    throw new InvalidOperationException(
                        "Bounded retained-handle read size is invalid.");
                RequireOrdinaryExactFile(handle, path, size);
                RequireOnlyDefaultDataStream(handle, size);
                byte[] bytes = new byte[(int)size];
                uint read;
                if (!ReadFile(handle, bytes, (uint)bytes.Length,
                        out read, IntPtr.Zero) || read != (uint)bytes.Length)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Bounded retained-handle read failed.");
                byte[] extra = new byte[1];
                if (!ReadFile(handle, extra, 1, out read, IntPtr.Zero) ||
                    read != 0)
                    throw new InvalidOperationException(
                        "Bounded retained-handle read length changed.");
                RequireOrdinaryExactFile(handle, path, size);
                RequireOnlyDefaultDataStream(handle, size);
                if (!Revalidate())
                    throw new InvalidOperationException(
                        "Read directory identity changed.");
                return bytes;
            }
            finally
            {
                CloseHandle(handle);
            }
        }

        // Publishes a bounded byte string through one retained native handle:
        // CREATE_NEW random temporary, write/flush/read-back, no-replace
        // handle rename, then a second same-handle identity/read-back check.
        // At no point is a closed temporary path trusted or moved by name.
        public void PublishNewFile(string leafName, byte[] bytes)
        {
            PublishNewFiles(
                new string[] { leafName }, new byte[][] { bytes });
        }

        // A final evidence set is a single rollback-capable batch. All
        // temporary handles are prepared first and remain held until every
        // no-replace rename and same-handle verification succeeds. If any
        // member fails, every renamed member is deleted by its retained
        // handle before this method returns and no final leaf remains.
        public void PublishNewFiles(string[] leafNames, byte[][] payloads)
        {
            PublishNewFilesCore(leafNames, payloads, false, false, false);
        }

        // Replaces one fixed metadata leaf only when the exact previously
        // validated bytes are still present. The old file is held without
        // share-write/delete, renamed by handle to a private backup, and the
        // prepared replacement is then renamed no-replace. A substitution at
        // either name fails closed; no mutable pathname is overwritten.
        public void PublishReplacingFile(
            string leafName, byte[] expectedPrevious, byte[] bytes)
        {
            PublishReplacingFileIfExact(
                leafName, expectedPrevious, bytes, false, false);
        }

        public bool VerifyReplacingRollbackPreserved()
        {
            if (disposed || !Revalidate()) return false;
            string leaf = ".hlclient-replacing-rollback-" +
                Guid.NewGuid().ToString("N") + ".json";
            byte[] original = Encoding.ASCII.GetBytes("{\"generation\":1}");
            byte[] replacement = Encoding.ASCII.GetBytes("{\"generation\":2}");
            bool failed = false;
            try
            {
                PublishNewFile(leaf, original);
                try
                {
                    PublishReplacingFileIfExact(
                        leaf, original, replacement, false, true);
                }
                catch
                {
                    failed = true;
                }
                byte[] observed = ReadExistingFile(leaf, 1024);
                bool exact = observed.Length == original.Length;
                for (int index = 0;
                     exact && index < original.Length; ++index)
                    exact = observed[index] == original[index];
                return failed && exact && Revalidate();
            }
            finally
            {
                DeleteFile(Path.Combine(canonicalPath, leaf));
            }
        }

        public bool VerifyReplacingExpectedPriorMismatchBlocked()
        {
            if (disposed || !Revalidate()) return false;
            string leaf = ".hlclient-replacing-mismatch-" +
                Guid.NewGuid().ToString("N") + ".json";
            byte[] original = Encoding.ASCII.GetBytes("{\"generation\":1}");
            byte[] wrong = Encoding.ASCII.GetBytes("{\"generation\":0}");
            byte[] replacement = Encoding.ASCII.GetBytes("{\"generation\":2}");
            bool failed = false;
            try
            {
                PublishNewFile(leaf, original);
                try
                {
                    PublishReplacingFile(leaf, wrong, replacement);
                }
                catch
                {
                    failed = true;
                }
                byte[] observed = ReadExistingFile(leaf, 1024);
                return failed && ExactBytes(observed, original) && Revalidate();
            }
            finally
            {
                DeleteFile(Path.Combine(canonicalPath, leaf));
            }
        }

        public bool VerifyReplacingSubstitutionBlocked()
        {
            if (disposed || !Revalidate()) return false;
            string leaf = ".hlclient-replacing-substitution-" +
                Guid.NewGuid().ToString("N") + ".json";
            byte[] original = Encoding.ASCII.GetBytes("{\"generation\":1}");
            byte[] replacement = Encoding.ASCII.GetBytes("{\"generation\":2}");
            byte[] substitute = Encoding.ASCII.GetBytes("substitute");
            HashSet<string> priorBackups = new HashSet<string>(
                Directory.GetFiles(
                    canonicalPath, ".hlclient-stock-runtime-prior-*.tmp"),
                StringComparer.OrdinalIgnoreCase);
            bool failed = false;
            try
            {
                PublishNewFile(leaf, original);
                try
                {
                    PublishReplacingFileIfExact(
                        leaf, original, replacement, true, false);
                }
                catch
                {
                    failed = true;
                }
                byte[] observed = File.ReadAllBytes(
                    Path.Combine(canonicalPath, leaf));
                return failed && ExactBytes(observed, substitute) &&
                    Revalidate();
            }
            finally
            {
                DeleteFile(Path.Combine(canonicalPath, leaf));
                foreach (string backup in Directory.GetFiles(
                    canonicalPath, ".hlclient-stock-runtime-prior-*.tmp"))
                    if (!priorBackups.Contains(backup)) DeleteFile(backup);
            }
        }

        // Deterministically forces a rollback after the first retained-handle
        // rename, substitutes a new file only after every trusted handle was
        // closed, and proves rollback never path-deletes that replacement.
        // Four and five members exercise the baseline and reconnect shapes.
        public bool VerifyRollbackReplacementPreserved(int memberCount)
        {
            if (disposed || (memberCount != 4 && memberCount != 5))
                return false;
            string nonce = Guid.NewGuid().ToString("N");
            string[] leaves = new string[memberCount];
            byte[][] payloads = new byte[memberCount][];
            for (int index = 0; index < memberCount; ++index)
            {
                leaves[index] = ".hlclient-rollback-selftest-" + nonce +
                    "-" + index.ToString() + ".json";
                payloads[index] = Encoding.UTF8.GetBytes(
                    "{\"member\":" + index.ToString() + "}");
            }
            bool failed = false;
            try
            {
                PublishNewFilesCore(leaves, payloads, true, false, false);
            }
            catch
            {
                failed = true;
            }
            string replacement = Path.Combine(canonicalPath, leaves[0]);
            byte[] expected = RollbackReplacementBytes();
            bool preserved = false;
            try
            {
                byte[] observed = File.ReadAllBytes(replacement);
                preserved = observed.Length == expected.Length;
                for (int index = 0;
                    preserved && index < expected.Length; ++index)
                    preserved = observed[index] == expected[index];
                return failed && preserved && Revalidate();
            }
            finally
            {
                // These names exist only inside the private self-test root.
                // Production rollback never performs this path deletion.
                for (int index = 0; index < leaves.Length; ++index)
                    DeleteFile(Path.Combine(canonicalPath, leaves[index]));
            }
        }

        private static byte[] RollbackReplacementBytes()
        {
            return new byte[] { 0x72, 0x65, 0x70, 0x6c, 0x61, 0x63, 0x65 };
        }

        private void PublishNewFilesCore(
            string[] leafNames, byte[][] payloads,
            bool injectReplacementAfterClose, bool replaceExisting,
            bool injectFailureAfterReplace)
        {
            if (disposed || !Revalidate())
                throw new InvalidOperationException(
                    "Publication directory capability is no longer valid.");
            if (leafNames == null || payloads == null ||
                leafNames.Length == 0 || leafNames.Length > 16 ||
                leafNames.Length != payloads.Length)
                throw new InvalidOperationException(
                    "Publication batch shape is invalid.");

            HashSet<string> uniqueLeaves = new HashSet<string>(
                StringComparer.OrdinalIgnoreCase);
            for (int index = 0; index < leafNames.Length; ++index)
            {
                if (!ValidLeafName(leafNames[index]) ||
                    !uniqueLeaves.Add(leafNames[index]))
                    throw new InvalidOperationException(
                        "Publication requires unique safe bounded leaf names.");
                if (payloads[index] == null || payloads[index].Length == 0 ||
                    payloads[index].Length > MaximumPublicationBytes)
                    throw new InvalidOperationException(
                        "Publication payload is empty or outside its byte bound.");
            }

            string[] destinations = new string[leafNames.Length];
            string[] temporaryPaths = new string[leafNames.Length];
            IntPtr[] temporaries = new IntPtr[leafNames.Length];
            bool[] renamed = new bool[leafNames.Length];
            bool[] deleteMarked = new bool[leafNames.Length];
            for (int index = 0; index < temporaries.Length; ++index)
                temporaries[index] = InvalidHandle;
            bool complete = false;
            bool rollbackComplete = true;
            try
            {
                for (int index = 0; index < leafNames.Length; ++index)
                {
                    destinations[index] = Path.Combine(
                        canonicalPath, leafNames[index]);
                    temporaries[index] = CreateTemporaryFile(
                        out temporaryPaths[index]);
                    RequireOrdinaryExactFile(
                        temporaries[index], temporaryPaths[index], 0);

                    uint written;
                    byte[] bytes = payloads[index];
                    if (!WriteFile(
                            temporaries[index], bytes, (uint)bytes.Length,
                            out written, IntPtr.Zero) ||
                        written != (uint)bytes.Length)
                        throw new Win32Exception(Marshal.GetLastWin32Error(),
                            "Atomic publication write failed.");
                    if (!FlushFileBuffers(temporaries[index]))
                        throw new Win32Exception(Marshal.GetLastWin32Error(),
                            "Atomic publication flush failed.");
                    RequireOrdinaryExactFile(
                        temporaries[index], temporaryPaths[index],
                        (ulong)bytes.Length);
                    RequireExactBytes(temporaries[index], bytes);
                }

                for (int index = 0; index < leafNames.Length; ++index)
                {
                    RenameOpenFile(
                        temporaries[index], destinations[index],
                        replaceExisting);
                    renamed[index] = true;
                    RequireOrdinaryExactFile(
                        temporaries[index], destinations[index],
                        (ulong)payloads[index].Length);
                    RequireExactBytes(temporaries[index], payloads[index]);
                    if (injectReplacementAfterClose && index == 0)
                        throw new IOException(
                            "Forced retained-handle rollback self-test.");
                    if (injectFailureAfterReplace && index == 0)
                        throw new IOException(
                            "Forced replacing-publication rollback self-test.");
                }
                if (!Revalidate())
                    throw new InvalidOperationException(
                        "Publication directory identity changed after batch rename.");
                complete = true;
            }
            finally
            {
                if (!complete)
                {
                    for (int index = 0; index < temporaries.Length; ++index)
                    {
                        if (temporaries[index] != IntPtr.Zero &&
                            temporaries[index] != InvalidHandle)
                        {
                            deleteMarked[index] =
                                MarkDeleteOnClose(temporaries[index]);
                            if (!deleteMarked[index]) rollbackComplete = false;
                        }
                    }
                }
                for (int index = 0; index < temporaries.Length; ++index)
                {
                    if (temporaries[index] != IntPtr.Zero &&
                        temporaries[index] != InvalidHandle)
                        CloseHandle(temporaries[index]);
                }
                if (!complete && injectReplacementAfterClose &&
                    renamed.Length != 0 && renamed[0])
                {
                    // The trusted file was already dispositioned and its
                    // handle closed. This new object deliberately reuses only
                    // the pathname and must never be deleted by rollback.
                    File.WriteAllBytes(
                        destinations[0], RollbackReplacementBytes());
                }
                if (!complete)
                {
                    for (int index = 0; index < temporaries.Length; ++index)
                    {
                        string cleanupPath = renamed[index]
                            ? destinations[index] : temporaryPaths[index];
                        // Absence is only an observation. Never delete by path
                        // after the retained handle closes: that name may now
                        // denote an unrelated replacement object.
                        if (!String.IsNullOrEmpty(cleanupPath) &&
                            GetFileAttributes(cleanupPath) != UInt32.MaxValue)
                            rollbackComplete = false;
                    }
                }
                if (!complete && !rollbackComplete)
                    throw new IOException(
                        "Atomic publication batch rollback was incomplete.");
            }
        }

        private void PublishReplacingFileIfExact(
            string leafName, byte[] expectedPrevious, byte[] bytes,
            bool injectSubstitutionBeforeReplace,
            bool injectFailureAfterReplace)
        {
            if (disposed || !Revalidate())
                throw new InvalidOperationException(
                    "Replacing publication directory capability is invalid.");
            if (!ValidLeafName(leafName) || expectedPrevious == null ||
                expectedPrevious.Length == 0 ||
                expectedPrevious.Length > MaximumPublicationBytes ||
                bytes == null || bytes.Length == 0 ||
                bytes.Length > MaximumPublicationBytes)
                throw new InvalidOperationException(
                    "Replacing publication parameters are invalid.");

            string destination = Path.Combine(canonicalPath, leafName);
            string temporaryPath = null;
            string backupPath = null;
            IntPtr previous = InvalidHandle;
            IntPtr replacement = InvalidHandle;
            bool previousMoved = false;
            bool complete = false;
            bool rollbackComplete = true;
            Exception failure = null;
            try
            {
                previous = CreateFile(
                    destination, GenericRead | DeleteAccess | FileReadAttributes,
                    FileShareRead, IntPtr.Zero, OpenExisting,
                    FileFlagOpenReparsePoint, IntPtr.Zero);
                if (previous == InvalidHandle)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Expected prior publication is absent or busy.");
                RequireOrdinaryExactFile(
                    previous, destination, (ulong)expectedPrevious.Length);
                RequireExactBytes(previous, expectedPrevious);
                RequireOrdinaryExactFile(
                    previous, destination, (ulong)expectedPrevious.Length);

                replacement = CreateTemporaryFile(out temporaryPath);
                RequireOrdinaryExactFile(replacement, temporaryPath, 0);
                uint written;
                if (!WriteFile(replacement, bytes, (uint)bytes.Length,
                        out written, IntPtr.Zero) ||
                    written != (uint)bytes.Length)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Replacing publication write failed.");
                if (!FlushFileBuffers(replacement))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Replacing publication flush failed.");
                RequireOrdinaryExactFile(
                    replacement, temporaryPath, (ulong)bytes.Length);
                RequireExactBytes(replacement, bytes);

                backupPath = Path.Combine(
                    canonicalPath, ".hlclient-stock-runtime-prior-" +
                    Guid.NewGuid().ToString("N") + ".tmp");
                RenameOpenFile(previous, backupPath, false);
                previousMoved = true;
                RequireOrdinaryExactFile(
                    previous, backupPath, (ulong)expectedPrevious.Length);
                RequireExactBytes(previous, expectedPrevious);

                if (injectSubstitutionBeforeReplace)
                    File.WriteAllBytes(destination,
                        Encoding.ASCII.GetBytes("substitute"));
                RenameOpenFile(replacement, destination, false);
                RequireOrdinaryExactFile(
                    replacement, destination, (ulong)bytes.Length);
                RequireExactBytes(replacement, bytes);
                if (injectFailureAfterReplace)
                    throw new IOException(
                        "Forced exact-prior replacement rollback self-test.");
                if (!Revalidate())
                    throw new InvalidOperationException(
                        "Replacing publication directory identity changed.");

                if (!MarkDeleteOnClose(previous))
                    throw new IOException(
                        "Prior publication could not be dispositioned.");
                if (!CloseHandle(previous))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Prior publication handle close failed.");
                previous = InvalidHandle;
                complete = true;
            }
            catch (Exception error)
            {
                failure = error;
            }
            finally
            {
                if (!complete)
                {
                    if (replacement != IntPtr.Zero &&
                        replacement != InvalidHandle)
                    {
                        if (!MarkDeleteOnClose(replacement))
                            rollbackComplete = false;
                        if (!CloseHandle(replacement))
                            rollbackComplete = false;
                        replacement = InvalidHandle;
                    }
                    if (previousMoved && previous != IntPtr.Zero &&
                        previous != InvalidHandle)
                    {
                        try
                        {
                            RenameOpenFile(previous, destination, false);
                            RequireOrdinaryExactFile(
                                previous, destination,
                                (ulong)expectedPrevious.Length);
                            RequireExactBytes(previous, expectedPrevious);
                        }
                        catch
                        {
                            rollbackComplete = false;
                        }
                    }
                }
                if (replacement != IntPtr.Zero &&
                    replacement != InvalidHandle)
                    CloseHandle(replacement);
                if (previous != IntPtr.Zero && previous != InvalidHandle)
                    CloseHandle(previous);
            }
            if (failure != null)
            {
                if (!rollbackComplete)
                    throw new IOException(
                        "Exact-prior replacing publication rollback was incomplete.",
                        failure);
                throw failure;
            }
        }

        // Exercises the same no-share-delete temporary-handle primitive used
        // by both final restoration and run-manifest publication. The test
        // succeeds only when delete and replacement-by-name are both blocked
        // while the trusted file handle remains open.
        public bool VerifyTemporarySubstitutionBlocked()
        {
            if (disposed || !Revalidate()) return false;
            string temporaryPath = null;
            string substitutePath = null;
            IntPtr temporary = InvalidHandle;
            try
            {
                temporary = CreateTemporaryFile(out temporaryPath);
                byte[] original = new byte[] { 0x31, 0x32, 0x33, 0x34 };
                uint written;
                if (!WriteFile(temporary, original, (uint)original.Length,
                        out written, IntPtr.Zero) ||
                    written != (uint)original.Length ||
                    !FlushFileBuffers(temporary))
                    return false;
                RequireOrdinaryExactFile(
                    temporary, temporaryPath, (ulong)original.Length);
                RequireExactBytes(temporary, original);

                substitutePath = temporaryPath + ".replacement";
                File.WriteAllBytes(substitutePath,
                    new byte[] { 0x61, 0x62, 0x63, 0x64 });
                bool deleteBlocked = !DeleteFile(temporaryPath);
                bool replacementBlocked = !MoveFileEx(
                    substitutePath, temporaryPath, MoveFileReplaceExisting);
                RequireOrdinaryExactFile(
                    temporary, temporaryPath, (ulong)original.Length);
                RequireExactBytes(temporary, original);
                return deleteBlocked && replacementBlocked && Revalidate();
            }
            catch
            {
                return false;
            }
            finally
            {
                if (temporary != IntPtr.Zero && temporary != InvalidHandle)
                {
                    MarkDeleteOnClose(temporary);
                    CloseHandle(temporary);
                }
                if (!String.IsNullOrEmpty(temporaryPath))
                    DeleteFile(temporaryPath);
                if (!String.IsNullOrEmpty(substitutePath))
                    DeleteFile(substitutePath);
            }
        }

        public void Dispose()
        {
            if (disposed) return;
            for (int index = handles.Count - 1; index >= 0; --index)
            {
                if (handles[index] != IntPtr.Zero && handles[index] != InvalidHandle)
                    CloseHandle(handles[index]);
            }
            handles.Clear();
            disposed = true;
        }

        private IntPtr RootHandle
        {
            get { return handles[0]; }
        }

        private void OpenAndHold(string path, bool requireDirectory)
        {
            IntPtr handle = CreateFile(
                path, FileReadAttributes, FileShareRead | FileShareWrite,
                IntPtr.Zero, OpenExisting,
                FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Could not retain directory identity for " + path + ".");
            try
            {
                StockRuntimeByHandleFileInformation information = Information(handle);
                if ((requireDirectory &&
                        (information.FileAttributes & FileAttributeDirectory) == 0) ||
                    (information.FileAttributes & FileAttributeReparsePoint) != 0 ||
                    !String.Equals(FinalPath(handle),
                        Path.GetFullPath(path).TrimEnd('\\', '/'),
                        StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "Directory capability encountered a reparse or path mismatch.");
                handles.Add(handle);
                handle = IntPtr.Zero;
            }
            finally
            {
                if (handle != IntPtr.Zero && handle != InvalidHandle)
                    CloseHandle(handle);
            }
        }

        private IntPtr CreateTemporaryFile(out string temporaryPath)
        {
            byte[] random = new byte[16];
            for (int attempt = 0; attempt < 16; ++attempt)
            {
                using (RandomNumberGenerator generator =
                        RandomNumberGenerator.Create())
                    generator.GetBytes(random);
                StringBuilder name = new StringBuilder(
                    ".hlclient-stock-runtime-");
                for (int index = 0; index < random.Length; ++index)
                    name.Append(random[index].ToString("x2"));
                name.Append(".tmp");
                temporaryPath = Path.Combine(canonicalPath, name.ToString());
                IntPtr handle = CreateFile(
                    temporaryPath,
                    GenericRead | GenericWrite | DeleteAccess |
                        FileReadAttributes,
                    FileShareRead, IntPtr.Zero, CreateNew,
                    FileAttributeTemporary | FileFlagOpenReparsePoint |
                        FileFlagWriteThrough,
                    IntPtr.Zero);
                if (handle != InvalidHandle) return handle;
                int error = Marshal.GetLastWin32Error();
                if (error != 80 && error != 183)
                    throw new Win32Exception(error,
                        "Atomic publication temporary creation failed.");
            }
            temporaryPath = null;
            throw new IOException(
                "Atomic publication exhausted random temporary names.");
        }

        private static bool ValidLeafName(string leafName)
        {
            if (String.IsNullOrEmpty(leafName) || leafName.Length > 160 ||
                leafName == "." || leafName == ".." ||
                !String.Equals(Path.GetFileName(leafName), leafName,
                    StringComparison.Ordinal) ||
                leafName.EndsWith(" ", StringComparison.Ordinal) ||
                leafName.EndsWith(".", StringComparison.Ordinal))
                return false;
            foreach (char value in leafName)
            {
                if (value < 0x20 || value == '/' || value == '\\' ||
                    value == ':' || value == '*' || value == '?' ||
                    value == '"' || value == '<' || value == '>' ||
                    value == '|')
                    return false;
            }
            return true;
        }

        private static bool ValidLowerHexRunId(string value)
        {
            if (String.IsNullOrEmpty(value) || value.Length != 32)
                return false;
            for (int index = 0; index < value.Length; ++index)
            {
                char character = value[index];
                if (!((character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f')))
                    return false;
            }
            return true;
        }

        private static void RequireOrdinaryExactDirectory(
            IntPtr handle, string expectedPath,
            StockRuntimeByHandleFileInformation expectedInformation)
        {
            StockRuntimeByHandleFileInformation observed =
                Information(handle);
            if ((observed.FileAttributes & FileAttributeDirectory) == 0 ||
                (observed.FileAttributes & FileAttributeReparsePoint) != 0 ||
                observed.VolumeSerialNumber !=
                    expectedInformation.VolumeSerialNumber ||
                observed.FileIndexHigh != expectedInformation.FileIndexHigh ||
                observed.FileIndexLow != expectedInformation.FileIndexLow ||
                !String.Equals(FinalPath(handle),
                    Path.GetFullPath(expectedPath).TrimEnd('\\', '/'),
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup directory identity is invalid.");
        }

        private static void RequireExactSingleChild(
            string parent, string expectedChild)
        {
            string[] entries = Directory.GetFileSystemEntries(parent);
            if (entries.Length != 1 ||
                !String.Equals(Path.GetFullPath(entries[0]), expectedChild,
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup parent inventory is ambiguous.");
        }

        private static void RequireEmptyDirectory(string path)
        {
            if (Directory.GetFileSystemEntries(path).Length != 0)
                throw new InvalidOperationException(
                    "Exact empty-child cleanup refuses nonempty content.");
        }

        private static void RequireOrdinaryExactFile(
            IntPtr handle, string expectedPath, ulong expectedSize)
        {
            StockRuntimeByHandleFileInformation information =
                Information(handle);
            ulong size = ((ulong)information.FileSizeHigh << 32) |
                information.FileSizeLow;
            if ((information.FileAttributes &
                    (FileAttributeDirectory | FileAttributeReparsePoint)) != 0 ||
                information.NumberOfLinks != 1 || size != expectedSize ||
                !String.Equals(FinalPath(handle),
                    Path.GetFullPath(expectedPath).TrimEnd('\\', '/'),
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "Atomic publication file identity is invalid.");
        }

        // FILE_STREAM_INFO is queried through the same retained file handle as
        // the byte read. A fixed upper bound makes an adversarial stream list a
        // typed failure instead of an allocation request. The only accepted
        // entry is the unnamed NTFS data stream for the exact primary length.
        private static void RequireOnlyDefaultDataStream(
            IntPtr handle, ulong expectedSize)
        {
            IntPtr buffer = Marshal.AllocHGlobal(MaximumStreamInformationBytes);
            try
            {
                if (!GetFileInformationByHandleEx(
                        handle, FileStreamInfo, buffer,
                        MaximumStreamInformationBytes))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Bounded retained-handle stream inventory failed.");

                int offset = 0;
                int count = 0;
                while (true)
                {
                    if (offset < 0 ||
                        offset > MaximumStreamInformationBytes - 24)
                        throw new InvalidOperationException(
                            "Bounded retained-handle stream inventory is invalid.");
                    uint next = unchecked((uint)Marshal.ReadInt32(buffer, offset));
                    uint nameBytes = unchecked((uint)Marshal.ReadInt32(
                        buffer, offset + 4));
                    long streamSize = Marshal.ReadInt64(buffer, offset + 8);
                    if (nameBytes == 0 || (nameBytes & 1U) != 0U ||
                        nameBytes > (uint)(MaximumStreamInformationBytes -
                            offset - 24))
                        throw new InvalidOperationException(
                            "Bounded retained-handle stream inventory is invalid.");
                    string name = Marshal.PtrToStringUni(
                        IntPtr.Add(buffer, offset + 24),
                        checked((int)(nameBytes / 2U)));
                    ++count;
                    if (count != 1 ||
                        !String.Equals(name, "::$DATA",
                            StringComparison.Ordinal) ||
                        streamSize < 0 || (ulong)streamSize != expectedSize)
                        throw new InvalidOperationException(
                            "Bounded retained-handle read requires only the default data stream.");
                    if (next == 0U) break;
                    if (next < 24U + nameBytes ||
                        next > (uint)(MaximumStreamInformationBytes - offset))
                        throw new InvalidOperationException(
                            "Bounded retained-handle stream inventory is invalid.");
                    offset = checked(offset + (int)next);
                }
                if (count != 1)
                    throw new InvalidOperationException(
                        "Bounded retained-handle read requires only the default data stream.");
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }

        private static void RequireOnlyDirectoryDataStreams(IntPtr handle)
        {
            IntPtr buffer = Marshal.AllocHGlobal(MaximumStreamInformationBytes);
            try
            {
                StockRuntimeIoStatusBlock ioStatus;
                int status = NtQueryInformationFile(
                    handle, out ioStatus, buffer,
                    MaximumStreamInformationBytes,
                    NtFileStreamInformation);
                if (status == StatusNoMoreFiles) return;
                if (status != 0)
                    throw new InvalidOperationException(
                        "Bounded retained-directory stream inventory failed.");
                ulong used = ioStatus.Information.ToUInt64();
                if (used > MaximumStreamInformationBytes)
                    throw new InvalidOperationException(
                        "Bounded retained-directory stream inventory is invalid.");
                int offset = 0;
                int count = 0;
                while ((ulong)offset < used)
                {
                    if (offset < 0 ||
                        offset > MaximumStreamInformationBytes - 24)
                        throw new InvalidOperationException(
                            "Bounded retained-directory stream inventory is invalid.");
                    uint next = unchecked((uint)Marshal.ReadInt32(
                        buffer, offset));
                    uint nameBytes = unchecked((uint)Marshal.ReadInt32(
                        buffer, offset + 4));
                    if (nameBytes == 0 || (nameBytes & 1U) != 0U ||
                        nameBytes > (uint)(MaximumStreamInformationBytes -
                            offset - 24) ||
                        (ulong)offset + 24U + nameBytes > used)
                        throw new InvalidOperationException(
                            "Bounded retained-directory stream inventory is invalid.");
                    string name = Marshal.PtrToStringUni(
                        IntPtr.Add(buffer, offset + 24),
                        checked((int)(nameBytes / 2U)));
                    ++count;
                    if (count > 128 ||
                        (!String.Equals(name, "::$DATA",
                             StringComparison.Ordinal) &&
                         !String.Equals(name, "::$INDEX_ALLOCATION",
                             StringComparison.Ordinal)))
                        throw new InvalidOperationException(
                            "Bounded retained-handle read requires only the default data stream.");
                    if (next == 0U) break;
                    if (next < 24U + nameBytes || (next & 7U) != 0U ||
                        (ulong)offset + next >= used)
                        throw new InvalidOperationException(
                            "Bounded retained-directory stream inventory is invalid.");
                    offset = checked(offset + (int)next);
                }
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }

        private static bool ExactBytes(byte[] observed, byte[] expected)
        {
            if (observed == null || expected == null ||
                observed.Length != expected.Length) return false;
            for (int index = 0; index < expected.Length; ++index)
                if (observed[index] != expected[index]) return false;
            return true;
        }

        private static void RequireExactBytes(IntPtr handle, byte[] expected)
        {
            long position;
            if (!SetFilePointerEx(handle, 0, out position, FileBegin) ||
                position != 0)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Atomic publication seek failed.");
            byte[] observed = new byte[expected.Length];
            uint read;
            if (!ReadFile(handle, observed, (uint)observed.Length,
                    out read, IntPtr.Zero) ||
                read != (uint)observed.Length)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Atomic publication read-back failed.");
            for (int index = 0; index < expected.Length; ++index)
            {
                if (observed[index] != expected[index])
                    throw new InvalidOperationException(
                        "Atomic publication read-back bytes differ.");
            }
            byte[] extra = new byte[1];
            if (!ReadFile(handle, extra, 1, out read, IntPtr.Zero) || read != 0)
                throw new InvalidOperationException(
                    "Atomic publication read-back length differs.");
        }

        private static void RenameOpenFile(
            IntPtr handle, string destination, bool replaceExisting)
        {
            byte[] nameBytes = Encoding.Unicode.GetBytes(destination);
            int rootOffset = IntPtr.Size == 8 ? 8 : 4;
            int lengthOffset = rootOffset + IntPtr.Size;
            int fileNameOffset = lengthOffset + 4;
            int headerSize = fileNameOffset + 4;
            IntPtr information = Marshal.AllocHGlobal(
                headerSize + nameBytes.Length);
            try
            {
                for (int index = 0;
                     index < headerSize + nameBytes.Length; ++index)
                    Marshal.WriteByte(information, index, 0);
                Marshal.WriteByte(
                    information, 0, replaceExisting ? (byte)1 : (byte)0);
                Marshal.WriteIntPtr(information, rootOffset, IntPtr.Zero);
                Marshal.WriteInt32(
                    information, lengthOffset, nameBytes.Length);
                Marshal.Copy(
                    nameBytes, 0, IntPtr.Add(information, fileNameOffset),
                    nameBytes.Length);
                if (!SetFileInformationByHandle(
                        handle, FileRenameInfo, information,
                        (uint)(headerSize + nameBytes.Length)))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Atomic no-replace publication rename failed.");
            }
            finally
            {
                Marshal.FreeHGlobal(information);
            }
        }

        private static bool MarkDeleteOnClose(IntPtr handle)
        {
            IntPtr disposition = Marshal.AllocHGlobal(4);
            try
            {
                Marshal.WriteInt32(disposition, 1);
                return SetFileInformationByHandle(
                    handle, FileDispositionInfo, disposition, 4);
            }
            finally
            {
                Marshal.FreeHGlobal(disposition);
            }
        }

        private static StockRuntimeByHandleFileInformation Information(IntPtr handle)
        {
            StockRuntimeByHandleFileInformation information;
            if (!GetFileInformationByHandle(handle, out information))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Directory identity query failed.");
            return information;
        }

        private static string FinalPath(IntPtr handle)
        {
            StringBuilder buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandle(
                handle, buffer, (uint)buffer.Capacity,
                FileNameNormalized | VolumeNameDos);
            if (length == 0 || length >= buffer.Capacity)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Directory final-path query failed.");
            string value = buffer.ToString();
            const string extendedPrefix = @"\\?\";
            if (value.StartsWith(extendedPrefix, StringComparison.Ordinal))
                value = value.Substring(extendedPrefix.Length);
            return value.TrimEnd('\\', '/');
        }
    }
}
'@
}

function New-RetainedDirectoryCapability {
    param([string]$Path, [string]$AnchorPath, [string]$Label)
    Initialize-RestorationDirectoryCapabilityNative
    try {
        return [Hlclient.StockRuntimeDirectoryCapability]::Open(
            $Path, $AnchorPath)
    } catch {
        throw "$Label retained directory identity could not be acquired: $($_.Exception.Message)"
    }
}

function Assert-RestorationDirectoryCapabilities {
    param([object]$Guard)
    foreach ($entry in @(
            @{ Capability = $Guard.ResearchDirectoryCapability; Label = 'research root' },
            @{ Capability = $Guard.BackupRootDirectoryCapability; Label = 'backup root' },
            @{ Capability = $Guard.BackupDataDirectoryCapability; Label = 'backup data root' })) {
        if ($null -eq $entry.Capability -or -not $entry.Capability.Revalidate()) {
            throw "Restoration $($entry.Label) retained identity changed."
        }
    }
}

function Close-RestorationBackupCapabilities {
    param([object]$Guard)
    foreach ($name in @('BackupDataDirectoryCapability',
            'BackupRootDirectoryCapability')) {
        if ($null -ne $Guard -and $null -ne $Guard.$name) {
            $Guard.$name.Dispose()
            $Guard.$name = $null
        }
    }
}

function Close-RestorationGuardCapabilities {
    param([object]$Guard)
    if ($null -eq $Guard) { return }
    Close-RestorationBackupCapabilities $Guard
    if ($null -ne $Guard.ResearchDirectoryCapability) {
        $Guard.ResearchDirectoryCapability.Dispose()
        $Guard.ResearchDirectoryCapability = $null
    }
}

function New-RunDirectoryCapability {
    param([string]$RunRoot)
    foreach ($leaf in @('raw', 'logs', 'version-observation.staged.json',
            'isolation-attestation.staged.json')) {
        $anchor = Join-Path $RunRoot $leaf
        if (Test-Path -LiteralPath $anchor) {
            Assert-NoReparsePointInExistingPath $anchor 'capture run identity anchor'
            return New-RetainedDirectoryCapability `
                -Path $RunRoot -AnchorPath $anchor -Label 'capture run root'
        }
    }
    throw 'Capture run has no approved identity anchor.'
}

function Assert-RunDirectoryCapability {
    param([object]$Capability, [string]$RunRoot)
    if ($null -eq $Capability -or -not $Capability.Revalidate() -or
        -not $Capability.CanonicalPath.Equals(
            [IO.Path]::GetFullPath($RunRoot).TrimEnd('\', '/'),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Capture run retained directory identity changed.'
    }
}

function New-RestorationGuard {
    param([string]$Root, [object]$Snapshot)
    $researchCapability = $null
    $backupRootCapability = $null
    $backupDataCapability = $null
    $guard = $null
    $temporary = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) (
        'hlclient-stock-runtime-restore-' + [Guid]::NewGuid().ToString('N'))))
    try {
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
        $researchAnchor = Join-Path $Root $markerName
        if (-not (Test-Path -LiteralPath $researchAnchor -PathType Leaf)) {
            throw 'Restoration research identity anchor is absent.'
        }
        $researchCapability = New-RetainedDirectoryCapability `
            -Path $Root -AnchorPath $researchAnchor -Label 'research root'
        Assert-NoReparsePointInExistingPath $temporary 'restoration backup path'
        [IO.Directory]::CreateDirectory($temporary) | Out-Null
        Assert-NoReparsePointInExistingPath $temporary 'restoration backup path'
        Assert-NoReparsePoint $temporary 'restoration backup root'
        $data = Join-Path $temporary 'data'
        [IO.Directory]::CreateDirectory($data) | Out-Null
        Assert-NoReparsePoint $data 'restoration backup data root'
        $backupIdentityLock = Join-Path $data '.hlclient-restoration-identity-lock'
        [IO.Directory]::CreateDirectory($backupIdentityLock) | Out-Null
        $backupRootCapability = New-RetainedDirectoryCapability `
            -Path $temporary -AnchorPath $data `
            -Label 'restoration backup root'
        $backupDataCapability = New-RetainedDirectoryCapability `
            -Path $data -AnchorPath $backupIdentityLock `
            -Label 'restoration backup data root'
        $backed = [Collections.Generic.List[object]]::new()
        $guard = [pscustomobject]@{
            Root = $Root; TemporaryRoot = $temporary; DataRoot = $data
            Before = $Snapshot; BackedEntries = @()
            ResearchDirectoryCapability = $researchCapability
            BackupRootDirectoryCapability = $backupRootCapability
            BackupDataDirectoryCapability = $backupDataCapability
        }
        # Back up the complete bounded tree. A whitelist-only backup can detect
        # drift outside known mutable paths but cannot restore it transactionally.
        foreach ($entry in @($Snapshot.Entries | Where-Object {
                    $_.RelativePath -ne '.'
                } | Sort-Object RelativePath)) {
            $source = Join-Path $Root $entry.RelativePath.Replace('/', '\')
            $destination = Join-Path $data $entry.RelativePath.Replace('/', '\')
            Assert-RestorationDirectoryCapabilities $guard
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
        Assert-RestorationDirectoryCapabilities $guard
        $guard.BackedEntries = @($backed)
        return $guard
    } catch {
        if ($null -ne $guard) {
            Close-RestorationGuardCapabilities $guard
        } else {
            foreach ($capability in @($backupDataCapability,
                    $backupRootCapability, $researchCapability)) {
                if ($null -ne $capability) { $capability.Dispose() }
            }
        }
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
    Assert-RestorationDirectoryCapabilities $Guard
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
            Assert-RestorationDirectoryCapabilities $Guard
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
                Assert-RestorationDirectoryCapabilities $Guard
                Remove-SafeTree $item.FullName $Guard.Root
            }
        }
    }
    foreach ($entry in @($Guard.BackedEntries | Where-Object Kind -eq 'directory' |
            Sort-Object { $_.RelativePath.Split('/').Count })) {
        $target = Join-Path $Guard.Root $entry.RelativePath.Replace('/', '\')
        $parent = Split-Path -Parent $target
        Assert-RestorationDirectoryCapabilities $Guard
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
        Assert-RestorationDirectoryCapabilities $Guard
        Assert-NoReparsePointInExistingPath $parent 'restoration file parent'
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        $existing = Get-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
        if ($null -ne $existing) {
            Assert-RestorationDirectoryCapabilities $Guard
            Remove-SafeTree $target $Guard.Root
        }
        Assert-NoReparsePointInExistingPath $parent 'restoration file parent'
        # Never overwrite: a raced-in link or file makes Copy fail closed.
        Assert-RestorationDirectoryCapabilities $Guard
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
    Assert-RestorationDirectoryCapabilities $Guard
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

function Assert-ExactJsonProperties {
    param(
        [object]$Value,
        [string[]]$Expected,
        [string]$Label
    )
    if ($null -eq $Value -or $Value -is [Array] -or $Value -is [string]) {
        throw "$Label must be one JSON object."
    }
    $actual = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    if ($actual.Count -ne $Expected.Count) {
        throw "$Label property set is not exact."
    }
    foreach ($name in $Expected) {
        if ($actual -cnotcontains $name) {
            throw "$Label property set is not exact."
        }
    }
    foreach ($name in $actual) {
        if ($Expected -cnotcontains $name) {
            throw "$Label property set is not exact."
        }
    }
}

function Get-BoundedJsonInteger {
    param(
        [object]$Value,
        [Int64]$Minimum,
        [Int64]$Maximum,
        [string]$Label
    )
    if (-not ($Value -is [byte] -or $Value -is [sbyte] -or
            $Value -is [Int16] -or $Value -is [UInt16] -or
            $Value -is [Int32] -or $Value -is [UInt32] -or
            $Value -is [Int64])) {
        throw "$Label must be an integer."
    }
    [Int64]$number = $Value
    if ($number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Label is outside its bound."
    }
    return $number
}

function Assert-LowerSha256Reference {
    param([object]$Value, [string]$Label)
    if (-not ($Value -is [string]) -or
        [string]$Value -cnotmatch '^[0-9a-f]{64}$') {
        throw "$Label is not a private SHA-256 reference."
    }
}

function Get-ResearchPreparationInventory {
    param([string]$Root, [object[]]$Items)
    $v1Records = [Collections.Generic.List[string]]::new()
    $v2Paths = [Collections.Generic.List[string]]::new()
    $v2Records = [Collections.Generic.List[string]]::new()
    [Int64]$totalBytes = 0
    $clientSha256 = $null
    $serverSha256 = $null
    foreach ($item in $Items) {
        $relative = Get-RelativePath $item.FullName $Root
        if ($relative -ceq $markerName -or
            $relative -ceq $pendingMarkerName -or
            $relative -ceq $preparationManifestName) {
            continue
        }
        [void]$v2Paths.Add($relative)
        $isDirectory =
            ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0
        if ($isDirectory) {
            [void]$v1Records.Add('d|' + $relative)
            [void]$v2Records.Add('d|' + $relative)
            continue
        }
        if ($item.Length -lt 0 -or
            $totalBytes -gt ($maximumResearchBytes - $item.Length)) {
            throw 'Research preparation inventory exceeds its byte bound.'
        }
        $totalBytes += $item.Length
        $sha256 = Get-FileSha256 $item.FullName
        [void]$v1Records.Add(
            ('f|{0}|{1}|{2}' -f $relative, $item.Length, $sha256))
        [void]$v2Records.Add(
            ('f|{0}|{1}|{2}' -f $relative, $item.Length,
                $sha256.ToLowerInvariant()))
        if ($relative -ceq 'hl.exe') { $clientSha256 = $sha256 }
        if ($relative -ceq 'hlds.exe') { $serverSha256 = $sha256 }
    }
    if ($v1Records.Count -lt 2 -or $null -eq $clientSha256 -or
        $null -eq $serverSha256) {
        throw 'Research preparation inventory lacks required launchers.'
    }
    $v1Canonical = @($v1Records | Sort-Object) -join "`n"
    $v2OrderedPaths = $v2Paths.ToArray()
    $v2Ordered = $v2Records.ToArray()
    # The native materializer orders structured inventory entries by their
    # relative path before adding the d|/f| record prefix. Sort parallel keys
    # and values here so the live-tree verifier consumes that exact contract;
    # sorting the finished records would incorrectly group every directory
    # ahead of every file.
    [Array]::Sort(
        $v2OrderedPaths, $v2Ordered, [StringComparer]::Ordinal)
    $v2Canonical = if ($v2Ordered.Count -eq 0) { '' } else {
        (@($v2Ordered) -join "`n") + "`n"
    }
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $utf8 = [Text.UTF8Encoding]::new($false, $true)
        $v1Sha256 = ([BitConverter]::ToString(
                $algorithm.ComputeHash($utf8.GetBytes($v1Canonical)))).Replace('-', '')
        $algorithm.Initialize()
        $v2Sha256 = ([BitConverter]::ToString(
                $algorithm.ComputeHash($utf8.GetBytes($v2Canonical)))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
    return [pscustomobject]@{
        EntryCount = $v1Records.Count
        ByteCount = $totalBytes
        V1Sha256 = $v1Sha256
        V2Sha256 = $v2Sha256
        ClientSha256 = $clientSha256
        ServerSha256 = $serverSha256
    }
}

function Assert-ResearchPendingMarker {
    param([string]$Root)
    $path = [IO.Path]::GetFullPath((Join-Path $Root $pendingMarkerName))
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw 'Research preparation v2 lacks its pending/commit state marker.'
    }
    $pending = Read-BoundedJson $path 4096 `
        'research preparation pending marker'
    Assert-ExactJsonProperties $pending @(
        'schema', 'category', 'paths_recorded') `
        'research preparation pending marker'
    if ($pending.schema -cne 'hlclient.stock-research-copy-pending.v1' -or
        $pending.category -cne 'awaiting_commit_marker' -or
        -not ($pending.paths_recorded -is [bool]) -or
        $pending.paths_recorded -ne $false) {
        throw 'Research preparation pending marker policy is invalid.'
    }
}

function Assert-ResearchPreparationManifest {
    param([string]$Root, [object[]]$Items)
    $path = [IO.Path]::GetFullPath(
        (Join-Path $Root $preparationManifestName))
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw 'Research root lacks a preparation manifest.'
    }
    $manifest = Read-BoundedJson $path 32768 'research preparation manifest'
    if ($null -eq $manifest.PSObject.Properties['schema'] -or
        -not ($manifest.schema -is [string])) {
        throw 'Research preparation manifest schema is absent.'
    }
    $inventory = Get-ResearchPreparationInventory $Root $Items

    if ([string]$manifest.schema -ceq
        'hlclient.stock-runtime-research-preparation.v1') {
        $expected = @(
            'schema', 'marker', 'source_inventory_entries',
            'source_inventory_bytes', 'source_inventory_sha256',
            'client_sha256', 'server_launcher_sha256', 'paths_recorded',
            'preparation_status')
        Assert-ExactJsonProperties $manifest $expected `
            'research preparation manifest v1'
        if ($manifest.marker -cne $markerText -or
            -not ($manifest.paths_recorded -is [bool]) -or
            $manifest.paths_recorded -ne $false -or
            $manifest.preparation_status -cne 'exact-copy-verified') {
            throw 'Research preparation manifest v1 policy is invalid.'
        }
        [Int64]$entryCount = Get-BoundedJsonInteger `
            $manifest.source_inventory_entries 2 $maximumEntries `
            'v1 source entry count'
        [Int64]$byteCount = Get-BoundedJsonInteger `
            $manifest.source_inventory_bytes 1 $maximumResearchBytes `
            'v1 source byte count'
        foreach ($property in @(
                'source_inventory_sha256', 'client_sha256',
                'server_launcher_sha256')) {
            if (-not ($manifest.$property -is [string]) -or
                [string]$manifest.$property -cnotmatch '^[0-9A-F]{64}$') {
                throw "Research preparation manifest v1 $property is invalid."
            }
        }
        if ($entryCount -ne $inventory.EntryCount -or
            $byteCount -ne $inventory.ByteCount -or
            $manifest.source_inventory_sha256 -cne $inventory.V1Sha256 -or
            $manifest.client_sha256 -cne $inventory.ClientSha256 -or
            $manifest.server_launcher_sha256 -cne $inventory.ServerSha256) {
            throw 'Research preparation manifest v1 inventory disagrees with the tree.'
        }
        return [pscustomobject]@{
            Schema = [string]$manifest.schema
            ExternalTargetProfile = 'none'
            ExternalTargetCount = [Int64]0
        }
    }

    if ([string]$manifest.schema -ceq
        'hlclient.stock-runtime-research-preparation.v3') {
        $expected = @(
            'schema', 'marker', 'preparation_profile',
            'source_root_identity_fingerprint',
            'source_inventory_entries', 'source_inventory_bytes',
            'source_inventory_sha256',
            'contained_materialized_link_count',
            'approved_external_materialized_link_count',
            'source_hardlink_count',
            'destination_entry_count', 'destination_byte_count',
            'destination_inventory_sha256',
            'destination_reparse_count', 'destination_hardlink_count',
            'destination_ads_count', 'external_approval_sha256',
            'external_classification_summary', 'executable_target_count',
            'mutable_state_target_count', 'source_unchanged_status',
            'external_targets_unchanged_status', 'evidence_eligibility',
            'external_target_profile',
            'client_binary_private_identity_reference',
            'server_binary_private_identity_reference', 'paths_recorded',
            'preparation_status')
        Assert-ExactJsonProperties $manifest $expected `
            'research preparation manifest v3'

        if ($manifest.marker -cne $markerText -or
            -not ($manifest.paths_recorded -is [bool]) -or
            $manifest.paths_recorded -ne $false -or
            $manifest.source_unchanged_status -cne 'verified' -or
            $manifest.external_targets_unchanged_status -cne 'verified') {
            throw 'Research preparation manifest v3 policy is invalid.'
        }

        # Evidence eligibility is checked before any active-capture tool or WFP
        # operation. Keep the public failure typed and path-free so an
        # ineligible private review cannot be converted into a canary launch.
        if (-not ($manifest.evidence_eligibility -is [string]) -or
            $manifest.evidence_eligibility -cne 'eligible') {
            throw 'research_copy_not_evidence_eligible'
        }

        [Int64]$sourceEntryCount = Get-BoundedJsonInteger `
            $manifest.source_inventory_entries 2 $maximumEntries `
            'v3 source entry count'
        [Int64]$sourceByteCount = Get-BoundedJsonInteger `
            $manifest.source_inventory_bytes 1 $maximumResearchBytes `
            'v3 source byte count'
        [Int64]$destinationEntryCount = Get-BoundedJsonInteger `
            $manifest.destination_entry_count 2 $maximumEntries `
            'v3 destination entry count'
        [Int64]$destinationByteCount = Get-BoundedJsonInteger `
            $manifest.destination_byte_count 1 $maximumResearchBytes `
            'v3 destination byte count'
        [Int64]$containedLinkCount = Get-BoundedJsonInteger `
            $manifest.contained_materialized_link_count 0 $sourceEntryCount `
            'v3 contained materialized link count'
        [Int64]$approvedExternalCount = Get-BoundedJsonInteger `
            $manifest.approved_external_materialized_link_count 0 `
            $sourceEntryCount 'v3 approved external materialized link count'
        [void](Get-BoundedJsonInteger $manifest.source_hardlink_count 0 `
            $sourceEntryCount 'v3 source hardlink count')
        [void](Get-BoundedJsonInteger $manifest.destination_reparse_count 0 0 `
            'v3 destination reparse count')
        [void](Get-BoundedJsonInteger $manifest.destination_hardlink_count 0 0 `
            'v3 destination hardlink count')
        [void](Get-BoundedJsonInteger $manifest.destination_ads_count 0 0 `
            'v3 destination alternate-data-stream count')
        [void](Get-BoundedJsonInteger $manifest.executable_target_count 0 0 `
            'v3 executable external target count')
        [void](Get-BoundedJsonInteger $manifest.mutable_state_target_count 0 0 `
            'v3 mutable-state external target count')
        if ($containedLinkCount -gt
            ($sourceEntryCount - $approvedExternalCount)) {
            throw 'Research preparation manifest v3 link counts are inconsistent.'
        }

        foreach ($property in @(
                'source_root_identity_fingerprint',
                'source_inventory_sha256', 'destination_inventory_sha256',
                'external_approval_sha256',
                'client_binary_private_identity_reference',
                'server_binary_private_identity_reference')) {
            Assert-LowerSha256Reference $manifest.$property `
                "research preparation manifest v3 $property"
        }

        $zeroSha256 = '0' * 64
        if ($manifest.preparation_profile -ceq 'ordinary-or-contained-v3') {
            if ($approvedExternalCount -ne 0 -or
                $manifest.external_approval_sha256 -cne $zeroSha256 -or
                $manifest.external_classification_summary -cne 'none' -or
                $manifest.external_target_profile -cne 'none' -or
                $manifest.preparation_status -cne
                    'exact-materialized-copy-verified') {
                throw 'Research preparation manifest v3 ordinary profile is invalid.'
            }
        } elseif ($manifest.preparation_profile -ceq
            'reviewed-external-targets-v1') {
            if ($approvedExternalCount -lt 1 -or
                $manifest.external_approval_sha256 -ceq $zeroSha256 -or
                $manifest.external_classification_summary -cne
                    'eligible_non_executable_asset_tree' -or
                $manifest.external_target_profile -cne
                    'reviewed-non-executable-v1' -or
                $manifest.preparation_status -cne
                    'exact-reviewed-materialized-copy-verified') {
                throw 'Research preparation manifest v3 reviewed profile is invalid.'
            }
            Assert-ExternalApprovalDigestAvailable `
                ([string]$manifest.external_approval_sha256)
        } else {
            throw 'Research preparation manifest v3 preparation profile is invalid.'
        }

        if ($sourceEntryCount -ne $inventory.EntryCount -or
            $sourceByteCount -ne $inventory.ByteCount -or
            $destinationEntryCount -ne $inventory.EntryCount -or
            $destinationByteCount -ne $inventory.ByteCount -or
            $manifest.source_inventory_sha256 -cne $inventory.V2Sha256 -or
            $manifest.destination_inventory_sha256 -cne $inventory.V2Sha256) {
            throw 'Research preparation manifest v3 inventory disagrees with the tree.'
        }

        # The manifest counts are attestations, not a substitute for checking
        # the exact destination. Re-screen every materialized entry before the
        # active caller can begin isolation or process work.
        foreach ($item in $Items) {
            Assert-OnlyDefaultDataStream $item.FullName 'v3 research entry'
            if (($item.Attributes -band [IO.FileAttributes]::Directory) -eq 0) {
                Assert-NoHardLink $item.FullName 'v3 research file'
            }
        }
        Assert-ResearchPendingMarker $Root
        return [pscustomobject]@{
            Schema = [string]$manifest.schema
            ExternalTargetProfile = [string]$manifest.external_target_profile
            ExternalTargetCount = $approvedExternalCount
        }
    }

    if ([string]$manifest.schema -cne
        'hlclient.stock-runtime-research-preparation.v2') {
        throw 'Research preparation manifest schema is unsupported.'
    }
    $expected = @(
        'schema', 'marker', 'topology_profile',
        'source_root_identity_fingerprint', 'entry_count', 'byte_count',
        'materialized_link_count', 'materialized_hardlink_count',
        'rejected_link_count', 'inventory_sha256',
        'client_binary_private_identity_reference',
        'server_binary_private_identity_reference',
        'destination_unlinked_status', 'source_unchanged_status',
        'paths_recorded', 'preparation_status')
    Assert-ExactJsonProperties $manifest $expected `
        'research preparation manifest v2'
    if ($manifest.marker -cne $markerText -or
        -not ($manifest.paths_recorded -is [bool]) -or
        $manifest.paths_recorded -ne $false -or
        $manifest.destination_unlinked_status -cne 'verified' -or
        $manifest.source_unchanged_status -cne 'verified' -or
        $manifest.preparation_status -cne
            'exact-materialized-copy-verified') {
        throw 'Research preparation manifest v2 policy is invalid.'
    }
    if (-not ($manifest.topology_profile -is [Array])) {
        throw 'Research preparation topology profile must be a JSON array.'
    }
    $topology = @($manifest.topology_profile)
    $allowedTopology = @(
        'ordinary_tree', 'source_path_ancestor_reparse',
        'source_root_reparse', 'source_internal_directory_junction',
        'source_internal_directory_symlink', 'source_file_hardlink')
    if ($topology.Count -lt 1 -or $topology.Count -gt $allowedTopology.Count) {
        throw 'Research preparation topology profile count is invalid.'
    }
    $seenTopology = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($category in $topology) {
        if (-not ($category -is [string]) -or
            $allowedTopology -cnotcontains [string]$category -or
            -not $seenTopology.Add([string]$category)) {
            throw 'Research preparation topology profile is invalid.'
        }
    }
    if ($seenTopology.Contains('ordinary_tree') -and $topology.Count -ne 1) {
        throw 'ordinary_tree cannot be combined with linked topology.'
    }

    [Int64]$entryCount = Get-BoundedJsonInteger $manifest.entry_count 2 `
        $maximumEntries 'v2 source entry count'
    [void](Get-BoundedJsonInteger $manifest.byte_count 1 `
        $maximumResearchBytes 'v2 source byte count')
    [Int64]$linkCount = Get-BoundedJsonInteger `
        $manifest.materialized_link_count 0 $entryCount `
        'v2 materialized link count'
    [Int64]$hardlinkCount = Get-BoundedJsonInteger `
        $manifest.materialized_hardlink_count 0 $entryCount `
        'v2 materialized hardlink count'
    [void](Get-BoundedJsonInteger $manifest.rejected_link_count 0 0 `
        'v2 rejected link count')
    if ($seenTopology.Contains('source_file_hardlink') -ne
        ($hardlinkCount -gt 0)) {
        throw 'Research preparation hardlink topology/count disagrees.'
    }
    $linkTopologyPresent =
        $seenTopology.Contains('source_root_reparse') -or
        $seenTopology.Contains('source_internal_directory_junction') -or
        $seenTopology.Contains('source_internal_directory_symlink')
    if ($linkTopologyPresent -ne ($linkCount -gt 0)) {
        throw 'Research preparation link topology/count disagrees.'
    }
    foreach ($property in @(
            'source_root_identity_fingerprint', 'inventory_sha256',
            'client_binary_private_identity_reference',
            'server_binary_private_identity_reference')) {
        Assert-LowerSha256Reference $manifest.$property `
            "research preparation manifest v2 $property"
    }
    if ($entryCount -ne $inventory.EntryCount -or
        [Int64]$manifest.byte_count -ne $inventory.ByteCount -or
        $manifest.inventory_sha256 -cne $inventory.V2Sha256) {
        throw 'Research preparation manifest v2 inventory disagrees with the tree.'
    }
    Assert-ResearchPendingMarker $Root
    return [pscustomobject]@{
        Schema = [string]$manifest.schema
        ExternalTargetProfile = 'none'
        ExternalTargetCount = [Int64]0
    }
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
    # Directory ADS are not members of the recursive file-system inventory.
    # Screen the exact root before accepting either preparation-manifest
    # schema; Get-ResearchSnapshot repeats this at every restoration boundary.
    Assert-OnlyDefaultDataStream $root 'research root'
    $canonicalRepositoryRoot = [IO.Path]::GetFullPath(
        (Get-Item -LiteralPath $repositoryRoot -Force -ErrorAction Stop).FullName
    ).TrimEnd('\', '/')
    $rootIdentity = Get-PhysicalPathIdentity $root 'research root'
    $repositoryIdentity =
        Get-PhysicalPathIdentity $canonicalRepositoryRoot 'repository root'
    if ((Test-PathAtOrBelow $root $canonicalRepositoryRoot) -or
        (Test-PathAtOrBelow $canonicalRepositoryRoot $root) -or
        (Test-PhysicalPathAtOrBelow $rootIdentity $repositoryIdentity) -or
        (Test-PhysicalPathAtOrBelow $repositoryIdentity $rootIdentity) -or
        $root -match '(?i)(?:^|[\\/])steamapps(?:[\\/]|$)') {
        throw 'Research root must be disjoint from repository and Steam libraries.'
    }
    foreach ($steamRoot in @(Get-KnownSteamRoots)) {
        $steamIdentity = Get-PhysicalPathIdentity $steamRoot 'Steam library root'
        if ((Test-PathAtOrBelow $root $steamRoot) -or
            (Test-PathAtOrBelow $steamRoot $root) -or
            (Test-PhysicalPathAtOrBelow $rootIdentity $steamIdentity) -or
            (Test-PhysicalPathAtOrBelow $steamIdentity $rootIdentity)) {
            throw 'Research root overlaps a configured Steam library.'
        }
    }
    $boundedResearchItems = @(Get-BoundedItems $root)
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
    $preparationManifest = Assert-ResearchPreparationManifest `
        $root $boundedResearchItems
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
    return [pscustomobject]@{
        Root = $root
        Client = $client
        Server = $server
        PreparationManifestSchema = $preparationManifest.Schema
        ExternalTargetProfile = $preparationManifest.ExternalTargetProfile
        ExternalTargetCount = [Int64]$preparationManifest.ExternalTargetCount
    }
}

function Test-IsElevatedAdministrator {
    if ($env:OS -cne 'Windows_NT') { return $false }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        return $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
    } finally {
        $identity.Dispose()
    }
}

function Resolve-TrustedRepositoryTool {
    param([string]$Path, [string]$ExpectedName, [string]$Label)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw "$Label path is required." }
    $tool = [IO.Path]::GetFullPath($Path)
    Assert-PathBelowRoot $tool $repositoryRoot $Label
    Assert-NoReparsePointInExistingPath $tool $Label
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf) -or
        [IO.Path]::GetFileName($tool) -cne $ExpectedName) {
        throw "$Label must name the canonical repository-built $ExpectedName."
    }
    Assert-OnlyDefaultDataStream $tool $Label
    Assert-NoHardLink $tool $Label
    return $tool
}

function Resolve-AppManifest {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'AppManifestPath is required for active environment validation.'
    }
    $manifest = [IO.Path]::GetFullPath($Path)
    Assert-NoReparsePointInExistingPath $manifest 'Steam App 70 manifest'
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf) -or
        [IO.Path]::GetFileName($manifest) -cne 'appmanifest_70.acf') {
        throw 'AppManifestPath must name appmanifest_70.acf.'
    }
    Assert-OnlyDefaultDataStream $manifest 'Steam App 70 manifest'
    Assert-NoHardLink $manifest 'Steam App 70 manifest'
    $item = Get-Item -LiteralPath $manifest -Force
    if ($item.Length -lt 1 -or $item.Length -gt $maximumSteamManifestBytes) {
        throw 'Steam App 70 manifest length is outside its bound.'
    }
    return $manifest
}

function ConvertTo-WindowsCommandLineArgument {
    param([string]$Value)
    if ($Value.Length -ne 0 -and $Value -cnotmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * (($slashes * 2) + 1)))
            [void]$builder.Append('"')
            $slashes = 0
            continue
        }
        if ($slashes -ne 0) {
            [void]$builder.Append(('\' * $slashes))
            $slashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($slashes -ne 0) { [void]$builder.Append(('\' * ($slashes * 2))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Initialize-OrchestratorCapabilityNative {
    if ($null -ne ('Hlclient.StockRuntimeOrchestratorCapability' -as [type])) {
        return
    }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Hlclient
{
    [StructLayout(LayoutKind.Sequential)]
    public struct StockRuntimeSecurityAttributes
    {
        public int Length;
        public IntPtr SecurityDescriptor;
        [MarshalAs(UnmanagedType.Bool)] public bool InheritHandle;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct StockRuntimeJobAccounting
    {
        public long TotalUserTime;
        public long TotalKernelTime;
        public long ThisPeriodTotalUserTime;
        public long ThisPeriodTotalKernelTime;
        public uint TotalPageFaultCount;
        public uint TotalProcesses;
        public uint ActiveProcesses;
        public uint TotalTerminatedProcesses;
    }

    public static class StockRuntimeOrchestratorCapability
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr CreateEvent(
            ref StockRuntimeSecurityAttributes attributes,
            [MarshalAs(UnmanagedType.Bool)] bool manualReset,
            [MarshalAs(UnmanagedType.Bool)] bool initialState,
            string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SetEvent(IntPtr handle);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr CreateJobObject(
            ref StockRuntimeSecurityAttributes attributes, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool TerminateJobObject(
            IntPtr job, uint exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool QueryInformationJobObject(
            IntPtr job, int informationClass,
            out StockRuntimeJobAccounting information,
            uint informationLength, IntPtr returnLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CloseHandle(IntPtr handle);
    }
}
'@
}

function New-OrchestratorTransactionCapability {
    Initialize-OrchestratorCapabilityNative
    $security = [Hlclient.StockRuntimeSecurityAttributes]::new()
    $security.Length = [Runtime.InteropServices.Marshal]::SizeOf(
        [type][Hlclient.StockRuntimeSecurityAttributes])
    $security.SecurityDescriptor = [IntPtr]::Zero
    $security.InheritHandle = $true
    $handle = [Hlclient.StockRuntimeOrchestratorCapability]::CreateEvent(
        [ref]$security, $true, $false, $null)
    if ($handle -eq [IntPtr]::Zero) {
        $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Wrapper transaction capability creation failed (Win32 $nativeError)."
    }
    return $handle
}

function New-OrchestratorProcessJobCapability {
    Initialize-OrchestratorCapabilityNative
    $security = [Hlclient.StockRuntimeSecurityAttributes]::new()
    $security.Length = [Runtime.InteropServices.Marshal]::SizeOf(
        [type][Hlclient.StockRuntimeSecurityAttributes])
    $security.SecurityDescriptor = [IntPtr]::Zero
    $security.InheritHandle = $true
    $handle = [Hlclient.StockRuntimeOrchestratorCapability]::CreateJobObject(
        [ref]$security, $null)
    if ($handle -eq [IntPtr]::Zero) {
        $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Wrapper process Job creation failed (Win32 $nativeError)."
    }
    return $handle
}

function Confirm-OrchestratorProcessJobCleanup {
    param(
        [IntPtr]$JobHandle,
        [int]$TimeoutMilliseconds = 10000,
        [int]$GracePeriodMilliseconds = 0
    )
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $terminationRequested = $false
    while ($true) {
        $accounting = [Hlclient.StockRuntimeJobAccounting]::new()
        $accountingSize = [Runtime.InteropServices.Marshal]::SizeOf(
            [type][Hlclient.StockRuntimeJobAccounting])
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::QueryInformationJobObject(
                $JobHandle, 1, [ref]$accounting, [uint32]$accountingSize,
                [IntPtr]::Zero)) {
            $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "Wrapper process Job accounting failed (Win32 $nativeError)."
        }
        if ($accounting.ActiveProcesses -eq 0) { return $true }
        if (-not $terminationRequested -and
            $clock.ElapsedMilliseconds -ge $GracePeriodMilliseconds) {
            if (-not [Hlclient.StockRuntimeOrchestratorCapability]::TerminateJobObject(
                    $JobHandle, 120)) {
                $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                throw "Wrapper process Job termination failed (Win32 $nativeError)."
            }
            $terminationRequested = $true
        }
        if ($clock.ElapsedMilliseconds -ge $TimeoutMilliseconds) {
            throw 'Wrapper process Job did not reach exact zero-process accounting.'
        }
        Start-Sleep -Milliseconds 10
    }
}

function Invoke-BoundedOrchestrator {
    param(
        [string]$Path,
        [string[]]$Arguments,
        [int]$TimeoutSeconds,
        [IntPtr]$CapabilityHandle = [IntPtr]::Zero,
        [IntPtr]$CleanupCapabilityHandle = [IntPtr]::Zero,
        [IntPtr]$JobHandle = [IntPtr]::Zero,
        [IntPtr]$GuardJobHandle = [IntPtr]::Zero,
        [IntPtr]$IsolationReleaseHandle = [IntPtr]::Zero,
        [object]$ExactExitState = $null
    )
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Path
    $start.Arguments = (@($Arguments | ForEach-Object {
                ConvertTo-WindowsCommandLineArgument ([string]$_)
            }) -join ' ')
    $start.WorkingDirectory = $repositoryRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    $result = $null
    $invocationError = $null
    $cleanupFailure = $null
    try {
        if (-not $process.Start()) { throw 'Orchestrator process did not start.' }
        $started = $true
        if ($null -ne $ExactExitState) { $ExactExitState.Started = $true }
        if ($CapabilityHandle -ne [IntPtr]::Zero -and
            [Hlclient.StockRuntimeOrchestratorCapability]::WaitForSingleObject(
                $CapabilityHandle, 5000) -ne 0) {
            throw 'Project orchestrator did not attest the inherited wrapper transaction capability.'
        }
        # Read both redirected streams concurrently into fixed-size chunks.
        # ReadToEndAsync would eventually reject oversized output but could
        # buffer it without a bound first.  These builders never admit more
        # than 64 KiB per stream; finally kills this exact process on overflow
        # or timeout, which closes its owned Job boundary in active mode.
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $stdoutBuilder = [Text.StringBuilder]::new()
        $stderrBuilder = [Text.StringBuilder]::new()
        $stdoutBuffer = [char[]]::new(2048)
        $stderrBuffer = [char[]]::new(2048)
        [Int64]$stdoutBytes = 0
        [Int64]$stderrBytes = 0
        $stdoutDone = $false
        $stderrDone = $false
        $stdoutTask = $process.StandardOutput.ReadAsync(
            $stdoutBuffer, 0, $stdoutBuffer.Length)
        $stderrTask = $process.StandardError.ReadAsync(
            $stderrBuffer, 0, $stderrBuffer.Length)
        while (-not ($stdoutDone -and $stderrDone)) {
            [Int64]$remainingMilliseconds =
                ([Int64]$TimeoutSeconds * 1000) - $clock.ElapsedMilliseconds
            if ($remainingMilliseconds -le 0) {
                throw 'Project orchestrator exceeded its bounded deadline.'
            }
            $pendingReads = [Collections.Generic.List[Threading.Tasks.Task]]::new()
            if (-not $stdoutDone) { [void]$pendingReads.Add($stdoutTask) }
            if (-not $stderrDone) { [void]$pendingReads.Add($stderrTask) }
            $waitMilliseconds = [Math]::Min(100, [int]$remainingMilliseconds)
            [void][Threading.Tasks.Task]::WaitAny(
                $pendingReads.ToArray(), $waitMilliseconds)

            if (-not $stdoutDone -and $stdoutTask.IsCompleted) {
                $read = $stdoutTask.GetAwaiter().GetResult()
                if ($read -eq 0) {
                    $stdoutDone = $true
                } else {
                    $chunk = [string]::new($stdoutBuffer, 0, $read)
                    $chunkBytes = [Text.Encoding]::UTF8.GetByteCount($chunk)
                    if ($stdoutBytes -gt (65536 - $chunkBytes)) {
                        throw 'Project orchestrator stdout exceeded its byte bound.'
                    }
                    $stdoutBytes += $chunkBytes
                    [void]$stdoutBuilder.Append($chunk)
                    $stdoutTask = $process.StandardOutput.ReadAsync(
                        $stdoutBuffer, 0, $stdoutBuffer.Length)
                }
            }
            if (-not $stderrDone -and $stderrTask.IsCompleted) {
                $read = $stderrTask.GetAwaiter().GetResult()
                if ($read -eq 0) {
                    $stderrDone = $true
                } else {
                    $chunk = [string]::new($stderrBuffer, 0, $read)
                    $chunkBytes = [Text.Encoding]::UTF8.GetByteCount($chunk)
                    if ($stderrBytes -gt (65536 - $chunkBytes)) {
                        throw 'Project orchestrator stderr exceeded its byte bound.'
                    }
                    $stderrBytes += $chunkBytes
                    [void]$stderrBuilder.Append($chunk)
                    $stderrTask = $process.StandardError.ReadAsync(
                        $stderrBuffer, 0, $stderrBuffer.Length)
                }
            }
        }
        [Int64]$remainingForExit =
            ([Int64]$TimeoutSeconds * 1000) - $clock.ElapsedMilliseconds
        if ($remainingForExit -le 0 -or
            -not $process.WaitForExit([int]$remainingForExit)) {
            throw 'Project orchestrator exceeded its bounded deadline.'
        }
        $stdout = $stdoutBuilder.ToString()
        $stderr = $stderrBuilder.ToString()
        $stdoutLines = @($stdout -split '\r?\n' | Where-Object { $_.Length -ne 0 })
        $stderrLines = @($stderr -split '\r?\n' | Where-Object { $_.Length -ne 0 })
        if ($stdoutLines.Count -gt 128 -or $stderrLines.Count -gt 128 -or
            @($stdoutLines + $stderrLines | Where-Object { $_.Length -gt 1024 }).Count -ne 0) {
            throw 'Project orchestrator output exceeded its line bound.'
        }
        $allowedKeys = @(
            'active-environment', 'isolation-canary', 'binary-profile',
            'stock-processes-started', 'capture-files-written', 'result',
            'orchestrator', 'failure-category', 'processes-started',
            'relay-ready', 'server-ready', 'client-ready', 'job-cleanup',
            'persistent-rules', 'ipv4-loopback', 'ipv6-loopback',
            'non-loopback-canary', 'isolation-cleanup', 'client-file-version',
            'client-signature', 'server-launcher-version', 'server-signature',
            'steam-app-id', 'steam-build-id', 'server-engine-version',
            'protocol', 'server-build', 'unexpected-children',
            'bounded-transport-complete', 'run-id', 'journal-entries',
            'raw-datagrams', 'sequenced-c2s', 'sequenced-s2c', 'duration-ms',
            'preflight-schema', 'elevation-status', 'app-manifest',
            'wfp-session', 'timestamp-category', 'connection-generations',
            'generation-distinct', 'candidate-conflict')
        $values = [Collections.Generic.Dictionary[string, string]]::new(
            [StringComparer]::Ordinal)
        foreach ($line in $stdoutLines) {
            if ($line -cnotmatch '^\[stock-runtime-orchestrator\] (?<key>[a-z0-9-]+)=(?<value>[A-Za-z0-9_.:/-]{1,128})$') {
                throw 'Project orchestrator emitted a non-contract stdout line.'
            }
            $key = $Matches.key
            if ($allowedKeys -cnotcontains $key -or $values.ContainsKey($key)) {
                throw 'Project orchestrator emitted an unknown or duplicate status key.'
            }
            $values.Add($key, $Matches.value)
        }
        $result = [pscustomobject]@{
            ExitCode = $process.ExitCode
            Values = $values
            StderrLineCount = $stderrLines.Count
        }
    } catch {
        $invocationError = $_
    } finally {
        # A pipeline stop/Ctrl+C can interrupt WaitForExit before the C++
        # boundary returns.  Terminating that exact owned orchestrator closes
        # its kill-on-close Job Object, which in turn stops only its verified
        # relay/guard/stock children.  Process.Dispose alone does not stop a
        # still-running child.
        if ($started) {
            try {
                if (-not $process.HasExited) {
                    try { $process.Kill() }
                    catch {
                        if (-not $process.HasExited) { throw }
                    }
                }
                if (-not $process.WaitForExit(5000) -or -not $process.HasExited) {
                    throw 'Exact orchestrator process exit was not confirmed after termination.'
                }
                if ($null -ne $ExactExitState) {
                    $ExactExitState.ExitConfirmed = $true
                    $ExactExitState.ExitCode = $process.ExitCode
                    $ExactExitState.NoOrchestratorProcessCreated = $false
                }
                if ($CleanupCapabilityHandle -ne [IntPtr]::Zero) {
                    $cleanupWait =
                        [Hlclient.StockRuntimeOrchestratorCapability]::WaitForSingleObject(
                            $CleanupCapabilityHandle, 0)
                    if ($cleanupWait -eq 0) {
                        if ($null -ne $ExactExitState) {
                            $ExactExitState.CleanupSignaled = $true
                        }
                    } elseif ($cleanupWait -ne 258) {
                        throw "Wrapper cleanup capability query failed (wait result $cleanupWait)."
                    }
                }
                if ($JobHandle -ne [IntPtr]::Zero) {
                    [void](Confirm-OrchestratorProcessJobCleanup $JobHandle)
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.CampaignJobCleanupConfirmed = $true
                    }
                }
                if ($IsolationReleaseHandle -ne [IntPtr]::Zero) {
                    if (-not [Hlclient.StockRuntimeOrchestratorCapability]::SetEvent(
                            $IsolationReleaseHandle)) {
                        $nativeError =
                            [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                        throw "Isolation guard release signal failed (Win32 $nativeError)."
                    }
                }
                if ($GuardJobHandle -ne [IntPtr]::Zero) {
                    [void](Confirm-OrchestratorProcessJobCleanup `
                        $GuardJobHandle 10000 5000)
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.GuardJobCleanupConfirmed = $true
                    }
                }
                if ($null -ne $ExactExitState -and
                    $JobHandle -ne [IntPtr]::Zero -and
                    $GuardJobHandle -ne [IntPtr]::Zero -and
                    $IsolationReleaseHandle -ne [IntPtr]::Zero) {
                    $ExactExitState.JobCleanupConfirmed = $true
                }
            } catch {
                $cleanupFailure = $_
                if ($null -ne $ExactExitState) {
                    $ExactExitState.Failure = $_.Exception.Message
                }
            }
        } else {
            # Process.Start failed before returning a process handle. No
            # orchestrator exists to signal cleanup, but the wrapper still
            # owns both exact Jobs and must prove them empty before allowing
            # restoration or releasing the isolation guard capability.
            try {
                if ($null -ne $ExactExitState) {
                    $ExactExitState.ExitConfirmed = $true
                    $ExactExitState.ExitCode = $null
                    $ExactExitState.NoOrchestratorProcessCreated = $true
                    $ExactExitState.Failure =
                        'orchestrator-process-not-created'
                }
                if ($JobHandle -ne [IntPtr]::Zero) {
                    [void](Confirm-OrchestratorProcessJobCleanup $JobHandle)
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.CampaignJobCleanupConfirmed = $true
                    }
                }
                if ($IsolationReleaseHandle -ne [IntPtr]::Zero -and
                    -not [Hlclient.StockRuntimeOrchestratorCapability]::SetEvent(
                        $IsolationReleaseHandle)) {
                    $nativeError =
                        [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                    throw "Isolation guard release signal failed (Win32 $nativeError)."
                }
                if ($GuardJobHandle -ne [IntPtr]::Zero) {
                    [void](Confirm-OrchestratorProcessJobCleanup `
                        $GuardJobHandle 10000 5000)
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.GuardJobCleanupConfirmed = $true
                    }
                }
                if ($null -ne $ExactExitState -and
                    $JobHandle -ne [IntPtr]::Zero -and
                    $GuardJobHandle -ne [IntPtr]::Zero -and
                    $IsolationReleaseHandle -ne [IntPtr]::Zero) {
                    $ExactExitState.JobCleanupConfirmed = $true
                }
            } catch {
                $cleanupFailure = $_
                if ($null -ne $ExactExitState) {
                    $ExactExitState.Failure = $_.Exception.Message
                }
            }
        }
        $process.Dispose()
    }
    if ($null -ne $cleanupFailure) {
        if ($null -ne $invocationError) {
            throw ($invocationError.Exception.Message + '; ' +
                $cleanupFailure.Exception.Message)
        }
        throw $cleanupFailure
    }
    if ($null -ne $invocationError) { throw $invocationError }
    return $result
}

function Assert-OrchestratorValue {
    param([object]$Result, [string]$Name, [string]$Expected)
    if (-not $Result.Values.ContainsKey($Name) -or
        $Result.Values[$Name] -cne $Expected) {
        throw "Project orchestrator did not attest $Name=$Expected."
    }
}

function Get-ExternalSteamStateSnapshot {
    param([string]$ManifestPath)
    $records = [Collections.Generic.List[string]]::new()
    $manifestItem = Get-Item -LiteralPath $ManifestPath -Force
    [void]$records.Add(('manifest|{0}|{1}|{2}|{3}|{4}' -f $manifestItem.Length,
            $manifestItem.CreationTimeUtc.Ticks, $manifestItem.LastWriteTimeUtc.Ticks,
            [Int64]$manifestItem.Attributes, (Get-FileSha256 $ManifestPath)))

    $steamApps = Split-Path -Parent $ManifestPath
    $steamRoot = Split-Path -Parent $steamApps
    $targets = [Collections.Generic.List[object]]::new()
    $primary = Join-Path $steamApps 'common\Half-Life'
    if (Test-Path -LiteralPath $primary -PathType Container) {
        [void]$targets.Add([pscustomobject]@{
                Category = 'primary-half-life-root'; Path = $primary; Recursive = $false })
    }
    $userdata = Join-Path $steamRoot 'userdata'
    if (Test-Path -LiteralPath $userdata -PathType Container) {
        Assert-NoReparsePointInExistingPath $userdata 'Steam userdata root'
        foreach ($account in @(Get-ChildItem -LiteralPath $userdata -Force -Directory |
                Sort-Object Name)) {
            if ($account.Name -cnotmatch '^[0-9]{1,20}$' -or
                ($account.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
            foreach ($relative in @('70', 'config')) {
                $candidate = Join-Path $account.FullName $relative
                if (Test-Path -LiteralPath $candidate -PathType Container) {
                    [void]$targets.Add([pscustomobject]@{
                            Category = 'userdata-' + $relative; Path = $candidate; Recursive = $true })
                }
            }
        }
    }

    $entryCount = 1
    [Int64]$totalBytes = $manifestItem.Length
    foreach ($target in @($targets)) {
        Assert-NoReparsePointInExistingPath $target.Path 'external Steam state'
        $rootItem = Get-Item -LiteralPath $target.Path -Force
        [void]$records.Add(('{0}|root|{1}|{2}|{3}' -f $target.Category,
                $rootItem.CreationTimeUtc.Ticks, $rootItem.LastWriteTimeUtc.Ticks,
                [Int64]$rootItem.Attributes))
        $entryCount++
        if (-not $target.Recursive) { continue }
        $queue = [Collections.Generic.Queue[IO.DirectoryInfo]]::new()
        $queue.Enqueue([IO.DirectoryInfo]$rootItem)
        while ($queue.Count -ne 0) {
            $directory = $queue.Dequeue()
            foreach ($item in @($directory.GetFileSystemInfos() | Sort-Object Name)) {
                if ($entryCount -ge 50000) { throw 'External Steam state exceeds its entry bound.' }
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                    throw 'External Steam state contains a reparse point.'
                }
                $prefix = $target.Path.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
                $relative = $item.FullName.Substring($prefix.Length).Replace('\', '/')
                if (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                    [void]$records.Add(('{0}|d|{1}|{2}|{3}|{4}' -f $target.Category,
                            $relative, $item.CreationTimeUtc.Ticks,
                            $item.LastWriteTimeUtc.Ticks, [Int64]$item.Attributes))
                    $queue.Enqueue([IO.DirectoryInfo]$item)
                } else {
                    Assert-OnlyDefaultDataStream $item.FullName 'external Steam file'
                    if ($item.Length -lt 0 -or $totalBytes -gt ([Int64]1073741824 - $item.Length)) {
                        throw 'External Steam state exceeds its byte bound.'
                    }
                    $totalBytes += $item.Length
                    [void]$records.Add(('{0}|f|{1}|{2}|{3}|{4}|{5}|{6}' -f
                            $target.Category, $relative, $item.Length,
                            $item.CreationTimeUtc.Ticks, $item.LastWriteTimeUtc.Ticks,
                            [Int64]$item.Attributes, (Get-FileSha256 $item.FullName)))
                }
                $entryCount++
            }
        }
    }
    $canonical = @($records | Sort-Object) -join "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($canonical)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-', '')
    } finally {
        $algorithm.Dispose()
    }
    return [pscustomobject]@{
        EntryCount = $entryCount; TotalBytes = $totalBytes; ManifestSha256 = $digest
    }
}

function Write-AtomicJsonNoOverwrite {
    param(
        [string]$Path,
        [object]$Value,
        [string]$Label,
        [object]$DirectoryCapability)
    if (Test-Path -LiteralPath $Path) { throw "$Label already exists." }
    $parent = Split-Path -Parent $Path
    Assert-RunDirectoryCapability $DirectoryCapability $parent
    Assert-NoReparsePointInExistingPath $parent "$Label parent"
    $json = ($Value | ConvertTo-Json -Depth 8) + "`r`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json)
    $DirectoryCapability.PublishNewFile(
        [IO.Path]::GetFileName($Path), $bytes)
    Assert-RunDirectoryCapability $DirectoryCapability $parent
    Assert-NoReparsePoint $Path $Label
    Assert-NoHardLink $Path $Label
}

function Write-AtomicJsonBatchNoOverwrite {
    param(
        [object[]]$Publications,
        [object]$DirectoryCapability)
    if ($null -eq $Publications -or $Publications.Count -lt 1 -or
        $Publications.Count -gt 16) {
        throw 'Atomic JSON publication batch shape is invalid.'
    }
    $parent = $null
    [string[]]$leaves = [string[]]::new($Publications.Count)
    [byte[][]]$payloads = [byte[][]]::new($Publications.Count)
    for ($index = 0; $index -lt $Publications.Count; $index++) {
        $entry = $Publications[$index]
        if ($null -eq $entry -or [string]::IsNullOrEmpty([string]$entry.Path) -or
            [string]::IsNullOrEmpty([string]$entry.Label)) {
            throw 'Atomic JSON publication batch entry is invalid.'
        }
        $entryParent = Split-Path -Parent ([string]$entry.Path)
        if ($null -eq $parent) { $parent = $entryParent }
        elseif (-not $parent.Equals(
                $entryParent, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Atomic JSON publication batch spans multiple directories.'
        }
        if (Test-Path -LiteralPath ([string]$entry.Path)) {
            throw "$($entry.Label) already exists."
        }
        $leaves[$index] = [IO.Path]::GetFileName([string]$entry.Path)
        $bytesProperty = $entry.PSObject.Properties['Bytes']
        if ($null -ne $bytesProperty) {
            $payloads[$index] = [byte[]]$bytesProperty.Value
        } else {
            $json = ($entry.Value | ConvertTo-Json -Depth 8) + "`r`n"
            $payloads[$index] = [Text.UTF8Encoding]::new($false).GetBytes($json)
        }
    }
    Assert-RunDirectoryCapability $DirectoryCapability $parent
    Assert-NoReparsePointInExistingPath $parent 'atomic JSON batch parent'
    # PublishNewFiles performs every identity, hard-link, size and exact-byte
    # check while all file handles are retained. Its successful return is the
    # commit point; do not introduce a fallible name-based check afterward.
    $DirectoryCapability.PublishNewFiles($leaves, $payloads)
}

function Publish-AcceptedEvidenceTransaction {
    param(
        [string]$RunRoot,
        [object]$Version,
        [byte[]]$VersionBytes,
        [object]$Isolation,
        [byte[]]$IsolationBytes,
        [object]$Restoration,
        [byte[]]$RestorationBytes,
        [object]$ReconnectObservation,
        [object]$RunManifest,
        [bool]$OwnedJobsExact,
        [bool]$RestorationExact,
        [bool]$ExternalStateExact,
        [bool]$CheckerWalkerReady,
        [string]$FailureCategory,
        [object]$DirectoryCapability)
    if (-not $OwnedJobsExact -or -not $RestorationExact -or
        -not $ExternalStateExact -or -not $CheckerWalkerReady -or
        $FailureCategory -cne 'none') {
        throw 'Final evidence publication gates are incomplete.'
    }
    if ($null -eq $Version -or $null -eq $Isolation -or
        $null -eq $Restoration -or $null -eq $RunManifest -or
        $null -eq $VersionBytes -or $VersionBytes.Length -eq 0 -or
        $null -eq $IsolationBytes -or $IsolationBytes.Length -eq 0 -or
        $null -eq $RestorationBytes -or $RestorationBytes.Length -eq 0 -or
        -not [bool]$RunManifest.accepted_transport_run -or
        -not [bool]$RunManifest.accepted_evidence_run -or
        [string]$RunManifest.failure_category -cne 'none') {
        throw 'Accepted evidence publication payload is invalid.'
    }
    $reconnectRun = [string]$RunManifest.scenario -ceq 'reconnect'
    if ($reconnectRun -ne ($null -ne $ReconnectObservation)) {
        throw 'Reconnect evidence payload does not match the accepted scenario.'
    }
    # The accepted manifest is deliberately the last member. The native held-
    # handle batch publishes the scenario-specific set or rolls every renamed
    # member back before returning an error.
    $publications = [Collections.Generic.List[object]]::new()
    [void]$publications.Add(
        [pscustomobject]@{
            Path = Join-Path $RunRoot 'version-observation.json'
            Bytes = $VersionBytes
            Label = 'final version observation'
        })
    [void]$publications.Add(
        [pscustomobject]@{
            Path = Join-Path $RunRoot 'isolation-attestation.json'
            Bytes = $IsolationBytes
            Label = 'final isolation attestation'
        })
    [void]$publications.Add(
        [pscustomobject]@{
            Path = Join-Path $RunRoot 'restoration-attestation.json'
            Bytes = $RestorationBytes
            Label = 'final restoration attestation'
        })
    if ($reconnectRun) {
        [void]$publications.Add(
            [pscustomobject]@{
                Path = Join-Path $RunRoot 'reconnect-observation.json'
                Value = $ReconnectObservation
                Label = 'final reconnect observation'
            })
    }
    # The accepted run manifest is always the final member and therefore the
    # externally visible transaction commit point for both scenario shapes.
    [void]$publications.Add(
        [pscustomobject]@{
            Path = Join-Path $RunRoot 'research-run-metadata.json'
            Value = $RunManifest
            Label = 'accepted research run manifest'
        })
    Write-AtomicJsonBatchNoOverwrite -Publications $publications.ToArray() `
        -DirectoryCapability $DirectoryCapability
}

function ConvertTo-RejectedRunManifest {
    param([object]$RunManifest, [string]$FailureCategory)
    if ($null -eq $RunManifest -or
        [string]::IsNullOrEmpty($FailureCategory) -or
        $FailureCategory -ceq 'none') {
        throw 'Rejected run manifest requires a typed failure category.'
    }
    $rejected = [ordered]@{}
    foreach ($key in $RunManifest.Keys) {
        $rejected[$key] = $RunManifest[$key]
    }
    foreach ($status in @(
            'isolation_status', 'process_ownership_status',
            'version_profile_status', 'relay_status', 'client_ready_status',
            'restoration_status', 'external_drift_status',
            'offline_replay_status', 'post_resource_boundary_status',
            'first_observation_status')) {
        $rejected[$status] = 'not-accepted'
    }
    $rejected['accepted_transport_run'] = $false
    $rejected['accepted_evidence_run'] = $false
    $rejected['failure_category'] = $FailureCategory
    foreach ($reconnectOnly in @(
            'connection_generation_count', 'exact_boundary_count',
            'runtime_candidate_count', 'generation_distinct',
            'candidate_conflict')) {
        if ($rejected.Contains($reconnectOnly)) {
            $rejected.Remove($reconnectOnly)
        }
    }
    return $rejected
}

function Write-RejectedManifestAfterEvidencePublicationFailure {
    param(
        [string]$RunRoot,
        [object]$RunManifest,
        [object]$DirectoryCapability)
    Assert-RunDirectoryCapability $DirectoryCapability $RunRoot
    foreach ($finalLeaf in @(
            'version-observation.json', 'isolation-attestation.json',
            'restoration-attestation.json', 'reconnect-observation.json',
            'research-run-metadata.json')) {
        if (Test-Path -LiteralPath (Join-Path $RunRoot $finalLeaf)) {
            throw 'Accepted evidence batch rollback did not leave every final leaf absent.'
        }
    }
    $rejectedManifest = ConvertTo-RejectedRunManifest `
        $RunManifest 'evidence_publication_failed'
    Write-AtomicJsonNoOverwrite -Path `
        (Join-Path $RunRoot 'research-run-metadata.json') `
        -Value $rejectedManifest `
        -Label 'publication-failed research run manifest' `
        -DirectoryCapability $DirectoryCapability
}

function Write-StagedRestorationAttestation {
    param(
        [string]$RunRoot,
        [object]$Before,
        [object]$After,
        [object]$ExternalBefore,
        [object]$ExternalAfter,
        [int]$ExitCode,
        [bool]$OwnedProcessesStopped,
        [object]$DirectoryCapability
    )
    $externalStatus = if ($ExternalBefore.ManifestSha256 -ceq
        $ExternalAfter.ManifestSha256) { 'none' } else { 'changed' }
    $value = [ordered]@{
        schema = 'hlclient.stock-runtime-restoration.v1'
        external_file_drift = $externalStatus
        snapshot_entry_count = $Before.EntryCount
        pre_manifest_sha256 = $Before.ManifestSha256
        post_manifest_sha256 = $After.ManifestSha256
        external_snapshot_entry_count = $ExternalBefore.EntryCount
        external_pre_manifest_sha256 = $ExternalBefore.ManifestSha256
        external_post_manifest_sha256 = $ExternalAfter.ManifestSha256
        created_files_removed = $true
        protected_paths_included = $true
        owned_processes_stopped = $OwnedProcessesStopped
        input_automation_used = $false
        input_events_injected = 0
        orchestrator_exit_code = $ExitCode
        restoration_status = $(if ($Before.ManifestSha256 -ceq $After.ManifestSha256) {
                'exact'
            } else { 'mismatch' })
    }
    Write-AtomicJsonNoOverwrite -Path `
        (Join-Path $RunRoot 'restoration-attestation.staged.json') `
        -Value $value -Label 'staged restoration attestation' `
        -DirectoryCapability $DirectoryCapability
    return $value
}

function Read-BoundedJson {
    param([string]$Path, [int]$MaximumBytes, [string]$Label)
    $full = [IO.Path]::GetFullPath($Path)
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $full)).TrimEnd('\', '/')
    $capability = $null
    try {
        # The file itself is the strict descendant anchor. Root and leaf stay
        # retained without share-delete while one native handle validates and
        # reads the exact bounded ordinary-file bytes.
        $capability = New-RetainedDirectoryCapability `
            -Path $parent -AnchorPath $full -Label $Label
        [byte[]]$bytes = $capability.ReadExistingFile(
            [IO.Path]::GetFileName($full), $MaximumBytes)
        if ($bytes.Length -lt 2) {
            throw "$Label length is outside its bound."
        }
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
        return $text | ConvertFrom-Json -ErrorAction Stop
    } catch {
        if ($_.Exception.Message -ceq "$Label length is outside its bound.") {
            throw
        }
        $failure = $_.Exception
        while ($null -ne $failure.InnerException) {
            $failure = $failure.InnerException
        }
        throw "$Label retained-handle JSON read failed: $($failure.Message)"
    } finally {
        if ($null -ne $capability) { $capability.Dispose() }
    }
}

function Read-BoundedJsonWithRetainedBytes {
    param(
        [string]$Path,
        [int]$MaximumBytes,
        [string]$Label,
        [object]$DirectoryCapability)
    $parent = Split-Path -Parent $Path
    Assert-RunDirectoryCapability $DirectoryCapability $parent
    try {
        [byte[]]$bytes = $DirectoryCapability.ReadExistingFile(
            [IO.Path]::GetFileName($Path), $MaximumBytes)
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
        $value = $text | ConvertFrom-Json
        return [pscustomobject]@{ Value = $value; Bytes = $bytes }
    } catch {
        $failure = $_.Exception
        while ($null -ne $failure.InnerException) {
            $failure = $failure.InnerException
        }
        throw "$Label retained-handle JSON read failed: $($failure.Message)"
    }
}

function Convert-PrefixedOutputToValues {
    param(
        [string[]]$Lines,
        [string]$Prefix,
        [string[]]$AllowedKeys,
        [string]$Label
    )
    if ($Lines.Count -gt 128) { throw "$Label exceeded its line bound." }
    $values = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::Ordinal)
    foreach ($line in $Lines) {
        if ($line.Length -gt 1024 -or
            $line -cnotmatch ('^' + [regex]::Escape($Prefix) +
                '(?<key>[a-z0-9-]+)=(?<value>[A-Za-z0-9_.:/-]{1,256})$')) {
            throw "$Label emitted a non-contract line."
        }
        $key = $Matches.key
        if ($AllowedKeys -cnotcontains $key -or $values.ContainsKey($key)) {
            throw "$Label emitted an unknown or duplicate key."
        }
        $values.Add($key, $Matches.value)
    }
    return $values
}

function Invoke-FirstObservationChecker {
    param([string]$CheckerPath, [string]$RunRoot)
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $CheckerPath --capture-root $RunRoot --scenario first-observation `
            --publication-stage prepublication 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($lines.Count -gt 128 -or
        [Text.Encoding]::UTF8.GetByteCount(($lines -join "`n")) -gt 65536) {
        throw 'First-observation checker output exceeded its bound.'
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Lines = $lines }
}

function Invoke-IndependentTransportWalker {
    param(
        [string]$WalkerPath,
        [string]$RunRoot,
        [Collections.Generic.Dictionary[string, string]]$CheckerValues,
        [bool]$Reconnect)
    $arguments = @(
        '-CaptureRoot', $RunRoot,
        '-BoundaryPayloadOrdinal', $CheckerValues['boundary-payload-ordinal'],
        '-BoundaryObservedOrdinal', $CheckerValues['boundary-observed-ordinal'],
        '-BoundaryDeliveryOrdinal', $CheckerValues['boundary-delivery-ordinal'],
        '-BoundaryByteOffset', $CheckerValues['boundary-byte-offset'],
        '-BoundaryBitOffset', $CheckerValues['boundary-bit-offset'],
        '-BoundarySourceSequence', $CheckerValues['boundary-source-sequence'],
        '-BoundarySourcePayloadBytes',
            $CheckerValues['boundary-source-payload-bytes'],
        '-BoundarySourcePayloadBits',
            $CheckerValues['boundary-source-payload-bits'],
        '-BoundaryNextUnconsumedBits',
            $CheckerValues['boundary-next-unconsumed-bits'],
        '-BoundaryReassembled', $CheckerValues['boundary-reassembled'],
        '-BoundaryDecompressed', $CheckerValues['boundary-decompressed'],
        '-CandidateBitWidth', $CheckerValues['candidate-bit-width'],
        '-FirstCandidate', $CheckerValues['first-candidate'])
    if ($Reconnect) {
        $arguments += @(
            '-GenerationBBoundaryPayloadOrdinal',
                $CheckerValues['generation-b-boundary-payload-ordinal'],
            '-GenerationBBoundaryObservedOrdinal',
                $CheckerValues['generation-b-boundary-observed-ordinal'],
            '-GenerationBBoundaryDeliveryOrdinal',
                $CheckerValues['generation-b-boundary-delivery-ordinal'],
            '-GenerationBBoundaryByteOffset',
                $CheckerValues['generation-b-boundary-byte-offset'],
            '-GenerationBBoundaryBitOffset',
                $CheckerValues['generation-b-boundary-bit-offset'],
            '-GenerationBBoundarySourceSequence',
                $CheckerValues['generation-b-boundary-source-sequence'],
            '-GenerationBBoundarySourcePayloadBytes',
                $CheckerValues['generation-b-boundary-source-payload-bytes'],
            '-GenerationBBoundarySourcePayloadBits',
                $CheckerValues['generation-b-boundary-source-payload-bits'],
            '-GenerationBBoundaryNextUnconsumedBits',
                $CheckerValues['generation-b-boundary-next-unconsumed-bits'],
            '-GenerationBBoundaryReassembled',
                $CheckerValues['generation-b-boundary-reassembled'],
            '-GenerationBBoundaryDecompressed',
                $CheckerValues['generation-b-boundary-decompressed'],
            '-GenerationBCandidateBitWidth',
                $CheckerValues['generation-b-candidate-bit-width'],
            '-GenerationBFirstCandidate',
                $CheckerValues['generation-b-first-candidate'])
    }
    $lines = @(& $WalkerPath @arguments 2>&1 |
        ForEach-Object { $_.ToString() })
    if ($lines.Count -gt 128 -or
        [Text.Encoding]::UTF8.GetByteCount(($lines -join "`n")) -gt 65536) {
        throw 'Independent transport walker output exceeded its bound.'
    }
    return ,$lines
}

function New-ReconnectCandidateObservation {
    param(
        [Collections.Generic.Dictionary[string, string]]$Values,
        [string]$Prefix)
    $candidate = $Values[$Prefix + 'first-candidate']
    $isPrefix = $candidate.StartsWith('bit-prefix:')
    [Int64]$numeric = [Int64]($candidate -replace '^bit-prefix:', '')
    return [ordered]@{
        observed = $true
        candidate_bit_width = [Int64]$Values[$Prefix + 'candidate-bit-width']
        numeric_candidate = $(if ($isPrefix) { $null } else { $numeric })
        bounded_bit_prefix = $(if ($isPrefix) { $numeric } else { $null })
        byte_aligned = $Values[$Prefix + 'boundary-byte-aligned'] -ceq 'true'
        body_consumed = $false
        semantic_category_assigned = $false
    }
}

function New-ReconnectFinalObservation {
    param([Collections.Generic.Dictionary[string, string]]$Values)
    $generations = [Collections.Generic.List[object]]::new()
    foreach ($identity in @(
            [pscustomobject]@{
                Label = 'a'; Ordinal = 1
                Process = 'owned_client_generation_a'
                Endpoint = 'research_client_generation_a'
                EndpointDistinct = $false; Shutdown = $true; Quiet = $true
            },
            [pscustomobject]@{
                Label = 'b'; Ordinal = 2
                Process = 'owned_client_generation_b'
                Endpoint = 'research_client_generation_b'
                EndpointDistinct = $true; Shutdown = $false; Quiet = $false
            })) {
        $prefix = 'generation-' + $identity.Label + '-'
        [void]$generations.Add([ordered]@{
            generation_ordinal = $identity.Ordinal
            profile_identity = $Values['profile']
            owned_client_process_role_identity = $identity.Process
            learned_client_endpoint_role_identity = $identity.Endpoint
            fresh_owned_client_process = $true
            learned_client_endpoint_observed = $true
            learned_client_endpoint_distinct_from_previous =
                $identity.EndpointDistinct
            first_observed_ordinal = [Int64]$Values[$prefix + 'first-observed-ordinal']
            last_observed_ordinal = [Int64]$Values[$prefix + 'last-observed-ordinal']
            connectionless_exchange_count =
                [Int64]$Values[$prefix + 'connectionless-exchanges']
            connect_observed = $true
            accept_observed = $true
            first_sequenced_packet_ordinal =
                [Int64]$Values[$prefix + 'first-sequenced-packet-ordinal']
            client_to_server_packet_count =
                [Int64]$Values[$prefix + 'client-to-server-packets']
            server_to_client_packet_count =
                [Int64]$Values[$prefix + 'server-to-client-packets']
            controlled_client_shutdown_observed = $identity.Shutdown
            retired_client_endpoint_quiet = $identity.Quiet
            exact_post_resource_boundary = [ordered]@{
                observed = $true
                replay_payload_ordinal =
                    [Int64]$Values[$prefix + 'boundary-payload-ordinal']
                corpus_observed_ordinal =
                    [Int64]$Values[$prefix + 'boundary-observed-ordinal']
                delivery_ordinal =
                    [Int64]$Values[$prefix + 'boundary-delivery-ordinal']
                byte_offset = [Int64]$Values[$prefix + 'boundary-byte-offset']
                bit_offset = [Int64]$Values[$prefix + 'boundary-bit-offset']
                source_payload_byte_count =
                    [Int64]$Values[$prefix + 'boundary-source-payload-bytes']
                source_payload_bit_count =
                    [Int64]$Values[$prefix + 'boundary-source-payload-bits']
                next_unconsumed_bit_count =
                    [Int64]$Values[$prefix + 'boundary-next-unconsumed-bits']
            }
            candidate_observation = New-ReconnectCandidateObservation `
                -Values $Values -Prefix $prefix
        })
    }
    return [ordered]@{
        schema = 'hlclient.stock-runtime-reconnect-observation.v1'
        connection_generation_count = 2
        exact_boundary_count = 2
        runtime_candidate_count = 2
        generation_distinct = $true
        candidate_conflict = $false
        guard_continuity = $true
        server_continuity = $true
        relay_continuity = $true
        cleanup_exact = $true
        restoration_exact = $true
        candidate_body_consumed = $false
        candidate_semantic_category_assigned = $false
        retired_generation_a_tail_sink = 'routing_only'
        retired_generation_a_server_tail_packet_count =
            [Int64]$Values['retired-generation-a-server-tail-packets']
        generation_b_sequenced_after_fresh_accept = $true
        generations = $generations.ToArray()
    }
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

if ($PSCmdlet.ParameterSetName -eq 'DirectoryCapabilityBootstrap') {
    Initialize-RestorationDirectoryCapabilityNative
    Write-Output '[stock-runtime-capture] directory-capability=initialized'
    Write-Output '[stock-runtime-capture] files-written=0'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] result=success'
    return
}

if ($PSCmdlet.ParameterSetName -eq 'RestorationSelfTest') {
    $systemTemporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $selfTestRoot = [IO.Path]::GetFullPath((Join-Path $systemTemporaryRoot (
        'hlclient-stock-runtime-selftest-' + [Guid]::NewGuid().ToString('N'))))
    $guard = $null
    $publicationCapability = $null
    $reconnectPublicationCapability = $null
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
        [IO.File]::WriteAllText(
            (Join-Path $testResearch $markerName), $markerText,
            [Text.Encoding]::ASCII)
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

        # A root-directory ADS is invisible to child enumeration. Prove that a
        # post-snapshot mutation is rejected by the independent restoration
        # snapshot gate, then remove only this self-test-owned stream.
        $rootAdsName = 'hlclient-restoration-root-ads-probe'
        $rootAdsRejected = $false
        try {
            [IO.File]::WriteAllText(
                ($testResearch + ':' + $rootAdsName), 'mutation',
                [Text.Encoding]::ASCII)
            try { [void](Get-ResearchSnapshot $testResearch) }
            catch {
                if ($_.Exception.Message -cmatch
                    '^research snapshot root must contain only its default data stream\.$') {
                    $rootAdsRejected = $true
                } else { throw }
            }
        } finally {
            [IO.File]::Delete($testResearch + ':' + $rootAdsName)
        }
        if (-not $rootAdsRejected) {
            throw 'Restoration self-test did not reject a research-root ADS mutation.'
        }

        $researchSwap = Join-Path $selfTestRoot 'research-swapped'
        $researchSwapBlocked = $false
        try { [IO.Directory]::Move($testResearch, $researchSwap) }
        catch { $researchSwapBlocked = $true }
        if (-not $researchSwapBlocked -or
            -not (Test-Path -LiteralPath $testResearch -PathType Container) -or
            (Test-Path -LiteralPath $researchSwap)) {
            throw 'Restoration self-test research-root swap was not blocked.'
        }
        $backupSwap = $guard.TemporaryRoot + '-swapped'
        $backupSwapBlocked = $false
        try { [IO.Directory]::Move($guard.TemporaryRoot, $backupSwap) }
        catch { $backupSwapBlocked = $true }
        if (-not $backupSwapBlocked -or
            -not (Test-Path -LiteralPath $guard.TemporaryRoot -PathType Container) -or
            (Test-Path -LiteralPath $backupSwap)) {
            throw 'Restoration self-test backup-root swap was not blocked.'
        }

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

        $publicationRun = Join-Path $selfTestRoot 'publication-run'
        [IO.Directory]::CreateDirectory(
            (Join-Path $publicationRun 'logs')) | Out-Null
        $publicationCapability = New-RunDirectoryCapability $publicationRun
        if (-not $publicationCapability.VerifyRootSubstitutionBlocked()) {
            throw 'Retained publication root substitution was not blocked.'
        }
        if (-not $publicationCapability.VerifyTemporarySubstitutionBlocked()) {
            throw 'Atomic publication temporary substitution was not blocked.'
        }
        if (-not $publicationCapability.VerifyRollbackReplacementPreserved(4) -or
            -not $publicationCapability.VerifyRollbackReplacementPreserved(5)) {
            throw 'Atomic publication rollback deleted a substituted pathname.'
        }
        $replaceLeaf = '.hlclient-replacing-publication-selftest.json'
        $replaceInitial = [Text.Encoding]::ASCII.GetBytes('{"generation":1}')
        $replaceFinal = [Text.Encoding]::ASCII.GetBytes('{"generation":2}')
        $publicationCapability.PublishNewFile($replaceLeaf, $replaceInitial)
        $publicationCapability.PublishReplacingFile(
            $replaceLeaf, $replaceInitial, $replaceFinal)
        $replaceObserved = $publicationCapability.ReadExistingFile(
            $replaceLeaf, 1024)
        if ([BitConverter]::ToString($replaceObserved) -cne
            [BitConverter]::ToString($replaceFinal)) {
            throw 'Retained-handle replacing publication changed its exact bytes.'
        }
        if (-not $publicationCapability.VerifyReplacingRollbackPreserved()) {
            throw 'Replacing publication did not preserve the prior manifest on rollback.'
        }
        if (-not $publicationCapability.VerifyReplacingExpectedPriorMismatchBlocked()) {
            throw 'Replacing publication accepted mismatched prior bytes.'
        }
        if (-not $publicationCapability.VerifyReplacingSubstitutionBlocked()) {
            throw 'Replacing publication overwrote a concurrent substituted leaf.'
        }

        $testVersion = [ordered]@{ schema = 'version-self-test' }
        $testIsolation = [ordered]@{ schema = 'isolation-self-test' }
        $testRestoration = [ordered]@{ schema = 'restoration-self-test' }
        $testVersionBytes = [Text.UTF8Encoding]::new($false).GetBytes(
            (($testVersion | ConvertTo-Json -Depth 8) + "`r`n"))
        $testIsolationBytes = [Text.UTF8Encoding]::new($false).GetBytes(
            (($testIsolation | ConvertTo-Json -Depth 8) + "`r`n"))
        $testRestorationBytes = [Text.UTF8Encoding]::new($false).GetBytes(
            (($testRestoration | ConvertTo-Json -Depth 8) + "`r`n"))
        $testManifest = [ordered]@{
            scenario = 'baseline'
            external_target_profile = 'none'
            external_target_count = 0
            accepted_transport_run = $true
            accepted_evidence_run = $true
            failure_category = 'none'
        }
        $gateCases = @(
            @{ Owned = $false; Restored = $true; External = $true; Checked = $true },
            @{ Owned = $true; Restored = $false; External = $true; Checked = $true },
            @{ Owned = $true; Restored = $true; External = $false; Checked = $true },
            @{ Owned = $true; Restored = $true; External = $true; Checked = $false })
        foreach ($gateCase in $gateCases) {
            $gateRejected = $false
            try {
                Publish-AcceptedEvidenceTransaction -RunRoot $publicationRun `
                    -Version $testVersion -VersionBytes $testVersionBytes `
                    -Isolation $testIsolation -IsolationBytes $testIsolationBytes `
                    -Restoration $testRestoration `
                    -RestorationBytes $testRestorationBytes `
                    -RunManifest $testManifest `
                    -OwnedJobsExact ([bool]$gateCase.Owned) `
                    -RestorationExact ([bool]$gateCase.Restored) `
                    -ExternalStateExact ([bool]$gateCase.External) `
                    -CheckerWalkerReady ([bool]$gateCase.Checked) `
                    -FailureCategory 'none' `
                    -DirectoryCapability $publicationCapability
            } catch {
                $gateRejected = $true
            }
            if (-not $gateRejected) {
                throw 'Final publication accepted an incomplete transaction gate.'
            }
            foreach ($finalLeaf in @(
                    'version-observation.json', 'isolation-attestation.json',
                    'restoration-attestation.json', 'research-run-metadata.json')) {
                if (Test-Path -LiteralPath (Join-Path $publicationRun $finalLeaf)) {
                    throw 'Failed transaction gate left a final evidence leaf.'
                }
            }
        }

        $startCapability = [IntPtr]::Zero
        $cleanupCapability = [IntPtr]::Zero
        $emptyCampaignJob = [IntPtr]::Zero
        $emptyGuardJob = [IntPtr]::Zero
        $emptyRelease = [IntPtr]::Zero
        try {
            $startCapability = New-OrchestratorTransactionCapability
            $cleanupCapability = New-OrchestratorTransactionCapability
            $emptyCampaignJob = New-OrchestratorProcessJobCapability
            $emptyGuardJob = New-OrchestratorProcessJobCapability
            $emptyRelease = New-OrchestratorTransactionCapability
            $startFailureState = [pscustomobject]@{
                Started = $false
                ExitConfirmed = $false
                ExitCode = $null
                NoOrchestratorProcessCreated = $false
                CleanupSignaled = $false
                CampaignJobCleanupConfirmed = $false
                GuardJobCleanupConfirmed = $false
                JobCleanupConfirmed = $false
                Failure = $null
            }
            $startFailureObserved = $false
            try {
                [void](Invoke-BoundedOrchestrator `
                    (Join-Path $selfTestRoot 'absent-orchestrator.exe') @() 1 `
                    $startCapability $cleanupCapability $emptyCampaignJob `
                    $emptyGuardJob $emptyRelease $startFailureState)
            } catch {
                $startFailureObserved = $true
            }
            if (-not $startFailureObserved -or $startFailureState.Started -or
                -not $startFailureState.ExitConfirmed -or
                -not $startFailureState.NoOrchestratorProcessCreated -or
                -not $startFailureState.CampaignJobCleanupConfirmed -or
                -not $startFailureState.GuardJobCleanupConfirmed -or
                -not $startFailureState.JobCleanupConfirmed -or
                $startFailureState.CleanupSignaled -or
                [string]$startFailureState.Failure -cne
                    'orchestrator-process-not-created' -or
                [Hlclient.StockRuntimeOrchestratorCapability]::WaitForSingleObject(
                    $emptyRelease, 0) -ne 0) {
                throw 'Orchestrator launch failure did not attest exact empty-Job cleanup.'
            }
        } finally {
            foreach ($emptyHandle in @(
                    $startCapability, $cleanupCapability, $emptyCampaignJob,
                    $emptyGuardJob, $emptyRelease)) {
                if ($emptyHandle -ne [IntPtr]::Zero) {
                    [void][Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                        $emptyHandle)
                }
            }
        }

        $collisionPath = Join-Path $publicationRun 'collision.json'
        [IO.File]::WriteAllText(
            $collisionPath, '{"owner":"external"}',
            [Text.UTF8Encoding]::new($false))
        [string[]]$rollbackLeaves = @(
            'partial-restoration.json', 'collision.json')
        [byte[][]]$rollbackPayloads = [byte[][]]::new(2)
        $rollbackPayloads[0] = [Text.Encoding]::UTF8.GetBytes(
            '{"schema":"partial-restoration"}')
        $rollbackPayloads[1] = [Text.Encoding]::UTF8.GetBytes(
            '{"owner":"wrapper"}')
        $rollbackObserved = $false
        try {
            $publicationCapability.PublishNewFiles(
                $rollbackLeaves, $rollbackPayloads)
        } catch {
            $rollbackObserved = $true
        }
        if (-not $rollbackObserved -or
            (Test-Path -LiteralPath (Join-Path $publicationRun `
                    'partial-restoration.json')) -or
            [IO.File]::ReadAllText($collisionPath) -cne '{"owner":"external"}') {
            throw 'Atomic publication failure left a partial final evidence set.'
        }
        Write-RejectedManifestAfterEvidencePublicationFailure `
            $publicationRun $testManifest $publicationCapability
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json')) {
            if (Test-Path -LiteralPath (Join-Path $publicationRun $finalLeaf)) {
                throw 'Failed accepted batch published a final evidence leaf.'
            }
        }
        $rejectedPublication = Read-BoundedJson `
            (Join-Path $publicationRun 'research-run-metadata.json') `
            65536 'publication-failed research run manifest'
        if ([bool]$rejectedPublication.accepted_transport_run -or
            [bool]$rejectedPublication.accepted_evidence_run -or
            [string]$rejectedPublication.failure_category -cne
                'evidence_publication_failed') {
            throw 'Failed accepted batch did not publish a typed rejected manifest.'
        }
        [IO.File]::Delete(
            (Join-Path $publicationRun 'research-run-metadata.json'))
        [IO.File]::Delete($collisionPath)

        Publish-AcceptedEvidenceTransaction -RunRoot $publicationRun `
            -Version $testVersion -VersionBytes $testVersionBytes `
            -Isolation $testIsolation -IsolationBytes $testIsolationBytes `
            -Restoration $testRestoration `
            -RestorationBytes $testRestorationBytes `
            -RunManifest $testManifest `
            -OwnedJobsExact $true -RestorationExact $true `
            -ExternalStateExact $true -CheckerWalkerReady $true `
            -FailureCategory 'none' `
            -DirectoryCapability $publicationCapability
        [byte[]]$retainedVersionBytes =
            $publicationCapability.ReadExistingFile(
                'version-observation.json', 65536)
        if ([Convert]::ToBase64String($retainedVersionBytes) -cne
            [Convert]::ToBase64String($testVersionBytes)) {
            throw 'Retained-handle publication read-back changed exact bytes.'
        }
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json', 'research-run-metadata.json')) {
            if (-not (Test-Path -LiteralPath `
                    (Join-Path $publicationRun $finalLeaf) -PathType Leaf)) {
                throw 'Successful baseline transaction omitted a final evidence leaf.'
            }
        }
        if (Test-Path -LiteralPath (
                Join-Path $publicationRun 'reconnect-observation.json')) {
            throw 'Successful baseline transaction published a reconnect-only leaf.'
        }
        $publicationCapability.Dispose()
        $publicationCapability = $null

        # Exercise the scenario-dependent five-member commit independently.
        # These values satisfy the exact reconnect-observation v1 shape while
        # remaining synthetic, path-free and body-unconsumed.
        $reconnectValues = [Collections.Generic.Dictionary[string, string]]::new(
            [StringComparer]::Ordinal)
        $reconnectValues.Add(
            'profile', 'stock_protocol_48_build_10210_evidence_pending')
        $reconnectValues.Add(
            'retired-generation-a-server-tail-packets', '0')
        foreach ($generation in @(
                [pscustomobject]@{ Label = 'a'; First = 0; Last = 9 },
                [pscustomobject]@{ Label = 'b'; First = 10; Last = 19 })) {
            $prefix = 'generation-' + $generation.Label + '-'
            $values = [ordered]@{
                'first-observed-ordinal' = [string]$generation.First
                'last-observed-ordinal' = [string]$generation.Last
                'connectionless-exchanges' = '2'
                'first-sequenced-packet-ordinal' =
                    [string]($generation.First + 2)
                'client-to-server-packets' = '3'
                'server-to-client-packets' = '5'
                'boundary-payload-ordinal' = '0'
                'boundary-observed-ordinal' =
                    [string]($generation.First + 4)
                'boundary-delivery-ordinal' = '3'
                'boundary-byte-offset' = '0'
                'boundary-bit-offset' = '0'
                'boundary-source-payload-bytes' = '1'
                'boundary-source-payload-bits' = '8'
                'boundary-next-unconsumed-bits' = '8'
                'boundary-byte-aligned' = 'true'
                'candidate-bit-width' = '8'
                'first-candidate' = '5'
            }
            foreach ($key in $values.Keys) {
                $reconnectValues.Add($prefix + $key, $values[$key])
            }
        }
        $testReconnectObservation =
            New-ReconnectFinalObservation -Values $reconnectValues
        $testReconnectManifest = [ordered]@{
            scenario = 'reconnect'
            external_target_profile = 'none'
            external_target_count = 0
            accepted_transport_run = $true
            accepted_evidence_run = $true
            failure_category = 'none'
            connection_generation_count = 2
            exact_boundary_count = 2
            runtime_candidate_count = 2
            generation_distinct = $true
            candidate_conflict = $false
        }
        $reconnectPublicationRun = Join-Path $selfTestRoot `
            'reconnect-publication-run'
        [IO.Directory]::CreateDirectory(
            (Join-Path $reconnectPublicationRun 'logs')) | Out-Null
        $reconnectPublicationCapability =
            New-RunDirectoryCapability $reconnectPublicationRun

        $reconnectGateRejected = $false
        try {
            Publish-AcceptedEvidenceTransaction `
                -RunRoot $reconnectPublicationRun `
                -Version $testVersion -VersionBytes $testVersionBytes `
                -Isolation $testIsolation -IsolationBytes $testIsolationBytes `
                -Restoration $testRestoration `
                -RestorationBytes $testRestorationBytes `
                -ReconnectObservation $testReconnectObservation `
                -RunManifest $testReconnectManifest `
                -OwnedJobsExact $true -RestorationExact $true `
                -ExternalStateExact $true -CheckerWalkerReady $false `
                -FailureCategory 'none' `
                -DirectoryCapability $reconnectPublicationCapability
        } catch {
            $reconnectGateRejected = $true
        }
        if (-not $reconnectGateRejected) {
            throw 'Reconnect publication accepted an incomplete transaction gate.'
        }
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json', 'reconnect-observation.json',
                'research-run-metadata.json')) {
            if (Test-Path -LiteralPath (
                    Join-Path $reconnectPublicationRun $finalLeaf)) {
                throw 'Rejected reconnect gate left a final evidence leaf.'
            }
        }

        Publish-AcceptedEvidenceTransaction `
            -RunRoot $reconnectPublicationRun `
            -Version $testVersion -VersionBytes $testVersionBytes `
            -Isolation $testIsolation -IsolationBytes $testIsolationBytes `
            -Restoration $testRestoration `
            -RestorationBytes $testRestorationBytes `
            -ReconnectObservation $testReconnectObservation `
            -RunManifest $testReconnectManifest `
            -OwnedJobsExact $true -RestorationExact $true `
            -ExternalStateExact $true -CheckerWalkerReady $true `
            -FailureCategory 'none' `
            -DirectoryCapability $reconnectPublicationCapability
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json', 'reconnect-observation.json',
                'research-run-metadata.json')) {
            if (-not (Test-Path -LiteralPath (
                        Join-Path $reconnectPublicationRun $finalLeaf) `
                    -PathType Leaf)) {
                throw 'Successful reconnect transaction omitted a final evidence leaf.'
            }
        }
        $publishedReconnect = Read-BoundedJson `
            (Join-Path $reconnectPublicationRun 'reconnect-observation.json') `
            65536 'self-test reconnect observation'
        if ([string]$publishedReconnect.schema -cne
                'hlclient.stock-runtime-reconnect-observation.v1' -or
            [Int64]$publishedReconnect.connection_generation_count -ne 2 -or
            @($publishedReconnect.generations).Count -ne 2) {
            throw 'Published reconnect observation does not retain its exact schema.'
        }
        $reconnectPublicationCapability.Dispose()
        $reconnectPublicationCapability = $null
        Write-Output '[stock-runtime-capture] hardlink-overwrite=blocked'
        Write-Output '[stock-runtime-capture] junction-traversal=blocked'
        Write-Output '[stock-runtime-capture] directory-swap=blocked'
        Write-Output '[stock-runtime-capture] publication-root-swap=blocked'
        Write-Output '[stock-runtime-capture] temporary-substitution=blocked'
        Write-Output '[stock-runtime-capture] orchestrator-start-failure-cleanup=exact'
        Write-Output '[stock-runtime-capture] failed-publication-rollback=exact'
        Write-Output '[stock-runtime-capture] rollback-replacement=preserved'
        Write-Output '[stock-runtime-capture] replacing-publication=retained-handle-exact'
        Write-Output '[stock-runtime-capture] replacing-rollback=prior-manifest-preserved'
        Write-Output '[stock-runtime-capture] replacing-prior-mismatch=blocked'
        Write-Output '[stock-runtime-capture] replacing-substitution=blocked'
        Write-Output '[stock-runtime-capture] final-evidence-batch=exact'
        Write-Output '[stock-runtime-capture] retained-handle-json-read=exact'
        Write-Output '[stock-runtime-capture] restoration-directory-identity=retained-volume-and-file-id'
        Write-Output '[stock-runtime-capture] external-sentinel-metadata=unchanged'
        Write-Output '[stock-runtime-capture] junction-target-metadata=unchanged'
        Write-Output '[stock-runtime-capture] restoration=exact'
        Write-Output '[stock-runtime-capture] result=restoration-self-test-success'
    } finally {
        if ($null -ne $reconnectPublicationCapability) {
            $reconnectPublicationCapability.Dispose()
            $reconnectPublicationCapability = $null
        }
        if ($null -ne $publicationCapability) {
            $publicationCapability.Dispose()
            $publicationCapability = $null
        }
        Close-RestorationGuardCapabilities $guard
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

if ($PSCmdlet.ParameterSetName -eq 'Preflight') {
    $research = Resolve-IsolatedResearchRoot
    [void](Get-ResearchSnapshot $research.Root)
    Write-Output ("[stock-runtime-capture] preparation-manifest={0}" -f
        $research.PreparationManifestSchema)
    Write-Output ("[stock-runtime-capture] external-target-profile={0}" -f
        $research.ExternalTargetProfile)
    Write-Output ("[stock-runtime-capture] external-target-count={0}" -f
        $research.ExternalTargetCount)
    Write-Output '[stock-runtime-capture] research-root=policy-screened-copy-physical-identity-pending'
    Write-Output '[stock-runtime-capture] client-version=1.1.1.1'
    Write-Output '[stock-runtime-capture] server-launcher-version=4.1.1.1'
    Write-Output '[stock-runtime-capture] stock-processes-started=0'
    Write-Output '[stock-runtime-capture] read-only-helper-processes-started=1'
    Write-Output '[stock-runtime-capture] files-written=0'
    Write-Output '[stock-runtime-capture] result=preflight-structural-success-isolation-evidence-pending'
    return
}

if ($PSCmdlet.ParameterSetName -eq 'ActivePreflight') {
    if (-not (Test-IsElevatedAdministrator)) {
        Write-Output '[stock-runtime-capture] active-environment=invalid'
        Write-Output '[stock-runtime-capture] failure-category=network_isolation_privilege_required'
        Write-Output '[stock-runtime-capture] stock-processes-started=0'
        Write-Output '[stock-runtime-capture] capture-files-written=0'
        throw 'Active environment validation requires an elevated PowerShell; automatic elevation is forbidden.'
    }
    try {
        $research = Resolve-IsolatedResearchRoot
    } catch {
        if ($_.Exception.Message -ceq
            'research_copy_not_evidence_eligible') {
            Write-Output '[stock-runtime-capture] active-environment=invalid'
            Write-Output '[stock-runtime-capture] failure-category=research_copy_not_evidence_eligible'
            Write-Output '[stock-runtime-capture] stock-processes-started=0'
            Write-Output '[stock-runtime-capture] capture-files-written=0'
            Write-Output '[stock-runtime-capture] wfp-sessions-started=0'
        }
        throw
    }
    [void](Get-ResearchSnapshot $research.Root)
    Write-Output ("[stock-runtime-capture] preparation-manifest={0}" -f
        $research.PreparationManifestSchema)
    Write-Output ("[stock-runtime-capture] external-target-profile={0}" -f
        $research.ExternalTargetProfile)
    Write-Output ("[stock-runtime-capture] external-target-count={0}" -f
        $research.ExternalTargetCount)
    $tool = Resolve-TrustedRepositoryTool $CaptureToolPath `
        'hlclient_stock_runtime_capture.exe' 'stock runtime relay'
    if ($ExpectedCaptureToolSha256 -and
        (Get-FileSha256 $tool) -cne $ExpectedCaptureToolSha256.ToUpperInvariant()) {
        throw 'Capture tool SHA-256 does not match the reviewed value.'
    }
    $guardPath = Resolve-TrustedRepositoryTool $NetworkIsolationGuardPath `
        'hlclient_stock_runtime_isolation_guard.exe' 'network isolation guard'
    $orchestratorPath = Resolve-TrustedRepositoryTool `
        (Join-Path (Split-Path -Parent $tool) 'hlclient_stock_runtime_orchestrator.exe') `
        'hlclient_stock_runtime_orchestrator.exe' 'stock runtime orchestrator'
    $manifestPath = Resolve-AppManifest $AppManifestPath
    [void](Get-ExternalSteamStateSnapshot $manifestPath)
    $arguments = @(
        '--validate-environment', '--research-root', $research.Root,
        '--client', $research.Client, '--server', $research.Server,
        '--relay', $tool, '--isolation-guard', $guardPath,
        '--app-manifest', $manifestPath, '--game', 'valve', '--map', 'boot_camp',
        '--relay-port', [string]$RelayPort, '--server-port', [string]$ServerPort)
    $result = Invoke-BoundedOrchestrator $orchestratorPath $arguments 90
    if ($result.ExitCode -ne 0) {
        $category = 'active_environment_validation_failed'
        if ($result.Values.ContainsKey('failure-category')) {
            $category = $result.Values['failure-category']
        }
        Write-Output '[stock-runtime-capture] active-environment=invalid'
        Write-Output "[stock-runtime-capture] failure-category=$category"
        Write-Output '[stock-runtime-capture] stock-processes-started=0'
        Write-Output '[stock-runtime-capture] capture-files-written=0'
        throw "Active environment validation failed with typed category $category."
    }
    Assert-OrchestratorValue $result active-environment valid
    Assert-OrchestratorValue $result preflight-schema `
        hlclient.stock-active-capture-preflight-attestation.v1
    Assert-OrchestratorValue $result elevation-status verified
    Assert-OrchestratorValue $result isolation-canary success
    Assert-OrchestratorValue $result binary-profile valid
    Assert-OrchestratorValue $result app-manifest valid
    Assert-OrchestratorValue $result wfp-session dynamic
    Assert-OrchestratorValue $result ipv4-loopback allowed
    if (-not $result.Values.ContainsKey('ipv6-loopback') -or
        @('allowed', 'capability-unavailable') -cnotcontains
            $result.Values['ipv6-loopback']) {
        throw 'Project orchestrator emitted an invalid IPv6 canary status.'
    }
    Assert-OrchestratorValue $result non-loopback-canary denied-os-classified
    Assert-OrchestratorValue $result isolation-cleanup exact
    Assert-OrchestratorValue $result timestamp-category current-session
    Assert-OrchestratorValue $result stock-processes-started 0
    Assert-OrchestratorValue $result capture-files-written 0
    Assert-OrchestratorValue $result result success
    Write-Output '[stock-runtime-capture] active-environment=valid'
    Write-Output '[stock-runtime-capture] preflight-schema=hlclient.stock-active-capture-preflight-attestation.v1'
    Write-Output '[stock-runtime-capture] elevation-status=verified'
    Write-Output '[stock-runtime-capture] isolation-canary=success'
    Write-Output '[stock-runtime-capture] binary-profile=valid'
    Write-Output '[stock-runtime-capture] app-manifest=valid'
    Write-Output '[stock-runtime-capture] wfp-session=dynamic'
    Write-Output '[stock-runtime-capture] ipv4-loopback=allowed'
    Write-Output ("[stock-runtime-capture] ipv6-loopback={0}" -f
        $result.Values['ipv6-loopback'])
    Write-Output '[stock-runtime-capture] non-loopback-canary=denied-os-classified'
    Write-Output '[stock-runtime-capture] isolation-cleanup=exact'
    Write-Output '[stock-runtime-capture] timestamp-category=current-session'
    Write-Output '[stock-runtime-capture] stock-processes-started=0'
    Write-Output '[stock-runtime-capture] capture-files-written=0'
    Write-Output '[stock-runtime-capture] result=success'
    return
}

$scenarioAliases = @{
    'drop-server-runtime' = 'drop-server-to-client-transport-ordinal'
    'duplicate-server-runtime' = 'duplicate-server-to-client-transport-ordinal'
    'reorder-server-runtime' = 'reorder-server-to-client-transport-ordinal'
}
$canonicalScenario = if ($scenarioAliases.ContainsKey($Scenario)) {
    $scenarioAliases[$Scenario]
} else { $Scenario }
$orchestratorScenario = switch ($canonicalScenario) {
    'drop-server-to-client-transport-ordinal' { 'drop-server-runtime'; break }
    'duplicate-server-to-client-transport-ordinal' { 'duplicate-server-runtime'; break }
    'reorder-server-to-client-transport-ordinal' { 'reorder-server-runtime'; break }
    default { $canonicalScenario }
}

if (($canonicalScenario -ceq 'baseline' -or
        $canonicalScenario -ceq 'idle-runtime') -and
    $MaximumDurationSeconds -lt 30) {
    Write-Output '[stock-runtime-capture] active-capture=blocked'
    Write-Output '[stock-runtime-capture] failure-category=minimum_observation_duration_required'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] files-written=0'
    throw 'Accepted baseline and idle-runtime observations require a requested duration of at least 30 seconds.'
}

if ($canonicalScenario -ceq 'reconnect' -and
    $MaximumDurationSeconds -lt 60) {
    Write-Output '[stock-runtime-capture] active-capture=blocked'
    Write-Output '[stock-runtime-capture] failure-category=minimum_reconnect_duration_required'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] files-written=0'
    throw 'A two-generation reconnect observation requires at least 60 seconds.'
}

if (-not (Test-IsElevatedAdministrator)) {
    Write-Output '[stock-runtime-capture] active-capture=blocked'
    Write-Output '[stock-runtime-capture] failure-category=network_isolation_privilege_required'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] files-written=0'
    Write-Output '[stock-runtime-capture] network-operations=0'
    throw 'Active capture requires an elevated PowerShell; automatic elevation is forbidden.'
}

if ($RelayPort -eq $ServerPort -or $MaximumPayloadBytes -gt $MaximumTotalRawBytes -or
    $MaximumDecompressedBytes -lt $MaximumReassembledBytes -or
    $MaximumRuntimeFrames -gt $MaximumMessageCount -or
    $MaximumClientPackets -gt $MaximumDatagrams -or
    $MaximumServerPackets -gt $MaximumDatagrams) {
    throw 'Capture limits violate cross-field policy.'
}
$activeScenarios = @(
    'baseline', 'idle-runtime', 'reconnect',
    'drop-server-to-client-transport-ordinal',
    'duplicate-server-to-client-transport-ordinal',
    'reorder-server-to-client-transport-ordinal')
if ($activeScenarios -cnotcontains $canonicalScenario) {
    throw 'The requested scenario is outside the M4.7.1.1 active-capture allowlist; no run was started.'
}
try {
    $research = Resolve-IsolatedResearchRoot
} catch {
    if ($_.Exception.Message -ceq 'research_copy_not_evidence_eligible') {
        Write-Output '[stock-runtime-capture] active-capture=blocked'
        Write-Output '[stock-runtime-capture] failure-category=research_copy_not_evidence_eligible'
        Write-Output '[stock-runtime-capture] processes-started=0'
        Write-Output '[stock-runtime-capture] stock-processes-started=0'
        Write-Output '[stock-runtime-capture] files-written=0'
        Write-Output '[stock-runtime-capture] capture-files-written=0'
        Write-Output '[stock-runtime-capture] network-operations=0'
        Write-Output '[stock-runtime-capture] wfp-sessions-started=0'
        Write-Output '[stock-runtime-capture] capture-runs-created=0'
        Write-Output '[stock-runtime-capture] restoration-backups-created=0'
    }
    throw
}
Write-Output ("[stock-runtime-capture] preparation-manifest={0}" -f
    $research.PreparationManifestSchema)
Write-Output ("[stock-runtime-capture] external-target-profile={0}" -f
    $research.ExternalTargetProfile)
Write-Output ("[stock-runtime-capture] external-target-count={0}" -f
    $research.ExternalTargetCount)
$output = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
if ($PreCampaignCanary) {
    if ($canonicalScenario -cne 'baseline' -or $Map -cne 'boot_camp' -or
        $output -ine $requiredCanaryOutputRoot) {
        throw 'PreCampaignCanary requires exact boot_camp/baseline and the repository manual-artifacts/stock-runtime-canary root.'
    }
} elseif ($output -ine $requiredOutputRoot) {
    throw 'OutputRoot must be the exact repository manual-artifacts/stock-runtime root.'
}
Assert-NoReparsePointInExistingPath $output 'stock runtime output root'
$tool = Resolve-TrustedRepositoryTool $CaptureToolPath `
    'hlclient_stock_runtime_capture.exe' 'stock runtime relay'
if ($ExpectedCaptureToolSha256 -and
    (Get-FileSha256 $tool) -cne $ExpectedCaptureToolSha256.ToUpperInvariant()) {
    throw 'Capture tool SHA-256 does not match the reviewed value.'
}
$guardPath = Resolve-TrustedRepositoryTool $NetworkIsolationGuardPath `
    'hlclient_stock_runtime_isolation_guard.exe' 'network isolation guard'
$toolDirectory = Split-Path -Parent $tool
$orchestratorPath = Resolve-TrustedRepositoryTool `
    (Join-Path $toolDirectory 'hlclient_stock_runtime_orchestrator.exe') `
    'hlclient_stock_runtime_orchestrator.exe' 'stock runtime orchestrator'
$checkerPath = Resolve-TrustedRepositoryTool `
    (Join-Path $toolDirectory 'hlclient_stock_runtime_check.exe') `
    'hlclient_stock_runtime_check.exe' 'stock runtime checker'
$manifestPath = Resolve-AppManifest $AppManifestPath
$walkerPath = Join-Path $PSScriptRoot 'walk_stock_runtime_transport.ps1'
if (-not (Test-Path -LiteralPath $walkerPath -PathType Leaf)) {
    throw 'Independent transport walker is absent.'
}

# Prove the binary profile and dynamic-isolation canary before creating a
# restoration backup or allowing the orchestrator to create a run directory.
# The active orchestrator repeats these checks inside the owned transaction;
# this first pass is the mutation-free-to-game-files environment gate.
$activeValidationArguments = @(
    '--validate-environment', '--research-root', $research.Root,
    '--client', $research.Client, '--server', $research.Server,
    '--relay', $tool, '--isolation-guard', $guardPath,
    '--app-manifest', $manifestPath, '--game', 'valve', '--map', $Map,
    '--relay-port', [string]$RelayPort, '--server-port', [string]$ServerPort)
$activeValidation = Invoke-BoundedOrchestrator $orchestratorPath `
    $activeValidationArguments 90
if ($activeValidation.ExitCode -ne 0) {
    $category = 'active_environment_validation_failed'
    if ($activeValidation.Values.ContainsKey('failure-category')) {
        $category = $activeValidation.Values['failure-category']
    }
    Write-Output '[stock-runtime-capture] active-capture=blocked'
    Write-Output "[stock-runtime-capture] failure-category=$category"
    Write-Output '[stock-runtime-capture] stock-processes-started=0'
    Write-Output '[stock-runtime-capture] capture-files-written=0'
    Write-Output '[stock-runtime-capture] restoration-backups-created=0'
    throw "Active environment validation failed before backup/run creation: $category."
}
Assert-OrchestratorValue $activeValidation active-environment valid
Assert-OrchestratorValue $activeValidation preflight-schema `
    hlclient.stock-active-capture-preflight-attestation.v1
Assert-OrchestratorValue $activeValidation elevation-status verified
Assert-OrchestratorValue $activeValidation isolation-canary success
Assert-OrchestratorValue $activeValidation binary-profile valid
Assert-OrchestratorValue $activeValidation app-manifest valid
Assert-OrchestratorValue $activeValidation wfp-session dynamic
Assert-OrchestratorValue $activeValidation ipv4-loopback allowed
if (-not $activeValidation.Values.ContainsKey('ipv6-loopback') -or
    @('allowed', 'capability-unavailable') -cnotcontains
        $activeValidation.Values['ipv6-loopback']) {
    throw 'Active preflight emitted an invalid IPv6 canary status.'
}
Assert-OrchestratorValue $activeValidation non-loopback-canary denied-os-classified
Assert-OrchestratorValue $activeValidation isolation-cleanup exact
Assert-OrchestratorValue $activeValidation timestamp-category current-session
Assert-OrchestratorValue $activeValidation stock-processes-started 0
Assert-OrchestratorValue $activeValidation capture-files-written 0
Assert-OrchestratorValue $activeValidation result success

$before = Get-ResearchSnapshot $research.Root
$externalBefore = Get-ExternalSteamStateSnapshot $manifestPath
$guard = New-RestorationGuard $research.Root $before
$runId = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $output $runId
$orchestratorResult = $null
$orchestratorExitCode = 255
$wrapperCapability = [IntPtr]::Zero
$wrapperCleanupCapability = [IntPtr]::Zero
$wrapperJob = [IntPtr]::Zero
$wrapperGuardJob = [IntPtr]::Zero
$isolationReleaseCapability = [IntPtr]::Zero
$orchestratorExitState = [pscustomobject]@{
    Started = $false
    ExitConfirmed = $false
    ExitCode = $null
    NoOrchestratorProcessCreated = $false
    CleanupSignaled = $false
    CampaignJobCleanupConfirmed = $false
    GuardJobCleanupConfirmed = $false
    JobCleanupConfirmed = $false
    Failure = $null
}
$primaryError = $null
$cleanupErrors = [Collections.Generic.List[string]]::new()
$after = $null
$externalAfter = $null
$runDirectoryCapability = $null
try {
    # This inheritable, one-use event is created only after the complete
    # restoration backup exists.  The active C++ orchestrator verifies the
    # actual parent PID and signals the first event before any environment
    # mutation or process launch.  A distinct event is signalled only after
    # typed Job cleanup reaches zero, independently of stdout parsing.
    $wrapperCapability = New-OrchestratorTransactionCapability
    $wrapperCleanupCapability = New-OrchestratorTransactionCapability
    $wrapperJob = New-OrchestratorProcessJobCapability
    $wrapperGuardJob = New-OrchestratorProcessJobCapability
    $isolationReleaseCapability = New-OrchestratorTransactionCapability
    $arguments = @(
        '--confirmation-token', $activeCaptureToken,
        '--wrapper-capability-handle', $wrapperCapability.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--wrapper-cleanup-capability-handle',
        $wrapperCleanupCapability.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--wrapper-job-handle', $wrapperJob.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--wrapper-guard-job-handle', $wrapperGuardJob.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--isolation-release-handle',
        $isolationReleaseCapability.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--wrapper-process-id', [string]$PID,
        '--run-root', $runRoot, '--research-root', $research.Root,
        '--client', $research.Client, '--server', $research.Server,
        '--relay', $tool, '--isolation-guard', $guardPath,
        '--app-manifest', $manifestPath, '--game', $Game, '--map', $Map,
        '--scenario', $orchestratorScenario, '--relay-port', [string]$RelayPort,
        '--server-port', [string]$ServerPort,
        '--max-duration-seconds', [string]$MaximumDurationSeconds,
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
        '--mutation-after-server-packets', [string]$MutationAfterServerPackets)
    if ($PreCampaignCanary) {
        $arguments += '--pre-campaign-canary'
    }
    $orchestratorResult = Invoke-BoundedOrchestrator $orchestratorPath $arguments `
        ($MaximumDurationSeconds + 90) $wrapperCapability `
        $wrapperCleanupCapability $wrapperJob $wrapperGuardJob `
        $isolationReleaseCapability $orchestratorExitState
    $orchestratorExitCode = $orchestratorResult.ExitCode
    if ($orchestratorExitCode -ne 0) {
        $category = 'orchestrator_failed'
        if ($orchestratorResult.Values.ContainsKey('failure-category')) {
            $category = $orchestratorResult.Values['failure-category']
        }
        throw "Stock runtime orchestrator failed: $category."
    }
    Assert-OrchestratorValue $orchestratorResult orchestrator success
    Assert-OrchestratorValue $orchestratorResult failure-category none
    Assert-OrchestratorValue $orchestratorResult result success
    Assert-OrchestratorValue $orchestratorResult relay-ready true
    Assert-OrchestratorValue $orchestratorResult server-ready true
    Assert-OrchestratorValue $orchestratorResult client-ready true
    Assert-OrchestratorValue $orchestratorResult job-cleanup exact
    Assert-OrchestratorValue $orchestratorResult bounded-transport-complete true
    if ($canonicalScenario -ceq 'reconnect') {
        Assert-OrchestratorValue $orchestratorResult connection-generations 2
        Assert-OrchestratorValue $orchestratorResult generation-distinct true
        Assert-OrchestratorValue $orchestratorResult candidate-conflict evidence-pending
    } else {
        Assert-OrchestratorValue $orchestratorResult connection-generations 1
        Assert-OrchestratorValue $orchestratorResult generation-distinct false
        Assert-OrchestratorValue $orchestratorResult candidate-conflict not-applicable
    }
    [Int64]$orchestratorDuration = 0
    if (-not $orchestratorResult.Values.ContainsKey('duration-ms') -or
        -not [Int64]::TryParse(
            $orchestratorResult.Values['duration-ms'], [ref]$orchestratorDuration) -or
        $orchestratorDuration -lt 0 -or
        $orchestratorDuration -gt (($MaximumDurationSeconds + 90) * 1000)) {
        throw 'Stock runtime orchestrator duration is absent or outside its bound.'
    }
    if (-not $orchestratorResult.Values.ContainsKey('processes-started') -or
        [Int64]$orchestratorResult.Values['processes-started'] -lt 2) {
        throw 'Stock runtime orchestrator did not attest its owned process count.'
    }
} catch {
    $primaryError = $_
} finally {
    if ($orchestratorExitState.ExitConfirmed) {
        $orchestratorExitCode = [int]$orchestratorExitState.ExitCode
    }
    if ($wrapperCapability -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $wrapperCapability)) {
            [void]$cleanupErrors.Add('Wrapper transaction capability close failed.')
        }
        $wrapperCapability = [IntPtr]::Zero
    }
    if ($wrapperCleanupCapability -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $wrapperCleanupCapability)) {
            [void]$cleanupErrors.Add('Wrapper cleanup capability close failed.')
        }
        $wrapperCleanupCapability = [IntPtr]::Zero
    }
    if ($wrapperJob -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $wrapperJob)) {
            [void]$cleanupErrors.Add('Wrapper process Job close failed.')
        }
        $wrapperJob = [IntPtr]::Zero
    }
    if ($wrapperGuardJob -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $wrapperGuardJob)) {
            [void]$cleanupErrors.Add('Wrapper isolation guard Job close failed.')
        }
        $wrapperGuardJob = [IntPtr]::Zero
    }
    if ($isolationReleaseCapability -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $isolationReleaseCapability)) {
            [void]$cleanupErrors.Add('Isolation release capability close failed.')
        }
        $isolationReleaseCapability = [IntPtr]::Zero
    }
    if (Test-Path -LiteralPath $runRoot -PathType Container) {
        try { $runDirectoryCapability = New-RunDirectoryCapability $runRoot }
        catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    }
    $cleanupAttested = $orchestratorExitState.ExitConfirmed -and
        $orchestratorExitState.JobCleanupConfirmed
    if ($cleanupAttested) {
        try { $after = Restore-ResearchState $guard }
        catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    } else {
        [void]$cleanupErrors.Add(
            'Owned process cleanup was not attested; research restoration was not started.')
    }
    try { $externalAfter = Get-ExternalSteamStateSnapshot $manifestPath }
    catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    if ($null -ne $after) {
        try {
            Assert-RestorationDirectoryCapabilities $guard
            Close-RestorationBackupCapabilities $guard
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
    Close-RestorationGuardCapabilities $guard
}

$runExists = Test-Path -LiteralPath $runRoot -PathType Container
if ($runExists) {
    try { Assert-RunDirectoryCapability $runDirectoryCapability $runRoot }
    catch {
        $runExists = $false
        [void]$cleanupErrors.Add($_.Exception.Message)
    }
}
$ownedStopped = $cleanupAttested
$restorationExact = $null -ne $after -and
    $after.ManifestSha256 -ceq $before.ManifestSha256
$externalExact = $null -ne $externalAfter -and
    $externalAfter.ManifestSha256 -ceq $externalBefore.ManifestSha256
$version = $null
$isolation = $null
$restoration = $null
$versionBytes = $null
$isolationBytes = $null
$restorationBytes = $null

if ($runExists -and $restorationExact -and $null -ne $externalAfter) {
    try {
        $restoration = Write-StagedRestorationAttestation $runRoot $before `
            $after $externalBefore $externalAfter $orchestratorExitCode `
            $ownedStopped $runDirectoryCapability
    } catch {
        [void]$cleanupErrors.Add($_.Exception.Message)
    }
}

$publicationReady = $false
$walkerValues = $null
$checkerValues = $null
$reconnectObservation = $null
$failureCategory = 'none'
$publicationFailureCategory = 'first_observation_publication_not_ready'
if ($null -ne $primaryError) {
    $failureCategory = 'orchestrator_failed'
    if ($null -ne $orchestratorResult -and
        $orchestratorResult.Values.ContainsKey('failure-category')) {
        $failureCategory = $orchestratorResult.Values['failure-category']
    }
} elseif (-not $restorationExact) {
    $failureCategory = 'research_restoration_failed'
} elseif (-not $externalExact) {
    $failureCategory = 'external_steam_state_changed'
} elseif ($cleanupErrors.Count -ne 0) {
    $failureCategory = 'transaction_cleanup_failed'
} elseif (-not $runExists) {
    $failureCategory = 'capture_run_not_created'
} else {
    try {
        Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
        $versionRecord = Read-BoundedJsonWithRetainedBytes `
            (Join-Path $runRoot 'version-observation.staged.json') `
            65536 'staged version observation' $runDirectoryCapability
        $version = $versionRecord.Value
        [byte[]]$versionBytes = $versionRecord.Bytes
        $isolationRecord = Read-BoundedJsonWithRetainedBytes `
            (Join-Path $runRoot 'isolation-attestation.staged.json') `
            65536 'staged isolation attestation' $runDirectoryCapability
        $isolation = $isolationRecord.Value
        [byte[]]$isolationBytes = $isolationRecord.Bytes
        $restorationRecord = Read-BoundedJsonWithRetainedBytes `
            (Join-Path $runRoot 'restoration-attestation.staged.json') `
            65536 'staged restoration attestation' $runDirectoryCapability
        $restoration = $restorationRecord.Value
        [byte[]]$restorationBytes = $restorationRecord.Bytes
        if ([string]$version.schema -cne 'hlclient.stock-runtime-version-observation.v1' -or
            [string]$version.map_category -cne $Map -or
            [string]$version.client_file_version -cne '1.1.1.1' -or
            [string]$version.client_pe_machine -cne 'x86' -or
            [string]$version.client_signature -cne 'valid' -or
            [string]$version.server_launcher_version -cne '4.1.1.1' -or
            [string]$version.server_pe_machine -cne 'x86' -or
            [string]$version.server_signature -cne 'valid' -or
            [Int64]$version.steam_app_id -ne 70 -or
            [Int64]$version.steam_build_id -ne 15961492 -or
            [string]$version.server_engine_version -cne '1.1.2.2' -or
            [Int64]$version.protocol -ne 48 -or [Int64]$version.server_build -ne 10210 -or
            [string]$version.client_profile_fingerprint -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$version.server_profile_fingerprint -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$version.evidence_status -cne 'observed') {
            throw 'Version observation is not accepted.'
        }
        if ([string]$isolation.schema -cne 'hlclient.stock-runtime-isolation-attestation.v1' -or
            [string]$isolation.session_type -cne 'dynamic' -or
            [Int64]$isolation.persistent_rule_count -ne 0 -or
            [string]$isolation.ipv4_loopback -cne 'allowed' -or
            @('allowed', 'capability_unavailable') -cnotcontains
                [string]$isolation.ipv6_loopback -or
            [string]$isolation.non_loopback_canary -cne 'denied_os_classified' -or
            [string]$isolation.cleanup_status -cne 'exact' -or
            [string]$isolation.evidence_status -cne 'observed') {
            throw 'Isolation attestation is not accepted.'
        }
        if ([string]$restoration.schema -cne 'hlclient.stock-runtime-restoration.v1' -or
            [string]$restoration.external_file_drift -cne 'none' -or
            [string]$restoration.restoration_status -cne 'exact' -or
            -not [bool]$restoration.created_files_removed -or
            -not [bool]$restoration.protected_paths_included -or
            -not [bool]$restoration.owned_processes_stopped -or
            [bool]$restoration.input_automation_used -or
            [Int64]$restoration.input_events_injected -ne 0 -or
            [string]$restoration.pre_manifest_sha256 -cne
                [string]$restoration.post_manifest_sha256 -or
            [string]$restoration.external_pre_manifest_sha256 -cne
                [string]$restoration.external_post_manifest_sha256) {
            throw 'Restoration attestation is not accepted.'
        }
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json', 'research-run-metadata.json')) {
            if (Test-Path -LiteralPath (Join-Path $runRoot $finalLeaf)) {
                throw 'Final evidence exists before publication review.'
            }
        }
        $first = Invoke-FirstObservationChecker $checkerPath $runRoot
        $second = Invoke-FirstObservationChecker $checkerPath $runRoot
        Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
        if ($first.ExitCode -ne 0 -or $second.ExitCode -ne 0 -or
            ($first.Lines -join "`n") -cne ($second.Lines -join "`n")) {
            throw 'Prepublication checker did not produce two identical successful runs.'
        }
        $checkerKeys = @(
            'profile', 'transport-valid', 'sequenced-c2s', 'sequenced-s2c',
            'fragments', 'duplicate-packets', 'old-packets',
            'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
            'delivered-fragment-datagrams', 'reassembled', 'decompressed',
            'signon-replay',
            'post-resource-boundary', 'boundary-payload-ordinal',
            'boundary-observed-ordinal', 'boundary-delivery-ordinal',
            'boundary-byte-offset', 'boundary-bit-offset',
            'boundary-source-sequence', 'boundary-source-payload-bytes',
            'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
            'boundary-reassembled', 'boundary-decompressed',
            'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
            'candidate-recurrence', 'candidate-stability', 'accepted-run',
            'publication-ready', 'result', 'structural-hash',
            'replay-structural-hash')
        $reconnectGenerationSuffixes = @(
            'first-observed-ordinal', 'last-observed-ordinal',
            'connectionless-exchanges', 'first-sequenced-packet-ordinal',
            'client-to-server-packets', 'server-to-client-packets',
            'boundary-payload-ordinal', 'boundary-observed-ordinal',
            'boundary-delivery-ordinal', 'boundary-byte-offset',
            'boundary-bit-offset', 'boundary-source-sequence',
            'boundary-source-payload-bytes', 'boundary-source-payload-bits',
            'boundary-next-unconsumed-bits', 'boundary-reassembled',
            'boundary-decompressed', 'boundary-byte-aligned',
            'candidate-bit-width', 'first-candidate',
            'candidate-body-consumed',
            'candidate-semantic-category-assigned',
            'replay-structural-hash')
        if ($canonicalScenario -ceq 'reconnect') {
            $checkerKeys += @(
                'connection-generation-count', 'exact-boundary-count',
                'runtime-candidate-count', 'generation-distinct',
                'candidate-conflict', 'retired-generation-a-tail-sink',
                'retired-generation-a-server-tail-packets',
                'generation-b-sequenced-after-fresh-accept')
            foreach ($label in @('a', 'b')) {
                foreach ($suffix in $reconnectGenerationSuffixes) {
                    $checkerKeys += 'generation-' + $label + '-' + $suffix
                }
            }
        }
        $checkerValues = Convert-PrefixedOutputToValues $first.Lines `
            '[stock-runtime] ' $checkerKeys 'first-observation checker'
        $expectedCandidateRecurrence = if ($canonicalScenario -ceq 'reconnect') {
            '2'
        } else { '1' }
        $expectedCandidateStability = if ($canonicalScenario -ceq 'reconnect') {
            'stable_observation'
        } else { 'single_observation' }
        if ($checkerValues['profile'] -cne
                'stock_protocol_48_build_10210_evidence_pending' -or
            $checkerValues['transport-valid'] -cne 'true' -or
            $checkerValues['signon-replay'] -cne 'complete' -or
            $checkerValues['post-resource-boundary'] -cne 'observed' -or
            $checkerValues['boundary-reassembled'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['boundary-decompressed'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['boundary-byte-aligned'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['first-candidate'] -cnotmatch
                '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
            [int]($checkerValues['first-candidate'] -replace '^bit-prefix:', '') -gt 255 -or
            $checkerValues['candidate-recurrence'] -cne
                $expectedCandidateRecurrence -or
            $checkerValues['candidate-stability'] -cne
                $expectedCandidateStability -or
            $checkerValues['structural-hash'] -cnotmatch '^[0-9a-f]{64}$' -or
            $checkerValues['replay-structural-hash'] -cnotmatch '^[0-9a-f]{64}$' -or
            $checkerValues['publication-ready'] -cne 'true' -or
            $checkerValues['accepted-run'] -cne 'false' -or
            $checkerValues['result'] -cne 'first-observation') {
            throw 'Prepublication checker did not reach publication readiness.'
        }
        if ($canonicalScenario -ceq 'reconnect' -and
            ($checkerValues['connection-generation-count'] -cne '2' -or
             $checkerValues['exact-boundary-count'] -cne '2' -or
             $checkerValues['runtime-candidate-count'] -cne '2' -or
             $checkerValues['generation-distinct'] -cne 'true' -or
             $checkerValues['candidate-conflict'] -cne 'false' -or
             $checkerValues['retired-generation-a-tail-sink'] -cne
                'routing_only' -or
             $checkerValues['generation-b-sequenced-after-fresh-accept'] -cne
                'true')) {
            throw 'Reconnect checker did not prove the exact A/B lifecycle.'
        }
        foreach ($countKey in @('sequenced-c2s', 'sequenced-s2c', 'fragments',
                'duplicate-packets', 'old-packets',
                'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
                'delivered-fragment-datagrams', 'reassembled', 'decompressed',
                'boundary-payload-ordinal',
                'boundary-observed-ordinal', 'boundary-delivery-ordinal',
                'boundary-byte-offset', 'boundary-source-sequence',
                'boundary-source-payload-bytes', 'boundary-source-payload-bits',
                'boundary-next-unconsumed-bits', 'candidate-bit-width')) {
            [Int64]$countValue = 0
            if (-not [Int64]::TryParse($checkerValues[$countKey], [ref]$countValue) -or
                $countValue -lt 0 -or $countValue -gt 268435456) {
                throw "Prepublication checker count $countKey is outside its bound."
            }
        }
        [Int64]$boundaryBitOffset = 0
        if (-not [Int64]::TryParse(
                $checkerValues['boundary-bit-offset'], [ref]$boundaryBitOffset) -or
            $boundaryBitOffset -lt 0 -or $boundaryBitOffset -gt 7) {
            throw 'Prepublication checker boundary bit offset is invalid.'
        }
        [Int64]$sourcePayloadBytes = $checkerValues['boundary-source-payload-bytes']
        [Int64]$sourcePayloadBits = $checkerValues['boundary-source-payload-bits']
        [Int64]$boundaryByteOffset = $checkerValues['boundary-byte-offset']
        [Int64]$remainingBits = $checkerValues['boundary-next-unconsumed-bits']
        [Int64]$candidateBitWidth = $checkerValues['candidate-bit-width']
        if ($sourcePayloadBits -ne ($sourcePayloadBytes * 8) -or
            (($boundaryByteOffset * 8) + $boundaryBitOffset + $remainingBits) -ne
                $sourcePayloadBits -or
            ($checkerValues['boundary-byte-aligned'] -ceq 'true') -ne
                ($boundaryBitOffset -eq 0) -or
            $candidateBitWidth -lt 1 -or $candidateBitWidth -gt 8 -or
            $candidateBitWidth -gt $remainingBits -or
            (($checkerValues['boundary-byte-aligned'] -ceq 'true') -and
                $candidateBitWidth -ne 8) -or
            ($checkerValues['first-candidate'].StartsWith('bit-prefix:') -and
                [int]($checkerValues['first-candidate'].Substring(11)) -ge
                    [Math]::Pow(2, $candidateBitWidth))) {
            throw 'Prepublication checker cursor/candidate geometry is inconsistent.'
        }
        if ($canonicalScenario -ceq 'reconnect') {
            $generationNumbers = @{}
            foreach ($label in @('a', 'b')) {
                $prefix = 'generation-' + $label + '-'
                $numbers = @{}
                foreach ($suffix in @(
                        'first-observed-ordinal', 'last-observed-ordinal',
                        'connectionless-exchanges',
                        'first-sequenced-packet-ordinal',
                        'client-to-server-packets', 'server-to-client-packets',
                        'boundary-payload-ordinal', 'boundary-observed-ordinal',
                        'boundary-delivery-ordinal', 'boundary-byte-offset',
                        'boundary-bit-offset', 'boundary-source-sequence',
                        'boundary-source-payload-bytes',
                        'boundary-source-payload-bits',
                        'boundary-next-unconsumed-bits',
                        'candidate-bit-width')) {
                    [Int64]$number = 0
                    if (-not [Int64]::TryParse(
                            $checkerValues[$prefix + $suffix], [ref]$number) -or
                        $number -lt 0 -or $number -gt 268435456) {
                        throw "Reconnect checker $prefix$suffix is outside its bound."
                    }
                    $numbers[$suffix] = $number
                }
                $candidate = $checkerValues[$prefix + 'first-candidate']
                if ($checkerValues[$prefix + 'boundary-reassembled'] -cnotmatch
                        '^(?:true|false)$' -or
                    $checkerValues[$prefix + 'boundary-decompressed'] -cnotmatch
                        '^(?:true|false)$' -or
                    $checkerValues[$prefix + 'boundary-byte-aligned'] -cnotmatch
                        '^(?:true|false)$' -or
                    $checkerValues[$prefix + 'candidate-body-consumed'] -cne
                        'false' -or
                    $checkerValues[$prefix +
                        'candidate-semantic-category-assigned'] -cne 'false' -or
                    $checkerValues[$prefix + 'replay-structural-hash'] -cnotmatch
                        '^[0-9a-f]{64}$' -or
                    $candidate -cnotmatch
                        '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
                    [int]($candidate -replace '^bit-prefix:', '') -gt 255) {
                    throw "Reconnect checker generation $label metadata is invalid."
                }
                $firstOrdinal = [Int64]$numbers['first-observed-ordinal']
                $lastOrdinal = [Int64]$numbers['last-observed-ordinal']
                $firstSequence =
                    [Int64]$numbers['first-sequenced-packet-ordinal']
                $observedBoundary =
                    [Int64]$numbers['boundary-observed-ordinal']
                $generationBytes =
                    [Int64]$numbers['boundary-source-payload-bytes']
                $generationBits =
                    [Int64]$numbers['boundary-source-payload-bits']
                $generationByteOffset =
                    [Int64]$numbers['boundary-byte-offset']
                $generationBitOffset =
                    [Int64]$numbers['boundary-bit-offset']
                $generationRemaining =
                    [Int64]$numbers['boundary-next-unconsumed-bits']
                $generationCandidateWidth =
                    [Int64]$numbers['candidate-bit-width']
                $generationAligned =
                    $checkerValues[$prefix + 'boundary-byte-aligned'] -ceq 'true'
                $candidateIsPrefix = $candidate.StartsWith('bit-prefix:')
                if ($firstOrdinal -gt $lastOrdinal -or
                    $firstSequence -lt $firstOrdinal -or
                    $firstSequence -gt $lastOrdinal -or
                    $observedBoundary -lt $firstOrdinal -or
                    $observedBoundary -gt $lastOrdinal -or
                    $numbers['connectionless-exchanges'] -lt 1 -or
                    $numbers['client-to-server-packets'] -lt 1 -or
                    $numbers['server-to-client-packets'] -lt 1 -or
                    $generationBitOffset -gt 7 -or $generationBytes -lt 1 -or
                    $generationBits -ne ($generationBytes * 8) -or
                    (($generationByteOffset * 8) + $generationBitOffset +
                        $generationRemaining) -ne $generationBits -or
                    $generationRemaining -lt 1 -or
                    $generationCandidateWidth -lt 1 -or
                    $generationCandidateWidth -gt 8 -or
                    $generationCandidateWidth -gt $generationRemaining -or
                    $generationAligned -ne ($generationBitOffset -eq 0) -or
                    $candidateIsPrefix -eq $generationAligned -or
                    ($generationAligned -and $generationCandidateWidth -ne 8) -or
                    ($candidateIsPrefix -and
                        [int]$candidate.Substring(11) -ge
                            [Math]::Pow(2, $generationCandidateWidth))) {
                    throw "Reconnect checker generation $label geometry is inconsistent."
                }
                $generationNumbers[$label] = $numbers
            }
            if ([Int64]$generationNumbers['a']['last-observed-ordinal'] -ge
                    [Int64]$generationNumbers['b']['first-observed-ordinal'] -or
                $checkerValues['generation-a-first-candidate'] -cne
                    $checkerValues['generation-b-first-candidate'] -or
                $checkerValues['generation-a-candidate-bit-width'] -cne
                    $checkerValues['generation-b-candidate-bit-width'] -or
                $checkerValues['generation-a-boundary-bit-offset'] -cne
                    $checkerValues['generation-b-boundary-bit-offset']) {
                throw 'Reconnect checker generations overlap or expose conflicting candidates.'
            }
            foreach ($suffix in @(
                    'boundary-payload-ordinal', 'boundary-observed-ordinal',
                    'boundary-delivery-ordinal', 'boundary-byte-offset',
                    'boundary-bit-offset', 'boundary-source-sequence',
                    'boundary-source-payload-bytes',
                    'boundary-source-payload-bits',
                    'boundary-next-unconsumed-bits', 'boundary-reassembled',
                    'boundary-decompressed', 'boundary-byte-aligned',
                    'candidate-bit-width', 'first-candidate')) {
                if ($checkerValues[$suffix] -cne
                    $checkerValues['generation-a-' + $suffix]) {
                    throw 'Reconnect checker aggregate representative is not generation A.'
                }
            }
            [Int64]$retiredTailPackets = 0
            if (-not [Int64]::TryParse(
                    $checkerValues['retired-generation-a-server-tail-packets'],
                    [ref]$retiredTailPackets) -or
                $retiredTailPackets -lt 0 -or
                $retiredTailPackets -gt $MaximumDatagrams) {
                throw 'Reconnect retired-generation A tail count is outside its bound.'
            }
        }
        [Int64]$replayAcceptedSequenced =
            [Int64]$checkerValues['sequenced-c2s'] +
            [Int64]$checkerValues['sequenced-s2c']
        [Int64]$replaySuppressedSequenced =
            [Int64]$checkerValues['duplicate-packets'] +
            [Int64]$checkerValues['old-packets']
        [Int64]$deliveredSequenced =
            [Int64]$checkerValues['delivered-sequenced-c2s'] +
            [Int64]$checkerValues['delivered-sequenced-s2c']
        [Int64]$routingOnlyTail = if ($canonicalScenario -ceq 'reconnect') {
            [Int64]$checkerValues['retired-generation-a-server-tail-packets']
        } else { 0 }
        if ([Int64]$checkerValues['sequenced-c2s'] -gt
                [Int64]$checkerValues['delivered-sequenced-c2s'] -or
            [Int64]$checkerValues['sequenced-s2c'] -gt
                [Int64]$checkerValues['delivered-sequenced-s2c'] -or
            [Int64]$checkerValues['fragments'] -gt
                [Int64]$checkerValues['delivered-fragment-datagrams'] -or
            ($replayAcceptedSequenced + $replaySuppressedSequenced +
                $routingOnlyTail) -ne
                $deliveredSequenced) {
            throw 'Replay accepted/suppressed accounting disagrees with delivered transport counts.'
        }
        [Int64]$sequencedServerPackets =
            $(if ($canonicalScenario -ceq 'reconnect') {
                $checkerValues['sequenced-s2c']
            } else {
                $checkerValues['delivered-sequenced-s2c']
            })
        if (($canonicalScenario -ceq 'baseline' -or
                $canonicalScenario -ceq 'idle-runtime') -and
            $orchestratorDuration -lt 30000) {
            $publicationFailureCategory = 'minimum_observation_duration_not_met'
            throw 'Baseline and idle-runtime acceptance require at least 30 seconds of actual owned-session duration.'
        }
        if ($sequencedServerPackets -lt 100) {
            $publicationFailureCategory =
                'per_run_server_packet_threshold_not_met'
            throw 'Every accepted scenario requires at least 100 generation-attributed sequenced server-to-client packets.'
        }
        $walkerFirst = Invoke-IndependentTransportWalker `
            -WalkerPath $walkerPath -RunRoot $runRoot `
            -CheckerValues $checkerValues `
            -Reconnect ($canonicalScenario -ceq 'reconnect')
        $walkerSecond = Invoke-IndependentTransportWalker `
            -WalkerPath $walkerPath -RunRoot $runRoot `
            -CheckerValues $checkerValues `
            -Reconnect ($canonicalScenario -ceq 'reconnect')
        Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
        if (($walkerFirst -join "`n") -cne ($walkerSecond -join "`n")) {
            throw 'Independent transport walker did not produce two identical runs.'
        }
        $walkerLines = $walkerFirst
        $walkerKeys = @(
            'run-id', 'journal-entries', 'raw-datagrams', 'raw-bytes',
            'observed-c2s', 'observed-s2c', 'delivered-c2s', 'delivered-s2c',
            'observed-connectionless-c2s', 'observed-connectionless-s2c',
            'observed-sequenced-c2s', 'observed-sequenced-s2c',
            'observed-fragment-datagrams', 'observed-reliable-datagrams',
            'delivered-connectionless-c2s', 'delivered-connectionless-s2c',
            'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
            'delivered-fragment-datagrams', 'delivered-reliable-datagrams',
            'wrong-source-datagrams', 'emitted-datagrams', 'transport-complete',
            'last-observed-timestamp-us', 'last-delivered-sequenced-s2c-timestamp-us',
            'observed-delivered-policy', 'final-manifest',
            'post-resource-boundary', 'boundary-payload-ordinal',
            'boundary-observed-ordinal', 'boundary-delivery-ordinal',
            'boundary-byte-offset', 'boundary-bit-offset',
            'boundary-source-sequence', 'boundary-source-payload-bytes',
            'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
            'boundary-reassembled', 'boundary-decompressed',
            'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
            'replay-structural-hash', 'result')
        if ($canonicalScenario -ceq 'reconnect') {
            $walkerKeys += @(
                'connection-generation-count', 'exact-boundary-count',
                'runtime-candidate-count', 'generation-distinct',
                'candidate-conflict', 'candidate-recurrence',
                'candidate-stability', 'retired-generation-a-tail-sink',
                'retired-generation-a-server-tail-packets',
                'generation-b-sequenced-after-fresh-accept')
            foreach ($label in @('a', 'b')) {
                foreach ($suffix in $reconnectGenerationSuffixes) {
                    $walkerKeys += 'generation-' + $label + '-' + $suffix
                }
            }
        }
        $walkerValues = Convert-PrefixedOutputToValues $walkerLines `
            '[stock-runtime-walk] ' $walkerKeys 'independent transport walker'
        if ($walkerValues['result'] -cne 'success' -or
            $walkerValues['run-id'] -cne $runId -or
            $walkerValues['final-manifest'] -cne 'absent-prepublication' -or
            $walkerValues['wrong-source-datagrams'] -cne '0' -or
            $walkerValues['transport-complete'] -cne 'true' -or
            $walkerValues['delivered-sequenced-c2s'] -cne $checkerValues['delivered-sequenced-c2s'] -or
            $walkerValues['delivered-sequenced-s2c'] -cne $checkerValues['delivered-sequenced-s2c'] -or
            $walkerValues['delivered-fragment-datagrams'] -cne $checkerValues['delivered-fragment-datagrams'] -or
            $walkerValues['boundary-payload-ordinal'] -cne $checkerValues['boundary-payload-ordinal'] -or
            $walkerValues['boundary-observed-ordinal'] -cne $checkerValues['boundary-observed-ordinal'] -or
            $walkerValues['boundary-delivery-ordinal'] -cne $checkerValues['boundary-delivery-ordinal'] -or
            $walkerValues['boundary-byte-offset'] -cne $checkerValues['boundary-byte-offset'] -or
            $walkerValues['boundary-bit-offset'] -cne $checkerValues['boundary-bit-offset'] -or
            $walkerValues['boundary-source-sequence'] -cne $checkerValues['boundary-source-sequence'] -or
            $walkerValues['boundary-source-payload-bytes'] -cne $checkerValues['boundary-source-payload-bytes'] -or
            $walkerValues['boundary-source-payload-bits'] -cne $checkerValues['boundary-source-payload-bits'] -or
            $walkerValues['boundary-next-unconsumed-bits'] -cne $checkerValues['boundary-next-unconsumed-bits'] -or
            $walkerValues['boundary-reassembled'] -cne $checkerValues['boundary-reassembled'] -or
            $walkerValues['boundary-decompressed'] -cne $checkerValues['boundary-decompressed'] -or
            $walkerValues['boundary-byte-aligned'] -cne $checkerValues['boundary-byte-aligned'] -or
            $walkerValues['candidate-bit-width'] -cne $checkerValues['candidate-bit-width'] -or
            $walkerValues['first-candidate'] -cne $checkerValues['first-candidate'] -or
            $walkerValues['replay-structural-hash'] -cne $checkerValues['replay-structural-hash']) {
            throw 'Independent walker and production structural summaries disagree.'
        }
        if ($canonicalScenario -ceq 'reconnect') {
            foreach ($key in @(
                    'connection-generation-count', 'exact-boundary-count',
                    'runtime-candidate-count', 'generation-distinct',
                    'candidate-conflict', 'candidate-recurrence',
                    'candidate-stability', 'retired-generation-a-tail-sink',
                    'retired-generation-a-server-tail-packets',
                    'generation-b-sequenced-after-fresh-accept')) {
                if ($walkerValues[$key] -cne $checkerValues[$key]) {
                    throw "Reconnect checker/walker aggregate $key disagrees."
                }
            }
            foreach ($label in @('a', 'b')) {
                foreach ($suffix in $reconnectGenerationSuffixes) {
                    $key = 'generation-' + $label + '-' + $suffix
                    if ($walkerValues[$key] -cne $checkerValues[$key]) {
                        throw "Reconnect checker/walker generation $key disagrees."
                    }
                }
            }
            $reconnectObservation =
                New-ReconnectFinalObservation -Values $checkerValues
        }
        if ($canonicalScenario -ceq 'baseline' -or
            $canonicalScenario -ceq 'idle-runtime') {
            [Int64]$lastObservedTransportUs = 0
            if (-not [Int64]::TryParse(
                    $walkerValues['last-observed-timestamp-us'],
                    [ref]$lastObservedTransportUs) -or
                $lastObservedTransportUs -lt 30000000) {
                $publicationFailureCategory = 'minimum_capture_duration_not_met'
                throw 'Baseline and idle-runtime acceptance require at least 30 seconds on the capture transport clock.'
            }
        }
        if ($canonicalScenario -ceq 'idle-runtime') {
            [Int64]$lastLiveS2cUs = 0
            if (-not [Int64]::TryParse(
                    $walkerValues['last-delivered-sequenced-s2c-timestamp-us'],
                    [ref]$lastLiveS2cUs)) {
                throw 'Independent walker did not publish the idle live-through timestamp.'
            }
            [Int64]$minimumLiveThroughUs = [Math]::Max(
                25000000, ([Int64]$orchestratorDuration - 5000) * 1000)
            if ($lastLiveS2cUs -lt 30000000 -or
                $lastLiveS2cUs -lt $minimumLiveThroughUs) {
                $publicationFailureCategory = 'idle_runtime_did_not_remain_live_through_end'
                throw 'Idle-runtime acceptance requires delivered sequenced server traffic through the final five seconds of a 30-second-or-longer run.'
            }
        }
        $publicationReady = $true
    } catch {
        $failureCategory = $publicationFailureCategory
        $primaryError = $_
    }
}

if ($runExists) {
    $duration = $null
    if ($null -ne $orchestratorResult -and
        $orchestratorResult.Values.ContainsKey('duration-ms')) {
        $duration = [Int64]$orchestratorResult.Values['duration-ms']
    }
    $rawCount = $null
    $journalCount = $null
    if ($null -ne $walkerValues) {
        $rawCount = [Int64]$walkerValues['raw-datagrams']
        $journalCount = [Int64]$walkerValues['journal-entries']
    }
    $runManifest = [ordered]@{
        schema = 'hlclient.stock-runtime-research-run.v1'
        run_id = $runId
        scenario = $canonicalScenario
        map_category = $Map
        external_target_profile = $research.ExternalTargetProfile
        external_target_count = [Int64]$research.ExternalTargetCount
        duration_ms = $duration
        isolation_status = $(if ($publicationReady) { 'verified' } else { 'not-accepted' })
        process_ownership_status = $(if ($ownedStopped) { 'verified-cleanup' } else { 'not-accepted' })
        version_profile_status = $(if ($publicationReady) { 'verified' } else { 'not-accepted' })
        relay_status = $(if ($null -ne $orchestratorResult -and
                $orchestratorResult.Values.ContainsKey('relay-ready')) {
                $orchestratorResult.Values['relay-ready']
            } else { 'not-observed' })
        client_ready_status = $(if ($null -ne $orchestratorResult -and
                $orchestratorResult.Values.ContainsKey('client-ready')) {
                $orchestratorResult.Values['client-ready']
            } else { 'not-observed' })
        restoration_status = $(if ($restorationExact) { 'exact' } else { 'not-exact' })
        external_drift_status = $(if ($externalExact) { 'none' } else { 'changed-or-unavailable' })
        raw_datagram_count = $rawCount
        journal_entry_count = $journalCount
        delivered_sequenced_c2s_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['delivered-sequenced-c2s']
            } else { $null })
        delivered_sequenced_s2c_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['delivered-sequenced-s2c']
            } else { $null })
        delivered_fragment_datagram_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['delivered-fragment-datagrams']
            } else { $null })
        reassembled_payload_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['reassembled']
            } else { $null })
        decompressed_payload_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['decompressed']
            } else { $null })
        offline_replay_status = $(if ($publicationReady) { 'success' } else { 'not-accepted' })
        post_resource_boundary_status = $(if ($publicationReady) { 'observed' } else { 'not-accepted' })
        post_resource_replay_payload_ordinal = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-payload-ordinal'] } else { $null })
        post_resource_corpus_observed_ordinal = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-observed-ordinal'] } else { $null })
        post_resource_delivery_ordinal = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-delivery-ordinal'] } else { $null })
        post_resource_byte_offset = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-byte-offset'] } else { $null })
        post_resource_bit_offset = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-bit-offset'] } else { $null })
        post_resource_source_sequence = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-source-sequence'] } else { $null })
        post_resource_source_payload_bytes = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-source-payload-bytes'] } else { $null })
        post_resource_source_payload_bits = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-source-payload-bits'] } else { $null })
        post_resource_next_unconsumed_bits = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-next-unconsumed-bits'] } else { $null })
        post_resource_reassembled = $(if ($null -ne $checkerValues) {
                $checkerValues['boundary-reassembled'] -ceq 'true' } else { $null })
        post_resource_decompressed = $(if ($null -ne $checkerValues) {
                $checkerValues['boundary-decompressed'] -ceq 'true' } else { $null })
        post_resource_boundary_byte_aligned = $(if ($null -ne $checkerValues) {
                $checkerValues['boundary-byte-aligned'] -ceq 'true'
            } else { $null })
        first_observation_status = $(if ($publicationReady) { 'observed' } else { 'not-accepted' })
        first_candidate = $(if ($null -ne $checkerValues) {
                $checkerValues['first-candidate']
            } else { $null })
        first_candidate_bit_width = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['candidate-bit-width'] } else { $null })
        first_candidate_recurrence = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['candidate-recurrence'] } else { $null })
        transport_structural_sha256 = $(if ($null -ne $checkerValues) {
                $checkerValues['structural-hash'] } else { $null })
        replay_structural_sha256 = $(if ($null -ne $checkerValues) {
                $checkerValues['replay-structural-hash'] } else { $null })
        last_delivered_sequenced_s2c_timestamp_us = $(if ($null -ne $walkerValues) {
                [Int64]$walkerValues['last-delivered-sequenced-s2c-timestamp-us']
            } else { $null })
        last_observed_transport_timestamp_us = $(if ($null -ne $walkerValues) {
                [Int64]$walkerValues['last-observed-timestamp-us']
            } else { $null })
        candidate_stability = $(if ($null -ne $checkerValues) {
                $checkerValues['candidate-stability']
            } else { $null })
        accepted_transport_run = $publicationReady
        accepted_evidence_run = $publicationReady
        failure_category = $(if ($publicationReady) { 'none' } else { $failureCategory })
    }
    if ($publicationReady -and $canonicalScenario -ceq 'reconnect') {
        $runManifest['connection_generation_count'] = 2
        $runManifest['exact_boundary_count'] = 2
        $runManifest['runtime_candidate_count'] = 2
        $runManifest['generation_distinct'] = $true
        $runManifest['candidate_conflict'] = $false
    }
    try {
        Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
        if ($publicationReady) {
            Publish-AcceptedEvidenceTransaction -RunRoot $runRoot `
                -Version $version -VersionBytes $versionBytes `
                -Isolation $isolation -IsolationBytes $isolationBytes `
                -Restoration $restoration `
                -RestorationBytes $restorationBytes `
                -ReconnectObservation $reconnectObservation `
                -RunManifest $runManifest `
                -OwnedJobsExact $ownedStopped `
                -RestorationExact $restorationExact `
                -ExternalStateExact $externalExact `
                -CheckerWalkerReady $publicationReady `
                -FailureCategory $failureCategory `
                -DirectoryCapability $runDirectoryCapability
        } else {
            # A rejected run may retain its typed accepted=false manifest and
            # staged candidates, but never any final evidence leaf.
            foreach ($finalLeaf in @(
                    'version-observation.json',
                    'isolation-attestation.json',
                    'restoration-attestation.json',
                    'reconnect-observation.json')) {
                if (Test-Path -LiteralPath (Join-Path $runRoot $finalLeaf)) {
                    throw 'Rejected run contains a final evidence leaf.'
                }
            }
            Write-AtomicJsonNoOverwrite -Path `
                (Join-Path $runRoot 'research-run-metadata.json') `
                -Value $runManifest -Label 'rejected research run manifest' `
                -DirectoryCapability $runDirectoryCapability
        }
    } catch {
        $publicationError = $_
        if ($publicationReady) {
            $failureCategory = 'evidence_publication_failed'
            $publicationReady = $false
            try {
                Write-RejectedManifestAfterEvidencePublicationFailure `
                    $runRoot $runManifest $runDirectoryCapability
            } catch {
                [void]$cleanupErrors.Add(
                    'Rejected publication manifest failed: ' +
                    $_.Exception.Message)
            }
        }
        [void]$cleanupErrors.Add($publicationError.Exception.Message)
    }
}

if ($cleanupErrors.Count -ne 0) {
    if ($null -ne $runDirectoryCapability) {
        $runDirectoryCapability.Dispose()
        $runDirectoryCapability = $null
    }
    if ($failureCategory -ceq 'none') { $failureCategory = 'transaction_cleanup_failed' }
    Write-Output '[stock-runtime-capture] active-capture=failed'
    Write-Output "[stock-runtime-capture] failure-category=$failureCategory"
    Write-Output '[stock-runtime-capture] accepted-evidence-run=false'
    Write-Output '[stock-runtime-capture] result=failed'
    $prefix = if ($null -ne $primaryError) { $primaryError.Exception.Message + '; ' } else { '' }
    throw ($prefix + ($cleanupErrors -join '; '))
}
if ($null -ne $primaryError) {
    if ($null -ne $runDirectoryCapability) {
        $runDirectoryCapability.Dispose()
        $runDirectoryCapability = $null
    }
    Write-Output '[stock-runtime-capture] active-capture=failed'
    Write-Output "[stock-runtime-capture] failure-category=$failureCategory"
    Write-Output '[stock-runtime-capture] accepted-evidence-run=false'
    Write-Output '[stock-runtime-capture] result=failed'
    throw $primaryError
}
if (-not $publicationReady) {
    if ($null -ne $runDirectoryCapability) {
        $runDirectoryCapability.Dispose()
        $runDirectoryCapability = $null
    }
    Write-Output '[stock-runtime-capture] active-capture=failed'
    Write-Output "[stock-runtime-capture] failure-category=$failureCategory"
    Write-Output '[stock-runtime-capture] accepted-evidence-run=false'
    Write-Output '[stock-runtime-capture] result=failed'
    throw "Stock runtime capture did not reach accepted publication: $failureCategory."
}

Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
$runDirectoryCapability.Dispose()
$runDirectoryCapability = $null
Write-Output "[stock-runtime-capture] run-id=$runId"
Write-Output '[stock-runtime-capture] active-capture=completed'
Write-Output "[stock-runtime-capture] stock-processes-started=$($orchestratorResult.Values['processes-started'])"
Write-Output "[stock-runtime-capture] owned-processes-started=$($orchestratorResult.Values['processes-started'])"
Write-Output '[stock-runtime-capture] relay-started=true'
Write-Output '[stock-runtime-capture] client-ready=true'
Write-Output '[stock-runtime-capture] bounded-transport-complete=true'
Write-Output '[stock-runtime-capture] restoration=exact'
Write-Output '[stock-runtime-capture] external-file-drift=none'
Write-Output '[stock-runtime-capture] post-resource-boundary=observed'
Write-Output '[stock-runtime-capture] first-observation=observed'
if ($canonicalScenario -ceq 'reconnect') {
    Write-Output '[stock-runtime-capture] connection-generations=2'
    Write-Output '[stock-runtime-capture] post-resource-boundaries=2'
    Write-Output '[stock-runtime-capture] runtime-candidates=2'
    Write-Output '[stock-runtime-capture] generation-distinct=true'
    Write-Output '[stock-runtime-capture] candidate-conflict=false'
}
Write-Output '[stock-runtime-capture] accepted-evidence-run=true'
Write-Output '[stock-runtime-capture] result=success'
