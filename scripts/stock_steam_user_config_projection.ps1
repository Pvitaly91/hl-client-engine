#requires -Version 5.1

Set-StrictMode -Version Latest

$script:StockSteamConfigMaximumBytes = 16MB
$script:StockSteamConfigHardMaximumBytes = 64MB
$script:StockSteamConfigMaximumDepth = 64
$script:StockSteamConfigMaximumNodes = 200000
$script:StockSteamConfigMaximumTokenLength = 65536
# Populated only after the required cross-run diagnostic repetition proves one
# exact changed semantic path set.  It is a path-set fingerprint, never a
# value hash and never public output.
$script:StockSteamAcceptedVolatilePathSetSha256 = ''
$script:StockSteamVolatileClasses = @(
    'playtime_counter', 'last_launch_timestamp', 'last_exit_timestamp',
    'volatile_launch_accounting', 'unrelated_steam_metadata')
$script:StockSteamFatalClasses = @(
    'authentication_or_secret', 'account_identity', 'launch_options',
    'compatibility_tool', 'branch_or_beta', 'language_or_locale',
    'install_or_library_path', 'cloud_state', 'controller_state',
    'network_state', 'application_configuration', 'unknown')

function Get-StockSteamSha256Text {
    param([string]$Text)
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Text)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString(
                $algorithm.ComputeHash($bytes))).Replace('-', '')
    } finally { $algorithm.Dispose() }
}

function Get-StockSteamCanonicalPath {
    param([string[]]$Segments)
    $parts = [Collections.Generic.List[string]]::new()
    foreach ($segment in $Segments) {
        [void]$parts.Add(([string]$segment.Length) + ':' + $segment)
    }
    return $parts -join '|'
}

function Get-StockSteamSemanticClass {
    param([string[]]$Segments, [string]$Kind)
    if ($Kind -cne 'leaf' -or $Segments.Count -lt 1) { return 'unknown' }
    $leaf = $Segments[$Segments.Count - 1]
    if ($Segments.Count -eq 2 -and
        $Segments[0] -ceq 'UserLocalConfigStore' -and
        $leaf -ceq 'AppInfoChangeNumber') {
        return 'unrelated_steam_metadata'
    }
    $appPrefix = @('UserLocalConfigStore', 'Software', 'Valve', 'Steam',
        'apps')
    $underSteamApplication = $Segments.Count -ge ($appPrefix.Count + 2)
    if ($underSteamApplication) {
        for ($index = 0; $index -lt $appPrefix.Count; ++$index) {
            if ($Segments[$index] -cne $appPrefix[$index]) {
                $underSteamApplication = $false
                break
            }
        }
    }
    if ($underSteamApplication -and
        $Segments[$appPrefix.Count] -cnotmatch '^[0-9]{1,10}$') {
        $underSteamApplication = $false
    }
    if (-not $underSteamApplication) { return 'unknown' }
    switch -CaseSensitive ($leaf) {
        'Playtime' { return 'playtime_counter' }
        'Playtime2wks' { return 'playtime_counter' }
        'LastPlayed' { return 'last_launch_timestamp' }
        'LastLaunchTime' { return 'last_launch_timestamp' }
        'LastExitTime' { return 'last_exit_timestamp' }
        'LaunchOptions' { return 'launch_options' }
        'BetaKey' { return 'branch_or_beta' }
        'Branch' { return 'branch_or_beta' }
        'language' { return 'language_or_locale' }
        'Language' { return 'language_or_locale' }
        'CompatTool' { return 'compatibility_tool' }
        'CompatToolMapping' { return 'compatibility_tool' }
        default { return 'unknown' }
    }
}

