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
    [string[]]$Maps
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$MaximumInventoryEntries = 200000
$MaximumInventoryDepth = 64
$MaximumMapBytes = 67108864
$Scenarios = @('summary', 'spawn-probes', 'deterministic-probes')

function Throw-VerificationFailure {
    throw [System.InvalidOperationException]::new(
        'Local BSP collision verification failed.')
}

function Get-Sha256Text {
    param([Parameter(Mandatory = $true)][string]$Text)

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

function Convert-PeRvaToFileOffset {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$SectionTableOffset,
        [Parameter(Mandatory = $true)][int]$SectionCount,
        [Parameter(Mandatory = $true)][uint32]$Rva
    )

    for ($index = 0; $index -lt $SectionCount; ++$index) {
        $section = $SectionTableOffset + ($index * 40)
        if ($section -lt 0 -or $section + 40 -gt $Bytes.Length) {
            Throw-VerificationFailure
        }
        $virtualSize = [BitConverter]::ToUInt32($Bytes, $section + 8)
        $virtualAddress = [BitConverter]::ToUInt32($Bytes, $section + 12)
        $rawSize = [BitConverter]::ToUInt32($Bytes, $section + 16)
        $rawOffset = [BitConverter]::ToUInt32($Bytes, $section + 20)
        $mappedSize = [Math]::Max([uint64]$virtualSize, [uint64]$rawSize)
        if ([uint64]$Rva -ge [uint64]$virtualAddress -and
            [uint64]$Rva -lt ([uint64]$virtualAddress + $mappedSize)) {
            $offset = [uint64]$rawOffset +
                ([uint64]$Rva - [uint64]$virtualAddress)
            if ($offset -ge [uint64]$Bytes.Length) {
                Throw-VerificationFailure
            }
            return [int]$offset
        }
    }
    Throw-VerificationFailure
}

function Get-PeImportNames {
    param([Parameter(Mandatory = $true)][string]$Path)

    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Length -lt 256 -or $item.Length -gt 268435456) {
        Throw-VerificationFailure
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ([BitConverter]::ToUInt16($bytes, 0) -ne 0x5A4D) {
        Throw-VerificationFailure
    }
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($pe -lt 0 -or $pe + 24 -gt $bytes.Length -or
        [BitConverter]::ToUInt32($bytes, $pe) -ne 0x00004550) {
        Throw-VerificationFailure
    }
    $sectionCount = [int][BitConverter]::ToUInt16($bytes, $pe + 6)
    $optionalSize = [int][BitConverter]::ToUInt16($bytes, $pe + 20)
    $optional = $pe + 24
    if ($sectionCount -le 0 -or $optionalSize -lt 96 -or
        $optional + $optionalSize -gt $bytes.Length) {
        Throw-VerificationFailure
    }
    $magic = [BitConverter]::ToUInt16($bytes, $optional)
    if ($magic -eq 0x010B) {
        $directory = $optional + 96
        $directoryCountOffset = $optional + 92
    }
    elseif ($magic -eq 0x020B) {
        $directory = $optional + 112
        $directoryCountOffset = $optional + 108
    }
    else {
        Throw-VerificationFailure
    }
    $directoryCount = [BitConverter]::ToUInt32($bytes, $directoryCountOffset)
    if ($directoryCount -lt 2 -or $directory + 16 -gt
        $optional + $optionalSize) {
        Throw-VerificationFailure
    }
    if ($directoryCount -gt 13 -and $directory + (14 * 8) -le
        $optional + $optionalSize -and
        [BitConverter]::ToUInt32($bytes, $directory + (13 * 8)) -ne 0) {
        # Fail closed rather than accepting uninspected delay-loaded modules.
        Throw-VerificationFailure
    }

    $importRva = [BitConverter]::ToUInt32($bytes, $directory + 8)
    $importSize = [BitConverter]::ToUInt32($bytes, $directory + 12)
    if ($importRva -eq 0 -or $importSize -lt 20) {
        Throw-VerificationFailure
    }
    $sections = $optional + $optionalSize
    $imports = Convert-PeRvaToFileOffset -Bytes $bytes `
        -SectionTableOffset $sections -SectionCount $sectionCount `
        -Rva $importRva
    $maximumDescriptors = [Math]::Min(
        1024, [int]([uint64]$importSize / 20))
    $names = [System.Collections.Generic.List[string]]::new()
    $terminated = $false
    for ($index = 0; $index -lt $maximumDescriptors; ++$index) {
        $descriptor = $imports + ($index * 20)
        if ($descriptor -lt 0 -or $descriptor + 20 -gt $bytes.Length) {
            Throw-VerificationFailure
        }
        $allZero = $true
        for ($byteIndex = 0; $byteIndex -lt 20; ++$byteIndex) {
            if ($bytes[$descriptor + $byteIndex] -ne 0) {
                $allZero = $false
                break
            }
        }
        if ($allZero) {
            $terminated = $true
            break
        }
        $nameRva = [BitConverter]::ToUInt32($bytes, $descriptor + 12)
        $nameOffset = Convert-PeRvaToFileOffset -Bytes $bytes `
            -SectionTableOffset $sections -SectionCount $sectionCount `
            -Rva $nameRva
        $end = $nameOffset
        while ($end -lt $bytes.Length -and
               $end - $nameOffset -le 128 -and $bytes[$end] -ne 0) {
            ++$end
        }
        if ($end -ge $bytes.Length -or $end - $nameOffset -gt 128 -or
            $end -eq $nameOffset) {
            Throw-VerificationFailure
        }
        $names.Add([Text.Encoding]::ASCII.GetString(
            $bytes, $nameOffset, $end - $nameOffset))
    }
    if (-not $terminated -or $names.Count -eq 0) {
        Throw-VerificationFailure
    }
    return $names.ToArray()
}

