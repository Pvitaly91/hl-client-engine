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
    [ValidateNotNullOrEmpty()]
    [string]$Map
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$MaximumManifestEntries = 200000
$MaximumManifestDepth = 64
$MaximumTargetBytes = 67108864

function Throw-VerificationFailure {
    throw [System.InvalidOperationException]::new('Local BSP verification failed.')
}

function Assert-SafeVirtualInputs {
    if ($Game.Length -gt 64 -or
        $Game -notmatch '^[A-Za-z0-9_-]+$' -or
        $Game -eq '.' -or
        $Game -eq '..') {
        Throw-VerificationFailure
    }

    if ($Map.Length -gt 255 -or
        $Map -notmatch '^[\x21-\x7E]+$' -or
        $Map.Contains('\') -or
        $Map.Contains(':') -or
        $Map.StartsWith('/') -or
        $Map.EndsWith('/') -or
        $Map.Contains('//')) {
        Throw-VerificationFailure
    }

    $segments = $Map.Split('/')
    if ($segments.Count -lt 2 -or
        $segments[0] -cne 'maps' -or
        $segments[-1] -notmatch '(?i)\.bsp$') {
        Throw-VerificationFailure
    }
    foreach ($segment in $segments) {
        if ([string]::IsNullOrEmpty($segment) -or
            $segment -eq '.' -or
            $segment -eq '..' -or
            $segment.Length -gt 255 -or
            $segment.EndsWith('.') -or
            $segment.EndsWith(' ')) {
            Throw-VerificationFailure
        }
        $stem = ($segment -split '\.', 2)[0]
        if ($stem -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
            Throw-VerificationFailure
        }
    }
}

function Get-Sha256Text {
    param([Parameter(Mandatory = $true)][string]$Text)

    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        $digest = $algorithm.ComputeHash($bytes)
        return ([System.BitConverter]::ToString($digest)).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

function Get-FileSystemItemOrNull {
    param([Parameter(Mandatory = $true)][string]$Path)

    try {
        return Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    }
    catch {
        if ($_.Exception -is [System.Management.Automation.ItemNotFoundException] -or
            $_.Exception -is [System.IO.DirectoryNotFoundException] -or
            $_.Exception -is [System.IO.FileNotFoundException]) {
            return $null
        }
        throw
    }
}

function Assert-ReparseFreeDirectoryPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter()][switch]$AllowMissingFinal,
        [Parameter()][switch]$AllowMissingTail
    )

    if ($Path.IndexOf([char]0) -ge 0 -or
        $Path -notmatch '^[A-Za-z]:[\\/]' -or
        $Path -match '^[\\/]{2}' -or
        $Path -match '^[\\/]{2}[?.][\\/]') {
        Throw-VerificationFailure
    }
    $rawRoot = [System.IO.Path]::GetPathRoot($Path)
    foreach ($component in $Path.Substring($rawRoot.Length).Split(
            [char[]]@('\', '/'),
            [System.StringSplitOptions]::RemoveEmptyEntries)) {
        if ($component -eq '.' -or $component -eq '..') {
            Throw-VerificationFailure
        }
    }

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $driveRoot = [System.IO.Path]::GetPathRoot($fullPath)
    $drive = [System.IO.DriveInfo]::new($driveRoot)
    if (-not $drive.IsReady -or
        $drive.DriveType -ne [System.IO.DriveType]::Fixed) {
        Throw-VerificationFailure
    }

    $current = $driveRoot
    $rootItem = Get-FileSystemItemOrNull -Path $current
    if ($null -eq $rootItem -or
        -not $rootItem.PSIsContainer -or
        (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
        Throw-VerificationFailure
    }

    $components = @($fullPath.Substring($driveRoot.Length).Split(
            [char[]]@('\', '/'),
            [System.StringSplitOptions]::RemoveEmptyEntries))
    for ($index = 0; $index -lt $components.Count; ++$index) {
        $current = Join-Path -Path $current -ChildPath $components[$index]
        $item = Get-FileSystemItemOrNull -Path $current
        if ($null -eq $item) {
            if ($AllowMissingTail -or
                ($AllowMissingFinal -and $index + 1 -eq $components.Count)) {
                return $null
            }
            Throw-VerificationFailure
        }
        if (-not $item.PSIsContainer -or
            (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
            Throw-VerificationFailure
        }
    }
    if ($fullPath -eq $driveRoot) {
        return $driveRoot
    }
    return $fullPath.TrimEnd([char[]]@('\', '/'))
}

function Get-RootManifest {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][int]$RootId
    )

    [void](Assert-ReparseFreeDirectoryPath -Path $Root -AllowMissingFinal)
    $rootItem = Get-FileSystemItemOrNull -Path $Root
    if ($null -eq $rootItem) {
        return ('{0}|missing' -f $RootId)
    }

    $rootPrefix = $rootItem.FullName.TrimEnd([char[]]@('\', '/'))
    $rows = [System.Collections.Generic.List[string]]::new()
    $pending = [System.Collections.Generic.Queue[object]]::new()
    $pending.Enqueue($rootItem)
    while ($pending.Count -gt 0) {
        $directory = $pending.Dequeue()
        $directory.Refresh()
        if (($directory.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            Throw-VerificationFailure
        }
        foreach ($entry in $directory.EnumerateFileSystemInfos()) {
            $entry.Refresh()
            if ($rows.Count -ge $MaximumManifestEntries -or
                (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
                Throw-VerificationFailure
            }
            $relativeName = $entry.FullName.Substring($rootPrefix.Length).TrimStart(
                [char[]]@('\', '/'))
            if (($relativeName -split '[\\/]').Count -gt $MaximumManifestDepth) {
                Throw-VerificationFailure
            }
            $isDirectory = $entry -is [System.IO.DirectoryInfo]
            $length = if ($isDirectory) { -1 } else { [int64]$entry.Length }
            $rows.Add(('{0}|{1}|{2}|{3}' -f @(
                $relativeName,
                [int]$entry.Attributes,
                $length,
                $entry.LastWriteTimeUtc.Ticks)))
            if ($isDirectory) {
                $pending.Enqueue($entry)
            }
        }
    }
    $rows.Sort([System.StringComparer]::Ordinal)
    return ('{0}|{1}|{2}' -f @(
        $RootId,
        $rows.Count,
        (Get-Sha256Text -Text ($rows.ToArray() -join "`n"))))
}

function Get-InventorySnapshot {
    param([Parameter(Mandatory = $true)][string[]]$Roots)

    $manifests = [System.Collections.Generic.List[string]]::new()
    for ($index = 0; $index -lt $Roots.Count; ++$index) {
        $manifests.Add((Get-RootManifest -Root $Roots[$index] -RootId ($index + 1)))
    }
    return Get-Sha256Text -Text ($manifests.ToArray() -join "`n")
}

function Open-RetainedTarget {
    param([Parameter(Mandatory = $true)][string]$Target)

    [void](Assert-ReparseFreeDirectoryPath -Path (Split-Path -Path $Target -Parent))
    $item = Get-FileSystemItemOrNull -Path $Target
    if ($null -eq $item -or
        $item.PSIsContainer -or
        (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
        Throw-VerificationFailure
    }

    $stream = $null
    try {
        $stream = [System.IO.File]::Open(
            $Target,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::Read)
        $retainedItem = Get-FileSystemItemOrNull -Path $Target
        if ($null -eq $retainedItem -or
            (($retainedItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) -or
            $stream.Length -gt $MaximumTargetBytes) {
            Throw-VerificationFailure
        }
        return [pscustomobject]@{
            Stream = $stream
            Target = $Target
        }
    }
    catch {
        if ($null -ne $stream) {
            $stream.Dispose()
        }
        throw
    }
}

function Get-TargetSnapshot {
    param([Parameter(Mandatory = $true)][pscustomobject]$RetainedTarget)

    $stream = $RetainedTarget.Stream
    $item = Get-FileSystemItemOrNull -Path $RetainedTarget.Target
    if ($null -eq $item -or
        $item.PSIsContainer -or
        (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) -or
        $stream.Length -gt $MaximumTargetBytes) {
        Throw-VerificationFailure
    }
    $writeTimeBefore = $item.LastWriteTimeUtc
    $stream.Position = 0
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $algorithm.ComputeHash($stream)
    }
    finally {
        $algorithm.Dispose()
    }
    $item.Refresh()
    $writeTimeAfter = $item.LastWriteTimeUtc
    if ($writeTimeBefore.Ticks -ne $writeTimeAfter.Ticks) {
        Throw-VerificationFailure
    }
    return [pscustomobject]@{
        Sha256 = ([System.BitConverter]::ToString($digest)).Replace('-', '').ToLowerInvariant()
        Length = [int64]$stream.Length
        WriteTimeTicks = $writeTimeAfter.Ticks
        WriteTimeUtc = $writeTimeAfter.ToString('o')
    }
}

function Assert-UnchangedState {
    param(
        [Parameter(Mandatory = $true)][string]$BeforeInventory,
        [Parameter(Mandatory = $true)][pscustomobject]$BeforeTarget,
        [Parameter(Mandatory = $true)][string[]]$Roots,
        [Parameter(Mandatory = $true)][pscustomobject]$RetainedTarget
    )

    if ((Get-InventorySnapshot -Roots $Roots) -cne $BeforeInventory) {
        Throw-VerificationFailure
    }
    $afterTarget = Get-TargetSnapshot -RetainedTarget $RetainedTarget
    if ($afterTarget.Sha256 -cne $BeforeTarget.Sha256 -or
        $afterTarget.Length -ne $BeforeTarget.Length -or
        $afterTarget.WriteTimeTicks -ne $BeforeTarget.WriteTimeTicks) {
        Throw-VerificationFailure
    }
}

function Invoke-Checker {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$GameDirectory,
        [Parameter(Mandatory = $true)][string]$VirtualMap
    )

    $output = @(& $Executable --basedir $Root --game $GameDirectory --map $VirtualMap 2>&1 |
        ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) {
        Throw-VerificationFailure
    }
    $summary = [string]::Join("`n", $output)
    if ([string]::IsNullOrWhiteSpace($summary)) {
        Throw-VerificationFailure
    }
    return $summary
}

try {
    Assert-SafeVirtualInputs

    $tool = Get-Item -LiteralPath $ToolPath -Force
    if ($tool.PSIsContainer -or
        $tool.Extension -ine '.exe' -or
        (($tool.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
        Throw-VerificationFailure
    }

    $baseFull = Assert-ReparseFreeDirectoryPath -Path $Basedir
    $rootNames = @($Game, 'valve') | Select-Object -Unique
    $roots = @($rootNames | ForEach-Object {
        [System.IO.Path]::GetFullPath((Join-Path -Path $baseFull -ChildPath $_))
    })
    $relativeMap = $Map.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $target = $null
    foreach ($root in $roots) {
        $candidate = [System.IO.Path]::GetFullPath((Join-Path -Path $root -ChildPath $relativeMap))
        $rootPrefix = $root.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
            [System.IO.Path]::DirectorySeparatorChar
        if (-not $candidate.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            Throw-VerificationFailure
        }
        $candidateParent = Split-Path -Path $candidate -Parent
        $validatedParent = Assert-ReparseFreeDirectoryPath `
            -Path $candidateParent -AllowMissingTail
        if ($null -eq $validatedParent) {
            continue
        }
        $candidateItem = Get-FileSystemItemOrNull -Path $candidate
        if ($null -eq $candidateItem) {
            continue
        }
        if ($candidateItem.PSIsContainer -or
            (($candidateItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
            Throw-VerificationFailure
        }
        $target = $candidate
        break
    }
    if ($null -eq $target) {
        Throw-VerificationFailure
    }

    $retainedTarget = Open-RetainedTarget -Target $target
    try {
        $beforeInventory = Get-InventorySnapshot -Roots $roots
        $beforeTarget = Get-TargetSnapshot -RetainedTarget $retainedTarget

        $firstSummary = Invoke-Checker -Executable $tool.FullName -Root $baseFull `
            -GameDirectory $Game -VirtualMap $Map
        Assert-UnchangedState -BeforeInventory $beforeInventory `
            -BeforeTarget $beforeTarget -Roots $roots `
            -RetainedTarget $retainedTarget

        $secondSummary = Invoke-Checker -Executable $tool.FullName -Root $baseFull `
            -GameDirectory $Game -VirtualMap $Map
        Assert-UnchangedState -BeforeInventory $beforeInventory `
            -BeforeTarget $beforeTarget -Roots $roots `
            -RetainedTarget $retainedTarget

        if ($firstSummary -cne $secondSummary) {
            Throw-VerificationFailure
        }

        Write-Output 'manual-bsp-verification=passed'
        Write-Output 'checker-runs=2'
        Write-Output 'deterministic-summary=true'
        Write-Output ('summary-sha256=' + (Get-Sha256Text -Text $firstSummary))
        Write-Output ('target-content-sha256=' + $beforeTarget.Sha256)
        Write-Output ('target-size=' + $beforeTarget.Length)
        Write-Output ('target-write-time-utc=' + $beforeTarget.WriteTimeUtc)
        Write-Output 'created-files=0'
        Write-Output 'deleted-files=0'
        Write-Output 'external-file-drift=none'
    }
    finally {
        $retainedTarget.Stream.Dispose()
    }
}
catch {
    [Console]::Error.WriteLine('manual-bsp-verification=failed')
    exit 1
}