function ConvertFrom-StockValveKeyValuesBytes {
    param(
        [byte[]]$Bytes,
        [ValidateRange(1, 67108864)]
        [int]$MaximumBytes = $script:StockSteamConfigMaximumBytes)
    if ($MaximumBytes -gt $script:StockSteamConfigHardMaximumBytes -or
        $null -eq $Bytes -or $Bytes.Length -gt $MaximumBytes) {
        throw 'Steam user-config source exceeds its bounded size.'
    }
    $offset = 0
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and
        $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) { $offset = 3 }
    $encoding = [Text.UTF8Encoding]::new($false, $true)
    try { $text = $encoding.GetString($Bytes, $offset, $Bytes.Length - $offset) }
    catch { throw 'Steam user-config encoding is invalid.' }

    $state = [pscustomobject]@{
        Text = $text
        Index = 0
        Nodes = 0
        DuplicatePath = $false
        IsGlobalUserConfig = $false
    }
    $nodes = [Collections.Generic.List[object]]::new()
    $pathCounts = [Collections.Generic.Dictionary[string, int]]::new(
        [StringComparer]::Ordinal)

    function Skip-StockValveTrivia {
        param([object]$State)
        while ($State.Index -lt $State.Text.Length) {
            $value = $State.Text[$State.Index]
            if ($value -eq ' ' -or $value -eq "`t" -or
                $value -eq "`r" -or $value -eq "`n") {
                ++$State.Index
                continue
            }
            if ($value -eq '/' -and $State.Index + 1 -lt $State.Text.Length -and
                $State.Text[$State.Index + 1] -eq '/') {
                $State.Index += 2
                while ($State.Index -lt $State.Text.Length -and
                    $State.Text[$State.Index] -ne "`r" -and
                    $State.Text[$State.Index] -ne "`n") { ++$State.Index }
                continue
            }
            break
        }
    }

    function Read-StockValveQuotedToken {
        param([object]$State)
        Skip-StockValveTrivia $State
        if ($State.Index -ge $State.Text.Length -or
            $State.Text[$State.Index] -ne '"') {
            throw 'Steam user-config requires a quoted token.'
        }
        ++$State.Index
        $builder = [Text.StringBuilder]::new()
        while ($State.Index -lt $State.Text.Length) {
            $value = $State.Text[$State.Index]
            ++$State.Index
            if ($value -eq '"') { return $builder.ToString() }
            if ($value -eq "`r" -or $value -eq "`n" -or [int]$value -eq 0) {
                throw 'Steam user-config token contains an invalid byte shape.'
            }
            if ($value -eq '\') {
                if ($State.Index -ge $State.Text.Length) {
                    throw 'Steam user-config escape is truncated.'
                }
                $escaped = $State.Text[$State.Index]
                ++$State.Index
                if ($escaped -ne '\' -and $escaped -ne '"') {
                    throw 'Steam user-config escape is unsupported.'
                }
                [void]$builder.Append($escaped)
            } else { [void]$builder.Append($value) }
            if ($builder.Length -gt $script:StockSteamConfigMaximumTokenLength) {
                throw 'Steam user-config token exceeds its bound.'
            }
        }
        throw 'Steam user-config quoted token is truncated.'
    }

    function Add-StockValveNode {
        param([string[]]$Path, [string]$Kind, [AllowNull()][string]$Value)
        ++$state.Nodes
        if ($state.Nodes -gt $script:StockSteamConfigMaximumNodes) {
            throw 'Steam user-config node count exceeds its bound.'
        }
        $canonical = Get-StockSteamCanonicalPath $Path
        if ($Path.Count -gt 6 -and
            $Path[0] -ceq 'UserLocalConfigStore' -and
            $Path[1] -ceq 'Software' -and $Path[2] -ceq 'Valve' -and
            $Path[3] -ceq 'Steam' -and $Path[4] -ceq 'apps' -and
            $Path[5] -ceq '70') {
            $state.IsGlobalUserConfig = $true
        }
        $occurrence = 0
        if ($pathCounts.ContainsKey($canonical)) {
            $occurrence = $pathCounts[$canonical]
            $pathCounts[$canonical] = $occurrence + 1
            $state.DuplicatePath = $true
        } else { $pathCounts.Add($canonical, 1) }
        $semantic = Get-StockSteamSemanticClass $Path $Kind
        $shape = if ($Kind -cne 'leaf') { 'object' }
            elseif ($Value -cmatch '^[0-9]+$') { 'unsigned-decimal' }
            else { 'string' }
        $protectedValue = $null
        if ($Kind -ceq 'leaf') {
            $protectedValue = [Security.SecureString]::new()
            foreach ($character in $Value.ToCharArray()) {
                $protectedValue.AppendChar($character)
            }
            $protectedValue.MakeReadOnly()
        }
        [void]$nodes.Add([pscustomobject]@{
                # Raw KeyValues paths may themselves contain account or login
                # identifiers. Publish only an opaque structural identifier.
                path_id = Get-StockSteamSha256Text $canonical
                occurrence = $occurrence
                kind = $Kind
                # Raw leaves are needed only for the in-process before/after
                # comparison.  Keep them non-serializable as plaintext: a
                # diagnostic manifest may retain this value object's shape,
                # but never the source string or a per-leaf digest.
                protected_value = $protectedValue
                value_shape = $shape
                semantic_class = $semantic
            })
    }

    function Read-StockValveObject {
        param([string[]]$Prefix, [int]$Depth, [bool]$RequireClosingBrace)
        if ($Depth -gt $script:StockSteamConfigMaximumDepth) {
            throw 'Steam user-config nesting exceeds its bound.'
        }
        while ($true) {
            Skip-StockValveTrivia $state
            if ($state.Index -ge $state.Text.Length) {
                if ($RequireClosingBrace) {
                    throw 'Steam user-config object is truncated.'
                }
                return
            }
            if ($state.Text[$state.Index] -eq '}') {
                if (-not $RequireClosingBrace) {
                    throw 'Steam user-config has an unmatched closing brace.'
                }
                ++$state.Index
                return
            }
            $key = Read-StockValveQuotedToken $state
            if ([string]::IsNullOrEmpty($key)) {
                throw 'Steam user-config key is empty.'
            }
            [string[]]$path = @($Prefix) + @($key)
            Skip-StockValveTrivia $state
            if ($state.Index -ge $state.Text.Length) {
                throw 'Steam user-config value is missing.'
            }
            if ($state.Text[$state.Index] -eq '{') {
                ++$state.Index
                Add-StockValveNode $path 'object' $null
                Read-StockValveObject $path ($Depth + 1) $true
            } else {
                $value = Read-StockValveQuotedToken $state
                Add-StockValveNode $path 'leaf' $value
            }
        }
    }

    Read-StockValveObject @() 0 $false
    Skip-StockValveTrivia $state
    if ($state.Index -ne $state.Text.Length) {
        throw 'Steam user-config has trailing syntax.'
    }

    $records = [Collections.Generic.List[string]]::new()
    foreach ($node in $nodes) {
        $value = ''
        if ($node.kind -ceq 'leaf') {
            $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR(
                $node.protected_value)
            try {
                $value = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
                    $pointer)
            } finally {
                [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
            }
        }
        $projectedValue = if ($script:StockSteamVolatileClasses -ccontains
            [string]$node.semantic_class) { '<volatile>' } else { $value }
        [void]$records.Add((@($node.path_id, [string]$node.occurrence,
                    $node.kind, $node.value_shape, $node.semantic_class,
                    $projectedValue) -join '|'))
    }
    return [pscustomobject]@{
        status = 'valid'
        entry_class = $(if ([bool]$state.IsGlobalUserConfig) {
                'global_steam_user_config'
            } else { 'other_keyvalues' })
        duplicate_path_ambiguity = [bool]$state.DuplicatePath
        node_count = $nodes.Count
        protected_projection_sha256 = Get-StockSteamSha256Text ($records -join "`n")
        nodes = @($nodes)
    }
}

