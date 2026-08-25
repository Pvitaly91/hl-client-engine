[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ToolPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Basedir,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Game,

    [Parameter(Mandatory = $true)]
    [AllowEmptyCollection()]
    [string[]]$Models,

    [Parameter(Mandatory = $true)]
    [AllowEmptyCollection()]
    [string[]]$Sprites
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Win32_ProcessStartTrace and Win32_Process queries require WMI privileges that
# are not available to every local developer account. Toolhelp snapshots expose
# the same parent/child relationship without requiring elevation.
if ($null -eq ('HlClientProcessSnapshot' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class HlClientProcessSnapshot
{
    private const uint TH32CS_SNAPPROCESS = 0x00000002;
    private const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x00001000;
    private const int AF_INET = 2;
    private const int AF_INET6 = 23;
    private const uint ERROR_INSUFFICIENT_BUFFER = 122;
    private const uint ERROR_NO_DATA = 232;
    private const int MaximumNetworkTableBytes = 64 * 1024 * 1024;
    private static readonly IntPtr InvalidHandleValue = new IntPtr(-1);

    public sealed class Entry
    {
        public int ParentProcessId { get; private set; }
        public string ExecutableName { get; private set; }
        public string FullPath { get; private set; }

        public Entry(int parentProcessId, string executableName,
            string fullPath)
        {
            ParentProcessId = parentProcessId;
            ExecutableName = executableName ?? string.Empty;
            FullPath = fullPath ?? string.Empty;
        }
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    private struct ProcessEntry32
    {
        public uint Size;
        public uint Usage;
        public uint ProcessId;
        public IntPtr DefaultHeapId;
        public uint ModuleId;
        public uint ThreadCount;
        public uint ParentProcessId;
        public int BasePriority;
        public uint Flags;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string ExecutableName;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateToolhelp32Snapshot(uint flags,
        uint processId);

    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern bool Process32First(IntPtr snapshot,
        ref ProcessEntry32 entry);

    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern bool Process32Next(IntPtr snapshot,
        ref ProcessEntry32 entry);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint desiredAccess,
        bool inheritHandle, uint processId);

    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    private static extern bool QueryFullProcessImageName(IntPtr process,
        uint flags, StringBuilder path, ref uint size);

    [DllImport("iphlpapi.dll", SetLastError = true)]
    private static extern uint GetExtendedTcpTable(IntPtr table,
        ref int size, bool order, int family, int tableClass, uint reserved);

    [DllImport("iphlpapi.dll", SetLastError = true)]
    private static extern uint GetExtendedUdpTable(IntPtr table,
        ref int size, bool order, int family, int tableClass, uint reserved);

    private static string TryGetFullPath(uint processId)
    {
        IntPtr process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
            false, processId);
        if (process == IntPtr.Zero)
        {
            return string.Empty;
        }
        try
        {
            const int MaximumPathCharacters = 32768;
            var path = new StringBuilder(MaximumPathCharacters);
            uint size = MaximumPathCharacters;
            return QueryFullProcessImageName(process, 0, path, ref size)
                ? path.ToString()
                : string.Empty;
        }
        finally
        {
            CloseHandle(process);
        }
    }

    public static Dictionary<int, Entry> Capture()
    {
        IntPtr snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == InvalidHandleValue)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        try
        {
            var result = new Dictionary<int, Entry>();
            var entry = new ProcessEntry32();
            entry.Size = (uint)Marshal.SizeOf(typeof(ProcessEntry32));
            if (!Process32First(snapshot, ref entry))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            do
            {
                // FullPath is consumed only when an observed conhost is
                // classified. Querying every process made the snapshot slow
                // enough for a short-lived console host to disappear first.
                result[(int)entry.ProcessId] = new Entry(
                    (int)entry.ParentProcessId,
                    entry.ExecutableName,
                    string.Equals(entry.ExecutableName, "conhost.exe",
                        StringComparison.OrdinalIgnoreCase)
                        ? TryGetFullPath(entry.ProcessId)
                        : string.Empty);
                entry.Size = (uint)Marshal.SizeOf(typeof(ProcessEntry32));
            }
            while (Process32Next(snapshot, ref entry));

            const int ErrorNoMoreFiles = 18;
            int error = Marshal.GetLastWin32Error();
            if (error != 0 && error != ErrorNoMoreFiles)
            {
                throw new Win32Exception(error);
            }
            return result;
        }
        finally
        {
            CloseHandle(snapshot);
        }
    }

    private static void AddTableProcessIds(HashSet<int> result,
        bool tcp, int family, int rowSize, int processIdOffset)
    {
        int size = 0;
        uint status = tcp
            ? GetExtendedTcpTable(IntPtr.Zero, ref size, false, family, 5, 0)
            : GetExtendedUdpTable(IntPtr.Zero, ref size, false, family, 1, 0);
        if (status == ERROR_NO_DATA)
        {
            return;
        }
        if (status != ERROR_INSUFFICIENT_BUFFER && status != 0)
        {
            throw new Win32Exception((int)status);
        }
        if (size < sizeof(int) || size > MaximumNetworkTableBytes)
        {
            throw new InvalidOperationException("Invalid network table size.");
        }

        IntPtr table = Marshal.AllocHGlobal(size);
        try
        {
            status = tcp
                ? GetExtendedTcpTable(table, ref size, false, family, 5, 0)
                : GetExtendedUdpTable(table, ref size, false, family, 1, 0);
            if (status == ERROR_NO_DATA)
            {
                return;
            }
            if (status != 0)
            {
                throw new Win32Exception((int)status);
            }
            int count = Marshal.ReadInt32(table);
            long required = sizeof(int) + (long)count * rowSize;
            if (count < 0 || required > size)
            {
                throw new InvalidOperationException("Invalid network table.");
            }
            for (int index = 0; index < count; ++index)
            {
                IntPtr row = IntPtr.Add(table, sizeof(int) + index * rowSize);
                result.Add(Marshal.ReadInt32(row, processIdOffset));
            }
        }
        finally
        {
            Marshal.FreeHGlobal(table);
        }
    }

    public static HashSet<int> CaptureNetworkEndpointProcessIds()
    {
        var result = new HashSet<int>();
        AddTableProcessIds(result, true, AF_INET, 24, 20);
        AddTableProcessIds(result, true, AF_INET6, 56, 52);
        AddTableProcessIds(result, false, AF_INET, 12, 8);
        AddTableProcessIds(result, false, AF_INET6, 28, 24);
        return result;
    }
}
'@
}
$MaximumEntries = 200000
$MaximumDepth = 64
$MaximumSelectedBytes = 67108864
$MaximumToolBytes = 67108864
$MaximumCheckerOutputBytes = 65536
$CheckerTimeoutMilliseconds = 30000
$ProcessStartObservationGraceMilliseconds = 250
$ProcessObservationIntervalMilliseconds = 5
$ForbiddenNetworkImports = @(
    'ws2_32.dll',
    'wsock32.dll',
    'winhttp.dll',
    'wininet.dll',
    'urlmon.dll',
    'dnsapi.dll',
    'mswsock.dll'
)
$ForbiddenNetworkApiEvidence = @(
    'WSAStartup',
    'WSASocketA',
    'WSASocketW',
    'socket',
    'connect',
    'getaddrinfo',
    'GetAddrInfoW',
    'DnsQuery_A',
    'DnsQuery_W',
    'WinHttpOpen',
    'InternetOpenA',
    'InternetOpenW',
    'URLDownloadToFileA',
    'URLDownloadToFileW'
)
$ForbiddenProcessSpawnEvidence = @(
    'CreateProcessA',
    'CreateProcessW',
    'CreateProcessAsUserA',
    'CreateProcessAsUserW',
    'ShellExecuteA',
    'ShellExecuteW',
    'ShellExecuteExA',
    'ShellExecuteExW',
    'WinExec',
    '_popen',
    '_wpopen',
    'NtCreateUserProcess',
    'RtlCreateUserProcess'
)

function Throw-VerificationFailure {
    throw [System.InvalidOperationException]::new(
        'Local GoldSrc visual-asset verification failed.')
}

function Get-Sha256Text {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Text
    )

    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        $digest = $algorithm.ComputeHash($bytes)
        return ([System.BitConverter]::ToString($digest)).Replace(
            '-', '').ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

function Assert-SafeVirtualName {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Kind
    )

    if ($Name.Length -eq 0 -or $Name.Length -gt 1024 -or
        $Name -notmatch '^[\x20-\x7E]+$' -or $Name.Contains('\') -or
        $Name.Contains(':') -or $Name.StartsWith('/') -or
        $Name.EndsWith('/') -or $Name.Contains('//')) {
        Throw-VerificationFailure
    }
    $segments = $Name.Split('/')
    if ($segments.Count -gt 64) {
        Throw-VerificationFailure
    }
    foreach ($segment in $segments) {
        if ([string]::IsNullOrEmpty($segment) -or $segment -eq '.' -or
            $segment -eq '..' -or $segment.Length -gt 255 -or
            $segment.EndsWith('.') -or
            $segment.EndsWith(' ')) {
            Throw-VerificationFailure
        }
        $stem = ($segment -split '\.', 2)[0]
        if ($stem -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
            Throw-VerificationFailure
        }
    }
    if (($Kind -eq 'model' -and
            ($segments[0] -ine 'models' -or $segments[-1] -notmatch '(?i)\.mdl$')) -or
        ($Kind -eq 'sprite' -and
            ($segments[0] -ine 'sprites' -or $segments[-1] -notmatch '(?i)\.spr$'))) {
        Throw-VerificationFailure
    }
}

function Resolve-SafeLeaf {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$VirtualName
    )

    $rootItem = Get-Item -LiteralPath $Root -Force
    if (-not $rootItem.PSIsContainer -or
        (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        Throw-VerificationFailure
    }
    $current = $rootItem
    $segments = $VirtualName.Split('/')
    for ($index = 0; $index -lt $segments.Count; ++$index) {
        $candidate = Join-Path $current.FullName $segments[$index]
        $item = Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
        if ($null -eq $item) {
            return $null
        }
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Throw-VerificationFailure
        }
        $isFinal = $index -eq $segments.Count - 1
        if (($isFinal -and $item.PSIsContainer) -or
            (-not $isFinal -and -not $item.PSIsContainer)) {
            Throw-VerificationFailure
        }
        $current = $item
    }

    $rootPrefix = $rootItem.FullName.TrimEnd([char[]]@('\', '/')) +
        [IO.Path]::DirectorySeparatorChar
    if (-not $current.FullName.StartsWith(
            $rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Throw-VerificationFailure
    }
    return $current
}

function Get-RootInventory {
    param([Parameter(Mandatory = $true)][string[]]$Roots)

    $rows = [System.Collections.Generic.List[string]]::new()
    for ($rootIndex = 0; $rootIndex -lt $Roots.Count; ++$rootIndex) {
        $root = Get-Item -LiteralPath $Roots[$rootIndex] -Force
        if (-not $root.PSIsContainer -or
            (($root.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            Throw-VerificationFailure
        }
        $prefix = $root.FullName.TrimEnd([char[]]@('\', '/'))
        $pending = [System.Collections.Generic.Queue[object]]::new()
        $pending.Enqueue([pscustomobject]@{ Item = $root; Depth = 0 })
        while ($pending.Count -gt 0) {
            $current = $pending.Dequeue()
            if ($current.Depth -gt $MaximumDepth) {
                Throw-VerificationFailure
            }
            foreach ($entry in @(Get-ChildItem -LiteralPath $current.Item.FullName -Force)) {
                if ($rows.Count -ge $MaximumEntries -or
                    (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
                    Throw-VerificationFailure
                }
                $relative = $entry.FullName.Substring($prefix.Length).TrimStart(
                    [char[]]@('\', '/')).Replace('\', '/')
                $length = if ($entry.PSIsContainer) { -1L } else { [int64]$entry.Length }
                $rows.Add(('{0}|{1}|{2}|{3}|{4}' -f @(
                    $rootIndex, $relative, [int]$entry.PSIsContainer,
                    $length, $entry.LastWriteTimeUtc.Ticks)))
                if ($entry.PSIsContainer) {
                    $pending.Enqueue([pscustomobject]@{
                        Item = $entry
                        Depth = $current.Depth + 1
                    })
                }
            }
        }
    }
    $rows.Sort([System.StringComparer]::Ordinal)
    return Get-Sha256Text -Text ($rows.ToArray() -join "`n")
}

function Get-SelectedFileSnapshot {
    param(
        [Parameter(Mandatory = $true)][string[]]$Paths,
        [Parameter(Mandatory = $true)][string[]]$Roots
    )

    $rows = [System.Collections.Generic.List[string]]::new()
    foreach ($path in ($Paths | Sort-Object -Unique)) {
        $item = Get-Item -LiteralPath $path -Force
        if ($item.PSIsContainer -or $item.Length -gt $MaximumSelectedBytes -or
            (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            Throw-VerificationFailure
        }
        $stableKey = $null
        for ($rootIndex = 0; $rootIndex -lt $Roots.Count; ++$rootIndex) {
            $root = (Get-Item -LiteralPath $Roots[$rootIndex] -Force).FullName
            $prefix = $root.TrimEnd([char[]]@('\', '/')) +
                [IO.Path]::DirectorySeparatorChar
            if ($item.FullName.StartsWith(
                    $prefix, [StringComparison]::OrdinalIgnoreCase)) {
                $relative = $item.FullName.Substring($prefix.Length).Replace(
                    '\', '/')
                $stableKey = '{0}|{1}' -f $rootIndex, $relative
                break
            }
        }
        if ($null -eq $stableKey) {
            Throw-VerificationFailure
        }
        $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        $rows.Add(('{0}|{1}|{2}|{3}' -f @(
            $stableKey, $hash.ToLowerInvariant(), [int64]$item.Length,
            $item.LastWriteTimeUtc.Ticks)))
    }
    $rows.Sort([System.StringComparer]::Ordinal)
    return Get-Sha256Text -Text ($rows.ToArray() -join "`n")
}

function Resolve-SelectedAsset {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Kind,
        [Parameter(Mandatory = $true)][string[]]$Roots
    )

    for ($index = 0; $index -lt $Roots.Count; ++$index) {
        $candidate = Resolve-SafeLeaf -Root $Roots[$index] -VirtualName $Name
        if ($null -ne $candidate) {
            $files = [System.Collections.Generic.List[string]]::new()
            $files.Add($candidate.FullName)
            if ($Kind -eq 'model') {
                $slash = $Name.LastIndexOf('/')
                $directory = if ($slash -ge 0) {
                    $Name.Substring(0, $slash + 1)
                }
                else {
                    ''
                }
                $fileName = if ($slash -ge 0) {
                    $Name.Substring($slash + 1)
                }
                else {
                    $Name
                }
                $stem = $fileName.Substring(0, $fileName.Length - 4)
                $textureName = $directory + $stem + 'T.mdl'
                $texture = Resolve-SafeLeaf -Root $Roots[$index] `
                    -VirtualName $textureName
                if ($null -ne $texture) {
                    $files.Add($texture.FullName)
                }
                for ($group = 1; $group -le 15; ++$group) {
                    $groupName = $directory + $stem +
                        $group.ToString('00') + '.mdl'
                    $groupFile = Resolve-SafeLeaf -Root $Roots[$index] `
                        -VirtualName $groupName
                    if ($null -ne $groupFile) {
                        $files.Add($groupFile.FullName)
                    }
                }
            }
            return [pscustomobject]@{
                Name = $Name
                Kind = $Kind
                Files = $files.ToArray()
            }
        }
    }
    return $null
}

function Assert-OfflineCheckerExecutable {
    param([Parameter(Mandatory = $true)][IO.FileInfo]$Tool)

    if ($Tool.PSIsContainer -or $Tool.Extension -ine '.exe' -or
        $Tool.BaseName -cne 'hlclient_goldsrc_asset_check' -or
        $Tool.Length -le 0 -or $Tool.Length -gt $MaximumToolBytes -or
        (($Tool.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        Throw-VerificationFailure
    }
    $bytes = [IO.File]::ReadAllBytes($Tool.FullName)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        Throw-VerificationFailure
    }
    $peOffset = [BitConverter]::ToUInt32($bytes, 0x3c)
    if ($peOffset -gt $bytes.Length - 4 -or
        $bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        Throw-VerificationFailure
    }

    # This purpose-built checker has no network dependency. Scan the bounded
    # PE image as well as observing its live process below, so a substituted
    # executable cannot make an unconditional `network=false` claim merely by
    # printing checker-shaped output.
    $imageText = [Text.Encoding]::ASCII.GetString($bytes)
    foreach ($library in $ForbiddenNetworkImports) {
        if ($imageText.IndexOf(
                $library, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            Throw-VerificationFailure
        }
    }
    foreach ($api in $ForbiddenNetworkApiEvidence) {
        if ($imageText.IndexOf(
                $api, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            Throw-VerificationFailure
        }
    }
    foreach ($api in $ForbiddenProcessSpawnEvidence) {
        if ($imageText.IndexOf(
                $api, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            Throw-VerificationFailure
        }
    }
}

function Test-TrustedWindowsConsoleHost {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Name,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$FullPath
    )

    if (-not $Name.Equals('conhost.exe',
            [StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }
    $systemRoot = [IO.Path]::GetFullPath($env:SystemRoot).TrimEnd(
        [char[]]@('\', '/'))
    foreach ($directory in @('System32', 'SysWOW64')) {
        $expected = [IO.Path]::GetFullPath(
            (Join-Path (Join-Path $systemRoot $directory) 'conhost.exe'))
        if ($FullPath.Equals($expected,
                [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Update-ObservedProcessTree {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.HashSet[int]]$ExistingProcessIds,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.HashSet[int]]$ObservedProcessIds,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.HashSet[int]]$DescendantProcessIds,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.HashSet[int]]$InfrastructureProcessIds,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.Dictionary[int, int]]$ParentByProcessId,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.Dictionary[int, string]]$NameByProcessId,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.Dictionary[int, string]]$PathByProcessId
    )

    foreach ($entry in [HlClientProcessSnapshot]::Capture().GetEnumerator()) {
        # Parent process IDs are retained after a parent exits and its PID may
        # later be reused. Ignore processes already present before the checker
        # starts so those stale IDs cannot be misclassified as descendants.
        if (-not $ExistingProcessIds.Contains($entry.Key) -or
            $ObservedProcessIds.Contains($entry.Key)) {
            $ParentByProcessId[$entry.Key] = $entry.Value.ParentProcessId
            $NameByProcessId[$entry.Key] = $entry.Value.ExecutableName
            $PathByProcessId[$entry.Key] = $entry.Value.FullPath
        }
    }

    $changed = $true
    while ($changed) {
        $changed = $false
        foreach ($entry in $ParentByProcessId.GetEnumerator()) {
            if (-not $ObservedProcessIds.Contains($entry.Key) -and
                $ObservedProcessIds.Contains($entry.Value)) {
                [void]$ObservedProcessIds.Add($entry.Key)
                if (Test-TrustedWindowsConsoleHost `
                        -Name $NameByProcessId[$entry.Key] `
                        -FullPath $PathByProcessId[$entry.Key]) {
                    [void]$InfrastructureProcessIds.Add($entry.Key)
                }
                else {
                    [void]$DescendantProcessIds.Add($entry.Key)
                }
                $changed = $true
            }
        }
    }
}

function Test-ObservedProcessTreeNetworkEndpoint {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.HashSet[int]]$ObservedProcessIds
    )

    $endpointProcessIds =
        [HlClientProcessSnapshot]::CaptureNetworkEndpointProcessIds()
    foreach ($processId in $ObservedProcessIds) {
        if ($endpointProcessIds.Contains($processId)) {
            return $true
        }
    }
    return $false
}

function Stop-ObservedProcessTree {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.HashSet[int]]$DescendantProcessIds
    )

    foreach ($processId in $DescendantProcessIds) {
        Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
    }
}

function ConvertTo-WindowsCommandLineArgument {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value)

    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') {
        return $Value
    }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            ++$backslashes
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * (2 * $backslashes + 1)))
            [void]$builder.Append('"')
        }
        else {
            [void]$builder.Append(('\' * $backslashes))
            [void]$builder.Append($character)
        }
        $backslashes = 0
    }
    [void]$builder.Append(('\' * (2 * $backslashes)))
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-AssetChecker {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$GameDirectory,
        [Parameter(Mandatory = $true)][pscustomobject]$Asset
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.Arguments = (@(
        '--basedir', $Root,
        '--game', $GameDirectory,
        '--asset', $Asset.Name,
        '--kind', $Asset.Kind) | ForEach-Object {
            ConvertTo-WindowsCommandLineArgument -Value ([string]$_)
        }) -join ' '

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $started = $false
    $checkerAccepted = $false
    $observedProcessIds =
        [System.Collections.Generic.HashSet[int]]::new()
    $descendantProcessIds =
        [System.Collections.Generic.HashSet[int]]::new()
    $infrastructureProcessIds =
        [System.Collections.Generic.HashSet[int]]::new()
    $parentByProcessId =
        [System.Collections.Generic.Dictionary[int, int]]::new()
    $nameByProcessId =
        [System.Collections.Generic.Dictionary[int, string]]::new()
    $pathByProcessId =
        [System.Collections.Generic.Dictionary[int, string]]::new()
    $existingProcessIds =
        [System.Collections.Generic.HashSet[int]]::new()
    foreach ($existingProcessId in
            [HlClientProcessSnapshot]::Capture().Keys) {
        [void]$existingProcessIds.Add($existingProcessId)
    }
    try {
        if (-not $process.Start()) {
            Throw-VerificationFailure
        }
        $started = $true
        [void]$observedProcessIds.Add($process.Id)

        $output = [Text.StringBuilder]::new()
        $errorOutput = [Text.StringBuilder]::new()
        $outputBuffer = New-Object 'char[]' 4096
        $errorBuffer = New-Object 'char[]' 4096
        $outputRead = $process.StandardOutput.ReadAsync(
            $outputBuffer, 0, $outputBuffer.Length)
        $errorRead = $process.StandardError.ReadAsync(
            $errorBuffer, 0, $errorBuffer.Length)
        $outputComplete = $false
        $errorComplete = $false
        $capturedOutputBytes = 0
        $networkObserved = $false
        $processSpawnObserved = $false
        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        $exitObservedAt = $null
        while ($true) {
            Update-ObservedProcessTree `
                -ExistingProcessIds $existingProcessIds `
                -ObservedProcessIds $observedProcessIds `
                -DescendantProcessIds $descendantProcessIds `
                -InfrastructureProcessIds $infrastructureProcessIds `
                -ParentByProcessId $parentByProcessId `
                -NameByProcessId $nameByProcessId `
                -PathByProcessId $pathByProcessId
            if ($descendantProcessIds.Count -ne 0) {
                $processSpawnObserved = $true
            }
            if (Test-ObservedProcessTreeNetworkEndpoint `
                    -ObservedProcessIds $observedProcessIds) {
                $networkObserved = $true
            }

            if (-not $outputComplete -and $outputRead.IsCompleted) {
                $read = $outputRead.GetAwaiter().GetResult()
                if ($read -eq 0) {
                    $outputComplete = $true
                }
                else {
                    $chunk = [string]::new($outputBuffer, 0, $read)
                    $capturedOutputBytes += [Text.Encoding]::UTF8.GetByteCount($chunk)
                    if ($capturedOutputBytes -le $MaximumCheckerOutputBytes) {
                        [void]$output.Append($chunk)
                        $outputRead = $process.StandardOutput.ReadAsync(
                            $outputBuffer, 0, $outputBuffer.Length)
                    }
                }
            }
            if (-not $errorComplete -and $errorRead.IsCompleted) {
                $read = $errorRead.GetAwaiter().GetResult()
                if ($read -eq 0) {
                    $errorComplete = $true
                }
                else {
                    $chunk = [string]::new($errorBuffer, 0, $read)
                    $capturedOutputBytes += [Text.Encoding]::UTF8.GetByteCount($chunk)
                    if ($capturedOutputBytes -le $MaximumCheckerOutputBytes) {
                        [void]$errorOutput.Append($chunk)
                        $errorRead = $process.StandardError.ReadAsync(
                            $errorBuffer, 0, $errorBuffer.Length)
                    }
                }
            }

            if ($capturedOutputBytes -gt $MaximumCheckerOutputBytes -or
                $networkObserved -or $processSpawnObserved -or
                (-not $process.HasExited -and
                    $stopwatch.ElapsedMilliseconds -ge
                        $CheckerTimeoutMilliseconds)) {
                Throw-VerificationFailure
            }
            if ($process.HasExited -and $null -eq $exitObservedAt) {
                $exitObservedAt = $stopwatch.ElapsedMilliseconds
            }
            if ($process.HasExited -and $outputComplete -and $errorComplete -and
                $stopwatch.ElapsedMilliseconds - $exitObservedAt -ge
                    $ProcessStartObservationGraceMilliseconds) {
                break
            }
            Start-Sleep -Milliseconds $ProcessObservationIntervalMilliseconds
        }
        $process.WaitForExit()
        if ($networkObserved -or $process.ExitCode -ne 0 -or
            $processSpawnObserved -or
            $capturedOutputBytes -gt $MaximumCheckerOutputBytes -or
            $output.Length -eq 0 -or $errorOutput.Length -ne 0) {
            Throw-VerificationFailure
        }
        $checkerAccepted = $true
        return $output.ToString().TrimEnd([char[]]@("`r", "`n"))
    }
    finally {
        if ($started -and -not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
        if ($started -and -not $checkerAccepted) {
            # Stop the root first so it cannot create another child between
            # descendant cleanup and root termination, then take a final
            # snapshot to include children that appeared at the boundary.
            Update-ObservedProcessTree `
                -ExistingProcessIds $existingProcessIds `
                -ObservedProcessIds $observedProcessIds `
                -DescendantProcessIds $descendantProcessIds `
                -InfrastructureProcessIds $infrastructureProcessIds `
                -ParentByProcessId $parentByProcessId `
                -NameByProcessId $nameByProcessId `
                -PathByProcessId $pathByProcessId
            Stop-ObservedProcessTree `
                -DescendantProcessIds $descendantProcessIds
        }
        $process.Dispose()
    }
}

try {
    if ($Game.Length -gt 64 -or $Game -notmatch '^[A-Za-z0-9_-]+$') {
        Throw-VerificationFailure
    }
    $tool = Get-Item -LiteralPath $ToolPath -Force
    Assert-OfflineCheckerExecutable -Tool $tool
    $base = [IO.Path]::GetFullPath($Basedir)
    if ($base -notmatch '^[A-Za-z]:[\\/]' -or
        -not (Test-Path -LiteralPath $base -PathType Container)) {
        Throw-VerificationFailure
    }
    $roots = @($Game, 'valve') | Select-Object -Unique | ForEach-Object {
        $root = [IO.Path]::GetFullPath((Join-Path $base $_))
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            Throw-VerificationFailure
        }
        $root
    }

    # Reject every reparse point before selected paths are resolved or hashed.
    # A second inventory below closes the read-only drift check.
    $beforeInventory = Get-RootInventory -Roots $roots

    $requested = [System.Collections.Generic.List[object]]::new()
    foreach ($model in $Models) {
        Assert-SafeVirtualName -Name $model -Kind model
        $requested.Add([pscustomobject]@{ Name = $model; Kind = 'model' })
    }
    foreach ($sprite in $Sprites) {
        Assert-SafeVirtualName -Name $sprite -Kind sprite
        $requested.Add([pscustomobject]@{ Name = $sprite; Kind = 'sprite' })
    }

    $selected = [System.Collections.Generic.List[object]]::new()
    $missing = 0
    foreach ($request in $requested) {
        $asset = Resolve-SelectedAsset -Name $request.Name `
            -Kind $request.Kind -Roots $roots
        if ($null -eq $asset) {
            ++$missing
            continue
        }
        $selected.Add($asset)
    }
    if ($selected.Count -eq 0) {
        Write-Output ('assets-missing=' + $missing)
        Write-Output 'verification=pending'
        exit 0
    }

    $selectedPaths = @($selected | ForEach-Object { $_.Files })
    $beforeFiles = Get-SelectedFileSnapshot -Paths $selectedPaths -Roots $roots
    $ordinal = 0
    foreach ($asset in $selected) {
        ++$ordinal
        $first = Invoke-AssetChecker -Executable $tool.FullName -Root $base `
            -GameDirectory $Game -Asset $asset
        $second = Invoke-AssetChecker -Executable $tool.FullName -Root $base `
            -GameDirectory $Game -Asset $asset
        if ($first -cne $second) {
            Throw-VerificationFailure
        }
        Write-Output ('asset-{0}-category={1}' -f $ordinal, $asset.Kind)
        Write-Output ('asset-{0}-summary-sha256={1}' -f @(
            $ordinal, (Get-Sha256Text -Text $first)))
    }
    if ((Get-RootInventory -Roots $roots) -cne $beforeInventory -or
        (Get-SelectedFileSnapshot -Paths $selectedPaths -Roots $roots) -cne
            $beforeFiles) {
        Throw-VerificationFailure
    }

    Write-Output ('assets-verified=' + $selected.Count)
    Write-Output ('assets-missing=' + $missing)
    Write-Output ('checker-runs=' + (2 * $selected.Count))
    Write-Output 'deterministic-summary=true'
    Write-Output 'created-files=0'
    Write-Output 'deleted-files=0'
    Write-Output 'checker-launched-descendant-processes-observed=false'
    Write-Output 'checker-network-api-evidence=none'
    Write-Output 'checker-process-tree-network-endpoints-observed=false'
    Write-Output 'external-file-drift=none'
}
catch {
    [Console]::Error.WriteLine('visual-asset-verification=failed')
    exit 1
}
