#requires -Version 5.1

<#
.SYNOPSIS
Exercises deterministic stock-runtime campaign resume with fake observations.

.DESCRIPTION
Runs the in-memory campaign resume tests plus a temporary metadata-only campaign
summary fixture below the exact ignored campaign root. It does not launch stock
binaries, write raw/auth data, retain a campaign manifest, or publish evidence.
#>
[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$TestExecutable = '.\build\bin\Debug\hlclient_tests.exe',

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$CheckerPath =
        '.\build\bin\Debug\hlclient_stock_runtime_check.exe'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$testPath = [IO.Path]::GetFullPath($TestExecutable)
$checker = [IO.Path]::GetFullPath($CheckerPath)
$manualRoot = Join-Path $repositoryRoot 'manual-artifacts\stock-runtime'
$manualParent = Split-Path -Parent $manualRoot
$runnerPath = Join-Path $PSScriptRoot 'run_stock_runtime_first_observations.ps1'
$evidencePath = Join-Path $repositoryRoot `
    'docs\evidence\GOLDSRC_STOCK_RUNTIME_FIRST_OBSERVATIONS.json'

if ($null -eq ('HlClient.CampaignFixture.OwnedDirectory' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace HlClient.CampaignFixture
{
    public static class OwnedDirectory
    {
        private const uint DeleteAccess = 0x00010000;
        private const uint FileReadAttributes = 0x00000080;
        private const uint FileShareRead = 0x00000001;
        private const uint FileShareWrite = 0x00000002;
        private const uint FileShareDelete = 0x00000004;
        private const uint FileWriteData = 0x00000002;
        private const uint FileAddSubdirectory = 0x00000004;
        private const uint FileWriteAttributes = 0x00000100;
        private const uint SynchronizeAccess = 0x00100000;
        private const uint OpenExisting = 3;
        private const uint FileFlagOpenReparsePoint = 0x00200000;
        private const uint FileFlagBackupSemantics = 0x02000000;
        private const uint FileAttributeDirectory = 0x00000010;
        private const uint FileAttributeReparsePoint = 0x00000400;
        private const int FileDispositionInfo = 4;
        private const uint ObjectCaseInsensitive = 0x00000040;
        private const uint FileCreate = 2;
        private const uint FileDirectoryFile = 0x00000001;
        private const uint FileSynchronousIoNonAlert = 0x00000020;
        private const uint FileNonDirectoryFile = 0x00000040;
        private const uint NtFileOpenReparsePoint = 0x00200000;

        [StructLayout(LayoutKind.Sequential)]
        private struct ByHandleFileInformation
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
        private struct FileDispositionInformation
        {
            [MarshalAs(UnmanagedType.Bool)]
            public bool DeleteFile;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct UnicodeString
        {
            public ushort Length;
            public ushort MaximumLength;
            public IntPtr Buffer;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ObjectAttributes
        {
            public int Length;
            public IntPtr RootDirectory;
            public IntPtr ObjectName;
            public uint Attributes;
            public IntPtr SecurityDescriptor;
            public IntPtr SecurityQualityOfService;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct IoStatusBlock
        {
            public IntPtr Status;
            public UIntPtr Information;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode,
            SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(
            string fileName, uint desiredAccess, uint shareMode,
            IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandle(
            SafeFileHandle file, out ByHandleFileInformation information);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetFileInformationByHandle(
            SafeFileHandle file, int informationClass,
            ref FileDispositionInformation information,
            uint bufferSize);

        [DllImport("ntdll.dll")]
        private static extern int NtCreateFile(
            out SafeFileHandle file, uint desiredAccess,
            ref ObjectAttributes objectAttributes,
            out IoStatusBlock ioStatusBlock, IntPtr allocationSize,
            uint fileAttributes, uint shareAccess, uint createDisposition,
            uint createOptions, IntPtr eaBuffer, uint eaLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetFilePointerEx(
            SafeFileHandle file, long distance, out long newPointer,
            uint moveMethod);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetEndOfFile(SafeFileHandle file);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WriteFile(
            SafeFileHandle file, byte[] buffer, uint bytesToWrite,
            out uint bytesWritten, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FlushFileBuffers(SafeFileHandle file);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern SafeFileHandle ReOpenFile(
            SafeFileHandle originalFile, uint desiredAccess,
            uint shareMode, uint flagsAndAttributes);

        private static bool IsOrdinary(
            SafeFileHandle handle, bool requireDirectory)
        {
            ByHandleFileInformation information;
            if (!GetFileInformationByHandle(handle, out information))
                return false;
            bool isDirectory =
                (information.FileAttributes & FileAttributeDirectory) != 0;
            return isDirectory == requireDirectory &&
                (information.FileAttributes & FileAttributeReparsePoint) == 0 &&
                (isDirectory || information.NumberOfLinks == 1);
        }

        private static SafeFileHandle OpenOrdinary(
            string path, bool requireDirectory, uint desiredAccess,
            uint shareMode)
        {
            SafeFileHandle handle = CreateFileW(
                path, desiredAccess, shareMode, IntPtr.Zero, OpenExisting,
                FileFlagOpenReparsePoint | FileFlagBackupSemantics,
                IntPtr.Zero);
            if (handle.IsInvalid)
            {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                throw new Win32Exception(error,
                    "Unable to retain the metadata fixture directory identity " +
                    "(native=" + error.ToString() + ").");
            }

            if (!IsOrdinary(handle, requireDirectory))
            {
                handle.Dispose();
                throw new InvalidOperationException(
                    "A metadata fixture entry is not one ordinary object.");
            }
            return handle;
        }

        private static SafeFileHandle CreateOwned(
            SafeFileHandle parent, string leaf, bool directory)
        {
            if (parent == null || parent.IsInvalid ||
                String.IsNullOrEmpty(leaf) || leaf.Length > 128 ||
                leaf == "." || leaf == ".." ||
                leaf.IndexOf('\\') >= 0 || leaf.IndexOf('/') >= 0)
                throw new ArgumentException("Invalid metadata fixture leaf.");

            IntPtr buffer = IntPtr.Zero;
            IntPtr namePointer = IntPtr.Zero;
            try
            {
                buffer = Marshal.StringToHGlobalUni(leaf);
                var name = new UnicodeString {
                    Length = checked((ushort)(leaf.Length * 2)),
                    MaximumLength = checked((ushort)((leaf.Length + 1) * 2)),
                    Buffer = buffer
                };
                namePointer = Marshal.AllocHGlobal(
                    Marshal.SizeOf(typeof(UnicodeString)));
                Marshal.StructureToPtr(name, namePointer, false);
                var attributes = new ObjectAttributes {
                    Length = Marshal.SizeOf(typeof(ObjectAttributes)),
                    RootDirectory = parent.DangerousGetHandle(),
                    ObjectName = namePointer,
                    Attributes = ObjectCaseInsensitive,
                    SecurityDescriptor = IntPtr.Zero,
                    SecurityQualityOfService = IntPtr.Zero
                };
                IoStatusBlock io;
                uint access = DeleteAccess | FileReadAttributes |
                    SynchronizeAccess;
                if (!directory)
                    access |= FileWriteData | FileWriteAttributes;
                uint options = FileSynchronousIoNonAlert |
                    NtFileOpenReparsePoint |
                    (directory ? FileDirectoryFile : FileNonDirectoryFile);
                uint share = FileShareRead | FileShareWrite |
                    (directory ? 0U : FileShareDelete);
                SafeFileHandle created;
                int status = NtCreateFile(
                    out created, access, ref attributes, out io, IntPtr.Zero,
                    0x00000080, share, FileCreate, options, IntPtr.Zero, 0);
                if (status < 0 || created == null || created.IsInvalid)
                {
                    if (created != null) created.Dispose();
                    throw new InvalidOperationException(
                        "Atomic metadata fixture creation failed with NTSTATUS 0x" +
                        unchecked((uint)status).ToString("X8") + ".");
                }
                if (!IsOrdinary(created, directory))
                {
                    created.Dispose();
                    throw new InvalidOperationException(
                        "Atomic metadata fixture creation returned a wrong type.");
                }
                return created;
            }
            finally
            {
                if (namePointer != IntPtr.Zero)
                    Marshal.FreeHGlobal(namePointer);
                if (buffer != IntPtr.Zero) Marshal.FreeHGlobal(buffer);
            }
        }

        public static SafeFileHandle CreateOwnedDirectory(
            SafeFileHandle parent, string leaf)
        {
            return CreateOwned(parent, leaf, true);
        }

        public static SafeFileHandle CreateOwnedFile(
            SafeFileHandle parent, string leaf)
        {
            return CreateOwned(parent, leaf, false);
        }

        private static SafeFileHandle ReopenExactFile(
            SafeFileHandle retained, uint desiredAccess)
        {
            if (retained == null || retained.IsInvalid)
                throw new ArgumentException("Invalid retained fixture file.");
            SafeFileHandle reopened = ReOpenFile(
                retained, desiredAccess,
                FileShareRead | FileShareWrite | FileShareDelete,
                FileFlagOpenReparsePoint);
            if (reopened.IsInvalid || !IsOrdinary(reopened, false))
            {
                int error = Marshal.GetLastWin32Error();
                reopened.Dispose();
                throw new Win32Exception(error,
                    "Unable to reopen the exact retained fixture file.");
            }
            return reopened;
        }

        public static SafeFileHandle RetainExactFile(
            SafeFileHandle created)
        {
            return ReopenExactFile(created, FileReadAttributes);
        }

        public static void WriteExactBytes(
            SafeFileHandle file, byte[] bytes)
        {
            if (file == null || file.IsInvalid || bytes == null ||
                bytes.Length > 131072)
                throw new ArgumentException("Invalid metadata fixture payload.");
            long position;
            if (!SetFilePointerEx(file, 0, out position, 0) ||
                !SetEndOfFile(file))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Unable to truncate the retained metadata fixture file.");
            uint written;
            if (bytes.Length != 0 &&
                (!WriteFile(file, bytes, (uint)bytes.Length, out written,
                    IntPtr.Zero) || written != (uint)bytes.Length))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Unable to write the retained metadata fixture file.");
            if (!FlushFileBuffers(file))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Unable to flush the retained metadata fixture file.");
        }

        public static void WriteExactBytesViaRetained(
            SafeFileHandle retained, byte[] bytes)
        {
            SafeFileHandle writer = ReopenExactFile(
                retained, FileWriteData | FileWriteAttributes |
                SynchronizeAccess);
            try { WriteExactBytes(writer, bytes); }
            finally { writer.Dispose(); }
        }

        public static void MarkRetainedFileForDeletion(
            SafeFileHandle retained)
        {
            SafeFileHandle deleter = ReopenExactFile(
                retained, DeleteAccess | FileReadAttributes);
            try { MarkExactEntryForDeletion(deleter); }
            finally { deleter.Dispose(); }
        }

        public static SafeFileHandle OpenOrdinaryCreateParent(string path)
        {
            // The repository/manual-artifacts parent is not owned by this
            // fixture and is never deleted. Hosted runners can retain their
            // checkout root through a handle which does not share deletion,
            // so requesting DELETE here would create a needless sharing-mode
            // conflict. The child itself is still created relative to this
            // verified ordinary parent and returned with DELETE access.
            return OpenOrdinary(
                path, true, FileAddSubdirectory | FileReadAttributes,
                FileShareRead | FileShareWrite);
        }

        public static SafeFileHandle OpenPinnedOrdinaryFile(string path)
        {
            return OpenOrdinary(
                path, false, DeleteAccess | FileReadAttributes,
                FileShareRead | FileShareWrite);
        }

        public static SafeFileHandle OpenRetainedOrdinaryFile(string path)
        {
            // Read sharing lets the checker inspect the fixture. Omitting
            // write/delete sharing pins this exact file identity until cleanup.
            return OpenOrdinary(
                path, false, FileReadAttributes, FileShareRead);
        }

        public static string GetIdentity(SafeFileHandle handle)
        {
            ByHandleFileInformation information;
            if (!GetFileInformationByHandle(handle, out information))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Unable to read a retained metadata fixture identity.");
            return information.VolumeSerialNumber.ToString("X8") + ":" +
                information.FileIndexHigh.ToString("X8") +
                information.FileIndexLow.ToString("X8");
        }

        public static void MarkExactEntryForDeletion(
            SafeFileHandle handle)
        {
            var information = new FileDispositionInformation {
                DeleteFile = true
            };
            if (!SetFileInformationByHandle(
                    handle, FileDispositionInfo, ref information,
                    (uint)Marshal.SizeOf(typeof(FileDispositionInformation))))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Unable to delete the retained metadata fixture identity.");
            }
        }
    }
}
'@
}

function Get-PathObservation {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return 'absent' }
    if ($item.PSIsContainer) {
        return 'directory|' + $item.CreationTimeUtc.Ticks + '|' +
            $item.LastWriteTimeUtc.Ticks
    }
    return 'file|' + $item.Length + '|' +
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Write-AtomicJson {
    param([string]$Path, [object]$Value)
    $text = ($Value | ConvertTo-Json -Depth 8) + "`n"
    if ([Text.Encoding]::UTF8.GetByteCount($text) -gt 131072) {
        throw 'Fake campaign manifest exceeds its test byte bound.'
    }
    $temporary = Join-Path (Split-Path -Parent $Path) `
        ('.campaign-test-' + [guid]::NewGuid().ToString('N') + '.tmp')
    $backup = Join-Path (Split-Path -Parent $Path) `
        ('.campaign-test-' + [guid]::NewGuid().ToString('N') + '.bak')
    try {
        [IO.File]::WriteAllText(
            $temporary, $text, [Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            [IO.File]::Replace($temporary, $Path, $backup, $true)
        } else {
            [IO.File]::Move($temporary, $Path)
        }
    } finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force
        }
        if (Test-Path -LiteralPath $backup -PathType Leaf) {
            Remove-Item -LiteralPath $backup -Force
        }
    }
}

