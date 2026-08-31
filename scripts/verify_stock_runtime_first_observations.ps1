#requires -Version 5.1

<#
.SYNOPSIS
Verifies a bounded stock-runtime first-observation corpus.

.DESCRIPTION
Enumerates exact ignored run IDs, validates final manifests/attestations, runs
the production checker twice, runs the independent PowerShell walker twice and
compares structural summaries, including both reconnect generations. It never
creates evidence or prints raw bytes.
Threshold failure is explicit and leaves the evidence JSON absent.
#>
[CmdletBinding(DefaultParameterSetName = 'Corpus')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Corpus')]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Corpus')]
    [ValidateNotNullOrEmpty()]
    [string]$CheckerPath,

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidateRange(1, 4096)]
    [int]$MinimumAcceptedRuns = 24,

    [Parameter(ParameterSetName = 'Corpus')]
    [ValidateRange(1, 100000000)]
    [Int64]$MinimumSequencedServerPackets = 1000,

    [Parameter(Mandatory = $true, ParameterSetName = 'EvidencePolicy')]
    [switch]$ValidateEvidencePolicy,

    [Parameter(Mandatory = $true, ParameterSetName = 'BoundedReadPolicy')]
    [switch]$ValidateBoundedReadPolicy
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$requiredCaptureRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime')).TrimEnd('\', '/')
$requiredCanaryRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime-canary')).TrimEnd('\', '/')
$canaryManifestSchema = 'hlclient.stock-runtime-pre-campaign-canary.v1'
$evidencePath = Join-Path $repositoryRoot `
    'docs\evidence\GOLDSRC_STOCK_RUNTIME_FIRST_OBSERVATIONS.json'
$walkerPath = Join-Path $PSScriptRoot 'walk_stock_runtime_transport.ps1'
$maximumRuns = 4096
$requiredEvidenceImplementationMessage =
    'Complete stock runtime capture campaign lifecycle'

function Test-ExternalTargetMetadata {
    param([Int64]$Count, [string]$Profile)
    return (($Count -eq 0 -and $Profile -ceq 'none') -or
        ($Count -gt 0 -and
            $Profile -ceq 'reviewed-non-executable-v1'))
}

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $full = [IO.Path]::GetFullPath($Path)
    $pathRoot = [IO.Path]::GetPathRoot($full)
    $current = $pathRoot
    foreach ($component in @($full.Substring($pathRoot.Length) -split '[\\/]' |
            Where-Object { $_ })) {
        $current = [IO.Path]::Combine($current, $component)
        if (-not (Test-Path -LiteralPath $current)) { break }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label contains a reparse point."
        }
    }
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
    $property = $item.PSObject.Properties['LinkType']
    if ($null -eq $property -or -not [string]::IsNullOrEmpty([string]$property.Value)) {
        throw "$Label must be an unlinked regular file."
    }
}

function Initialize-BoundedEvidenceReaderNative {
    if ($null -ne ('Hlclient.StockRuntimeBoundedEvidenceFile' -as [type])) {
        return
    }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Hlclient
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct StockRuntimeEvidenceByHandleInformation
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

    public sealed class StockRuntimeBoundedEvidenceFile : IDisposable
    {
        private const uint GenericRead = 0x80000000;
        private const uint OpenExisting = 3;
        private const uint FileAttributeDirectory = 0x10;
        private const uint FileAttributeSparseFile = 0x200;
        private const uint FileAttributeReparsePoint = 0x400;
        private const uint FileFlagBackupSemantics = 0x02000000;
        private const uint FileFlagOpenReparsePoint = 0x00200000;
        private const uint FileFlagSequentialScan = 0x08000000;
        private const int FileStreamInfo = 7;
        private const int StreamInformationBytes = 65536;
        private const int StreamInformationHeaderBytes = 24;

        private readonly SafeFileHandle handle;
        private readonly string expectedPath;
        private readonly Snapshot initial;
        private bool disposed;

        private struct Snapshot
        {
            internal uint Attributes;
            internal uint CreationLow;
            internal uint CreationHigh;
            internal uint WriteLow;
            internal uint WriteHigh;
            internal uint VolumeSerial;
            internal ulong Size;
            internal uint Links;
            internal uint FileIndexHigh;
            internal uint FileIndexLow;
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
            SafeFileHandle file,
            out StockRuntimeEvidenceByHandleInformation information);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandleEx(
            SafeFileHandle file, int informationClass,
            IntPtr information, uint bufferSize);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode,
            SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(
            SafeFileHandle file, StringBuilder path, uint pathLength,
            uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ReadFile(
            SafeFileHandle file, byte[] buffer, uint bytesToRead,
            out uint bytesRead, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetFilePointerEx(
            SafeFileHandle file, long distance, out long newPosition,
            uint moveMethod);

        private StockRuntimeBoundedEvidenceFile(
            SafeFileHandle retainedHandle, string canonicalPath,
            int maximumBytes)
        {
            handle = retainedHandle;
            expectedPath = canonicalPath;
            initial = Information(handle);
            RequireOrdinaryBounded(initial, maximumBytes);
            RequireExactPath(handle, expectedPath);
            RequireOnlyDefaultStream(handle);
            Text = ReadStrictUtf8(handle, initial.Size);
            ValidateUnchanged();
        }

        public string Text { get; private set; }

        public static StockRuntimeBoundedEvidenceFile Open(
            string path, int maximumBytes)
        {
            if (String.IsNullOrEmpty(path) || maximumBytes < 2)
                throw new ArgumentException(
                    "Bounded evidence reader arguments are invalid.");
            string canonicalPath = Path.GetFullPath(path).TrimEnd('\\', '/');
            SafeFileHandle opened = CreateFileW(
                canonicalPath, GenericRead, 0, IntPtr.Zero, OpenExisting,
                FileFlagBackupSemantics | FileFlagOpenReparsePoint |
                    FileFlagSequentialScan,
                IntPtr.Zero);
            if (opened == null || opened.IsInvalid)
            {
                int error = Marshal.GetLastWin32Error();
                if (opened != null) opened.Dispose();
                throw new Win32Exception(
                    error, "Bounded evidence file open failed.");
            }
            try
            {
                return new StockRuntimeBoundedEvidenceFile(
                    opened, canonicalPath, maximumBytes);
            }
            catch
            {
                opened.Dispose();
                throw;
            }
        }

        public void ValidateUnchanged()
        {
            RequireOpen();
            Snapshot current = Information(handle);
            if (!SameSnapshot(initial, current))
                throw new InvalidDataException(
                    "Bounded evidence file changed while retained.");
            RequireExactPath(handle, expectedPath);
            RequireOnlyDefaultStream(handle);
        }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            handle.Dispose();
        }

        private void RequireOpen()
        {
            if (disposed || handle == null || handle.IsInvalid ||
                handle.IsClosed)
                throw new ObjectDisposedException(
                    "StockRuntimeBoundedEvidenceFile");
        }

        private static Snapshot Information(SafeFileHandle file)
        {
            StockRuntimeEvidenceByHandleInformation information;
            if (!GetFileInformationByHandle(file, out information))
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "Bounded evidence identity query failed.");
            Snapshot result = new Snapshot();
            result.Attributes = information.FileAttributes;
            result.CreationLow = (uint)information.CreationTime.dwLowDateTime;
            result.CreationHigh = (uint)information.CreationTime.dwHighDateTime;
            result.WriteLow = (uint)information.LastWriteTime.dwLowDateTime;
            result.WriteHigh = (uint)information.LastWriteTime.dwHighDateTime;
            result.VolumeSerial = information.VolumeSerialNumber;
            result.Size = ((ulong)information.FileSizeHigh << 32) |
                information.FileSizeLow;
            result.Links = information.NumberOfLinks;
            result.FileIndexHigh = information.FileIndexHigh;
            result.FileIndexLow = information.FileIndexLow;
            return result;
        }

        private static void RequireOrdinaryBounded(
            Snapshot value, int maximumBytes)
        {
            if ((value.Attributes & FileAttributeReparsePoint) != 0)
                throw new InvalidDataException(
                    "Bounded evidence file is a reparse point.");
            if ((value.Attributes & FileAttributeDirectory) != 0)
                throw new InvalidDataException(
                    "Bounded evidence path is not a regular file.");
            if ((value.Attributes & FileAttributeSparseFile) != 0)
                throw new InvalidDataException(
                    "Bounded evidence file is sparse.");
            if (value.Links != 1)
                throw new InvalidDataException(
                    "Bounded evidence file is hardlinked.");
            if (value.Size < 2 || value.Size > (ulong)maximumBytes)
                throw new InvalidDataException(
                    "Bounded evidence file length is outside its bound.");
        }

        private static bool SameSnapshot(Snapshot left, Snapshot right)
        {
            return left.Attributes == right.Attributes &&
                left.CreationLow == right.CreationLow &&
                left.CreationHigh == right.CreationHigh &&
                left.WriteLow == right.WriteLow &&
                left.WriteHigh == right.WriteHigh &&
                left.VolumeSerial == right.VolumeSerial &&
                left.Size == right.Size && left.Links == right.Links &&
                left.FileIndexHigh == right.FileIndexHigh &&
                left.FileIndexLow == right.FileIndexLow;
        }

        private static string FinalPath(SafeFileHandle file)
        {
            StringBuilder buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(
                file, buffer, (uint)buffer.Capacity, 0);
            if (length == 0 || length >= buffer.Capacity)
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "Bounded evidence final-path query failed.");
            string value = buffer.ToString();
            const string uncPrefix = @"\\?\UNC\";
            const string extendedPrefix = @"\\?\";
            if (value.StartsWith(uncPrefix, StringComparison.Ordinal))
                value = @"\\" + value.Substring(uncPrefix.Length);
            else if (value.StartsWith(
                         extendedPrefix, StringComparison.Ordinal))
                value = value.Substring(extendedPrefix.Length);
            return value.TrimEnd('\\', '/');
        }

        private static void RequireExactPath(
            SafeFileHandle file, string expected)
        {
            if (!String.Equals(
                    FinalPath(file), expected,
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException(
                    "Bounded evidence file resolved through a path alias.");
        }

        private static void RequireOnlyDefaultStream(SafeFileHandle file)
        {
            IntPtr buffer = Marshal.AllocHGlobal(StreamInformationBytes);
            try
            {
                if (!GetFileInformationByHandleEx(
                        file, FileStreamInfo, buffer,
                        StreamInformationBytes))
                    throw new Win32Exception(
                        Marshal.GetLastWin32Error(),
                        "Bounded evidence stream query failed.");
                int offset = 0;
                int count = 0;
                for (;;)
                {
                    if (offset < 0 ||
                        offset > StreamInformationBytes -
                            StreamInformationHeaderBytes)
                        throw new InvalidDataException(
                            "Bounded evidence stream metadata is invalid.");
                    uint next = unchecked((uint)Marshal.ReadInt32(
                        buffer, offset));
                    uint nameBytes = unchecked((uint)Marshal.ReadInt32(
                        buffer, offset + 4));
                    if ((nameBytes & 1) != 0 ||
                        nameBytes > Int32.MaxValue ||
                        (ulong)offset + StreamInformationHeaderBytes +
                            nameBytes > StreamInformationBytes)
                        throw new InvalidDataException(
                            "Bounded evidence stream metadata is invalid.");
                    string name = Marshal.PtrToStringUni(
                        IntPtr.Add(
                            buffer,
                            offset + StreamInformationHeaderBytes),
                        (int)nameBytes / 2);
                    ++count;
                    if (count != 1 || !String.Equals(
                            name, "::$DATA", StringComparison.Ordinal))
                        throw new InvalidDataException(
                            "Bounded evidence file contains an alternate stream.");
                    if (next == 0) break;
                    if (next < StreamInformationHeaderBytes ||
                        (next & 7) != 0 ||
                        (ulong)offset + next >= StreamInformationBytes)
                        throw new InvalidDataException(
                            "Bounded evidence stream metadata is invalid.");
                    offset += (int)next;
                }
                if (count != 1)
                    throw new InvalidDataException(
                        "Bounded evidence stream metadata is invalid.");
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }

        private static string ReadStrictUtf8(
            SafeFileHandle file, ulong size)
        {
            long position;
            if (!SetFilePointerEx(file, 0, out position, 0) || position != 0)
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "Bounded evidence seek failed.");
            byte[] bytes = new byte[(int)size];
            byte[] chunk = new byte[65536];
            int offset = 0;
            while (offset < bytes.Length)
            {
                uint requested = (uint)Math.Min(
                    chunk.Length, bytes.Length - offset);
                uint read;
                if (!ReadFile(
                        file, chunk, requested, out read, IntPtr.Zero))
                    throw new Win32Exception(
                        Marshal.GetLastWin32Error(),
                        "Bounded evidence read failed.");
                if (read == 0 || read > requested)
                    throw new EndOfStreamException(
                        "Bounded evidence file ended early.");
                Buffer.BlockCopy(
                    chunk, 0, bytes, offset, (int)read);
                offset += (int)read;
            }
            uint extraRead;
            if (!ReadFile(
                    file, chunk, 1, out extraRead, IntPtr.Zero))
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "Bounded evidence EOF check failed.");
            if (extraRead != 0)
                throw new InvalidDataException(
                    "Bounded evidence file exceeded its retained size.");
            int start = bytes.Length >= 3 && bytes[0] == 0xEF &&
                    bytes[1] == 0xBB && bytes[2] == 0xBF
                ? 3
                : 0;
            try
            {
                return new UTF8Encoding(false, true).GetString(
                    bytes, start, bytes.Length - start);
            }
            catch (DecoderFallbackException exception)
            {
                throw new InvalidDataException(
                    "Bounded evidence file is not strict UTF-8.",
                    exception);
            }
        }
    }
}
'@
}

function Open-BoundedEvidenceText {
    param([string]$Path, [int]$MaximumBytes, [string]$Label)
    Assert-NoReparsePointInExistingPath $Path $Label
    Initialize-BoundedEvidenceReaderNative
    $reader = $null
    try {
        $reader = [Hlclient.StockRuntimeBoundedEvidenceFile]::Open(
            $Path, $MaximumBytes)
        Assert-NoReparsePointInExistingPath $Path $Label
        $reader.ValidateUnchanged()
        return [pscustomobject]@{ Reader = $reader; Text = $reader.Text }
    } catch {
        if ($null -ne $reader) { $reader.Dispose() }
        throw
    }
}

function Initialize-BoundedEvidenceReaderFixtureNative {
    if ($null -ne ('Hlclient.StockRuntimeEvidenceReaderFixture' -as [type])) {
        return
    }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Hlclient
{
    public static class StockRuntimeEvidenceReaderFixture
    {
        private const uint GenericWrite = 0x40000000;
        private const uint CreateNew = 1;
        private const uint FileAttributeNormal = 0x80;
        private const uint FsctlSetSparse = 0x000900C4;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode,
            SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(
            string fileName, uint desiredAccess, uint shareMode,
            IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DeviceIoControl(
            SafeFileHandle file, uint controlCode,
            IntPtr input, uint inputBytes,
            IntPtr output, uint outputBytes,
            out uint returnedBytes, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WriteFile(
            SafeFileHandle file, byte[] buffer, uint bytesToWrite,
            out uint bytesWritten, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FlushFileBuffers(SafeFileHandle file);

        public static void CreateSparseFile(string path)
        {
            SafeFileHandle file = CreateFileW(
                Path.GetFullPath(path), GenericWrite, 0, IntPtr.Zero,
                CreateNew, FileAttributeNormal, IntPtr.Zero);
            if (file == null || file.IsInvalid)
            {
                int error = Marshal.GetLastWin32Error();
                if (file != null) file.Dispose();
                throw new Win32Exception(
                    error, "Sparse evidence fixture open failed.");
            }
            using (file)
            {
                uint returned;
                if (!DeviceIoControl(
                        file, FsctlSetSparse, IntPtr.Zero, 0,
                        IntPtr.Zero, 0, out returned, IntPtr.Zero))
                    throw new Win32Exception(
                        Marshal.GetLastWin32Error(),
                        "Sparse evidence fixture marking failed.");
                byte[] bytes = new byte[] { (byte)'{', (byte)'}' };
                uint written;
                if (!WriteFile(
                        file, bytes, (uint)bytes.Length,
                        out written, IntPtr.Zero) ||
                    written != bytes.Length || !FlushFileBuffers(file))
                    throw new Win32Exception(
                        Marshal.GetLastWin32Error(),
                        "Sparse evidence fixture write failed.");
            }
        }
    }
}
'@
}

function Read-BoundedJson {
    param([string]$Path, [int]$MaximumBytes, [string]$Label)
    $bounded = Open-BoundedEvidenceText $Path $MaximumBytes $Label
    try {
        try {
            $value = ConvertFrom-Json -InputObject ([string]$bounded.Text) `
                -ErrorAction Stop
        } catch {
            throw "$Label is invalid JSON."
        }
        $bounded.Reader.ValidateUnchanged()
        return $value
    } finally {
        $bounded.Reader.Dispose()
    }
}

