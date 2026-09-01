#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureScriptPath = '.\scripts\capture_stock_runtime_state.ps1',

    [Parameter()]
    [AllowEmptyString()]
    [string]$WalkerScriptPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$capture = [IO.Path]::GetFullPath($CaptureScriptPath)
if (-not (Test-Path -LiteralPath $capture -PathType Leaf)) {
    throw 'Capture wrapper is unavailable.'
}
$scriptDirectory = [IO.Path]::GetFullPath(
    (Split-Path -Parent $MyInvocation.MyCommand.Path)).TrimEnd('\', '/')
if ([string]::IsNullOrWhiteSpace($WalkerScriptPath)) {
    $WalkerScriptPath = Join-Path $scriptDirectory `
        'walk_stock_runtime_transport.ps1'
}
$walker = [IO.Path]::GetFullPath($WalkerScriptPath)
if (-not (Test-Path -LiteralPath $walker -PathType Leaf)) {
    throw 'Transport walker is unavailable.'
}
$repositoryRoot = [IO.Path]::GetFullPath(
    (Join-Path (Split-Path -Parent $walker) '..')).TrimEnd('\', '/')
$manualArtifacts = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts')).TrimEnd('\', '/')
$manualArtifactsCreated = -not (Test-Path -LiteralPath $manualArtifacts)
$walkerParent = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime')).TrimEnd('\', '/')
$walkerParentCreated = -not (Test-Path -LiteralPath $walkerParent)
$walkerFixture = [IO.Path]::GetFullPath((Join-Path $walkerParent (
    [Guid]::NewGuid().ToString('N'))))
$walkerCanaryParent = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime-canary')).TrimEnd('\', '/')
$walkerCanaryParentCreated = -not (Test-Path -LiteralPath $walkerCanaryParent)
$walkerCanaryFixture = [IO.Path]::GetFullPath((Join-Path $walkerCanaryParent (
    [Guid]::NewGuid().ToString('N'))))
$walkerNearParent = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime-canary-near')).TrimEnd('\', '/')
$walkerNearParentCreated = -not (Test-Path -LiteralPath $walkerNearParent)
$walkerNearFixture = [IO.Path]::GetFullPath((Join-Path $walkerNearParent (
    [Guid]::NewGuid().ToString('N'))))
$walkerReparseFixture = [IO.Path]::GetFullPath((Join-Path $walkerCanaryParent (
    [Guid]::NewGuid().ToString('N'))))
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$walkerOtherFixture = [IO.Path]::GetFullPath((Join-Path $temporaryRoot (
    [Guid]::NewGuid().ToString('N'))))
$fixture = [IO.Path]::GetFullPath((Join-Path $temporaryRoot (
    'hlclient-stock-runtime-manifest-test-' + [Guid]::NewGuid().ToString('N'))))
$v3HardlinkTarget = $null
$approvalReviewParent = [IO.Path]::GetFullPath((Join-Path `
        $manualArtifacts 'stock-runtime-source-review')).TrimEnd('\', '/')
$approvalReviewParentCreated = -not (Test-Path -LiteralPath $approvalReviewParent)
$approvalReviewRoot = [IO.Path]::GetFullPath((Join-Path `
        $approvalReviewParent ([Guid]::NewGuid().ToString('N'))))

function Get-TextSha256 {
    param([string]$Text, [switch]$Lower)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = ([BitConverter]::ToString($algorithm.ComputeHash(
                    [Text.UTF8Encoding]::new($false, $true).GetBytes($Text)))).Replace('-', '')
    } finally { $algorithm.Dispose() }
    if ($Lower) { return $hash.ToLowerInvariant() }
    return $hash
}

function Get-BytesSha256 {
    param([byte[]]$Bytes)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString(
                $algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    } finally { $algorithm.Dispose() }
}

function Initialize-TestAdsFixtureNative {
    if ($null -ne ('Hlclient.StockRuntimeTestAdsFixture' -as [type])) {
        return
    }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Hlclient
{
    public static class StockRuntimeTestAdsFixture
    {
        private const uint GenericWrite = 0x40000000;
        private const uint DeleteAccess = 0x00010000;
        private const uint FileShareRead = 0x1;
        private const uint FileShareWrite = 0x2;
        private const uint FileShareDelete = 0x4;
        private const uint CreateAlways = 2;
        private const uint OpenExisting = 3;
        private const uint FileFlagBackupSemantics = 0x02000000;
        private const int FileDispositionInfo = 4;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode,
            SetLastError = true)]
        private static extern IntPtr CreateFileW(
            string fileName, uint desiredAccess, uint shareMode,
            IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WriteFile(
            IntPtr file, byte[] buffer, uint bytesToWrite,
            out uint bytesWritten, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FlushFileBuffers(IntPtr file);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr file);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetFileInformationByHandle(
            IntPtr file, int informationClass,
            IntPtr information, uint bufferSize);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode,
            SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DeleteFileW(string path);

        public static void Write(string streamPath, byte[] bytes)
        {
            if (String.IsNullOrWhiteSpace(streamPath) || bytes == null ||
                bytes.Length < 1 || bytes.Length > 4096)
                throw new InvalidOperationException(
                    "ADS fixture write parameters are invalid.");
            IntPtr file = CreateFileW(
                streamPath, GenericWrite, 0, IntPtr.Zero, CreateAlways,
                FileFlagBackupSemantics, IntPtr.Zero);
            if (file == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "ADS fixture open failed.");
            try
            {
                uint written;
                if (!WriteFile(
                        file, bytes, (uint)bytes.Length, out written,
                        IntPtr.Zero) || written != bytes.Length ||
                    !FlushFileBuffers(file))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "ADS fixture write failed.");
            }
            finally
            {
                CloseHandle(file);
            }
        }

        public static void Delete(string streamPath)
        {
            if (DeleteFileW(streamPath)) return;
            int directError = Marshal.GetLastWin32Error();
            if (directError == 2 || directError == 3) return;
            IntPtr file = CreateFileW(
                streamPath, DeleteAccess,
                FileShareRead | FileShareWrite | FileShareDelete,
                IntPtr.Zero, OpenExisting, FileFlagBackupSemantics,
                IntPtr.Zero);
            if (file == InvalidHandle)
            {
                int error = Marshal.GetLastWin32Error();
                if (error == 2 || error == 3) return;
                throw new Win32Exception(error,
                    "ADS fixture cleanup open failed.");
            }
            IntPtr disposition = Marshal.AllocHGlobal(4);
            try
            {
                Marshal.WriteInt32(disposition, 1);
                if (!SetFileInformationByHandle(
                        file, FileDispositionInfo, disposition, 4))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "ADS fixture cleanup failed.");
            }
            finally
            {
                Marshal.FreeHGlobal(disposition);
                CloseHandle(file);
            }
        }
    }
}
'@
}

function Get-TestAdsPath {
    param([string]$Path, [string]$StreamName)
    if ([string]::IsNullOrWhiteSpace($Path) -or
        $StreamName -cnotmatch '^[A-Za-z0-9-]{1,128}$') {
        throw 'ADS fixture path parameters are invalid.'
    }
    return [IO.Path]::GetFullPath($Path) + ':' + $StreamName + ':$DATA'
}

function Set-TestAlternateDataStream {
    param([string]$Path, [string]$StreamName)
    Initialize-TestAdsFixtureNative
    [Hlclient.StockRuntimeTestAdsFixture]::Write(
        (Get-TestAdsPath $Path $StreamName),
        [Text.Encoding]::ASCII.GetBytes('mutation'))
}

function Remove-TestAlternateDataStream {
    param([string]$Path, [string]$StreamName)
    Initialize-TestAdsFixtureNative
    [Hlclient.StockRuntimeTestAdsFixture]::Delete(
        (Get-TestAdsPath $Path $StreamName))
}

function Reset-TestDirectoryAfterAdsProbe {
    param([string]$Path, [string]$SchemaLabel)
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $full)).TrimEnd('\', '/')
    $leaf = [IO.Path]::GetFileName($full)
    $resetLeaf = $leaf + '-ads-reset-' + $SchemaLabel
    $reset = [IO.Path]::GetFullPath((Join-Path $parent $resetLeaf)).TrimEnd('\', '/')
    if ($parent -ine $temporaryRoot -or
        $leaf -cnotmatch '^hlclient-stock-runtime-manifest-test-[0-9a-f]{32}$' -or
        $SchemaLabel -cnotmatch '^v[123]$' -or
        $resetLeaf -cnotmatch
            '^hlclient-stock-runtime-manifest-test-[0-9a-f]{32}-ads-reset-v[123]$' -or
        -not (Test-Path -LiteralPath $full -PathType Container) -or
        (Test-Path -LiteralPath $reset)) {
        throw 'ADS fixture directory reset target is invalid.'
    }
    $rootAttributes = [IO.File]::GetAttributes($full)
    if (($rootAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'ADS fixture directory reset root is a reparse point.'
    }

    [IO.Directory]::Move($full, $reset)
    [IO.Directory]::CreateDirectory($full) | Out-Null
    $children = @([IO.Directory]::GetFileSystemEntries($reset))
    if ($children.Count -gt 64) {
        throw 'ADS fixture directory reset entry bound was exceeded.'
    }
    foreach ($child in $children) {
        $childFull = [IO.Path]::GetFullPath($child)
        $childParent = [IO.Path]::GetFullPath(
            (Split-Path -Parent $childFull)).TrimEnd('\', '/')
        $childLeaf = [IO.Path]::GetFileName($childFull)
        $attributes = [IO.File]::GetAttributes($childFull)
        if ($childParent -ine $reset -or
            [string]::IsNullOrWhiteSpace($childLeaf) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'ADS fixture directory reset child is invalid.'
        }
        $destination = [IO.Path]::GetFullPath((Join-Path $full $childLeaf))
        if ((Split-Path -Parent $destination) -ine $full -or
            (Test-Path -LiteralPath $destination)) {
            throw 'ADS fixture directory reset destination is invalid.'
        }
        if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) {
            [IO.Directory]::Move($childFull, $destination)
        } else {
            [IO.File]::Move($childFull, $destination)
        }
    }
    if (@([IO.Directory]::GetFileSystemEntries($reset)).Count -ne 0) {
        throw 'ADS fixture directory reset source is not empty.'
    }
    [IO.Directory]::Delete($reset, $false)
}

function Invoke-ExpectedManifestResult {
    param([string]$ExpectedMessage)
    $message = ''
    try {
        & $capture -ValidateResearchRoot `
            -ResearchHalfLifeRoot $fixture `
            -ClientPath (Join-Path $fixture 'hl.exe') `
            -HldsPath (Join-Path $fixture 'hlds.exe') | Out-Null
    } catch { $message = $_.Exception.Message }
    if ($message -cnotmatch $ExpectedMessage) {
        throw "Research manifest gate returned an unexpected result: $message"
    }
}

function Write-Manifest {
    param([object]$Value)
    [IO.File]::WriteAllText(
        (Join-Path $fixture '.hlclient-research-preparation.json'),
        (($Value | ConvertTo-Json -Depth 5) + "`r`n"),
        [Text.UTF8Encoding]::new($false, $true))
}

function Invoke-ExpectedRootAdsRejection {
    param([string]$SchemaLabel)
    $streamName = 'hlclient-research-root-ads-' + $SchemaLabel
    try {
        # The stream is deliberately introduced only after the preparation
        # manifest has been published, modelling post-preparation mutation.
        Set-TestAlternateDataStream $fixture $streamName
        Invoke-ExpectedManifestResult `
            '^research root must contain only its default data stream\.$'
    } finally {
        Remove-TestAlternateDataStream $fixture $streamName
        # Windows PowerShell 5.1 cannot enumerate/remove directory ADS through
        # its FileSystem provider. Recreate this exact disposable GUID root
        # after the native probe so later schema checks start from an ordinary
        # directory with the same independently moved fixture leaves.
        Reset-TestDirectoryAfterAdsProbe $fixture $SchemaLabel
    }
}

function Write-WalkerJournalFixture {
    param([string]$JournalPath, [byte[]]$RawBytes)
    $entry = [ordered]@{
        schema = 'hlclient.stock-runtime-transport-journal.v1'
        observed_ordinal = 0
        direction = 'client_to_server'
        direction_ordinal = 1
        relative_timestamp_us = 0
        payload_byte_count = $RawBytes.Length
        raw_filename = '00000000-c2s.bin'
        source_role = 'research_client'
        destination_role = 'research_server'
        action = 'forward'
        hold_state = 'none'
        emitted_ordinals = @(0)
        delivered = $true
        wrong_source = $false
        sha256 = Get-BytesSha256 $RawBytes
    }
    [IO.File]::WriteAllText(
        $JournalPath, (($entry | ConvertTo-Json -Compress) + "`r`n"),
        [Text.UTF8Encoding]::new($false, $true))
}

function Invoke-WalkerExpectedFailure {
    param(
        [string]$ExpectedMessage,
        [string]$Root = $walkerFixture)
    $message = ''
    try {
        & $walker -CaptureRoot $Root | Out-Null
    } catch { $message = $_.Exception.Message }
    if ($message -cnotmatch $ExpectedMessage) {
        throw "Transport walker returned an unexpected result: $message"
    }
}

function Write-WalkerRootFixture {
    param([string]$Root, [byte[]]$RawBytes)
    [IO.Directory]::CreateDirectory((Join-Path $Root 'raw')) | Out-Null
    [IO.File]::WriteAllBytes(
        (Join-Path $Root 'raw\00000000-c2s.bin'), $RawBytes)
    Write-WalkerJournalFixture (Join-Path $Root 'transport-journal.jsonl') `
        $RawBytes
}

function Assert-WalkerRootAccepted {
    param([string]$Root, [string]$Label)
    $output = @(& $walker -CaptureRoot $Root)
    if ($output -cnotcontains '[stock-runtime-walk] result=success') {
        throw "Transport walker rejected its exact $Label fixture."
    }
}

try {
    if (-not $fixture.StartsWith(
            $temporaryRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Research manifest fixture escaped the temporary root.'
    }
    [IO.Directory]::CreateDirectory((Join-Path $fixture 'valve')) | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $fixture 'hl.exe'), [byte[]](0))
    [IO.File]::WriteAllBytes((Join-Path $fixture 'hlds.exe'), [byte[]](0))
    [IO.File]::WriteAllText(
        (Join-Path $fixture '.hlclient-research-isolated'),
        'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1',
        [Text.Encoding]::ASCII)

    $clientHash = (Get-FileHash -LiteralPath (Join-Path $fixture 'hl.exe') `
        -Algorithm SHA256).Hash
    $serverHash = (Get-FileHash -LiteralPath (Join-Path $fixture 'hlds.exe') `
        -Algorithm SHA256).Hash
    $v1Records = @(
        'd|valve',
        ('f|hl.exe|1|{0}' -f $clientHash),
        ('f|hlds.exe|1|{0}' -f $serverHash)) | Sort-Object
    $v1 = [ordered]@{
        schema = 'hlclient.stock-runtime-research-preparation.v1'
        marker = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
        source_inventory_entries = 3
        source_inventory_bytes = 2
        source_inventory_sha256 = Get-TextSha256 ($v1Records -join "`n")
        client_sha256 = $clientHash
        server_launcher_sha256 = $serverHash
        paths_recorded = $false
        preparation_status = 'exact-copy-verified'
    }
    Write-Manifest $v1
    Invoke-ExpectedRootAdsRejection 'v1'
    Invoke-ExpectedManifestResult '^stock client version is not accepted'

    $preparationPath = Join-Path $fixture '.hlclient-research-preparation.json'
    $preparationStreamName = 'hlclient-retained-read-mutation'
    try {
        Set-TestAlternateDataStream $preparationPath $preparationStreamName
        Invoke-ExpectedManifestResult `
            '^research preparation manifest retained-handle JSON read failed: Bounded retained-handle read requires only the default data stream\.$'
    } finally {
        Remove-TestAlternateDataStream $preparationPath $preparationStreamName
    }

    $oversizedManifest = [IO.File]::Open(
        $preparationPath, [IO.FileMode]::Create,
        [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $oversizedManifest.SetLength(32769) } finally {
        $oversizedManifest.Dispose()
    }
    Invoke-ExpectedManifestResult `
        '^research preparation manifest retained-handle JSON read failed: Bounded retained-handle read size is invalid\.$'
    Write-Manifest $v1

    $preparationHardlinkTarget = Join-Path $fixture `
        '.hlclient-preparation-hardlink-target.json'
    [IO.File]::Copy($preparationPath, $preparationHardlinkTarget, $true)
    [IO.File]::Delete($preparationPath)
    try {
        New-Item -ItemType HardLink -Path $preparationPath `
            -Target $preparationHardlinkTarget -ErrorAction Stop | Out-Null
        Invoke-ExpectedManifestResult `
            '^research preparation manifest retained-handle JSON read failed: Atomic publication file identity is invalid\.$'
    } finally {
        [IO.File]::Delete($preparationPath)
        [IO.File]::Delete($preparationHardlinkTarget)
        Write-Manifest $v1
    }

    $v1.source_inventory_sha256 = 'A' * 64
    Write-Manifest $v1
    Invoke-ExpectedManifestResult '^Research preparation manifest v1 inventory disagrees'

    [string[]]$v2Paths = @('valve', 'hl.exe', 'hlds.exe')
    [string[]]$v2Records = @(
        'd|valve',
        ('f|hl.exe|1|{0}' -f $clientHash.ToLowerInvariant()),
        ('f|hlds.exe|1|{0}' -f $serverHash.ToLowerInvariant()))
    [string[]]$legacyGroupedV2Records = @($v2Records)
    [Array]::Sort($legacyGroupedV2Records, [StringComparer]::Ordinal)
    [Array]::Sort($v2Paths, $v2Records, [StringComparer]::Ordinal)
    $legacyGroupedV2Sha256 = Get-TextSha256 `
        ((@($legacyGroupedV2Records) -join "`n") + "`n") -Lower
    $v2 = [ordered]@{
        schema = 'hlclient.stock-runtime-research-preparation.v2'
        marker = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
        topology_profile = @('ordinary_tree')
        source_root_identity_fingerprint = '0' * 64
        entry_count = 3
        byte_count = 2
        materialized_link_count = 0
        materialized_hardlink_count = 0
        rejected_link_count = 0
        inventory_sha256 = Get-TextSha256 `
            ((@($v2Records) -join "`n") + "`n") -Lower
        client_binary_private_identity_reference = '1' * 64
        server_binary_private_identity_reference = '2' * 64
        destination_unlinked_status = 'verified'
        source_unchanged_status = 'verified'
        paths_recorded = $false
        preparation_status = 'exact-materialized-copy-verified'
    }
    $pendingPath = Join-Path $fixture '.hlclient-research-pending'
    $pending = [ordered]@{
        schema = 'hlclient.stock-research-copy-pending.v1'
        category = 'awaiting_commit_marker'
        paths_recorded = $false
    }
    [IO.File]::WriteAllText(
        $pendingPath, (($pending | ConvertTo-Json) + "`r`n"),
        [Text.UTF8Encoding]::new($false, $true))
    Write-Manifest $v2
    Invoke-ExpectedRootAdsRejection 'v2'
    Invoke-ExpectedManifestResult '^stock client version is not accepted'

    $v2.inventory_sha256 = $legacyGroupedV2Sha256
    Write-Manifest $v2
    Invoke-ExpectedManifestResult `
        '^Research preparation manifest v2 inventory disagrees'
    $v2.inventory_sha256 = Get-TextSha256 `
        ((@($v2Records) -join "`n") + "`n") -Lower
    Write-Manifest $v2

    [IO.File]::Delete($pendingPath)
    Invoke-ExpectedManifestResult `
        '^Research preparation v2 lacks its pending/commit state marker'
    [IO.File]::WriteAllText(
        $pendingPath, (($pending | ConvertTo-Json) + "`r`n"),
        [Text.UTF8Encoding]::new($false, $true))

    $v2.inventory_sha256 = 'f' * 64
    Write-Manifest $v2
    Invoke-ExpectedManifestResult '^Research preparation manifest v2 inventory disagrees'

    $v2.inventory_sha256 = Get-TextSha256 `
        ((@($v2Records) -join "`n") + "`n") -Lower
    $v2.topology_profile = @('source_link_target_outside_root')
    Write-Manifest $v2
    Invoke-ExpectedManifestResult '^Research preparation topology profile is invalid'

    $v3InventorySha256 = Get-TextSha256 `
        ((@($v2Records) -join "`n") + "`n") -Lower
    $v3 = [ordered]@{
        schema = 'hlclient.stock-runtime-research-preparation.v3'
        marker = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
        preparation_profile = 'ordinary-or-contained-v3'
        source_root_identity_fingerprint = '3' * 64
        source_inventory_entries = 3
        source_inventory_bytes = 2
        source_inventory_sha256 = $v3InventorySha256
        contained_materialized_link_count = 0
        approved_external_materialized_link_count = 0
        source_hardlink_count = 0
        destination_entry_count = 3
        destination_byte_count = 2
        destination_inventory_sha256 = $v3InventorySha256
        destination_reparse_count = 0
        destination_hardlink_count = 0
        destination_ads_count = 0
        external_approval_sha256 = '0' * 64
        external_classification_summary = 'none'
        executable_target_count = 0
        mutable_state_target_count = 0
        source_unchanged_status = 'verified'
        external_targets_unchanged_status = 'verified'
        evidence_eligibility = 'eligible'
        external_target_profile = 'none'
        client_binary_private_identity_reference = '4' * 64
        server_binary_private_identity_reference = '5' * 64
        paths_recorded = $false
        preparation_status = 'exact-materialized-copy-verified'
    }
    Write-Manifest $v3
    Invoke-ExpectedRootAdsRejection 'v3'
    Invoke-ExpectedManifestResult '^stock client version is not accepted'

    $v3.source_inventory_sha256 = $legacyGroupedV2Sha256
    $v3.destination_inventory_sha256 = $legacyGroupedV2Sha256
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^Research preparation manifest v3 inventory disagrees with the tree'
    $v3.source_inventory_sha256 = $v3InventorySha256
    $v3.destination_inventory_sha256 = $v3InventorySha256

    $v3.source_inventory_sha256 = '8' * 64
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^Research preparation manifest v3 inventory disagrees with the tree'
    $v3.source_inventory_sha256 = $v3InventorySha256

    $v3.preparation_profile = 'reviewed-external-targets-v1'
    $v3.approved_external_materialized_link_count = 1
    [IO.Directory]::CreateDirectory($approvalReviewRoot) | Out-Null
    $approvalFixtureId = [IO.Path]::GetFileName($approvalReviewRoot)
    $approvalBytes = [Text.UTF8Encoding]::new($false, $true).GetBytes(
        ('{{"schema":"hlclient.stock-runtime-external-target-approval.v1",' +
            '"fixture":"{0}"}}' -f $approvalFixtureId))
    [IO.File]::WriteAllBytes(
        (Join-Path $approvalReviewRoot 'external-target-approval.json'),
        $approvalBytes)
    $v3.external_approval_sha256 = Get-BytesSha256 $approvalBytes
    $v3.external_classification_summary =
        'eligible_non_executable_asset_tree'
    $v3.external_target_profile = 'reviewed-non-executable-v1'
    $v3.preparation_status = 'exact-reviewed-materialized-copy-verified'
    Write-Manifest $v3
    Invoke-ExpectedManifestResult '^stock client version is not accepted'

    $v3.external_approval_sha256 = '6' * 64
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^Reviewed research copy approval digest is not backed by one exact local artifact'
    $v3.external_approval_sha256 = Get-BytesSha256 $approvalBytes

    $v3.evidence_eligibility = 'ineligible_external_code'
    Write-Manifest $v3
    Invoke-ExpectedManifestResult '^research_copy_not_evidence_eligible$'
    $v3.evidence_eligibility = 'eligible'

    $v3.external_approval_sha256 = '0' * 64
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^Research preparation manifest v3 reviewed profile is invalid'
    $v3.external_approval_sha256 = Get-BytesSha256 $approvalBytes

    $v3.destination_hardlink_count = 1
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^v3 destination hardlink count is outside its bound'
    $v3.destination_hardlink_count = 0

    $v3.source_unchanged_status = 'changed'
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^Research preparation manifest v3 policy is invalid'
    $v3.source_unchanged_status = 'verified'

    $v3.destination_inventory_sha256 = '7' * 64
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^Research preparation manifest v3 inventory disagrees with the tree'
    $v3.destination_inventory_sha256 = $v3InventorySha256

    $v3.client_binary_private_identity_reference = 'A' * 64
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^research preparation manifest v3 client_binary_private_identity_reference is not a private SHA-256 reference'
    $v3.client_binary_private_identity_reference = '4' * 64

    $v3.external_target_path = 'private-path-must-not-be-metadata'
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^research preparation manifest v3 property set is not exact'
    [void]$v3.Remove('external_target_path')

    [IO.File]::Delete($pendingPath)
    Write-Manifest $v3
    Invoke-ExpectedManifestResult `
        '^Research preparation v2 lacks its pending/commit state marker'
    [IO.File]::WriteAllText(
        $pendingPath, (($pending | ConvertTo-Json) + "`r`n"),
        [Text.UTF8Encoding]::new($false, $true))

    $v3ClientPath = Join-Path $fixture 'hl.exe'
    $v3ClientStream = 'hlclient-v3-destination-ads'
    Write-Manifest $v3
    try {
        Set-TestAlternateDataStream $v3ClientPath $v3ClientStream
        Invoke-ExpectedManifestResult `
            '^v3 research entry must contain only its default data stream\.$'
    } finally {
        Remove-TestAlternateDataStream $v3ClientPath $v3ClientStream
    }

    $v3HardlinkTarget = [IO.Path]::GetFullPath($fixture +
        '-v3-hardlink-target.bin')
    if ((Split-Path -Parent $v3HardlinkTarget) -ine $temporaryRoot -or
        [IO.Path]::GetFileName($v3HardlinkTarget) -cnotmatch
            '^hlclient-stock-runtime-manifest-test-[0-9a-f]{32}-v3-hardlink-target\.bin$' -or
        (Test-Path -LiteralPath $v3HardlinkTarget)) {
        throw 'V3 hardlink fixture target is invalid.'
    }
    [IO.File]::WriteAllBytes($v3HardlinkTarget, [byte[]](0))
    [IO.File]::Delete($v3ClientPath)
    try {
        New-Item -ItemType HardLink -Path $v3ClientPath `
            -Target $v3HardlinkTarget -ErrorAction Stop | Out-Null
        Write-Manifest $v3
        Invoke-ExpectedManifestResult '^v3 research file must not be linked\.$'
    } finally {
        if (Test-Path -LiteralPath $v3ClientPath) {
            [IO.File]::Delete($v3ClientPath)
        }
        [IO.File]::Copy($v3HardlinkTarget, $v3ClientPath, $false)
        [IO.File]::Delete($v3HardlinkTarget)
    }

    # Exercise the same retained native reader through the real walker entry
    # points. The oversized leaf is length-only (SetLength) so the reader must
    # reject it before allocating or invoking either JSON parser.
    [byte[]]$walkerRaw = [byte[]](0xff, 0xff, 0xff, 0xff, 0x70, 0x69, 0x6e, 0x67)
    Write-WalkerRootFixture $walkerFixture $walkerRaw
    $walkerRawPath = Join-Path $walkerFixture 'raw\00000000-c2s.bin'
    $walkerJournalPath = Join-Path $walkerFixture 'transport-journal.jsonl'
    Assert-WalkerRootAccepted $walkerFixture 'campaign-root'

    Write-WalkerRootFixture $walkerCanaryFixture $walkerRaw
    Assert-WalkerRootAccepted $walkerCanaryFixture 'canary-root'

    [IO.Directory]::CreateDirectory($walkerNearFixture) | Out-Null
    Invoke-WalkerExpectedFailure `
        '^CaptureRoot must be an exact run child of a repository stock-runtime campaign or canary root\.$' `
        $walkerNearFixture
    [IO.Directory]::CreateDirectory($walkerOtherFixture) | Out-Null
    Invoke-WalkerExpectedFailure `
        '^CaptureRoot must be an exact run child of a repository stock-runtime campaign or canary root\.$' `
        $walkerOtherFixture

    $walkerReparseCreated = $false
    try {
        try {
            New-Item -ItemType Junction -Path $walkerReparseFixture `
                -Target $walkerFixture -ErrorAction Stop | Out-Null
            $walkerReparseCreated = $true
        } catch {
            Write-Output '[stock-runtime-research-manifest-test] walker-root-reparse=capability-unavailable'
        }
        if ($walkerReparseCreated) {
            Invoke-WalkerExpectedFailure `
                '^capture run contains a reparse point\.$' $walkerReparseFixture
            Write-Output '[stock-runtime-research-manifest-test] walker-root-reparse=blocked'
        }
    } finally {
        if ($walkerReparseCreated -and
            (Test-Path -LiteralPath $walkerReparseFixture)) {
            $reparseParent = [IO.Path]::GetFullPath(
                (Split-Path -Parent $walkerReparseFixture)).TrimEnd('\', '/')
            $reparseLeaf = [IO.Path]::GetFileName($walkerReparseFixture)
            $reparseAttributes = [IO.File]::GetAttributes($walkerReparseFixture)
            if ($reparseParent -ine $walkerCanaryParent -or
                $reparseLeaf -cnotmatch '^[0-9a-f]{32}$' -or
                ($reparseAttributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
                throw 'Walker reparse fixture cleanup target is invalid.'
            }
            [IO.Directory]::Delete($walkerReparseFixture, $false)
        }
    }

    $oversized = [IO.File]::Open(
        $walkerJournalPath, [IO.FileMode]::Create,
        [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $oversized.SetLength(67108865) } finally { $oversized.Dispose() }
    Invoke-WalkerExpectedFailure `
        '^transport journal retained-handle read failed: Bounded native read size is invalid\.$'
    Write-WalkerJournalFixture $walkerJournalPath $walkerRaw

    $journalStreamName = 'hlclient-retained-read-mutation'
    try {
        Set-TestAlternateDataStream $walkerJournalPath $journalStreamName
        Invoke-WalkerExpectedFailure `
            '^transport journal retained-handle read failed: Bounded native read requires only the default data stream\.$'
    } finally {
        Remove-TestAlternateDataStream $walkerJournalPath $journalStreamName
    }

    $hardlinkTarget = Join-Path $walkerFixture 'journal-hardlink-target.jsonl'
    [IO.File]::Copy($walkerJournalPath, $hardlinkTarget, $true)
    [IO.File]::Delete($walkerJournalPath)
    try {
        New-Item -ItemType HardLink -Path $walkerJournalPath `
            -Target $hardlinkTarget -ErrorAction Stop | Out-Null
        Invoke-WalkerExpectedFailure `
            '^transport journal retained-handle read failed: Bounded native read file identity is invalid\.$'
    } finally {
        [IO.File]::Delete($walkerJournalPath)
        [IO.File]::Delete($hardlinkTarget)
        Write-WalkerJournalFixture $walkerJournalPath $walkerRaw
    }

    $symlinkTarget = Join-Path $walkerFixture 'journal-symlink-target.jsonl'
    [IO.File]::Copy($walkerJournalPath, $symlinkTarget, $true)
    [IO.File]::Delete($walkerJournalPath)
    $symlinkCreated = $false
    try {
        try {
            New-Item -ItemType SymbolicLink -Path $walkerJournalPath `
                -Target $symlinkTarget -ErrorAction Stop | Out-Null
            $symlinkCreated = $true
        } catch {
            Write-Output '[stock-runtime-research-manifest-test] walker-reparse=capability-unavailable'
        }
        if ($symlinkCreated) {
            Invoke-WalkerExpectedFailure `
                '^transport journal retained-handle read failed:'
            Write-Output '[stock-runtime-research-manifest-test] walker-reparse=blocked'
        }
    } finally {
        [IO.File]::Delete($walkerJournalPath)
        [IO.File]::Delete($symlinkTarget)
        Write-WalkerJournalFixture $walkerJournalPath $walkerRaw
    }

    $finalManifestPath = Join-Path $walkerFixture 'research-run-metadata.json'
    $oversized = [IO.File]::Open(
        $finalManifestPath, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $oversized.SetLength(131073) } finally { $oversized.Dispose() }
    try {
        Invoke-WalkerExpectedFailure `
            '^research run manifest retained-handle read failed: Bounded native read size is invalid\.$'
    } finally {
        [IO.File]::Delete($finalManifestPath)
    }

    Write-Output '[stock-runtime-research-manifest-test] v1=passed'
    Write-Output '[stock-runtime-research-manifest-test] v2=passed'
    Write-Output '[stock-runtime-research-manifest-test] v3=passed'
    Write-Output '[stock-runtime-research-manifest-test] v3-ineligible=blocked-before-active-capture'
    Write-Output '[stock-runtime-research-manifest-test] mutation=passed'
    Write-Output '[stock-runtime-research-manifest-test] walker-bounded-read=passed'
    Write-Output '[stock-runtime-research-manifest-test] walker-path-roots=campaign-and-canary'
    Write-Output '[stock-runtime-research-manifest-test] walker-path-rejections=near-name-and-other-parent'
    Write-Output '[stock-runtime-research-manifest-test] processes-started=0'
    Write-Output '[stock-runtime-research-manifest-test] result=success'
} finally {
    if (Test-Path -LiteralPath $approvalReviewRoot) {
        $approvalParent = [IO.Path]::GetFullPath(
            (Split-Path -Parent $approvalReviewRoot)).TrimEnd('\', '/')
        $approvalLeaf = [IO.Path]::GetFileName($approvalReviewRoot)
        $approvalAttributes = [IO.File]::GetAttributes($approvalReviewRoot)
        if ($approvalParent -ine $approvalReviewParent -or
            $approvalLeaf -cnotmatch '^[0-9a-f]{32}$' -or
            ($approvalAttributes -band
                [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Approval digest fixture cleanup target is invalid.'
        }
        [IO.Directory]::Delete($approvalReviewRoot, $true)
    }
    if ($approvalReviewParentCreated -and
        (Test-Path -LiteralPath $approvalReviewParent) -and
        @(Get-ChildItem -LiteralPath $approvalReviewParent -Force).Count -eq 0) {
        [IO.Directory]::Delete($approvalReviewParent, $false)
    }
    if ($null -ne $v3HardlinkTarget -and
        (Test-Path -LiteralPath $v3HardlinkTarget)) {
        $hardlinkTargetParent = [IO.Path]::GetFullPath(
            (Split-Path -Parent $v3HardlinkTarget)).TrimEnd('\', '/')
        $hardlinkTargetLeaf = [IO.Path]::GetFileName($v3HardlinkTarget)
        $hardlinkTargetAttributes =
            [IO.File]::GetAttributes($v3HardlinkTarget)
        if ($hardlinkTargetParent -ine $temporaryRoot -or
            $hardlinkTargetLeaf -cnotmatch
                '^hlclient-stock-runtime-manifest-test-[0-9a-f]{32}-v3-hardlink-target\.bin$' -or
            ($hardlinkTargetAttributes -band
                [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'V3 hardlink fixture cleanup target is invalid.'
        }
        [IO.File]::Delete($v3HardlinkTarget)
    }
    foreach ($cleanup in @(
            [pscustomobject]@{ Path = $walkerCanaryFixture; Parent = $walkerCanaryParent },
            [pscustomobject]@{ Path = $walkerNearFixture; Parent = $walkerNearParent })) {
        if (Test-Path -LiteralPath $cleanup.Path) {
            $cleanupParent = [IO.Path]::GetFullPath(
                (Split-Path -Parent $cleanup.Path)).TrimEnd('\', '/')
            $cleanupLeaf = [IO.Path]::GetFileName($cleanup.Path)
            $cleanupAttributes = [IO.File]::GetAttributes($cleanup.Path)
            if ($cleanupParent -ine $cleanup.Parent -or
                $cleanupLeaf -cnotmatch '^[0-9a-f]{32}$' -or
                ($cleanupAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Walker auxiliary fixture cleanup target is invalid.'
            }
            [IO.Directory]::Delete($cleanup.Path, $true)
        }
    }
    if (Test-Path -LiteralPath $walkerOtherFixture) {
        $otherParent = [IO.Path]::GetFullPath(
            (Split-Path -Parent $walkerOtherFixture)).TrimEnd('\', '/')
        $otherLeaf = [IO.Path]::GetFileName($walkerOtherFixture)
        $otherAttributes = [IO.File]::GetAttributes($walkerOtherFixture)
        if ($otherParent -ine $temporaryRoot -or
            $otherLeaf -cnotmatch '^[0-9a-f]{32}$' -or
            ($otherAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Walker other-parent fixture cleanup target is invalid.'
        }
        [IO.Directory]::Delete($walkerOtherFixture, $true)
    }
    foreach ($parentCleanup in @(
            [pscustomobject]@{ Path = $walkerCanaryParent; Created = $walkerCanaryParentCreated },
            [pscustomobject]@{ Path = $walkerNearParent; Created = $walkerNearParentCreated })) {
        if ($parentCleanup.Created -and
            (Test-Path -LiteralPath $parentCleanup.Path) -and
            @(Get-ChildItem -LiteralPath $parentCleanup.Path -Force).Count -eq 0) {
            [IO.Directory]::Delete($parentCleanup.Path, $false)
        }
    }
    if (Test-Path -LiteralPath $walkerFixture) {
        $walkerFixtureParent = [IO.Path]::GetFullPath(
            (Split-Path -Parent $walkerFixture)).TrimEnd('\', '/')
        $walkerFixtureLeaf = [IO.Path]::GetFileName($walkerFixture)
        $walkerFixtureAttributes = [IO.File]::GetAttributes($walkerFixture)
        if ($walkerFixtureParent -ine $walkerParent -or
            $walkerFixtureLeaf -cnotmatch '^[0-9a-f]{32}$' -or
            ($walkerFixtureAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Walker bounded-read fixture cleanup target is invalid.'
        }
        [IO.Directory]::Delete($walkerFixture, $true)
    }
    if ($walkerParentCreated -and (Test-Path -LiteralPath $walkerParent) -and
        @(Get-ChildItem -LiteralPath $walkerParent -Force).Count -eq 0) {
        [IO.Directory]::Delete($walkerParent, $false)
        if ($manualArtifactsCreated -and
            (Test-Path -LiteralPath $manualArtifacts) -and
            @(Get-ChildItem -LiteralPath $manualArtifacts -Force).Count -eq 0) {
            [IO.Directory]::Delete($manualArtifacts, $false)
        }
    }
    if (Test-Path -LiteralPath $fixture) {
        $parent = [IO.Path]::GetFullPath((Split-Path -Parent $fixture)).TrimEnd('\', '/')
        $leaf = [IO.Path]::GetFileName($fixture)
        $attributes = [IO.File]::GetAttributes($fixture)
        if ($parent -ine $temporaryRoot -or
            $leaf -cnotmatch '^hlclient-stock-runtime-manifest-test-[0-9a-f]{32}$' -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Research manifest fixture cleanup target is invalid.'
        }
        [IO.Directory]::Delete($fixture, $true)
    }
}
