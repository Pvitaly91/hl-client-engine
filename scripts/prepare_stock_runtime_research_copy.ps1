#requires -Version 5.1

<#
.SYNOPSIS
Creates a new, isolated Half-Life research copy for stock-runtime capture.

.DESCRIPTION
The destination must not exist. The helper copies one Half-Life tree, rejects
reparse points, alternate data streams and hard links, compares a bounded
source/destination inventory, then publishes a private preparation manifest
and the exact isolation marker. It never modifies the source tree.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SourceHalfLifeRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$DestinationHalfLifeRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$markerName = '.hlclient-research-isolated'
$markerText = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
$manifestName = '.hlclient-research-preparation.json'
$maximumEntries = 200000
$maximumBytes = [Int64]17179869184

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

function Test-PathAtOrBelow {
    param([string]$Path, [string]$Root)
    $pathValue = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $rootValue = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    return $pathValue.Equals($rootValue, [StringComparison]::OrdinalIgnoreCase) -or
        $pathValue.StartsWith(
            $rootValue + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($fullPath)
    $current = $root
    foreach ($component in @($fullPath.Substring($root.Length) -split '[\\/]' |
            Where-Object { $_ })) {
        $current = [IO.Path]::Combine($current, $component)
        if (-not (Test-Path -LiteralPath $current)) { break }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label contains a reparse point."
        }
    }
}

