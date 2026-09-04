#requires -Version 5.1

Set-StrictMode -Version Latest

$script:StockExternalDriftScopes = @(
    'none', 'app_manifest', 'steam_half_life_launcher',
    'steam_hlds_launcher', 'steam_valve_client_dll',
    'steam_valve_server_dll', 'steam_hlfx_tree',
    'steam_other_monitored_entry', 'steam_library_metadata',
    'research_protected_entry', 'diagnostic_output', 'unknown')
$script:StockExternalDriftKinds = @(
    'none', 'content_changed', 'size_changed', 'last_write_changed',
    'creation_time_changed', 'attributes_changed', 'identity_replaced',
    'created', 'removed', 'directory_metadata_changed',
    'snapshot_entry_unreadable', 'snapshot_incomplete')

function Initialize-StockExternalIdentityNative {
    if ($null -ne ('Hlclient.StockExternalIdentity' -as [type])) { return }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;

namespace Hlclient
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct StockExternalByHandleInformation
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

    public sealed class StockExternalObservation
    {
        public string Identity { get; set; }
        public long Size { get; set; }
        public long CreationTicks { get; set; }
        public long LastWriteTicks { get; set; }
        public long Attributes { get; set; }
        public bool IsDirectory { get; set; }
    }

    public static class StockExternalIdentity
    {
        private const uint FileReadAttributes = 0x80;
        private const uint FileShareRead = 0x1;
        private const uint FileShareWrite = 0x2;
        private const uint FileShareDelete = 0x4;
        private const uint OpenExisting = 3;
        private const uint FileFlagBackupSemantics = 0x02000000;
        private const uint FileFlagOpenReparsePoint = 0x00200000;
        private const uint FileAttributeDirectory = 0x10;
        private const uint FileAttributeReparsePoint = 0x400;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateFile(
            string fileName, uint desiredAccess, uint shareMode,
            IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandle(
            IntPtr handle, out StockExternalByHandleInformation information);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);

        private static long Ticks(
            System.Runtime.InteropServices.ComTypes.FILETIME value)
        {
            long fileTime = ((long)value.dwHighDateTime << 32) |
                (uint)value.dwLowDateTime;
            return DateTime.FromFileTimeUtc(fileTime).Ticks;
        }

        public static StockExternalObservation Observe(string path)
        {
            string canonical = Path.GetFullPath(path).TrimEnd('\\', '/');
            IntPtr handle = CreateFile(
                canonical, FileReadAttributes,
                FileShareRead | FileShareWrite | FileShareDelete,
                IntPtr.Zero, OpenExisting,
                FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "External-state identity open failed.");
            try
            {
                StockExternalByHandleInformation value;
                if (!GetFileInformationByHandle(handle, out value))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "External-state identity query failed.");
                if ((value.FileAttributes & FileAttributeReparsePoint) != 0)
                    throw new InvalidOperationException(
                        "External-state entry is a reparse point.");
                ulong size = ((ulong)value.FileSizeHigh << 32) | value.FileSizeLow;
                ulong fileId = ((ulong)value.FileIndexHigh << 32) | value.FileIndexLow;
                return new StockExternalObservation {
                    Identity = value.VolumeSerialNumber.ToString("X8") + ":" +
                        fileId.ToString("X16"),
                    Size = checked((long)size),
                    CreationTicks = Ticks(value.CreationTime),
                    LastWriteTicks = Ticks(value.LastWriteTime),
                    Attributes = value.FileAttributes,
                    IsDirectory = (value.FileAttributes & FileAttributeDirectory) != 0
                };
            }
            finally { CloseHandle(handle); }
        }
    }
}
'@
}