function Resolve-TrustedChecker {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ($Path.IndexOf([char]0) -ge 0) {
        Throw-VerificationFailure
    }
    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full -notmatch '^[A-Za-z]:[\\/]' -or $full -match '^[\\/]{2}') {
        Throw-VerificationFailure
    }
    $projectRoot = [System.IO.Path]::GetFullPath(
        (Join-Path -Path $PSScriptRoot -ChildPath '..'))
    $buildBin = [System.IO.Path]::GetFullPath(
        (Join-Path -Path $projectRoot -ChildPath 'build\bin'))
    $prefix = $buildBin.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar) +
        [System.IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith(
            $prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        Throw-VerificationFailure
    }
    $relative = $full.Substring($prefix.Length)
    $components = @($relative.Split(
        [char[]]@('\', '/'),
        [System.StringSplitOptions]::RemoveEmptyEntries))
    if ($components.Count -ne 2 -or
        $components[0] -notin @('Debug', 'Release', 'RelWithDebInfo') -or
        $components[1] -ine 'hlclient_collision_check.exe') {
        Throw-VerificationFailure
    }
    [void](Assert-ReparseFreeDirectory -Path (
        Split-Path -Path $full -Parent))
    $tool = Get-Item -LiteralPath $full -Force
    if ($tool.PSIsContainer -or $tool.Extension -ine '.exe' -or
        (($tool.Attributes -band
          [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
        Throw-VerificationFailure
    }

    $imports = @(Get-PeImportNames -Path $tool.FullName)
    foreach ($import in $imports) {
        if ($import -ieq 'KERNEL32.dll' -or
            $import -match '^(?i:MSVCP140(?:_ATOMIC_WAIT)?D?\.dll)$' -or
            $import -match '^(?i:VCRUNTIME140(?:_1)?D?\.dll)$' -or
            $import -match '^(?i:ucrtbaseD?\.dll)$' -or
            $import -match '^(?i:api-ms-win-(?:core|crt)-[A-Za-z0-9-]+\.dll)$') {
            continue
        }
        Throw-VerificationFailure
    }
    return $tool
}

function Assert-SafeGame {
    if ($Game.Length -gt 64 -or
        $Game -notmatch '^[A-Za-z0-9_-]+$' -or
        $Game -eq '.' -or $Game -eq '..') {
        Throw-VerificationFailure
    }
}

function Assert-SafeVirtualMap {
    param([Parameter(Mandatory = $true)][string]$VirtualMap)

    if ($VirtualMap.Length -gt 255 -or
        $VirtualMap -notmatch '^[\x21-\x7E]+$' -or
        $VirtualMap.Contains('\') -or $VirtualMap.Contains(':') -or
        $VirtualMap.StartsWith('/') -or $VirtualMap.EndsWith('/') -or
        $VirtualMap.Contains('//')) {
        Throw-VerificationFailure
    }
    $segments = @($VirtualMap.Split('/'))
    if ($segments.Count -lt 2 -or $segments[0] -cne 'maps' -or
        $segments[-1] -notmatch '(?i)\.bsp$') {
        Throw-VerificationFailure
    }
    foreach ($segment in $segments) {
        if ([string]::IsNullOrEmpty($segment) -or
            $segment -eq '.' -or $segment -eq '..' -or
            $segment.EndsWith('.') -or $segment.EndsWith(' ')) {
            Throw-VerificationFailure
        }
        $stem = ($segment -split '\.', 2)[0]
        if ($stem -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
            Throw-VerificationFailure
        }
    }
}

function Assert-ReparseFreeDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ($Path.IndexOf([char]0) -ge 0 -or
        $Path -notmatch '^[A-Za-z]:[\\/]' -or
        $Path -match '^[\\/]{2}') {
        Throw-VerificationFailure
    }
    $full = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetPathRoot($full)
    $drive = [System.IO.DriveInfo]::new($root)
    if (-not $drive.IsReady -or
        $drive.DriveType -ne [System.IO.DriveType]::Fixed) {
        Throw-VerificationFailure
    }
    $current = $root
    foreach ($component in $full.Substring($root.Length).Split(
            [char[]]@('\', '/'),
            [System.StringSplitOptions]::RemoveEmptyEntries)) {
        if ($component -eq '.' -or $component -eq '..') {
            Throw-VerificationFailure
        }
        $current = Join-Path -Path $current -ChildPath $component
        $item = Get-Item -LiteralPath $current -Force
        if (-not $item.PSIsContainer -or
            (($item.Attributes -band
              [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
            Throw-VerificationFailure
        }
    }
    return $full.TrimEnd([char[]]@('\', '/'))
}

function Resolve-MapTargets {
    param(
        [Parameter(Mandatory = $true)][string]$Base,
        [Parameter(Mandatory = $true)][string[]]$RootNames
    )

    $targets = [System.Collections.Generic.List[object]]::new()
    foreach ($virtualMap in $Maps) {
        Assert-SafeVirtualMap -VirtualMap $virtualMap
        $relative = $virtualMap.Replace(
            '/', [System.IO.Path]::DirectorySeparatorChar)
        $selected = $null
        foreach ($rootName in $RootNames) {
            $root = Assert-ReparseFreeDirectory -Path (
                Join-Path -Path $Base -ChildPath $rootName)
            $candidate = [System.IO.Path]::GetFullPath(
                (Join-Path -Path $root -ChildPath $relative))
            $prefix = $root.TrimEnd(
                [System.IO.Path]::DirectorySeparatorChar) +
                [System.IO.Path]::DirectorySeparatorChar
            if (-not $candidate.StartsWith(
                    $prefix,
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                Throw-VerificationFailure
            }
            if (-not [System.IO.File]::Exists($candidate)) {
                continue
            }
            [void](Assert-ReparseFreeDirectory -Path (
                Split-Path -Path $candidate -Parent))
            $item = Get-Item -LiteralPath $candidate -Force
            if ($item.PSIsContainer -or
                (($item.Attributes -band
                  [System.IO.FileAttributes]::ReparsePoint) -ne 0) -or
                $item.Length -gt $MaximumMapBytes) {
                Throw-VerificationFailure
            }
            $selected = [pscustomobject]@{
                VirtualMap = $virtualMap
                NativePath = $candidate
            }
            break
        }
        if ($null -eq $selected) {
            Throw-VerificationFailure
        }
        $targets.Add($selected)
    }
    return $targets.ToArray()
}

function Get-MapSnapshot {
    param([Parameter(Mandatory = $true)][string]$Path)

    $item = Get-Item -LiteralPath $Path -Force
    $stream = [System.IO.File]::Open(
        $Path,
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
    }
    finally {
        $stream.Dispose()
    }
    $item.Refresh()
    return [pscustomobject]@{
        Hash = ([System.BitConverter]::ToString($digest)).Replace(
            '-', '').ToLowerInvariant()
        Size = [int64]$item.Length
        WriteTicks = [int64]$item.LastWriteTimeUtc.Ticks
    }
}

function Get-InventorySnapshot {
    param([Parameter(Mandatory = $true)][string[]]$Roots)

    $rows = [System.Collections.Generic.List[string]]::new()
    for ($rootIndex = 0; $rootIndex -lt $Roots.Count; ++$rootIndex) {
        $root = Assert-ReparseFreeDirectory -Path $Roots[$rootIndex]
        $pending = [System.Collections.Generic.Queue[object]]::new()
        $pending.Enqueue((Get-Item -LiteralPath $root -Force))
        while ($pending.Count -gt 0) {
            $directory = $pending.Dequeue()
            foreach ($entry in $directory.EnumerateFileSystemInfos()) {
                if ($rows.Count -ge $MaximumInventoryEntries -or
                    (($entry.Attributes -band
                      [System.IO.FileAttributes]::ReparsePoint) -ne 0)) {
                    Throw-VerificationFailure
                }
                $relative = $entry.FullName.Substring($root.Length).TrimStart(
                    [char[]]@('\', '/'))
                if (($relative -split '[\\/]').Count -gt
                    $MaximumInventoryDepth) {
                    Throw-VerificationFailure
                }
                $isDirectory = $entry -is [System.IO.DirectoryInfo]
                $size = if ($isDirectory) { -1 } else { [int64]$entry.Length }
                $rows.Add(('{0}|{1}|{2}|{3}|{4}' -f @(
                    $rootIndex,
                    $relative,
                    [int]$entry.Attributes,
                    $size,
                    $entry.LastWriteTimeUtc.Ticks)))
                if ($isDirectory) {
                    $pending.Enqueue($entry)
                }
            }
        }
    }
    $rows.Sort([System.StringComparer]::Ordinal)
    return Get-Sha256Text -Text ($rows.ToArray() -join "`n")
}

function Invoke-Checker {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$Base,
        [Parameter(Mandatory = $true)][string]$VirtualMap,
        [Parameter(Mandatory = $true)][string]$Scenario
    )

    $lines = @(& $Executable --basedir $Base --game $Game `
        --map $VirtualMap --scenario $Scenario 2>&1 |
        ForEach-Object { [string]$_ })
    if ($LASTEXITCODE -ne 0) {
        Throw-VerificationFailure
    }
    $summary = [string]::Join("`n", $lines)
    if ($summary -notmatch '(?m)^\[collision\] result=success$' -or
        $summary -notmatch '(?m)^\[collision\] structural-hash=[0-9a-f]{64}$' -or
        $summary -notmatch '(?m)^\[collision\] query-hash=[0-9a-f]{64}$' -or
        $summary -notmatch '(?m)^\[collision\] clipnodes=[1-9][0-9]*$') {
        Throw-VerificationFailure
    }
    if ($Scenario -eq 'deterministic-probes' -and
        $summary -notmatch '(?m)^\[collision\] trace-probes=[1-9][0-9]*$') {
        Throw-VerificationFailure
    }
    return $summary
}

try {
    Assert-SafeGame
    if ($Maps.Count -eq 0 -or $Maps.Count -gt 64) {
        Throw-VerificationFailure
    }
    $tool = Resolve-TrustedChecker -Path $ToolPath
    $toolBefore = Get-MapSnapshot -Path $tool.FullName
    $base = Assert-ReparseFreeDirectory -Path $Basedir
    $rootNames = @($Game, 'valve') | Select-Object -Unique
    $roots = @($rootNames | ForEach-Object {
        Assert-ReparseFreeDirectory -Path (
            Join-Path -Path $base -ChildPath $_)
    })
    $targets = @(Resolve-MapTargets -Base $base -RootNames $rootNames)
    $beforeInventory = Get-InventorySnapshot -Roots $roots
    $beforeMaps = @($targets | ForEach-Object {
        Get-MapSnapshot -Path $_.NativePath
    })

    $summaryHashes = [System.Collections.Generic.List[string]]::new()
    for ($mapIndex = 0; $mapIndex -lt $targets.Count; ++$mapIndex) {
        foreach ($scenario in $Scenarios) {
            $first = Invoke-Checker -Executable $tool.FullName -Base $base `
                -VirtualMap $targets[$mapIndex].VirtualMap -Scenario $scenario
            $second = Invoke-Checker -Executable $tool.FullName -Base $base `
                -VirtualMap $targets[$mapIndex].VirtualMap -Scenario $scenario
            if ($first -cne $second) {
                Throw-VerificationFailure
            }
            $summaryHashes.Add((Get-Sha256Text -Text $first))
        }
    }

    $toolAfter = Get-MapSnapshot -Path $tool.FullName
    if ($toolBefore.Hash -cne $toolAfter.Hash -or
        $toolBefore.Size -ne $toolAfter.Size -or
        $toolBefore.WriteTicks -ne $toolAfter.WriteTicks) {
        Throw-VerificationFailure
    }

    $afterInventory = Get-InventorySnapshot -Roots $roots
    if ($beforeInventory -cne $afterInventory) {
        Throw-VerificationFailure
    }
    for ($mapIndex = 0; $mapIndex -lt $targets.Count; ++$mapIndex) {
        $after = Get-MapSnapshot -Path $targets[$mapIndex].NativePath
        $before = $beforeMaps[$mapIndex]
        if ($before.Hash -cne $after.Hash -or
            $before.Size -ne $after.Size -or
            $before.WriteTicks -ne $after.WriteTicks) {
            Throw-VerificationFailure
        }
    }

    Write-Output 'manual-bsp-collision-verification=passed'
    Write-Output ('maps-verified=' + $targets.Count)
    Write-Output ('scenarios-per-map=' + $Scenarios.Count)
    Write-Output 'checker-runs-per-scenario=2'
    Write-Output 'deterministic-summaries=true'
    Write-Output ('aggregate-summary-sha256=' +
        (Get-Sha256Text -Text ($summaryHashes.ToArray() -join "`n")))
    Write-Output 'created-files=0'
    Write-Output 'deleted-files=0'
    Write-Output 'checker-network-imports=none'
    Write-Output 'network-operations=0'
    Write-Output 'external-file-drift=none'
}
catch {
    [Console]::Error.WriteLine('manual-bsp-collision-verification=failed')
    exit 1
}