function Get-StringSha256 {
    param([string]$Value)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString(
                $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Value)))).Replace(
            '-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function Assert-ExactImplementationCommit {
    param([string]$Commit, [string]$Label)
    if ($Commit -cnotmatch '^[0-9a-f]{40}$' -or
        $Commit -ceq '0000000000000000000000000000000000000000') {
        throw "$Label is not a canonical nonzero commit SHA."
    }
    $gitCommand = Get-Command git.exe -ErrorAction Stop
    $commitExists = @(& $gitCommand.Source -C $repositoryRoot cat-file -t `
        $Commit 2>$null)
    if ($LASTEXITCODE -ne 0 -or $commitExists.Count -ne 1 -or
        $commitExists[0] -cne 'commit') {
        throw "$Label does not resolve to a commit."
    }
    $commitMessage = @(& $gitCommand.Source -C $repositoryRoot show -s `
        --format=%s $Commit 2>$null)
    if ($LASTEXITCODE -ne 0 -or $commitMessage.Count -ne 1 -or
        $commitMessage[0] -cne $requiredEvidenceImplementationMessage) {
        throw "$Label has the wrong exact implementation subject."
    }
    & $gitCommand.Source -C $repositoryRoot merge-base --is-ancestor `
        $Commit HEAD 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "$Label is not an ancestor of the verified checkout."
    }
}

function Assert-ExactFilePair {
    param([string]$StagedPath, [string]$FinalPath, [string]$Label)
    $staged = Get-Item -LiteralPath $StagedPath -Force
    $final = Get-Item -LiteralPath $FinalPath -Force
    if ($staged.Length -ne $final.Length -or
        (Get-FileHash -LiteralPath $StagedPath -Algorithm SHA256).Hash -cne
            (Get-FileHash -LiteralPath $FinalPath -Algorithm SHA256).Hash) {
        throw "$Label staged/final leaves are not byte-identical."
    }
}

function Get-StrictInteger {
    param([object]$Value, [string]$Name, [Int64]$Minimum, [Int64]$Maximum)
    $property = $Value.PSObject.Properties[$Name]
    if ($null -eq $property -or $property.Value -is [bool] -or
        $property.Value -isnot [ValueType]) { throw "$Name is not an integer." }
    [Int64]$number = $property.Value
    if ([double]$property.Value -ne [double]$number -or
        $number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Name is outside its bound."
    }
    return $number
}

function Get-CampaignFailurePublication {
    param([string]$FailureCategory)
    if ($FailureCategory -ceq 'bounded-session-incomplete') {
        return 'incomplete'
    }
    return 'rejected'
}

function Test-CampaignFailureBlocksResume {
    param([string]$FailureCategory)
    return (Get-CampaignFailurePublication $FailureCategory) -cne 'incomplete'
}

function Assert-NoCampaignResumeBlockingFailures {
    param([int]$Count)
    if ($Count -lt 0 -or $Count -gt $maximumRuns) {
        throw 'Campaign resume-blocking failure count is outside its bound.'
    }
    if ($Count -ne 0) {
        throw ('Campaign verification is blocked by retained rejected/fatal ' +
            'runs. Only bounded-session-incomplete may coexist with a ' +
            'resumable or evidence-eligible campaign.')
    }
}

function Assert-CampaignManifestSummaryIdentity {
    param([object]$Manifest, [object]$Summary)
    $commit = [string]$Manifest.implementation_commit
    $hash = [string]$Manifest.campaign_structural_sha256
    if ($commit -cnotmatch '^[0-9a-f]{40}$' -or
        $commit -ceq '0000000000000000000000000000000000000000' -or
        $hash -cnotmatch '^[0-9a-f]{64}$' -or
        -not $Summary.ContainsKey('implementation-commit') -or
        -not $Summary.ContainsKey('structural-hash') -or
        $Summary['implementation-commit'] -cne $commit -or
        $Summary['structural-hash'] -cne $hash) {
        throw 'Campaign summary identity differs from the persisted manifest.'
    }
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Allowed, [string]$Label)
    if ($null -eq $Value -or $Value -is [ValueType] -or $Value -is [string]) {
        throw "$Label must be a JSON object."
    }
    $names = @($Value.PSObject.Properties.Name)
    if ($names.Count -ne $Allowed.Count -or
        @($names | Sort-Object -Unique).Count -ne $Allowed.Count) {
        throw "$Label has duplicate, missing or unknown properties."
    }
    foreach ($name in $names) {
        if ($Allowed -cnotcontains $name) {
            throw "$Label contains forbidden property '$name'."
        }
    }
}

function Get-CanaryBindingSha256 {
    param([object]$Value)
    $canonical = @(
        "schema=$([string]$Value.schema)",
        "implementation_commit=$([string]$Value.implementation_commit)",
        "run_id=$([string]$Value.run_id)",
        "map_category=$([string]$Value.map_category)",
        "scenario=$([string]$Value.scenario)",
        "external_target_profile=$([string]$Value.external_target_profile)",
        "external_target_count=$([string]$Value.external_target_count)",
        "accepted_evidence_run=$(([bool]$Value.accepted_evidence_run).ToString().ToLowerInvariant())",
        "delivered_sequenced_s2c_count=$([string]$Value.delivered_sequenced_s2c_count)",
        "exact_boundary_count=$([string]$Value.exact_boundary_count)",
        "runtime_candidate_count=$([string]$Value.runtime_candidate_count)",
        "candidate_stability=$([string]$Value.candidate_stability)",
        "profile_fingerprint=$([string]$Value.profile_fingerprint)",
        "transport_structural_sha256=$([string]$Value.transport_structural_sha256)",
        "replay_structural_sha256=$([string]$Value.replay_structural_sha256)",
        "checker_output_sha256=$([string]$Value.checker_output_sha256)",
        "accepted_before_campaign=$(([bool]$Value.accepted_before_campaign).ToString().ToLowerInvariant())",
        "counted_in_campaign=$(([bool]$Value.counted_in_campaign).ToString().ToLowerInvariant())") -join '|'
    return Get-StringSha256 $canonical
}

function Assert-CanaryManifestContract {
    param([object]$Manifest, [object]$Expected)
    $names = @(
        'schema', 'implementation_commit', 'run_id', 'map_category', 'scenario',
        'external_target_profile', 'external_target_count',
        'accepted_evidence_run', 'delivered_sequenced_s2c_count',
        'exact_boundary_count', 'runtime_candidate_count', 'candidate_stability',
        'profile_fingerprint', 'transport_structural_sha256',
        'replay_structural_sha256', 'checker_output_sha256',
        'accepted_before_campaign', 'counted_in_campaign',
        'canary_structural_sha256')
    Assert-ExactProperties $Manifest $names 'pre-campaign canary manifest'
    if ([string]$Manifest.schema -cne $canaryManifestSchema -or
        [string]$Manifest.implementation_commit -cnotmatch '^[0-9a-f]{40}$' -or
        [string]$Manifest.run_id -cnotmatch '^[0-9a-f]{32}$' -or
        [string]$Manifest.map_category -cne 'boot_camp' -or
        [string]$Manifest.scenario -cne 'baseline' -or
        [string]$Manifest.external_target_count -cnotmatch '^(?:0|[1-9][0-9]*)$' -or
        [Int64]$Manifest.external_target_count -gt 4096 -or
        -not (Test-ExternalTargetMetadata `
            ([Int64]$Manifest.external_target_count) `
            ([string]$Manifest.external_target_profile)) -or
        $Manifest.accepted_evidence_run -isnot [bool] -or
        -not [bool]$Manifest.accepted_evidence_run -or
        [string]$Manifest.delivered_sequenced_s2c_count -cnotmatch
            '^(?:[1-9][0-9]{2,})$' -or
        [Int64]$Manifest.delivered_sequenced_s2c_count -lt 100 -or
        [Int64]$Manifest.delivered_sequenced_s2c_count -gt 131072 -or
        [string]$Manifest.exact_boundary_count -cne '1' -or
        [string]$Manifest.runtime_candidate_count -cne '1' -or
        [string]$Manifest.candidate_stability -cne 'single_observation' -or
        [string]$Manifest.profile_fingerprint -cnotmatch '^[0-9a-f]{64}$' -or
        [string]$Manifest.transport_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        [string]$Manifest.replay_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        [string]$Manifest.checker_output_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $Manifest.accepted_before_campaign -isnot [bool] -or
        -not [bool]$Manifest.accepted_before_campaign -or
        $Manifest.counted_in_campaign -isnot [bool] -or
        [bool]$Manifest.counted_in_campaign -or
        [string]$Manifest.canary_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        [string]$Manifest.canary_structural_sha256 -cne
            (Get-CanaryBindingSha256 $Manifest)) {
        throw 'Pre-campaign canary manifest violates its exact policy.'
    }
    foreach ($name in $names) {
        if ([string]$Manifest.$name -cne [string]$Expected.$name) {
            throw "Pre-campaign canary manifest disagrees at '$name'."
        }
    }
}

function Assert-SanitizedEvidenceText {
    param([string]$Text)
    # The exact object allowlists below are the primary policy. These lexical
    # checks additionally fail closed on accidental sensitive field names or
    # values before any evidence is accepted.
    $propertyNames = @([regex]::Matches(
            $Text, '"(?<name>[A-Za-z0-9_-]+)"\s*:') |
        ForEach-Object { $_.Groups['name'].Value })
    $forbiddenProperty = @($propertyNames | Where-Object {
            $_ -match '(?i)(?:^|[_-])(?:raw|auth(?:entication)?|ticket|player|steam[_-]?id|user[_-]?id|identity|fingerprint|path|(?:ip|ipv4|ipv6)[_-]?address|port|config|process[_-]?log|entity|screenshot)(?:$|[_-])'
        }).Count -ne 0
    if ($forbiddenProperty -or
        $Text -match '(?i)(?:[A-Z]:\\|/Users/|\\Users\\|steamapps[\\/]|https?://|\b(?:127\.0\.0\.1|10\.[0-9.]+|192\.168\.[0-9.]+|172\.(?:1[6-9]|2[0-9]|3[01])\.[0-9.]+|::1)\b)' -or
        $Text -match '(?i)"[^"]*(?:steam ticket|authentication bytes|player name|raw payload|process log|private config|screenshot)[^"]*"') {
        throw 'First-observation evidence contains a forbidden field or value.'
    }
}

function Assert-FirstObservationGeometry {
    param(
        [object]$Boundary,
        [Int64]$CandidateBitWidth,
        [string]$FirstCandidate,
        [string]$Label)
    [void](Get-StrictInteger $Boundary replay_payload_ordinal 0 65536)
    [void](Get-StrictInteger $Boundary corpus_observed_ordinal 0 65535)
    [void](Get-StrictInteger $Boundary delivery_ordinal 0 131071)
    [void](Get-StrictInteger $Boundary byte_offset 0 1048576)
    [void](Get-StrictInteger $Boundary bit_offset 0 7)
    [void](Get-StrictInteger $Boundary source_netchan_sequence 0 1073741823)
    [void](Get-StrictInteger $Boundary source_payload_byte_count 1 1048576)
    [void](Get-StrictInteger $Boundary source_payload_bit_count 8 8388608)
    [void](Get-StrictInteger $Boundary next_unconsumed_bit_count 1 8388608)
    if ($Boundary.reassembled -isnot [bool] -or
        $Boundary.decompressed -isnot [bool] -or
        $Boundary.byte_aligned -isnot [bool] -or
        [Int64]$Boundary.bit_offset -gt 7 -or
        [Int64]$Boundary.source_payload_byte_count -lt 1 -or
        [Int64]$Boundary.source_payload_bit_count -ne
            ([Int64]$Boundary.source_payload_byte_count * 8) -or
        (([Int64]$Boundary.byte_offset * 8) + [Int64]$Boundary.bit_offset +
            [Int64]$Boundary.next_unconsumed_bit_count) -ne
                [Int64]$Boundary.source_payload_bit_count -or
        [Int64]$Boundary.next_unconsumed_bit_count -lt 1 -or
        [bool]$Boundary.byte_aligned -ne ([Int64]$Boundary.bit_offset -eq 0) -or
        $CandidateBitWidth -lt 1 -or $CandidateBitWidth -gt 8 -or
        $CandidateBitWidth -gt [Int64]$Boundary.next_unconsumed_bit_count -or
        $FirstCandidate -cnotmatch '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
        [int]($FirstCandidate -replace '^bit-prefix:', '') -gt 255 -or
        (-not $FirstCandidate.StartsWith('bit-prefix:') -and
            (-not [bool]$Boundary.byte_aligned -or $CandidateBitWidth -ne 8)) -or
        ($FirstCandidate.StartsWith('bit-prefix:') -and
            ([bool]$Boundary.byte_aligned -or
                [int]$FirstCandidate.Substring(11) -ge
                    [Math]::Pow(2, $CandidateBitWidth)))) {
        throw "$Label has inconsistent exact cursor/prefix-width geometry."
    }
}

function Convert-PrefixedOutputToValues {
    param([string[]]$Lines, [string]$Prefix, [string[]]$AllowedKeys, [string]$Label)
    if ($Lines.Count -gt 128 -or
        [Text.Encoding]::UTF8.GetByteCount(($Lines -join "`n")) -gt 65536) {
        throw "$Label output exceeded its bound."
    }
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

function Invoke-Checker {
    param([string]$Path, [string]$RunRoot)
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $Path --capture-root $RunRoot --scenario first-observation 2>&1 |
            ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    return [pscustomobject]@{ Lines = $lines; ExitCode = $exitCode }
}

function Invoke-CampaignChecker {
    param(
        [string]$Path, [string]$CampaignRoot,
        [string[]]$WalkerValidatedRunIds)
    $arguments = @('--capture-root', $CampaignRoot,
        '--scenario', 'campaign-summary')
    foreach ($runId in @($WalkerValidatedRunIds | Sort-Object)) {
        if ($runId -cnotmatch '^[0-9a-f]{32}$') {
            throw 'Walker-validated campaign run ID is malformed.'
        }
        $arguments += @('--independent-walker-validated-run', $runId)
    }
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $Path @arguments 2>&1 |
            ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    return [pscustomobject]@{ Lines = $lines; ExitCode = $exitCode }
}