function New-StockExternalStateEntry {
    param([string]$Scope, [string]$RelativePath, [string]$Path)
    Assert-StockExternalDriftToken $Scope $script:StockExternalDriftScopes `
        'External drift scope'
    try {
        if (Get-Command Assert-NoReparsePointInExistingPath -ErrorAction SilentlyContinue) {
            Assert-NoReparsePointInExistingPath $Path 'external drift entry'
        }
        if (Get-Command Assert-OnlyDefaultDataStream -ErrorAction SilentlyContinue) {
            Assert-OnlyDefaultDataStream $Path 'external drift entry'
        }
        Initialize-StockExternalIdentityNative
        $before = [Hlclient.StockExternalIdentity]::Observe($Path)
        $sha256 = if ($before.IsDirectory) { '' } else {
            (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
        }
        $after = [Hlclient.StockExternalIdentity]::Observe($Path)
        if ($before.Identity -cne $after.Identity -or
            $before.Size -ne $after.Size -or
            $before.CreationTicks -ne $after.CreationTicks -or
            $before.LastWriteTicks -ne $after.LastWriteTicks -or
            $before.Attributes -ne $after.Attributes -or
            $before.IsDirectory -ne $after.IsDirectory) {
            throw 'External-state entry changed during observation.'
        }
        return [pscustomobject]@{
            scope = $Scope
            relative_path = $RelativePath.Replace('\', '/')
            entry_kind = $(if ($before.IsDirectory) { 'directory' } else { 'file' })
            read_status = 'readable'
            identity = $before.Identity
            size = $(if ($before.IsDirectory) { [Int64]0 } else { $before.Size })
            sha256 = $sha256
            last_write_ticks = $before.LastWriteTicks
            creation_ticks = $before.CreationTicks
            attributes = $before.Attributes
            reparse_status = 'absent'
            ads_status = 'default-only'
        }
    } catch {
        return [pscustomobject]@{
            scope = $Scope
            relative_path = $RelativePath.Replace('\', '/')
            entry_kind = 'unavailable'
            read_status = 'unreadable'
            identity = $null
            size = $null
            sha256 = $null
            last_write_ticks = $null
            creation_ticks = $null
            attributes = $null
            reparse_status = 'unknown'
            ads_status = 'unknown'
        }
    }
}

function Assert-StockExternalDriftToken {
    param([string]$Value, [string[]]$Allowed, [string]$Label)
    if ($Allowed -cnotcontains $Value) {
        throw "$Label is outside its exact allowlist."
    }
}

function Get-StockExternalEntryKey {
    param([object]$Entry)
    Assert-StockExternalDriftToken ([string]$Entry.scope) `
        $script:StockExternalDriftScopes 'External drift scope'
    $relative = [string]$Entry.relative_path
    if ([string]::IsNullOrEmpty($relative) -or
        $relative.StartsWith('/') -or $relative.StartsWith('\') -or
        $relative -match '(^|/|\\)\.\.($|/|\\)' -or
        [IO.Path]::IsPathRooted($relative)) {
        throw 'External drift relative path is invalid.'
    }
    return ([string]$Entry.scope) + "`0" + $relative.Replace('\', '/')
}

function Get-StockOrdinalSortedStrings {
    param([string[]]$Values)
    [string[]]$copy = @($Values)
    [Array]::Sort($copy, [StringComparer]::Ordinal)
    return $copy
}

function New-StockExternalStateSnapshot {
    param([object[]]$Entries, [string]$Phase)
    if ([string]::IsNullOrEmpty($Phase) -or $Phase -cnotmatch
        '^(idle_control|wfp_preflight|standard_server_diagnostic|private_server_diagnostic|post_cleanup_settle)$') {
        throw 'External drift phase is invalid.'
    }
    $byKey = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    foreach ($entry in @($Entries)) {
        $key = Get-StockExternalEntryKey $entry
        if ($byKey.ContainsKey($key)) {
            throw 'External drift snapshot contains a duplicate entry.'
        }
        $byKey.Add($key, $entry)
    }
    [string[]]$keys = @($byKey.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    $canonicalRecords = [Collections.Generic.List[string]]::new()
    foreach ($key in $keys) {
        $entry = $byKey[$key]
        $values = @(
            [string]$entry.scope, [string]$entry.relative_path,
            [string]$entry.entry_kind, [string]$entry.read_status,
            [string]$entry.identity, [string]$entry.size,
            [string]$entry.sha256, [string]$entry.last_write_ticks,
            [string]$entry.creation_ticks, [string]$entry.attributes,
            [string]$entry.reparse_status, [string]$entry.ads_status)
        [void]$canonicalRecords.Add(($values -join '|'))
    }
    $canonical = $canonicalRecords -join "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($canonical)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = ([BitConverter]::ToString(
                $algorithm.ComputeHash($bytes))).Replace('-', '')
    } finally { $algorithm.Dispose() }
    $orderedEntries = @($keys | ForEach-Object { $byKey[$_] })
    return [pscustomobject]@{
        schema = 'hlclient.stock-external-state-snapshot.v1'
        phase = $Phase
        entries = $orderedEntries
        entry_count = $keys.Count
        manifest_sha256 = $digest
        EntryCount = $keys.Count
        ManifestSha256 = $digest
    }
}