function Write-CreatedJson {
    param([IDisposable]$Handle, [object]$Value)
    $text = ($Value | ConvertTo-Json -Depth 8) + "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($text)
    if ($bytes.Length -gt 131072) {
        throw 'Fake campaign manifest exceeds its test byte bound.'
    }
    [HlClient.CampaignFixture.OwnedDirectory]::WriteExactBytes(
        $Handle, $bytes)
}

function Write-RetainedJson {
    param([IDisposable]$Handle, [object]$Value)
    $text = ($Value | ConvertTo-Json -Depth 8) + "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($text)
    if ($bytes.Length -gt 131072) {
        throw 'Fake campaign manifest exceeds its test byte bound.'
    }
    [HlClient.CampaignFixture.OwnedDirectory]::WriteExactBytesViaRetained(
        $Handle, $bytes)
}

function Invoke-CampaignSummary {
    param(
        [string]$Root, [string]$RefreshCommit = '',
        [string[]]$WalkerValidatedRunIds = @(),
        [string]$ExternalTargetProfile = 'reviewed-non-executable-v1',
        [Int64]$ExternalTargetCount = 1)
    $arguments = @('--capture-root', $Root, '--scenario', 'campaign-summary')
    if (-not [string]::IsNullOrEmpty($RefreshCommit)) {
        $arguments += @(
            '--campaign-refresh-implementation-commit', $RefreshCommit,
            '--campaign-external-target-profile', $ExternalTargetProfile,
            '--campaign-external-target-count', [string]$ExternalTargetCount)
    }
    foreach ($runId in $WalkerValidatedRunIds) {
        $arguments += @('--independent-walker-validated-run', $runId)
    }
    $savedPreference = $ErrorActionPreference
    $locationPushed = $false
    try {
        $ErrorActionPreference = 'Continue'
        Push-Location -LiteralPath $repositoryRoot
        $locationPushed = $true
        $output = @(& $checker @arguments 2>&1 |
            ForEach-Object { $_.ToString() })
        $nativeExit = $LASTEXITCODE
    } finally {
        if ($locationPushed) { Pop-Location }
        $ErrorActionPreference = $savedPreference
    }
    return [pscustomobject]@{ Lines = $output; ExitCode = $nativeExit }
}

