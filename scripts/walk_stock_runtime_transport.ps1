#requires -Version 5.1

<#
.SYNOPSIS
Independently validates one stock-runtime transport journal and raw inventory.

.DESCRIPTION
This metadata walker does not invoke the production checker. It validates the
JSONL schema, ordinal/reference geometry, raw filenames, sizes and SHA-256,
then independently classifies connectionless and sequenced datagrams and the
sequenced header flags. It never prints packet bytes or endpoint addresses.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureRoot,

    [Parameter()][ValidateRange(-1, 65536)]
    [Int64]$BoundaryPayloadOrdinal = -1,
    [Parameter()][ValidateRange(-1, 65535)]
    [Int64]$BoundaryObservedOrdinal = -1,
    [Parameter()][ValidateRange(-1, 131071)]
    [Int64]$BoundaryDeliveryOrdinal = -1,
    [Parameter()][ValidateRange(-1, 1048576)]
    [Int64]$BoundaryByteOffset = -1,
    [Parameter()][ValidateRange(-1, 7)]
    [Int64]$BoundaryBitOffset = -1,
    [Parameter()][ValidateRange(-1, 1073741823)]
    [Int64]$BoundarySourceSequence = -1,
    [Parameter()][ValidateRange(-1, 1048576)]
    [Int64]$BoundarySourcePayloadBytes = -1,
    [Parameter()][ValidateRange(-1, 8388608)]
    [Int64]$BoundarySourcePayloadBits = -1,
    [Parameter()][ValidateRange(-1, 8388608)]
    [Int64]$BoundaryNextUnconsumedBits = -1,
    [Parameter()][ValidateSet('', 'true', 'false')]
    [string]$BoundaryReassembled = '',
    [Parameter()][ValidateSet('', 'true', 'false')]
    [string]$BoundaryDecompressed = '',
    [Parameter()][ValidateRange(0, 8)]
    [Int64]$CandidateBitWidth = 0,
    [Parameter()][AllowEmptyString()]
    [string]$FirstCandidate = '',

    [Parameter()][ValidateRange(-1, 65536)]
    [Int64]$GenerationBBoundaryPayloadOrdinal = -1,
    [Parameter()][ValidateRange(-1, 65535)]
    [Int64]$GenerationBBoundaryObservedOrdinal = -1,
    [Parameter()][ValidateRange(-1, 131071)]
    [Int64]$GenerationBBoundaryDeliveryOrdinal = -1,
    [Parameter()][ValidateRange(-1, 1048576)]
    [Int64]$GenerationBBoundaryByteOffset = -1,
    [Parameter()][ValidateRange(-1, 7)]
    [Int64]$GenerationBBoundaryBitOffset = -1,
    [Parameter()][ValidateRange(-1, 1073741823)]
    [Int64]$GenerationBBoundarySourceSequence = -1,
    [Parameter()][ValidateRange(-1, 1048576)]
    [Int64]$GenerationBBoundarySourcePayloadBytes = -1,
    [Parameter()][ValidateRange(-1, 8388608)]
    [Int64]$GenerationBBoundarySourcePayloadBits = -1,
    [Parameter()][ValidateRange(-1, 8388608)]
    [Int64]$GenerationBBoundaryNextUnconsumedBits = -1,
    [Parameter()][ValidateSet('', 'true', 'false')]
    [string]$GenerationBBoundaryReassembled = '',
    [Parameter()][ValidateSet('', 'true', 'false')]
    [string]$GenerationBBoundaryDecompressed = '',
    [Parameter()][ValidateRange(0, 8)]
    [Int64]$GenerationBCandidateBitWidth = 0,
    [Parameter()][AllowEmptyString()]
    [string]$GenerationBFirstCandidate = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$maximumEntries = 65536
$maximumJournalBytes = 67108864
$maximumLineBytes = 4096
$maximumPayloadBytes = 65507
$maximumTotalRawBytes = [Int64]536870912
$journalSchema = 'hlclient.stock-runtime-transport-journal.v1'
$requiredProperties = @(
    'schema', 'observed_ordinal', 'direction', 'direction_ordinal',
    'relative_timestamp_us', 'payload_byte_count', 'raw_filename',
    'source_role', 'destination_role', 'action', 'hold_state',
    'emitted_ordinals', 'delivered', 'wrong_source', 'sha256')
