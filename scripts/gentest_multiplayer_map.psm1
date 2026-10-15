# Local QA derivative only. No game launch, downloads, SDK/server changes or
# original-asset edits. Mandatory tests use generated, project-owned bytes.
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'external_manual_map.ps1')
$script:PinnedGentestSha256 = '9338A39EAA14332625A8F4935DED3345A1CD9EC3ABB141750590091A4D21D7A2'

function Skip-GentestWhitespace {
    param([string]$Text, $Cursor)
    while ($Cursor.At -lt $Text.Length) {
        if ([char]::IsWhiteSpace($Text[$Cursor.At])) { $Cursor.At++; continue }
        if ($Text[$Cursor.At] -eq '/' -and $Cursor.At + 1 -lt $Text.Length -and
            $Text[$Cursor.At + 1] -eq '/') {
            while ($Cursor.At -lt $Text.Length -and $Text[$Cursor.At] -ne "`n") { $Cursor.At++ }
            continue
        }
        break
    }
}

function Read-GentestQuotedToken {
    param([string]$Text, $Cursor)
    Skip-GentestWhitespace $Text $Cursor
    if ($Cursor.At -ge $Text.Length -or $Text[$Cursor.At] -ne '"') { throw 'gentest_mp_entity_grammar' }
    $Cursor.At++
    $start = $Cursor.At
    # GoldSrc's quoted token terminates at the next literal quote. Backslashes
    # and braces in a value are data, not escapes or entity delimiters.
    while ($Cursor.At -lt $Text.Length -and $Text[$Cursor.At] -ne '"') {
        if ([char]::IsControl($Text[$Cursor.At])) { throw 'gentest_mp_entity_control' }
        $Cursor.At++
    }
    if ($Cursor.At -ge $Text.Length) { throw 'gentest_mp_entity_quote' }
    $value = $Text.Substring($start, $Cursor.At - $start)
    if ($value.Length -gt 4096) { throw 'gentest_mp_entity_token_bound' }
    $Cursor.At++
    return [pscustomobject]@{ Value=$value; Start=$start; Length=$value.Length }
}

function Read-GentestEntities {
    param([string]$Text)
    $cursor = [pscustomobject]@{ At=0 }
    $entities = [Collections.Generic.List[object]]::new()
    while ($true) {
        Skip-GentestWhitespace $Text $cursor
        if ($cursor.At -eq $Text.Length) { break }
        if ($Text[$cursor.At] -ne '{') { throw 'gentest_mp_entity_open' }
        $start = $cursor.At++
        $pairs = [Collections.Generic.List[object]]::new()
        while ($true) {
            Skip-GentestWhitespace $Text $cursor
            if ($cursor.At -ge $Text.Length) { throw 'gentest_mp_entity_close' }
            if ($Text[$cursor.At] -eq '}') { $cursor.At++; break }
            $key = Read-GentestQuotedToken $Text $cursor
            $value = Read-GentestQuotedToken $Text $cursor
            if ($key.Value.Length -eq 0 -or $key.Value.Length -gt 128) { throw 'gentest_mp_entity_key' }
            $pairs.Add([pscustomobject]@{ Key=$key.Value; Value=$value.Value;
                ValueStart=$value.Start; ValueLength=$value.Length })
            if ($pairs.Count -gt 256) { throw 'gentest_mp_entity_pair_bound' }
        }
        $classes = @($pairs | Where-Object Key -EQ 'classname')
        if ($classes.Count -ne 1) { throw 'gentest_mp_classname_not_unique' }
        if (@($pairs | Where-Object Key -EQ 'origin').Count -gt 1) { throw 'gentest_mp_origin_not_unique' }
        $entities.Add([pscustomobject]@{ Start=$start; Length=$cursor.At-$start;
            Classname=$classes[0].Value; ClassPair=$classes[0]; Pairs=@($pairs.ToArray()) })
        if ($entities.Count -gt 4096) { throw 'gentest_mp_entity_count_bound' }
    }
    return ,$entities.ToArray()
}