function Convert-CampaignSummary {
    param([string[]]$Lines)
    $values = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::Ordinal)
    if ($Lines.Count -gt 32 -or
        [Text.Encoding]::UTF8.GetByteCount(($Lines -join "`n")) -gt 16384) {
        throw 'Campaign summary output exceeds its test bound.'
    }
    foreach ($line in $Lines) {
        if ($line -cnotmatch
                '^\[stock-runtime\] (?<key>[a-z0-9-]+)=(?<value>[A-Za-z0-9_.:-]{1,256})$' -or
            $values.ContainsKey($Matches.key)) {
            throw 'Campaign summary emitted a non-contract line.'
        }
        $values.Add($Matches.key, $Matches.value)
    }
    return $values
}

function New-FakeCampaignManifest {
    param(
        [string]$ImplementationCommit,
        [string]$StructuralHash,
        [int]$Attempted = 2,
        [int]$Rejected = 1,
        [int]$Incomplete = 1)
    $matrix = @(
        [ordered]@{ map_category = 'boot_camp'; scenario = 'baseline'; required_runs = 6; accepted_runs = 0 },
        [ordered]@{ map_category = 'crossfire'; scenario = 'baseline'; required_runs = 4; accepted_runs = 0 },
        [ordered]@{ map_category = 'stalkyard'; scenario = 'baseline'; required_runs = 4; accepted_runs = 0 },
        [ordered]@{ map_category = 'crossfire'; scenario = 'idle-runtime'; required_runs = 4; accepted_runs = 0 },
        [ordered]@{ map_category = 'boot_camp'; scenario = 'drop-server-to-client-transport-ordinal'; required_runs = 2; accepted_runs = 0 },
        [ordered]@{ map_category = 'crossfire'; scenario = 'duplicate-server-to-client-transport-ordinal'; required_runs = 1; accepted_runs = 0 },
        [ordered]@{ map_category = 'stalkyard'; scenario = 'reorder-server-to-client-transport-ordinal'; required_runs = 1; accepted_runs = 0 },
        [ordered]@{ map_category = 'boot_camp'; scenario = 'reconnect'; required_runs = 2; accepted_runs = 0 }
    )
    return [ordered]@{
        schema = 'hlclient.stock-runtime-first-campaign.v1'
        implementation_commit = $ImplementationCommit
        profile_fingerprint = 'evidence_pending'
        external_target_profile = 'reviewed-non-executable-v1'
        external_target_count = 1
        required_matrix = $matrix
        attempted_slots = $Attempted
        accepted_slots = 0
        rejected_slots = $Rejected
        incomplete_slots = $Incomplete
        pending_slots = 24
        packet_totals = [ordered]@{
            sequenced_c2s = 0; sequenced_s2c = 0
            reassembled = 0; decompressed = 0
        }
        boundary_totals = [ordered]@{
            exact = 0; candidates = 0; reconnect_generations = 0
        }
        candidate_stability = 'evidence_pending'
        threshold_status = 'pending'
        campaign_structural_sha256 = $StructuralHash
    }
}