function Invoke-Walker {
    param([string]$Path, [string[]]$Arguments)
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $LASTEXITCODE = 0
        $lines = @(& $Path @Arguments 2>&1 |
            ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    return [pscustomobject]@{ Lines = $lines; ExitCode = $exitCode }
}

function Assert-DeterministicToolPair {
    param([object]$First, [object]$Second, [string]$Label)
    if ($null -eq $First -or $null -eq $Second -or
        $First.ExitCode -isnot [int] -or $Second.ExitCode -isnot [int] -or
        [int]$First.ExitCode -ne 0 -or [int]$Second.ExitCode -ne 0 -or
        ($First.Lines -join "`n") -cne ($Second.Lines -join "`n")) {
        throw "$Label is unsuccessful or non-deterministic."
    }
}

function Invoke-DeterministicWalkerPair {
    param([scriptblock]$Invocation, [string]$Label)
    if ($null -eq $Invocation) {
        throw "$Label invocation is absent."
    }
    $first = & $Invocation
    $second = & $Invocation
    Assert-DeterministicToolPair $first $second $Label
    return $first
}

function Assert-IndependentWalkerMatchesChecker {
    param(
        [object]$WalkerValues,
        [object]$CheckerValues,
        [string]$RunId,
        [bool]$Reconnect,
        [string]$ExpectedExternalTargetProfile,
        [Int64]$ExpectedExternalTargetCount)
    $agreementKeys = @(
        'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
        'delivered-fragment-datagrams', 'boundary-payload-ordinal',
        'boundary-observed-ordinal', 'boundary-delivery-ordinal',
        'boundary-byte-offset', 'boundary-bit-offset',
        'boundary-source-sequence', 'boundary-source-payload-bytes',
        'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
        'boundary-reassembled', 'boundary-decompressed',
        'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
        'replay-structural-hash')
    foreach ($key in $agreementKeys) {
        if (-not $WalkerValues.ContainsKey($key) -or
            -not $CheckerValues.ContainsKey($key) -or
            [string]$WalkerValues[$key] -cne [string]$CheckerValues[$key]) {
            throw "Independent walker and production checker disagree on '$key'."
        }
    }
    if (-not $WalkerValues.ContainsKey('run-id') -or
        [string]$WalkerValues['run-id'] -cne $RunId -or
        [string]$WalkerValues['result'] -cne 'success' -or
        [string]$WalkerValues['final-manifest'] -cne 'accepted' -or
        [string]$WalkerValues['external-target-profile'] -cne
            $ExpectedExternalTargetProfile -or
        [string]$WalkerValues['external-target-count'] -cne
            [string]$ExpectedExternalTargetCount -or
        [string]$WalkerValues['wrong-source-datagrams'] -cne '0' -or
        [string]$WalkerValues['transport-complete'] -cne 'true') {
        throw 'Independent walker did not validate the exact accepted run.'
    }
    if ($Reconnect) {
        foreach ($key in @($reconnectOutputKeys + @(
                    'candidate-recurrence', 'candidate-stability'))) {
            if (-not $WalkerValues.ContainsKey($key) -or
                -not $CheckerValues.ContainsKey($key) -or
                [string]$WalkerValues[$key] -cne
                    [string]$CheckerValues[$key]) {
                throw "Independent reconnect walker disagrees on '$key'."
            }
        }
    }
}

function Get-FirstCandidateStabilityProfile {
    param([object]$Values)
    $keys = @(
        'boundary-bit-offset', 'boundary-byte-aligned',
        'candidate-bit-width', 'first-candidate')
    foreach ($key in $keys) {
        if ($null -eq $Values -or -not $Values.ContainsKey($key)) {
            throw "Candidate stability input lacks '$key'."
        }
    }
    # Transport/delivery ordinals, netchan sequence, payload size and absolute
    # byte offset are occurrence geometry.  They legitimately vary by run and
    # map.  Cross-run candidate identity retains only exact bit alignment,
    # observed prefix width and the neutral candidate representation; the
    # version profile is compared independently below.
    return @($keys | ForEach-Object { [string]$Values[$_] }) -join '|'
}

$canonicalRuntimeScenarios = @(
    'baseline', 'idle-runtime', 'reconnect',
    'drop-server-to-client-transport-ordinal',
    'duplicate-server-to-client-transport-ordinal',
    'reorder-server-to-client-transport-ordinal')
$legacyScenarioAliases = @{
    'drop-server-runtime' = 'drop-server-to-client-transport-ordinal'
    'duplicate-server-runtime' = 'duplicate-server-to-client-transport-ordinal'
    'reorder-server-runtime' = 'reorder-server-to-client-transport-ordinal'
}
$stockRuntimeMapCategories = @('boot_camp', 'crossfire', 'stalkyard')

function Get-CanonicalRuntimeScenario {
    param([string]$Scenario, [string]$Label)
    if ($legacyScenarioAliases.ContainsKey($Scenario)) {
        return [string]$legacyScenarioAliases[$Scenario]
    }
    if ($canonicalRuntimeScenarios -ccontains $Scenario) {
        return $Scenario
    }
    throw "$Label is outside the exact runtime scenario/alias allowlist."
}

function Get-StrictOutputInteger {
    param(
        [object]$Values, [string]$Name, [Int64]$Minimum,
        [Int64]$Maximum, [string]$Label)
    if ($null -eq $Values -or -not $Values.ContainsKey($Name)) {
        throw "$Label lacks '$Name'."
    }
    $text = [string]$Values[$Name]
    [Int64]$number = 0
    if ($text -cnotmatch '^(?:0|[1-9][0-9]*)$' -or
        -not [Int64]::TryParse(
            $text, [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
        $number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Label '$Name' is outside its integer contract."
    }
    return $number
}

function Get-AdmittedCandidateBinding {
    param([object]$Values)
    if ($null -eq $Values -or -not $Values.ContainsKey('first-candidate')) {
        throw "Admitted candidate binding lacks 'first-candidate'."
    }
    return [pscustomobject]@{
        Representation = [string]$Values['first-candidate']
        BitWidth = Get-StrictOutputInteger $Values 'candidate-bit-width' 1 8 `
            'Admitted candidate binding'
    }
}

function Test-EvidenceCandidateMatchesBinding {
    param([object]$EvidenceCandidate, [object]$AdmittedBinding)
    return $null -ne $EvidenceCandidate -and
        $null -ne $AdmittedBinding -and
        [string]$EvidenceCandidate.representation -ceq
            [string]$AdmittedBinding.Representation -and
        (Get-StrictInteger $EvidenceCandidate bit_width 1 8) -eq
            [Int64]$AdmittedBinding.BitWidth
}

function Assert-RunEvidenceBindings {
    param(
        [object]$Run, [object]$Capture, [object]$Version,
        [object]$CheckerValues, [object]$WalkerValues)

    $canonicalScenario = Get-CanonicalRuntimeScenario `
        ([string]$Run.scenario) 'Final manifest scenario'
    $capturedScenario = Get-CanonicalRuntimeScenario `
        ([string]$Capture.scenario) 'Capture metadata scenario'
    if ($canonicalScenario -cne $capturedScenario) {
        throw 'Final manifest scenario disagrees with capture metadata.'
    }
    $mapCategory = [string]$Run.map_category
    if ($stockRuntimeMapCategories -cnotcontains $mapCategory -or
        [string]$Version.map_category -cne $mapCategory) {
        throw 'Final manifest map category disagrees with the immutable version/run observation.'
    }

    $rawDatagrams = Get-StrictInteger $Run raw_datagram_count 1 65536
    $journalEntries = Get-StrictInteger $Run journal_entry_count 1 65536
    $captureObserved = Get-StrictInteger $Capture observed_datagrams 1 65536
    $captureMaximumDurationMs = Get-StrictInteger $Capture `
        maximum_duration_ms 1 300000
    $captureRawBytes = Get-StrictInteger $Capture observed_raw_bytes 1 536870912
    $captureClient = Get-StrictInteger $Capture client_packets 0 65536
    $captureServer = Get-StrictInteger $Capture server_packets 0 65536
    $captureEmitted = Get-StrictInteger $Capture emitted_datagrams 1 131072
    $walkerRaw = Get-StrictOutputInteger $WalkerValues 'raw-datagrams' 1 65536 `
        'Independent walker'
    $walkerJournal = Get-StrictOutputInteger $WalkerValues 'journal-entries' 1 65536 `
        'Independent walker'
    $walkerRawBytes = Get-StrictOutputInteger $WalkerValues 'raw-bytes' 1 536870912 `
        'Independent walker'
    $walkerObservedC2s = Get-StrictOutputInteger $WalkerValues 'observed-c2s' 0 65536 `
        'Independent walker'
    $walkerObservedS2c = Get-StrictOutputInteger $WalkerValues 'observed-s2c' 0 65536 `
        'Independent walker'
    $walkerEmitted = Get-StrictOutputInteger $WalkerValues 'emitted-datagrams' 1 131072 `
        'Independent walker'
    if ($rawDatagrams -ne $journalEntries -or
        $rawDatagrams -ne $captureObserved -or
        $rawDatagrams -ne $walkerRaw -or
        $journalEntries -ne $walkerJournal -or
        $captureRawBytes -ne $walkerRawBytes -or
        $captureClient -ne $walkerObservedC2s -or
        $captureServer -ne $walkerObservedS2c -or
        $captureEmitted -ne $walkerEmitted) {
        throw 'Final manifest, capture metadata and walker raw/journal counters disagree.'
    }

    $sequencedC2s = Get-StrictInteger $Run delivered_sequenced_c2s_count 0 131072
    $sequencedS2c = Get-StrictInteger $Run delivered_sequenced_s2c_count 0 131072
    $fragmentDatagrams = Get-StrictInteger $Run `
        delivered_fragment_datagram_count 0 131072
    $reassembledPayloads = Get-StrictInteger $Run reassembled_payload_count 0 131072
    $decompressedPayloads = Get-StrictInteger $Run decompressed_payload_count 0 131072
    $checkerReplaySequencedC2s = Get-StrictOutputInteger $CheckerValues 'sequenced-c2s' `
        0 131072 'Production checker'
    $checkerReplaySequencedS2c = Get-StrictOutputInteger $CheckerValues 'sequenced-s2c' `
        0 131072 'Production checker'
    $checkerReplayFragments = Get-StrictOutputInteger $CheckerValues 'fragments' `
        0 131072 'Production checker'
    $checkerReplayDuplicates = Get-StrictOutputInteger $CheckerValues `
        'duplicate-packets' 0 131072 'Production checker'
    $checkerReplayOld = Get-StrictOutputInteger $CheckerValues `
        'old-packets' 0 131072 'Production checker'
    $checkerDeliveredSequencedC2s = Get-StrictOutputInteger $CheckerValues `
        'delivered-sequenced-c2s' 0 131072 'Production checker'
    $checkerDeliveredSequencedS2c = Get-StrictOutputInteger $CheckerValues `
        'delivered-sequenced-s2c' 0 131072 'Production checker'
    $checkerDeliveredFragments = Get-StrictOutputInteger $CheckerValues `
        'delivered-fragment-datagrams' `
        0 131072 'Production checker'
    $checkerReassembled = Get-StrictOutputInteger $CheckerValues 'reassembled' `
        0 131072 'Production checker'
    $checkerDecompressed = Get-StrictOutputInteger $CheckerValues 'decompressed' `
        0 131072 'Production checker'
    $walkerSequencedC2s = Get-StrictOutputInteger $WalkerValues `
        'delivered-sequenced-c2s' 0 131072 'Independent walker'
    $walkerSequencedS2c = Get-StrictOutputInteger $WalkerValues `
        'delivered-sequenced-s2c' 0 131072 'Independent walker'
    $walkerFragments = Get-StrictOutputInteger $WalkerValues `
        'delivered-fragment-datagrams' 0 131072 'Independent walker'
    if ($sequencedC2s -ne $checkerDeliveredSequencedC2s -or
        $sequencedC2s -ne $walkerSequencedC2s -or
        $sequencedS2c -ne $checkerDeliveredSequencedS2c -or
        $sequencedS2c -ne $walkerSequencedS2c -or
        $fragmentDatagrams -ne $checkerDeliveredFragments -or
        $fragmentDatagrams -ne $walkerFragments -or
        $reassembledPayloads -ne $checkerReassembled -or
        $decompressedPayloads -ne $checkerDecompressed) {
        throw 'Final manifest transport/replay counters disagree with recomputed facts.'
    }
    $retiredGenerationATailPackets = 0
    if ($canonicalScenario -ceq 'reconnect') {
        $retiredGenerationATailPackets = Get-StrictOutputInteger $CheckerValues `
            'retired-generation-a-server-tail-packets' 0 65536 `
            'Production checker reconnect replay'
    }
    if ($checkerReplaySequencedC2s -gt $checkerDeliveredSequencedC2s -or
        $checkerReplaySequencedS2c -gt $checkerDeliveredSequencedS2c -or
        $checkerReplayFragments -gt $checkerDeliveredFragments -or
        ($checkerReplaySequencedC2s + $checkerReplaySequencedS2c +
            $checkerReplayDuplicates + $checkerReplayOld +
            $retiredGenerationATailPackets) -ne
            ($checkerDeliveredSequencedC2s + $checkerDeliveredSequencedS2c)) {
        throw 'Replay accepted/suppressed accounting disagrees with delivered transport populations.'
    }

    $transportHash = [string]$Run.transport_structural_sha256
    $replayHash = [string]$Run.replay_structural_sha256
    if ($transportHash -cnotmatch '^[0-9a-f]{64}$' -or
        $transportHash -cne [string]$CheckerValues['structural-hash'] -or
        $replayHash -cnotmatch '^[0-9a-f]{64}$' -or
        $replayHash -cne [string]$CheckerValues['replay-structural-hash'] -or
        $replayHash -cne [string]$WalkerValues['replay-structural-hash']) {
        throw 'Final manifest structural hashes disagree with checker/walker facts.'
    }

    $lastObserved = Get-StrictInteger $Run `
        last_observed_transport_timestamp_us 0 300000000
    $lastDeliveredS2c = Get-StrictInteger $Run `
        last_delivered_sequenced_s2c_timestamp_us 0 300000000
    $durationMs = Get-StrictInteger $Run duration_ms 0 390000
    $walkerLastObserved = Get-StrictOutputInteger $WalkerValues `
        'last-observed-timestamp-us' 0 300000000 'Independent walker'
    $walkerLastDeliveredS2c = Get-StrictOutputInteger $WalkerValues `
        'last-delivered-sequenced-s2c-timestamp-us' 0 300000000 `
        'Independent walker'
    if ($lastObserved -ne $walkerLastObserved -or
        $lastDeliveredS2c -ne $walkerLastDeliveredS2c) {
        throw 'Final manifest timestamps disagree with the independently walked journal.'
    }
    if ([string]$Run.client_ready_status -cne 'true' -or
        $lastObserved -gt (($durationMs + 1) * 1000) -or
        $durationMs -gt ($captureMaximumDurationMs + 90000)) {
        throw 'Final manifest duration/readiness is inconsistent with capture bounds.'
    }
    $expectedPerRunStability = if ($canonicalScenario -ceq 'reconnect') {
        'stable_observation'
    } else { 'single_observation' }
    if ([string]$Run.candidate_stability -cne $expectedPerRunStability -or
        [string]$CheckerValues['candidate-stability'] -cne
            $expectedPerRunStability) {
        throw 'Per-run candidate stability disagrees with the checker boundary.'
    }

    return [pscustomobject]@{
        CanonicalScenario = $canonicalScenario
        MapCategory = $mapCategory
        RawDatagrams = $walkerRaw
        # Non-reconnect slots bind peer-delivered populations. Reconnect slots
        # bind the independently replayed A+B populations, excluding the
        # retired-A routing tail retained by the byte-preserving journal.
        SequencedC2s = $(if ($canonicalScenario -ceq 'reconnect') {
                $checkerReplaySequencedC2s
            } else { $checkerDeliveredSequencedC2s })
        SequencedS2c = $(if ($canonicalScenario -ceq 'reconnect') {
                $checkerReplaySequencedS2c
            } else { $checkerDeliveredSequencedS2c })
        FragmentDatagrams = $(if ($canonicalScenario -ceq 'reconnect') {
                $checkerReplayFragments
            } else { $checkerDeliveredFragments })
        ReplayAcceptedSequencedC2s = $checkerReplaySequencedC2s
        ReplayAcceptedSequencedS2c = $checkerReplaySequencedS2c
        ReplayAcceptedFragmentDatagrams = $checkerReplayFragments
        ReplayDuplicatePackets = $checkerReplayDuplicates
        ReplayOldPackets = $checkerReplayOld
        ReassembledPayloads = $checkerReassembled
        DecompressedPayloads = $checkerDecompressed
        LastObservedTimestampUs = $walkerLastObserved
        LastDeliveredSequencedS2cTimestampUs = $walkerLastDeliveredS2c
        DurationMs = $durationMs
        ClientReady = $true
        TransportStructuralHash = $transportHash
        ReplayStructuralHash = $replayHash
        CandidateStability = $expectedPerRunStability
        ClientFileVersion = [string]$Version.client_file_version
        ServerLauncherVersion = [string]$Version.server_launcher_version
        ServerEngineVersion = [string]$Version.server_engine_version
        Protocol = [Int64]$Version.protocol
        ServerBuild = [Int64]$Version.server_build
        SteamBuildId = [Int64]$Version.steam_build_id
        ClientProfileFingerprint = [string]$Version.client_profile_fingerprint
        ServerProfileFingerprint = [string]$Version.server_profile_fingerprint
    }
}

if ($PSCmdlet.ParameterSetName -ceq 'BoundedReadPolicy') {
    if ($env:OS -cne 'Windows_NT') {
        throw 'Bounded evidence reader policy fixtures require Windows.'
    }
    Initialize-BoundedEvidenceReaderNative
    Initialize-BoundedEvidenceReaderFixtureNative
    $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) `
        ('hlclient-evidence-reader-' + [Guid]::NewGuid().ToString('N'))
    $junctionPath = Join-Path $fixtureRoot 'reparse-evidence.json'
    $junctionCreated = $false
    [void][IO.Directory]::CreateDirectory($fixtureRoot)
    try {
        $utf8 = [Text.UTF8Encoding]::new($false)
        $validPath = Join-Path $fixtureRoot 'ordinary.json'
        [IO.File]::WriteAllText($validPath, '{}', $utf8)
        $valid = Open-BoundedEvidenceText $validPath 1048576 `
            'ordinary bounded evidence fixture'
        try {
            if ([string]$valid.Text -cne '{}') {
                throw 'Bounded evidence reader changed ordinary fixture bytes.'
            }
            $valid.Reader.ValidateUnchanged()
        } finally {
            $valid.Reader.Dispose()
        }

        function Assert-BoundedEvidenceReaderRejection {
            param([string]$Path, [string]$ExpectedReason, [string]$Label)
            $opened = $null
            try {
                $opened = Open-BoundedEvidenceText $Path 1048576 $Label
                throw "$Label was unexpectedly accepted."
            } catch {
                $reason = $_.Exception.Message
                if ($null -ne $opened) {
                    throw "$Label was unexpectedly accepted."
                }
                if ($reason -cnotmatch $ExpectedReason) {
                    throw "$Label produced the wrong typed rejection: $reason"
                }
            } finally {
                if ($null -ne $opened) { $opened.Reader.Dispose() }
            }
        }

        $oversizedPath = Join-Path $fixtureRoot 'oversized.json'
        [IO.File]::WriteAllBytes(
            $oversizedPath, [byte[]]::new(1048577))
        Assert-BoundedEvidenceReaderRejection $oversizedPath `
            'outside its bound' 'oversized evidence fixture'

        $sparsePath = Join-Path $fixtureRoot 'sparse.json'
        [Hlclient.StockRuntimeEvidenceReaderFixture]::CreateSparseFile(
            $sparsePath)
        $sparseAttributes = [IO.File]::GetAttributes($sparsePath)
        if (($sparseAttributes -band [IO.FileAttributes]::SparseFile) -eq 0) {
            throw 'Sparse evidence fixture did not retain its sparse attribute.'
        }
        Assert-BoundedEvidenceReaderRejection $sparsePath `
            'is sparse' 'sparse evidence fixture'

        $junctionTarget = Join-Path $fixtureRoot 'reparse-target'
        [void][IO.Directory]::CreateDirectory($junctionTarget)
        [IO.File]::WriteAllText(
            (Join-Path $junctionTarget 'payload.json'), '{}', $utf8)
        [void](New-Item -ItemType Junction -Path $junctionPath `
                -Target $junctionTarget)
        $junctionCreated = $true
        Assert-BoundedEvidenceReaderRejection $junctionPath `
            'reparse point' 'reparse evidence fixture'

        $hardlinkTarget = Join-Path $fixtureRoot 'hardlink-target.json'
        $hardlinkPath = Join-Path $fixtureRoot 'hardlink.json'
        [IO.File]::WriteAllText($hardlinkTarget, '{}', $utf8)
        [void](New-Item -ItemType HardLink -Path $hardlinkPath `
                -Target $hardlinkTarget)
        Assert-BoundedEvidenceReaderRejection $hardlinkPath `
            'hardlinked' 'hardlinked evidence fixture'
    } finally {
        if ($junctionCreated -and
            [IO.Directory]::Exists($junctionPath)) {
            [IO.Directory]::Delete($junctionPath)
        }
        if ([IO.Directory]::Exists($fixtureRoot)) {
            [IO.Directory]::Delete($fixtureRoot, $true)
        }
    }
    if (Test-Path -LiteralPath $fixtureRoot) {
        throw 'Bounded evidence reader fixture cleanup was incomplete.'
    }
    Write-Output '[stock-runtime-bounded-evidence-reader] ordinary-acceptances=1'
    Write-Output '[stock-runtime-bounded-evidence-reader] oversized-rejections=1'
    Write-Output '[stock-runtime-bounded-evidence-reader] sparse-rejections=1'
    Write-Output '[stock-runtime-bounded-evidence-reader] reparse-rejections=1'
    Write-Output '[stock-runtime-bounded-evidence-reader] hardlink-rejections=1'
    Write-Output '[stock-runtime-bounded-evidence-reader] cleanup=exact'
    Write-Output '[stock-runtime-bounded-evidence-reader] result=success'
    return
}

if ($PSCmdlet.ParameterSetName -ceq 'EvidencePolicy') {
    if ((Get-CampaignFailurePublication 'bounded-session-incomplete') -cne
            'incomplete' -or
        (Get-CampaignFailurePublication 'client-ready-not-observed') -cne
            'rejected' -or
        (Get-CampaignFailurePublication 'timeout') -cne 'rejected') {
        throw 'Typed campaign failure publication policy diverged.'
    }
    $fatalResumeCategories = @(
        'network_isolation_privilege_required',
        'research_restoration_failed',
        'external_steam_state_changed',
        'version_profile_mismatch',
        'raw_artifact_integrity_failed')
    $fatalResumeRejections = @($fatalResumeCategories | Where-Object {
            Test-CampaignFailureBlocksResume $_
        }).Count
    if ($fatalResumeRejections -ne $fatalResumeCategories.Count -or
        (Test-CampaignFailureBlocksResume 'bounded-session-incomplete')) {
        throw 'Verifier fatal campaign resume policy failed open.'
    }
    Assert-NoCampaignResumeBlockingFailures 0
    $fatalResumeStateRejections = 0
    try { Assert-NoCampaignResumeBlockingFailures 1 }
    catch { $fatalResumeStateRejections++ }
    if ($fatalResumeStateRejections -ne 1) {
        throw 'Verifier retained fatal campaign state failed open.'
    }
    $fakeCampaignManifest = [pscustomobject]@{
        implementation_commit = ('a' * 40)
        campaign_structural_sha256 = ('b' * 64)
    }
    $fakeCampaignSummary = @{
        'implementation-commit' = ('a' * 40)
        'structural-hash' = ('b' * 64)
    }
    Assert-CampaignManifestSummaryIdentity $fakeCampaignManifest `
        $fakeCampaignSummary
    $campaignIdentityRejected = 0
    $mutatedSummary = $fakeCampaignSummary.Clone()
    $mutatedSummary['structural-hash'] = ('c' * 64)
    try {
        Assert-CampaignManifestSummaryIdentity $fakeCampaignManifest `
            $mutatedSummary
    } catch { $campaignIdentityRejected++ }
    $mutatedSummary = $fakeCampaignSummary.Clone()
    $mutatedSummary['implementation-commit'] = ('d' * 40)
    try {
        Assert-CampaignManifestSummaryIdentity $fakeCampaignManifest `
            $mutatedSummary
    } catch { $campaignIdentityRejected++ }
    if ($campaignIdentityRejected -ne 2) {
        throw 'Campaign manifest/summary identity mutations failed open.'
    }
    $topLevelKeys = @(
        'schema', 'implementation_commit', 'stock_profile', 'isolation_profile',
        'run_counts', 'map_scenario_ordinals', 'transport_counts',
        'observation_counts', 'boundary', 'candidate',
        'transport_structural_hashes', 'replay_structural_hashes', 'restoration')
    $validShape = [ordered]@{}
    foreach ($key in $topLevelKeys) { $validShape[$key] = $null }
    Assert-ExactProperties ([pscustomobject]$validShape) $topLevelKeys `
        'policy self-test valid shape'
    Assert-SanitizedEvidenceText (
        ([pscustomobject]$validShape | ConvertTo-Json -Depth 4 -Compress))

    $forbiddenRejected = 0
    $forbiddenShape = [ordered]@{}
    foreach ($key in $topLevelKeys) { $forbiddenShape[$key] = $null }
    $forbiddenShape['raw_payload'] = '00'
    try {
        Assert-ExactProperties ([pscustomobject]$forbiddenShape) $topLevelKeys `
            'policy self-test forbidden shape'
    } catch { $forbiddenRejected++ }
    try { Assert-SanitizedEvidenceText '{"schema":"v1","raw_payload":"00"}' }
    catch { $forbiddenRejected++ }
    if ($forbiddenRejected -ne 2) {
        throw 'Evidence policy did not reject both forbidden-key paths.'
    }

    $validBoundary = [pscustomobject]@{
        replay_payload_ordinal = 4; corpus_observed_ordinal = 10
        delivery_ordinal = 9; byte_offset = 2; bit_offset = 3
        source_netchan_sequence = 7; source_payload_byte_count = 4
        source_payload_bit_count = 32; next_unconsumed_bit_count = 13
        reassembled = $false; decompressed = $false; byte_aligned = $false
    }
    Assert-FirstObservationGeometry $validBoundary 5 'bit-prefix:17' `
        'policy self-test valid boundary'
    $geometryRejected = 0
    $cursorMismatch = $validBoundary.PSObject.Copy()
    $cursorMismatch.next_unconsumed_bit_count = 12
    try { Assert-FirstObservationGeometry $cursorMismatch 5 'bit-prefix:17' `
            'policy self-test cursor mismatch' }
    catch { $geometryRejected++ }
    try { Assert-FirstObservationGeometry $validBoundary 4 'bit-prefix:17' `
            'policy self-test prefix-width mismatch' }
    catch { $geometryRejected++ }
    $remainingTooSmall = $validBoundary.PSObject.Copy()
    $remainingTooSmall.byte_offset = 3
    $remainingTooSmall.bit_offset = 4
    $remainingTooSmall.next_unconsumed_bit_count = 4
    try { Assert-FirstObservationGeometry $remainingTooSmall 5 'bit-prefix:17' `
            'policy self-test candidate exceeds remaining bits' }
    catch { $geometryRejected++ }
    if ($geometryRejected -ne 3) {
        throw 'Evidence policy did not reject cursor/prefix/remaining-width mismatches.'
    }
    $firstProfile = @{
        'boundary-bit-offset' = '3'; 'boundary-byte-aligned' = 'false'
        'candidate-bit-width' = '5'; 'first-candidate' = 'bit-prefix:17'
        'boundary-payload-ordinal' = '4'; 'boundary-delivery-ordinal' = '9'
    }
    $otherOccurrence = @{
        'boundary-bit-offset' = '3'; 'boundary-byte-aligned' = 'false'
        'candidate-bit-width' = '5'; 'first-candidate' = 'bit-prefix:17'
        'boundary-payload-ordinal' = '91'; 'boundary-delivery-ordinal' = '117'
    }
    if ((Get-FirstCandidateStabilityProfile $firstProfile) -cne
        (Get-FirstCandidateStabilityProfile $otherOccurrence)) {
        throw 'Run-specific transport ordinals contaminated candidate stability.'
    }
    $differentAlignment = $otherOccurrence.Clone()
    $differentAlignment['boundary-bit-offset'] = '4'
    $alignmentRejected = [int](
        (Get-FirstCandidateStabilityProfile $firstProfile) -cne
        (Get-FirstCandidateStabilityProfile $differentAlignment))
    if ($alignmentRejected -ne 1) {
        throw 'Candidate stability did not retain exact bit alignment.'
    }
    $admittedCandidateBinding = Get-AdmittedCandidateBinding $firstProfile
    $rejectedOverflowProfile = $firstProfile.Clone()
    $rejectedOverflowProfile['first-candidate'] = 'bit-prefix:18'
    $rejectedOverflowCandidate = [pscustomobject]@{
        representation = [string]$rejectedOverflowProfile['first-candidate']
        bit_width = [Int64]$rejectedOverflowProfile['candidate-bit-width']
    }
    $acceptedCandidate = [pscustomobject]@{
        representation = 'bit-prefix:17'; bit_width = 5
    }
    $overflowCandidateBindingRejected = [int](
        -not (Test-EvidenceCandidateMatchesBinding `
            $rejectedOverflowCandidate $admittedCandidateBinding))
    if ($overflowCandidateBindingRejected -ne 1 -or
        -not (Test-EvidenceCandidateMatchesBinding `
            $acceptedCandidate $admittedCandidateBinding)) {
        throw 'Rejected overflow candidate replaced the admitted evidence binding.'
    }

    $bindingRun = [pscustomobject]@{
        scenario = 'drop-server-to-client-transport-ordinal'
        map_category = 'boot_camp'
        raw_datagram_count = 10; journal_entry_count = 10
        delivered_sequenced_c2s_count = 3
        delivered_sequenced_s2c_count = 5
        delivered_fragment_datagram_count = 2
        reassembled_payload_count = 1
        decompressed_payload_count = 2
        transport_structural_sha256 = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
        replay_structural_sha256 = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
        duration_ms = 45000; client_ready_status = 'true'
        candidate_stability = 'single_observation'
        last_observed_transport_timestamp_us = 30000000
        last_delivered_sequenced_s2c_timestamp_us = 29900000
    }
    $bindingCapture = [pscustomobject]@{
        scenario = 'drop-server-runtime'; observed_datagrams = 10
        maximum_duration_ms = 45000
        observed_raw_bytes = 500; client_packets = 4; server_packets = 6
        emitted_datagrams = 10
    }
    $bindingVersion = [pscustomobject]@{
        map_category = 'boot_camp'
        client_file_version = '1.1.1.1'
        server_launcher_version = '4.1.1.1'
        server_engine_version = '1.1.2.2'
        protocol = 48
        server_build = 10210
        steam_build_id = 15961492
        client_profile_fingerprint =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
        server_profile_fingerprint =
            'dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd'
    }
    $bindingChecker = @{
        'sequenced-c2s' = '3'; 'sequenced-s2c' = '5'; 'fragments' = '2'
        'duplicate-packets' = '0'; 'old-packets' = '0'
        'delivered-sequenced-c2s' = '3'; 'delivered-sequenced-s2c' = '5'
        'delivered-fragment-datagrams' = '2'
        'reassembled' = '1'; 'decompressed' = '2'
        'candidate-stability' = 'single_observation'
        'structural-hash' = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
        'replay-structural-hash' = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
    }
    $bindingWalker = @{
        'raw-datagrams' = '10'; 'journal-entries' = '10'; 'raw-bytes' = '500'
        'observed-c2s' = '4'; 'observed-s2c' = '6'; 'emitted-datagrams' = '10'
        'delivered-sequenced-c2s' = '3'; 'delivered-sequenced-s2c' = '5'
        'delivered-fragment-datagrams' = '2'
        'last-observed-timestamp-us' = '30000000'
        'last-delivered-sequenced-s2c-timestamp-us' = '29900000'
        'replay-structural-hash' = 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'
    }
    [void](Assert-RunEvidenceBindings $bindingRun $bindingCapture `
        $bindingVersion $bindingChecker $bindingWalker)

    # Duplicate and reordered-old delivery emissions are journal facts even
    # though replay suppresses them before incrementing accepted-new counts.
    $replaySuppressionAccepted = 0
    $duplicateChecker = $bindingChecker.Clone()
    $duplicateChecker['sequenced-s2c'] = '4'
    $duplicateChecker['fragments'] = '1'
    $duplicateChecker['duplicate-packets'] = '1'
    try {
        [void](Assert-RunEvidenceBindings $bindingRun $bindingCapture `
            $bindingVersion $duplicateChecker $bindingWalker)
        $replaySuppressionAccepted++
    } catch {}
    $reorderedOldChecker = $bindingChecker.Clone()
    $reorderedOldChecker['sequenced-s2c'] = '4'
    $reorderedOldChecker['old-packets'] = '1'
    try {
        [void](Assert-RunEvidenceBindings $bindingRun $bindingCapture `
            $bindingVersion $reorderedOldChecker $bindingWalker)
        $replaySuppressionAccepted++
    } catch {}
    if ($replaySuppressionAccepted -ne 2) {
        throw 'Binding policy rejected valid duplicate/reordered-old replay suppression.'
    }

    function Test-BindingMutationRejected {
        param([scriptblock]$Mutation)
        $mutatedRun = $bindingRun.PSObject.Copy()
        $mutatedCapture = $bindingCapture.PSObject.Copy()
        $mutatedVersion = $bindingVersion.PSObject.Copy()
        $mutatedChecker = $bindingChecker.Clone()
        $mutatedWalker = $bindingWalker.Clone()
        try {
            & $Mutation $mutatedRun $mutatedCapture $mutatedVersion `
                $mutatedChecker $mutatedWalker
            [void](Assert-RunEvidenceBindings $mutatedRun $mutatedCapture `
                $mutatedVersion $mutatedChecker $mutatedWalker)
            return 0
        } catch { return 1 }
    }

    $scenarioBindingRejected = 0
    $scenarioBindingRejected += Test-BindingMutationRejected {
        param($run) $run.scenario = 'baseline'
    }
    $scenarioBindingRejected += Test-BindingMutationRejected {
        param($run, $capture) $capture.scenario = 'drop-server'
    }
    if ($scenarioBindingRejected -ne 2) {
        throw 'Binding policy did not reject scenario mismatch/unknown alias.'
    }
    $mapBindingRejected = Test-BindingMutationRejected {
        param($run, $capture, $version) $version.map_category = 'crossfire'
    }
    if ($mapBindingRejected -ne 1) {
        throw 'Binding policy did not reject independently attested map mismatch.'
    }

    $manifestCounterRejected = 0
    foreach ($counterName in @(
            'raw_datagram_count', 'journal_entry_count',
            'delivered_sequenced_c2s_count',
            'delivered_sequenced_s2c_count',
            'delivered_fragment_datagram_count', 'reassembled_payload_count',
            'decompressed_payload_count')) {
        $manifestCounterRejected += Test-BindingMutationRejected {
            param($run) $run.$counterName = [Int64]$run.$counterName + 1
        }
    }
    if ($manifestCounterRejected -ne 7) {
        throw 'Binding policy did not reject every final-manifest counter family.'
    }
    $sourceCounterRejected = 0
    $sourceCounterRejected += Test-BindingMutationRejected {
        param($run, $capture) $capture.observed_raw_bytes = 501
    }
    $sourceCounterRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker, $walker)
        $walker['observed-c2s'] = '5'
    }
    $sourceCounterRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker) $checker['reassembled'] = '2'
    }
    if ($sourceCounterRejected -ne 3) {
        throw 'Binding policy did not reject mutated capture/walker/checker counters.'
    }
    $replayCounterBoundRejected = Test-BindingMutationRejected {
        param($run, $capture, $version, $checker)
        $checker['sequenced-s2c'] = '6'
    }
    if ($replayCounterBoundRejected -ne 1) {
        throw 'Binding policy did not reject replay accepted-new count above delivery count.'
    }
    $replayAccountingRejected = 0
    $replayAccountingRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker)
        $checker['sequenced-s2c'] = '4'
    }
    $replayAccountingRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker)
        $checker['duplicate-packets'] = '1'
    }
    if ($replayAccountingRejected -ne 2) {
        throw 'Binding policy did not reject omitted/extra replay suppression accounting.'
    }

    $hashBindingRejected = 0
    $hashBindingRejected += Test-BindingMutationRejected {
        param($run) $run.transport_structural_sha256 =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
    }
    $hashBindingRejected += Test-BindingMutationRejected {
        param($run) $run.replay_structural_sha256 =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
    }
    $hashBindingRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker)
        $checker['structural-hash'] =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
    }
    $hashBindingRejected += Test-BindingMutationRejected {
        param($run, $capture, $version, $checker, $walker)
        $walker['replay-structural-hash'] =
            'cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc'
    }
    if ($hashBindingRejected -ne 4) {
        throw 'Binding policy did not reject manifest/checker/walker hash mutations.'
    }

    $timestampBindingRejected = 0
    $timestampBindingRejected += Test-BindingMutationRejected {
        param($run) $run.last_observed_transport_timestamp_us = 30000001
    }
    $timestampBindingRejected += Test-BindingMutationRejected {
        param($run) $run.last_delivered_sequenced_s2c_timestamp_us = 29900001
    }
    if ($timestampBindingRejected -ne 2) {
        throw 'Binding policy did not reject both final-manifest timestamps.'
    }
    $candidateStabilityRejected = Test-BindingMutationRejected {
        param($run) $run.candidate_stability = 'stable_observation'
    }
    if ($candidateStabilityRejected -ne 1) {
        throw 'Binding policy did not reject per-run cross-run stability forgery.'
    }
    $agreementChecker = $bindingChecker.Clone()
    $agreementWalker = $bindingWalker.Clone()
    foreach ($key in @(
            'boundary-payload-ordinal', 'boundary-observed-ordinal',
            'boundary-delivery-ordinal', 'boundary-byte-offset',
            'boundary-bit-offset', 'boundary-source-sequence',
            'boundary-source-payload-bytes', 'boundary-source-payload-bits',
            'boundary-next-unconsumed-bits', 'candidate-bit-width',
            'first-candidate')) {
        $agreementChecker[$key] = '1'
        $agreementWalker[$key] = '1'
    }
    foreach ($key in @(
            'boundary-reassembled', 'boundary-decompressed',
            'boundary-byte-aligned')) {
        $agreementChecker[$key] = 'false'
        $agreementWalker[$key] = 'false'
    }
    $agreementWalker['run-id'] = '2' * 32
    $agreementWalker['result'] = 'success'
    $agreementWalker['final-manifest'] = 'accepted'
    $agreementWalker['external-target-profile'] = 'none'
    $agreementWalker['external-target-count'] = '0'
    $agreementWalker['wrong-source-datagrams'] = '0'
    $agreementWalker['transport-complete'] = 'true'
    Assert-IndependentWalkerMatchesChecker $agreementWalker `
        $agreementChecker ('2' * 32) $false 'none' 0

    $walkerInvocationState = @{ Count = 0; Diverge = $false }
    $fakeWalkerInvocation = {
        $walkerInvocationState.Count++
        $suffix = if ($walkerInvocationState.Diverge -and
            $walkerInvocationState.Count -eq 2) { '-mutated' } else { '' }
        return [pscustomobject]@{
            Lines = @("[stock-runtime-walk] result=success$suffix")
            ExitCode = 0
        }
    }
    [void](Invoke-DeterministicWalkerPair $fakeWalkerInvocation `
        'Policy canary walker')
    if ($walkerInvocationState.Count -ne 2) {
        throw 'Canary walker deterministic pair did not invoke exactly twice.'
    }
    $canaryWalkerGateRejections = 0
    $walkerInvocationState.Count = 0
    $walkerInvocationState.Diverge = $true
    try {
        [void](Invoke-DeterministicWalkerPair $fakeWalkerInvocation `
            'Policy canary walker')
    } catch { $canaryWalkerGateRejections++ }
    $nonzeroWalker = [pscustomobject]@{
        Lines = @('[stock-runtime-walk] result=success'); ExitCode = 9 }
    $zeroWalker = [pscustomobject]@{
        Lines = @('[stock-runtime-walk] result=success'); ExitCode = 0 }
    try {
        Assert-DeterministicToolPair $nonzeroWalker $zeroWalker `
            'Policy canary walker'
    } catch { $canaryWalkerGateRejections++ }
    $mutatedAgreement = $agreementWalker.Clone()
    $mutatedAgreement['first-candidate'] = '2'
    try {
        Assert-IndependentWalkerMatchesChecker $mutatedAgreement `
            $agreementChecker ('2' * 32) $false 'none' 0
    } catch { $canaryWalkerGateRejections++ }
    if ($canaryWalkerGateRejections -ne 3) {
        throw 'Canary independent-walker gate mutations failed open.'
    }
    $fakeCanary = [ordered]@{
        schema = $canaryManifestSchema
        implementation_commit = '1' * 40
        run_id = '2' * 32
        map_category = 'boot_camp'
        scenario = 'baseline'
        external_target_profile = 'none'
        external_target_count = 0
        accepted_evidence_run = $true
        delivered_sequenced_s2c_count = 100
        exact_boundary_count = 1
        runtime_candidate_count = 1
        candidate_stability = 'single_observation'
        profile_fingerprint = '3' * 64
        transport_structural_sha256 = '4' * 64
        replay_structural_sha256 = '5' * 64
        checker_output_sha256 = '6' * 64
        accepted_before_campaign = $true
        counted_in_campaign = $false
    }
    $fakeCanary.canary_structural_sha256 = Get-CanaryBindingSha256 $fakeCanary
    $fakeCanary = [pscustomobject]$fakeCanary
    Assert-CanaryManifestContract $fakeCanary $fakeCanary
    $reviewedCanary = $fakeCanary | ConvertTo-Json -Depth 4 | ConvertFrom-Json
    $reviewedCanary.external_target_profile = 'reviewed-non-executable-v1'
    $reviewedCanary.external_target_count = 1
    $reviewedCanary.canary_structural_sha256 =
        Get-CanaryBindingSha256 $reviewedCanary
    Assert-CanaryManifestContract $reviewedCanary $reviewedCanary
    $externalTargetMetadataRejections = 0
    foreach ($mutation in @(
            { param($value) $value.external_target_profile =
                'syntactically-valid-unknown'; $value.external_target_count = 1 },
            { param($value) $value.external_target_profile =
                'reviewed-non-executable-v1'; $value.external_target_count = 0 },
            { param($value) $value.external_target_profile =
                'none'; $value.external_target_count = 1 },
            { param($value) $value.external_target_profile =
                'syntactically-valid-unknown'; $value.external_target_count = 0 })) {
        $mutated = $fakeCanary | ConvertTo-Json -Depth 4 | ConvertFrom-Json
        & $mutation $mutated
        $mutated.canary_structural_sha256 = Get-CanaryBindingSha256 $mutated
        try { Assert-CanaryManifestContract $mutated $mutated }
        catch { $externalTargetMetadataRejections++ }
    }
    if ($externalTargetMetadataRejections -ne 4) {
        throw 'Verifier canary external-target metadata policy failed open.'
    }
    $canaryMutationRejected = 0
    foreach ($mutation in @(
            { param($value) $value.implementation_commit = '7' * 40 },
            { param($value) $value.run_id = '8' * 32 },
            { param($value) $value.accepted_before_campaign = $false },
            { param($value) $value.counted_in_campaign = $true })) {
        $mutated = $fakeCanary | ConvertTo-Json -Depth 4 | ConvertFrom-Json
        & $mutation $mutated
        try { Assert-CanaryManifestContract $mutated $fakeCanary }
        catch { $canaryMutationRejected++ }
    }
    if ($canaryMutationRejected -ne 4) {
        throw 'Verifier pre-campaign canary mutation policy failed open.'
    }
    Write-Output '[stock-runtime-evidence-policy] forbidden-key-rejections=2'
    Write-Output '[stock-runtime-evidence-policy] cursor-width-rejections=3'
    Write-Output '[stock-runtime-evidence-policy] candidate-alignment-rejections=1'
    Write-Output '[stock-runtime-evidence-policy] scenario-binding-rejections=2'
    Write-Output '[stock-runtime-evidence-policy] map-binding-rejections=1'
    Write-Output '[stock-runtime-evidence-policy] manifest-counter-binding-rejections=7'
    Write-Output '[stock-runtime-evidence-policy] source-counter-binding-rejections=3'
    Write-Output '[stock-runtime-evidence-policy] replay-suppression-binding-acceptances=2'
    Write-Output '[stock-runtime-evidence-policy] replay-counter-bound-rejections=1'
    Write-Output '[stock-runtime-evidence-policy] replay-accounting-rejections=2'
    Write-Output '[stock-runtime-evidence-policy] hash-binding-rejections=4'
    Write-Output '[stock-runtime-evidence-policy] timestamp-binding-rejections=2'
    Write-Output '[stock-runtime-evidence-policy] candidate-stability-binding-rejections=1'
    Write-Output '[stock-runtime-evidence-policy] rejected-overflow-candidate-binding-rejections=1'
    Write-Output '[stock-runtime-evidence-policy] external-target-metadata-acceptances=2'
    Write-Output "[stock-runtime-evidence-policy] external-target-metadata-rejections=$externalTargetMetadataRejections"
    Write-Output '[stock-runtime-evidence-policy] canary-mutation-rejections=4'
    Write-Output "[stock-runtime-evidence-policy] fatal-resume-category-rejections=$fatalResumeRejections"
    Write-Output "[stock-runtime-evidence-policy] fatal-resume-state-rejections=$fatalResumeStateRejections"
    Write-Output '[stock-runtime-evidence-policy] canary-walker-invocations=2'
    Write-Output "[stock-runtime-evidence-policy] canary-walker-gate-rejections=$canaryWalkerGateRejections"
    Write-Output '[stock-runtime-evidence-policy] implementation-commit-chain=exact-message-and-ancestor'
    Write-Output '[stock-runtime-evidence-policy] failure-publication-mutations=3'
    Write-Output '[stock-runtime-evidence-policy] campaign-identity-rejections=2'
    Write-Output '[stock-runtime-evidence-policy] files-written=0'
    Write-Output '[stock-runtime-evidence-policy] result=success'
    return
}

$root = [IO.Path]::GetFullPath($CaptureRoot).TrimEnd('\', '/')
if ($root -ine $requiredCaptureRoot -or
    -not (Test-Path -LiteralPath $root -PathType Container)) {
    throw 'CaptureRoot must be the exact existing repository manual-artifacts/stock-runtime root.'
}
Assert-NoReparsePointInExistingPath $root 'capture corpus root'
$checker = [IO.Path]::GetFullPath($CheckerPath)
if (-not (Test-Path -LiteralPath $checker -PathType Leaf) -or
    [IO.Path]::GetFileName($checker) -cne 'hlclient_stock_runtime_check.exe' -or
    -not $checker.StartsWith(
        $repositoryRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'CheckerPath must name the repository-built stock-runtime checker.'
}
Assert-NoReparsePointInExistingPath $checker 'stock runtime checker'
Assert-OnlyDefaultDataStream $checker 'stock runtime checker'
Assert-NoHardLink $checker 'stock runtime checker'
if (-not (Test-Path -LiteralPath $walkerPath -PathType Leaf)) {
    throw 'Independent transport walker is absent.'
}

$gitIgnore = Join-Path $repositoryRoot '.gitignore'
if ((Get-Content -Raw -LiteralPath $gitIgnore) -cnotmatch '(?m)^/manual-artifacts/\s*$') {
    throw 'Repository-wide manual-artifacts ignore rule is absent.'
}
$git = Get-Command git.exe -ErrorAction Stop
$tracked = @(& $git.Source -C $repositoryRoot ls-files -- `
    'manual-artifacts/stock-runtime' 2>$null)
if ($LASTEXITCODE -ne 0 -or $tracked.Count -ne 0) {
    throw 'Raw stock-runtime artifacts are tracked or Git index inspection failed.'
}

$rootEntries = @(Get-ChildItem -LiteralPath $root -Force | Sort-Object Name)
$entries = @($rootEntries | Where-Object { $_.PSIsContainer })
if ($entries.Count -gt $maximumRuns) { throw 'Capture corpus exceeds its run bound.' }
if (@($rootEntries | Where-Object {
            ($_.PSIsContainer -and $_.Name -cnotmatch '^[0-9a-f]{32}$') -or
            (-not $_.PSIsContainer -and $_.Name -cne 'campaign-manifest.json') }).Count -ne 0 -or
    @($rootEntries | Where-Object { $_.Name -ceq 'campaign-manifest.json' }).Count -ne 1) {
    throw 'Capture corpus root contains a non-run entry.'
}
$campaignManifest = Read-BoundedJson `
    (Join-Path $root 'campaign-manifest.json') 131072 'campaign manifest'
$campaignManifestKeys = @(
    'schema', 'implementation_commit', 'profile_fingerprint',
    'external_target_profile', 'external_target_count',
    'required_matrix', 'attempted_slots', 'accepted_slots', 'rejected_slots',
    'incomplete_slots', 'pending_slots', 'packet_totals', 'boundary_totals',
    'candidate_stability', 'threshold_status', 'campaign_structural_sha256')
Assert-ExactProperties $campaignManifest $campaignManifestKeys `
    'campaign manifest'
if ([string]$campaignManifest.schema -cne
        'hlclient.stock-runtime-first-campaign.v1' -or
    [string]$campaignManifest.implementation_commit -cnotmatch
        '^[0-9a-f]{40}$' -or
    [string]$campaignManifest.campaign_structural_sha256 -cnotmatch
        '^[0-9a-f]{64}$' -or
    ([string]$campaignManifest.profile_fingerprint -cne 'evidence_pending' -and
        [string]$campaignManifest.profile_fingerprint -cnotmatch
            '^[0-9a-f]{64}$') -or
    -not (Test-ExternalTargetMetadata `
        (Get-StrictInteger $campaignManifest external_target_count 0 4096) `
        ([string]$campaignManifest.external_target_profile))) {
    throw 'Campaign manifest identity or structural hash is invalid.'
}
$campaignImplementationCommit =
    [string]$campaignManifest.implementation_commit
Assert-ExactImplementationCommit $campaignImplementationCommit `
    'Campaign implementation commit'
Assert-ExactProperties $campaignManifest.packet_totals @(
    'sequenced_c2s', 'sequenced_s2c', 'reassembled', 'decompressed') `
    'campaign packet totals'
Assert-ExactProperties $campaignManifest.boundary_totals @(
    'exact', 'candidates', 'reconnect_generations') `
    'campaign boundary totals'

$accepted = 0
$rejected = 0
$incomplete = 0
$resumeBlockingFailures = 0
[Int64]$rawDatagrams = 0
[Int64]$sequencedC2s = 0
[Int64]$sequencedS2c = 0
[Int64]$fragments = 0
[Int64]$reassembled = 0
[Int64]$decompressed = 0
[Int64]$postResource = 0
[Int64]$exactBoundaries = 0
[Int64]$runtimeCandidates = 0
[Int64]$reconnectGenerations = 0
$scenarioCounts = @{
    'boot_camp|baseline' = 0; 'crossfire|baseline' = 0; 'stalkyard|baseline' = 0
    'crossfire|idle-runtime' = 0; 'boot_camp|reconnect' = 0
    'boot_camp|drop-server-to-client-transport-ordinal' = 0
    'crossfire|duplicate-server-to-client-transport-ordinal' = 0
    'stalkyard|reorder-server-to-client-transport-ordinal' = 0
}
$scenarioTargets = @{
    'boot_camp|baseline' = 6
    'crossfire|baseline' = 4
    'stalkyard|baseline' = 4
    'crossfire|idle-runtime' = 4
    'boot_camp|drop-server-to-client-transport-ordinal' = 2
    'crossfire|duplicate-server-to-client-transport-ordinal' = 1
    'stalkyard|reorder-server-to-client-transport-ordinal' = 1
    'boot_camp|reconnect' = 2
}
$candidateProfile = $null
$admittedCandidateBinding = $null
$boundaryEvidence = $null
$versionProfile = $null
$versionEvidence = $null
$isolationEvidence = $null
$candidateConflict = $false
$versionConflict = $false
$walkerAgreements = 0
$walkerValidatedRunIds = [Collections.Generic.List[string]]::new()
$checkerDeterminism = 0
$transportStructuralHashes = [Collections.Generic.List[string]]::new()
$replayStructuralHashes = [Collections.Generic.List[string]]::new()

$reconnectOutputKeys = @(
    'connection-generation-count', 'exact-boundary-count',
    'runtime-candidate-count', 'generation-distinct', 'candidate-conflict',
    'retired-generation-a-tail-sink',
    'retired-generation-a-server-tail-packets',
    'generation-b-sequenced-after-fresh-accept')
$generationOutputSuffixes = @(
    'first-observed-ordinal', 'last-observed-ordinal',
    'connectionless-exchanges', 'first-sequenced-packet-ordinal',
    'client-to-server-packets', 'server-to-client-packets',
    'boundary-payload-ordinal', 'boundary-observed-ordinal',
    'boundary-delivery-ordinal', 'boundary-byte-offset', 'boundary-bit-offset',
    'boundary-source-sequence', 'boundary-source-payload-bytes',
    'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
    'boundary-reassembled', 'boundary-decompressed', 'boundary-byte-aligned',
    'candidate-bit-width', 'first-candidate', 'candidate-body-consumed',
    'candidate-semantic-category-assigned', 'replay-structural-hash')
foreach ($generationLabel in @('a', 'b')) {
    foreach ($suffix in $generationOutputSuffixes) {
        $reconnectOutputKeys += "generation-$generationLabel-$suffix"
    }
}

$checkerKeys = @(
    'profile', 'transport-valid', 'sequenced-c2s', 'sequenced-s2c',
    'fragments', 'duplicate-packets', 'old-packets',
    'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
    'delivered-fragment-datagrams', 'reassembled', 'decompressed', 'signon-replay',
    'post-resource-boundary', 'boundary-payload-ordinal',
    'boundary-observed-ordinal', 'boundary-delivery-ordinal',
    'boundary-byte-offset', 'boundary-bit-offset',
    'boundary-source-sequence', 'boundary-source-payload-bytes',
    'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
    'boundary-reassembled', 'boundary-decompressed',
    'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
    'candidate-recurrence', 'candidate-stability', 'accepted-run',
    'publication-ready', 'result', 'structural-hash',
    'replay-structural-hash') + $reconnectOutputKeys
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
    'external-target-profile', 'external-target-count',
    'post-resource-boundary', 'boundary-payload-ordinal',
    'boundary-observed-ordinal', 'boundary-delivery-ordinal',
    'boundary-byte-offset', 'boundary-bit-offset',
    'boundary-source-sequence', 'boundary-source-payload-bytes',
    'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
    'boundary-reassembled', 'boundary-decompressed',
    'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
    'candidate-recurrence', 'candidate-stability',
    'replay-structural-hash', 'result') + $reconnectOutputKeys

function Get-VerifiedPreCampaignCanary {
    param([string]$ImplementationCommit)
    if (-not (Test-Path -LiteralPath $requiredCanaryRoot -PathType Container)) {
        throw 'Exact pre-campaign canary root is absent.'
    }
    Assert-NoReparsePointInExistingPath $requiredCanaryRoot `
        'pre-campaign canary root'
    $rootEntries = @(Get-ChildItem -LiteralPath $requiredCanaryRoot -Force |
        Sort-Object Name)
    $runs = @($rootEntries | Where-Object {
            $_.PSIsContainer -and $_.Name -cmatch '^[0-9a-f]{32}$' })
    $manifests = @($rootEntries | Where-Object {
            -not $_.PSIsContainer -and $_.Name -ceq 'canary-manifest.json' })
    if ($rootEntries.Count -ne 2 -or $runs.Count -ne 1 -or
        $manifests.Count -ne 1 -or
        @($rootEntries | Where-Object {
                ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
            }).Count -ne 0) {
        throw 'Pre-campaign canary root is not exactly one run plus one manifest.'
    }
    $runRoot = $runs[0].FullName
    $run = Read-BoundedJson `
        (Join-Path $runRoot 'research-run-metadata.json') 262144 `
        'pre-campaign canary run manifest'
    if ([string]$run.schema -cne 'hlclient.stock-runtime-research-run.v1' -or
        [string]$run.run_id -cne $runs[0].Name -or
        [string]$run.map_category -cne 'boot_camp' -or
        [string]$run.scenario -cne 'baseline' -or
        $run.accepted_evidence_run -isnot [bool] -or
        -not [bool]$run.accepted_evidence_run -or
        $run.accepted_transport_run -isnot [bool] -or
        -not [bool]$run.accepted_transport_run -or
        [string]$run.failure_category -cne 'none') {
        throw 'Pre-campaign canary run is not an accepted boot_camp/baseline observation.'
    }
    $first = Invoke-Checker $checker $runRoot
    $second = Invoke-Checker $checker $runRoot
    Assert-DeterministicToolPair $first $second 'Pre-campaign canary checker'
    $values = Convert-PrefixedOutputToValues $first.Lines `
        '[stock-runtime] ' $checkerKeys 'Pre-campaign canary checker'
    if ($values['accepted-run'] -cne 'true' -or
        $values['publication-ready'] -cne 'true' -or
        $values['result'] -cne 'first-observation' -or
        $values['transport-valid'] -cne 'true' -or
        $values['signon-replay'] -cne 'complete' -or
        $values['post-resource-boundary'] -cne 'observed' -or
        $values['candidate-recurrence'] -cne '1' -or
        $values['candidate-stability'] -cne 'single_observation' -or
        $values.ContainsKey('connection-generation-count') -or
        $values['structural-hash'] -cnotmatch '^[0-9a-f]{64}$' -or
        $values['replay-structural-hash'] -cnotmatch '^[0-9a-f]{64}$') {
        throw 'Pre-campaign canary checker did not prove its exact observation gate.'
    }
    $canaryWalkerInvocation = {
        Invoke-Walker $walkerPath @('-CaptureRoot', $runRoot)
    }.GetNewClosure()
    $walkerResult = Invoke-DeterministicWalkerPair `
        $canaryWalkerInvocation 'Pre-campaign canary independent walker'
    $walkerValues = Convert-PrefixedOutputToValues $walkerResult.Lines `
        '[stock-runtime-walk] ' $walkerKeys `
        'Pre-campaign canary independent walker'
    Assert-IndependentWalkerMatchesChecker $walkerValues $values `
        $runs[0].Name $false ([string]$run.external_target_profile) `
        (Get-StrictInteger $run external_target_count 0 4096)
    [Int64]$deliveredS2c = Get-StrictOutputInteger $values `
        'delivered-sequenced-s2c' 100 131072 'Pre-campaign canary checker'
    $version = Read-BoundedJson (Join-Path $runRoot 'version-observation.json') `
        65536 'pre-campaign canary version observation'
    $profileText = '{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}' -f
        [string]$version.client_file_version,
        [string]$version.server_launcher_version,
        [string]$version.server_engine_version,
        [string]$version.protocol, [string]$version.server_build,
        [string]$version.steam_build_id,
        [string]$version.client_profile_fingerprint,
        [string]$version.server_profile_fingerprint
    $expected = [ordered]@{
        schema = $canaryManifestSchema
        implementation_commit = $ImplementationCommit
        run_id = $runs[0].Name
        map_category = 'boot_camp'
        scenario = 'baseline'
        external_target_profile = [string]$run.external_target_profile
        external_target_count = Get-StrictInteger $run external_target_count 0 4096
        accepted_evidence_run = $true
        delivered_sequenced_s2c_count = $deliveredS2c
        exact_boundary_count = 1
        runtime_candidate_count = 1
        candidate_stability = 'single_observation'
        profile_fingerprint = Get-StringSha256 $profileText
        transport_structural_sha256 = [string]$values['structural-hash']
        replay_structural_sha256 = [string]$values['replay-structural-hash']
        checker_output_sha256 = Get-StringSha256 ($first.Lines -join "`n")
        accepted_before_campaign = $true
        counted_in_campaign = $false
    }
    $expected.canary_structural_sha256 = Get-CanaryBindingSha256 $expected
    $published = Read-BoundedJson $manifests[0].FullName 32768 `
        'pre-campaign canary manifest'
    Assert-CanaryManifestContract $published ([pscustomobject]$expected)
    return [pscustomobject]@{
        RunId = $runs[0].Name
        ProfileFingerprint = [string]$expected.profile_fingerprint
        StructuralHash = [string]$expected.canary_structural_sha256
        ExternalTargetProfile = [string]$expected.external_target_profile
        ExternalTargetCount = [Int64]$expected.external_target_count
    }
}

$canaryBinding = Get-VerifiedPreCampaignCanary $campaignImplementationCommit
if ([string]$campaignManifest.profile_fingerprint -cne 'evidence_pending' -and
    [string]$campaignManifest.profile_fingerprint -cne
        [string]$canaryBinding.ProfileFingerprint) {
    throw 'Campaign stock profile differs from the accepted pre-campaign canary.'
}
if ([string]$campaignManifest.external_target_profile -cne
        [string]$canaryBinding.ExternalTargetProfile -or
    (Get-StrictInteger $campaignManifest external_target_count 0 4096) -ne
        [Int64]$canaryBinding.ExternalTargetCount) {
    throw 'Campaign external-target binding differs from the accepted pre-campaign canary.'
}

foreach ($directory in $entries) {
    try {
        Assert-NoReparsePointInExistingPath $directory.FullName 'capture run'
        $baseEntries = @(
            'capture-metadata.json', 'research-run-metadata.json',
            'version-observation.staged.json',
            'isolation-attestation.staged.json',
            'restoration-attestation.staged.json',
            'transport-journal.jsonl', 'raw', 'logs')
        $acceptedOnlyEntries = @(
            'version-observation.json', 'isolation-attestation.json',
            'restoration-attestation.json')
        $reconnectEntries = @(
            'reconnect-transport-observation.staged.json',
            'reconnect-orchestration.staged.json', 'reconnect-observation.json')
        $allowedEntries = @($baseEntries) + $acceptedOnlyEntries +
            $reconnectEntries
        $children = @(Get-ChildItem -LiteralPath $directory.FullName -Force)
        if (@($children | Where-Object { $allowedEntries -cnotcontains $_.Name }).Count -ne 0 -or
            @($baseEntries | Where-Object {
                    -not (Test-Path -LiteralPath (Join-Path $directory.FullName $_)) }).Count -ne 0) {
            throw 'Run directory structure is incomplete or contains an unknown entry.'
        }
        $capture = Read-BoundedJson (Join-Path $directory.FullName 'capture-metadata.json') `
            1048576 'capture metadata'
        Assert-ExactProperties $capture @(
            'schema', 'profile', 'scenario', 'runtime_result', 'runtime_ready',
            'stock_versions', 'maximum_duration_ms', 'maximum_datagrams',
            'maximum_total_raw_bytes', 'maximum_payload_bytes',
            'maximum_reassembled_bytes', 'maximum_decompressed_bytes',
            'maximum_message_count', 'maximum_runtime_frames',
            'maximum_client_packets', 'maximum_server_packets',
            'observed_datagrams', 'observed_raw_bytes', 'client_packets',
            'server_packets', 'emitted_datagrams', 'emitted_bytes',
            'dropped_datagrams', 'duplicated_datagrams', 'delayed_datagrams',
            'ignored_wrong_source_datagrams', 'perturbation_count',
            'bounded_transport_complete', 'byte_preserving',
            'private_ipv4_loopback', 'one_learned_client_endpoint',
            'one_upstream_socket', 'exact_source_validation',
            'payload_rewritten', 'raw_datagrams_stored',
            'accepted_evidence_run') 'capture metadata'
        if ([string]$capture.schema -cne 'hlclient.stock-runtime-capture-metadata.v1') {
            throw 'Capture metadata v1 compatibility is absent.'
        }
        $run = Read-BoundedJson (Join-Path $directory.FullName 'research-run-metadata.json') `
            131072 'research run manifest'
        $canonicalScenario = Get-CanonicalRuntimeScenario `
            ([string]$run.scenario) 'Final manifest scenario'
        $runKeys = @(
            'schema', 'run_id', 'scenario', 'map_category', 'duration_ms',
            'isolation_status', 'process_ownership_status',
            'version_profile_status', 'relay_status', 'client_ready_status',
            'restoration_status', 'external_drift_status',
            'external_target_profile', 'external_target_count',
            'raw_datagram_count', 'journal_entry_count',
            'delivered_sequenced_c2s_count',
            'delivered_sequenced_s2c_count',
            'delivered_fragment_datagram_count', 'reassembled_payload_count',
            'decompressed_payload_count', 'offline_replay_status',
            'post_resource_boundary_status',
            'post_resource_replay_payload_ordinal',
            'post_resource_corpus_observed_ordinal',
            'post_resource_delivery_ordinal', 'post_resource_byte_offset',
            'post_resource_bit_offset', 'post_resource_source_sequence',
            'post_resource_source_payload_bytes',
            'post_resource_source_payload_bits',
            'post_resource_next_unconsumed_bits',
            'post_resource_reassembled', 'post_resource_decompressed',
            'post_resource_boundary_byte_aligned',
            'first_observation_status', 'first_candidate',
            'first_candidate_bit_width', 'first_candidate_recurrence',
            'transport_structural_sha256', 'replay_structural_sha256',
            'last_delivered_sequenced_s2c_timestamp_us',
            'last_observed_transport_timestamp_us', 'candidate_stability',
            'accepted_transport_run', 'accepted_evidence_run',
            'failure_category')
        $acceptedReconnect = $canonicalScenario -ceq 'reconnect' -and
            $run.accepted_evidence_run -is [bool] -and
            [bool]$run.accepted_evidence_run
        if ($acceptedReconnect) {
            $runKeys += @(
                'connection_generation_count', 'exact_boundary_count',
                'runtime_candidate_count', 'generation_distinct',
                'candidate_conflict')
        }
        Assert-ExactProperties $run $runKeys 'research run manifest'
        if ([string]$run.schema -cne
                'hlclient.stock-runtime-research-run.v1' -or
            [string]$run.run_id -cne $directory.Name -or
            $run.accepted_evidence_run -isnot [bool] -or
            $run.accepted_transport_run -isnot [bool]) {
            throw 'Research run manifest is invalid.'
        }
        $externalTargetCount = Get-StrictInteger $run external_target_count 0 4096
        if (-not (Test-ExternalTargetMetadata $externalTargetCount `
                ([string]$run.external_target_profile)) -or
            [string]$run.external_target_profile -cne
                [string]$canaryBinding.ExternalTargetProfile -or
            $externalTargetCount -ne
                [Int64]$canaryBinding.ExternalTargetCount) {
            throw 'Research run manifest external-target metadata is invalid.'
        }
        $acceptedPublication = [bool]$run.accepted_evidence_run
        $presentAcceptedOnlyEntries = @($acceptedOnlyEntries | Where-Object {
                Test-Path -LiteralPath (Join-Path $directory.FullName $_)
            })
        if (($acceptedPublication -and
                $presentAcceptedOnlyEntries.Count -ne
                    $acceptedOnlyEntries.Count) -or
            (-not $acceptedPublication -and
                $presentAcceptedOnlyEntries.Count -ne 0)) {
            throw 'Final evidence leaves disagree with publication state.'
        }
        $presentReconnectEntries = @($reconnectEntries | Where-Object {
                Test-Path -LiteralPath (Join-Path $directory.FullName $_)
            })
        if ($canonicalScenario -cne 'reconnect' -and
            $presentReconnectEntries.Count -ne 0) {
            throw 'Non-reconnect run contains a reconnect-only leaf.'
        }
        if ($acceptedReconnect -and $presentReconnectEntries.Count -ne 3) {
            throw 'Accepted reconnect run lacks its staged/final reconnect observations.'
        }
        if ($canonicalScenario -ceq 'reconnect' -and -not $acceptedReconnect -and
            ($presentReconnectEntries.Count -eq 1 -or
                $presentReconnectEntries.Count -eq 3 -or
                $presentReconnectEntries -ccontains 'reconnect-observation.json')) {
            throw 'Incomplete reconnect leaves are not an atomic staged pair.'
        }
        if (-not $acceptedPublication) {
            if ([bool]$run.accepted_transport_run) {
                throw 'Non-accepted evidence run claims accepted transport.'
            }
            if ([string]::IsNullOrWhiteSpace([string]$run.failure_category) -or
                [string]$run.failure_category -ceq 'none') {
                throw 'Non-accepted run lacks a typed failure category.'
            }
            if ((Get-CampaignFailurePublication ([string]$run.failure_category)) -ceq
                    'incomplete') {
                $incomplete++
            } else {
                $rejected++
                $resumeBlockingFailures++
            }
            continue
        }
        $version = Read-BoundedJson (Join-Path $directory.FullName 'version-observation.json') `
            65536 'version observation'
        $stagedVersion = Read-BoundedJson `
            (Join-Path $directory.FullName 'version-observation.staged.json') `
            65536 'staged version observation'
        $versionKeys = @(
            'schema', 'map_category', 'client_file_version',
            'client_pe_machine', 'client_signature',
            'client_profile_fingerprint', 'server_launcher_version',
            'server_pe_machine', 'server_signature',
            'server_profile_fingerprint', 'steam_app_id', 'steam_build_id',
            'server_engine_version', 'protocol', 'server_build',
            'evidence_status')
        Assert-ExactProperties $version $versionKeys 'version observation'
        Assert-ExactProperties $stagedVersion $versionKeys `
            'staged version observation'
        $isolation = Read-BoundedJson (Join-Path $directory.FullName 'isolation-attestation.json') `
            65536 'isolation attestation'
        $stagedIsolation = Read-BoundedJson `
            (Join-Path $directory.FullName 'isolation-attestation.staged.json') `
            65536 'staged isolation attestation'
        $isolationKeys = @(
            'schema', 'session_type', 'persistent_rule_count',
            'ipv4_loopback', 'ipv6_loopback', 'non_loopback_canary',
            'cleanup_status', 'evidence_status')
        Assert-ExactProperties $isolation $isolationKeys 'isolation attestation'
        Assert-ExactProperties $stagedIsolation $isolationKeys `
            'staged isolation attestation'
        $restoration = Read-BoundedJson (Join-Path $directory.FullName 'restoration-attestation.json') `
            131072 'restoration attestation'
        $stagedRestoration = Read-BoundedJson `
            (Join-Path $directory.FullName 'restoration-attestation.staged.json') `
            131072 'staged restoration attestation'
        $restorationKeys = @(
            'schema', 'external_file_drift', 'snapshot_entry_count',
            'pre_manifest_sha256', 'post_manifest_sha256',
            'external_snapshot_entry_count', 'external_pre_manifest_sha256',
            'external_post_manifest_sha256', 'created_files_removed',
            'protected_paths_included', 'owned_processes_stopped',
            'input_automation_used', 'input_events_injected',
            'orchestrator_exit_code', 'restoration_status')
        Assert-ExactProperties $restoration $restorationKeys `
            'restoration attestation'
        Assert-ExactProperties $stagedRestoration $restorationKeys `
            'staged restoration attestation'
        Assert-ExactFilePair `
            (Join-Path $directory.FullName 'version-observation.staged.json') `
            (Join-Path $directory.FullName 'version-observation.json') `
            'version observation'
        Assert-ExactFilePair `
            (Join-Path $directory.FullName 'isolation-attestation.staged.json') `
            (Join-Path $directory.FullName 'isolation-attestation.json') `
            'isolation attestation'
        Assert-ExactFilePair `
            (Join-Path $directory.FullName 'restoration-attestation.staged.json') `
            (Join-Path $directory.FullName 'restoration-attestation.json') `
            'restoration attestation'
        if ($acceptedReconnect -and
            ((Get-StrictInteger $run connection_generation_count 2 2) -ne 2 -or
                (Get-StrictInteger $run exact_boundary_count 2 2) -ne 2 -or
                (Get-StrictInteger $run runtime_candidate_count 2 2) -ne 2 -or
                $run.generation_distinct -isnot [bool] -or
                -not [bool]$run.generation_distinct -or
                $run.candidate_conflict -isnot [bool] -or
                [bool]$run.candidate_conflict)) {
            throw 'Accepted reconnect run has invalid generation/boundary claims.'
        }
        if (-not $run.accepted_transport_run -or
            [string]$run.restoration_status -cne 'exact' -or
            [string]$run.external_drift_status -cne 'none' -or
            [string]$run.offline_replay_status -cne 'success' -or
            [string]$run.post_resource_boundary_status -cne 'observed' -or
            [string]$run.first_observation_status -cne 'observed') {
            throw 'Accepted run does not satisfy all final gates.'
        }
        if ([string]$version.schema -cne 'hlclient.stock-runtime-version-observation.v1' -or
            $stockRuntimeMapCategories -cnotcontains [string]$version.map_category -or
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
            throw 'Version/profile observation is not accepted.'
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
        $currentIsolationEvidence = [ordered]@{
            session_type = [string]$isolation.session_type
            persistent_rule_count = [Int64]$isolation.persistent_rule_count
            ipv4_loopback = [string]$isolation.ipv4_loopback
            ipv6_loopback = [string]$isolation.ipv6_loopback
            non_loopback_canary = [string]$isolation.non_loopback_canary
            cleanup_status = [string]$isolation.cleanup_status
        }
        if ($null -eq $isolationEvidence) {
            $isolationEvidence = $currentIsolationEvidence
        } elseif (($isolationEvidence | ConvertTo-Json -Compress) -cne
            ($currentIsolationEvidence | ConvertTo-Json -Compress)) {
            throw 'Isolation profile conflicts cross-run.'
        }
        if ([string]$restoration.schema -cne 'hlclient.stock-runtime-restoration.v1' -or
            [string]$restoration.restoration_status -cne 'exact' -or
            [string]$restoration.external_file_drift -cne 'none' -or
            [string]$restoration.pre_manifest_sha256 -cne
                [string]$restoration.post_manifest_sha256 -or
            $restoration.created_files_removed -cne $true -or
            $restoration.owned_processes_stopped -cne $true -or
            $restoration.input_automation_used -cne $false) {
            throw 'Restoration attestation is not accepted.'
        }

        $first = Invoke-Checker $checker $directory.FullName
        $second = Invoke-Checker $checker $directory.FullName
        Assert-DeterministicToolPair $first $second 'Production checker'
        $checkerValues = Convert-PrefixedOutputToValues $first.Lines `
            '[stock-runtime] ' $checkerKeys 'production checker'
        $expectedCandidateRecurrence = if ($canonicalScenario -ceq 'reconnect') {
            '2'
        } else { '1' }
        $expectedCandidateStability = if ($canonicalScenario -ceq 'reconnect') {
            'stable_observation'
        } else { 'single_observation' }
        if ($checkerValues['profile'] -cne
                'stock_protocol_48_build_10210_evidence_pending' -or
            $checkerValues['accepted-run'] -cne 'true' -or
            $checkerValues['result'] -cne 'first-observation' -or
            $checkerValues['transport-valid'] -cne 'true' -or
            $checkerValues['signon-replay'] -cne 'complete' -or
            $checkerValues['post-resource-boundary'] -cne 'observed' -or
            $checkerValues['boundary-reassembled'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['boundary-decompressed'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['boundary-byte-aligned'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['first-candidate'] -cnotmatch
                '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
            [int]($checkerValues['first-candidate'] -replace '^bit-prefix:', '') -gt 255 -or
            $checkerValues['candidate-recurrence'] -cne $expectedCandidateRecurrence -or
            $checkerValues['candidate-stability'] -cne $expectedCandidateStability -or
            $checkerValues['publication-ready'] -cne 'true' -or
            $checkerValues['structural-hash'] -cnotmatch '^[0-9a-f]{64}$' -or
            $checkerValues['replay-structural-hash'] -cnotmatch '^[0-9a-f]{64}$') {
            throw 'Production checker did not accept the run.'
        }
        foreach ($countKey in @('boundary-payload-ordinal',
                'boundary-observed-ordinal', 'boundary-delivery-ordinal',
                'boundary-byte-offset', 'boundary-source-sequence',
                'boundary-source-payload-bytes', 'boundary-source-payload-bits',
                'boundary-next-unconsumed-bits', 'candidate-bit-width')) {
            [Int64]$value = 0
            if (-not [Int64]::TryParse($checkerValues[$countKey], [ref]$value) -or
                $value -lt 0 -or $value -gt 8388608) {
                throw "Production checker $countKey is outside its bound."
            }
        }
        [Int64]$boundaryBitOffset = 0
        if (-not [Int64]::TryParse(
                $checkerValues['boundary-bit-offset'], [ref]$boundaryBitOffset) -or
            $boundaryBitOffset -lt 0 -or $boundaryBitOffset -gt 7) {
            throw 'Production checker boundary bit offset is invalid.'
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
            throw 'Production checker cursor/candidate geometry is inconsistent.'
        }
        if ($canonicalScenario -ceq 'reconnect') {
            foreach ($requiredKey in $reconnectOutputKeys) {
                if (-not $checkerValues.ContainsKey($requiredKey)) {
                    throw "Production checker lacks reconnect key '$requiredKey'."
                }
            }
            if ($checkerValues['connection-generation-count'] -cne '2' -or
                $checkerValues['exact-boundary-count'] -cne '2' -or
                $checkerValues['runtime-candidate-count'] -cne '2' -or
                $checkerValues['generation-distinct'] -cne 'true' -or
                $checkerValues['candidate-conflict'] -cne 'false' -or
                $checkerValues['retired-generation-a-tail-sink'] -cne 'routing_only' -or
                $checkerValues['generation-b-sequenced-after-fresh-accept'] -cne 'true') {
                throw 'Production checker reconnect aggregate is invalid.'
            }
            [void](Get-StrictOutputInteger $checkerValues `
                'retired-generation-a-server-tail-packets' 0 65536 `
                'Production checker reconnect replay')
            foreach ($generationLabel in @('a', 'b')) {
                $generationPrefix = "generation-$generationLabel-"
                foreach ($integerSuffix in @(
                        'first-observed-ordinal', 'last-observed-ordinal',
                        'connectionless-exchanges', 'first-sequenced-packet-ordinal',
                        'client-to-server-packets', 'server-to-client-packets',
                        'boundary-payload-ordinal', 'boundary-observed-ordinal',
                        'boundary-delivery-ordinal', 'boundary-byte-offset',
                        'boundary-source-sequence', 'boundary-source-payload-bytes',
                        'boundary-source-payload-bits',
                        'boundary-next-unconsumed-bits', 'candidate-bit-width')) {
                    [void](Get-StrictOutputInteger $checkerValues `
                        ($generationPrefix + $integerSuffix) 0 8388608 `
                        "Production checker reconnect generation $generationLabel")
                }
                [Int64]$generationBitOffset = Get-StrictOutputInteger $checkerValues `
                    ($generationPrefix + 'boundary-bit-offset') 0 7 `
                    "Production checker reconnect generation $generationLabel"
                $generationCandidate = [string]$checkerValues[
                    $generationPrefix + 'first-candidate']
                $generationWidth = Get-StrictOutputInteger $checkerValues `
                    ($generationPrefix + 'candidate-bit-width') 1 8 `
                    "Production checker reconnect generation $generationLabel"
                if ($checkerValues[$generationPrefix + 'boundary-reassembled'] -cnotmatch
                        '^(?:true|false)$' -or
                    $checkerValues[$generationPrefix + 'boundary-decompressed'] -cnotmatch
                        '^(?:true|false)$' -or
                    $checkerValues[$generationPrefix + 'boundary-byte-aligned'] -cnotmatch
                        '^(?:true|false)$' -or
                    $checkerValues[$generationPrefix + 'candidate-body-consumed'] -cne 'false' -or
                    $checkerValues[$generationPrefix + 'candidate-semantic-category-assigned'] -cne 'false' -or
                    $generationCandidate -cnotmatch
                        '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
                    [int]($generationCandidate -replace '^bit-prefix:', '') -gt 255 -or
                    ($checkerValues[$generationPrefix + 'boundary-byte-aligned'] -ceq 'true') -ne
                        ($generationBitOffset -eq 0) -or
                    $checkerValues[$generationPrefix + 'replay-structural-hash'] -cnotmatch
                        '^[0-9a-f]{64}$' -or $generationWidth -gt 8) {
                    throw "Production checker reconnect generation $generationLabel is invalid."
                }
            }
            if ((Get-StrictOutputInteger $checkerValues `
                    'generation-a-last-observed-ordinal' 0 65535 `
                    'Production checker reconnect generation A') -ge
                (Get-StrictOutputInteger $checkerValues `
                    'generation-b-first-observed-ordinal' 0 65535 `
                    'Production checker reconnect generation B') -or
                $checkerValues['generation-a-boundary-bit-offset'] -cne
                    $checkerValues['generation-b-boundary-bit-offset'] -or
                $checkerValues['generation-a-boundary-byte-aligned'] -cne
                    $checkerValues['generation-b-boundary-byte-aligned'] -or
                $checkerValues['generation-a-candidate-bit-width'] -cne
                    $checkerValues['generation-b-candidate-bit-width'] -or
                $checkerValues['generation-a-first-candidate'] -cne
                    $checkerValues['generation-b-first-candidate']) {
                throw 'Production checker reconnect generations are not distinct/stable.'
            }
        }
        $checkerDeterminism++

        $walkerArguments = @('-CaptureRoot', $directory.FullName)
        if ($canonicalScenario -ceq 'reconnect') {
            $walkerArguments += @(
                '-GenerationBBoundaryPayloadOrdinal',
                $checkerValues['generation-b-boundary-payload-ordinal'],
                '-GenerationBBoundaryObservedOrdinal',
                $checkerValues['generation-b-boundary-observed-ordinal'],
                '-GenerationBBoundaryDeliveryOrdinal',
                $checkerValues['generation-b-boundary-delivery-ordinal'],
                '-GenerationBBoundaryByteOffset',
                $checkerValues['generation-b-boundary-byte-offset'],
                '-GenerationBBoundaryBitOffset',
                $checkerValues['generation-b-boundary-bit-offset'],
                '-GenerationBBoundarySourceSequence',
                $checkerValues['generation-b-boundary-source-sequence'],
                '-GenerationBBoundarySourcePayloadBytes',
                $checkerValues['generation-b-boundary-source-payload-bytes'],
                '-GenerationBBoundarySourcePayloadBits',
                $checkerValues['generation-b-boundary-source-payload-bits'],
                '-GenerationBBoundaryNextUnconsumedBits',
                $checkerValues['generation-b-boundary-next-unconsumed-bits'],
                '-GenerationBBoundaryReassembled',
                $checkerValues['generation-b-boundary-reassembled'],
                '-GenerationBBoundaryDecompressed',
                $checkerValues['generation-b-boundary-decompressed'],
                '-GenerationBCandidateBitWidth',
                $checkerValues['generation-b-candidate-bit-width'],
                '-GenerationBFirstCandidate',
                $checkerValues['generation-b-first-candidate'])
        }
        $walkerInvocation = {
            Invoke-Walker $walkerPath $walkerArguments
        }.GetNewClosure()
        $walkerResult = Invoke-DeterministicWalkerPair $walkerInvocation `
            'Independent walker'
        $walkerValues = Convert-PrefixedOutputToValues $walkerResult.Lines `
            '[stock-runtime-walk] ' $walkerKeys 'independent walker'
        Assert-IndependentWalkerMatchesChecker $walkerValues $checkerValues `
            $directory.Name ($canonicalScenario -ceq 'reconnect') `
            ([string]$canaryBinding.ExternalTargetProfile) `
            ([Int64]$canaryBinding.ExternalTargetCount)
        $binding = Assert-RunEvidenceBindings $run $capture $version `
            $checkerValues $walkerValues
        $walkerAgreements++

        $candidate = Get-FirstCandidateStabilityProfile $checkerValues
        $profile = '{0}|{1}|{2}|{3}|{4}|{5}' -f
            $binding.ClientFileVersion, $binding.ServerLauncherVersion,
            $binding.ServerEngineVersion, $binding.Protocol,
            $binding.ServerBuild, ($binding.SteamBuildId.ToString() + '|' +
                $binding.ClientProfileFingerprint + '|' +
                $binding.ServerProfileFingerprint)

        $canonicalScenario = [string]$binding.CanonicalScenario
        $key = '{0}|{1}' -f [string]$binding.MapCategory, $canonicalScenario
        if (-not $scenarioCounts.ContainsKey($key) -or
            $scenarioCounts[$key] -ge [int]$scenarioTargets[$key]) {
            throw 'Accepted run is outside or exceeds its campaign matrix slot.'
        }
        $runSequencedC2s = [Int64]$binding.SequencedC2s
        $runSequencedS2c = [Int64]$binding.SequencedS2c
        $durationMs = [Int64]$binding.DurationMs
        if (($canonicalScenario -ceq 'baseline' -or
                $canonicalScenario -ceq 'idle-runtime') -and
            $durationMs -lt 30000) {
            throw 'Accepted baseline/idle run is shorter than 30 seconds.'
        }
        if (($canonicalScenario -ceq 'baseline' -or
                $canonicalScenario -ceq 'idle-runtime') -and
            [Int64]$binding.LastObservedTimestampUs -lt 30000000) {
            throw 'Accepted baseline/idle run is shorter than 30 seconds on the capture transport clock.'
        }
        if ($runSequencedS2c -lt 100) {
            throw 'Accepted run is below its 100-packet generation-attributed S2C gate.'
        }
        if ($canonicalScenario -ceq 'idle-runtime') {
            $lastLiveS2cUs = [Int64]$binding.LastDeliveredSequencedS2cTimestampUs
            [Int64]$minimumLiveThroughUs = [Math]::Max(
                25000000, ($durationMs - 5000) * 1000)
            if ($lastLiveS2cUs -lt 30000000 -or
                $lastLiveS2cUs -lt $minimumLiveThroughUs -or
                -not [bool]$binding.ClientReady) {
                throw 'Accepted idle run did not remain client-ready with S2C traffic through its final five seconds.'
            }
        }
        if ($null -eq $versionProfile) {
            $versionProfile = $profile
        } elseif ($versionProfile -cne $profile) {
            throw 'Version profile conflicts cross-run.'
        }
        if ($null -eq $candidateProfile) {
            $newCandidateBinding = Get-AdmittedCandidateBinding $checkerValues
            $candidateProfile = $candidate
            $admittedCandidateBinding = $newCandidateBinding
        } elseif ($candidateProfile -cne $candidate) {
            $candidateConflict = $true
        }
        if ($null -eq $boundaryEvidence) {
            $boundaryEvidence = [ordered]@{
                replay_payload_ordinal = [Int64]$checkerValues['boundary-payload-ordinal']
                corpus_observed_ordinal = [Int64]$checkerValues['boundary-observed-ordinal']
                delivery_ordinal = [Int64]$checkerValues['boundary-delivery-ordinal']
                byte_offset = [Int64]$checkerValues['boundary-byte-offset']
                bit_offset = [Int64]$checkerValues['boundary-bit-offset']
                source_netchan_sequence = [Int64]$checkerValues['boundary-source-sequence']
                source_payload_byte_count = [Int64]$checkerValues['boundary-source-payload-bytes']
                source_payload_bit_count = [Int64]$checkerValues['boundary-source-payload-bits']
                next_unconsumed_bit_count = [Int64]$checkerValues['boundary-next-unconsumed-bits']
                reassembled = $checkerValues['boundary-reassembled'] -ceq 'true'
                decompressed = $checkerValues['boundary-decompressed'] -ceq 'true'
                byte_aligned = $checkerValues['boundary-byte-aligned'] -ceq 'true'
            }
        }
        if ($null -eq $versionEvidence) {
            $versionEvidence = [ordered]@{
                client_file_version = [string]$binding.ClientFileVersion
                server_launcher_version = [string]$binding.ServerLauncherVersion
                server_engine_version = [string]$binding.ServerEngineVersion
                protocol = [Int64]$binding.Protocol
                server_build = [Int64]$binding.ServerBuild
                app_build = [Int64]$binding.SteamBuildId
            }
        }
        $scenarioCounts[$key]++
        $accepted++
        [void]$walkerValidatedRunIds.Add($directory.Name)
        $rawDatagrams += [Int64]$binding.RawDatagrams
        $sequencedC2s += $runSequencedC2s
        $sequencedS2c += $runSequencedS2c
        $fragments += [Int64]$binding.FragmentDatagrams
        $reassembled += [Int64]$binding.ReassembledPayloads
        $decompressed += [Int64]$binding.DecompressedPayloads
        $runObservationCount = if ($canonicalScenario -ceq 'reconnect') { 2 } else { 1 }
        $postResource += $runObservationCount
        $exactBoundaries += $runObservationCount
        $runtimeCandidates += $runObservationCount
        if ($canonicalScenario -ceq 'reconnect') { $reconnectGenerations += 2 }
        [void]$transportStructuralHashes.Add(
            [string]$binding.TransportStructuralHash)
        [void]$replayStructuralHashes.Add([string]$binding.ReplayStructuralHash)
    } catch {
        $rejected++
        $resumeBlockingFailures++
    }
}

Assert-NoCampaignResumeBlockingFailures $resumeBlockingFailures

$campaignSummaryKeys = @(
    'profile', 'external-target-profile', 'external-target-count',
    'accepted', 'rejected', 'incomplete', 'pending',
    'sequenced-c2s', 'sequenced-s2c', 'reassembled', 'decompressed',
    'boundaries', 'candidates', 'reconnect-generations',
    'candidate-stability', 'threshold', 'implementation-commit',
    'structural-hash', 'result')
$firstCampaignSummary = Invoke-CampaignChecker $checker $root `
    $walkerValidatedRunIds.ToArray()
$secondCampaignSummary = Invoke-CampaignChecker $checker $root `
    $walkerValidatedRunIds.ToArray()
if ($firstCampaignSummary.ExitCode -ne 0 -or
    $secondCampaignSummary.ExitCode -ne 0 -or
    ($firstCampaignSummary.Lines -join "`n") -cne
        ($secondCampaignSummary.Lines -join "`n")) {
    throw 'Campaign checker summary is unsuccessful or non-deterministic.'
}
$campaignSummary = Convert-PrefixedOutputToValues `
    $firstCampaignSummary.Lines '[stock-runtime] ' $campaignSummaryKeys `
    'campaign checker summary'
Assert-CampaignManifestSummaryIdentity $campaignManifest $campaignSummary
if ($campaignSummary['profile'] -cne
        'stock_protocol_48_build_10210_evidence_pending' -or
    $campaignSummary['external-target-profile'] -cne
        [string]$canaryBinding.ExternalTargetProfile -or
    $campaignSummary['external-target-count'] -cne
        [string]$canaryBinding.ExternalTargetCount -or
    $campaignSummary['result'] -cne 'campaign-summary' -or
    $campaignSummary['accepted'] -cne [string]$accepted -or
    $campaignSummary['rejected'] -cne [string]$rejected -or
    $campaignSummary['incomplete'] -cne [string]$incomplete -or
    $campaignSummary['pending'] -cne [string](24 - $accepted) -or
    $campaignSummary['sequenced-c2s'] -cne [string]$sequencedC2s -or
    $campaignSummary['sequenced-s2c'] -cne [string]$sequencedS2c -or
    $campaignSummary['reassembled'] -cne [string]$reassembled -or
    $campaignSummary['decompressed'] -cne [string]$decompressed -or
    $campaignSummary['boundaries'] -cne [string]$exactBoundaries -or
    $campaignSummary['candidates'] -cne [string]$runtimeCandidates -or
    $campaignSummary['reconnect-generations'] -cne [string]$reconnectGenerations -or
    $campaignSummary['implementation-commit'] -cne
        $campaignImplementationCommit -or
    $campaignSummary['structural-hash'] -cne
        [string]$campaignManifest.campaign_structural_sha256) {
    throw 'Campaign checker summary disagrees with independently aggregated facts.'
}

$baselineAccepted = $scenarioCounts['boot_camp|baseline'] +
    $scenarioCounts['crossfire|baseline'] + $scenarioCounts['stalkyard|baseline']
$idleAccepted = $scenarioCounts['crossfire|idle-runtime']
$reconnectAccepted = $scenarioCounts['boot_camp|reconnect']
$perturbationAccepted =
    $scenarioCounts['boot_camp|drop-server-to-client-transport-ordinal'] +
    $scenarioCounts['crossfire|duplicate-server-to-client-transport-ordinal'] +
    $scenarioCounts['stalkyard|reorder-server-to-client-transport-ordinal']
$matrixReady = $scenarioCounts['boot_camp|baseline'] -ge 6 -and
    $scenarioCounts['crossfire|baseline'] -ge 4 -and
    $scenarioCounts['stalkyard|baseline'] -ge 4 -and $idleAccepted -ge 4 -and
    $reconnectAccepted -ge 2 -and
    $scenarioCounts['boot_camp|drop-server-to-client-transport-ordinal'] -ge 2 -and
    $scenarioCounts['crossfire|duplicate-server-to-client-transport-ordinal'] -ge 1 -and
    $scenarioCounts['stalkyard|reorder-server-to-client-transport-ordinal'] -ge 1
$thresholdReady = $accepted -ge $MinimumAcceptedRuns -and $matrixReady -and
    $sequencedS2c -ge $MinimumSequencedServerPackets -and
    $postResource -ge 26 -and $exactBoundaries -ge 26 -and
    $runtimeCandidates -ge 26 -and $reconnectGenerations -ge 4 -and
    $null -ne $candidateProfile -and
    -not $candidateConflict -and -not $versionConflict
$campaignThresholdReady = $accepted -eq 24 -and $matrixReady -and
    $sequencedS2c -ge 1000 -and $postResource -ge 26 -and
    $exactBoundaries -ge 26 -and $runtimeCandidates -ge 26 -and
    $reconnectGenerations -ge 4 -and $null -ne $candidateProfile -and
    -not $candidateConflict -and -not $versionConflict
$expectedCampaignStability = if ($candidateConflict) {
    'candidate_conflicting'
} elseif ($runtimeCandidates -ge 2) { 'stable_observation' } else { 'evidence_pending' }
$expectedCampaignThreshold = if ($candidateConflict) {
    'conflicting'
} elseif ($campaignThresholdReady) { 'passed' } else { 'pending' }
if ($campaignSummary['threshold'] -cne $expectedCampaignThreshold -or
    $campaignSummary['candidate-stability'] -cne $expectedCampaignStability) {
    throw 'Campaign checker threshold/stability disagrees with the independent verifier.'
}

$campaignTargets = @{
    'boot_camp|baseline' = 6
    'crossfire|baseline' = 4
    'stalkyard|baseline' = 4
    'crossfire|idle-runtime' = 4
    'boot_camp|drop-server-to-client-transport-ordinal' = 2
    'crossfire|duplicate-server-to-client-transport-ordinal' = 1
    'stalkyard|reorder-server-to-client-transport-ordinal' = 1
    'boot_camp|reconnect' = 2
}
$manifestMatrix = @($campaignManifest.required_matrix)
if ($manifestMatrix.Count -ne $campaignTargets.Count) {
    throw 'Campaign manifest matrix has the wrong size.'
}
$manifestMatrixKeys = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::Ordinal)
foreach ($entry in $manifestMatrix) {
    Assert-ExactProperties $entry @(
        'map_category', 'scenario', 'required_runs', 'accepted_runs') `
        'campaign matrix entry'
    $key = '{0}|{1}' -f [string]$entry.map_category,
        [string]$entry.scenario
    if (-not $campaignTargets.ContainsKey($key) -or
        -not $manifestMatrixKeys.Add($key) -or
        (Get-StrictInteger $entry required_runs 0 24) -ne
            [Int64]$campaignTargets[$key] -or
        (Get-StrictInteger $entry accepted_runs 0 24) -ne
            [Int64]$scenarioCounts[$key]) {
        throw 'Campaign manifest matrix disagrees with verified slots.'
    }
}
$expectedProfileFingerprint = if ($accepted -eq 0) {
    'evidence_pending'
} else {
    Get-StringSha256 $versionProfile
}
if ((Get-StrictInteger $campaignManifest attempted_slots 0 $maximumRuns) -ne
        $entries.Count -or
    (Get-StrictInteger $campaignManifest accepted_slots 0 $maximumRuns) -ne
        $accepted -or
    (Get-StrictInteger $campaignManifest rejected_slots 0 $maximumRuns) -ne
        $rejected -or
    (Get-StrictInteger $campaignManifest incomplete_slots 0 $maximumRuns) -ne
        $incomplete -or
    (Get-StrictInteger $campaignManifest pending_slots 0 24) -ne
        (24 - $accepted) -or
    (Get-StrictInteger $campaignManifest.packet_totals sequenced_c2s 0 `
        100000000) -ne $sequencedC2s -or
    (Get-StrictInteger $campaignManifest.packet_totals sequenced_s2c 0 `
        100000000) -ne $sequencedS2c -or
    (Get-StrictInteger $campaignManifest.packet_totals reassembled 0 `
        100000000) -ne $reassembled -or
    (Get-StrictInteger $campaignManifest.packet_totals decompressed 0 `
        100000000) -ne $decompressed -or
    (Get-StrictInteger $campaignManifest.boundary_totals exact 0 `
        100000000) -ne $exactBoundaries -or
    (Get-StrictInteger $campaignManifest.boundary_totals candidates 0 `
        100000000) -ne $runtimeCandidates -or
    (Get-StrictInteger $campaignManifest.boundary_totals reconnect_generations 0 `
        100000000) -ne $reconnectGenerations -or
    [string]$campaignManifest.profile_fingerprint -cne
        $expectedProfileFingerprint -or
    [string]$campaignManifest.external_target_profile -cne
        [string]$canaryBinding.ExternalTargetProfile -or
    (Get-StrictInteger $campaignManifest external_target_count 0 4096) -ne
        [Int64]$canaryBinding.ExternalTargetCount -or
    [string]$campaignManifest.candidate_stability -cne
        $expectedCampaignStability -or
    [string]$campaignManifest.threshold_status -cne
        $expectedCampaignThreshold) {
    throw 'Campaign manifest disagrees with independently verified facts.'
}

Write-Output "[stock-runtime-first-verify] canary-run-id=$($canaryBinding.RunId)"
Write-Output '[stock-runtime-first-verify] canary-accepted=true'
Write-Output '[stock-runtime-first-verify] canary-counted-in-matrix=false'
Write-Output "[stock-runtime-first-verify] accepted-runs=$accepted"
Write-Output "[stock-runtime-first-verify] rejected-runs=$rejected"
Write-Output "[stock-runtime-first-verify] incomplete-runs=$incomplete"
Write-Output "[stock-runtime-first-verify] pending-runs=$(24 - $accepted)"
Write-Output "[stock-runtime-first-verify] baseline-accepted=$baselineAccepted"
Write-Output "[stock-runtime-first-verify] idle-accepted=$idleAccepted"
Write-Output "[stock-runtime-first-verify] reconnect-accepted=$reconnectAccepted"
Write-Output "[stock-runtime-first-verify] perturbation-accepted=$perturbationAccepted"
Write-Output "[stock-runtime-first-verify] raw-datagrams=$rawDatagrams"
Write-Output "[stock-runtime-first-verify] sequenced-c2s=$sequencedC2s"
Write-Output "[stock-runtime-first-verify] sequenced-s2c=$sequencedS2c"
Write-Output "[stock-runtime-first-verify] fragments=$fragments"
Write-Output "[stock-runtime-first-verify] reassembled=$reassembled"
Write-Output "[stock-runtime-first-verify] decompressed=$decompressed"
Write-Output "[stock-runtime-first-verify] post-resource-observations=$postResource"
Write-Output "[stock-runtime-first-verify] exact-boundaries=$exactBoundaries"
Write-Output "[stock-runtime-first-verify] runtime-candidates=$runtimeCandidates"
Write-Output "[stock-runtime-first-verify] reconnect-generations=$reconnectGenerations"
Write-Output ("[stock-runtime-first-verify] candidate-cross-run-stability={0}" -f
    $(if ($accepted -ge $MinimumAcceptedRuns -and $null -ne $candidateProfile -and
            -not $candidateConflict -and -not $versionConflict) {
            'stable_observation'
        } else { 'evidence_pending' }))
Write-Output "[stock-runtime-first-verify] checker-deterministic-runs=$checkerDeterminism"
Write-Output "[stock-runtime-first-verify] walker-agreements=$walkerAgreements"
Write-Output "[stock-runtime-first-verify] implementation-commit=$campaignImplementationCommit"
Write-Output ("[stock-runtime-first-verify] campaign-structural-hash={0}" -f
    [string]$campaignManifest.campaign_structural_sha256)

if (-not $thresholdReady) {
    if (Test-Path -LiteralPath $evidencePath) {
        throw 'First-observation evidence JSON exists before its threshold passes.'
    }
    Write-Output '[stock-runtime-first-verify] evidence-threshold=pending'
    Write-Output '[stock-runtime-first-verify] evidence-json=absent'
    Write-Output '[stock-runtime-first-verify] result=evidence_pending'
    throw 'Stock runtime first-observation evidence threshold is not met.'
}

if (-not (Test-Path -LiteralPath $evidencePath -PathType Leaf)) {
    Write-Output '[stock-runtime-first-verify] evidence-threshold=passed'
    Write-Output '[stock-runtime-first-verify] evidence-json=pending-publication'
    throw 'Threshold passed, but the sanitized evidence-only publication is absent.'
}
$boundedEvidence = Open-BoundedEvidenceText `
    $evidencePath 1048576 'first-observation evidence'
try {
$evidenceText = [string]$boundedEvidence.Text
Assert-SanitizedEvidenceText $evidenceText
try {
    $evidence = ConvertFrom-Json -InputObject $evidenceText -ErrorAction Stop
} catch {
    throw 'First-observation evidence is invalid JSON.'
}
$evidenceKeys = @(
    'schema', 'implementation_commit', 'stock_profile', 'isolation_profile',
    'run_counts', 'map_scenario_ordinals', 'transport_counts',
    'observation_counts', 'boundary', 'candidate',
    'transport_structural_hashes', 'replay_structural_hashes', 'restoration')
Assert-ExactProperties $evidence $evidenceKeys 'first-observation evidence'
if ([string]$evidence.schema -cne
        'hlclient.goldsrc-stock-runtime-first-observations.v1' -or
    [string]$evidence.implementation_commit -cnotmatch '^[0-9a-f]{40}$') {
    throw 'First-observation evidence identity is invalid.'
}
$implementationCommit = [string]$evidence.implementation_commit
if ($implementationCommit -cne $campaignImplementationCommit) {
    throw 'Evidence implementation commit differs from the pinned campaign commit.'
}
Assert-ExactImplementationCommit $implementationCommit `
    'Evidence implementation commit'

Assert-ExactProperties $evidence.stock_profile @(
    'client_file_version', 'server_launcher_version',
    'server_engine_version', 'protocol', 'server_build', 'app_build') `
    'evidence stock profile'
if (($evidence.stock_profile | ConvertTo-Json -Compress) -cne
    ($versionEvidence | ConvertTo-Json -Compress)) {
    throw 'Evidence stock profile disagrees with accepted runs.'
}
Assert-ExactProperties $evidence.isolation_profile @(
    'session_type', 'persistent_rule_count', 'ipv4_loopback', 'ipv6_loopback',
    'non_loopback_canary', 'cleanup_status') 'evidence isolation profile'
if (($evidence.isolation_profile | ConvertTo-Json -Compress) -cne
    ($isolationEvidence | ConvertTo-Json -Compress)) {
    throw 'Evidence isolation profile disagrees with accepted runs.'
}
Assert-ExactProperties $evidence.run_counts @(
    'accepted', 'rejected', 'incomplete') 'evidence run counts'
if ((Get-StrictInteger $evidence.run_counts accepted `
        $MinimumAcceptedRuns $maximumRuns) -ne $accepted -or
    (Get-StrictInteger $evidence.run_counts rejected 0 $maximumRuns) -ne $rejected -or
    (Get-StrictInteger $evidence.run_counts incomplete 0 $maximumRuns) -ne $incomplete) {
    throw 'Evidence run counts disagree with the verified corpus.'
}