function Assert-LocalFixedDrivePath {
    param([string]$Path, [string]$Label)
    if ($Path -cnotmatch '^(?<drive>[A-Za-z]):\\') {
        throw "$Label must use a drive-letter path; UNC and volume aliases are rejected."
    }
    $driveName = $Matches.drive.ToUpperInvariant()
    $drive = [IO.DriveInfo]::new($driveName + ':\')
    if (-not $drive.IsReady -or $drive.DriveType -ne [IO.DriveType]::Fixed) {
        throw "$Label must reside on a ready fixed local drive."
    }
    $subst = Join-Path $env:SystemRoot 'System32\subst.exe'
    if (-not (Test-Path -LiteralPath $subst -PathType Leaf)) {
        throw 'subst.exe is unavailable; destination identity cannot be screened.'
    }
    $mappings = @(& $subst 2>$null | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) { throw 'subst.exe listing failed.' }
    $prefix = '^\s*' + [regex]::Escape($driveName + ':\:') + '\s*=>'
    if (@($mappings | Where-Object { $_ -match $prefix }).Count -ne 0) {
        throw "$Label must not use a substituted drive."
    }
}

function Resolve-LexicalPathThroughExistingAncestor {
    param([string]$Path)
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $suffix = [Collections.Generic.Stack[string]]::new()
    $current = $full
    while (-not (Test-Path -LiteralPath $current)) {
        $name = [IO.Path]::GetFileName($current)
        if ([string]::IsNullOrEmpty($name)) {
            throw 'Path has no existing canonical ancestor.'
        }
        $suffix.Push($name)
        $parent = Split-Path -Parent $current
        if ($parent -eq $current) { throw 'Path ancestor resolution did not progress.' }
        $current = $parent
    }
    $resolved = [IO.Path]::GetFullPath(
        (Get-Item -LiteralPath $current -Force).FullName).TrimEnd('\', '/')
    while ($suffix.Count -ne 0) { $resolved = Join-Path $resolved $suffix.Pop() }
    return [IO.Path]::GetFullPath($resolved).TrimEnd('\', '/')
}

function Get-VolumeRelativeIdentity {
    param([string]$Path)
    $resolved = Resolve-LexicalPathThroughExistingAncestor $Path
    if ($resolved -cnotmatch '^(?<drive>[A-Za-z]):\\(?<suffix>.*)$') {
        throw 'Volume-relative identity requires a drive-letter path.'
    }
    $driveName = $Matches.drive
    $relativeSuffix = $Matches.suffix
    $volumeName = [Text.StringBuilder]::new(64)
    if (-not [Hlclient.StockRuntimePathIdentity]::GetVolumeNameForVolumeMountPoint(
            ($driveName.ToUpperInvariant() + ':\'), $volumeName,
            $volumeName.Capacity)) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Physical volume identity is unavailable (Win32 $errorCode)."
    }
    return [pscustomobject]@{
        Path = $resolved
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

function Get-KnownSteamLibraryRoots {
    $roots = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($registryPath in @('HKCU:\Software\Valve\Steam',
            'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        if (-not (Test-Path -LiteralPath $registryPath)) { continue }
        $record = Get-ItemProperty -LiteralPath $registryPath
        foreach ($name in @('SteamPath', 'InstallPath')) {
            $property = $record.PSObject.Properties[$name]
            if ($null -ne $property -and
                -not [string]::IsNullOrWhiteSpace([string]$property.Value)) {
                [void]$roots.Add([IO.Path]::GetFullPath([string]$property.Value))
            }
        }
    }
    foreach ($steamRoot in @($roots)) {
        $libraries = Join-Path $steamRoot 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $libraries -PathType Leaf)) { continue }
        $item = Get-Item -LiteralPath $libraries -Force
        if ($item.Length -lt 1 -or $item.Length -gt 1048576) {
            throw 'Steam libraryfolders.vdf is outside its byte bound.'
        }
        foreach ($match in [regex]::Matches((Get-Content -Raw -LiteralPath $libraries),
                '"path"\s+"(?<path>[^"]+)"')) {
            [void]$roots.Add([IO.Path]::GetFullPath(
                    $match.Groups['path'].Value.Replace('\\', '\')))
        }
    }
    return @($roots)
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    $streams = @(Get-Item -LiteralPath $Path -Stream * -ErrorAction Stop)
    if (@($streams | Where-Object { $_.Stream -cne ':$DATA' }).Count -ne 0) {
        throw "$Label contains an alternate data stream."
    }
}

function Assert-NoHardLink {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force
    $linkType = $item.PSObject.Properties['LinkType']
    if ($null -eq $linkType) { throw "$Label hard-link state is unavailable." }
    if (-not [string]::IsNullOrEmpty([string]$linkType.Value)) {
        throw "$Label is linked."
    }
}

function Get-BoundedInventory {
    param([string]$Root, [switch]$RequireUnlinkedFiles)
    $records = [Collections.Generic.List[string]]::new()
    $queue = [Collections.Generic.Queue[IO.DirectoryInfo]]::new()
    $queue.Enqueue([IO.DirectoryInfo](Get-Item -LiteralPath $Root -Force))
    [Int64]$totalBytes = 0
    while ($queue.Count -ne 0) {
        $directory = $queue.Dequeue()
        if (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Inventory contains a reparse-point directory.'
        }
        foreach ($item in @($directory.GetFileSystemInfos() | Sort-Object Name)) {
            if ($records.Count -ge $maximumEntries) {
                throw 'Half-Life tree exceeds the entry bound.'
            }
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Half-Life tree contains a reparse point.'
            }
            $prefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
            $relative = $item.FullName.Substring($prefix.Length).Replace('\', '/')
            if (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                [void]$records.Add('d|' + $relative)
                $queue.Enqueue([IO.DirectoryInfo]$item)
                continue
            }
            Assert-OnlyDefaultDataStream $item.FullName 'Half-Life file'
            if ($RequireUnlinkedFiles) { Assert-NoHardLink $item.FullName 'copied Half-Life file' }
            if ($item.Length -lt 0 -or $totalBytes -gt ($maximumBytes - $item.Length)) {
                throw 'Half-Life tree exceeds the byte bound.'
            }
            $totalBytes += $item.Length
            $sha = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
            [void]$records.Add(('f|{0}|{1}|{2}' -f $relative, $item.Length, $sha))
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
        Records = @($records | Sort-Object)
        EntryCount = $records.Count
        TotalBytes = $totalBytes
        ManifestSha256 = $digest
    }
}

$sourceRequested = [IO.Path]::GetFullPath($SourceHalfLifeRoot).TrimEnd('\', '/')
$destinationRequested = [IO.Path]::GetFullPath($DestinationHalfLifeRoot).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $sourceRequested -PathType Container)) {
    throw 'SourceHalfLifeRoot must be an existing directory.'
}
if (Test-Path -LiteralPath $destinationRequested) {
    throw 'DestinationHalfLifeRoot must not already exist.'
}
Assert-LocalFixedDrivePath $sourceRequested 'source root'
Assert-LocalFixedDrivePath $destinationRequested 'destination root'
Assert-NoReparsePointInExistingPath $sourceRequested 'source root'
Assert-NoReparsePointInExistingPath $destinationRequested 'destination parent'

$source = [IO.Path]::GetFullPath(
    (Get-Item -LiteralPath $sourceRequested -Force).FullName).TrimEnd('\', '/')
$destination = Resolve-LexicalPathThroughExistingAncestor $destinationRequested
$sourceIdentity = Get-VolumeRelativeIdentity $source
$destinationIdentity = Get-VolumeRelativeIdentity $destination
if ((Test-PhysicalPathAtOrBelow $destinationIdentity $sourceIdentity) -or
    (Test-PhysicalPathAtOrBelow $sourceIdentity $destinationIdentity)) {
    throw 'Source and destination trees must be disjoint.'
}
if ($destination -match '(?i)(?:^|[\\/])steamapps(?:[\\/]|$)') {
    throw 'Destination must not be a primary Steam-library path.'
}
foreach ($steamRoot in @(Get-KnownSteamLibraryRoots)) {
    if (-not (Test-Path -LiteralPath $steamRoot -PathType Container)) { continue }
    $steamIdentity = Get-VolumeRelativeIdentity $steamRoot
    if ((Test-PhysicalPathAtOrBelow $destinationIdentity $steamIdentity) -or
        (Test-PhysicalPathAtOrBelow $steamIdentity $destinationIdentity)) {
        throw 'Destination overlaps a configured Steam library.'
    }
}
foreach ($launcher in @('hl.exe', 'hlds.exe')) {
    $path = Join-Path $source $launcher
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Source tree lacks $launcher."
    }
}
if (Test-Path -LiteralPath (Join-Path $source $markerName)) {
    throw 'Source tree is already marked as a research copy.'
}

$sourceInventory = Get-BoundedInventory -Root $source
$destinationParent = Split-Path -Parent $destination
if (-not (Test-Path -LiteralPath $destinationParent -PathType Container)) {
    [IO.Directory]::CreateDirectory($destinationParent) | Out-Null
}
Assert-NoReparsePointInExistingPath $destinationParent 'destination parent'
if (Test-Path -LiteralPath $destination) {
    throw 'Destination appeared concurrently before copy.'
}
$staging = Join-Path $destinationParent (
    '.hlclient-stock-runtime-copy-' + [Guid]::NewGuid().ToString('N'))
$published = $false
try {
    Copy-Item -LiteralPath $source -Destination $staging -Recurse -ErrorAction Stop
    Assert-NoReparsePointInExistingPath $staging 'copied research staging root'
    $destinationInventory = Get-BoundedInventory -Root $staging -RequireUnlinkedFiles
    if ($destinationInventory.EntryCount -ne $sourceInventory.EntryCount -or
        $destinationInventory.TotalBytes -ne $sourceInventory.TotalBytes -or
        $destinationInventory.ManifestSha256 -cne $sourceInventory.ManifestSha256) {
        throw 'Copied research inventory differs from the source.'
    }

    foreach ($launcher in @('hl.exe', 'hlds.exe')) {
        $sourceLauncher = Join-Path $source $launcher
        $destinationLauncher = Join-Path $staging $launcher
        if ((Get-FileHash -LiteralPath $sourceLauncher -Algorithm SHA256).Hash -cne
            (Get-FileHash -LiteralPath $destinationLauncher -Algorithm SHA256).Hash) {
            throw "Copied $launcher differs from its source."
        }
    }

    $manifest = [ordered]@{
        schema = 'hlclient.stock-runtime-research-preparation.v1'
        marker = $markerText
        source_inventory_entries = $sourceInventory.EntryCount
        source_inventory_bytes = $sourceInventory.TotalBytes
        source_inventory_sha256 = $sourceInventory.ManifestSha256
        client_sha256 = (Get-FileHash -LiteralPath (Join-Path $staging 'hl.exe') `
            -Algorithm SHA256).Hash.ToUpperInvariant()
        server_launcher_sha256 = (Get-FileHash -LiteralPath (Join-Path $staging 'hlds.exe') `
            -Algorithm SHA256).Hash.ToUpperInvariant()
        paths_recorded = $false
        preparation_status = 'exact-copy-verified'
    }
    $manifestPath = Join-Path $staging $manifestName
    $markerPath = Join-Path $staging $markerName
    [IO.File]::WriteAllText(
        $manifestPath,
        (($manifest | ConvertTo-Json -Depth 4) + "`r`n"),
        [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($markerPath, $markerText, [Text.Encoding]::ASCII)
    Assert-OnlyDefaultDataStream $manifestPath 'preparation manifest'
    Assert-NoHardLink $manifestPath 'preparation manifest'
    Assert-OnlyDefaultDataStream $markerPath 'research marker'
    Assert-NoHardLink $markerPath 'research marker'
    if (Test-Path -LiteralPath $destination) {
        throw 'Destination appeared concurrently before atomic publication.'
    }
    [IO.Directory]::Move($staging, $destination)
    $published = $true
} catch {
    Write-Output '[stock-runtime-prepare] result=failed-unpublished-staging-retained'
    Write-Warning "A helper-owned bounded staging tree may remain at '$staging'; the requested destination was not published."
    throw
}
if (-not $published -or -not (Test-Path -LiteralPath $destination -PathType Container)) {
    throw 'Research copy publication did not complete.'
}

Write-Output '[stock-runtime-prepare] source-modified=false'
Write-Output '[stock-runtime-prepare] copied-launchers=2'
Write-Output "[stock-runtime-prepare] copied-entry-count=$($sourceInventory.EntryCount)"
Write-Output '[stock-runtime-prepare] marker=HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
Write-Output '[stock-runtime-prepare] result=success'