function Compare-StockExternalStateSnapshot {
    param([object]$Before, [object]$After, [string]$Phase)
    if ($null -eq $Before -or $null -eq $After -or
        [string]$Before.schema -cne 'hlclient.stock-external-state-snapshot.v1' -or
        [string]$After.schema -cne 'hlclient.stock-external-state-snapshot.v1') {
        throw 'External drift comparison requires two typed snapshots.'
    }
    $beforeByKey = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    $afterByKey = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    foreach ($entry in @($Before.entries)) {
        $beforeByKey.Add((Get-StockExternalEntryKey $entry), $entry)
    }
    foreach ($entry in @($After.entries)) {
        $afterByKey.Add((Get-StockExternalEntryKey $entry), $entry)
    }
    [string[]]$allKeys = @($beforeByKey.Keys) + @($afterByKey.Keys)
    [Array]::Sort($allKeys, [StringComparer]::Ordinal)
    $uniqueKeys = [Collections.Generic.List[string]]::new()
    $last = $null
    foreach ($key in $allKeys) {
        if ($null -eq $last -or $key -cne $last) {
            [void]$uniqueKeys.Add($key); $last = $key
        }
    }
    $changes = [Collections.Generic.List[object]]::new()
    $unchanged = 0
    foreach ($key in $uniqueKeys) {
        $hasBefore = $beforeByKey.ContainsKey($key)
        $hasAfter = $afterByKey.ContainsKey($key)
        if (-not $hasBefore -or -not $hasAfter) {
            $entry = if ($hasBefore) { $beforeByKey[$key] } else { $afterByKey[$key] }
            [void]$changes.Add([pscustomobject]@{
                    scope = [string]$entry.scope
                    relative_path = [string]$entry.relative_path
                    kind = $(if ($hasBefore) { 'removed' } else { 'created' })
                    changed_fields = @('presence') })
            continue
        }
        $beforeEntry = $beforeByKey[$key]
        $afterEntry = $afterByKey[$key]
        if ([string]$beforeEntry.read_status -cne 'readable' -or
            [string]$afterEntry.read_status -cne 'readable') {
            [void]$changes.Add([pscustomobject]@{
                    scope = [string]$beforeEntry.scope
                    relative_path = [string]$beforeEntry.relative_path
                    kind = 'snapshot_entry_unreadable'
                    changed_fields = @('read_status') })
            continue
        }
        $fields = [Collections.Generic.List[string]]::new()
        foreach ($name in @('entry_kind', 'identity', 'size', 'sha256',
                'last_write_ticks', 'creation_ticks', 'attributes',
                'reparse_status', 'ads_status')) {
            if ([string]$beforeEntry.$name -cne [string]$afterEntry.$name) {
                [void]$fields.Add($name)
            }
        }
        if ($fields.Count -eq 0) { ++$unchanged; continue }
        $kind = if ($fields -contains 'entry_kind' -or
            $fields -contains 'identity') { 'identity_replaced' }
        elseif ($fields -contains 'size') { 'size_changed' }
        elseif ($fields -contains 'sha256') { 'content_changed' }
        elseif ([string]$beforeEntry.entry_kind -ceq 'directory') {
            'directory_metadata_changed'
        } elseif ($fields -contains 'last_write_ticks') { 'last_write_changed' }
        elseif ($fields -contains 'creation_ticks') { 'creation_time_changed' }
        elseif ($fields -contains 'attributes') { 'attributes_changed' }
        else { 'snapshot_incomplete' }
        Assert-StockExternalDriftToken $kind $script:StockExternalDriftKinds `
            'External drift kind'
        [void]$changes.Add([pscustomobject]@{
                scope = [string]$beforeEntry.scope
                relative_path = [string]$beforeEntry.relative_path
                kind = $kind
                changed_fields = @($fields) })
    }
    $scopeSet = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    $pairSet = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($change in $changes) {
        [void]$scopeSet.Add([string]$change.scope)
        [void]$pairSet.Add(([string]$change.scope) + '|' + [string]$change.kind)
    }
    [string[]]$scopeKinds = @($pairSet)
    [Array]::Sort($scopeKinds, [StringComparer]::Ordinal)
    $unreadable = @($changes | Where-Object {
            $_.kind -ceq 'snapshot_entry_unreadable' -or
            $_.kind -ceq 'snapshot_incomplete' }).Count
    $content = @($changes | Where-Object {
            $_.kind -ceq 'content_changed' -or $_.kind -ceq 'size_changed' }).Count
    $metadata = @($changes | Where-Object {
            $_.kind -in @('last_write_changed', 'creation_time_changed',
                'attributes_changed', 'directory_metadata_changed') }).Count
    return [pscustomobject]@{
        schema = 'hlclient.stock-external-drift.v1'
        phase = $Phase
        before_manifest_sha256 = [string]$Before.manifest_sha256
        after_manifest_sha256 = [string]$After.manifest_sha256
        unchanged_entries = $unchanged
        content_changes = $content
        metadata_only_changes = $metadata
        identity_replacements = @($changes | Where-Object kind -ceq 'identity_replaced').Count
        created = @($changes | Where-Object kind -ceq 'created').Count
        removed = @($changes | Where-Object kind -ceq 'removed').Count
        unreadable = $unreadable
        changed_scopes = $scopeSet.Count
        scope_kinds = $scopeKinds
        changes = @($changes)
        result = $(if ($unreadable -ne 0) { 'incomplete' }
            elseif ($changes.Count -eq 0) { 'none' } else { 'changed' })
    }
}

function Write-StockExternalDriftPublicOutput {
    param([object]$Difference)
    Write-Output "[stock-drift] phase=$($Difference.phase)"
    foreach ($pair in @($Difference.scope_kinds)) {
        $parts = $pair.Split('|')
        Write-Output "[stock-drift] scope=$($parts[0]) kind=$($parts[1])"
    }
    foreach ($entry in @(
            @('changed-scopes', $Difference.changed_scopes),
            @('content-changes', $Difference.content_changes),
            @('metadata-only-changes', $Difference.metadata_only_changes),
            @('identity-replacements', $Difference.identity_replacements),
            @('created', $Difference.created),
            @('removed', $Difference.removed),
            @('unreadable', $Difference.unreadable),
            @('result', $Difference.result))) {
        Write-Output "[stock-drift] $($entry[0])=$($entry[1])"
    }
}
