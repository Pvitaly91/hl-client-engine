# Narrow policy extension of the existing restoration guard. No process launch.
$script:ManualFileWork = [ordered]@{ copied_bytes=[long]0; hashed_bytes=[long]0; tree_scans=0 }
$script:ManualClock = [Diagnostics.Stopwatch]::StartNew()
$script:ManualPhase = 'preflight'

function Write-ManualPhase {
    param([string]$Phase)
    $script:ManualPhase = $Phase
    Write-Host "[manual-launch] phase=$Phase elapsed_ms=$($script:ManualClock.ElapsedMilliseconds) clock=wrapper"
}

function Get-FastManagedNames {
    @('valve/liblist.gam', 'valve/addons/metamod/config.ini',
      'valve/addons/metamod/hlclient_test50_plugins.ini',
      'valve/addons/metamod/hlclient_test50_empty.cfg',
      'valve/addons/metamod/hlclient_test50_metamod.dll')
}

function Get-FastManagedByteLimit {
    param([string]$Relative)
    if ($Relative -cnotin @(Get-FastManagedNames)) { throw 'fast_scope_path_not_owned' }
    # The sole staged binary is 226304 bytes. Do not widen config/recovery
    # bounds globally or permit arbitrary DLLs through this exception.
    if ($Relative -ceq 'valve/addons/metamod/hlclient_test50_metamod.dll') { return 262144 }
    return 65536
}

function Assert-FastManagedFile {
    param([string]$Root, [string]$Relative)
    $limit = Get-FastManagedByteLimit $Relative
    $path = Join-Path $Root $Relative
    Assert-NoReparsePointInExistingPath $path 'fast managed file'
    if (Test-Path -LiteralPath $path) {
        $item = Get-Item -LiteralPath $path -Force
        if ($item.PSIsContainer -or $item.Length -gt $limit) { throw 'fast_managed_file_shape' }
        Assert-OnlyDefaultDataStream $path 'fast managed file'
        Assert-NoHardLink $path 'fast managed file'
    }
    return $path
}

function Assert-FastResearchPreparation {
    param([string]$Root)
    # The enclosing Resolve-IsolatedResearchRoot still validates physical root
    # separation, marker bytes, selected BSP and critical launch identities.
    $manifest = Read-BoundedJson (Join-Path $Root $preparationManifestName) 32768 'fast preparation marker'
    if ($manifest.schema -cne 'hlclient.stock-runtime-research-preparation.v3' -or
        $manifest.preparation_status -cne 'exact-materialized-copy-verified') {
        throw 'fast_research_not_prepared: prepare the research installation explicitly'
    }
    Assert-ResearchPendingMarker $Root
    foreach ($relative in @('steam_appid.txt','valve/dlls/hl.dll','valve/liblist.gam')) {
        $path = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "fast_required_file_missing: $relative" }
        Assert-NoReparsePointInExistingPath $path 'fast required file'
        Assert-OnlyDefaultDataStream $path 'fast required file'
        Assert-NoHardLink $path 'fast required file'
    }
    if ((Read-BoundedAsciiMarker (Join-Path $Root 'steam_appid.txt')).Trim() -cne '70') {
        throw 'fast_appid_invalid'
    }
    return [pscustomobject]@{ Schema=$manifest.schema; ExternalTargetProfile='not_attested_fast';
        ExternalTargetCount=0; InventoryStatus='trusted_prepared_installation_full_content_not_verified' }
}