$allowedActions = @('forward', 'drop', 'duplicate', 'hold_for_delay', 'hold_for_reorder')
$allowedHoldStates = @('none', 'held', 'released', 'unresolved')
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$requiredCaptureParents = @(
    [IO.Path]::GetFullPath(
        (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime')).TrimEnd('\', '/'),
    [IO.Path]::GetFullPath(
        (Join-Path $repositoryRoot 'manual-artifacts\stock-runtime-canary')).TrimEnd('\', '/')
)

function Initialize-StockRuntimeWalkerBoundedReader {
    if ($null -ne ('Hlclient.StockRuntimeWalkerBoundedReader' -as [type])) {
        return
    }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Hlclient
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct StockRuntimeWalkerFileInformation
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

    public static class StockRuntimeWalkerBoundedReader
    {
        private const uint GenericRead = 0x80000000;
        private const uint FileReadAttributes = 0x80;
        private const uint FileShareRead = 0x1;
        private const uint OpenExisting = 3;
        private const uint FileFlagOpenReparsePoint = 0x00200000;
        private const uint FileFlagSequentialScan = 0x08000000;
        private const uint FileAttributeDirectory = 0x10;
        private const uint FileAttributeReparsePoint = 0x400;
        private const int FileStreamInfo = 7;
        private const int MaximumStreamInformationBytes = 64 * 1024;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateFile(
            string fileName, uint desiredAccess, uint shareMode,
            IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandle(
            IntPtr handle, out StockRuntimeWalkerFileInformation information);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandleEx(
            IntPtr handle, int informationClass, IntPtr information,
            uint bufferSize);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandle(
            IntPtr handle, StringBuilder path, uint pathLength, uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ReadFile(
            IntPtr handle, IntPtr buffer, uint bytesToRead,
            out uint bytesRead, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);

        public static byte[] Read(string path, long minimumBytes,
            long maximumBytes)
        {
            if (String.IsNullOrWhiteSpace(path) || minimumBytes < 1 ||
                maximumBytes < minimumBytes || maximumBytes > Int32.MaxValue)
                throw new InvalidOperationException(
                    "Bounded native read parameters are invalid.");
            string expectedPath = Path.GetFullPath(path).TrimEnd('\\', '/');
            IntPtr handle = CreateFile(
                expectedPath, GenericRead | FileReadAttributes, FileShareRead,
                IntPtr.Zero, OpenExisting,
                FileFlagOpenReparsePoint | FileFlagSequentialScan,
                IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Bounded native read open failed.");
            try
            {
                StockRuntimeWalkerFileInformation before = Information(handle);
                ulong size = FileSize(before);
                if (size < (ulong)minimumBytes || size > (ulong)maximumBytes)
                    throw new InvalidOperationException(
                        "Bounded native read size is invalid.");
                RequireOrdinaryUniqueFile(handle, expectedPath, before, size);
                RequireOnlyDefaultDataStream(handle, size);

                byte[] bytes = new byte[(int)size];
                GCHandle pinned = GCHandle.Alloc(bytes, GCHandleType.Pinned);
                try
                {
                    int offset = 0;
                    while (offset < bytes.Length)
                    {
                        uint read;
                        if (!ReadFile(handle,
                                IntPtr.Add(pinned.AddrOfPinnedObject(), offset),
                                (uint)(bytes.Length - offset), out read,
                                IntPtr.Zero))
                            throw new Win32Exception(Marshal.GetLastWin32Error(),
                                "Bounded native read failed.");
                        if (read == 0)
                            throw new InvalidOperationException(
                                "Bounded native read ended before its retained length.");
                        offset = checked(offset + (int)read);
                    }
                }
                finally
                {
                    pinned.Free();
                }

                byte[] extra = new byte[1];
                GCHandle extraPinned = GCHandle.Alloc(extra, GCHandleType.Pinned);
                try
                {
                    uint extraRead;
                    if (!ReadFile(handle, extraPinned.AddrOfPinnedObject(), 1,
                            out extraRead, IntPtr.Zero))
                        throw new Win32Exception(Marshal.GetLastWin32Error(),
                            "Bounded native EOF verification failed.");
                    if (extraRead != 0)
                        throw new InvalidOperationException(
                            "Bounded native read length changed.");
                }
                finally
                {
                    extraPinned.Free();
                }

                StockRuntimeWalkerFileInformation after = Information(handle);
                if (!SameInformation(before, after))
                    throw new InvalidOperationException(
                        "Bounded native read identity changed.");
                RequireOrdinaryUniqueFile(handle, expectedPath, after, size);
                RequireOnlyDefaultDataStream(handle, size);
                return bytes;
            }
            finally
            {
                CloseHandle(handle);
            }
        }

        private static StockRuntimeWalkerFileInformation Information(
            IntPtr handle)
        {
            StockRuntimeWalkerFileInformation information;
            if (!GetFileInformationByHandle(handle, out information))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Bounded native identity query failed.");
            return information;
        }

        private static ulong FileSize(StockRuntimeWalkerFileInformation value)
        {
            return ((ulong)value.FileSizeHigh << 32) | value.FileSizeLow;
        }

        private static bool SameFileTime(
            System.Runtime.InteropServices.ComTypes.FILETIME left,
            System.Runtime.InteropServices.ComTypes.FILETIME right)
        {
            return left.dwHighDateTime == right.dwHighDateTime &&
                left.dwLowDateTime == right.dwLowDateTime;
        }

        private static bool SameInformation(
            StockRuntimeWalkerFileInformation left,
            StockRuntimeWalkerFileInformation right)
        {
            return left.FileAttributes == right.FileAttributes &&
                left.VolumeSerialNumber == right.VolumeSerialNumber &&
                left.FileSizeHigh == right.FileSizeHigh &&
                left.FileSizeLow == right.FileSizeLow &&
                left.NumberOfLinks == right.NumberOfLinks &&
                left.FileIndexHigh == right.FileIndexHigh &&
                left.FileIndexLow == right.FileIndexLow &&
                SameFileTime(left.CreationTime, right.CreationTime) &&
                SameFileTime(left.LastWriteTime, right.LastWriteTime);
        }

        private static void RequireOrdinaryUniqueFile(IntPtr handle,
            string expectedPath, StockRuntimeWalkerFileInformation information,
            ulong expectedSize)
        {
            if ((information.FileAttributes &
                    (FileAttributeDirectory | FileAttributeReparsePoint)) != 0 ||
                information.NumberOfLinks != 1 ||
                FileSize(information) != expectedSize ||
                !String.Equals(FinalPath(handle), expectedPath,
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "Bounded native read file identity is invalid.");
        }

        private static string FinalPath(IntPtr handle)
        {
            StringBuilder path = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandle(
                handle, path, (uint)path.Capacity, 0);
            if (length == 0 || length >= (uint)path.Capacity)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Bounded native final-path query failed.");
            string value = path.ToString();
            const string uncPrefix = @"\\?\UNC\";
            const string extendedPrefix = @"\\?\";
            if (value.StartsWith(uncPrefix, StringComparison.OrdinalIgnoreCase))
                value = @"\\" + value.Substring(uncPrefix.Length);
            else if (value.StartsWith(
                    extendedPrefix, StringComparison.OrdinalIgnoreCase))
                value = value.Substring(extendedPrefix.Length);
            return value.TrimEnd('\\', '/');
        }

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
                        "Bounded native stream inventory failed.");
                int offset = 0;
                int count = 0;
                while (true)
                {
                    if (offset < 0 ||
                        offset > MaximumStreamInformationBytes - 24)
                        throw new InvalidOperationException(
                            "Bounded native stream inventory is invalid.");
                    uint next = unchecked((uint)Marshal.ReadInt32(buffer, offset));
                    uint nameBytes = unchecked((uint)Marshal.ReadInt32(
                        buffer, offset + 4));
                    long streamSize = Marshal.ReadInt64(buffer, offset + 8);
                    if (nameBytes == 0 || (nameBytes & 1U) != 0U ||
                        nameBytes > (uint)(MaximumStreamInformationBytes -
                            offset - 24))
                        throw new InvalidOperationException(
                            "Bounded native stream inventory is invalid.");
                    string name = Marshal.PtrToStringUni(
                        IntPtr.Add(buffer, offset + 24),
                        checked((int)(nameBytes / 2U)));
                    ++count;
                    if (count != 1 ||
                        !String.Equals(name, "::$DATA",
                            StringComparison.Ordinal) ||
                        streamSize < 0 || (ulong)streamSize != expectedSize)
                        throw new InvalidOperationException(
                            "Bounded native read requires only the default data stream.");
                    if (next == 0U) break;
                    if (next < 24U + nameBytes ||
                        next > (uint)(MaximumStreamInformationBytes - offset))
                        throw new InvalidOperationException(
                            "Bounded native stream inventory is invalid.");
                    offset = checked(offset + (int)next);
                }
                if (count != 1)
                    throw new InvalidOperationException(
                        "Bounded native read requires only the default data stream.");
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
    }
}
'@
}

function Read-BoundedOrdinaryFile {
    param(
        [string]$Path,
        [Int64]$MinimumBytes,
        [Int64]$MaximumBytes,
        [string]$Label)
    Initialize-StockRuntimeWalkerBoundedReader
    try {
        return [Hlclient.StockRuntimeWalkerBoundedReader]::Read(
            [IO.Path]::GetFullPath($Path), $MinimumBytes, $MaximumBytes)
    } catch {
        $failure = $_.Exception
        while ($null -ne $failure.InnerException) {
            $failure = $failure.InnerException
        }
        throw "$Label retained-handle read failed: $($failure.Message)"
    }
}

function ConvertFrom-StrictUtf8 {
    param([byte[]]$Bytes, [string]$Label)
    try {
        return [Text.UTF8Encoding]::new($false, $true).GetString($Bytes)
    } catch {
        throw "$Label is not strict UTF-8."
    }
}

function Get-ByteSha256 {
    param([byte[]]$Bytes)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString(
                $algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($full)
    $current = $root
    foreach ($component in @($full.Substring($root.Length) -split '[\\/]' |
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
    $linkType = $item.PSObject.Properties['LinkType']
    if ($null -eq $linkType -or -not [string]::IsNullOrEmpty([string]$linkType.Value)) {
        throw "$Label must be an unlinked regular file."
    }
}

function Get-StrictInteger {
    param([object]$Value, [string]$Name, [Int64]$Minimum, [Int64]$Maximum)
    $property = $Value.PSObject.Properties[$Name]
    if ($null -eq $property -or $property.Value -is [bool] -or
        $property.Value -isnot [ValueType]) {
        throw "Journal property $Name is not an integer."
    }
    [Int64]$number = $property.Value
    if ([double]$property.Value -ne [double]$number -or
        $number -lt $Minimum -or $number -gt $Maximum) {
        throw "Journal property $Name is outside its bound."
    }
    return $number
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Names, [string]$Label)
    if ($null -eq $Value) { throw "$Label is absent." }
    $actual = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    if ($actual.Count -ne $Names.Count -or
        @($actual | Sort-Object -Unique).Count -ne $Names.Count) {
        throw "$Label does not have its exact property cardinality."
    }
    foreach ($name in $actual) {
        if ($Names -cnotcontains $name) {
            throw "$Label contains unknown property $name."
        }
    }
}

function Read-BoundedJson {
    param([string]$Path, [Int64]$MaximumBytes, [string]$Label)
    [byte[]]$bytes = Read-BoundedOrdinaryFile $Path 2 $MaximumBytes $Label
    $text = ConvertFrom-StrictUtf8 $bytes $Label
    try { return $text | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "$Label is invalid JSON." }
}

function Test-ConnectionlessAsciiPrefix {
    param([byte[]]$Bytes, [string]$Prefix)
    if ($Bytes.Length -lt (4 + $Prefix.Length) -or
        $Bytes[0] -ne 0xff -or $Bytes[1] -ne 0xff -or
        $Bytes[2] -ne 0xff -or $Bytes[3] -ne 0xff) {
        return $false
    }
    $expected = [Text.Encoding]::ASCII.GetBytes($Prefix)
    for ($index = 0; $index -lt $expected.Length; $index++) {
        if ($Bytes[4 + $index] -ne $expected[$index]) { return $false }
    }
    return $true
}

function Get-StringSha256 {
    param([string]$Value)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Value)
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally { $sha.Dispose() }
}

function Get-UInt32LittleEndian {
    param([byte[]]$Bytes, [int]$Offset)
    return [uint32]$Bytes[$Offset] -bor
        ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 3] -shl 24)
}

function Decode-NetchanPayload {
    param([byte[]]$Datagram, [uint32]$NumericSequence)
    $decoded = [byte[]]::new($Datagram.Length - 8)
    [Array]::Copy($Datagram, 8, $decoded, 0, $decoded.Length)
    $table = [byte[]](
        0x05, 0x61, 0x7a, 0xed, 0x1b, 0xca, 0x0d, 0x9b,
        0x4a, 0xf1, 0x64, 0xc7, 0xb5, 0x8e, 0xdf, 0xa0)
    [uint64]$key = $NumericSequence -band 0xff
    [uint64]$inverseKey = [uint64]0xffffffff - $key
    $wordCount = [Math]::Floor($decoded.Length / 4)
    for ($wordIndex = 0; $wordIndex -lt $wordCount; $wordIndex++) {
        $offset = $wordIndex * 4
        [uint64]$value = Get-UInt32LittleEndian $decoded $offset
        $value = ($value -bxor $key) -band [uint64]0xffffffff
        for ($byteIndex = 0; $byteIndex -lt 4; $byteIndex++) {
            $shiftedIndex = [uint64]$byteIndex -shl $byteIndex
            $tableValue = [uint64]$table[(($wordIndex -band 15) + $byteIndex) -band 15]
            $mask = ([uint64]0xa5 -bor $shiftedIndex -bor
                [uint64]$byteIndex -bor $tableValue) -band 0xff
            $value = ($value -bxor ($mask -shl ($byteIndex * 8))) -band
                [uint64]0xffffffff
        }
        $swapped = (($value -band 0x000000ff) -shl 24) -bor
            (($value -band 0x0000ff00) -shl 8) -bor
            (($value -band 0x00ff0000) -shr 8) -bor
            (($value -band 0xff000000) -shr 24)
        $value = ($swapped -bxor $inverseKey) -band [uint64]0xffffffff
        for ($byteIndex = 0; $byteIndex -lt 4; $byteIndex++) {
            $decoded[$offset + $byteIndex] = [byte](
                ($value -shr ($byteIndex * 8)) -band 0xff)
        }
    }
    return ,$decoded
}

function Get-NetchanGeometry {
    param([byte[]]$Bytes)
    $isConnectionless = $Bytes.Length -ge 4 -and $Bytes[0] -eq 0xff -and
        $Bytes[1] -eq 0xff -and $Bytes[2] -eq 0xff -and $Bytes[3] -eq 0xff
    if ($isConnectionless) {
        return [pscustomobject]@{
            Classification = 'connectionless'; Fragmented = $false
            Reliable = $false; FragmentDescriptorCount = 0
            Sequence = $null; PayloadByteCount = $Bytes.Length - 4
        }
    }
    if ($Bytes.Length -lt 8) { throw 'Datagram is shorter than the netchan header.' }
    if ($Bytes.Length -gt 16384) {
        throw 'Sequenced datagram exceeds the established netchan codec bound.'
    }
    [uint32]$sequenceWord = Get-UInt32LittleEndian $Bytes 0
    [uint32]$acknowledgementWord = Get-UInt32LittleEndian $Bytes 4
    if ($sequenceWord -eq [uint32]0xfffffffe -or
        ($acknowledgementWord -band [uint32]0x40000000) -ne 0) {
        throw 'Datagram uses an unsupported special/reserved netchan word.'
    }
    [uint32]$numericSequence = $sequenceWord -band [uint32]0x3fffffff
    $fragmented = ($sequenceWord -band [uint32]0x40000000) -ne 0
    $reliable = ($sequenceWord -band [uint32]0x80000000) -ne 0
    $descriptorCount = 0
    if ($fragmented) {
        $body = Decode-NetchanPayload $Bytes $numericSequence
        $cursor = 0
        $ranges = [Collections.Generic.List[object]]::new()
        for ($slot = 0; $slot -lt 2; $slot++) {
            if ($cursor -ge $body.Length) { throw 'Fragment presence geometry is truncated.' }
            $presence = $body[$cursor]
            $cursor++
            if ($presence -gt 1) { throw 'Fragment presence value is invalid.' }
            if ($presence -eq 0) { continue }
            if ($cursor -gt ($body.Length - 8)) { throw 'Fragment descriptor is truncated.' }
            [uint32]$packed = Get-UInt32LittleEndian $body $cursor
            $offset = [uint16]($body[$cursor + 4] -bor ($body[$cursor + 5] -shl 8))
            $length = [uint16]($body[$cursor + 6] -bor ($body[$cursor + 7] -shl 8))
            $cursor += 8
            $fragmentIndex = [uint16]($packed -shr 16)
            $fragmentCount = [uint16]($packed -band 0xffff)
            if ($fragmentIndex -eq 0 -or $fragmentCount -eq 0 -or
                $fragmentIndex -gt $fragmentCount -or $length -eq 0 -or
                ([Int64]$offset + [Int64]$length) -gt 65535) {
                throw 'Fragment ID/count/length geometry is invalid.'
            }
            [void]$ranges.Add([pscustomobject]@{
                    Offset = [Int64]$offset; Length = [Int64]$length })
            $descriptorCount++
        }
        if ($descriptorCount -eq 0) {
            throw 'Fragment flag is set without a present descriptor.'
        }
        [Int64]$payloadLength = $body.Length - $cursor
        [Int64]$expectedOffset = 0
        foreach ($range in @($ranges | Sort-Object Offset)) {
            if ($range.Offset -ne $expectedOffset -or
                $range.Offset -gt ($payloadLength - $range.Length)) {
                throw 'Fragment payload ranges are overlapping, gapped or out of bounds.'
            }
            $expectedOffset += $range.Length
        }
    }
    return [pscustomobject]@{
        Classification = 'sequenced'; Fragmented = $fragmented
        Reliable = $reliable; FragmentDescriptorCount = $descriptorCount
        Sequence = [Int64]$numericSequence
        PayloadByteCount = $(if ($fragmented) { $null } else { $Bytes.Length - 8 })
    }
}

function New-BoundaryMetadata {
    param(
        [Int64]$PayloadOrdinal, [Int64]$ObservedOrdinal,
        [Int64]$DeliveryOrdinal, [Int64]$ByteOffset, [Int64]$BitOffset,
        [Int64]$SourceSequence, [Int64]$SourcePayloadBytes,
        [Int64]$SourcePayloadBits, [Int64]$NextUnconsumedBits,
        [bool]$Reassembled, [bool]$Decompressed,
        [Int64]$BitWidth, [string]$Candidate)
    return [pscustomobject]@{
        PayloadOrdinal = $PayloadOrdinal
        ObservedOrdinal = $ObservedOrdinal
        DeliveryOrdinal = $DeliveryOrdinal
        ByteOffset = $ByteOffset
        BitOffset = $BitOffset
        SourceSequence = $SourceSequence
        SourcePayloadBytes = $SourcePayloadBytes
        SourcePayloadBits = $SourcePayloadBits
        NextUnconsumedBits = $NextUnconsumedBits
        Reassembled = $Reassembled
        Decompressed = $Decompressed
        CandidateBitWidth = $BitWidth
        FirstCandidate = $Candidate
    }
}

$boundaryArgumentCount = @(
    ($BoundaryPayloadOrdinal -ge 0), ($BoundaryObservedOrdinal -ge 0),
    ($BoundaryDeliveryOrdinal -ge 0), ($BoundaryByteOffset -ge 0),
    ($BoundaryBitOffset -ge 0), ($BoundarySourceSequence -ge 0),
    ($BoundarySourcePayloadBytes -ge 0), ($BoundarySourcePayloadBits -ge 0),
    ($BoundaryNextUnconsumedBits -ge 0),
    (-not [string]::IsNullOrEmpty($BoundaryReassembled)),
    (-not [string]::IsNullOrEmpty($BoundaryDecompressed)),
    ($CandidateBitWidth -gt 0),
    (-not [string]::IsNullOrEmpty($FirstCandidate)) |
        Where-Object { $_ }).Count
if ($boundaryArgumentCount -ne 0 -and $boundaryArgumentCount -ne 13) {
    throw 'Exact boundary metadata arguments must be supplied as one complete set.'
}
$requestedBoundary = $null
if ($boundaryArgumentCount -eq 13) {
    $requestedBoundary = New-BoundaryMetadata `
        $BoundaryPayloadOrdinal $BoundaryObservedOrdinal $BoundaryDeliveryOrdinal `
        $BoundaryByteOffset $BoundaryBitOffset $BoundarySourceSequence `
        $BoundarySourcePayloadBytes $BoundarySourcePayloadBits `
        $BoundaryNextUnconsumedBits ($BoundaryReassembled -ceq 'true') `
        ($BoundaryDecompressed -ceq 'true') $CandidateBitWidth $FirstCandidate
}

$generationBBoundaryArgumentCount = @(
    ($GenerationBBoundaryPayloadOrdinal -ge 0),
    ($GenerationBBoundaryObservedOrdinal -ge 0),
    ($GenerationBBoundaryDeliveryOrdinal -ge 0),
    ($GenerationBBoundaryByteOffset -ge 0),
    ($GenerationBBoundaryBitOffset -ge 0),
    ($GenerationBBoundarySourceSequence -ge 0),
    ($GenerationBBoundarySourcePayloadBytes -ge 0),
    ($GenerationBBoundarySourcePayloadBits -ge 0),
    ($GenerationBBoundaryNextUnconsumedBits -ge 0),
    (-not [string]::IsNullOrEmpty($GenerationBBoundaryReassembled)),
    (-not [string]::IsNullOrEmpty($GenerationBBoundaryDecompressed)),
    ($GenerationBCandidateBitWidth -gt 0),
    (-not [string]::IsNullOrEmpty($GenerationBFirstCandidate)) |
        Where-Object { $_ }).Count
if ($generationBBoundaryArgumentCount -ne 0 -and
    $generationBBoundaryArgumentCount -ne 13) {
    throw 'Generation-B boundary arguments must be supplied as one complete set.'
}
$requestedGenerationBBoundary = $null
if ($generationBBoundaryArgumentCount -eq 13) {
    $requestedGenerationBBoundary = New-BoundaryMetadata `
        $GenerationBBoundaryPayloadOrdinal `
        $GenerationBBoundaryObservedOrdinal `
        $GenerationBBoundaryDeliveryOrdinal `
        $GenerationBBoundaryByteOffset $GenerationBBoundaryBitOffset `
        $GenerationBBoundarySourceSequence `
        $GenerationBBoundarySourcePayloadBytes `
        $GenerationBBoundarySourcePayloadBits `
        $GenerationBBoundaryNextUnconsumedBits `
        ($GenerationBBoundaryReassembled -ceq 'true') `
        ($GenerationBBoundaryDecompressed -ceq 'true') `
        $GenerationBCandidateBitWidth $GenerationBFirstCandidate
}

$root = [IO.Path]::GetFullPath($CaptureRoot).TrimEnd('\', '/')
$rootParent = [IO.Path]::GetFullPath(
    (Split-Path -Parent $root)).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $root -PathType Container) -or
    [IO.Path]::GetFileName($root) -cnotmatch '^[0-9a-f]{32}$' -or
    @($requiredCaptureParents | Where-Object { $_ -ieq $rootParent }).Count -ne 1) {
    throw 'CaptureRoot must be an exact run child of a repository stock-runtime campaign or canary root.'
}
Assert-NoReparsePointInExistingPath $root 'capture run'
$runItem = Get-Item -LiteralPath $root -Force
if (($runItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'CaptureRoot must not be a reparse point.'
}
$runId = $runItem.Name
$journalPath = Join-Path $root 'transport-journal.jsonl'
$rawRoot = Join-Path $root 'raw'
if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf)) {
    throw 'transport-journal.jsonl is absent.'
}
if (-not (Test-Path -LiteralPath $rawRoot -PathType Container)) {
    throw 'raw directory is absent.'
}
Assert-NoReparsePointInExistingPath $rawRoot 'raw inventory'
[byte[]]$journalBytes = Read-BoundedOrdinaryFile $journalPath 1 `
    $maximumJournalBytes 'transport journal'
$journalText = ConvertFrom-StrictUtf8 $journalBytes 'transport journal'
$lineList = [Collections.Generic.List[string]]::new()
$lineReader = [IO.StringReader]::new($journalText)
try {
    while ($true) {
        $line = $lineReader.ReadLine()
        if ($null -eq $line) { break }
        [void]$lineList.Add($line)
    }
} finally {
    $lineReader.Dispose()
}
$lines = @($lineList)
if ($lines.Count -lt 1 -or $lines.Count -gt $maximumEntries) {
    throw 'Transport journal cardinality is outside its bound.'
}
$rawFiles = @(Get-ChildItem -LiteralPath $rawRoot -Force)
if ($rawFiles.Count -ne $lines.Count) {
    throw 'Journal/raw cardinality mismatch.'
}
foreach ($rawItem in $rawFiles) {
    if ($rawItem.PSIsContainer -or
        ($rawItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        $rawItem.Name -cnotmatch '^[0-9]{8}-(?:c2s|s2c)\.bin$') {
        throw 'Raw inventory contains an unexpected entry.'
    }
}

$seenRaw = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$emissions = [Collections.Generic.List[Int64]]::new()
[Int64]$totalRawBytes = 0
[Int64]$lastTimestamp = -1
$directionCounts = @{ c2s = [Int64]0; s2c = [Int64]0 }
$observedConnectionless = @{ c2s = [Int64]0; s2c = [Int64]0 }
$observedSequenced = @{ c2s = [Int64]0; s2c = [Int64]0 }
$deliveredConnectionless = @{ c2s = [Int64]0; s2c = [Int64]0 }
$deliveredSequenced = @{ c2s = [Int64]0; s2c = [Int64]0 }
[Int64]$observedFragments = 0
[Int64]$deliveredFragments = 0
[Int64]$observedReliable = 0
[Int64]$deliveredReliable = 0
[Int64]$deliveredC2s = 0
[Int64]$deliveredS2c = 0
[Int64]$wrongSourceCount = 0
$deliveryRecords = [Collections.Generic.Dictionary[Int64, object]]::new()
$semanticEntries = [Collections.Generic.List[object]]::new()
$observedRecords = [Collections.Generic.List[object]]::new()
$transportComplete = $true
[Int64]$lastDeliveredSequencedS2cTimestampUs = -1

for ($index = 0; $index -lt $lines.Count; $index++) {
    $line = [string]$lines[$index]
    if ([Text.Encoding]::UTF8.GetByteCount($line) -lt 2 -or
        [Text.Encoding]::UTF8.GetByteCount($line) -gt $maximumLineBytes) {
        throw "Journal line $index is outside its byte bound."
    }
    $names = @([regex]::Matches($line, '"(?<name>[a-z0-9_]+)"\s*:') |
        ForEach-Object { $_.Groups['name'].Value })
    if ($names.Count -ne $requiredProperties.Count -or
        @($names | Sort-Object -Unique).Count -ne $requiredProperties.Count) {
        throw "Journal line $index has duplicate, missing or unknown properties."
    }
    foreach ($name in $names) {
        if ($requiredProperties -cnotcontains $name) {
            throw "Journal line $index has unknown property $name."
        }
    }
    try { $entry = $line | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "Journal line $index is invalid JSON." }
    if ([string]$entry.schema -cne $journalSchema) {
        throw "Journal line $index has the wrong schema."
    }
    if ((Get-StrictInteger $entry observed_ordinal 0 ($maximumEntries - 1)) -ne $index) {
        throw "Journal observed ordinals are not contiguous at $index."
    }
    $direction = [string]$entry.direction
    if ($direction -cne 'client_to_server' -and
        $direction -cne 'server_to_client') {
        throw "Journal line $index has an invalid direction."
    }
    $shortDirection = if ($direction -ceq 'client_to_server') { 'c2s' } else { 's2c' }
    $directionCounts[$shortDirection]++
    if ((Get-StrictInteger $entry direction_ordinal 1 $maximumEntries) -ne
        $directionCounts[$shortDirection]) {
        throw "Journal direction ordinals are not contiguous at $index."
    }
    $timestamp = Get-StrictInteger $entry relative_timestamp_us 0 300000000
    if ($timestamp -lt $lastTimestamp) { throw 'Journal timestamps are not monotonic.' }
    $lastTimestamp = $timestamp
    $payloadBytes = Get-StrictInteger $entry payload_byte_count 1 $maximumPayloadBytes
    if ($totalRawBytes -gt ($maximumTotalRawBytes - $payloadBytes)) {
        throw 'Raw inventory exceeds its total byte bound.'
    }
    $totalRawBytes += $payloadBytes
    $expectedName = '{0:D8}-{1}.bin' -f $index, $shortDirection
    if ([string]$entry.raw_filename -cne $expectedName -or
        -not $seenRaw.Add($expectedName)) {
        throw "Journal raw filename is invalid at $index."
    }
    $expectedSource = if ($shortDirection -ceq 'c2s') {
        'research_client'
    } else { 'research_server' }
    $expectedDestination = if ($shortDirection -ceq 'c2s') {
        'research_server'
    } else { 'research_client' }
    if ([string]$entry.destination_role -cne $expectedDestination -or
        (-not [bool]$entry.wrong_source -and
            [string]$entry.source_role -cne $expectedSource) -or
        ([bool]$entry.wrong_source -and
            [string]$entry.source_role -cne 'unexpected_source')) {
        throw "Journal roles disagree with direction/source admission at $index."
    }
    if ($allowedActions -cnotcontains [string]$entry.action -or
        $allowedHoldStates -cnotcontains [string]$entry.hold_state -or
        $entry.delivered -isnot [bool] -or $entry.wrong_source -isnot [bool]) {
        throw "Journal action/state is invalid at $index."
    }
    if ($entry.wrong_source) {
        $wrongSourceCount++
        $transportComplete = $false
    }
    $entryEmissions = @($entry.emitted_ordinals)
    if ($entryEmissions.Count -gt 2) { throw 'Journal entry exceeds its emission bound.' }
    foreach ($emission in $entryEmissions) {
        if ($emission -is [bool] -or $emission -isnot [ValueType]) {
            throw 'Journal emission ordinal is not an integer.'
        }
        [Int64]$emissionValue = $emission
        if ([double]$emission -ne [double]$emissionValue -or
            $emissionValue -lt 0 -or $emissionValue -ge ($maximumEntries * 2)) {
            throw 'Journal emission ordinal is outside its bound.'
        }
        [void]$emissions.Add($emissionValue)
    }
    for ($emissionIndex = 1; $emissionIndex -lt $entryEmissions.Count;
        $emissionIndex++) {
        if ([Int64]$entryEmissions[$emissionIndex] -ne
            ([Int64]$entryEmissions[$emissionIndex - 1] + 1)) {
            throw 'One datagram duplicate emissions are not consecutive.'
        }
    }
    if ([bool]$entry.delivered -ne ($entryEmissions.Count -ne 0)) {
        throw "Journal delivered state disagrees with emissions at $index."
    }
    $action = [string]$entry.action
    $holdState = [string]$entry.hold_state
    if ($action -ceq 'forward') {
        $requiredEmissions = if ($entry.wrong_source) { 0 } else { 1 }
        if ($holdState -cne 'none' -or
            $entryEmissions.Count -ne $requiredEmissions) {
            throw 'Forward journal action has invalid hold/emission state.'
        }
    } elseif ($action -ceq 'drop') {
        if ($holdState -cne 'none' -or $entryEmissions.Count -ne 0) {
            throw 'Drop journal action has invalid hold/emission state.'
        }
    } elseif ($action -ceq 'duplicate') {
        if ($entry.wrong_source -or $holdState -cne 'none' -or
            $entryEmissions.Count -ne 2) {
            throw 'Duplicate journal action must own two emissions.'
        }
    } else {
        if ($entry.wrong_source) {
            throw 'Wrong-source journal entry cannot enter a held action.'
        }
        if ($holdState -ceq 'held') {
            if ($entryEmissions.Count -ne 0) {
                throw 'Held journal action cannot own an emission.'
            }
            $transportComplete = $false
        } elseif ($holdState -ceq 'unresolved') {
            if ($entryEmissions.Count -gt 1) {
                throw 'Unresolved journal action exceeds its deadline emission bound.'
            }
            $transportComplete = $false
        } elseif ($holdState -cne 'released' -or $entryEmissions.Count -ne 1) {
            throw 'Held journal action has invalid hold/emission state.'
        }
    }
    if ($entry.delivered) {
        if ($shortDirection -ceq 'c2s') { $deliveredC2s += $entryEmissions.Count }
        else { $deliveredS2c += $entryEmissions.Count }
    }

    $rawPath = Join-Path $rawRoot $expectedName
    [byte[]]$bytes = Read-BoundedOrdinaryFile $rawPath $payloadBytes `
        $payloadBytes 'raw datagram'
    if ([string]$entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        (Get-ByteSha256 $bytes) -cne [string]$entry.sha256) {
        throw "Raw size or SHA-256 mismatch at $index."
    }
    $geometry = Get-NetchanGeometry $bytes
    $connectPrefix = Test-ConnectionlessAsciiPrefix $bytes 'connect 48 '
    $acceptPrefix = Test-ConnectionlessAsciiPrefix $bytes 'B '
    if ($geometry.Classification -ceq 'connectionless') {
        $observedConnectionless[$shortDirection]++
    } else {
        $observedSequenced[$shortDirection]++
        if ($geometry.Fragmented) { $observedFragments++ }
        if ($geometry.Reliable) { $observedReliable++ }
    }
    [void]$observedRecords.Add([pscustomobject]@{
            ObservedOrdinal = [Int64]$index
            Direction = $shortDirection
            Path = $rawPath
            Geometry = $geometry
            ConnectPrefix = $connectPrefix
            AcceptPrefix = $acceptPrefix
        })
    foreach ($emission in $entryEmissions) {
        [Int64]$emissionValue = $emission
        if ($deliveryRecords.ContainsKey($emissionValue)) {
            throw 'Emission ordinal is referenced more than once.'
        }
        $deliveryRecords.Add($emissionValue, [pscustomobject]@{
                Path = $rawPath; Direction = $shortDirection
                ObservedOrdinal = [Int64]$index
                RelativeTimestampUs = [Int64]$timestamp
                Geometry = $geometry })
    }
    [void]$semanticEntries.Add([pscustomobject]@{
            Direction = $shortDirection
            Action = $action
            HoldState = $holdState
            Emissions = @($entryEmissions | ForEach-Object { [Int64]$_ })
        })
}

# A released hold is paired with the next observed datagram in the same
# direction. Delay emits the held bytes first; reorder emits the successor
# first. A deadline-released delay may have no successor, but reorder cannot.
for ($index = 0; $index -lt $semanticEntries.Count; $index++) {
    $held = $semanticEntries[$index]
    if (($held.Action -cne 'hold_for_delay' -and
            $held.Action -cne 'hold_for_reorder') -or
        $held.HoldState -cne 'released') {
        continue
    }
    $successor = $null
    for ($candidateIndex = $index + 1;
        $candidateIndex -lt $semanticEntries.Count; $candidateIndex++) {
        if ($semanticEntries[$candidateIndex].Direction -ceq $held.Direction) {
            $successor = $semanticEntries[$candidateIndex]
            break
        }
    }
    if ($null -eq $successor) {
        if ($held.Action -ceq 'hold_for_delay') { continue }
        throw 'Released reorder has no same-direction successor.'
    }
    if ($successor.Action -cne 'forward' -or
        $successor.Emissions.Count -ne 1 -or $held.Emissions.Count -ne 1) {
        throw 'Held datagram has no exact same-direction release successor.'
    }
    $delayOrder = [Int64]$held.Emissions[0] -lt [Int64]$successor.Emissions[0]
    if (($held.Action -ceq 'hold_for_delay' -and -not $delayOrder) -or
        ($held.Action -ceq 'hold_for_reorder' -and $delayOrder)) {
        throw 'Held-datagram emission order contradicts the selected action.'
    }
}

$orderedEmissions = @($emissions | Sort-Object)
for ($index = 0; $index -lt $orderedEmissions.Count; $index++) {
    if ($orderedEmissions[$index] -ne $index) {
        throw 'Emitted ordinals are not one contiguous peer-visible sequence.'
    }
}

for ([Int64]$index = 0; $index -lt $emissions.Count; $index++) {
    if (-not $deliveryRecords.ContainsKey($index)) {
        throw 'Delivered stream lacks a contiguous emission record.'
    }
    $record = $deliveryRecords[$index]
    $geometry = $record.Geometry
    if ($geometry.Classification -ceq 'connectionless') {
        $deliveredConnectionless[$record.Direction]++
    } else {
        $deliveredSequenced[$record.Direction]++
        if ($geometry.Fragmented) { $deliveredFragments++ }
        if ($geometry.Reliable) { $deliveredReliable++ }
        if ($record.Direction -ceq 's2c' -and
            $record.RelativeTimestampUs -gt $lastDeliveredSequencedS2cTimestampUs) {
            $lastDeliveredSequencedS2cTimestampUs = $record.RelativeTimestampUs
        }
    }
}

$reconnectTransportPath = Join-Path $root `
    'reconnect-transport-observation.staged.json'
$reconnectOrchestrationPath = Join-Path $root `
    'reconnect-orchestration.staged.json'
$hasReconnectTransport = Test-Path -LiteralPath $reconnectTransportPath -PathType Leaf
$hasReconnectOrchestration = Test-Path -LiteralPath $reconnectOrchestrationPath -PathType Leaf
if ($hasReconnectTransport -ne $hasReconnectOrchestration) {
    throw 'Reconnect staged transport and orchestration documents are atomic.'
}
$reconnect = $hasReconnectTransport
$reconnectTransport = $null
$reconnectOrchestration = $null
$reconnectGenerations = @()
[Int64]$retiredGenerationATailPackets = 0
if ($reconnect) {
    if ($null -eq $requestedBoundary -or
        $null -eq $requestedGenerationBBoundary) {
        throw 'Reconnect walking requires exact independent A and B boundaries.'
    }
    $reconnectTransport = Read-BoundedJson $reconnectTransportPath 65536 `
        'reconnect transport observation'
    $transportKeys = @(
        'schema', 'connection_generation_count', 'generation_distinct',
        'generation_a_tail_emitter_ready_before_shutdown',
        'generation_a_controlled_shutdown', 'generation_a_endpoint_quiet',
        'guard_continuity', 'server_continuity', 'relay_continuity',
        'post_resource_boundary_status', 'candidate_status',
        'candidate_body_consumed', 'candidate_semantic_category_assigned',
        'retired_generation_a_tail_sink',
        'retired_generation_a_server_tail_packet_count',
        'generation_b_sequenced_after_fresh_accept',
        'bounded_transport_complete', 'generations')
    Assert-ExactProperties $reconnectTransport $transportKeys `
        'reconnect transport observation'
    if ([string]$reconnectTransport.schema -cne
            'hlclient.stock-runtime-reconnect-transport-observation.v1' -or
        (Get-StrictInteger $reconnectTransport connection_generation_count 2 2) -ne 2 -or
        $reconnectTransport.generation_distinct -isnot [bool] -or
        -not [bool]$reconnectTransport.generation_distinct -or
        $reconnectTransport.generation_a_tail_emitter_ready_before_shutdown `
            -isnot [bool] -or
        -not [bool]$reconnectTransport.generation_a_tail_emitter_ready_before_shutdown -or
        [string]$reconnectTransport.generation_a_controlled_shutdown -cne
            'observed_by_orchestrator' -or
        $reconnectTransport.generation_a_endpoint_quiet -isnot [bool] -or
        -not [bool]$reconnectTransport.generation_a_endpoint_quiet -or
        [string]$reconnectTransport.guard_continuity -cne
            'observed_by_orchestrator' -or
        [string]$reconnectTransport.server_continuity -cne
            'observed_by_orchestrator' -or
        [string]$reconnectTransport.relay_continuity -cne 'observed' -or
        [string]$reconnectTransport.post_resource_boundary_status -cne
            'evidence_pending' -or
        [string]$reconnectTransport.candidate_status -cne 'evidence_pending' -or
        $reconnectTransport.candidate_body_consumed -isnot [bool] -or
        [bool]$reconnectTransport.candidate_body_consumed -or
        $reconnectTransport.candidate_semantic_category_assigned -isnot [bool] -or
        [bool]$reconnectTransport.candidate_semantic_category_assigned -or
        [string]$reconnectTransport.retired_generation_a_tail_sink -cne
            'routing_only' -or
        $reconnectTransport.generation_b_sequenced_after_fresh_accept -isnot [bool] -or
        -not [bool]$reconnectTransport.generation_b_sequenced_after_fresh_accept -or
        $reconnectTransport.bounded_transport_complete -isnot [bool] -or
        -not [bool]$reconnectTransport.bounded_transport_complete) {
        throw 'Reconnect transport observation has invalid fixed claims.'
    }
    $retiredGenerationATailPackets = Get-StrictInteger $reconnectTransport `
        retired_generation_a_server_tail_packet_count 0 $maximumEntries
    $reconnectGenerations = @($reconnectTransport.generations)
    if ($reconnectGenerations.Count -ne 2) {
        throw 'Reconnect transport observation requires exactly two generations.'
    }

    $generationKeys = @(
        'generation_ordinal', 'endpoint_role_identity',
        'process_role_identity', 'first_observed_ordinal',
        'last_observed_ordinal', 'connectionless_exchange_count',
        'connect_observed', 'accept_observed',
        'first_sequenced_packet_ordinal', 'client_to_server_packet_count',
        'server_to_client_packet_count', 'profile_identity',
        'post_resource_boundary_status', 'candidate_status',
        'candidate_body_consumed', 'candidate_semantic_category_assigned')
    $expectedEndpoints = @('research_client_generation_a',
        'research_client_generation_b')
    $expectedProcesses = @('owned_client_generation_a',
        'owned_client_generation_b')
    for ($generationIndex = 0; $generationIndex -lt 2; $generationIndex++) {
        $generation = $reconnectGenerations[$generationIndex]
        Assert-ExactProperties $generation $generationKeys `
            "reconnect generation $($generationIndex + 1)"
        $first = Get-StrictInteger $generation first_observed_ordinal 0 `
            ($maximumEntries - 1)
        $last = Get-StrictInteger $generation last_observed_ordinal $first `
            ($maximumEntries - 1)
        $firstSequence = Get-StrictInteger $generation `
            first_sequenced_packet_ordinal $first $last
        $expectedC2s = Get-StrictInteger $generation `
            client_to_server_packet_count 1 $maximumEntries
        $expectedS2c = Get-StrictInteger $generation `
            server_to_client_packet_count 1 $maximumEntries
        $expectedConnectionless = Get-StrictInteger $generation `
            connectionless_exchange_count 1 $maximumEntries
        if ((Get-StrictInteger $generation generation_ordinal 1 2) -ne
                ($generationIndex + 1) -or
            [string]$generation.endpoint_role_identity -cne
                $expectedEndpoints[$generationIndex] -or
            [string]$generation.process_role_identity -cne
                $expectedProcesses[$generationIndex] -or
            [string]$generation.profile_identity -cne
                'stock_protocol_48_build_10210_evidence_pending' -or
            $generation.connect_observed -isnot [bool] -or
            -not [bool]$generation.connect_observed -or
            $generation.accept_observed -isnot [bool] -or
            -not [bool]$generation.accept_observed -or
            [string]$generation.post_resource_boundary_status -cne
                'evidence_pending' -or
            [string]$generation.candidate_status -cne 'evidence_pending' -or
            $generation.candidate_body_consumed -isnot [bool] -or
            [bool]$generation.candidate_body_consumed -or
            $generation.candidate_semantic_category_assigned -isnot [bool] -or
            [bool]$generation.candidate_semantic_category_assigned) {
            throw 'Reconnect generation contains invalid fixed claims.'
        }

        [Int64]$actualC2s = 0
        [Int64]$actualS2c = 0
        [Int64]$actualConnectionless = 0
        $connectSeen = $false
        $acceptSeen = $false
        $actualFirstSequence = $null
        foreach ($record in @($observedRecords | Where-Object {
                    $_.ObservedOrdinal -ge $first -and
                    $_.ObservedOrdinal -le $last })) {
            $retiredTail = $generationIndex -eq 1 -and
                $record.Direction -ceq 's2c' -and
                $record.Geometry.Classification -ceq 'sequenced' -and
                $record.ObservedOrdinal -lt $firstSequence
            if ($retiredTail) { continue }
            if ($record.Direction -ceq 'c2s') { $actualC2s++ }
            else { $actualS2c++ }
            if ($record.Geometry.Classification -ceq 'connectionless') {
                $actualConnectionless++
                if ($record.Direction -ceq 'c2s' -and
                    $record.ConnectPrefix) {
                    $connectSeen = $true
                }
                if ($record.Direction -ceq 's2c' -and $connectSeen -and
                    $record.AcceptPrefix) {
                    $acceptSeen = $true
                }
            } elseif ($record.Direction -ceq 's2c' -and $acceptSeen -and
                $null -eq $actualFirstSequence) {
                $actualFirstSequence = [Int64]$record.ObservedOrdinal
            }
        }
        if ($actualC2s -ne $expectedC2s -or $actualS2c -ne $expectedS2c -or
            $actualConnectionless -ne $expectedConnectionless -or
            -not $connectSeen -or -not $acceptSeen -or
            $null -eq $actualFirstSequence -or
            $actualFirstSequence -ne $firstSequence) {
            throw 'Reconnect generation counters/handshake disagree with the journal.'
        }
    }
    $aLast = Get-StrictInteger $reconnectGenerations[0] last_observed_ordinal `
        0 ($maximumEntries - 1)
    $bFirst = Get-StrictInteger $reconnectGenerations[1] first_observed_ordinal `
        0 ($maximumEntries - 1)
    $bFirstSequence = Get-StrictInteger $reconnectGenerations[1] `
        first_sequenced_packet_ordinal 0 ($maximumEntries - 1)
    if ($aLast -ge $bFirst) {
        throw 'Reconnect generation proof ranges overlap.'
    }
    $actualTail = @($observedRecords | Where-Object {
            $_.ObservedOrdinal -gt $aLast -and
            $_.ObservedOrdinal -lt $bFirstSequence -and
            $_.Direction -ceq 's2c' -and
            $_.Geometry.Classification -ceq 'sequenced' }).Count
    if ($actualTail -ne $retiredGenerationATailPackets) {
        throw 'Retired generation-A server tail count disagrees with journal routing.'
    }

    $reconnectOrchestration = Read-BoundedJson $reconnectOrchestrationPath `
        65536 'reconnect orchestration attestation'
    $orchestrationKeys = @(
        'schema', 'connection_generation_count', 'generation_distinct',
        'generation_a_process_role_identity',
        'generation_b_process_role_identity',
        'generation_a_endpoint_role_identity',
        'generation_b_endpoint_role_identity',
        'generation_a_tail_emitter_ready_before_shutdown',
        'generation_a_controlled_shutdown', 'generation_a_endpoint_quiet',
        'generation_b_fresh_owned_process',
        'generation_b_fresh_connection_lifecycle', 'guard_continuity',
        'server_continuity', 'relay_continuity', 'cleanup_status',
        'restoration_status', 'post_resource_boundary_status',
        'candidate_status', 'candidate_body_consumed',
        'candidate_semantic_category_assigned', 'publication_status')
    Assert-ExactProperties $reconnectOrchestration $orchestrationKeys `
        'reconnect orchestration attestation'
    if ([string]$reconnectOrchestration.schema -cne
            'hlclient.stock-runtime-reconnect-orchestration.v1' -or
        (Get-StrictInteger $reconnectOrchestration `
            connection_generation_count 2 2) -ne 2 -or
        $reconnectOrchestration.generation_distinct -isnot [bool] -or
        -not [bool]$reconnectOrchestration.generation_distinct -or
        [string]$reconnectOrchestration.generation_a_process_role_identity -cne
            'owned_client_generation_a' -or
        [string]$reconnectOrchestration.generation_b_process_role_identity -cne
            'owned_client_generation_b' -or
        [string]$reconnectOrchestration.generation_a_endpoint_role_identity -cne
            'research_client_generation_a' -or
        [string]$reconnectOrchestration.generation_b_endpoint_role_identity -cne
            'research_client_generation_b' -or
        $reconnectOrchestration.generation_a_tail_emitter_ready_before_shutdown `
            -isnot [bool] -or
        -not [bool]$reconnectOrchestration.generation_a_tail_emitter_ready_before_shutdown -or
        $reconnectOrchestration.generation_a_controlled_shutdown -isnot [bool] -or
        -not [bool]$reconnectOrchestration.generation_a_controlled_shutdown -or
        $reconnectOrchestration.generation_a_endpoint_quiet -isnot [bool] -or
        -not [bool]$reconnectOrchestration.generation_a_endpoint_quiet -or
        $reconnectOrchestration.generation_b_fresh_owned_process -isnot [bool] -or
        -not [bool]$reconnectOrchestration.generation_b_fresh_owned_process -or
        [string]$reconnectOrchestration.generation_b_fresh_connection_lifecycle -cne
            'observed_by_relay' -or
        $reconnectOrchestration.guard_continuity -isnot [bool] -or
        -not [bool]$reconnectOrchestration.guard_continuity -or
        $reconnectOrchestration.server_continuity -isnot [bool] -or
        -not [bool]$reconnectOrchestration.server_continuity -or
        $reconnectOrchestration.relay_continuity -isnot [bool] -or
        -not [bool]$reconnectOrchestration.relay_continuity -or
        [string]$reconnectOrchestration.cleanup_status -cne 'exact' -or
        [string]$reconnectOrchestration.restoration_status -cne 'wrapper_pending' -or
        [string]$reconnectOrchestration.post_resource_boundary_status -cne
            'evidence_pending' -or
        [string]$reconnectOrchestration.candidate_status -cne 'evidence_pending' -or
        $reconnectOrchestration.candidate_body_consumed -isnot [bool] -or
        [bool]$reconnectOrchestration.candidate_body_consumed -or
        $reconnectOrchestration.candidate_semantic_category_assigned -isnot [bool] -or
        [bool]$reconnectOrchestration.candidate_semantic_category_assigned -or
        [string]$reconnectOrchestration.publication_status -cne 'staged') {
        throw 'Reconnect orchestration attestation has invalid fixed claims.'
    }
} elseif ($generationBBoundaryArgumentCount -ne 0) {
    throw 'Generation-B boundary is forbidden for a single-generation run.'
}

$finalManifestState = 'absent-prepublication'
$boundaryState = 'not-published'
$boundary = $requestedBoundary
$finalManifestPath = Join-Path $root 'research-run-metadata.json'
if (Test-Path -LiteralPath $finalManifestPath -PathType Leaf) {
    $manifest = Read-BoundedJson $finalManifestPath 131072 `
        'research run manifest'
    if ([string]$manifest.schema -cne 'hlclient.stock-runtime-research-run.v1' -or
        [string]$manifest.run_id -cne $runId) {
        throw 'Research run manifest identity is invalid.'
    }
    if ($manifest.accepted_evidence_run -isnot [bool] -or
        $manifest.accepted_transport_run -isnot [bool]) {
        throw 'Research run manifest acceptance fields are not Boolean.'
    }
    if ($manifest.accepted_evidence_run) {
        $expectedCandidateRecurrence = if ($reconnect) { 2 } else { 1 }
        $expectedCandidateStability = if ($reconnect) {
            'stable_observation'
        } else { 'single_observation' }
        $reconnectClaimNames = @(
            'connection_generation_count', 'exact_boundary_count',
            'runtime_candidate_count', 'generation_distinct',
            'candidate_conflict')
        if ($reconnect) {
            if ((Get-StrictInteger $manifest connection_generation_count 2 2) -ne 2 -or
                (Get-StrictInteger $manifest exact_boundary_count 2 2) -ne 2 -or
                (Get-StrictInteger $manifest runtime_candidate_count 2 2) -ne 2 -or
                $manifest.generation_distinct -isnot [bool] -or
                -not [bool]$manifest.generation_distinct -or
                $manifest.candidate_conflict -isnot [bool] -or
                [bool]$manifest.candidate_conflict) {
                throw 'Accepted reconnect manifest lacks exact A/B claims.'
            }
        } else {
            foreach ($claimName in $reconnectClaimNames) {
                if ($null -ne $manifest.PSObject.Properties[$claimName]) {
                    throw 'Single-generation manifest contains reconnect-only claims.'
                }
            }
        }
        if ((Get-StrictInteger $manifest raw_datagram_count 1 $maximumEntries) -ne
                $rawFiles.Count -or
            (Get-StrictInteger $manifest journal_entry_count 1 $maximumEntries) -ne
                $lines.Count -or
            (Get-StrictInteger $manifest delivered_sequenced_c2s_count 0 ($maximumEntries * 2)) -ne
                $deliveredSequenced.c2s -or
            (Get-StrictInteger $manifest delivered_sequenced_s2c_count 0 ($maximumEntries * 2)) -ne
                $deliveredSequenced.s2c -or
            (Get-StrictInteger $manifest delivered_fragment_datagram_count 0 ($maximumEntries * 2)) -ne
                $deliveredFragments -or
            (Get-StrictInteger $manifest last_observed_transport_timestamp_us `
                0 300000000) -ne $lastTimestamp -or
            (Get-StrictInteger $manifest last_delivered_sequenced_s2c_timestamp_us `
                0 300000000) -ne $lastDeliveredSequencedS2cTimestampUs -or
            -not $manifest.accepted_transport_run -or -not $transportComplete -or
            $wrongSourceCount -ne 0 -or
            [string]$manifest.restoration_status -cne 'exact' -or
            [string]$manifest.external_drift_status -cne 'none' -or
            [string]$manifest.post_resource_boundary_status -cne 'observed' -or
            $manifest.post_resource_reassembled -isnot [bool] -or
            $manifest.post_resource_decompressed -isnot [bool] -or
            $manifest.post_resource_boundary_byte_aligned -isnot [bool] -or
            [string]$manifest.first_observation_status -cne 'observed' -or
            [string]$manifest.first_candidate -cnotmatch
                '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
            [int]([string]$manifest.first_candidate -replace '^bit-prefix:', '') -gt 255 -or
            (Get-StrictInteger $manifest first_candidate_recurrence `
                $expectedCandidateRecurrence $expectedCandidateRecurrence) -ne
                $expectedCandidateRecurrence -or
            [string]$manifest.transport_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$manifest.replay_structural_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$manifest.candidate_stability -cne
                $expectedCandidateStability) {
            throw 'Accepted research run manifest disagrees with walker geometry/gates.'
        }
        $manifestBoundary = New-BoundaryMetadata `
            (Get-StrictInteger $manifest post_resource_replay_payload_ordinal 0 65536) `
            (Get-StrictInteger $manifest post_resource_corpus_observed_ordinal 0 65535) `
            (Get-StrictInteger $manifest post_resource_delivery_ordinal 0 131071) `
            (Get-StrictInteger $manifest post_resource_byte_offset 0 1048576) `
            (Get-StrictInteger $manifest post_resource_bit_offset 0 7) `
            (Get-StrictInteger $manifest post_resource_source_sequence 0 1073741823) `
            (Get-StrictInteger $manifest post_resource_source_payload_bytes 1 1048576) `
            (Get-StrictInteger $manifest post_resource_source_payload_bits 8 8388608) `
            (Get-StrictInteger $manifest post_resource_next_unconsumed_bits 1 8388608) `
            ([bool]$manifest.post_resource_reassembled) `
            ([bool]$manifest.post_resource_decompressed) `
            (Get-StrictInteger $manifest first_candidate_bit_width 1 8) `
            ([string]$manifest.first_candidate)
        if ($null -ne $boundary -and
            ($boundary | ConvertTo-Json -Compress) -cne
                ($manifestBoundary | ConvertTo-Json -Compress)) {
            throw 'Caller boundary metadata disagrees with the final manifest.'
        }
        $boundary = $manifestBoundary
        $finalManifestState = 'accepted'
    } else {
        if ([string]::IsNullOrWhiteSpace([string]$manifest.failure_category) -or
            [string]$manifest.failure_category -ceq 'none') {
            throw 'Incomplete/rejected run lacks a typed failure category.'
        }
        $finalManifestState = 'not-accepted'
    }
    $boundaryState = [string]$manifest.post_resource_boundary_status
}

$boundaryOutput = [ordered]@{
    PayloadOrdinal = 'unavailable'; ObservedOrdinal = 'unavailable'
    DeliveryOrdinal = 'unavailable'; ByteOffset = 'unavailable'
    BitOffset = 'unavailable'; SourceSequence = 'unavailable'
    SourcePayloadBytes = 'unavailable'; SourcePayloadBits = 'unavailable'
    NextUnconsumedBits = 'unavailable'; Reassembled = 'unavailable'
    Decompressed = 'unavailable'; ByteAligned = 'unavailable'
    CandidateBitWidth = 'unavailable'; FirstCandidate = 'unavailable'
    ReplayStructuralHash = 'unavailable'
}
if ($null -ne $boundary) {
    if ($boundary.PayloadOrdinal -lt 0 -or $boundary.PayloadOrdinal -gt 65536 -or
        $boundary.ObservedOrdinal -lt 0 -or $boundary.ObservedOrdinal -ge $lines.Count -or
        $boundary.DeliveryOrdinal -lt 0 -or
        -not $deliveryRecords.ContainsKey($boundary.DeliveryOrdinal) -or
        $boundary.ByteOffset -lt 0 -or $boundary.BitOffset -lt 0 -or
        $boundary.BitOffset -gt 7 -or $boundary.SourcePayloadBytes -lt 1 -or
        $boundary.SourcePayloadBits -ne ($boundary.SourcePayloadBytes * 8) -or
        (($boundary.ByteOffset * 8) + $boundary.BitOffset +
            $boundary.NextUnconsumedBits) -ne $boundary.SourcePayloadBits -or
        $boundary.NextUnconsumedBits -lt 1 -or
        $boundary.CandidateBitWidth -lt 1 -or $boundary.CandidateBitWidth -gt 8 -or
        $boundary.CandidateBitWidth -gt $boundary.NextUnconsumedBits -or
        [string]$boundary.FirstCandidate -cnotmatch
            '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
        [int]([string]$boundary.FirstCandidate -replace '^bit-prefix:', '') -gt 255 -or
        ((-not ([string]$boundary.FirstCandidate).StartsWith('bit-prefix:')) -and
            ($boundary.BitOffset -ne 0 -or $boundary.CandidateBitWidth -ne 8)) -or
        (([string]$boundary.FirstCandidate).StartsWith('bit-prefix:') -and
            ($boundary.BitOffset -eq 0 -or
                [int]([string]$boundary.FirstCandidate).Substring(11) -ge
                    [Math]::Pow(2, $boundary.CandidateBitWidth)))) {
        throw 'Exact post-resource boundary has inconsistent bounded geometry.'
    }
    $sourceRecord = $deliveryRecords[$boundary.DeliveryOrdinal]
    if ($sourceRecord.Direction -cne 's2c' -or
        $sourceRecord.ObservedOrdinal -ne $boundary.ObservedOrdinal) {
        throw 'Exact post-resource boundary does not reference its delivered S2C owner.'
    }
    $sourceGeometry = $sourceRecord.Geometry
    if ($sourceGeometry.Classification -cne 'sequenced' -or
        $sourceGeometry.Sequence -ne $boundary.SourceSequence -or
        ($boundary.Reassembled -and -not $sourceGeometry.Fragmented) -or
        (-not $boundary.Reassembled -and -not $boundary.Decompressed -and
            -not $sourceGeometry.Fragmented -and
            $sourceGeometry.PayloadByteCount -ne $boundary.SourcePayloadBytes)) {
        throw 'Exact post-resource boundary source provenance disagrees with the independently decoded netchan datagram.'
    }
    $canonical = 'hlclient.stock-runtime-replay-structure.v1' +
        "|run=$runId" +
        "|replay-payload=$($boundary.PayloadOrdinal)" +
        "|observed=$($boundary.ObservedOrdinal)" +
        "|delivery=$($boundary.DeliveryOrdinal)" +
        "|byte=$($boundary.ByteOffset)" +
        "|bit=$($boundary.BitOffset)" +
        "|source-sequence=$($boundary.SourceSequence)" +
        "|source-bytes=$($boundary.SourcePayloadBytes)" +
        "|source-bits=$($boundary.SourcePayloadBits)" +
        "|remaining-bits=$($boundary.NextUnconsumedBits)" +
        ('|reassembled=' + $(if ($boundary.Reassembled) { 'true' } else { 'false' })) +
        ('|decompressed=' + $(if ($boundary.Decompressed) { 'true' } else { 'false' })) +
        "|candidate-width=$($boundary.CandidateBitWidth)" +
        "|candidate=$($boundary.FirstCandidate)"
    $boundaryOutput.PayloadOrdinal = [string]$boundary.PayloadOrdinal
    $boundaryOutput.ObservedOrdinal = [string]$boundary.ObservedOrdinal
    $boundaryOutput.DeliveryOrdinal = [string]$boundary.DeliveryOrdinal
    $boundaryOutput.ByteOffset = [string]$boundary.ByteOffset
    $boundaryOutput.BitOffset = [string]$boundary.BitOffset
    $boundaryOutput.SourceSequence = [string]$boundary.SourceSequence
    $boundaryOutput.SourcePayloadBytes = [string]$boundary.SourcePayloadBytes
    $boundaryOutput.SourcePayloadBits = [string]$boundary.SourcePayloadBits
    $boundaryOutput.NextUnconsumedBits = [string]$boundary.NextUnconsumedBits
    $boundaryOutput.Reassembled = $(if ($boundary.Reassembled) { 'true' } else { 'false' })
    $boundaryOutput.Decompressed = $(if ($boundary.Decompressed) { 'true' } else { 'false' })
    $boundaryOutput.ByteAligned = $(if ($boundary.BitOffset -eq 0) { 'true' } else { 'false' })
    $boundaryOutput.CandidateBitWidth = [string]$boundary.CandidateBitWidth
    $boundaryOutput.FirstCandidate = [string]$boundary.FirstCandidate
    $boundaryOutput.ReplayStructuralHash = Get-StringSha256 $canonical
    $boundaryState = 'observed'
    if ($finalManifestState -ceq 'accepted' -and
        ((-not $reconnect -and
            $boundaryOutput.ReplayStructuralHash -cne
                [string]$manifest.replay_structural_sha256) -or
         ([bool]$manifest.post_resource_boundary_byte_aligned) -ne
            ($boundary.BitOffset -eq 0))) {
        throw 'Independent replay structural hash disagrees with the final manifest.'
    }
}

$generationBBoundaryOutput = $null
$reconnectReplayStructuralHash = $null
$generationAReplayStructuralHash = $boundaryOutput.ReplayStructuralHash
if ($reconnect) {
    $boundaryB = $requestedGenerationBBoundary
    if ($boundaryB.PayloadOrdinal -lt 0 -or $boundaryB.PayloadOrdinal -gt 65536 -or
        $boundaryB.ObservedOrdinal -lt 0 -or
        $boundaryB.ObservedOrdinal -ge $lines.Count -or
        $boundaryB.DeliveryOrdinal -lt 0 -or
        -not $deliveryRecords.ContainsKey($boundaryB.DeliveryOrdinal) -or
        $boundaryB.ByteOffset -lt 0 -or $boundaryB.BitOffset -lt 0 -or
        $boundaryB.BitOffset -gt 7 -or $boundaryB.SourcePayloadBytes -lt 1 -or
        $boundaryB.SourcePayloadBits -ne ($boundaryB.SourcePayloadBytes * 8) -or
        (($boundaryB.ByteOffset * 8) + $boundaryB.BitOffset +
            $boundaryB.NextUnconsumedBits) -ne $boundaryB.SourcePayloadBits -or
        $boundaryB.NextUnconsumedBits -lt 1 -or
        $boundaryB.CandidateBitWidth -lt 1 -or
        $boundaryB.CandidateBitWidth -gt 8 -or
        $boundaryB.CandidateBitWidth -gt $boundaryB.NextUnconsumedBits -or
        [string]$boundaryB.FirstCandidate -cnotmatch
            '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
        [int]([string]$boundaryB.FirstCandidate -replace '^bit-prefix:', '') -gt 255 -or
        ((-not ([string]$boundaryB.FirstCandidate).StartsWith('bit-prefix:')) -and
            ($boundaryB.BitOffset -ne 0 -or $boundaryB.CandidateBitWidth -ne 8)) -or
        (([string]$boundaryB.FirstCandidate).StartsWith('bit-prefix:') -and
            ($boundaryB.BitOffset -eq 0 -or
                [int]([string]$boundaryB.FirstCandidate).Substring(11) -ge
                    [Math]::Pow(2, $boundaryB.CandidateBitWidth)))) {
        throw 'Generation-B exact post-resource boundary is inconsistent.'
    }
    $sourceRecordB = $deliveryRecords[$boundaryB.DeliveryOrdinal]
    if ($sourceRecordB.Direction -cne 's2c' -or
        $sourceRecordB.ObservedOrdinal -ne $boundaryB.ObservedOrdinal) {
        throw 'Generation-B boundary does not reference its delivered S2C owner.'
    }
    $sourceGeometryB = $sourceRecordB.Geometry
    if ($sourceGeometryB.Classification -cne 'sequenced' -or
        $sourceGeometryB.Sequence -ne $boundaryB.SourceSequence -or
        ($boundaryB.Reassembled -and -not $sourceGeometryB.Fragmented) -or
        (-not $boundaryB.Reassembled -and -not $boundaryB.Decompressed -and
            -not $sourceGeometryB.Fragmented -and
            $sourceGeometryB.PayloadByteCount -ne $boundaryB.SourcePayloadBytes)) {
        throw 'Generation-B boundary provenance disagrees with netchan geometry.'
    }
    $aFirst = Get-StrictInteger $reconnectGenerations[0] first_observed_ordinal `
        0 ($maximumEntries - 1)
    $aLast = Get-StrictInteger $reconnectGenerations[0] last_observed_ordinal `
        $aFirst ($maximumEntries - 1)
    $bFirst = Get-StrictInteger $reconnectGenerations[1] first_observed_ordinal `
        0 ($maximumEntries - 1)
    $bLast = Get-StrictInteger $reconnectGenerations[1] last_observed_ordinal `
        $bFirst ($maximumEntries - 1)
    if ($boundary.ObservedOrdinal -lt $aFirst -or
        $boundary.ObservedOrdinal -gt $aLast -or
        $boundaryB.ObservedOrdinal -lt $bFirst -or
        $boundaryB.ObservedOrdinal -gt $bLast) {
        throw 'Reconnect boundaries do not belong to their generation ranges.'
    }
    if ($boundary.FirstCandidate -cne $boundaryB.FirstCandidate -or
        $boundary.CandidateBitWidth -ne $boundaryB.CandidateBitWidth -or
        $boundary.BitOffset -ne $boundaryB.BitOffset) {
        throw 'Reconnect generations expose conflicting neutral candidates.'
    }
    $canonicalB = 'hlclient.stock-runtime-replay-structure.v1' +
        "|run=$runId" +
        "|replay-payload=$($boundaryB.PayloadOrdinal)" +
        "|observed=$($boundaryB.ObservedOrdinal)" +
        "|delivery=$($boundaryB.DeliveryOrdinal)" +
        "|byte=$($boundaryB.ByteOffset)" +
        "|bit=$($boundaryB.BitOffset)" +
        "|source-sequence=$($boundaryB.SourceSequence)" +
        "|source-bytes=$($boundaryB.SourcePayloadBytes)" +
        "|source-bits=$($boundaryB.SourcePayloadBits)" +
        "|remaining-bits=$($boundaryB.NextUnconsumedBits)" +
        ('|reassembled=' + $(if ($boundaryB.Reassembled) { 'true' } else { 'false' })) +
        ('|decompressed=' + $(if ($boundaryB.Decompressed) { 'true' } else { 'false' })) +
        "|candidate-width=$($boundaryB.CandidateBitWidth)" +
        "|candidate=$($boundaryB.FirstCandidate)"
    $generationBBoundaryOutput = [ordered]@{
        PayloadOrdinal = [string]$boundaryB.PayloadOrdinal
        ObservedOrdinal = [string]$boundaryB.ObservedOrdinal
        DeliveryOrdinal = [string]$boundaryB.DeliveryOrdinal
        ByteOffset = [string]$boundaryB.ByteOffset
        BitOffset = [string]$boundaryB.BitOffset
        SourceSequence = [string]$boundaryB.SourceSequence
        SourcePayloadBytes = [string]$boundaryB.SourcePayloadBytes
        SourcePayloadBits = [string]$boundaryB.SourcePayloadBits
        NextUnconsumedBits = [string]$boundaryB.NextUnconsumedBits
        Reassembled = $(if ($boundaryB.Reassembled) { 'true' } else { 'false' })
        Decompressed = $(if ($boundaryB.Decompressed) { 'true' } else { 'false' })
        ByteAligned = $(if ($boundaryB.BitOffset -eq 0) { 'true' } else { 'false' })
        CandidateBitWidth = [string]$boundaryB.CandidateBitWidth
        FirstCandidate = [string]$boundaryB.FirstCandidate
        ReplayStructuralHash = Get-StringSha256 $canonicalB
    }
    $reconnectCanonical =
        'hlclient.stock-runtime-reconnect-replay-structure.v1' +
        "|run=$runId" +
        "|generation-1=$($boundaryOutput.ReplayStructuralHash)" +
        "|observed-first-1=$aFirst|observed-last-1=$aLast" +
        "|generation-2=$($generationBBoundaryOutput.ReplayStructuralHash)" +
        "|observed-first-2=$bFirst|observed-last-2=$bLast"
    $reconnectReplayStructuralHash = Get-StringSha256 $reconnectCanonical
    if ($finalManifestState -ceq 'accepted' -and
        [string]$manifest.replay_structural_sha256 -cne
            $reconnectReplayStructuralHash) {
        throw 'Reconnect A/B structural hash disagrees with final run manifest.'
    }
    $boundaryOutput.ReplayStructuralHash = $reconnectReplayStructuralHash

    $finalReconnectPath = Join-Path $root 'reconnect-observation.json'
    $hasFinalReconnect = Test-Path -LiteralPath $finalReconnectPath -PathType Leaf
    if (($finalManifestState -ceq 'accepted') -ne $hasFinalReconnect) {
        throw 'Final reconnect observation exists only with an accepted manifest.'
    }
    if ($hasFinalReconnect) {
        $finalReconnect = Read-BoundedJson $finalReconnectPath 65536 `
            'final reconnect observation'
        $finalRootKeys = @(
            'schema', 'connection_generation_count', 'exact_boundary_count',
            'runtime_candidate_count', 'generation_distinct',
            'candidate_conflict', 'guard_continuity', 'server_continuity',
            'relay_continuity', 'cleanup_exact', 'restoration_exact',
            'candidate_body_consumed',
            'candidate_semantic_category_assigned',
            'retired_generation_a_tail_sink',
            'retired_generation_a_server_tail_packet_count',
            'generation_b_sequenced_after_fresh_accept', 'generations')
        Assert-ExactProperties $finalReconnect $finalRootKeys `
            'final reconnect observation'
        if ([string]$finalReconnect.schema -cne
                'hlclient.stock-runtime-reconnect-observation.v1' -or
            (Get-StrictInteger $finalReconnect connection_generation_count 2 2) -ne 2 -or
            (Get-StrictInteger $finalReconnect exact_boundary_count 2 2) -ne 2 -or
            (Get-StrictInteger $finalReconnect runtime_candidate_count 2 2) -ne 2 -or
            $finalReconnect.generation_distinct -isnot [bool] -or
            -not [bool]$finalReconnect.generation_distinct -or
            $finalReconnect.candidate_conflict -isnot [bool] -or
            [bool]$finalReconnect.candidate_conflict -or
            $finalReconnect.guard_continuity -isnot [bool] -or
            -not [bool]$finalReconnect.guard_continuity -or
            $finalReconnect.server_continuity -isnot [bool] -or
            -not [bool]$finalReconnect.server_continuity -or
            $finalReconnect.relay_continuity -isnot [bool] -or
            -not [bool]$finalReconnect.relay_continuity -or
            $finalReconnect.cleanup_exact -isnot [bool] -or
            -not [bool]$finalReconnect.cleanup_exact -or
            $finalReconnect.restoration_exact -isnot [bool] -or
            -not [bool]$finalReconnect.restoration_exact -or
            $finalReconnect.candidate_body_consumed -isnot [bool] -or
            [bool]$finalReconnect.candidate_body_consumed -or
            $finalReconnect.candidate_semantic_category_assigned -isnot [bool] -or
            [bool]$finalReconnect.candidate_semantic_category_assigned -or
            [string]$finalReconnect.retired_generation_a_tail_sink -cne
                'routing_only' -or
            (Get-StrictInteger $finalReconnect `
                retired_generation_a_server_tail_packet_count 0 $maximumEntries) -ne
                $retiredGenerationATailPackets -or
            $finalReconnect.generation_b_sequenced_after_fresh_accept -isnot [bool] -or
            -not [bool]$finalReconnect.generation_b_sequenced_after_fresh_accept -or
            @($finalReconnect.generations).Count -ne 2) {
            throw 'Final reconnect observation has invalid aggregate claims.'
        }
        $finalGenerationKeys = @(
            'generation_ordinal', 'profile_identity',
            'owned_client_process_role_identity',
            'learned_client_endpoint_role_identity',
            'fresh_owned_client_process', 'learned_client_endpoint_observed',
            'learned_client_endpoint_distinct_from_previous',
            'first_observed_ordinal', 'last_observed_ordinal',
            'connectionless_exchange_count', 'connect_observed',
            'accept_observed', 'first_sequenced_packet_ordinal',
            'client_to_server_packet_count', 'server_to_client_packet_count',
            'controlled_client_shutdown_observed',
            'retired_client_endpoint_quiet', 'exact_post_resource_boundary',
            'candidate_observation')
        $finalBoundaryKeys = @(
            'observed', 'replay_payload_ordinal', 'corpus_observed_ordinal',
            'delivery_ordinal', 'byte_offset', 'bit_offset',
            'source_payload_byte_count', 'source_payload_bit_count',
            'next_unconsumed_bit_count')
        $finalCandidateKeys = @(
            'observed', 'candidate_bit_width', 'numeric_candidate',
            'bounded_bit_prefix', 'byte_aligned', 'body_consumed',
            'semantic_category_assigned')
        $expectedBoundaries = @($boundary, $boundaryB)
        for ($index = 0; $index -lt 2; $index++) {
            $finalGeneration = @($finalReconnect.generations)[$index]
            $stagedGeneration = $reconnectGenerations[$index]
            $expectedBoundary = $expectedBoundaries[$index]
            Assert-ExactProperties $finalGeneration $finalGenerationKeys `
                "final reconnect generation $($index + 1)"
            Assert-ExactProperties $finalGeneration.exact_post_resource_boundary `
                $finalBoundaryKeys 'final reconnect boundary'
            Assert-ExactProperties $finalGeneration.candidate_observation `
                $finalCandidateKeys 'final reconnect candidate'
            if ((Get-StrictInteger $finalGeneration generation_ordinal 1 2) -ne
                    ($index + 1) -or
                [string]$finalGeneration.profile_identity -cne
                    'stock_protocol_48_build_10210_evidence_pending' -or
                [string]$finalGeneration.owned_client_process_role_identity -cne
                    $expectedProcesses[$index] -or
                [string]$finalGeneration.learned_client_endpoint_role_identity -cne
                    $expectedEndpoints[$index] -or
                (Get-StrictInteger $finalGeneration first_observed_ordinal 0 `
                    ($maximumEntries - 1)) -ne
                    (Get-StrictInteger $stagedGeneration first_observed_ordinal 0 `
                        ($maximumEntries - 1)) -or
                (Get-StrictInteger $finalGeneration last_observed_ordinal 0 `
                    ($maximumEntries - 1)) -ne
                    (Get-StrictInteger $stagedGeneration last_observed_ordinal 0 `
                        ($maximumEntries - 1)) -or
                $finalGeneration.fresh_owned_client_process -isnot [bool] -or
                -not [bool]$finalGeneration.fresh_owned_client_process -or
                $finalGeneration.learned_client_endpoint_observed -isnot [bool] -or
                -not [bool]$finalGeneration.learned_client_endpoint_observed -or
                [bool]$finalGeneration.learned_client_endpoint_distinct_from_previous -ne
                    ($index -ne 0) -or
                [bool]$finalGeneration.controlled_client_shutdown_observed -ne
                    ($index -eq 0) -or
                [bool]$finalGeneration.retired_client_endpoint_quiet -ne
                    ($index -eq 0) -or
                -not [bool]$finalGeneration.exact_post_resource_boundary.observed -or
                (Get-StrictInteger $finalGeneration.exact_post_resource_boundary `
                    replay_payload_ordinal 0 65536) -ne
                    $expectedBoundary.PayloadOrdinal -or
                (Get-StrictInteger $finalGeneration.exact_post_resource_boundary `
                    corpus_observed_ordinal 0 ($maximumEntries - 1)) -ne
                    $expectedBoundary.ObservedOrdinal -or
                (Get-StrictInteger $finalGeneration.exact_post_resource_boundary `
                    delivery_ordinal 0 ($maximumEntries * 2 - 1)) -ne
                    $expectedBoundary.DeliveryOrdinal -or
                (Get-StrictInteger $finalGeneration.exact_post_resource_boundary `
                    byte_offset 0 1048576) -ne $expectedBoundary.ByteOffset -or
                (Get-StrictInteger $finalGeneration.exact_post_resource_boundary `
                    bit_offset 0 7) -ne $expectedBoundary.BitOffset -or
                (Get-StrictInteger $finalGeneration.exact_post_resource_boundary `
                    source_payload_byte_count 1 1048576) -ne
                    $expectedBoundary.SourcePayloadBytes -or
                (Get-StrictInteger $finalGeneration.exact_post_resource_boundary `
                    source_payload_bit_count 8 8388608) -ne
                    $expectedBoundary.SourcePayloadBits -or
                (Get-StrictInteger $finalGeneration.exact_post_resource_boundary `
                    next_unconsumed_bit_count 1 8388608) -ne
                    $expectedBoundary.NextUnconsumedBits -or
                -not [bool]$finalGeneration.candidate_observation.observed -or
                (Get-StrictInteger $finalGeneration.candidate_observation `
                    candidate_bit_width 1 8) -ne
                    $expectedBoundary.CandidateBitWidth -or
                [bool]$finalGeneration.candidate_observation.byte_aligned -ne
                    ($expectedBoundary.BitOffset -eq 0) -or
                [bool]$finalGeneration.candidate_observation.body_consumed -or
                [bool]$finalGeneration.candidate_observation.semantic_category_assigned) {
                throw 'Final reconnect generation disagrees with staged/replayed facts.'
            }
            $expectedValue = [int]([string]$expectedBoundary.FirstCandidate -replace
                '^bit-prefix:', '')
            $isPrefix = ([string]$expectedBoundary.FirstCandidate).StartsWith(
                'bit-prefix:')
            if (($isPrefix -and
                    ($null -ne $finalGeneration.candidate_observation.numeric_candidate -or
                     [int]$finalGeneration.candidate_observation.bounded_bit_prefix -ne
                        $expectedValue)) -or
                (-not $isPrefix -and
                    ($null -ne $finalGeneration.candidate_observation.bounded_bit_prefix -or
                     [int]$finalGeneration.candidate_observation.numeric_candidate -ne
                        $expectedValue))) {
                throw 'Final reconnect candidate representation disagrees with replay.'
            }
        }
    }
}

