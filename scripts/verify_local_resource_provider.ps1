[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ToolPath,

    [Parameter(Mandatory = $true)]
    [string]$Basedir,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$Game = 'valve'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$MaximumManifestEntries = 200000
$MaximumManifestDepth = 64

trap {
    Write-Output 'local-provider-verification schema=hlclient.local-provider-read-only.v1'
    Write-Output 'verification-error=sanitized'
    Write-Output 'external-file-drift=unknown'
    exit 2
}

function Get-TextSha256 {
    param([Parameter(Mandatory = $true)][string]$Text)

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $algorithm.ComputeHash($bytes)
        return ([System.BitConverter]::ToString($digest)).Replace('-', '')
    }
    finally {
        $algorithm.Dispose()
    }
}

function Test-SafeGameDirectory {
    param([Parameter(Mandatory = $true)][string]$Value)

    if ($Value.Length -eq 0 -or $Value.Length -gt 255 -or
        $Value -eq '.' -or $Value -eq '..' -or
        $Value.EndsWith('.') -or $Value.EndsWith(' ') -or
        $Value.IndexOfAny([char[]]@('/', '\', ':')) -ge 0) {
        return $false
    }
    foreach ($character in $Value.ToCharArray()) {
        $code = [int]$character
        if ($code -lt 0x20 -or $code -gt 0x7e) {
            return $false
        }
    }
    $stem = ($Value -split '\.', 2)[0]
    return $stem -notmatch '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$'
}

function Get-FileSystemItemOrNull {
    param([Parameter(Mandatory = $true)][string]$Path)

    try {
        return Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    }
    catch {
        if ($_.Exception -is
                [System.Management.Automation.ItemNotFoundException] -or
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
        [Parameter()][switch]$AllowMissingFinal
    )

    if ($Path.IndexOf([char]0) -ge 0 -or
        $Path -notmatch '^[A-Za-z]:[\\/]' -or
        $Path -match '^[\\/]{2}' -or
        $Path -match '^[\\/]{2}[?.][\\/]') {
        throw 'Directory path must be an absolute local drive path.'
    }

    $rawRoot = [System.IO.Path]::GetPathRoot($Path)
    $rawRemainder = $Path.Substring($rawRoot.Length)
    foreach ($component in $rawRemainder.Split(
            [char[]]@('\', '/'),
            [System.StringSplitOptions]::RemoveEmptyEntries)) {
        if ($component -eq '.' -or $component -eq '..') {
            throw 'Directory path aliases are not accepted.'
        }
    }

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $driveRoot = [System.IO.Path]::GetPathRoot($fullPath)
    $drive = [System.IO.DriveInfo]::new($driveRoot)
    if (-not $drive.IsReady -or
        $drive.DriveType -ne [System.IO.DriveType]::Fixed) {
        throw 'Directory path must be on a ready fixed local drive.'
    }

    $current = $driveRoot
    $rootItem = Get-FileSystemItemOrNull -Path $current
    if ($null -eq $rootItem) {
        throw 'Directory path drive root is unavailable.'
    }
    if (-not $rootItem.PSIsContainer -or
        ($rootItem.Attributes -band
            [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Directory path root is not an ordinary local directory.'
    }

    $remainder = $fullPath.Substring($driveRoot.Length)
    $components = @($remainder.Split(
            [char[]]@('\', '/'),
            [System.StringSplitOptions]::RemoveEmptyEntries))
    for ($index = 0; $index -lt $components.Count; ++$index) {
        $component = $components[$index]
        $current = Join-Path -Path $current -ChildPath $component
        $item = Get-FileSystemItemOrNull -Path $current
        if ($null -eq $item) {
            if ($AllowMissingFinal -and
                $index + 1 -eq $components.Count) {
                return $null
            }
            throw 'Directory path component is unavailable.'
        }
        if (-not $item.PSIsContainer -or
            ($item.Attributes -band
                [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Directory path contains a non-directory or reparse point.'
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

    $rootItem = Get-FileSystemItemOrNull -Path $Root
    if ($null -eq $rootItem) {
        return [pscustomobject]@{
            root_id = $RootId
            exists = $false
            entry_count = 0
            manifest_sha256 = Get-TextSha256 -Text 'missing-root'
        }
    }

    if (($rootItem.Attributes -band
            [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Root manifest rejects non-directory or reparse-point roots.'
    }
    if (-not $rootItem.PSIsContainer) {
        throw 'Root manifest requires a directory root.'
    }

    $rootPrefix = $rootItem.FullName.TrimEnd([char[]]@('\', '/'))
    $rows = [System.Collections.Generic.List[string]]::new()
    $pending = [System.Collections.Generic.Queue[object]]::new()
    $pending.Enqueue($rootItem)
    while ($pending.Count -gt 0) {
        $directory = $pending.Dequeue()
        $directory.Refresh()
        if (($directory.Attributes -band
                [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Root manifest rejects directory reparse points.'
        }

        foreach ($entry in $directory.EnumerateFileSystemInfos()) {
            $entry.Refresh()
            if ($rows.Count -ge $MaximumManifestEntries) {
                throw 'Root manifest exceeds its entry safety bound.'
            }
            if (($entry.Attributes -band
                    [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Root manifest rejects nested reparse points.'
            }

            $isDirectory = $entry -is [System.IO.DirectoryInfo]
            $length = if ($isDirectory) { -1 } else { $entry.Length }
            $relativeName = $entry.FullName.Substring($rootPrefix.Length)
            $relativeName = $relativeName.TrimStart([char[]]@('\', '/'))
            $depth = ($relativeName -split '[\\/]').Count
            if ($depth -gt $MaximumManifestDepth) {
                throw 'Root manifest exceeds its depth safety bound.'
            }
            $rows.Add(('{0}|{1}|{2}|{3}' -f @(
                $relativeName,
                [int]$entry.Attributes,
                $length,
                $entry.LastWriteTimeUtc.Ticks
            )))

            if ($isDirectory) {
                $pending.Enqueue($entry)
            }
        }
    }
    $rows.Sort([System.StringComparer]::Ordinal)

    return [pscustomobject]@{
        root_id = $RootId
        exists = $true
        entry_count = $rows.Count
        manifest_sha256 = Get-TextSha256 -Text ($rows.ToArray() -join "`n")
    }
}

function Get-TargetSnapshot {
    param(
        [Parameter(Mandatory = $true)][string[]]$Roots
    )

    $targets = @()
    $manifests = @()
    for ($index = 0; $index -lt $Roots.Count; ++$index) {
        $root = $Roots[$index]
        $rootId = $index + 1
        $manifests += Get-RootManifest -Root $root -RootId $rootId
        $target = Join-Path -Path $root -ChildPath 'tempdecal.wad'
        $item = Get-FileSystemItemOrNull -Path $target
        if ($null -ne $item) {
            if (($item.Attributes -band
                    [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Target snapshot rejects a reparse point.'
            }
            if ($item.PSIsContainer) {
                throw 'Target snapshot rejects a directory.'
            }
            $targets += [pscustomobject]@{
                root_id = $rootId
                exists = $true
                length = [int64]$item.Length
                write_time_ticks = [int64]$item.LastWriteTimeUtc.Ticks
                content_sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
            }
        }
        else {
            $targets += [pscustomobject]@{
                root_id = $rootId
                exists = $false
                length = [int64]0
                write_time_ticks = [int64]0
                content_sha256 = ''
            }
        }
    }

    return [pscustomobject]@{
        targets = $targets
        manifests = $manifests
    }
}

function Test-SnapshotsEqual {
    param(
        [Parameter(Mandatory = $true)]$Before,
        [Parameter(Mandatory = $true)]$After
    )

    if ($Before.targets.Count -ne $After.targets.Count -or
        $Before.manifests.Count -ne $After.manifests.Count) {
        return $false
    }
    for ($index = 0; $index -lt $Before.targets.Count; ++$index) {
        $left = $Before.targets[$index]
        $right = $After.targets[$index]
        if ($left.root_id -ne $right.root_id -or
            $left.exists -ne $right.exists -or
            $left.length -ne $right.length -or
            $left.write_time_ticks -ne $right.write_time_ticks -or
            $left.content_sha256 -ne $right.content_sha256) {
            return $false
        }
    }
    for ($index = 0; $index -lt $Before.manifests.Count; ++$index) {
        $left = $Before.manifests[$index]
        $right = $After.manifests[$index]
        if ($left.root_id -ne $right.root_id -or
            $left.exists -ne $right.exists -or
            $left.entry_count -ne $right.entry_count -or
            $left.manifest_sha256 -ne $right.manifest_sha256) {
            return $false
        }
    }
    return $true
}

$tool = $null
$base = $null
try {
    $tool = (Resolve-Path -LiteralPath $ToolPath).ProviderPath
    $base = Assert-ReparseFreeDirectoryPath -Path $Basedir
}
catch {
    Write-Error 'Local-provider verifier input is unavailable or invalid.'
    exit 2
}

if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
    Write-Error 'Local-provider verifier requires an existing tool and basedir.'
    exit 2
}

if (-not (Test-SafeGameDirectory -Value $Game)) {
    Write-Error 'Local-provider verifier requires a safe game directory on a fixed local basedir.'
    exit 2
}

$candidateRoots = @()
if ($Game -cne 'valve') {
    $candidateRoots += Join-Path -Path $base -ChildPath $Game
}
$candidateRoots += Join-Path -Path $base -ChildPath 'valve'

foreach ($candidateRoot in $candidateRoots) {
    [void](Assert-ReparseFreeDirectoryPath -Path $candidateRoot -AllowMissingFinal)
}

$before = Get-TargetSnapshot -Roots $candidateRoots
$toolExit = 1
try {
    & $tool `
        --basedir $base `
        --game $Game `
        --check-consistency-provider
    $toolExit = $LASTEXITCODE
}
finally {
    $after = Get-TargetSnapshot -Roots $candidateRoots
}

$unchanged = Test-SnapshotsEqual -Before $before -After $after
if (-not $unchanged) {
    Write-Output 'local-provider-verification schema=hlclient.local-provider-read-only.v1'
    Write-Output 'external-file-drift=detected'
    exit 1
}

Write-Output 'local-provider-verification schema=hlclient.local-provider-read-only.v1'
Write-Output ('roots-observed={0}' -f $candidateRoots.Count)
Write-Output 'content-hash-unchanged=true'
Write-Output 'size-unchanged=true'
Write-Output 'write-time-unchanged=true'
Write-Output 'created-or-removed-files=0'
Write-Output 'external-file-drift=none'

if ($toolExit -ne 0) {
    exit $toolExit
}
exit 0