function Read-GentestVector {
    param([string]$Value, [double]$MaximumMagnitude = 32768)
    $parts = @([regex]::Split($Value.Trim(), '\s+'))
    if ($parts.Count -ne 3) { throw 'gentest_mp_vector_arity' }
    $numbers = [double[]]::new(3)
    for ($i=0; $i -lt 3; $i++) {
        $number = 0.0
        if (-not [double]::TryParse($parts[$i], [Globalization.NumberStyles]::Float,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
            -not [double]::IsFinite($number) -or [Math]::Abs($number) -gt $MaximumMagnitude) {
            throw 'gentest_mp_vector_range'
        }
        $numbers[$i] = $number
    }
    return ,$numbers
}

function ConvertTo-GentestMultiplayerBsp {
    [CmdletBinding()]
    param([Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$SourceBytes)
    if ($SourceBytes.Length -lt 124 -or $SourceBytes.Length -gt 268435456 -or
        [BitConverter]::ToInt32($SourceBytes, 0) -ne 30) { throw 'gentest_mp_bsp_header' }
    $ranges = [Collections.Generic.List[object]]::new()
    for ($i=0; $i -lt 15; $i++) {
        $offset = [BitConverter]::ToInt32($SourceBytes, 4+8*$i)
        $length = [BitConverter]::ToInt32($SourceBytes, 8+8*$i)
        if ($offset -lt 0 -or $length -lt 0 -or $offset -gt $SourceBytes.Length -or
            [long]$offset + $length -gt $SourceBytes.Length -or ($length -gt 0 -and $offset -lt 124)) {
            throw 'gentest_mp_lump_bounds'
        }
        if ($length -gt 0) { $ranges.Add([pscustomobject]@{ Index=$i; Start=$offset; End=[long]$offset+$length }) }
    }
    $ordered = @($ranges | Sort-Object Start)
    for ($i=1; $i -lt $ordered.Count; $i++) {
        if ($ordered[$i].Start -lt $ordered[$i-1].End) { throw 'gentest_mp_lump_overlap' }
    }
    $entityOffset = [BitConverter]::ToInt32($SourceBytes, 4)
    $entityLength = [BitConverter]::ToInt32($SourceBytes, 8)
    if ($entityLength -lt 1 -or $entityLength -gt 131072) { throw 'gentest_mp_entity_lump_bound' }
    if ($SourceBytes[$entityOffset+$entityLength-1] -ne 0) { throw 'gentest_mp_entity_nul' }
    for ($i=0; $i -lt $entityLength-1; $i++) {
        if ($SourceBytes[$entityOffset+$i] -eq 0) { throw 'gentest_mp_entity_interior_nul' }
    }
    $text = [Text.Encoding]::Latin1.GetString($SourceBytes, $entityOffset, $entityLength-1)
    $entities = Read-GentestEntities $text
    if ($entities.Count -eq 0 -or $entities[0].Classname -cne 'worldspawn') { throw 'gentest_mp_worldspawn' }
    if (@($entities | Where-Object Classname -CEQ 'worldspawn').Count -ne 1) { throw 'gentest_mp_worldspawn_count' }
    if (@($entities | Where-Object Classname -CEQ 'info_player_deathmatch').Count -ne 0) { throw 'gentest_mp_existing_deathmatch' }
    $starts = @($entities | Where-Object Classname -CEQ 'info_player_start')
    if ($starts.Count -ne 2) { throw 'gentest_mp_requires_two_authored_starts' }
    $positions = [Collections.Generic.List[object]]::new()
    $origins = [Collections.Generic.List[string]]::new()
    $copies = [Collections.Generic.List[string]]::new()
    foreach ($start in $starts) {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($pair in $start.Pairs) {
            if ($pair.Key -notin @('classname','origin','angles','angle') -or -not $seen.Add($pair.Key)) {
                throw 'gentest_mp_spawn_field_profile'
            }
        }
        $origin = @($start.Pairs | Where-Object Key -EQ 'origin')
        if ($origin.Count -ne 1) { throw 'gentest_mp_spawn_origin' }
        $positions.Add((Read-GentestVector $origin[0].Value))
        $origins.Add($origin[0].Value)
        foreach ($angles in @($start.Pairs | Where-Object Key -EQ 'angles')) {
            [void](Read-GentestVector $angles.Value 3600)
        }
        foreach ($angle in @($start.Pairs | Where-Object Key -EQ 'angle')) {
            $number = 0.0
            if (-not [double]::TryParse($angle.Value, [Globalization.NumberStyles]::Float,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
                -not [double]::IsFinite($number) -or [Math]::Abs($number) -gt 3600) { throw 'gentest_mp_angle_range' }
        }
        $raw = $text.Substring($start.Start, $start.Length)
        $copies.Add($raw.Remove($start.ClassPair.ValueStart-$start.Start, $start.ClassPair.ValueLength).
            Insert($start.ClassPair.ValueStart-$start.Start, 'info_player_deathmatch'))
    }
    $dx = $positions[0][0] - $positions[1][0]
    $dy = $positions[0][1] - $positions[1][1]
    if ($dx*$dx + $dy*$dy -lt 256*256) { throw 'gentest_mp_spawns_too_close' }
    $replacement = [Text.Encoding]::Latin1.GetBytes($text + "`n" + ($copies -join "`n") + "`n`0")
    if ($replacement.Length -gt 131072) { throw 'gentest_mp_output_entity_bound' }
    $newOffset = ($SourceBytes.Length+3) -band -4
    $total = [long]$newOffset + $replacement.Length
    if ($total -gt 268435456) { throw 'gentest_mp_output_bsp_bound' }
    $output = [byte[]]::new([int]$total)
    [Array]::Copy($SourceBytes, $output, $SourceBytes.Length)
    [Array]::Copy($replacement, 0, $output, $newOffset, $replacement.Length)
    [BitConverter]::GetBytes([int]$newOffset).CopyTo($output, 4)
    [BitConverter]::GetBytes([int]$replacement.Length).CopyTo($output, 8)
    return [pscustomobject]@{ Bytes=$output; EntityCountBefore=$entities.Count;
        EntityCountAfter=$entities.Count+2; OriginalStartCount=2; DeathmatchSpawnCount=2;
        SpawnOrigins=$origins.ToArray() }
}

function New-GentestMultiplayerMap {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$ResearchRoot,
          [Parameter(Mandatory)][string]$RepositoryRoot, [switch]$CheckOnly)
    $selected = Resolve-ExternalManualMapBsp $ResearchRoot (Join-Path $ResearchRoot 'valve/maps/gentest.bsp') $RepositoryRoot
    $marker = Join-Path $selected.Root '.hlclient-research-isolated'
    $manifestPath = Join-Path $selected.Root '.hlclient-research-preparation.json'
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or
        -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'gentest_mp_research_not_prepared' }
    foreach ($path in @($marker,$manifestPath)) {
        if (((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'gentest_mp_marker_reparse' }
    }
    if ((Get-Item -LiteralPath $marker).Length -gt 64 -or (Get-Item -LiteralPath $manifestPath).Length -gt 32768 -or
        [IO.File]::ReadAllText($marker).TrimEnd("`r","`n") -cne 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1') {
        throw 'gentest_mp_research_not_prepared'
    }
    $manifest = [IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json -ErrorAction Stop
    if ($manifest.schema -cne 'hlclient.stock-runtime-research-preparation.v3' -or
        $manifest.preparation_status -cne 'exact-materialized-copy-verified') { throw 'gentest_mp_research_not_prepared' }
    $source = [IO.File]::Open($selected.Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($source.Length -lt 124 -or $source.Length -gt 268435456) { throw 'gentest_mp_source_size' }
        $bytes = [byte[]]::new([int]$source.Length)
        $read = 0
        while ($read -lt $bytes.Length) {
            $count = $source.Read($bytes, $read, $bytes.Length-$read)
            if ($count -le 0) { throw 'gentest_mp_source_short_read' }
            $read += $count
        }
        $sourceSha = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
        if ($sourceSha -cne $script:PinnedGentestSha256) { throw 'gentest_mp_source_identity_mismatch' }
        $derived = ConvertTo-GentestMultiplayerBsp $bytes
        $outputSha = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($derived.Bytes))
        $destination = Join-Path $selected.Root 'valve/maps/gentest_mp.bsp'
        $status = 'checked_not_written'
        if (Test-Path -LiteralPath $destination) {
            [void](Resolve-ExternalManualMapBsp $selected.Root $destination $RepositoryRoot)
            if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -cne $outputSha) { throw 'gentest_mp_destination_conflict' }
            $status = 'already_prepared'
        } elseif (-not $CheckOnly) {
            # Fixed sibling basename; source resolution checked every ancestor.
            # CreateNew refuses races/conflicts and never overwrites an asset.
            $file = [IO.File]::Open($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $file.Write($derived.Bytes); $file.Flush($true) } finally { $file.Dispose() }
            [void](Resolve-ExternalManualMapBsp $selected.Root $destination $RepositoryRoot)
            if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -cne $outputSha) { throw 'gentest_mp_write_verification_failed' }
            $status = 'prepared'
        }
        if ((Get-FileHash -LiteralPath $selected.Path -Algorithm SHA256).Hash -cne $sourceSha) { throw 'gentest_mp_original_changed' }
        return [pscustomobject]@{ Status=$status; Map='gentest_mp'; Destination=$destination;
            SourceSha256=$sourceSha; Sha256=$outputSha; DeathmatchSpawnCount=2;
            SpawnOrigins=$derived.SpawnOrigins; OriginalUnchanged=$true; GameProcessesStarted=0 }
    } finally { $source.Dispose() }
}

Export-ModuleMember -Function ConvertTo-GentestMultiplayerBsp,New-GentestMultiplayerMap
