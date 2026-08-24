[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ViewerPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Basedir,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Game,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Map,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 1000000)]
    [int]$Frames
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$MaximumInventoryEntries = 200000
$MaximumVerifiedFileBytes = 67108864

function Throw-VerificationFailure {
    throw [System.InvalidOperationException]::new(
        'Local world-render verification failed.')
}

function Assert-SafeVirtualInputs {
    if ($Game.Length -gt 64 -or
        $Game -notmatch '^[A-Za-z0-9_-]+$' -or
        $Game -eq '.' -or $Game -eq '..') {
        Throw-VerificationFailure
    }
    if ($Map.Length -gt 1024 -or
        $Map -notmatch '^[\x21-\x7E]+$' -or
        $Map.Contains('\') -or $Map.Contains(':') -or
        $Map.StartsWith('/') -or $Map.EndsWith('/') -or
        $Map.Contains('//')) {
        Throw-VerificationFailure
    }
    $segments = @($Map.Split('/'))
    if ($segments.Count -lt 2 -or $segments[0] -cne 'maps' -or
        $segments[-1] -notmatch '(?i)\.bsp$') {
        Throw-VerificationFailure
    }
    foreach ($segment in $segments) {
        if ([string]::IsNullOrEmpty($segment) -or
            $segment -eq '.' -or $segment -eq '..' -or
            $segment.Length -gt 255 -or
            $segment.EndsWith('.') -or $segment.EndsWith(' ')) {
            Throw-VerificationFailure
        }
    }
}

function Assert-FixedReparseFreeDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ($Path.IndexOf([char]0) -ge 0 -or
        $Path -notmatch '^[A-Za-z]:[\\/]' -or
        $Path -match '^[\/]{2}') {
        Throw-VerificationFailure
    }
    $full = [System.IO.Path]::GetFullPath($Path)
    $driveRoot = [System.IO.Path]::GetPathRoot($full)
    $drive = [System.IO.DriveInfo]::new($driveRoot)
    if (-not $drive.IsReady -or
        $drive.DriveType -ne [System.IO.DriveType]::Fixed) {
        Throw-VerificationFailure
    }
    $current = $driveRoot
    $components = @($full.Substring($driveRoot.Length).Split(
        [char[]]@('\', '/'),
        [System.StringSplitOptions]::RemoveEmptyEntries))
    foreach ($component in $components) {
        if ($component -eq '.' -or $component -eq '..') {
            Throw-VerificationFailure
        }
        $current = Join-Path -Path $current -ChildPath $component
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (-not $item.PSIsContainer -or
            (($item.Attributes -band
                [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
            Throw-VerificationFailure
        }
    }
    return $full.TrimEnd([char[]]@('\', '/'))
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

function Get-FileSnapshot {
    param([Parameter(Mandatory = $true)][string]$Path)

    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer -or
        (($item.Attributes -band
            [System.IO.FileAttributes]::ReparsePoint) -ne 0) -or
        $item.Length -gt $MaximumVerifiedFileBytes) {
        Throw-VerificationFailure
    }
    $stream = [System.IO.File]::Open(
        $item.FullName,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read)
    try {
        $algorithm = [System.Security.Cryptography.SHA256]::Create()
        try {
            $digest = $algorithm.ComputeHash($stream)
        }
        finally {
            $algorithm.Dispose()
        }
        $item.Refresh()
        return [pscustomobject]@{
            Content = ([System.BitConverter]::ToString($digest)).Replace(
                '-', '').ToLowerInvariant()
            Length = [int64]$item.Length
            WriteTime = $item.LastWriteTimeUtc.Ticks
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Get-InventorySnapshot {
    param([Parameter(Mandatory = $true)][string[]]$Roots)

    $rows = [System.Collections.Generic.List[string]]::new()
    for ($rootIndex = 0; $rootIndex -lt $Roots.Count; ++$rootIndex) {
        $root = $Roots[$rootIndex]
        $rootItem = Get-Item -LiteralPath $root -Force -ErrorAction Stop
        foreach ($entry in Get-ChildItem -LiteralPath $root -Force -Recurse) {
            if ($rows.Count -ge $MaximumInventoryEntries -or
                (($entry.Attributes -band
                    [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
                Throw-VerificationFailure
            }
            $relative = $entry.FullName.Substring($rootItem.FullName.Length).
                TrimStart([char[]]@('\', '/'))
            $length = if ($entry.PSIsContainer) {
                -1
            } else {
                [int64]$entry.Length
            }
            $rows.Add(('{0}|{1}|{2}|{3}|{4}' -f @(
                $rootIndex,
                $relative,
                [int]$entry.Attributes,
                $length,
                $entry.LastWriteTimeUtc.Ticks)))
        }
    }
    $rows.Sort([System.StringComparer]::Ordinal)
    return Get-Sha256Text -Text ($rows.ToArray() -join "`n")
}

function Get-WadSnapshot {
    param([Parameter(Mandatory = $true)][string[]]$Roots)

    $rows = [System.Collections.Generic.List[string]]::new()
    for ($rootIndex = 0; $rootIndex -lt $Roots.Count; ++$rootIndex) {
        foreach ($wad in Get-ChildItem -LiteralPath $Roots[$rootIndex] `
                -Filter '*.wad' -File -Force -Recurse) {
            if ($rows.Count -ge $MaximumInventoryEntries -or
                (($wad.Attributes -band
                    [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
                Throw-VerificationFailure
            }
            $snapshot = Get-FileSnapshot -Path $wad.FullName
            $relative = $wad.FullName.Substring($Roots[$rootIndex].Length).
                TrimStart([char[]]@('\', '/'))
            $rows.Add(('{0}|{1}|{2}|{3}|{4}' -f @(
                $rootIndex,
                $relative,
                $snapshot.Content,
                $snapshot.Length,
                $snapshot.WriteTime)))
        }
    }
    $rows.Sort([System.StringComparer]::Ordinal)
    return [pscustomobject]@{
        Count = $rows.Count
        Digest = Get-Sha256Text -Text ($rows.ToArray() -join "`n")
    }
}

function Get-ExactCounter {
    param(
        [Parameter(Mandatory = $true)][string[]]$Output,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $matchingRows = @($Output | Where-Object {
        $_ -match ('^{0}=([0-9]+)$' -f [regex]::Escape($Name))
    })
    if ($matchingRows.Count -ne 1) {
        Throw-VerificationFailure
    }
    if ($matchingRows[0] -notmatch '=([0-9]+)$') {
        Throw-VerificationFailure
    }
    $counterText = $Matches[1]
    $value = [uint64]0
    if (-not [uint64]::TryParse(
            $counterText,
            [System.Globalization.NumberStyles]::None,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [ref]$value)) {
        Throw-VerificationFailure
    }
    return $value
}

function Invoke-Viewer {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$GameDirectory,
        [Parameter(Mandatory = $true)][string]$VirtualMap,
        [Parameter(Mandatory = $true)][int]$FrameCount
    )

    $savedFrameLimit = [Environment]::GetEnvironmentVariable(
        'HLCLIENT_SMOKE_TEST_FRAMES', 'Process')
    try {
        [Environment]::SetEnvironmentVariable(
            'HLCLIENT_SMOKE_TEST_FRAMES',
            $FrameCount.ToString(
                [System.Globalization.CultureInfo]::InvariantCulture),
            'Process')
        $output = @(& $Executable --basedir $Root --game $GameDirectory `
            --map $VirtualMap --camera static 2>&1 |
            ForEach-Object { [string]$_ })
        if ($LASTEXITCODE -ne 0) {
            Throw-VerificationFailure
        }
        return $output
    }
    finally {
        [Environment]::SetEnvironmentVariable(
            'HLCLIENT_SMOKE_TEST_FRAMES', $savedFrameLimit, 'Process')
    }
}

try {
    Assert-SafeVirtualInputs
    $viewer = Get-Item -LiteralPath $ViewerPath -Force -ErrorAction Stop
    if ($viewer.PSIsContainer -or $viewer.Extension -ine '.exe' -or
        (($viewer.Attributes -band
            [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
        Throw-VerificationFailure
    }

    $base = Assert-FixedReparseFreeDirectory -Path $Basedir
    $rootNames = @($Game, 'valve') | Select-Object -Unique
    $roots = @($rootNames | ForEach-Object {
        Assert-FixedReparseFreeDirectory -Path (
            Join-Path -Path $base -ChildPath $_)
    })

    $relativeMap = $Map.Replace(
        '/', [System.IO.Path]::DirectorySeparatorChar)
    $mapTarget = $null
    foreach ($root in $roots) {
        $candidate = [System.IO.Path]::GetFullPath(
            (Join-Path -Path $root -ChildPath $relativeMap))
        $prefix = $root.TrimEnd(
            [System.IO.Path]::DirectorySeparatorChar) +
            [System.IO.Path]::DirectorySeparatorChar
        if (-not $candidate.StartsWith(
                $prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            Throw-VerificationFailure
        }
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $mapTarget = $candidate
            break
        }
    }
    if ($null -eq $mapTarget) {
        Throw-VerificationFailure
    }

    $inventoryBefore = Get-InventorySnapshot -Roots $roots
    $mapBefore = Get-FileSnapshot -Path $mapTarget
    $wadsBefore = Get-WadSnapshot -Roots $roots

    [string[]]$output = @(Invoke-Viewer -Executable $viewer.FullName `
        -Root $base -GameDirectory $Game -VirtualMap $Map `
        -FrameCount $Frames)

    $uploadCount = Get-ExactCounter -Output $output `
        -Name 'world-upload-count'
    $renderedFrames = Get-ExactCounter -Output $output `
        -Name 'rendered-frames'
    $drawCalls = Get-ExactCounter -Output $output -Name 'draw-calls'
    $triangles = Get-ExactCounter -Output $output -Name 'triangles'
    $networkOperations = Get-ExactCounter -Output $output `
        -Name 'network-operations'
    $writesPerformed = Get-ExactCounter -Output $output `
        -Name 'writes-performed'
    if ($uploadCount -ne 1U -or
        $renderedFrames -ne [uint64]$Frames -or
        $drawCalls -eq 0U -or $triangles -eq 0U -or
        $networkOperations -ne 0U -or $writesPerformed -ne 0U) {
        Throw-VerificationFailure
    }

    $inventoryAfter = Get-InventorySnapshot -Roots $roots
    $mapAfter = Get-FileSnapshot -Path $mapTarget
    $wadsAfter = Get-WadSnapshot -Roots $roots
    if ($inventoryAfter -cne $inventoryBefore -or
        $mapAfter.Content -cne $mapBefore.Content -or
        $mapAfter.Length -ne $mapBefore.Length -or
        $mapAfter.WriteTime -ne $mapBefore.WriteTime -or
        $wadsAfter.Count -ne $wadsBefore.Count -or
        $wadsAfter.Digest -cne $wadsBefore.Digest) {
        Throw-VerificationFailure
    }

    Write-Output 'manual-world-render-verification=passed'
    Write-Output ('rendered-frames=' + $renderedFrames)
    Write-Output ('draw-calls=' + $drawCalls)
    Write-Output ('triangles=' + $triangles)
    Write-Output ('wad-files-snapshotted=' + $wadsBefore.Count)
    Write-Output 'network-operations=0'
    Write-Output 'writes-performed=0'
    Write-Output 'created-files=0'
    Write-Output 'deleted-files=0'
    Write-Output 'external-file-drift=none'
}
catch {
    [Console]::Error.WriteLine('manual-world-render-verification=failed')
    exit 1
}