function Assert-PendingCampaignSummary {
    param([object]$Values, [string]$ImplementationCommit)
    $expected = [ordered]@{
        profile = 'stock_protocol_48_build_10210_evidence_pending'
        'external-target-profile' = 'reviewed-non-executable-v1'
        'external-target-count' = '1'
        accepted = '0'; rejected = '1'; incomplete = '1'; pending = '24'
        'sequenced-c2s' = '0'; 'sequenced-s2c' = '0'
        reassembled = '0'; decompressed = '0'; boundaries = '0'
        candidates = '0'; 'reconnect-generations' = '0'
        'candidate-stability' = 'evidence_pending'; threshold = 'pending'
        'implementation-commit' = $ImplementationCommit
        result = 'campaign-summary'
    }
    if ($Values.Count -ne ($expected.Count + 1) -or
        $Values['structural-hash'] -cnotmatch '^[0-9a-f]{64}$') {
        throw 'Metadata-only campaign summary shape is invalid.'
    }
    foreach ($entry in $expected.GetEnumerator()) {
        if (-not $Values.ContainsKey($entry.Key) -or
            $Values[$entry.Key] -cne $entry.Value) {
            throw ("Metadata-only campaign summary disagrees at " +
                "'$($entry.Key)': expected '$($entry.Value)', actual " +
                "'$($Values[$entry.Key])'.")
        }
    }
}