Write-Output "[stock-runtime-walk] run-id=$runId"
Write-Output "[stock-runtime-walk] journal-entries=$($lines.Count)"
Write-Output "[stock-runtime-walk] raw-datagrams=$($rawFiles.Count)"
Write-Output "[stock-runtime-walk] raw-bytes=$totalRawBytes"
Write-Output "[stock-runtime-walk] observed-c2s=$($directionCounts.c2s)"
Write-Output "[stock-runtime-walk] observed-s2c=$($directionCounts.s2c)"
Write-Output "[stock-runtime-walk] delivered-c2s=$deliveredC2s"
Write-Output "[stock-runtime-walk] delivered-s2c=$deliveredS2c"
Write-Output "[stock-runtime-walk] observed-connectionless-c2s=$($observedConnectionless.c2s)"
Write-Output "[stock-runtime-walk] observed-connectionless-s2c=$($observedConnectionless.s2c)"
Write-Output "[stock-runtime-walk] observed-sequenced-c2s=$($observedSequenced.c2s)"
Write-Output "[stock-runtime-walk] observed-sequenced-s2c=$($observedSequenced.s2c)"
Write-Output "[stock-runtime-walk] observed-fragment-datagrams=$observedFragments"
Write-Output "[stock-runtime-walk] observed-reliable-datagrams=$observedReliable"
Write-Output "[stock-runtime-walk] delivered-connectionless-c2s=$($deliveredConnectionless.c2s)"
Write-Output "[stock-runtime-walk] delivered-connectionless-s2c=$($deliveredConnectionless.s2c)"
Write-Output "[stock-runtime-walk] delivered-sequenced-c2s=$($deliveredSequenced.c2s)"
Write-Output "[stock-runtime-walk] delivered-sequenced-s2c=$($deliveredSequenced.s2c)"
Write-Output "[stock-runtime-walk] delivered-fragment-datagrams=$deliveredFragments"
Write-Output "[stock-runtime-walk] delivered-reliable-datagrams=$deliveredReliable"
Write-Output "[stock-runtime-walk] wrong-source-datagrams=$wrongSourceCount"
Write-Output "[stock-runtime-walk] emitted-datagrams=$($emissions.Count)"
Write-Output "[stock-runtime-walk] last-observed-timestamp-us=$lastTimestamp"
Write-Output "[stock-runtime-walk] last-delivered-sequenced-s2c-timestamp-us=$lastDeliveredSequencedS2cTimestampUs"
Write-Output ("[stock-runtime-walk] transport-complete=" +
    $(if ($transportComplete) { 'true' } else { 'false' }))
