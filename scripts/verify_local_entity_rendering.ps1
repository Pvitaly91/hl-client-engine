[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string] $ViewerPath,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string] $Basedir,

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string] $Game = 'valve',

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^maps/[A-Za-z0-9_.-]+\.bsp$')]
    [string] $Map,

    [ValidateNotNullOrEmpty()]
    [string[]] $Models = @(),

    [ValidateNotNullOrEmpty()]
    [string[]] $Sprites = @(),

    [ValidateRange(2, 600)]
    [int] $Frames = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-UniqueVirtualAssets {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]] $ModelNames,

        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]] $SpriteNames
    )

    $seen = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($virtualName in @($ModelNames) + @($SpriteNames)) {
        if (-not $seen.Add($virtualName)) {
            throw 'Model and Sprite arguments must be case-insensitively unique'
        }
    }
}

function Get-ExactUnsignedSummary {
    param(
        [Parameter(Mandatory = $true)][string] $Output,
        [Parameter(Mandatory = $true)][string] $Name
    )

    $pattern = '(?m)^' + [regex]::Escape($Name) + '=([0-9]+)$'
    $matches = [regex]::Matches($Output, $pattern)
    if ($matches.Count -ne 1) {
        throw "Entity viewer must report exactly one $Name summary"
    }
    $value = [uint64]0
    if (-not [uint64]::TryParse(
            $matches[0].Groups[1].Value, [ref]$value)) {
        throw "Entity viewer $Name summary is outside the unsigned range"
    }
    return $value
}

Assert-UniqueVirtualAssets -ModelNames $Models -SpriteNames $Sprites
$expectedStudioCount = [uint64]$Models.Count
$expectedSpriteCount = [uint64]$Sprites.Count
$expectedUploadCount = $expectedStudioCount + $expectedSpriteCount