function Assert-CampaignManifestRejected {
    param([string]$Root)
    $result = Invoke-CampaignSummary $Root
    if ($result.ExitCode -eq 0 -or
        @($result.Lines | Where-Object {
                $_ -match '^\[stock-runtime\] result=[a-z0-9_-]+$' }).Count -ne 1) {
        throw 'Mutated campaign manifest was not rejected with one typed result.'
    }
}

function Assert-ExactMetadataFixtureShape {
    param([string]$FixturePath, [string[]]$RunIds)

    $rootItem = Get-Item -LiteralPath $FixturePath -Force -ErrorAction Stop
    if (($rootItem.Attributes -band [IO.FileAttributes]::Directory) -eq 0 -or
        ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Metadata fixture root identity or type drifted.'
    }
    $expectedRootNames = @('campaign-manifest.json') + $RunIds
    $actualRootEntries = @(Get-ChildItem -LiteralPath $FixturePath -Force |
        Sort-Object -Property Name)
    if (($actualRootEntries.Name -join "`n") -cne
        (($expectedRootNames | Sort-Object) -join "`n")) {
        throw 'Metadata fixture root gained or lost an entry; refusing cleanup.'
    }
    foreach ($entry in $actualRootEntries) {
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Metadata fixture entry became a reparse point; refusing cleanup.'
        }
    }
    $manifest = Get-Item -LiteralPath `
        (Join-Path $FixturePath 'campaign-manifest.json') `
        -Force -ErrorAction Stop
    if (($manifest.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
        throw 'Metadata fixture manifest changed type; refusing cleanup.'
    }
    foreach ($runId in $RunIds) {
        $runRoot = Join-Path $FixturePath $runId
        $run = Get-Item -LiteralPath $runRoot -Force -ErrorAction Stop
        $children = @(Get-ChildItem -LiteralPath $runRoot -Force)
        if (($run.Attributes -band [IO.FileAttributes]::Directory) -eq 0 -or
            ($run.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            $children.Count -ne 1 -or
            $children[0].Name -cne 'research-run-metadata.json' -or
            ($children[0].Attributes -band
                [IO.FileAttributes]::Directory) -ne 0 -or
            ($children[0].Attributes -band
                [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Metadata fixture run drifted; refusing cleanup.'
        }
    }
}

if (-not $testPath.StartsWith(
        $repositoryRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $testPath -PathType Leaf) -or
    [IO.Path]::GetFileName($testPath) -cne 'hlclient_tests.exe') {
    throw 'TestExecutable must name the repository-built hlclient_tests.exe.'
}
if (-not $checker.StartsWith(
        $repositoryRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $checker -PathType Leaf) -or
    [IO.Path]::GetFileName($checker) -cne
        'hlclient_stock_runtime_check.exe') {
    throw 'CheckerPath must name the repository-built stock-runtime checker.'
}
if (-not (Test-Path -LiteralPath $runnerPath -PathType Leaf)) {
    throw 'Campaign runner is absent.'
}

$beforeManual = Get-PathObservation $manualRoot
$beforeManualParent = Get-PathObservation $manualParent
$beforeEvidence = Get-PathObservation $evidencePath
$saved = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $lines = @(& $testPath '[goldsrc][stock-runtime][campaign][resume]' `
            '--reporter' 'compact' 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
} finally { $ErrorActionPreference = $saved }
if ($exitCode -ne 0) {
    throw "Fake campaign resume tests failed: $($lines -join ' ')"
}
$policyLines = @(& $runnerPath -ValidateCampaignAggregationPolicy |
    ForEach-Object { $_.ToString() })
if ($policyLines -cnotcontains
        '[stock-runtime-campaign-policy] retired-tail-inflation-rejections=1' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] run-reparse-rejections=1' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] failure-publication-mutations=3' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] fatal-resume-category-rejections=5' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] fatal-resume-state-rejections=1' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] walker-nonzero-exit-rejections=1' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] external-target-metadata-acceptances=2' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] external-target-metadata-rejections=4' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] mixed-external-target-binding-rejections=2' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] canary-mutation-rejections=4' -or
    $policyLines -cnotcontains
        '[stock-runtime-campaign-policy] unbound-canary-rebind-rejections=1' -or
    $policyLines -cnotcontains '[stock-runtime-campaign-policy] files-written=0' -or
    $policyLines -cnotcontains '[stock-runtime-campaign-policy] result=success') {
    throw 'Campaign aggregation policy self-test failed.'
}

$fixtureCommit = '1111111111111111111111111111111111111111'
$fixtureRoot = [IO.Path]::GetFullPath($manualRoot).TrimEnd('\', '/')
$requiredFixtureRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime')).TrimEnd('\', '/')
if ($fixtureRoot -ine $requiredFixtureRoot) {
    throw 'Metadata-only fixture is not the exact ignored campaign root.'
}
$manifestPath = Join-Path $fixtureRoot 'campaign-manifest.json'
$filesystemFixtureResult = 'skipped-existing-campaign-root'
$filesystemClassification = 'not-run'
$cleanupDriftRejections = 0
if ($beforeManual -ceq 'absent') {
  $fixtureCreated = $false
  $fixtureParentCreated = $false
  $fixtureRepositoryHandle = $null
  $fixtureParentHandle = $null
  $fixtureRootHandle = $null
  $fixtureRunHandles = [Collections.Generic.List[IDisposable]]::new()
  $fixtureRetainedFiles = [Collections.Generic.List[IDisposable]]::new()
  $fixtureMetadataHandles = [Collections.Generic.List[object]]::new()
  $fixtureRunIds = @(
      '00000000000000000000000000000001',
      '00000000000000000000000000000002')
  try {
    $fixtureParent = [IO.Path]::GetFullPath(
        (Split-Path -Parent $fixtureRoot)).TrimEnd('\', '/')
    $requiredFixtureParent = [IO.Path]::GetFullPath(
        (Join-Path $repositoryRoot 'manual-artifacts')).TrimEnd('\', '/')
    if ($fixtureParent -ine $requiredFixtureParent) {
        throw 'Metadata fixture parent is outside the exact ignored root.'
    }
    if (Test-Path -LiteralPath $fixtureParent -PathType Container) {
        $fixtureParentHandle =
            [HlClient.CampaignFixture.OwnedDirectory]::OpenOrdinaryCreateParent(
                $fixtureParent)
    } else {
        if ((Get-PathObservation $fixtureParent) -cne 'absent') {
            throw 'Metadata fixture parent is not one ordinary directory.'
        }
        $fixtureRepositoryHandle =
            [HlClient.CampaignFixture.OwnedDirectory]::OpenOrdinaryCreateParent(
                $repositoryRoot)
        $fixtureParentHandle =
            [HlClient.CampaignFixture.OwnedDirectory]::CreateOwnedDirectory(
                $fixtureRepositoryHandle, 'manual-artifacts')
        $fixtureParentCreated = $true
    }
    $fixtureRootHandle =
        [HlClient.CampaignFixture.OwnedDirectory]::CreateOwnedDirectory(
            $fixtureParentHandle, [IO.Path]::GetFileName($fixtureRoot))
    $fixtureCreated = $true
    $fakeRuns = @(
        [ordered]@{
            run_id = $fixtureRunIds[0]
            map_category = 'boot_camp'; scenario = 'baseline'
            accepted_evidence_run = $false
            failure_category = 'bounded-session-incomplete'
        },
        [ordered]@{
            run_id = $fixtureRunIds[1]
            map_category = 'crossfire'; scenario = 'baseline'
            accepted_evidence_run = $false
            failure_category = 'timeout'
        }
    )
    foreach ($fakeRun in $fakeRuns) {
        $runRoot = Join-Path $fixtureRoot $fakeRun.run_id
        $runHandle =
            [HlClient.CampaignFixture.OwnedDirectory]::CreateOwnedDirectory(
                $fixtureRootHandle, $fakeRun.run_id)
        $fixtureRunHandles.Add($runHandle)
        $metadata = [ordered]@{
            schema = 'hlclient.stock-runtime-research-run.v1'
            run_id = $fakeRun.run_id
            map_category = $fakeRun.map_category
            scenario = $fakeRun.scenario
            accepted_transport_run = $false
            accepted_evidence_run = $false
            failure_category = $fakeRun.failure_category
            external_target_profile = 'reviewed-non-executable-v1'
            external_target_count = 1
        }
        $metadataPath = Join-Path $runRoot 'research-run-metadata.json'
        $createdMetadata = $null
        try {
            $createdMetadata =
                [HlClient.CampaignFixture.OwnedDirectory]::CreateOwnedFile(
                    $runHandle, 'research-run-metadata.json')
            Write-CreatedJson $createdMetadata $metadata
            $metadataHandle =
                [HlClient.CampaignFixture.OwnedDirectory]::RetainExactFile(
                    $createdMetadata)
            $fixtureRetainedFiles.Add($metadataHandle)
            $fixtureMetadataHandles.Add($metadataHandle)
        } finally {
            if ($null -ne $createdMetadata) { $createdMetadata.Dispose() }
        }
    }

    $refreshA = Invoke-CampaignSummary $fixtureRoot $fixtureCommit
    $refreshB = Invoke-CampaignSummary $fixtureRoot $fixtureCommit
    if ($refreshA.ExitCode -ne 0 -or $refreshB.ExitCode -ne 0 -or
        ($refreshA.Lines -join "`n") -cne ($refreshB.Lines -join "`n")) {
        throw ('Metadata-only campaign refresh is unsuccessful or ' +
            "non-deterministic: A=$($refreshA.ExitCode) " +
            "B=$($refreshB.ExitCode) output=$($refreshA.Lines -join ' | ')")
    }
    $refreshValues = Convert-CampaignSummary $refreshA.Lines
    Assert-PendingCampaignSummary $refreshValues $fixtureCommit
    $validatedRejectedRuns = Invoke-CampaignSummary $fixtureRoot `
        $fixtureCommit $fixtureRunIds
    if ($validatedRejectedRuns.ExitCode -ne 0 -or
        ($validatedRejectedRuns.Lines -join "`n") -cne
            ($refreshA.Lines -join "`n")) {
        throw 'Walker capability changed rejected/incomplete classification.'
    }
    $mixedBindingRejections = 0
    $mixedProfile = $fakeRuns[1].Clone()
    $mixedProfile.schema = 'hlclient.stock-runtime-research-run.v1'
    $mixedProfile.accepted_transport_run = $false
    $mixedProfile.external_target_profile = 'none'
    $mixedProfile.external_target_count = 0
    Write-RetainedJson $fixtureMetadataHandles[1] $mixedProfile
    if ((Invoke-CampaignSummary $fixtureRoot $fixtureCommit).ExitCode -ne 0) {
        $mixedBindingRejections++
    }
    $mixedCount = $mixedProfile.Clone()
    $mixedCount.external_target_profile = 'reviewed-non-executable-v1'
    $mixedCount.external_target_count = 2
    Write-RetainedJson $fixtureMetadataHandles[1] $mixedCount
    if ((Invoke-CampaignSummary $fixtureRoot $fixtureCommit).ExitCode -ne 0) {
        $mixedBindingRejections++
    }
    $restoredMetadata = $mixedProfile.Clone()
    $restoredMetadata.external_target_profile = 'reviewed-non-executable-v1'
    $restoredMetadata.external_target_count = 1
    Write-RetainedJson $fixtureMetadataHandles[1] $restoredMetadata
    if ($mixedBindingRejections -ne 2) {
        throw 'Mixed external-target campaign bindings failed open.'
    }
    $duplicateWalkerCapability = Invoke-CampaignSummary $fixtureRoot `
        $fixtureCommit @($fixtureRunIds[0], $fixtureRunIds[0])
    if ($duplicateWalkerCapability.ExitCode -ne 2) {
        throw 'Duplicate walker-validation capability did not fail closed.'
    }
    $campaignHash = [string]$refreshValues['structural-hash']
    $canonicalManifest = New-FakeCampaignManifest $fixtureCommit $campaignHash
    $createdManifest = $null
    try {
        $createdManifest =
            [HlClient.CampaignFixture.OwnedDirectory]::CreateOwnedFile(
                $fixtureRootHandle, 'campaign-manifest.json')
        Write-CreatedJson $createdManifest $canonicalManifest
        $manifestHandle =
            [HlClient.CampaignFixture.OwnedDirectory]::RetainExactFile(
                $createdManifest)
        $fixtureRetainedFiles.Add($manifestHandle)
    } finally {
        if ($null -ne $createdManifest) { $createdManifest.Dispose() }
    }

    $normalA = Invoke-CampaignSummary $fixtureRoot
    $normalB = Invoke-CampaignSummary $fixtureRoot
    if ($normalA.ExitCode -ne 0 -or $normalB.ExitCode -ne 0 -or
        ($normalA.Lines -join "`n") -cne ($normalB.Lines -join "`n") -or
        ($normalA.Lines -join "`n") -cne ($refreshA.Lines -join "`n")) {
        throw 'Persisted metadata-only campaign summary is not deterministic.'
    }
    Assert-PendingCampaignSummary `
        (Convert-CampaignSummary $normalA.Lines) $fixtureCommit

    $commitMutation = New-FakeCampaignManifest `
        '2222222222222222222222222222222222222222' $campaignHash
    Write-RetainedJson $manifestHandle $commitMutation
    Assert-CampaignManifestRejected $fixtureRoot
    $hashMutation = New-FakeCampaignManifest $fixtureCommit `
        'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
    Write-RetainedJson $manifestHandle $hashMutation
    Assert-CampaignManifestRejected $fixtureRoot
    $countMutation = New-FakeCampaignManifest $fixtureCommit $campaignHash `
        -Rejected 2
    Write-RetainedJson $manifestHandle $countMutation
    Assert-CampaignManifestRejected $fixtureRoot
    Write-RetainedJson $manifestHandle $canonicalManifest
    $finalSummary = Invoke-CampaignSummary $fixtureRoot
    if ($finalSummary.ExitCode -ne 0 -or
        ($finalSummary.Lines -join "`n") -cne ($normalA.Lines -join "`n")) {
        throw 'Campaign summary did not recover exactly after manifest mutations.'
    }
    $probe = $null
    try {
      $probe = [HlClient.CampaignFixture.OwnedDirectory]::CreateOwnedFile(
          $fixtureRootHandle, '.campaign-test-drift-probe')
      [HlClient.CampaignFixture.OwnedDirectory]::WriteExactBytes(
          $probe, [byte[]]@(0x5a))
      try {
        Assert-ExactMetadataFixtureShape $fixtureRoot $fixtureRunIds
      } catch {
        if ($_.Exception.Message -cne
            'Metadata fixture root gained or lost an entry; refusing cleanup.') {
          throw
        }
        $cleanupDriftRejections += 1
      }
      if ($cleanupDriftRejections -ne 1) {
        throw 'Metadata fixture cleanup did not reject entry drift.'
      }
    } finally {
      if ($null -ne $probe) {
        [HlClient.CampaignFixture.OwnedDirectory]::MarkExactEntryForDeletion(
            $probe)
        $probe.Dispose()
      }
    }
    $filesystemFixtureResult = 'metadata-only'
    $filesystemClassification = 'rejected-1-incomplete-1'
  } finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot).TrimEnd('\', '/')
    if ($resolvedFixture -ine $requiredFixtureRoot) {
        throw 'Refusing unsafe metadata-only campaign fixture cleanup.'
    }
    try {
      if ($fixtureCreated -and $null -ne $fixtureRootHandle -and
          -not $fixtureRootHandle.IsInvalid -and
          (Test-Path -LiteralPath $resolvedFixture -PathType Container)) {
        Assert-ExactMetadataFixtureShape $resolvedFixture $fixtureRunIds
        try {
          # Every directory/file handle has been retained by the same atomic
          # relative NtCreateFile operation which created that object.
          Assert-ExactMetadataFixtureShape $resolvedFixture $fixtureRunIds
          foreach ($handle in $fixtureRetainedFiles) {
            [HlClient.CampaignFixture.OwnedDirectory]::MarkRetainedFileForDeletion(
                $handle)
          }
          foreach ($handle in $fixtureRetainedFiles) { $handle.Dispose() }
          $fixtureRetainedFiles.Clear()
          foreach ($handle in $fixtureRunHandles) {
            [HlClient.CampaignFixture.OwnedDirectory]::MarkExactEntryForDeletion(
                $handle)
            $handle.Dispose()
          }
          $fixtureRunHandles.Clear()
          if (@(Get-ChildItem -LiteralPath $resolvedFixture -Force).Count -ne 0) {
            throw 'Metadata fixture gained an entry during cleanup; refusing root deletion.'
          }
          [HlClient.CampaignFixture.OwnedDirectory]::MarkExactEntryForDeletion(
              $fixtureRootHandle)
          $fixtureRootHandle.Dispose()
          $fixtureRootHandle = $null
        } finally { }
      }
      if ($fixtureParentCreated -and $null -ne $fixtureParentHandle -and
          -not $fixtureParentHandle.IsInvalid) {
        $parentItem = Get-Item -LiteralPath $fixtureParent -Force
        if (-not $parentItem.PSIsContainer -or
            ($parentItem.Attributes -band
                [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            @(Get-ChildItem -LiteralPath $fixtureParent -Force).Count -ne 0) {
          throw 'Owned metadata fixture parent drifted; refusing cleanup.'
        }
        [HlClient.CampaignFixture.OwnedDirectory]::MarkExactEntryForDeletion(
            $fixtureParentHandle)
      }
    } finally {
      foreach ($handle in $fixtureRetainedFiles) { $handle.Dispose() }
      foreach ($handle in $fixtureRunHandles) { $handle.Dispose() }
      if ($null -ne $fixtureRootHandle) {
        $fixtureRootHandle.Dispose()
      }
      if ($null -ne $fixtureParentHandle) {
        $fixtureParentHandle.Dispose()
      }
      if ($null -ne $fixtureRepositoryHandle) {
        $fixtureRepositoryHandle.Dispose()
      }
    }
  }
}
if ((Get-PathObservation $manualRoot) -cne $beforeManual -or
    ($beforeManualParent -ceq 'absent' -and
        (Get-PathObservation $manualParent) -cne 'absent') -or
    (Get-PathObservation $evidencePath) -cne $beforeEvidence) {
    throw 'Fake campaign resume tests changed capture artifacts or evidence.'
}

Write-Output '[stock-runtime-campaign-resume-test] corpus=fake-in-memory'
Write-Output "[stock-runtime-campaign-resume-test] filesystem-corpus=$filesystemFixtureResult"
Write-Output "[stock-runtime-campaign-resume-test] filesystem-classification=$filesystemClassification"
Write-Output '[stock-runtime-campaign-resume-test] campaign-summary-determinism=verified'
Write-Output '[stock-runtime-campaign-resume-test] walker-capability-rejections=1'
Write-Output '[stock-runtime-campaign-resume-test] manifest-mutation-rejections=3'
Write-Output '[stock-runtime-campaign-resume-test] deterministic-missing-slots=verified'
Write-Output '[stock-runtime-campaign-resume-test] retired-tail-inflation-rejections=1'
Write-Output '[stock-runtime-campaign-resume-test] run-reparse-rejections=1'
Write-Output '[stock-runtime-campaign-resume-test] failure-publication-mutations=3'
Write-Output '[stock-runtime-campaign-resume-test] fatal-resume-category-rejections=5'
Write-Output '[stock-runtime-campaign-resume-test] fatal-resume-state-rejections=1'
Write-Output '[stock-runtime-campaign-resume-test] walker-nonzero-exit-rejections=1'
Write-Output '[stock-runtime-campaign-resume-test] canary-mutation-rejections=4'
Write-Output '[stock-runtime-campaign-resume-test] unbound-canary-rebind-rejections=1'
Write-Output "[stock-runtime-campaign-resume-test] cleanup-drift-rejections=$cleanupDriftRejections"
Write-Output '[stock-runtime-campaign-resume-test] raw-artifacts-written=0'
Write-Output '[stock-runtime-campaign-resume-test] artifacts-retained=0'
Write-Output '[stock-runtime-campaign-resume-test] evidence-written=0'
Write-Output '[stock-runtime-campaign-resume-test] result=success'