$expectedOrdinals = @(
    [ordered]@{ map_ordinal = 0; scenario_ordinal = 0; map_category = 'boot_camp'; scenario = 'baseline'; accepted_runs = $scenarioCounts['boot_camp|baseline'] },
    [ordered]@{ map_ordinal = 1; scenario_ordinal = 0; map_category = 'crossfire'; scenario = 'baseline'; accepted_runs = $scenarioCounts['crossfire|baseline'] },
    [ordered]@{ map_ordinal = 2; scenario_ordinal = 0; map_category = 'stalkyard'; scenario = 'baseline'; accepted_runs = $scenarioCounts['stalkyard|baseline'] },
    [ordered]@{ map_ordinal = 1; scenario_ordinal = 1; map_category = 'crossfire'; scenario = 'idle-runtime'; accepted_runs = $scenarioCounts['crossfire|idle-runtime'] },
    [ordered]@{ map_ordinal = 0; scenario_ordinal = 2; map_category = 'boot_camp'; scenario = 'reconnect'; accepted_runs = $scenarioCounts['boot_camp|reconnect'] },
    [ordered]@{ map_ordinal = 0; scenario_ordinal = 3; map_category = 'boot_camp'; scenario = 'drop-server-to-client-transport-ordinal'; accepted_runs = $scenarioCounts['boot_camp|drop-server-to-client-transport-ordinal'] },
    [ordered]@{ map_ordinal = 1; scenario_ordinal = 4; map_category = 'crossfire'; scenario = 'duplicate-server-to-client-transport-ordinal'; accepted_runs = $scenarioCounts['crossfire|duplicate-server-to-client-transport-ordinal'] },
    [ordered]@{ map_ordinal = 2; scenario_ordinal = 5; map_category = 'stalkyard'; scenario = 'reorder-server-to-client-transport-ordinal'; accepted_runs = $scenarioCounts['stalkyard|reorder-server-to-client-transport-ordinal'] })