Write-Output '[stock-runtime-walk] observed-delivered-policy=distinct'
Write-Output "[stock-runtime-walk] final-manifest=$finalManifestState"
Write-Output "[stock-runtime-walk] post-resource-boundary=$boundaryState"
Write-Output "[stock-runtime-walk] boundary-payload-ordinal=$($boundaryOutput.PayloadOrdinal)"
Write-Output "[stock-runtime-walk] boundary-observed-ordinal=$($boundaryOutput.ObservedOrdinal)"
Write-Output "[stock-runtime-walk] boundary-delivery-ordinal=$($boundaryOutput.DeliveryOrdinal)"
Write-Output "[stock-runtime-walk] boundary-byte-offset=$($boundaryOutput.ByteOffset)"
Write-Output "[stock-runtime-walk] boundary-bit-offset=$($boundaryOutput.BitOffset)"
Write-Output "[stock-runtime-walk] boundary-source-sequence=$($boundaryOutput.SourceSequence)"
Write-Output "[stock-runtime-walk] boundary-source-payload-bytes=$($boundaryOutput.SourcePayloadBytes)"
Write-Output "[stock-runtime-walk] boundary-source-payload-bits=$($boundaryOutput.SourcePayloadBits)"
Write-Output "[stock-runtime-walk] boundary-next-unconsumed-bits=$($boundaryOutput.NextUnconsumedBits)"
Write-Output "[stock-runtime-walk] boundary-reassembled=$($boundaryOutput.Reassembled)"
Write-Output "[stock-runtime-walk] boundary-decompressed=$($boundaryOutput.Decompressed)"
Write-Output "[stock-runtime-walk] boundary-byte-aligned=$($boundaryOutput.ByteAligned)"
Write-Output "[stock-runtime-walk] candidate-bit-width=$($boundaryOutput.CandidateBitWidth)"
Write-Output "[stock-runtime-walk] first-candidate=$($boundaryOutput.FirstCandidate)"
Write-Output "[stock-runtime-walk] replay-structural-hash=$($boundaryOutput.ReplayStructuralHash)"
if ($reconnect) {
    Write-Output '[stock-runtime-walk] connection-generation-count=2'
    Write-Output '[stock-runtime-walk] exact-boundary-count=2'
    Write-Output '[stock-runtime-walk] runtime-candidate-count=2'
    Write-Output '[stock-runtime-walk] generation-distinct=true'
    Write-Output '[stock-runtime-walk] candidate-conflict=false'
    Write-Output '[stock-runtime-walk] candidate-recurrence=2'
    Write-Output '[stock-runtime-walk] candidate-stability=stable_observation'
    Write-Output '[stock-runtime-walk] retired-generation-a-tail-sink=routing_only'
    Write-Output "[stock-runtime-walk] retired-generation-a-server-tail-packets=$retiredGenerationATailPackets"
    Write-Output '[stock-runtime-walk] generation-b-sequenced-after-fresh-accept=true'
    $generationOutputs = @($boundaryOutput, $generationBBoundaryOutput)
    for ($index = 0; $index -lt 2; $index++) {
        $label = if ($index -eq 0) { 'a' } else { 'b' }
        $generation = $reconnectGenerations[$index]
        $output = $generationOutputs[$index]
        $generationHash = if ($index -eq 0) {
            $generationAReplayStructuralHash
        } else { $generationBBoundaryOutput.ReplayStructuralHash }
        Write-Output "[stock-runtime-walk] generation-$label-first-observed-ordinal=$($generation.first_observed_ordinal)"
        Write-Output "[stock-runtime-walk] generation-$label-last-observed-ordinal=$($generation.last_observed_ordinal)"
        Write-Output "[stock-runtime-walk] generation-$label-connectionless-exchanges=$($generation.connectionless_exchange_count)"
        Write-Output "[stock-runtime-walk] generation-$label-first-sequenced-packet-ordinal=$($generation.first_sequenced_packet_ordinal)"
        Write-Output "[stock-runtime-walk] generation-$label-client-to-server-packets=$($generation.client_to_server_packet_count)"
        Write-Output "[stock-runtime-walk] generation-$label-server-to-client-packets=$($generation.server_to_client_packet_count)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-payload-ordinal=$($output.PayloadOrdinal)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-observed-ordinal=$($output.ObservedOrdinal)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-delivery-ordinal=$($output.DeliveryOrdinal)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-byte-offset=$($output.ByteOffset)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-bit-offset=$($output.BitOffset)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-source-sequence=$($output.SourceSequence)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-source-payload-bytes=$($output.SourcePayloadBytes)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-source-payload-bits=$($output.SourcePayloadBits)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-next-unconsumed-bits=$($output.NextUnconsumedBits)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-reassembled=$($output.Reassembled)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-decompressed=$($output.Decompressed)"
        Write-Output "[stock-runtime-walk] generation-$label-boundary-byte-aligned=$($output.ByteAligned)"
        Write-Output "[stock-runtime-walk] generation-$label-candidate-bit-width=$($output.CandidateBitWidth)"
        Write-Output "[stock-runtime-walk] generation-$label-first-candidate=$($output.FirstCandidate)"
        Write-Output "[stock-runtime-walk] generation-$label-candidate-body-consumed=false"
        Write-Output "[stock-runtime-walk] generation-$label-candidate-semantic-category-assigned=false"
        Write-Output "[stock-runtime-walk] generation-$label-replay-structural-hash=$generationHash"
    }
}
Write-Output '[stock-runtime-walk] result=success'
