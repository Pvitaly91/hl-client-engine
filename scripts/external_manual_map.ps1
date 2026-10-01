# Read-only preflight for an explicitly installed, user-owned BSP. This file is
# dot-sourced by Start-HLClient-H2-Manual.ps1; it never copies game assets.
function Resolve-ExternalManualMapBsp {
    param([string]$ResearchRoot, [string]$BspPath, [string]$RepositoryRoot)
    if ($BspPath -cnotmatch '^[A-Za-z]:[\\/]') {
        throw 'unsafe_path: ExternalMapBsp must be an absolute local-drive path.'
    }
    $root = [IO.Path]::GetFullPath($ResearchRoot).TrimEnd('\', '/')
    $repo = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $path = [IO.Path]::GetFullPath($BspPath)
    if ([IO.Path]::GetExtension($path) -in @('.zip', '.rar', '.7z')) {
        throw 'archive_not_extracted: supply the actual BSP after inspecting and extracting the package yourself.'
    }
    if ($root.Equals($repo, [StringComparison]::OrdinalIgnoreCase) -or
        $root.StartsWith($repo + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $repo.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $root -match '(?i)(?:^|[\\/])steamapps(?:[\\/]|$)') {
        throw 'unsafe_path: research root must be outside the repository and Steam library.'
    }
    $name = [IO.Path]::GetFileNameWithoutExtension($path)
    if ($name -cnotmatch '^[A-Za-z0-9_][A-Za-z0-9_-]{0,30}$' -or
        [IO.Path]::GetExtension($path) -ine '.bsp') {
        throw 'unsafe_path: BSP basename or extension is not a safe GoldSrc map name.'
    }
    $expected = [IO.Path]::GetFullPath((Join-Path $root ("valve\maps\{0}.bsp" -f $name)))
    if (-not $path.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'map_not_visible_to_client_root: BSP must already be in research-root valve\maps; no copy or overlay is performed.'
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw 'bsp_not_found: selected BSP is absent from the isolated research root.'
    }
    $cursor = $path
    while ($cursor) {
        $entry = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'unsafe_path: BSP or an ancestor is a reparse point.'
        }
        $parent = [IO.Path]::GetDirectoryName($cursor)
        if (-not $parent -or $parent -eq $cursor) { break }
        $cursor = $parent
    }
    $file = Get-Item -LiteralPath $path -Force
    $linkProperty = $file.PSObject.Properties['LinkType']
    if ($null -eq $linkProperty -or -not [string]::IsNullOrEmpty([string]$linkProperty.Value)) {
        throw 'unsafe_path: BSP hard-link state is not an ordinary single file.'
    }
    if ($file.Length -lt 124 -or $file.Length -gt 268435456) {
        throw 'malformed_bsp: BSP has an unsupported size.'
    }
    if ([IO.DriveInfo]::new([IO.Path]::GetPathRoot($path)).DriveType -ne
        [IO.DriveType]::Fixed) {
        throw 'unsafe_path: BSP must be on a fixed local drive.'
    }
    return [pscustomobject]@{ Map=$name; Path=$path; Root=$root }
}

function Get-ExternalMapDeclaredWads {
    param([string]$BspPath)
    $stream = [IO.File]::Open($BspPath, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        try {
            if ($reader.ReadInt32() -ne 30) { throw 'malformed_bsp: expected GoldSrc BSP version 30.' }
            $offset = $reader.ReadInt32()
            $length = $reader.ReadInt32()
            if ($offset -lt 124 -or $length -lt 1 -or $length -gt 131072 -or
                [long]$offset + $length -gt $stream.Length) {
                throw 'malformed_bsp: entity lump is out of bounds.'
            }
            $stream.Position = $offset
            $bytes = $reader.ReadBytes($length)
            if ($bytes.Length -ne $length) { throw 'malformed_bsp: short entity lump.' }
            $entities = [Text.Encoding]::Latin1.GetString($bytes)
            $world = [regex]::Match($entities, '(?s)\{([^{}]*)\}')
            if (-not $world.Success -or
                $world.Groups[1].Value -cnotmatch '"classname"\s*"worldspawn"') {
                throw 'malformed_bsp: first entity is not worldspawn.'
            }
            $declaration = [regex]::Match($world.Groups[1].Value, '"wad"\s*"([^"]*)"')
            if (-not $declaration.Success) { return @() }
            $result = [Collections.Generic.List[string]]::new()
            foreach ($segment in $declaration.Groups[1].Value.Split(';')) {
                $trimmed = $segment.Trim()
                if (-not $trimmed) { continue }
                $base = ($trimmed -split '[\\/]')[-1]
                if ($base.Length -gt 128 -or $base -notmatch '(?i)\.wad$' -or
                    $base -match '[^\x20-\x7e]|[\\/:]' -or
                    $base -match '^(?i:con|prn|aux|nul|com[1-9]|lpt[1-9])\.') {
                    throw 'unsafe_path: worldspawn WAD declaration has no safe basename.'
                }
                if ($base -notin $result) { $result.Add($base) }
                if ($result.Count -gt 128) { throw 'malformed_bsp: WAD declaration count is unbounded.' }
            }
            return @($result)
        } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Get-ExternalMapResInventory {
    param([string]$ResearchRoot, [string]$Map)
    $res = Join-Path $ResearchRoot ("valve\maps\{0}.res" -f $Map)
    if (-not (Test-Path -LiteralPath $res -PathType Leaf)) { return @() }
    $item = Get-Item -LiteralPath $res -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        $item.Length -gt 65536) { throw 'unsafe_path: RES file is reparse or unbounded.' }
    $lines = [IO.File]::ReadAllLines($res)
    if ($lines.Length -gt 2048) { throw 'unsafe_path: RES entry count is unbounded.' }
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($line in $lines) {
        $match = [regex]::Match($line, '^\s*"([^"]+)"')
        if (-not $match.Success) { continue }
        $virtual = $match.Groups[1].Value.Replace('/', '\')
        $components = $virtual.Split('\')
        if ($virtual.Length -gt 240 -or $virtual -match '[^\x20-\x7e]|[:]' -or
            @($components | Where-Object { -not $_ -or $_ -in @('.', '..') -or
                $_.EndsWith('.') -or $_.EndsWith(' ') }).Count -ne 0) {
            throw 'unsafe_path: RES contains an unsafe virtual resource name.'
        }
        $target = Join-Path $ResearchRoot ('valve\' + $virtual)
        $entries.Add([pscustomobject]@{ Name=$virtual.Replace('\','/');
            Present=(Test-Path -LiteralPath $target -PathType Leaf) })
    }
    return @($entries)
}

function Invoke-ExternalManualMapPreflight {
    param([string]$RepositoryRoot, [System.Collections.IDictionary]$Configuration,
          [string]$BspPath)
    $selected = Resolve-ExternalManualMapBsp $Configuration.ResearchHalfLifeRoot $BspPath $RepositoryRoot
    if ($Configuration.Map -cne $selected.Map -or $Configuration.Game -cne 'valve' -or
        $Configuration.ValidationMode -cne 'Fast') {
        throw 'client_map_identity_mismatch: external selection and managed runner disagree.'
    }
    $marker = Join-Path $selected.Root '.hlclient-research-isolated'
    $manifestPath = Join-Path $selected.Root '.hlclient-research-preparation.json'
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or
        -not (Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
        (Get-Item -LiteralPath $marker).Length -gt 64 -or
        (Get-Item -LiteralPath $manifestPath).Length -gt 32768) {
        throw 'research_copy_not_prepared: isolated marker or bounded preparation manifest is missing.'
    }
    if ([IO.File]::ReadAllText($marker).TrimEnd("`r", "`n") -cne
        'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1') {
        throw 'research_copy_not_prepared: isolated marker content disagrees.'
    }
    $manifest = [IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json -ErrorAction Stop
    if ($manifest.schema -cne 'hlclient.stock-runtime-research-preparation.v3' -or
        $manifest.preparation_status -cne 'exact-materialized-copy-verified') {
        throw 'research_copy_not_prepared: Fast launch requires the existing prepared v3 research copy.'
    }
    $checker = Join-Path $RepositoryRoot 'build\bin\Release\hlclient_bsp_compat_check.exe'
    if (-not (Test-Path -LiteralPath $checker -PathType Leaf)) {
        throw 'bsp_checker_missing: build the Release hlclient_bsp_compat_check target.'
    }
    $wads = @(Get-ExternalMapDeclaredWads $selected.Path)
    foreach ($wad in $wads) {
        $present = Test-Path -LiteralPath (Join-Path $selected.Root ('valve\' + $wad)) -PathType Leaf
        Write-Host "[external-map] declared_wad=$wad present=$($present.ToString().ToLowerInvariant())"
    }
    $res = @(Get-ExternalMapResInventory $selected.Root $selected.Map)
    foreach ($entry in $res) {
        Write-Host "[external-map] res=$($entry.Name) present=$($entry.Present.ToString().ToLowerInvariant())"
    }
    $PSNativeCommandUseErrorActionPreference = $false
    $summary = @(& $checker --basedir $selected.Root --game valve `
        --map ("maps/{0}.bsp" -f $selected.Map) --validate-through textures 2>&1)
    if ($LASTEXITCODE -ne 0) {
        if (@($wads | Where-Object { -not (Test-Path -LiteralPath (Join-Path $selected.Root ('valve\' + $_)) -PathType Leaf) }).Count) {
            throw 'missing_wad: texture import failed and a declared WAD is absent from the approved valve root.'
        }
        throw "malformed_bsp: production BSP/texture import failed ($($summary -join ' '))."
    }
    foreach ($line in $summary) { Write-Host "[external-map] $line" }
    Write-Host "[external-map] map=$($selected.Map) declared_wads=$($wads.Count) res_entries=$($res.Count)"
    Write-Host "[external-map] release_sha256=$((Get-FileHash -LiteralPath $Configuration.ClientPath -Algorithm SHA256).Hash)"
    Write-Host '[external-map] preflight=pass; client/server use the same isolated valve root; no game process started by this check.'
}