$actualOrdinals = @($evidence.map_scenario_ordinals)
if ($actualOrdinals.Count -ne $expectedOrdinals.Count) {
    throw 'Evidence map/scenario ordinal cardinality is invalid.'
}
for ($index = 0; $index -lt $actualOrdinals.Count; $index++) {
    Assert-ExactProperties $actualOrdinals[$index] @(
        'map_ordinal', 'scenario_ordinal', 'map_category', 'scenario',
        'accepted_runs') "evidence map/scenario ordinal $index"
    foreach ($integerName in @('map_ordinal', 'scenario_ordinal', 'accepted_runs')) {
        [void](Get-StrictInteger $actualOrdinals[$index] $integerName 0 $maximumRuns)
    }
    if (($actualOrdinals[$index] | ConvertTo-Json -Compress) -cne
        ($expectedOrdinals[$index] | ConvertTo-Json -Compress)) {
        throw "Evidence map/scenario ordinal $index disagrees with the corpus."
    }
}

Assert-ExactProperties $evidence.transport_counts @(
    'sequenced_c2s', 'sequenced_s2c', 'reassembled_payloads',
    'decompressed_payloads') 'evidence transport counts'
if ((Get-StrictInteger $evidence.transport_counts sequenced_c2s 0 100000000) -ne
        $sequencedC2s -or
    (Get-StrictInteger $evidence.transport_counts sequenced_s2c `
        $MinimumSequencedServerPackets 100000000) -ne $sequencedS2c -or
    (Get-StrictInteger $evidence.transport_counts reassembled_payloads 0 100000000) -ne
        $reassembled -or
    (Get-StrictInteger $evidence.transport_counts decompressed_payloads 0 100000000) -ne
        $decompressed) {
    throw 'Evidence transport/replay counts disagree with the corpus.'
}

Assert-ExactProperties $evidence.observation_counts @(
    'exact_boundaries', 'runtime_candidates', 'reconnect_generations') `
    'evidence observation counts'
if ((Get-StrictInteger $evidence.observation_counts exact_boundaries 26 100000000) -ne
        $exactBoundaries -or
    (Get-StrictInteger $evidence.observation_counts runtime_candidates 26 100000000) -ne
        $runtimeCandidates -or
    (Get-StrictInteger $evidence.observation_counts reconnect_generations 4 100000000) -ne
        $reconnectGenerations) {
    throw 'Evidence boundary/candidate/reconnect totals disagree with the corpus.'
}

$boundaryKeys = @(
    'replay_payload_ordinal', 'corpus_observed_ordinal', 'delivery_ordinal',
    'byte_offset', 'bit_offset', 'source_netchan_sequence',
    'source_payload_byte_count', 'source_payload_bit_count',
    'next_unconsumed_bit_count', 'reassembled', 'decompressed', 'byte_aligned')
Assert-ExactProperties $evidence.boundary $boundaryKeys 'evidence boundary'
Assert-FirstObservationGeometry $evidence.boundary `
    (Get-StrictInteger $evidence.candidate bit_width 1 8) `
    ([string]$evidence.candidate.representation) 'evidence boundary/candidate'
if (($evidence.boundary | ConvertTo-Json -Compress) -cne
    ($boundaryEvidence | ConvertTo-Json -Compress)) {
    throw 'Evidence exact post-resource boundary disagrees with accepted runs.'
}

Assert-ExactProperties $evidence.candidate @(
    'neutral_name', 'representation', 'bit_width', 'recurrence_count',
    'stability', 'semantic_status', 'body_consumed') 'evidence candidate'
if ([string]$evidence.candidate.neutral_name -cne
        'first_post_resource_runtime_candidate' -or
    -not (Test-EvidenceCandidateMatchesBinding `
        $evidence.candidate $admittedCandidateBinding) -or
    (Get-StrictInteger $evidence.candidate recurrence_count `
        26 100000000) -ne $runtimeCandidates -or
    [string]$evidence.candidate.stability -cne 'stable_observation' -or
    [string]$evidence.candidate.semantic_status -cne 'unassigned' -or
    $evidence.candidate.body_consumed -cne $false) {
    throw 'Evidence candidate metadata/recurrence is invalid.'
}

$expectedTransportHashes = @($transportStructuralHashes | Sort-Object)
$expectedReplayHashes = @($replayStructuralHashes | Sort-Object)
$actualTransportHashes = @($evidence.transport_structural_hashes)
$actualReplayHashes = @($evidence.replay_structural_hashes)
if ($actualTransportHashes.Count -ne $accepted -or
    $actualReplayHashes.Count -ne $accepted -or
    @($actualTransportHashes | Where-Object { $_ -isnot [string] -or
            $_ -cnotmatch '^[0-9a-f]{64}$' }).Count -ne 0 -or
    @($actualReplayHashes | Where-Object { $_ -isnot [string] -or
            $_ -cnotmatch '^[0-9a-f]{64}$' }).Count -ne 0 -or
    (@($actualTransportHashes | Sort-Object) -join '|') -cne
        ($expectedTransportHashes -join '|') -or
    (@($actualReplayHashes | Sort-Object) -join '|') -cne
        ($expectedReplayHashes -join '|')) {
    throw 'Evidence structural hash sets disagree with accepted runs.'
}

Assert-ExactProperties $evidence.restoration @(
    'status', 'external_drift') 'evidence restoration'
if ([string]$evidence.restoration.status -cne 'exact' -or
    [string]$evidence.restoration.external_drift -cne 'none') {
    throw 'Evidence restoration/drift profile is invalid.'
}
$boundedEvidence.Reader.ValidateUnchanged()
Write-Output '[stock-runtime-first-verify] evidence-threshold=passed'
Write-Output '[stock-runtime-first-verify] evidence-json=valid'
Write-Output '[stock-runtime-first-verify] raw-artifacts-ignored=true'
Write-Output '[stock-runtime-first-verify] result=success'
} finally {
    $boundedEvidence.Reader.Dispose()
}
