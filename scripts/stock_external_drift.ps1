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
        $semanticProjection = if (-not $before.IsDirectory -and
            $Scope -ceq 'steam_library_metadata' -and
            (Get-Command Get-StockSteamUserConfigProjection `
                -ErrorAction SilentlyContinue)) {
            Get-StockSteamUserConfigProjection $Path
        } else {
            [pscustomobject]@{ status = 'not-applicable'; entry_class = 'none' }
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
            semantic_projection = $semanticProjection
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
            semantic_projection = [pscustomobject]@{
                status = 'unavailable'; entry_class = 'none'
            }
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
                    changed_fields = @('presence')
                    identity_changed = $false
                    content_digest_changed = $false
                    size_changed = $false
                    last_write_changed = $false
                    creation_time_changed = $false
                    attributes_changed = $false
                    entry_kind_changed = $false
                    presence_changed = $true
                    reparse_changed = $false
                    ads_changed = $false })
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
                    changed_fields = @('read_status')
                    identity_changed = $false
                    content_digest_changed = $false
                    size_changed = $false
                    last_write_changed = $false
                    creation_time_changed = $false
                    attributes_changed = $false
                    entry_kind_changed = $false
                    presence_changed = $false
                    reparse_changed = $false
                    ads_changed = $false })
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
        $semanticRewrite = 'none'
        $semanticProjectionStatus = 'not-applicable'
        $semanticCandidateEligible = $false
        $semanticAdvisoryEligible = $false
        $semanticVolatileClasses = @()
        $semanticUnknownChanges = 0
        $semanticFatalChanges = 0
        $semanticNonMonotonicChanges = 0
        $semanticChangedLeafCount = 0
        $semanticChangedPathSetSha256 = $null
        $beforeSemanticProperty = $beforeEntry.PSObject.Properties[
            'semantic_projection']
        $afterSemanticProperty = $afterEntry.PSObject.Properties[
            'semantic_projection']
        if ([string]$beforeEntry.scope -ceq 'steam_library_metadata' -and
            ($fields -contains 'sha256' -or $fields -contains 'size' -or
             $fields -contains 'identity') -and
            $null -ne $beforeSemanticProperty -and
            $null -ne $afterSemanticProperty -and
            (([string]$beforeSemanticProperty.Value.entry_class -ceq
                    'global_steam_user_config') -or
             ([string]$afterSemanticProperty.Value.entry_class -ceq
                    'global_steam_user_config')) -and
            (Get-Command Compare-StockSteamUserConfigProjection `
                -ErrorAction SilentlyContinue)) {
            $semantic = Compare-StockSteamUserConfigProjection `
                $beforeSemanticProperty.Value $afterSemanticProperty.Value
            $semanticRewrite = 'observed'
            $semanticProjectionStatus = [string]$semantic.status
            $semanticCandidateEligible = [bool]$semantic.candidate_eligible
            $semanticAdvisoryEligible = [bool]$semantic.eligible
            $semanticVolatileClasses = @($semantic.volatile_classes)
            $semanticUnknownChanges = [int]$semantic.unknown_changes
            $semanticFatalChanges = [int]$semantic.fatal_changes
            $semanticNonMonotonicChanges =
                [int]$semantic.non_monotonic_changes
            $semanticChangedLeafCount = [int]$semantic.changed_leaf_count
            $semanticChangedPathSetSha256 =
                [string]$semantic.changed_path_set_sha256
        }
        [void]$changes.Add([pscustomobject]@{
                scope = [string]$beforeEntry.scope
                relative_path = [string]$beforeEntry.relative_path
                kind = $kind
                changed_fields = @($fields)
                identity_changed = $fields -contains 'identity'
                content_digest_changed = $fields -contains 'sha256'
                size_changed = $fields -contains 'size'
                last_write_changed = $fields -contains 'last_write_ticks'
                creation_time_changed = $fields -contains 'creation_ticks'
                attributes_changed = $fields -contains 'attributes'
                entry_kind_changed = $fields -contains 'entry_kind'
                presence_changed = $false
                reparse_changed = $fields -contains 'reparse_status'
                ads_changed = $fields -contains 'ads_status'
                steam_user_config_rewrite = $semanticRewrite
                steam_user_config_projection = $semanticProjectionStatus
                steam_user_config_candidate_eligible =
                    $semanticCandidateEligible
                steam_user_config_advisory_eligible =
                    $semanticAdvisoryEligible
                steam_user_config_volatile_classes =
                    $semanticVolatileClasses
                steam_user_config_unknown_changes =
                    $semanticUnknownChanges
                steam_user_config_fatal_changes =
                    $semanticFatalChanges
                steam_user_config_non_monotonic_changes =
                    $semanticNonMonotonicChanges
                steam_user_config_changed_leaf_count =
                    $semanticChangedLeafCount
                steam_user_config_changed_path_set_sha256 =
                    $semanticChangedPathSetSha256 })
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
    # These dimensions are deliberately orthogonal.  A replace-by-rename may
    # change identity, digest, size, and timestamps in the same observation;
    # its primary compatibility kind must not hide any of those facts.
    $digest = @($changes | Where-Object content_digest_changed).Count
    $size = @($changes | Where-Object size_changed).Count
    $identity = @($changes | Where-Object {
            $_.identity_changed -or $_.entry_kind_changed }).Count
    $timestamp = @($changes | Where-Object {
            $_.last_write_changed -or $_.creation_time_changed }).Count
    $content = @($changes | Where-Object {
            $_.content_digest_changed -or $_.size_changed }).Count
    $metadata = @($changes | Where-Object {
            $_.last_write_changed -or $_.creation_time_changed -or
            $_.attributes_changed -or
            ($_.kind -ceq 'directory_metadata_changed') }).Count
    $steamRewrites = @($changes | Where-Object {
            $property = $_.PSObject.Properties['steam_user_config_rewrite']
            $null -ne $property -and [string]$property.Value -ceq 'observed'
        })
    $steamProjection = if ($steamRewrites.Count -eq 0) { 'none' }
        elseif (@($steamRewrites | Where-Object {
                    $_.steam_user_config_projection -cne 'match' }).Count -eq 0) {
            'match'
        } elseif (@($steamRewrites | Where-Object {
                    $_.steam_user_config_projection -ceq 'incomplete' }).Count -ne 0) {
            'incomplete'
        } else { 'mismatch' }
    $volatileClassSet = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($rewrite in $steamRewrites) {
        foreach ($class in @($rewrite.steam_user_config_volatile_classes)) {
            [void]$volatileClassSet.Add([string]$class)
        }
    }
    $semanticUnknown = 0
    $semanticFatal = 0
    $semanticNonMonotonic = 0
    foreach ($rewrite in $steamRewrites) {
        $semanticUnknown += [int]$rewrite.steam_user_config_unknown_changes
        $semanticFatal += [int]$rewrite.steam_user_config_fatal_changes
        $semanticNonMonotonic +=
            [int]$rewrite.steam_user_config_non_monotonic_changes
    }
    $promotedRewriteCount = @($steamRewrites | Where-Object {
            [bool]$_.steam_user_config_advisory_eligible }).Count
    $criticalChanges = [Collections.Generic.List[object]]::new()
    foreach ($change in $changes) {
        $isPromotedRewrite = $change.PSObject.Properties[
                'steam_user_config_advisory_eligible'] -and
            [bool]$change.steam_user_config_advisory_eligible
        $isCompanionDirectoryMetadata = $promotedRewriteCount -eq 1 -and
            [string]$change.scope -ceq 'steam_library_metadata' -and
            [string]$change.kind -ceq 'directory_metadata_changed' -and
            -not [bool]$change.presence_changed -and
            -not [bool]$change.reparse_changed -and
            -not [bool]$change.ads_changed
        if (-not $isPromotedRewrite -and -not $isCompanionDirectoryMetadata) {
            [void]$criticalChanges.Add($change)
        }
    }
    $criticalResult = if ($unreadable -ne 0) { 'incomplete' }
        elseif ($criticalChanges.Count -eq 0) { 'none' } else { 'changed' }
    return [pscustomobject]@{
        schema = 'hlclient.stock-external-drift.v1'
        phase = $Phase
        before_manifest_sha256 = [string]$Before.manifest_sha256
        after_manifest_sha256 = [string]$After.manifest_sha256
        unchanged_entries = $unchanged
        content_changes = $content
        metadata_only_changes = $metadata
        metadata_changes = $metadata
        digest_changes = $digest
        size_changes = $size
        identity_replacements = $identity
        timestamp_changes = $timestamp
        created = @($changes | Where-Object kind -ceq 'created').Count
        removed = @($changes | Where-Object kind -ceq 'removed').Count
        unreadable = $unreadable
        changed_scopes = $scopeSet.Count
        scope_kinds = $scopeKinds
        changes = @($changes)
        critical_external_drift = $criticalResult
        steam_user_config_rewrite = $(if ($steamRewrites.Count -eq 0) {
                'none'
            } else { 'observed' })
        steam_user_config_rewrite_count = $steamRewrites.Count
        steam_user_config_projection = $steamProjection
        steam_user_config_volatile_classes = $volatileClassSet.Count
        steam_user_config_unknown_changes = [int]$semanticUnknown
        steam_user_config_fatal_changes = [int]$semanticFatal
        steam_user_config_non_monotonic_changes =
            [int]$semanticNonMonotonic
        steam_user_config_candidate_count = @($steamRewrites | Where-Object {
                [bool]$_.steam_user_config_candidate_eligible }).Count
        steam_user_config_advisory_count = $promotedRewriteCount
        result = $criticalResult
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
            @('digest-changes', $Difference.digest_changes),
            @('size-changes', $Difference.size_changes),
            @('identity-replacements', $Difference.identity_replacements),
            @('timestamp-changes', $Difference.timestamp_changes),
            @('critical-external-drift',
                $Difference.critical_external_drift),
            @('steam-user-config-rewrite',
                $Difference.steam_user_config_rewrite),
            @('steam-user-config-projection',
                $Difference.steam_user_config_projection),
            @('steam-user-config-volatile-classes',
                $Difference.steam_user_config_volatile_classes),
            @('steam-user-config-unknown-changes',
                $Difference.steam_user_config_unknown_changes),
            @('steam-user-config-fatal-changes',
                $Difference.steam_user_config_fatal_changes),
            @('steam-user-config-non-monotonic-changes',
                $Difference.steam_user_config_non_monotonic_changes),
            @('created', $Difference.created),
            @('removed', $Difference.removed),
            @('unreadable', $Difference.unreadable),
            @('result', $Difference.result))) {
        Write-Output "[stock-drift] $($entry[0])=$($entry[1])"
    }
}