function Get-FastTestHealthPlan {
    param([string]$Root, [object]$Components)
    $plans = [ordered]@{}
    # This check also applies when HP assistance is OFF: never silently load
    # an old helper/custom gamedll left by a different kind of transaction.
    # The caller recovers an owned Fast journal before reaching this point.
    $liblist = Assert-FastManagedFile $Root 'valve/liblist.gam'
    $content = [IO.File]::ReadAllText($liblist)
    $pattern = '(?m)^gamedll\s+"dlls[\\/]hl\.dll"\s*\r?$'
    if ([regex]::Matches($content,$pattern).Count -ne 1 -or
        [regex]::Matches($content,'(?m)^\s*gamedll\s+').Count -ne 1) {
        throw 'fast_original_gamedll_invalid: an unrelated profile must be restored explicitly'
    }
    if ($null -eq $Components) { return $plans }
    foreach ($path in @($Components.Meta,$Components.Helper)) {
        Assert-NoReparsePointInExistingPath $path 'prepared profile component'
        Assert-OnlyDefaultDataStream $path 'prepared profile component'
        Assert-NoHardLink $path 'prepared profile component'
    }
    # Stock swds rejects absolute game-DLL paths. Stage only the verified
    # prepared Metamod bytes inside the same scoped transaction. The health
    # plugin itself remains at its prepared path, interpreted by Metamod.
    # No absolute -dll on HLDS argv either: -steam inside HLC-steamcfg selects
    # GUI/AdminServer through the launcher's substring option parser.
    $metaRelative = 'valve/addons/metamod/hlclient_test50_metamod.dll'
    $meta = 'addons/metamod/hlclient_test50_metamod.dll'
    $helper = ([IO.Path]::GetFullPath($Components.Helper)).Replace('\','/')
    if ($helper -match '[\s"]' -or $helper.Length -gt 240) {
        throw 'fast_test50_component_path_not_representable: prepared DLL paths must be short and whitespace-free'
    }
    # Lock and bound the bytes actually staged; a changed source between the
    # component receipt check and this read must not become executable input.
    $stream = [IO.File]::Open($Components.Meta,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
        if ($stream.Length -le 0 -or $stream.Length -gt (Get-FastManagedByteLimit $metaRelative)) {
            throw 'fast_test50_metamod_size_bound'
        }
        $reader = [IO.BinaryReader]::new($stream)
        try { $metaBytes = $reader.ReadBytes([int]$stream.Length) } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $metaHash = [Convert]::ToHexString($sha.ComputeHash($metaBytes)) } finally { $sha.Dispose() }
    $script:ManualFileWork.hashed_bytes += $metaBytes.Length
    if ($Components.MetaSha -cnotmatch '^[0-9A-F]{64}$' -or $metaHash -cne $Components.MetaSha) {
        throw 'fast_test50_metamod_source_changed'
    }
    $plans['valve/liblist.gam'] = [Text.UTF8Encoding]::new($false).GetBytes(
        [regex]::Replace($content,$pattern,"gamedll `"$meta`""))
    $plans['valve/addons/metamod/config.ini'] = [Text.Encoding]::ASCII.GetBytes(
        "gamedll dlls/hl.dll`nplugins_file addons/metamod/hlclient_test50_plugins.ini`nexec_cfg addons/metamod/hlclient_test50_empty.cfg`n")
    $plans['valve/addons/metamod/hlclient_test50_plugins.ini'] = [Text.Encoding]::ASCII.GetBytes("win32 $helper`n")
    $plans['valve/addons/metamod/hlclient_test50_empty.cfg'] = [Text.Encoding]::ASCII.GetBytes("// Test profile: no automatic commands.`n")
    $plans[$metaRelative] = $metaBytes
    return $plans
}

function Get-ScopedResearchSnapshot {
    param([string]$Root, [System.Collections.IDictionary]$Plans)
    $entries = @(); [long]$total = 0
    foreach ($relative in $Plans.Keys) {
        $path = Assert-FastManagedFile $Root $relative
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $item = Get-Item -LiteralPath $path -Force
        $entries += [pscustomobject]@{ RelativePath=$relative; Kind='file'; Length=[long]$item.Length;
            Sha256=(Get-FileSha256 $path); CreationTicks=$item.CreationTimeUtc.Ticks;
            WriteTicks=$item.LastWriteTimeUtc.Ticks; Attributes=[long]$item.Attributes }
        $total += $item.Length
    }
    # Fingerprint covers only this explicit list, never the tree.
    $bytes = [Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject @($entries) -Compress))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest=[Convert]::ToHexString($sha.ComputeHash($bytes)) } finally { $sha.Dispose() }
    [pscustomobject]@{ Entries=@($entries); EntryCount=$entries.Count; TotalBytes=$total; ManifestSha256=$digest }
}

function Restore-ScopedResearchState {
    param([object]$Guard)
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    Assert-RestorationDirectoryCapabilities $Guard
    foreach ($entry in $Guard.ScopedRecord.entries) {
        if ($deadline.Elapsed.TotalSeconds -gt 30) { throw 'scoped_restoration_timeout' }
        $target = Assert-FastManagedFile $Guard.Root $entry.relative
        $original = @($Guard.Before.Entries | Where-Object RelativePath -CEQ $entry.relative)
        if ($original.Count -gt 1) { throw 'scoped_duplicate_before_entry' }
        $current = if (Test-Path -LiteralPath $target) { Get-FileSha256 $target } else { $null }
        $beforeHash = if ($original.Count) { $original[0].Sha256 } else { $null }
        if ($null -ne $current -and $current -cne $entry.after_sha -and $current -cne $beforeHash) {
            throw "scoped_foreign_change_preserved: $($entry.relative)"
        }
        if ($original.Count) {
            $before = $original[0]
            $source = Join-Path $Guard.DataRoot $entry.relative
            Assert-NoReparsePointInExistingPath $source 'scoped recovery bytes'
            Assert-OnlyDefaultDataStream $source 'scoped recovery bytes'
            Assert-NoHardLink $source 'scoped recovery bytes'
            if ((Get-FileSha256 $source) -cne $before.Sha256) { throw 'scoped_backup_changed' }
            Assert-RestorationDirectoryCapabilities $Guard
            if (Test-Path -LiteralPath $target) { Remove-SafeEntry $target $Guard.Root }
            [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
            [IO.File]::Copy($source,$target,$false)
            $script:ManualFileWork.copied_bytes += $before.Length
            $item=Get-Item -LiteralPath $target -Force
            $item.CreationTimeUtc=[DateTime]::new([long]$before.CreationTicks,[DateTimeKind]::Utc)
            $item.LastWriteTimeUtc=[DateTime]::new([long]$before.WriteTicks,[DateTimeKind]::Utc)
            $item.Attributes=[IO.FileAttributes]([long]$before.Attributes)
            $verified=Get-Item -LiteralPath $target -Force
            if ((Get-FileSha256 $target) -cne $before.Sha256 -or
                $verified.CreationTimeUtc.Ticks -ne $before.CreationTicks -or
                $verified.LastWriteTimeUtc.Ticks -ne $before.WriteTicks -or
                [long]$verified.Attributes -ne $before.Attributes) { throw 'scoped_restore_bytes_or_metadata_mismatch' }
        } elseif ($null -ne $current) {
            Assert-RestorationDirectoryCapabilities $Guard
            Remove-SafeEntry $target $Guard.Root
        }
    }
    foreach ($relative in @($Guard.ScopedRecord.created_directories | Sort-Object Length -Descending)) {
        if ($relative -cnotin @('valve/addons','valve/addons/metamod')) { throw 'scoped_directory_not_owned' }
        $path=Join-Path $Guard.Root $relative
        Assert-NoReparsePointInExistingPath $path 'scoped directory'
        if ((Test-Path -LiteralPath $path) -and @(Get-ChildItem -LiteralPath $path -Force).Count -eq 0) {
            [IO.Directory]::Delete($path,$false) # Never recursively remove foreign content.
        }
    }
    Assert-RestorationDirectoryCapabilities $Guard
    return $Guard.Before
}

function Assert-NoActiveManualProcesses {
    param([string]$Root)
    $images=@((Join-Path $Root 'hlds.exe'),
        (Join-Path $repositoryRoot 'build/bin/Release/hlclient.exe'),
        (Join-Path $repositoryRoot 'build/bin/Release/hlclient_stock_runtime_orchestrator.exe')) |
        ForEach-Object { [IO.Path]::GetFullPath($_) }
    $active=@(Get-CimInstance Win32_Process -Filter "Name='hlds.exe' OR Name='hlclient.exe' OR Name='hlclient_stock_runtime_orchestrator.exe'" -OperationTimeoutSec 10 -ErrorAction Stop | Where-Object {
        $_.ExecutablePath -and $images -icontains [IO.Path]::GetFullPath($_.ExecutablePath) })
    if ($active.Count) { throw 'fast_previous_native_process_still_active' }
}

function Open-FastManualLease {
    param([string]$Root)
    Assert-NoActiveManualProcesses $Root
    $path=Join-Path $Root '.hlclient-manual-fast.pending.json'
    Assert-NoReparsePointInExistingPath $path 'fast transaction marker'
    $existed=Test-Path -LiteralPath $path
    if ($existed) {
        Assert-OnlyDefaultDataStream $path 'fast transaction marker'
        Assert-NoHardLink $path 'fast transaction marker'
    }
    try { $stream=[IO.File]::Open($path,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::Read) }
    catch { throw 'fast_transaction_busy_or_inaccessible: another wrapper owns the research installation' }
    try {
        if ($existed) {
            if ($stream.Length -eq 0 -or $stream.Length -gt 65536) { throw 'fast_pending_marker_invalid' }
            $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8,$true,1024,$true)
            try { $record=$reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
            if ($record.schema -cne 'hlclient.scoped-restoration.v1' -or
                $record.root -ine $Root -or $record.run_id -cnotmatch '^[0-9a-f]{32}$' -or
                $record.backup -ine (Join-Path ([IO.Path]::GetTempPath()) ('hlclient-stock-runtime-restore-'+$record.run_id)) -or
                $record.root_creation_ticks -ne (Get-Item -LiteralPath $Root).CreationTimeUtc.Ticks -or
                $record.marker_sha -cne (Get-FileSha256 (Join-Path $Root $markerName)) -or
                @($record.entries).Count -gt 5 -or @($record.before.Entries).Count -gt 5 -or
                @($record.created_directories).Count -gt 2) {
                throw 'fast_pending_ownership_mismatch'
            }
            $owner=Get-Process -Id $record.owner_pid -ErrorAction SilentlyContinue
            if ($null -ne $owner -and $owner.StartTime.ToUniversalTime().Ticks -eq $record.owner_started_ticks) {
                throw 'fast_previous_owner_still_active'
            }
            # A wrapper crash can leave native children alive temporarily. Do
            # not restore underneath them and never kill processes by name.
            Assert-NoActiveManualProcesses $Root
            $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            foreach ($entry in $record.entries) {
                [void](Assert-FastManagedFile $Root $entry.relative)
                if (-not $seen.Add($entry.relative) -or $entry.after_sha -cnotmatch '^[0-9A-F]{64}$') { throw 'fast_pending_entries_invalid' }
            }
            $beforeSeen=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            foreach ($entry in $record.before.Entries) {
                if (-not $seen.Contains($entry.RelativePath) -or $entry.Kind -cne 'file' -or
                    -not $beforeSeen.Add($entry.RelativePath) -or
                    $entry.CreationTicks -lt 0 -or $entry.WriteTicks -lt 0 -or
                    $entry.CreationTicks -gt [DateTime]::MaxValue.Ticks -or $entry.WriteTicks -gt [DateTime]::MaxValue.Ticks -or
                    ([long]$entry.Attributes -band ([long][IO.FileAttributes]::ReparsePoint -bor [long][IO.FileAttributes]::Directory)) -ne 0 -or
                    $entry.Length -lt 0 -or $entry.Length -gt (Get-FastManagedByteLimit $entry.RelativePath) -or
                    $entry.Sha256 -cnotmatch '^[0-9A-F]{64}$') {
                    throw 'fast_pending_backup_entry_invalid'
                }
            }
            $data=Join-Path $record.backup 'data'
            Assert-NoReparsePointInExistingPath $data 'fast retained backup'
            $guard=[pscustomobject]@{Root=$Root; TemporaryRoot=$record.backup; DataRoot=$data; Before=$record.before;
                ScopedRecord=$record; ResearchDirectoryCapability=$null; BackupRootDirectoryCapability=$null; BackupDataDirectoryCapability=$null}
            try {
                $guard.ResearchDirectoryCapability=New-RetainedDirectoryCapability $Root (Join-Path $Root $markerName) 'research root'
                $guard.BackupRootDirectoryCapability=New-RetainedDirectoryCapability $record.backup $data 'backup root'
                $guard.BackupDataDirectoryCapability=New-RetainedDirectoryCapability $data (Join-Path $data '.hlclient-restoration-identity-lock') 'backup data'
                [void](Restore-ScopedResearchState $guard)
                Write-Host "[manual-launch] recovered_run=$($record.run_id) scope=managed_mutable_files"
            } finally { Close-RestorationGuardCapabilities $guard }
        }
        return [pscustomobject]@{Path=$path; Stream=$stream; Existed=$existed}
    } catch { $stream.Dispose(); throw }
}

function New-FastManualGuard {
    param([string]$Root, [string]$RunId, [System.Collections.IDictionary]$Plans, [object]$Lease)
    $before=Get-ScopedResearchSnapshot $Root $Plans
    $guard=New-RestorationGuard $Root $before -BackupId $RunId
    try {
    $entries=@(); $created=@()
    foreach ($relative in $Plans.Keys) {
        if ($Plans[$relative].Length -gt (Get-FastManagedByteLimit $relative)) { throw 'fast_planned_bytes_bound' }
        $sha=[Security.Cryptography.SHA256]::Create()
        try { $digest=[Convert]::ToHexString($sha.ComputeHash([byte[]]$Plans[$relative])) } finally { $sha.Dispose() }
        $entries += [pscustomobject]@{relative=$relative;after_sha=$digest}
    }
    if ($Plans.Count) {
        foreach ($relative in @('valve/addons','valve/addons/metamod')) {
            if (-not (Test-Path -LiteralPath (Join-Path $Root $relative))) { $created += $relative }
        }
    }
    $record=[pscustomobject]@{schema='hlclient.scoped-restoration.v1'; root=$Root; run_id=$RunId;
        owner_pid=$PID; owner_started_ticks=(Get-Process -Id $PID).StartTime.ToUniversalTime().Ticks;
        root_creation_ticks=(Get-Item -LiteralPath $Root).CreationTimeUtc.Ticks;
        marker_sha=(Get-FileSha256 (Join-Path $Root $markerName)); backup=$guard.TemporaryRoot;
        before=$before; entries=@($entries); created_directories=@($created)}
    $bytes=[Text.Encoding]::UTF8.GetBytes(($record | ConvertTo-Json -Depth 10 -Compress))
    Assert-RestorationDirectoryCapabilities $guard
    # Retain the small recovery record with its exact UUID backup, also after
    # successful completion. The pending marker alone is removed on success.
    $journal=[IO.File]::Open((Join-Path $guard.TemporaryRoot 'scoped-transaction.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $journal.Write($bytes,0,$bytes.Length); $journal.Flush($true) } finally { $journal.Dispose() }
    $Lease.Stream.Position=0; $Lease.Stream.SetLength(0); $Lease.Stream.Write($bytes,0,$bytes.Length); $Lease.Stream.Flush($true)
    $guard | Add-Member NoteProperty ScopedRecord $record
    $guard | Add-Member NoteProperty Lease $Lease
    return $guard
    } catch { Close-RestorationGuardCapabilities $guard; throw }
}

function Set-FastManagedFiles {
    param([object]$Guard, [System.Collections.IDictionary]$Plans)
    foreach ($relative in $Plans.Keys) {
        Assert-RestorationDirectoryCapabilities $Guard
        $path=Assert-FastManagedFile $Guard.Root $relative
        $before=@($Guard.Before.Entries | Where-Object RelativePath -CEQ $relative)
        if (($before.Count -eq 0 -and (Test-Path -LiteralPath $path)) -or
            ($before.Count -ne 0 -and (-not (Test-Path -LiteralPath $path) -or
             (Get-FileSha256 $path) -cne $before[0].Sha256))) { throw 'fast_preparation_foreign_change_preserved' }
        [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
        [IO.File]::WriteAllBytes($path,[byte[]]$Plans[$relative])
        if ($relative -ceq 'valve/addons/metamod/hlclient_test50_metamod.dll') {
            $script:ManualFileWork.copied_bytes += $Plans[$relative].Length
            $entry = @($Guard.ScopedRecord.entries | Where-Object relative -CEQ $relative)
            if ($entry.Count -ne 1 -or (Get-FileSha256 $path) -cne $entry[0].after_sha) {
                throw 'fast_test50_metamod_staged_hash_mismatch'
            }
        }
    }
}

function Close-FastManualLease {
    param([object]$Lease, [bool]$Restored)
    if ($null -eq $Lease) { return }
    $Lease.Stream.Dispose()
    if ($Restored) {
        Assert-NoReparsePointInExistingPath $Lease.Path 'completed fast marker'
        Assert-NoHardLink $Lease.Path 'completed fast marker'
        [IO.File]::Delete($Lease.Path)
    }
}