$gameRoot = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::Combine($Basedir, $Game))
if (-not (Test-Path -LiteralPath $gameRoot -PathType Container)) {
    throw 'Selected game root is missing'
}
$gameRootItem = Get-Item -LiteralPath $gameRoot -Force
if (($gameRootItem.Attributes -band
        [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Selected game root must not be a reparse point'
}

function Resolve-CheckedAsset {
    param([Parameter(Mandatory = $true)][string] $VirtualName)

    if ($VirtualName -notmatch '^(maps|models|sprites)/[A-Za-z0-9_./-]+$' -or
        $VirtualName.Contains('..') -or $VirtualName.Contains('\')) {
        throw "Unsafe virtual asset name: $VirtualName"
    }

    $candidate = [System.IO.Path]::GetFullPath(
        [System.IO.Path]::Combine($gameRoot, $VirtualName.Replace('/', '\')))
    $prefix = $gameRoot.TrimEnd('\') + '\'
    if (-not $candidate.StartsWith(
            $prefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Asset escaped the selected game root: $VirtualName"
    }
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Asset is missing: $VirtualName"
    }
    return $candidate
}

function Get-AssetEvidence {
    param([Parameter(Mandatory = $true)][string] $Path)

    $item = Get-Item -LiteralPath $Path -Force
    [pscustomobject]@{
        Path = $item.FullName
        Length = $item.Length
        LastWriteTimeUtc = $item.LastWriteTimeUtc
        Hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
    }
}

function Get-GameRootEvidence {
    $reparsePoints = @(Get-ChildItem -LiteralPath $gameRoot -Recurse -Force |
        Where-Object {
            ($_.Attributes -band
                [System.IO.FileAttributes]::ReparsePoint) -ne 0
        })
    if ($reparsePoints.Count -ne 0) {
        throw 'Selected game root contains unsupported reparse points'
    }
    return @(Get-ChildItem -LiteralPath $gameRoot -File -Recurse -Force |
        Sort-Object -Property FullName |
        ForEach-Object { Get-AssetEvidence $_.FullName })
}

function Get-StudioBundleSources {
    $sources = @{}
    foreach ($virtualModel in $Models) {
        $main = Resolve-CheckedAsset $virtualModel
        $sources[$main] = $true
        $directory = [System.IO.Path]::GetDirectoryName($main)
        $stem = [System.IO.Path]::GetFileNameWithoutExtension($main)
        $texture = [System.IO.Path]::Combine(
            $directory, $stem + 'T.mdl')
        if (Test-Path -LiteralPath $texture -PathType Leaf) {
            $sources[[System.IO.Path]::GetFullPath($texture)] = $true
        }
        for ($ordinal = 1; $ordinal -le 15; ++$ordinal) {
            $group = [System.IO.Path]::Combine(
                $directory,
                $stem + $ordinal.ToString('00') + '.mdl')
            if (Test-Path -LiteralPath $group -PathType Leaf) {
                $sources[[System.IO.Path]::GetFullPath($group)] = $true
            }
        }
    }
    return @($sources.Keys | Sort-Object)
}

function Assert-UnchangedEvidence {
    param(
        [Parameter(Mandatory = $true)][object[]] $Before,
        [Parameter(Mandatory = $true)][object[]] $After,
        [Parameter(Mandatory = $true)][string] $Label
    )

    $beforeByPath = @{}
    $afterByPath = @{}
    foreach ($entry in $Before) { $beforeByPath[$entry.Path] = $entry }
    foreach ($entry in $After) { $afterByPath[$entry.Path] = $entry }
    $created = @($afterByPath.Keys | Where-Object {
            -not $beforeByPath.ContainsKey($_)
        })
    $deleted = @($beforeByPath.Keys | Where-Object {
            -not $afterByPath.ContainsKey($_)
        })
    $changed = @($beforeByPath.Keys | Where-Object {
            if (-not $afterByPath.ContainsKey($_)) { return $false }
            $left = $beforeByPath[$_]
            $right = $afterByPath[$_]
            return $left.Length -ne $right.Length -or
                $left.LastWriteTimeUtc -ne $right.LastWriteTimeUtc -or
                $left.Hash -ne $right.Hash
        })
    if ($created.Count -ne 0 -or $deleted.Count -ne 0 -or
        $changed.Count -ne 0) {
        throw "$Label drift: created=$($created.Count) " +
            "deleted=$($deleted.Count) changed=$($changed.Count)"
    }
}

$virtualAssets = @($Map) + @($Models) + @($Sprites)
$resolvedAssets = @($virtualAssets | ForEach-Object { Resolve-CheckedAsset $_ })
$before = @($resolvedAssets | ForEach-Object { Get-AssetEvidence $_ })
$studioBundleSources = @(Get-StudioBundleSources)
$bundleBefore = @($studioBundleSources |
    ForEach-Object { Get-AssetEvidence $_ })
$rootBefore = @(Get-GameRootEvidence)

$arguments = @(
    '--basedir', $Basedir,
    '--game', $Game,
    '--map', $Map,
    '--fixture', $(if ($Models.Count -gt 0 -and $Sprites.Count -gt 0) {
            'mixed'
        } elseif ($Models.Count -gt 0) {
            'studio'
        } else {
            'sprite'
        }),
    '--camera', 'orbit'
)
foreach ($model in $Models) {
    $null = Resolve-CheckedAsset $model
    $arguments += @('--model', $model)
}
foreach ($sprite in $Sprites) {
    $null = Resolve-CheckedAsset $sprite
    $arguments += @('--sprite', $sprite)
}

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = [System.IO.Path]::GetFullPath($ViewerPath)
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.Environment['HLCLIENT_SMOKE_TEST_FRAMES'] = $Frames.ToString()
foreach ($argument in $arguments) {
    $startInfo.ArgumentList.Add($argument)
}
$observedEndpoint = $false
$process = $null
try {
    $process = [System.Diagnostics.Process]::Start($startInfo)
    if ($null -eq $process) {
        throw 'Unable to start the entity viewer'
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while (-not $process.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) {
            $process.Kill($true)
            throw 'Entity viewer exceeded the 60-second verification deadline'
        }
        if (Get-Command Get-NetTCPConnection -ErrorAction SilentlyContinue) {
            $observedEndpoint = $observedEndpoint -or [bool](
                Get-NetTCPConnection -OwningProcess $process.Id `
                    -ErrorAction SilentlyContinue)
        }
        if (Get-Command Get-NetUDPEndpoint -ErrorAction SilentlyContinue) {
            $observedEndpoint = $observedEndpoint -or [bool](
                Get-NetUDPEndpoint -OwningProcess $process.Id `
                    -ErrorAction SilentlyContinue)
        }
        Start-Sleep -Milliseconds 25
        $process.Refresh()
    }

    $process.WaitForExit()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stdout = $stdout.Replace("`r`n", "`n").Replace("`r", "`n")
    $stderr = $process.StandardError.ReadToEnd()
    if ($process.ExitCode -ne 0) {
        throw "Entity viewer failed with exit code $($process.ExitCode): $stderr"
    }
    $requestedStudio = Get-ExactUnsignedSummary `
        -Output $stdout -Name 'requested-studio'
    $requestedSprite = Get-ExactUnsignedSummary `
        -Output $stdout -Name 'requested-sprite'
    $renderUploads = Get-ExactUnsignedSummary `
        -Output $stdout -Name '[render] entity-uploads'
    $summaryUploads = Get-ExactUnsignedSummary `
        -Output $stdout -Name 'entity-uploads'
    if ($requestedStudio -ne $expectedStudioCount -or
        $requestedSprite -ne $expectedSpriteCount -or
        $renderUploads -ne $expectedUploadCount -or
        $summaryUploads -ne $expectedUploadCount) {
        throw 'Entity viewer asset and upload counts do not match the exact request'
    }
    if ($observedEndpoint -or $stdout -notmatch 'network-operations=0') {
        throw 'Entity viewer did not prove its zero-network boundary'
    }
    if ($stdout -notmatch '(?m)^writes=0$' -or
        $stdout -notmatch "(?m)^frames=$Frames$" -or
        $stdout -notmatch '(?m)^world-upload=1$' -or
        $stdout -notmatch '(?m)^world-scene-upload=1$' -or
        $stdout -notmatch '(?m)^\[entity\] snapshots=2$' -or
        $stdout -notmatch '(?m)^pose-fallbacks=0$' -or
        $stdout -notmatch
            '(?m)^entity-frame-revision-changes=[1-9][0-9]*$') {
        throw "Entity viewer summary was incomplete or unbounded: $stdout"
    }
    if ($Models.Count -gt 0 -and
        $stdout -notmatch '(?m)^studio-draws=[1-9][0-9]*$') {
        throw 'Entity viewer did not draw the requested Studio entities'
    }
    if ($Sprites.Count -gt 0 -and
        $stdout -notmatch '(?m)^sprite-draws=[1-9][0-9]*$') {
        throw 'Entity viewer did not draw the requested Sprite entities'
    }
} finally {
    if ($null -ne $process) {
        $process.Dispose()
    }
}

$after = @($resolvedAssets | ForEach-Object { Get-AssetEvidence $_ })
$bundleAfter = @($studioBundleSources |
    ForEach-Object { Get-AssetEvidence $_ })
$rootAfter = @(Get-GameRootEvidence)
Assert-UnchangedEvidence -Before $before -After $after -Label 'Selected asset'
Assert-UnchangedEvidence `
    -Before $bundleBefore -After $bundleAfter -Label 'Studio bundle source'
Assert-UnchangedEvidence `
    -Before $rootBefore -After $rootAfter -Label 'Selected game root'

Write-Output "assets=$($before.Count)"
Write-Output "studio-bundle-sources=$($bundleBefore.Count)"
Write-Output "game-root-files=$($rootBefore.Count)"
Write-Output 'network-operations=0'
Write-Output 'files-created=0 files-deleted=0 writes=0'
Write-Output 'external-file-drift=none'