function Get-StockSteamUserConfigProjection {
    param([string]$Path)
    try {
        $item = Get-Item -LiteralPath $Path -Force
        if ($item.PSIsContainer -or
            ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            $item.Length -gt $script:StockSteamConfigMaximumBytes) {
            return [pscustomobject]@{ status = 'not-applicable'; entry_class = 'none' }
        }
        if (Get-Command Assert-NoReparsePointInExistingPath -ErrorAction SilentlyContinue) {
            Assert-NoReparsePointInExistingPath $Path 'Steam user-config semantic projection'
        }
        if (Get-Command Assert-OnlyDefaultDataStream -ErrorAction SilentlyContinue) {
            Assert-OnlyDefaultDataStream $Path 'Steam user-config semantic projection'
        }
        $bytes = [IO.File]::ReadAllBytes($Path)
        try {
            $projection = ConvertFrom-StockValveKeyValuesBytes $bytes
        } finally {
            if ($null -ne $bytes) { [Array]::Clear($bytes, 0, $bytes.Length) }
        }
        return $projection
    } catch {
        return [pscustomobject]@{ status = 'not-applicable'; entry_class = 'none' }
    }
}

function Compare-StockSteamUserConfigProjection {
    param([object]$Before, [object]$After)
    if ($null -eq $Before -or $null -eq $After -or
        [string]$Before.status -cne 'valid' -or
        [string]$After.status -cne 'valid' -or
        [string]$Before.entry_class -cne 'global_steam_user_config' -or
        [string]$After.entry_class -cne 'global_steam_user_config' -or
        [bool]$Before.duplicate_path_ambiguity -or
        [bool]$After.duplicate_path_ambiguity) {
        return [pscustomobject]@{
            status = 'incomplete'; eligible = $false
            candidate_eligible = $false; changed_leaf_count = 0
            volatile_classes = @(); unknown_changes = 0; fatal_changes = 1
            non_monotonic_changes = 0
            changed_path_set_sha256 = $null
        }
    }
    $beforeMap = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    $afterMap = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    foreach ($node in @($Before.nodes)) {
        $beforeMap.Add($node.path_id + '#' + [string]$node.occurrence, $node)
    }
    foreach ($node in @($After.nodes)) {
        $afterMap.Add($node.path_id + '#' + [string]$node.occurrence, $node)
    }
    [string[]]$keys = @($beforeMap.Keys) + @($afterMap.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    $unique = [Collections.Generic.List[string]]::new()
    $last = $null
    foreach ($key in $keys) {
        if ($null -eq $last -or $key -cne $last) {
            [void]$unique.Add($key)
            $last = $key
        }
    }
    $changedPaths = [Collections.Generic.List[string]]::new()
    $classSet = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    $unknown = 0
    $fatal = 0
    $nonMonotonic = 0
    foreach ($key in $unique) {
        if (-not $beforeMap.ContainsKey($key) -or
            -not $afterMap.ContainsKey($key)) {
            ++$fatal
            [void]$changedPaths.Add($key)
            continue
        }
        $beforeNode = $beforeMap[$key]
        $afterNode = $afterMap[$key]
        if ([string]$beforeNode.kind -cne [string]$afterNode.kind -or
            [string]$beforeNode.value_shape -cne [string]$afterNode.value_shape) {
            ++$fatal
            [void]$changedPaths.Add($key)
            continue
        }
        if ([string]$beforeNode.kind -cne 'leaf') { continue }
        $beforePointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR(
            $beforeNode.protected_value)
        $afterPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR(
            $afterNode.protected_value)
        try {
            $beforeValue = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
                $beforePointer)
            $afterValue = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
                $afterPointer)
        } finally {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($beforePointer)
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($afterPointer)
        }
        if ($beforeValue -ceq $afterValue) { continue }
        [void]$changedPaths.Add($key)
        $semantic = [string]$beforeNode.semantic_class
        if ($semantic -cne [string]$afterNode.semantic_class) {
            ++$fatal
        } elseif ($semantic -ceq 'unknown') {
            ++$unknown
            ++$fatal
        } elseif ($script:StockSteamFatalClasses -ccontains $semantic) {
            ++$fatal
        } elseif ($script:StockSteamVolatileClasses -ccontains $semantic) {
            if ([string]$beforeNode.value_shape -cne 'unsigned-decimal') {
                ++$fatal
            } else {
                [UInt64]$beforeNumber = 0
                [UInt64]$afterNumber = 0
                if (-not [UInt64]::TryParse(
                        $beforeValue, [ref]$beforeNumber) -or
                    -not [UInt64]::TryParse(
                        $afterValue, [ref]$afterNumber) -or
                    $afterNumber -lt $beforeNumber) {
                    ++$nonMonotonic
                    ++$fatal
                } else { [void]$classSet.Add($semantic) }
            }
        } else { ++$fatal }
    }
    [string[]]$classes = @($classSet)
    [Array]::Sort($classes, [StringComparer]::Ordinal)
    [string[]]$pathArray = @($changedPaths)
    [Array]::Sort($pathArray, [StringComparer]::Ordinal)
    $projectionMatch = [string]$Before.protected_projection_sha256 -ceq
        [string]$After.protected_projection_sha256
    $candidateEligible = $changedPaths.Count -gt 0 -and $fatal -eq 0 -and
        $unknown -eq 0 -and $projectionMatch
    $pathSetSha256 = Get-StockSteamSha256Text ($pathArray -join "`n")
    $eligible = $candidateEligible -and
        -not [string]::IsNullOrEmpty(
            $script:StockSteamAcceptedVolatilePathSetSha256) -and
        $pathSetSha256 -ceq $script:StockSteamAcceptedVolatilePathSetSha256
    return [pscustomobject]@{
        status = $(if ($projectionMatch) { 'match' } else { 'mismatch' })
        eligible = $eligible
        candidate_eligible = $candidateEligible
        changed_leaf_count = $changedPaths.Count
        volatile_classes = $classes
        unknown_changes = $unknown
        fatal_changes = $fatal
        non_monotonic_changes = $nonMonotonic
        changed_path_set_sha256 = $pathSetSha256
    }
}
