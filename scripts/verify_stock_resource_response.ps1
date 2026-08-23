#requires -Version 5.1

<#
.SYNOPSIS
Validates isolated stock resource-response research and sanitized evidence.

.DESCRIPTION
This verifier never launches hl.exe, hlds.exe, or the project client. Its
isolation parameter set performs a read-only, fail-closed preflight of a
user-supplied Half-Life research copy. Its projection parameter set reads only
bounded ignored artifacts below manual-artifacts/resource-response-captures.
It refuses to create tracked evidence until every required active scenario is
present and internally consistent.

Raw decoded carriers, provider material, resource names, authentication data,
and following server payloads remain ignored. Only structural metadata may be
written to docs/evidence/GOLDSRC_RESOURCE_CLIENT_RESPONSE_STOCK.json.

.PARAMETER ProjectEvidenceSet
Project a complete ignored evidence corpus. This is the default parameter set.

.PARAMETER ValidateMetadataPath
Validate the exact tracked sanitized projection without rewriting it.

.PARAMETER ValidateMetadataSetRoot
Resolve and validate the projection below the exact docs/evidence directory.

.PARAMETER ValidateResearchRoot
Validate a user-supplied isolated stock research copy. No process is started.

.PARAMETER ResearchHalfLifeRoot
Path to an isolated Half-Life copy with the required marker. Steam libraries,
the primary installation, reparse points, and marker mismatches are rejected.
#>

[CmdletBinding(DefaultParameterSetName = 'Project')]
param(
    [Parameter(ParameterSetName = 'Project')]
    [switch]$ProjectEvidenceSet,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [ValidateNotNullOrEmpty()]
    [string]$ValidateMetadataPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'ValidateSet')]
    [ValidateNotNullOrEmpty()]
    [string]$ValidateMetadataSetRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Isolation')]
    [switch]$ValidateResearchRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Isolation')]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptPath = [IO.Path]::GetFullPath($MyInvocation.MyCommand.Path)
$repositoryRoot = [IO.Path]::GetFullPath(
    (Join-Path (Split-Path -Parent $scriptPath) '..'))
$manualRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'manual-artifacts'))
$captureRoot = [IO.Path]::GetFullPath(
    (Join-Path $manualRoot 'resource-response-captures'))
$projectionRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'docs/evidence'))
$projectionPath = [IO.Path]::GetFullPath(
    (Join-Path $projectionRoot 'GOLDSRC_RESOURCE_CLIENT_RESPONSE_STOCK.json'))

$isolationMarkerName = '.hlclient-research-isolated'
$isolationMarkerText = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
$captureSchema = 'hlclient.stock-resource-response-capture-metadata.v1'
$restorationSchema = 'hlclient.stock-resource-response-restoration.v1'
$projectionSchema = 'hlclient.stock-resource-client-response-evidence.v1'
$stockProfile = 'stock-hl-1.1.1.1-to-stock-hlds-protocol48-build10210'

$maximumCaptureMetadataBytes = 524288
$maximumRunConfigBytes = 16384
$maximumRestorationAttestationBytes = 1024
$maximumProjectionBytes = 1048576
$maximumCarrierBytes = 4096
$maximumConcurrentTailBytes = 256
$maximumPostResponsePayloadBytes = 1048576
$maximumResponseTransmissions = 8
$maximumNextPayloadTransmissions = 8
$maximumAcceptedRuns = 256
$maximumPacketsPerRun = 1024
$maximumDatagramBytes = 4096
$maximumTotalBytesPerRun = 2097152
$maximumTimeoutSeconds = 90

$expectedDescriptorBytes = '01010001000000290000'
$expectedCanonicalSemanticSha256 =
    '451A85ADDBF2B6B2D05E9F424BDFCF711655803706EF6D59B116299D7B5D17C9'

$requiredScenarioMinimums = [ordered]@{
    baseline = 8
    'clean-server-restart' = 3
    reconnect = 3
    'drop-response-datagram' = 2
    'drop-covering-ack' = 2
    'duplicate-response-datagram' = 2
    'drop-first-following-server-payload' = 2
    'local-resource-intact' = 1
    'local-resource-absent' = 1
    'local-resource-changed' = 1
}

function Get-Sha256Hex {
    param([byte[]]$Bytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '')
    }
    finally { $sha.Dispose() }
}

function Get-FileSha256Hex {
    param([string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Get-NormalizedVerifierSha256 {
    $text = [IO.File]::ReadAllText($scriptPath)
    $normalized = $text.Replace("`r`n", "`n").Replace("`r", "`n")
    $encoding = New-Object Text.UTF8Encoding($false)
    return Get-Sha256Hex -Bytes $encoding.GetBytes($normalized)
}

function Assert-NoReparsePoint {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must not be a reparse point."
    }
}

function Assert-NoReparsePointInExistingPath {
    param([string]$Path, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $pathRoot = [IO.Path]::GetPathRoot($fullPath)
    $current = $pathRoot
    foreach ($component in @($fullPath.Substring($pathRoot.Length) -split '[\\/]' |
            Where-Object { $_ })) {
        $current = [IO.Path]::Combine($current, $component)
        if (-not (Test-Path -LiteralPath $current)) { continue }
        Assert-NoReparsePoint -Path $current -Label $Label
    }
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    $streams = @(Get-Item -LiteralPath $Path -Stream * -ErrorAction SilentlyContinue)
    if ($streams.Count -gt 0 -and
        @($streams | Where-Object { $_.Stream -cne ':$DATA' }).Count -ne 0) {
        throw "$Label must contain only the default data stream."
    }
}

function Assert-PathBelowRoot {
    param([string]$Path, [string]$Root, [string]$Label)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith(
            $fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label is outside its exact bounded root."
    }
}

function Assert-ExactProperties {
    param([object]$Value, [string[]]$Allowed, [string]$Label)
    if ($null -eq $Value) { throw "$Label is absent." }
    $actual = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    if ($actual.Count -ne $Allowed.Count) {
        throw "$Label has an unexpected property count."
    }
    foreach ($name in $actual) {
        if ($Allowed -cnotcontains $name) {
            throw "$Label contains unexpected property '$name'."
        }
    }
    foreach ($name in $Allowed) {
        if ($actual -cnotcontains $name) {
            throw "$Label lacks required property '$name'."
        }
    }
}

function Assert-RequiredProperties {
    param([object]$Value, [string[]]$Required, [string]$Label)
    if ($null -eq $Value) { throw "$Label is absent." }
    $actual = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    foreach ($name in $Required) {
        if ($actual -cnotcontains $name) {
            throw "$Label lacks required property '$name'."
        }
    }
}

function Get-StrictBoolean {
    param([object]$Value, [string]$Label)
    if ($Value -isnot [bool]) { throw "$Label must be a JSON boolean." }
    return [bool]$Value
}

function Get-BoundedInteger {
    param(
        [object]$Value,
        [string]$Label,
        [decimal]$Minimum,
        [decimal]$Maximum)
    if ($null -eq $Value -or
        $Value -is [bool] -or
        $Value -is [float] -or
        $Value -is [double] -or
        $Value -is [decimal]) {
        throw "$Label must be an integral JSON number."
    }
    try { $number = [decimal]$Value }
    catch { throw "$Label is not a bounded integer." }
    if ($number -ne [Math]::Truncate($number) -or
        $number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Label is outside its accepted integer range."
    }
    return $number
}

function Assert-Sha256 {
    param([object]$Value, [string]$Label)
    if ($Value -isnot [string] -or
        [string]$Value -cnotmatch '^[0-9A-F]{64}$') {
        throw "$Label must be an uppercase SHA-256 value."
    }
}

function Resolve-ImmediateFile {
    param(
        [string]$Directory,
        [string]$Name,
        [int64]$MaximumBytes,
        [string]$Label)
    if ($Name -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
        throw "$Label has an invalid bounded filename."
    }
    $candidate = [IO.Path]::GetFullPath((Join-Path $Directory $Name))
    Assert-PathBelowRoot -Path $candidate -Root $Directory -Label $Label
    if ([IO.Path]::GetFullPath((Split-Path -Parent $candidate)) -cne
        [IO.Path]::GetFullPath($Directory)) {
        throw "$Label must be an immediate child of its run directory."
    }
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "$Label is absent."
    }
    Assert-NoReparsePoint -Path $candidate -Label $Label
    Assert-OnlyDefaultDataStream -Path $candidate -Label $Label
    $length = (Get-Item -LiteralPath $candidate -Force).Length
    if ($length -lt 1 -or $length -gt $MaximumBytes) {
        throw "$Label is outside its byte bound."
    }
    return $candidate
}

function Read-U16Le {
    param([byte[]]$Bytes, [int]$Offset, [string]$Label)
    if ($Offset -lt 0 -or $Offset + 2 -gt $Bytes.Length) {
        throw "$Label is truncated."
    }
    return [uint16]([uint16]$Bytes[$Offset] -bor
        ([uint16]$Bytes[$Offset + 1] -shl 8))
}

function Read-U32Le {
    param([byte[]]$Bytes, [int]$Offset, [string]$Label)
    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        throw "$Label is truncated."
    }
    return [uint32]([uint32]$Bytes[$Offset] -bor
        ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 3] -shl 24))
}

function Get-CarrierProjection {
    param([string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 54 -or $bytes.Length -gt $maximumCarrierBytes) {
        throw 'Decoded response carrier is outside its exact structural bound.'
    }

    $descriptor = [byte[]]::new(10)
    [Array]::Copy($bytes, 0, $descriptor, 0, 10)
    $descriptorHex = ([BitConverter]::ToString($descriptor)).Replace('-', '')
    if ($descriptorHex -cne $expectedDescriptorBytes) {
        throw 'Response carrier fragment descriptor is outside the confirmed profile.'
    }
    $packedId = Read-U32Le -Bytes $bytes -Offset 1 -Label 'packed fragment ID'
    $fragmentOffset = Read-U16Le -Bytes $bytes -Offset 5 -Label 'fragment offset'
    $fragmentLength = Read-U16Le -Bytes $bytes -Offset 7 -Label 'fragment length'
    if ($packedId -ne 65537 -or $fragmentOffset -ne 0 -or
        $fragmentLength -ne 41 -or $bytes[9] -ne 0) {
        throw 'Response carrier descriptor fields changed.'
    }

    $semanticEnd = 10 + [int]$fragmentLength
    if ($semanticEnd -gt $bytes.Length) {
        throw 'Response carrier selected semantic range overruns the body.'
    }
    $semantic = [byte[]]::new([int]$fragmentLength)
    [Array]::Copy($bytes, 10, $semantic, 0, $semantic.Length)
    if ($semantic[0] -ne 5 -or
        (Read-U16Le -Bytes $semantic -Offset 1 -Label 'response count') -ne 1) {
        throw 'Response semantic opcode/count profile changed.'
    }

    $nameTerminator = -1
    for ($index = 3; $index -lt $semantic.Length; ++$index) {
        if ($semantic[$index] -eq 0) {
            $nameTerminator = $index
            break
        }
    }
    if ($nameTerminator -ne 16) {
        throw 'Response semantic resource-name width changed.'
    }
    for ($index = 3; $index -lt $nameTerminator; ++$index) {
        if ($semantic[$index] -lt 0x20 -or $semantic[$index] -gt 0x7e) {
            throw 'Response semantic resource name is outside the bounded byte profile.'
        }
    }
    if ($semantic[17] -ne 3 -or
        (Read-U16Le -Bytes $semantic -Offset 18 -Label 'response index') -ne 0 -or
        (Read-U32Le -Bytes $semantic -Offset 20 -Label 'response size') -eq 0 -or
        $semantic[24] -ne 4) {
        throw 'Response semantic structural resource fields changed.'
    }

    $tailLength = $bytes.Length - $semanticEnd
    if ($tailLength -lt 3 -or $tailLength -gt $maximumConcurrentTailBytes) {
        throw 'Concurrent response tail is outside its owning bound.'
    }
    $tail = [byte[]]::new($tailLength)
    [Array]::Copy($bytes, $semanticEnd, $tail, 0, $tail.Length)
    if ($tail[0] -ne 2 -or [int]$tail[1] -ne $tail.Length - 3) {
        throw 'Concurrent response tail controls changed.'
    }

    return [pscustomobject]@{
        FullBodyBytes = $bytes.Length
        DescriptorOffset = 0
        DescriptorBytes = 10
        PackedFragmentId = [uint32]$packedId
        FragmentIndex = 1
        FragmentCount = 1
        FragmentOffset = 0
        SemanticOffset = 10
        SemanticBytes = 41
        SemanticSha256 = Get-Sha256Hex -Bytes $semantic
        TailOffset = $semanticEnd
        TailBytes = $tail.Length
        TailSha256 = Get-Sha256Hex -Bytes $tail
    }
}

function Get-ScenarioMinimum {
    param([string]$Name)
    if ($requiredScenarioMinimums.Contains($Name)) {
        return [int]$requiredScenarioMinimums[$Name]
    }
    return 0
}

function Get-EventByOrder {
    param([object[]]$Events, [uint32]$Order, [string]$Label)
    $matches = @($Events | Where-Object { [uint32]$_.order -eq $Order })
    if ($matches.Count -ne 1) { throw "$Label event link is not unique." }
    return $matches[0]
}

function Get-ProjectedScenario {
    param([string]$RunLabel, [string]$RawScenario, [object]$RunConfig)
    switch ($RawScenario) {
        'DropResourceResponse' { return 'drop-response-datagram' }
        'DropResourceResponseAck' { return 'drop-covering-ack' }
        'DuplicateResourceResponse' { return 'duplicate-response-datagram' }
        'DropFirstPostResponseServerFragment' {
            return 'drop-first-following-server-payload'
        }
        'Baseline' {
            if ($RunLabel -cmatch '^m313-baseline(?:-[a-z0-9-]+)?$') {
                return 'baseline'
            }
            if ($RunLabel -cmatch '^m313-restart(?:-[a-z0-9-]+)?$') {
                return 'clean-server-restart'
            }
            if ($RunLabel -cmatch '^m313-reconnect(?:-[a-z0-9-]+)?$') {
                if ([int]$RunConfig.client_ordinal -ne 2) {
                    throw 'Reconnect-labelled run did not capture client ordinal two.'
                }
                return 'reconnect'
            }
            if ($RunLabel -cmatch '^m313-local-intact(?:-[a-z0-9-]+)?$') {
                return 'local-resource-intact'
            }
            if ($RunLabel -cmatch '^m313-local-absent(?:-[a-z0-9-]+)?$') {
                return 'local-resource-absent'
            }
            if ($RunLabel -cmatch '^m313-local-changed(?:-[a-z0-9-]+)?$') {
                return 'local-resource-changed'
            }
            if ($RunLabel -cmatch '^m313-server-profile-a(?:-[a-z0-9-]+)?$') {
                return 'server-consistency-profile-a'
            }
            if ($RunLabel -cmatch '^m313-server-profile-b(?:-[a-z0-9-]+)?$') {
                return 'server-consistency-profile-b'
            }
        }
    }
    throw 'Capture run label/raw scenario pair is not an accepted evidence role.'
}

function Assert-ResponseTransmissions {
    param([object]$Metadata, [string]$RunDirectory, [string]$RawScenario)
    $transmissions = @($Metadata.resource_response_transmissions)
    $postBodies = @($Metadata.post_boundary_client_reliable_transmissions)
    $events = @($Metadata.events)
    if ($transmissions.Count -lt 1 -or
        $transmissions.Count -gt $maximumResponseTransmissions -or
        $postBodies.Count -lt $transmissions.Count -or $postBodies.Count -gt 64) {
        throw 'Response transmission/body count is outside its bound.'
    }
    $bodyFiles = @(Get-ChildItem -LiteralPath $RunDirectory -Force -File |
        Where-Object { $_.Name -cmatch
                '^research-post-boundary-client-[0-9]{3}\.bin$' } |
        Sort-Object Name)
    if ($bodyFiles.Count -ne $postBodies.Count) {
        throw 'Decoded client-body file/metadata counts disagree.'
    }
    for ($bodyIndex = 0; $bodyIndex -lt $bodyFiles.Count; ++$bodyIndex) {
        $expectedName = 'research-post-boundary-client-{0:D3}.bin' -f
            ($bodyIndex + 1)
        if ($bodyFiles[$bodyIndex].Name -cne $expectedName -or
            $bodyFiles[$bodyIndex].Length -lt 1 -or
            $bodyFiles[$bodyIndex].Length -gt $maximumCarrierBytes) {
            throw 'Decoded client-body file sequence/bound is invalid.'
        }
        Assert-NoReparsePoint -Path $bodyFiles[$bodyIndex].FullName `
            -Label 'decoded client body'
        Assert-OnlyDefaultDataStream -Path $bodyFiles[$bodyIndex].FullName `
            -Label 'decoded client body'
    }

    $duplicateFlag = Get-StrictBoolean `
        $Metadata.duplicate_resource_response_datagram_forwarded `
        'duplicate response datagram flag'
    $duplicateActions = @($Metadata.actions | Where-Object {
            [string]$_.action -ceq 'forward-first-resource-response-twice' })
    $dropActions = @($Metadata.actions | Where-Object {
            [string]$_.action -ceq
                'drop-first-resource-response-transmission' })
    if ($RawScenario -ceq 'DuplicateResourceResponse') {
        if (-not $duplicateFlag -or $duplicateActions.Count -ne 1) {
            throw 'Duplicate-response scenario lacks its exact relay mutation.'
        }
    }
    elseif ($duplicateFlag -or $duplicateActions.Count -ne 0) {
        throw 'Response duplication occurred outside its bounded scenario.'
    }
    if ($RawScenario -ceq 'DropResourceResponse') {
        if ($dropActions.Count -ne 1) {
            throw 'Drop-response scenario lacks its exact relay mutation.'
        }
    }
    elseif ($dropActions.Count -ne 0) {
        throw 'Response datagram was dropped outside its bounded scenario.'
    }

    $projections = [Collections.Generic.List[object]]::new()
    $sequences = [Collections.Generic.HashSet[uint32]]::new()
    foreach ($transmission in $transmissions) {
        Assert-ExactProperties -Value $transmission -Allowed @(
            'event_order', 'sequence', 'acknowledgement',
            'descriptor_area_bytes', 'descriptor_hex', 'semantic_bytes',
            'semantic_sha256', 'opcode', 'entry_count',
            'concurrent_tail_bytes', 'concurrent_tail_sha256', 'forwarded') `
            -Label 'response transmission'
        $eventOrder = [uint32](Get-BoundedInteger $transmission.event_order `
            'response transmission event order' 1 $maximumPacketsPerRun)
        $sequence = [uint32](Get-BoundedInteger $transmission.sequence `
            'response transmission sequence' 0 1073741823)
        [void](Get-BoundedInteger $transmission.acknowledgement `
            'response transmission acknowledgement' 0 1073741823)
        if (-not $sequences.Add($sequence)) {
            throw 'Response retransmission did not use a fresh numeric sequence.'
        }

        $postMatches = @()
        for ($postIndex = 0; $postIndex -lt $postBodies.Count; ++$postIndex) {
            if ([uint32]$postBodies[$postIndex].event_order -eq $eventOrder) {
                $postMatches += [pscustomobject]@{
                    Index = $postIndex
                    Value = $postBodies[$postIndex]
                }
            }
        }
        if ($postMatches.Count -ne 1) {
            throw 'Response transmission lacks one decoded-body evidence link.'
        }
        $post = $postMatches[0].Value
        Assert-ExactProperties -Value $post -Allowed @(
            'event_order', 'sequence', 'decoded_body_bytes',
            'decoded_body_sha256') -Label 'decoded client body summary'
        Assert-Sha256 $post.decoded_body_sha256 'decoded client body hash'
        if ([uint32]$post.sequence -ne $sequence) {
            throw 'Response transmission/body sequence link changed.'
        }
        $fileName = 'research-post-boundary-client-{0:D3}.bin' -f
            ($postMatches[0].Index + 1)
        $carrierPath = Resolve-ImmediateFile -Directory $RunDirectory `
            -Name $fileName -MaximumBytes $maximumCarrierBytes `
            -Label 'decoded response carrier'
        $carrier = Get-CarrierProjection -Path $carrierPath
        Assert-Sha256 $transmission.semantic_sha256 'response semantic hash'
        Assert-Sha256 $transmission.concurrent_tail_sha256 `
            'response concurrent-tail hash'
        if ((Get-FileSha256Hex -Path $carrierPath) -cne
                [string]$post.decoded_body_sha256 -or
            $carrier.FullBodyBytes -ne [int]$post.decoded_body_bytes -or
            [int]$transmission.descriptor_area_bytes -ne
                $carrier.DescriptorBytes -or
            [string]$transmission.descriptor_hex -cne $expectedDescriptorBytes -or
            [int]$transmission.semantic_bytes -ne $carrier.SemanticBytes -or
            [string]$transmission.semantic_sha256 -cne
                $carrier.SemanticSha256 -or
            [int]$transmission.opcode -ne 5 -or
            [int]$transmission.entry_count -ne 1 -or
            [int]$transmission.concurrent_tail_bytes -ne $carrier.TailBytes -or
            [string]$transmission.concurrent_tail_sha256 -cne
                $carrier.TailSha256) {
            throw 'Response relay metadata disagrees with the decoded carrier.'
        }

        $event = Get-EventByOrder -Events $events -Order $eventOrder `
            -Label 'response transmission'
        $generation = Get-StrictBoolean $event.reliable_present `
            'response reliable generation'
        if ([string]$event.direction -cne 'c2s' -or
            [string]$event.class -cne 'sequenced' -or
            [uint32]$event.sequence -ne $sequence -or
            -not (Get-StrictBoolean $event.fragmented `
                'response fragmented flag')) {
            throw 'Response event transport identity is inconsistent.'
        }
        $forwarded = Get-StrictBoolean $transmission.forwarded `
            'response forwarded flag'
        $forwardCount = if ($RawScenario -ceq 'DuplicateResourceResponse' -and
            $eventOrder -eq [uint32]$duplicateActions[0].event_order) { 2 }
        elseif ($forwarded) { 1 }
        else { 0 }
        $projections.Add([pscustomobject]@{
            EventOrder = $eventOrder
            Sequence = $sequence
            ReliableGeneration = $generation
            ForwardCount = $forwardCount
            Carrier = $carrier
        })
    }

    $ordered = @($projections | Sort-Object EventOrder)
    for ($index = 1; $index -lt $ordered.Count; ++$index) {
        if ($ordered[$index].EventOrder -le $ordered[$index - 1].EventOrder -or
            $ordered[$index].ReliableGeneration -ne
                $ordered[0].ReliableGeneration -or
            $ordered[$index].Carrier.SemanticSha256 -cne
                $ordered[0].Carrier.SemanticSha256) {
            throw 'Response retransmission identity/generation is inconsistent.'
        }
    }
    switch ($RawScenario) {
        'DropResourceResponse' {
            if ($ordered.Count -lt 2 -or $ordered[0].ForwardCount -ne 0 -or
                [uint32]$dropActions[0].event_order -ne $ordered[0].EventOrder -or
                @($ordered | Select-Object -Skip 1 |
                    Where-Object ForwardCount -ge 1).Count -lt 1) {
                throw 'Drop-response scenario lacks its bounded retry.'
            }
        }
        'DropResourceResponseAck' {
            if ($ordered.Count -lt 2 -or
                @($ordered | Where-Object ForwardCount -ne 1).Count -ne 0) {
                throw 'Drop-ACK scenario lacks forwarded transport retry.'
            }
        }
        'DuplicateResourceResponse' {
            if ($ordered.Count -ne 1 -or $ordered[0].ForwardCount -ne 2) {
                throw 'Duplicate-response scenario carrier count changed.'
            }
        }
        default {
            if ($ordered.Count -ne 1 -or $ordered[0].ForwardCount -ne 1) {
                throw 'Baseline response transport count changed.'
            }
        }
    }
    return $ordered
}

function Assert-CoveringAck {
    param([object]$Metadata, [object[]]$Transmissions, [string]$RawScenario)
    $rawAcks = @($Metadata.resource_response_acknowledgements |
        Sort-Object { [uint32]$_.event_order })
    if ($rawAcks.Count -lt 1 -or $rawAcks.Count -gt 64) {
        throw 'Covering ACK count is outside its bound.'
    }
    $firstForwardedRaw = @($rawAcks | Where-Object { $_.forwarded -eq $true } |
        Select-Object -First 1)
    if ($firstForwardedRaw.Count -ne 1) {
        throw 'Run lacks a forwarded covering ACK candidate.'
    }
    $firstForwardedOrder = [uint32]$firstForwardedRaw[0].event_order
    $acks = @($rawAcks | Where-Object {
            [uint32]$_.event_order -le $firstForwardedOrder })
    $events = @($Metadata.events)
    $dropActions = @($Metadata.actions | Where-Object {
            [string]$_.action -ceq
                'drop-first-covering-resource-response-ack-packet' })
    $projected = [Collections.Generic.List[object]]::new()
    foreach ($ack in $acks) {
        Assert-ExactProperties -Value $ack -Allowed @(
            'event_order', 'server_sequence', 'acknowledgement',
            'reliable_ack', 'fragmented', 'decoded_body_bytes', 'forwarded') `
            -Label 'covering ACK candidate'
        $eventOrder = [uint32](Get-BoundedInteger $ack.event_order `
            'covering ACK event order' 1 $maximumPacketsPerRun)
        $serverSequence = [uint32](Get-BoundedInteger $ack.server_sequence `
            'covering ACK server sequence' 0 1073741823)
        $acknowledgement = [uint32](Get-BoundedInteger $ack.acknowledgement `
            'covering ACK numeric value' 0 1073741823)
        $generation = Get-StrictBoolean $ack.reliable_ack `
            'covering ACK generation'
        if ((Get-BoundedInteger $ack.decoded_body_bytes `
                'covering ACK decoded body bytes' 8 8) -ne 8 -or
            (Get-StrictBoolean $ack.fragmented 'covering ACK fragment flag')) {
            throw 'Covering ACK is not the confirmed padding-only packet.'
        }
        $event = Get-EventByOrder -Events $events -Order $eventOrder `
            -Label 'covering ACK'
        if ([string]$event.direction -cne 's2c' -or
            [string]$event.class -cne 'sequenced' -or
            [uint32]$event.sequence -ne $serverSequence -or
            [uint32]$event.acknowledgement -ne $acknowledgement -or
            (Get-StrictBoolean $event.reliable_ack `
                'covering ACK event generation') -ne $generation -or
            [int]$event.decoded_body_bytes -ne 8) {
            throw 'Covering ACK event link is inconsistent.'
        }
        $projected.Add([pscustomobject]@{
            EventOrder = $eventOrder
            ServerSequence = $serverSequence
            Acknowledgement = $acknowledgement
            ReliableGeneration = $generation
            Forwarded = Get-StrictBoolean $ack.forwarded `
                'covering ACK forwarded flag'
        })
    }
    $ordered = @($projected | Sort-Object EventOrder)
    if ($RawScenario -ceq 'DropResourceResponseAck') {
        if ($dropActions.Count -ne 1 -or $ordered.Count -lt 2 -or
            [uint32]$dropActions[0].event_order -ne $ordered[0].EventOrder -or
            $ordered[0].Forwarded -or
            @($ordered | Select-Object -Skip 1 | Where-Object Forwarded).Count -lt 1) {
            throw 'Drop-ACK scenario lacks a later forwarded covering ACK.'
        }
    }
    elseif ($dropActions.Count -ne 0 -or
        @($ordered | Where-Object { -not $_.Forwarded }).Count -ne 0) {
        throw 'Covering ACK was dropped outside its bounded scenario.'
    }
    $covering = @($ordered | Where-Object Forwarded | Select-Object -First 1)
    if ($covering.Count -ne 1) { throw 'Run lacks a forwarded covering ACK.' }
    $latest = @($Transmissions | Where-Object {
            $_.ForwardCount -ge 1 -and $_.EventOrder -lt $covering[0].EventOrder
        } | Sort-Object EventOrder | Select-Object -Last 1)
    if ($latest.Count -ne 1 -or
        $covering[0].Acknowledgement -lt $latest[0].Sequence -or
        $covering[0].ReliableGeneration -ne $latest[0].ReliableGeneration) {
        throw 'ACK does not cover the latest forwarded response transmission.'
    }
    return [pscustomobject]@{
        EventOrder = $covering[0].EventOrder
        ServerSequence = $covering[0].ServerSequence
        Acknowledgement = $covering[0].Acknowledgement
        ReliableGeneration = $covering[0].ReliableGeneration
        CoverDelta = [uint32](
            $covering[0].Acknowledgement - $latest[0].Sequence)
    }
}

function Get-FragmentSlot {
    param([object]$Event, [int]$Index, [string]$Label)
    $matches = @($Event.fragment_slots | Where-Object {
            $_.present -eq $true -and [int]$_.packed_index -eq $Index })
    if ($matches.Count -ne 1) { throw "$Label fragment slot is not unique." }
    return $matches[0]
}

function Assert-NextServerBoundary {
    param(
        [object]$Metadata,
        [string]$RunDirectory,
        [string]$RawScenario,
        [uint32]$ResponseEventOrder)
    $boundary = $Metadata.post_response_service_boundary
    Assert-ExactProperties -Value $boundary -Allowed @(
        'envelope', 'compressed_transfer_bytes', 'trailing_compressed_bytes',
        'service_payload_bytes', 'first_opcode', 'first_opcode_offset',
        'first_opcode_body_unconsumed', 'service_payload_sha256') `
        -Label 'post-response service boundary'
    if ([string]$boundary.envelope -cne 'BZ2-NUL-plus-standard-bzip2' -or
        [int]$boundary.trailing_compressed_bytes -ne 0 -or
        [int]$boundary.first_opcode_offset -ne 0 -or
        -not (Get-StrictBoolean $boundary.first_opcode_body_unconsumed `
            'next server body-unconsumed flag')) {
        throw 'Post-response server boundary contract changed.'
    }

    $transferPath = Resolve-ImmediateFile -Directory $RunDirectory `
        -Name 'research-post-response-transfer.bin' `
        -MaximumBytes $maximumPostResponsePayloadBytes `
        -Label 'post-response compressed transfer'
    $payloadPath = Resolve-ImmediateFile -Directory $RunDirectory `
        -Name 'research-post-response-service-payload.bin' `
        -MaximumBytes $maximumPostResponsePayloadBytes `
        -Label 'post-response service payload'
    $payload = [IO.File]::ReadAllBytes($payloadPath)
    Assert-Sha256 $boundary.service_payload_sha256 `
        'post-response service payload hash'
    if ((Get-Item -LiteralPath $transferPath -Force).Length -ne
            [int]$boundary.compressed_transfer_bytes -or
        $payload.Length -ne [int]$boundary.service_payload_bytes -or
        (Get-Sha256Hex -Bytes $payload) -cne
            [string]$boundary.service_payload_sha256 -or
        [int]$payload[0] -ne [int]$boundary.first_opcode) {
        throw 'Post-response server payload geometry/hash/opcode changed.'
    }

    $allTransfers = @($Metadata.transfers)
    $transferOrdinals = @($allTransfers | ForEach-Object { [int]$_.ordinal } |
        Sort-Object)
    $thirdTransfers = @($allTransfers | Where-Object {
            [int]$_.ordinal -eq 3 })
    if ($allTransfers.Count -ne 3 -or
        ($transferOrdinals -join ',') -cne '1,2,3' -or
        $thirdTransfers.Count -ne 1) {
        throw 'Next server transfer is not one exact third transfer.'
    }
    $transfer = $thirdTransfers[0]
    Assert-RequiredProperties -Value $transfer -Required @(
        'ordinal', 'stream', 'declared_count', 'reassembled_bytes',
        'reassembled_sha256', 'observed_in_index_order') `
        -Label 'post-response transfer summary'
    Assert-Sha256 $transfer.reassembled_sha256 'post-response transfer hash'
    $declaredCount = [int](Get-BoundedInteger $transfer.declared_count `
        'post-response fragment count' 1 64)
    if ([int]$transfer.stream -ne 0 -or
        [int]$transfer.reassembled_bytes -ne
            (Get-Item -LiteralPath $transferPath -Force).Length -or
        [string]$transfer.reassembled_sha256 -cne
            (Get-FileSha256Hex -Path $transferPath) -or
        -not (Get-StrictBoolean $transfer.observed_in_index_order `
            'post-response fragment order')) {
        throw 'Post-response transfer summary disagrees with raw evidence.'
    }

    $ackLinks = @($Metadata.fragment_acknowledgements | Where-Object {
            [int]$_.transfer_ordinal -eq 3 })
    if ($ackLinks.Count -ne $declaredCount) {
        throw 'Post-response transfer lacks one ACK link per accepted fragment.'
    }
    $events = @($Metadata.events)
    $acceptedSequences = [Collections.Generic.HashSet[uint32]]::new()
    $firstAcceptedEvent = [uint32]::MaxValue
    foreach ($index in 1..$declaredCount) {
        $links = @($ackLinks | Where-Object { [int]$_.fragment_index -eq $index })
        if ($links.Count -ne 1) {
            throw 'Post-response fragment index/ACK coverage is incomplete.'
        }
        $eventOrder = [uint32](Get-BoundedInteger $links[0].fragment_event_order `
            'post-response fragment event order' 1 $maximumPacketsPerRun)
        $event = Get-EventByOrder -Events $events -Order $eventOrder `
            -Label 'post-response fragment'
        $slot = Get-FragmentSlot -Event $event -Index $index `
            -Label 'post-response'
        Assert-Sha256 $slot.payload_sha256 'post-response fragment payload hash'
        if ([string]$event.direction -cne 's2c' -or
            [string]$event.class -cne 'sequenced' -or
            -not (Get-StrictBoolean $event.fragmented `
                'post-response fragmented flag') -or
            [int]$slot.packed_count -ne $declaredCount -or
            -not $acceptedSequences.Add([uint32]$event.sequence)) {
            throw 'Post-response fragment transport identity is inconsistent.'
        }
        if ($eventOrder -lt $firstAcceptedEvent) { $firstAcceptedEvent = $eventOrder }
    }
    if ($firstAcceptedEvent -lt $ResponseEventOrder) {
        throw 'Next server transfer precedes the client response.'
    }

    $dropActions = @($Metadata.actions | Where-Object {
            [string]$_.action -ceq
                'drop-first-post-response-server-fragment' })
    $retransmitted = $false
    if ($RawScenario -ceq 'DropFirstPostResponseServerFragment') {
        if ($dropActions.Count -ne 1) {
            throw 'Drop-next scenario lacks its exact relay mutation.'
        }
        $dropOrder = [uint32](Get-BoundedInteger $dropActions[0].event_order `
            'dropped post-response event order' 1 $maximumPacketsPerRun)
        $droppedEvent = Get-EventByOrder -Events $events -Order $dropOrder `
            -Label 'dropped post-response fragment'
        $droppedSlot = Get-FragmentSlot -Event $droppedEvent -Index 1 `
            -Label 'dropped post-response'
        $firstLink = @($ackLinks | Where-Object { [int]$_.fragment_index -eq 1 })[0]
        $retryEvent = Get-EventByOrder -Events $events `
            -Order ([uint32]$firstLink.fragment_event_order) `
            -Label 'retried post-response fragment'
        $retrySlot = Get-FragmentSlot -Event $retryEvent -Index 1 `
            -Label 'retried post-response'
        if ($dropOrder -ge [uint32]$firstLink.fragment_event_order -or
            [uint32]$droppedEvent.sequence -eq [uint32]$retryEvent.sequence -or
            [string]$droppedSlot.payload_sha256 -cne
                [string]$retrySlot.payload_sha256) {
            throw 'Dropped next-server fragment lacks a fresh-sequence retry.'
        }
        $retransmitted = $true
    }
    elseif ($dropActions.Count -ne 0) {
        throw 'Post-response fragment was dropped outside its scenario.'
    }

    return [pscustomobject]@{
        EventOrder = $firstAcceptedEvent
        PayloadBytes = $payload.Length
        PayloadSha256 = [string]$boundary.service_payload_sha256
        Opcode = [int]$boundary.first_opcode
        OpcodeOffset = 0
        TransmissionCount = if ($retransmitted) { 2 } else { 1 }
    }
}

function Get-RunProjection {
    param([string]$RunDirectory)
    $runId = Split-Path -Leaf $RunDirectory
    if ($runId -cnotmatch
        '^(?<label>m313-[a-z0-9-]{1,120})-(?<raw>[a-z]+)-[0-9]{8}-[0-9]{6}-[0-9]{3}$') {
        throw 'Capture run ID is outside the sanitized allowlist.'
    }
    $runLabel = [string]$Matches['label']
    $rawScenarioToken = [string]$Matches['raw']

    $attestationPath = Resolve-ImmediateFile -Directory $RunDirectory `
        -Name 'research-restoration-attestation.json' `
        -MaximumBytes $maximumRestorationAttestationBytes `
        -Label 'research restoration attestation'
    $attestation = Get-Content -Raw -LiteralPath $attestationPath |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ExactProperties -Value $attestation -Allowed @(
        'schema', 'external_file_drift', 'isolated_copy',
        'snapshot_entry_count', 'restored_entry_count',
        'pre_manifest_sha256', 'post_manifest_sha256',
        'created_file_count', 'created_file_removal_count',
        'local_mutation_target_restored') `
        -Label 'research restoration attestation'
    if ([string]$attestation.schema -cne $restorationSchema -or
        [string]$attestation.external_file_drift -cne 'none' -or
        -not (Get-StrictBoolean $attestation.isolated_copy `
            'restoration isolated-copy flag')) {
        throw 'Run lacks a successful isolated-copy restoration attestation.'
    }
    $snapshotEntryCount = [int](Get-BoundedInteger `
        $attestation.snapshot_entry_count 'restoration snapshot entry count' `
        1 4096)
    $restoredEntryCount = [int](Get-BoundedInteger `
        $attestation.restored_entry_count 'restoration restored entry count' `
        1 4096)
    $createdFileCount = [int](Get-BoundedInteger `
        $attestation.created_file_count 'restoration created-file count' `
        0 4096)
    $createdFileRemovalCount = [int](Get-BoundedInteger `
        $attestation.created_file_removal_count `
        'restoration created-file removal count' 0 4096)
    Assert-Sha256 $attestation.pre_manifest_sha256 `
        'restoration pre-run manifest hash'
    Assert-Sha256 $attestation.post_manifest_sha256 `
        'restoration post-run manifest hash'
    $localMutationTargetRestored = Get-StrictBoolean `
        $attestation.local_mutation_target_restored `
        'restoration local-mutation-target flag'
    if ($restoredEntryCount -ne $snapshotEntryCount -or
        $createdFileRemovalCount -ne $createdFileCount -or
        [string]$attestation.pre_manifest_sha256 -cne
            [string]$attestation.post_manifest_sha256) {
        throw 'Run restoration counts or pre/post manifest hashes disagree.'
    }

    $configPath = Resolve-ImmediateFile -Directory $RunDirectory `
        -Name 'research-run-config.json' -MaximumBytes $maximumRunConfigBytes `
        -Label 'research run config'
    $config = Get-Content -Raw -LiteralPath $configPath |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ExactProperties -Value $config -Allowed @(
        'map', 'prime_map', 'max_players', 'client_ordinal',
        'disconnected_first_client_before_capture', 'first_client_seconds',
        'first_client_active_count', 'first_client_user_ids',
        'post_capture_active_count', 'post_capture_user_ids', 'hostname',
        'synthetic_player_name', 'player_model', 'top_color', 'bottom_color',
        'cvar_name', 'cvar_value', 'cvar_rcon_applied',
        'cvar_console_evidence', 'scenario', 'server_port', 'relay_port') `
        -Label 'research run config'

    $metadataPath = Resolve-ImmediateFile -Directory $RunDirectory `
        -Name 'metadata.json' -MaximumBytes $maximumCaptureMetadataBytes `
        -Label 'capture metadata'
    $metadata = Get-Content -Raw -LiteralPath $metadataPath |
        ConvertFrom-Json -ErrorAction Stop
    Assert-RequiredProperties -Value $metadata -Required @(
        'schema', 'profile', 'scenario', 'completion', 'loopback_only',
        'byte_preserving_relay', 'same_upstream_socket',
        'exact_server_endpoint_validation',
        'client_endpoint_learned_from_canonical_getchallenge',
        'raw_packet_bytes_stored', 'packet_count', 'post_accept_packet_count',
        'total_bytes', 'maximum_packets', 'maximum_post_accept_packets',
        'maximum_datagram_bytes', 'maximum_total_bytes', 'timeout_seconds',
        'connect_seen', 'accept_seen', 'scenario_mutation_count',
        'ignored_wrong_source_count', 'held_packet_at_end',
        'post_boundary_client_reliable_transmissions',
        'resource_response_transmissions',
        'resource_response_acknowledgements',
        'duplicate_resource_response_datagram_forwarded',
        'post_response_service_boundary', 'transfers',
        'fragment_acknowledgements', 'events', 'actions') `
        -Label 'capture metadata'
    if ([string]$metadata.schema -cne $captureSchema -or
        [string]$metadata.profile -cne $stockProfile -or
        @('bounded_complete', 'scenario_incomplete') -cnotcontains
            [string]$metadata.completion) {
        throw 'Capture identity or bounded completion profile is invalid.'
    }
    foreach ($field in @(
            'loopback_only', 'byte_preserving_relay', 'same_upstream_socket',
            'exact_server_endpoint_validation',
            'client_endpoint_learned_from_canonical_getchallenge',
            'connect_seen', 'accept_seen')) {
        if (-not (Get-StrictBoolean $metadata.$field "capture $field")) {
            throw "Capture requires $field=true."
        }
    }
    foreach ($field in @('raw_packet_bytes_stored', 'held_packet_at_end')) {
        if (Get-StrictBoolean $metadata.$field "capture $field") {
            throw "Capture requires $field=false."
        }
    }
    $packetBound = [int](Get-BoundedInteger $metadata.maximum_packets `
        'capture packet bound' 1 $maximumPacketsPerRun)
    $postAcceptBound = [int](Get-BoundedInteger `
        $metadata.maximum_post_accept_packets 'post-accept packet bound' `
        1 $maximumPacketsPerRun)
    $byteBound = [int](Get-BoundedInteger $metadata.maximum_total_bytes `
        'capture total-byte bound' 1 $maximumTotalBytesPerRun)
    [void](Get-BoundedInteger $metadata.maximum_datagram_bytes `
        'capture datagram bound' 576 $maximumDatagramBytes)
    [void](Get-BoundedInteger $metadata.timeout_seconds `
        'capture timeout bound' 1 $maximumTimeoutSeconds)
    $packetCount = [int](Get-BoundedInteger $metadata.packet_count `
        'capture packet count' 1 $packetBound)
    [void](Get-BoundedInteger $metadata.post_accept_packet_count `
        'capture post-accept packet count' 1 $postAcceptBound)
    [void](Get-BoundedInteger $metadata.total_bytes `
        'capture total bytes' 1 $byteBound)
    [void](Get-BoundedInteger $metadata.ignored_wrong_source_count `
        'capture rejected-source count' 0 4)
    if ($packetCount -gt $packetBound) {
        throw 'Capture exceeded a declared packet/byte/source bound.'
    }
    $events = @($metadata.events)
    if ($events.Count -ne $packetCount -or
        $events.Count -gt $maximumPacketsPerRun) {
        throw 'Capture event ledger count is inconsistent.'
    }

    $rawScenario = [string]$metadata.scenario
    if ([string]$config.scenario -cne $rawScenario -or
        $rawScenario.ToLowerInvariant() -cne $rawScenarioToken -or
        @('Baseline', 'DropResourceResponse', 'DropResourceResponseAck',
            'DuplicateResourceResponse',
            'DropFirstPostResponseServerFragment') -cnotcontains $rawScenario) {
        throw 'Capture scenario identity is inconsistent or out of scope.'
    }
    $scenario = Get-ProjectedScenario -RunLabel $runLabel `
        -RawScenario $rawScenario -RunConfig $config
    $map = [string]$config.map
    if (@('boot_camp', 'crossfire', 'stalkyard') -cnotcontains $map) {
        throw 'Capture map is outside the required evidence profiles.'
    }
    [void](Get-BoundedInteger $config.max_players `
        'research max players' 2 32)
    [void](Get-BoundedInteger $config.client_ordinal `
        'research client ordinal' 1 2)
    [void](Get-BoundedInteger $config.server_port `
        'research server port' 1024 65534)
    [void](Get-BoundedInteger $config.relay_port `
        'research relay port' 1024 65534)
    [void](Get-StrictBoolean `
        $config.disconnected_first_client_before_capture `
        'research first-client disconnect flag')
    $cvarApplied = Get-StrictBoolean $config.cvar_rcon_applied `
        'research cvar-applied flag'
    $serverProfile = $scenario -ceq 'server-consistency-profile-a' -or
        $scenario -ceq 'server-consistency-profile-b'
    if ($serverProfile) {
        if (-not $cvarApplied -or
            [string]::IsNullOrWhiteSpace([string]$config.cvar_name) -or
            [string]::IsNullOrWhiteSpace([string]$config.cvar_value) -or
            $null -eq $config.cvar_console_evidence) {
            throw 'Server-consistency run lacks confirmed applied cvar evidence.'
        }
    }
    elseif ($cvarApplied -or [string]$config.cvar_name -cne '' -or
        [string]$config.cvar_value -cne '') {
        throw 'Non-consistency capture contains a server-cvar differential.'
    }
    $expectedMutationCount = if ($rawScenario -ceq 'Baseline') { 0 } else { 1 }
    $mutationCount = [int](Get-BoundedInteger `
        $metadata.scenario_mutation_count 'capture relay mutation count' 0 1)
    if ($mutationCount -ne $expectedMutationCount) {
        throw 'Capture relay mutation count disagrees with its scenario.'
    }
    $variant = switch ($scenario) {
        'local-resource-intact' { 'intact' }
        'local-resource-absent' { 'absent' }
        'local-resource-changed' { 'changed-one-byte' }
        default { 'unchanged' }
    }

    $requiresLocalMutationRestoration = @(
        'local-resource-absent', 'local-resource-changed') -ccontains $scenario
    if ($localMutationTargetRestored -ne $requiresLocalMutationRestoration) {
        throw 'Local-mutation restoration proof disagrees with the projected scenario.'
    }

    $responseMetadata = $metadata.PSObject.Properties[
        'resource_response_transmissions'].Value
    $responseObserved = @($responseMetadata).Count -ne 0
    if (-not $responseObserved) {
        if (-not $requiresLocalMutationRestoration -or
            $rawScenario -cne 'Baseline' -or
            [string]$metadata.completion -cne 'scenario_incomplete') {
            throw 'Only bounded absent/changed local differentials may omit a response.'
        }
        foreach ($field in @(
                'resource_response_transmissions',
                'resource_response_acknowledgements')) {
            $value = $metadata.PSObject.Properties[$field].Value
            if ($value -isnot [Array] -or @($value).Count -ne 0) {
                throw "Bounded no-response differential requires empty $field."
            }
        }
        if ($null -ne $metadata.post_response_service_boundary -or
            (Get-StrictBoolean `
                $metadata.duplicate_resource_response_datagram_forwarded `
                'duplicate response datagram flag')) {
            throw 'Bounded no-response differential contains response continuation state.'
        }
        $actionMetadata = $metadata.PSObject.Properties['actions'].Value
        if ($actionMetadata -isnot [Array]) {
            throw 'Bounded no-response differential requires an action array.'
        }
        $responseActions = @($actionMetadata | Where-Object {
                @(
                    'drop-first-resource-response-transmission',
                    'forward-first-resource-response-twice',
                    'drop-first-covering-resource-response-ack-packet',
                    'drop-first-post-response-server-fragment') -ccontains
                    [string]$_.action
            })
        if ($responseActions.Count -ne 0) {
            throw 'Bounded no-response differential contains a response mutation action.'
        }
        $postResponseFiles = @(Get-ChildItem -LiteralPath $RunDirectory -Force -File |
            Where-Object { $_.Name -cmatch '^research-post-response-' })
        if ($postResponseFiles.Count -ne 0) {
            throw 'Bounded no-response differential contains post-response raw evidence.'
        }
        return [pscustomobject]@{
            RunId = $runId
            Scenario = $scenario
            Map = $map
            LocalVariant = $variant
            BoundedOutcome = 'response-not-observed'
            ResponseObserved = $false
            Transmissions = @()
            Ack = $null
            Next = $null
        }
    }
    if ([string]$metadata.completion -cne 'bounded_complete') {
        throw 'Observed response requires bounded response/ACK/next-boundary completion.'
    }

    $transmissions = Assert-ResponseTransmissions -Metadata $metadata `
        -RunDirectory $RunDirectory -RawScenario $rawScenario
    $ack = Assert-CoveringAck -Metadata $metadata `
        -Transmissions $transmissions -RawScenario $rawScenario
    $next = Assert-NextServerBoundary -Metadata $metadata `
        -RunDirectory $RunDirectory -RawScenario $rawScenario `
        -ResponseEventOrder $transmissions[0].EventOrder
    return [pscustomobject]@{
        RunId = $runId
        Scenario = $scenario
        Map = $map
        LocalVariant = $variant
        BoundedOutcome = 'response-and-next-boundary-complete'
        ResponseObserved = $true
        Transmissions = $transmissions
        Ack = $ack
        Next = $next
    }
}

function Get-CountObject {
    param([hashtable]$Table)
    $result = [ordered]@{}
    foreach ($key in @($Table.Keys | Sort-Object)) {
        $result[$key] = [int]$Table[$key]
    }
    return [pscustomobject]$result
}

function Get-DependencyOutcome {
    param([object[]]$Runs, [string]$Scenario, [string]$CanonicalSha256)
    $members = @($Runs | Where-Object Scenario -ceq $Scenario)
    if ($members.Count -ne 1) {
        throw "Differential scenario '$Scenario' must contain exactly one run."
    }
    if (-not $members[0].ResponseObserved) { return 'response-not-observed' }
    $sha = [string]$members[0].Transmissions[0].Carrier.SemanticSha256
    if ($sha -ceq $CanonicalSha256) { return 'canonical-semantic-bytes' }
    return 'changed-semantic-bytes'
}

function Project-EvidenceSet {
    if (-not (Test-Path -LiteralPath $captureRoot -PathType Container)) {
        throw 'Ignored resource-response evidence root is absent; projection refused.'
    }
    Assert-NoReparsePointInExistingPath -Path $captureRoot `
        -Label 'resource-response evidence root'
    Assert-NoReparsePoint -Path $captureRoot `
        -Label 'resource-response evidence root'
    $candidates = @(Get-ChildItem -LiteralPath $captureRoot -Force -Directory |
        Where-Object { $_.Name -cmatch '^m313-[a-z0-9-]{1,160}$' } |
        Sort-Object Name)
    if ($candidates.Count -gt $maximumAcceptedRuns) {
        throw 'Resource-response evidence candidate count exceeds its bound.'
    }
    $directories = [Collections.Generic.List[object]]::new()
    foreach ($candidate in $candidates) {
        Assert-NoReparsePoint -Path $candidate.FullName `
            -Label 'capture candidate directory'
        $attestation = Join-Path $candidate.FullName `
            'research-restoration-attestation.json'
        if (Test-Path -LiteralPath $attestation -PathType Leaf) {
            $directories.Add($candidate)
        }
    }
    if ($directories.Count -lt 1) {
        throw 'No restoration-attested resource-response runs are available.'
    }
    $liveStockProcesses = @(Get-Process -Name 'hl', 'hlds' `
        -ErrorAction SilentlyContinue | Where-Object { -not $_.HasExited })
    if ($liveStockProcesses.Count -ne 0) {
        throw 'Stock hl/hlds process remains live during offline projection.'
    }

    $runs = [Collections.Generic.List[object]]::new()
    foreach ($directory in $directories) {
        Assert-NoReparsePoint -Path $directory.FullName -Label 'capture run directory'
        $runs.Add((Get-RunProjection -RunDirectory $directory.FullName))
    }

    $scenarioCounts = @{}
    $mapCounts = @{}
    foreach ($run in $runs) {
        if (-not $scenarioCounts.ContainsKey($run.Scenario)) {
            $scenarioCounts[$run.Scenario] = 0
        }
        ++$scenarioCounts[$run.Scenario]
        if (-not $mapCounts.ContainsKey($run.Map)) { $mapCounts[$run.Map] = 0 }
        ++$mapCounts[$run.Map]
    }
    foreach ($scenario in $requiredScenarioMinimums.Keys) {
        $count = if ($scenarioCounts.ContainsKey($scenario)) {
            [int]$scenarioCounts[$scenario]
        }
        else { 0 }
        if ($count -lt (Get-ScenarioMinimum -Name $scenario)) {
            throw "Required scenario '$scenario' is incomplete; projection refused."
        }
    }
    foreach ($map in @('boot_camp', 'crossfire', 'stalkyard')) {
        $count = if ($mapCounts.ContainsKey($map)) { [int]$mapCounts[$map] } else { 0 }
        if ($count -lt 3) {
            throw "Required map profile '$map' has fewer than three accepted runs."
        }
    }
    $consistencyA = if ($scenarioCounts.ContainsKey(
            'server-consistency-profile-a')) {
        [int]$scenarioCounts['server-consistency-profile-a']
    }
    else { 0 }
    $consistencyB = if ($scenarioCounts.ContainsKey(
            'server-consistency-profile-b')) {
        [int]$scenarioCounts['server-consistency-profile-b']
    }
    else { 0 }
    if (($consistencyA -ne 0 -or $consistencyB -ne 0) -and
        ($consistencyA -lt 2 -or $consistencyB -lt 2)) {
        throw 'Optional server-consistency evidence requires two runs per profile.'
    }

    $observed = @($runs | Where-Object ResponseObserved)
    if ($observed.Count -lt 1) { throw 'Corpus contains no observed response.' }
    $canonicalMembers = @($observed | Where-Object {
            $_.LocalVariant -ceq 'unchanged' -or $_.LocalVariant -ceq 'intact'
        })
    $canonicalHashes = @($canonicalMembers | ForEach-Object {
            $_.Transmissions[0].Carrier.SemanticSha256
        } | Sort-Object -Unique)
    if ($canonicalHashes.Count -ne 1 -or
        [string]$canonicalHashes[0] -cne $expectedCanonicalSemanticSha256) {
        throw 'Canonical response semantic bytes are not stable/confirmed.'
    }
    $canonicalSha = [string]$canonicalHashes[0]

    foreach ($map in @('boot_camp', 'crossfire', 'stalkyard')) {
        $hashes = @($canonicalMembers | Where-Object Map -ceq $map |
            ForEach-Object { $_.Transmissions[0].Carrier.SemanticSha256 } |
            Sort-Object -Unique)
        if ($hashes.Count -ne 1 -or [string]$hashes[0] -cne $canonicalSha) {
            throw "Canonical response changed across map profile '$map'."
        }
    }
    foreach ($stableScenario in @(
            'clean-server-restart', 'reconnect')) {
        $hashes = @($canonicalMembers |
            Where-Object Scenario -ceq $stableScenario |
            ForEach-Object { $_.Transmissions[0].Carrier.SemanticSha256 } |
            Sort-Object -Unique)
        if ($hashes.Count -ne 1 -or [string]$hashes[0] -cne $canonicalSha) {
            throw "Canonical response changed across '$stableScenario' runs."
        }
    }
    $localIntact = Get-DependencyOutcome -Runs $runs `
        -Scenario 'local-resource-intact' -CanonicalSha256 $canonicalSha
    $localAbsent = Get-DependencyOutcome -Runs $runs `
        -Scenario 'local-resource-absent' -CanonicalSha256 $canonicalSha
    $localChanged = Get-DependencyOutcome -Runs $runs `
        -Scenario 'local-resource-changed' -CanonicalSha256 $canonicalSha
    if ($localIntact -cne 'canonical-semantic-bytes') {
        throw 'Intact local-resource differential is not canonical.'
    }
    $providerRequirement = if (
        $localAbsent -ceq 'canonical-semantic-bytes' -and
        $localChanged -ceq 'canonical-semantic-bytes') {
        'not-required-by-observed-local-differential'
    }
    else {
        'required-or-stage-gated-by-observed-local-differential'
    }

    $tailSizes = @($observed | ForEach-Object {
            $_.Transmissions | ForEach-Object { $_.Carrier.TailBytes }
        } | Sort-Object -Unique)
    $fullBodySizes = @($observed | ForEach-Object {
            $_.Transmissions | ForEach-Object { $_.Carrier.FullBodyBytes }
        } | Sort-Object -Unique)
    $reconnectBodySizes = @($observed |
        Where-Object Scenario -ceq 'reconnect' |
        ForEach-Object {
            $_.Transmissions | ForEach-Object { $_.Carrier.FullBodyBytes }
        } | Sort-Object -Unique)
    if ($fullBodySizes -cnotcontains 62 -or
        $reconnectBodySizes -cnotcontains 64 -or
        $reconnectBodySizes -cnotcontains 66 -or
        $reconnectBodySizes -cnotcontains 68) {
        throw 'Required normal/coalesced 62/64/66/68 carrier evidence is incomplete.'
    }
    $semanticHashes = @($observed | ForEach-Object {
            $_.Transmissions[0].Carrier.SemanticSha256
        } | Sort-Object -Unique)
    $nextOpcodes = @($observed | ForEach-Object { $_.Next.Opcode } |
        Sort-Object -Unique)
    $nextSizes = @($observed | ForEach-Object { $_.Next.PayloadBytes })
    $responseTransmissionCount = [int](($observed | ForEach-Object {
                $_.Transmissions.Count
            } | Measure-Object -Sum).Sum)
    $responseRetransmissionRuns = @($observed | Where-Object {
            $_.Transmissions.Count -gt 1 }).Count
    $nextRetransmissionRuns = @($observed | Where-Object {
            $_.Next.TransmissionCount -gt 1 }).Count
    $ackGenerationZero = @($observed | Where-Object {
            -not $_.Ack.ReliableGeneration }).Count
    $ackGenerationOne = $observed.Count - $ackGenerationZero
    $exactCoveringAcks = @($observed | Where-Object {
            $_.Ack.CoverDelta -eq 0 }).Count
    if ($ackGenerationZero -lt 1 -or $ackGenerationOne -lt 1 -or
        $responseRetransmissionRuns -lt 4 -or $nextRetransmissionRuns -lt 2) {
        throw 'Generation/retransmission evidence coverage is incomplete.'
    }

    return [pscustomobject][ordered]@{
        schema = $projectionSchema
        profile = $stockProfile
        verifier_normalized_sha256 = Get-NormalizedVerifierSha256
        methodology = [pscustomobject][ordered]@{
            mode = 'offline-existing-ignored-artifacts'
            tracked_sanitized_projection = $true
            ignored_raw_sources = $true
            transport = 'private-ipv4-loopback-udp'
            byte_preserving_relay = $true
            one_upstream_socket = $true
            exact_endpoint_validation = $true
            packet_byte_time_bounds = $true
            stock_processes_started_by_verifier = $false
            no_opcode_scanning = $true
        }
        research_root_policy = [pscustomobject][ordered]@{
            accepted_capture_mode = 'user-supplied-isolated-copy-only'
            primary_steam_install = 'rejected-no-override'
            explicit_preflight_parameter = 'ResearchHalfLifeRoot'
            isolation_marker = $isolationMarkerName
            external_file_drift = 'none'
            capture_metadata_asserts_restoration = $true
        }
        semantic_gate = [pscustomobject][ordered]@{
            opcode = 5
            semantic_name = 'Opcode5ResourceResponse'
            status = 'neutral-exact-structural-profile'
            semantic_bytes = 41
            canonical_semantic_sha256 = $canonicalSha
            captured_response_replay = $false
        }
        carrier_geometry = [pscustomobject][ordered]@{
            descriptor_offset = 0
            descriptor_bytes = 10
            descriptor_hex = $expectedDescriptorBytes
            selected_semantic_offset = 10
            selected_semantic_bytes = 41
            concurrent_tail_offset = 51
            full_decoded_body_sizes = $fullBodySizes
        }
        semantic_layout = [pscustomobject][ordered]@{
            byte_aligned = $true
            exact_fields = @(
                [pscustomobject][ordered]@{ name = 'opcode'; offset = 0; bytes = 1; encoding = 'u8' },
                [pscustomobject][ordered]@{ name = 'entry_count_candidate'; offset = 1; bytes = 2; encoding = 'u16le' },
                [pscustomobject][ordered]@{ name = 'resource_name_bytes'; offset = 3; bytes = 14; encoding = 'bounded-nul-string-value-suppressed' },
                [pscustomobject][ordered]@{ name = 'resource_type_candidate'; offset = 17; bytes = 1; encoding = 'u8' },
                [pscustomobject][ordered]@{ name = 'resource_index_candidate'; offset = 18; bytes = 2; encoding = 'u16le' },
                [pscustomobject][ordered]@{ name = 'resource_size_candidate'; offset = 20; bytes = 4; encoding = 'u32le-value-suppressed' },
                [pscustomobject][ordered]@{ name = 'resource_flags_candidate'; offset = 24; bytes = 1; encoding = 'u8-value-suppressed' },
                [pscustomobject][ordered]@{ name = 'opaque_material'; offset = 25; bytes = 16; encoding = 'byte-array-value-suppressed' }
            )
            observed_semantic_sha256_values = $semanticHashes
            raw_values_projected = $false
        }
        dependency_results = [pscustomobject][ordered]@{
            map_profiles_stable = $true
            clean_restart_profile_stable = $true
            reconnect_profile_stable = $true
            local_resource_intact = $localIntact
            local_resource_absent = $localAbsent
            local_resource_changed = $localChanged
            provider_requirement = $providerRequirement
        }
        reliable_lifecycle = [pscustomobject][ordered]@{
            semantic_queue_count_per_observed_run = 1
            observed_response_runs = $observed.Count
            transport_transmissions = $responseTransmissionCount
            retransmission_runs = $responseRetransmissionRuns
            acknowledgement_generation_zero_runs = $ackGenerationZero
            acknowledgement_generation_one_runs = $ackGenerationOne
            exact_numeric_cover_runs = $exactCoveringAcks
            later_numeric_cover_runs = $observed.Count - $exactCoveringAcks
            server_semantic_handling_count_per_run = 1
        }
        concurrent_tail = [pscustomobject][ordered]@{
            classification = 'descriptor-unselected-contemporaneous-payload'
            opcode_candidate = 2
            length_control_relation = 'byte1-equals-tail-bytes-minus-three'
            observed_tail_sizes = $tailSizes
            retransmitted_with_semantic_builder = $false
            raw_values_projected = $false
        }
        next_server_boundary = [pscustomobject][ordered]@{
            observed_runs = $observed.Count
            opcode_offset = 0
            opcode_values = $nextOpcodes
            minimum_payload_bytes = [int](($nextSizes | Measure-Object -Minimum).Minimum)
            maximum_payload_bytes = [int](($nextSizes | Measure-Object -Maximum).Maximum)
            body_unconsumed = $true
            retransmission_runs = $nextRetransmissionRuns
            raw_payload_projected = $false
        }
        scenario_counts = Get-CountObject -Table $scenarioCounts
        map_counts = Get-CountObject -Table $mapCounts
        aggregate = [pscustomobject][ordered]@{
            accepted_runs = $runs.Count
            response_observed_runs = $observed.Count
            bounded_no_response_differentials = $runs.Count - $observed.Count
            external_file_drift = 'none'
            raw_carrier_bytes_projected = $false
            raw_tail_bytes_projected = $false
            raw_provider_material_projected = $false
            raw_next_payload_projected = $false
            authentication_material_projected = $false
            game_resource_names_projected = $false
        }
    }
}

function Assert-ProjectionMetadata {
    param([object]$Metadata)
    Assert-ExactProperties -Value $Metadata -Allowed @(
        'schema', 'profile', 'verifier_normalized_sha256', 'methodology',
        'research_root_policy', 'semantic_gate', 'carrier_geometry',
        'semantic_layout', 'dependency_results', 'reliable_lifecycle',
        'concurrent_tail', 'next_server_boundary', 'scenario_counts',
        'map_counts', 'aggregate') -Label 'projection'
    if ([string]$Metadata.schema -cne $projectionSchema -or
        [string]$Metadata.profile -cne $stockProfile -or
        [string]$Metadata.verifier_normalized_sha256 -cne
            (Get-NormalizedVerifierSha256)) {
        throw 'Projection identity/verifier hash is invalid.'
    }
    Assert-ExactProperties -Value $Metadata.methodology -Allowed @(
        'mode', 'tracked_sanitized_projection', 'ignored_raw_sources',
        'transport', 'byte_preserving_relay', 'one_upstream_socket',
        'exact_endpoint_validation', 'packet_byte_time_bounds',
        'stock_processes_started_by_verifier', 'no_opcode_scanning') `
        -Label 'projection methodology'
    if ([string]$Metadata.methodology.mode -cne
            'offline-existing-ignored-artifacts' -or
        [string]$Metadata.methodology.transport -cne
            'private-ipv4-loopback-udp') {
        throw 'Projection methodology is invalid.'
    }
    foreach ($field in @(
            'tracked_sanitized_projection', 'ignored_raw_sources',
            'byte_preserving_relay', 'one_upstream_socket',
            'exact_endpoint_validation', 'packet_byte_time_bounds',
            'no_opcode_scanning')) {
        if (-not (Get-StrictBoolean $Metadata.methodology.$field `
                "projection methodology $field")) {
            throw "Projection methodology requires $field=true."
        }
    }
    if (Get-StrictBoolean $Metadata.methodology.stock_processes_started_by_verifier `
        'verifier stock process flag') {
        throw 'Offline verifier must not start stock processes.'
    }

    Assert-ExactProperties -Value $Metadata.research_root_policy -Allowed @(
        'accepted_capture_mode', 'primary_steam_install',
        'explicit_preflight_parameter', 'isolation_marker',
        'external_file_drift', 'capture_metadata_asserts_restoration') `
        -Label 'research-root policy'
    if ([string]$Metadata.research_root_policy.accepted_capture_mode -cne
            'user-supplied-isolated-copy-only' -or
        [string]$Metadata.research_root_policy.primary_steam_install -cne
            'rejected-no-override' -or
        [string]$Metadata.research_root_policy.explicit_preflight_parameter -cne
            'ResearchHalfLifeRoot' -or
        [string]$Metadata.research_root_policy.isolation_marker -cne
            $isolationMarkerName -or
        [string]$Metadata.research_root_policy.external_file_drift -cne 'none' -or
        -not (Get-StrictBoolean `
            $Metadata.research_root_policy.capture_metadata_asserts_restoration `
            'capture restoration assertion')) {
        throw 'Projection research-root policy is invalid.'
    }

    Assert-ExactProperties -Value $Metadata.semantic_gate -Allowed @(
        'opcode', 'semantic_name', 'status', 'semantic_bytes',
        'canonical_semantic_sha256', 'captured_response_replay') `
        -Label 'semantic gate'
    if ((Get-BoundedInteger $Metadata.semantic_gate.opcode `
            'semantic opcode' 5 5) -ne 5 -or
        [string]$Metadata.semantic_gate.semantic_name -cne
            'Opcode5ResourceResponse' -or
        [string]$Metadata.semantic_gate.status -cne
            'neutral-exact-structural-profile' -or
        (Get-BoundedInteger $Metadata.semantic_gate.semantic_bytes `
            'semantic bytes' 41 41) -ne 41 -or
        (Get-StrictBoolean $Metadata.semantic_gate.captured_response_replay `
            'captured response replay')) {
        throw 'Projection semantic gate is invalid.'
    }
    Assert-Sha256 $Metadata.semantic_gate.canonical_semantic_sha256 `
        'canonical semantic hash'
    if ([string]$Metadata.semantic_gate.canonical_semantic_sha256 -cne
        $expectedCanonicalSemanticSha256) {
        throw 'Projection canonical semantic hash changed.'
    }

    Assert-ExactProperties -Value $Metadata.carrier_geometry -Allowed @(
        'descriptor_offset', 'descriptor_bytes', 'descriptor_hex',
        'selected_semantic_offset', 'selected_semantic_bytes',
        'concurrent_tail_offset', 'full_decoded_body_sizes') `
        -Label 'carrier geometry'
    if ((Get-BoundedInteger $Metadata.carrier_geometry.descriptor_offset `
            'descriptor offset' 0 0) -ne 0 -or
        (Get-BoundedInteger $Metadata.carrier_geometry.descriptor_bytes `
            'descriptor bytes' 10 10) -ne 10 -or
        [string]$Metadata.carrier_geometry.descriptor_hex -cne
            $expectedDescriptorBytes -or
        (Get-BoundedInteger $Metadata.carrier_geometry.selected_semantic_offset `
            'semantic offset' 10 10) -ne 10 -or
        (Get-BoundedInteger $Metadata.carrier_geometry.selected_semantic_bytes `
            'semantic bytes' 41 41) -ne 41 -or
        (Get-BoundedInteger $Metadata.carrier_geometry.concurrent_tail_offset `
            'tail offset' 51 51) -ne 51) {
        throw 'Projection carrier geometry is invalid.'
    }
    $bodySizes = @($Metadata.carrier_geometry.full_decoded_body_sizes)
    if ($bodySizes.Count -lt 1) { throw 'Projection lacks carrier sizes.' }
    foreach ($size in $bodySizes) {
        [void](Get-BoundedInteger $size 'carrier size' 54 $maximumCarrierBytes)
    }
    foreach ($requiredSize in @(62, 64, 66, 68)) {
        if ($bodySizes -cnotcontains $requiredSize) {
            throw "Projection lacks confirmed carrier size $requiredSize."
        }
    }

    Assert-ExactProperties -Value $Metadata.semantic_layout -Allowed @(
        'byte_aligned', 'exact_fields', 'observed_semantic_sha256_values',
        'raw_values_projected') -Label 'semantic layout'
    if (-not (Get-StrictBoolean $Metadata.semantic_layout.byte_aligned `
            'semantic byte alignment') -or
        (Get-StrictBoolean $Metadata.semantic_layout.raw_values_projected `
            'semantic raw values projected')) {
        throw 'Projection semantic layout flags are invalid.'
    }
    $fields = @($Metadata.semantic_layout.exact_fields)
    $expectedFields = @(
        @('opcode', 0, 1, 'u8'),
        @('entry_count_candidate', 1, 2, 'u16le'),
        @('resource_name_bytes', 3, 14, 'bounded-nul-string-value-suppressed'),
        @('resource_type_candidate', 17, 1, 'u8'),
        @('resource_index_candidate', 18, 2, 'u16le'),
        @('resource_size_candidate', 20, 4, 'u32le-value-suppressed'),
        @('resource_flags_candidate', 24, 1, 'u8-value-suppressed'),
        @('opaque_material', 25, 16, 'byte-array-value-suppressed'))
    if ($fields.Count -ne $expectedFields.Count) {
        throw 'Projection semantic field count is invalid.'
    }
    for ($index = 0; $index -lt $fields.Count; ++$index) {
        Assert-ExactProperties -Value $fields[$index] -Allowed @(
            'name', 'offset', 'bytes', 'encoding') -Label "semantic field $index"
        if ([string]$fields[$index].name -cne $expectedFields[$index][0] -or
            [int]$fields[$index].offset -ne [int]$expectedFields[$index][1] -or
            [int]$fields[$index].bytes -ne [int]$expectedFields[$index][2] -or
            [string]$fields[$index].encoding -cne $expectedFields[$index][3]) {
            throw "Projection semantic field $index is invalid."
        }
    }
    $observedSemanticHashes = @(
        $Metadata.semantic_layout.observed_semantic_sha256_values)
    if ($observedSemanticHashes.Count -lt 1 -or
        $observedSemanticHashes -cnotcontains $expectedCanonicalSemanticSha256) {
        throw 'Projection lacks its canonical observed semantic hash.'
    }
    foreach ($hash in $observedSemanticHashes) {
        Assert-Sha256 $hash 'observed semantic hash'
    }

    Assert-ExactProperties -Value $Metadata.dependency_results -Allowed @(
        'map_profiles_stable', 'clean_restart_profile_stable',
        'reconnect_profile_stable', 'local_resource_intact',
        'local_resource_absent', 'local_resource_changed',
        'provider_requirement') -Label 'dependency results'
    foreach ($field in @(
            'map_profiles_stable', 'clean_restart_profile_stable',
            'reconnect_profile_stable')) {
        if (-not (Get-StrictBoolean $Metadata.dependency_results.$field `
                "dependency result $field")) {
            throw "Dependency result $field must be true."
        }
    }
    foreach ($field in @(
            'local_resource_intact', 'local_resource_absent',
            'local_resource_changed')) {
        if (@(
                'canonical-semantic-bytes', 'changed-semantic-bytes',
                'response-not-observed') -cnotcontains
            [string]$Metadata.dependency_results.$field) {
            throw "Dependency result $field is invalid."
        }
    }
    if ([string]$Metadata.dependency_results.local_resource_intact -cne
        'canonical-semantic-bytes') {
        throw 'Projection intact local-resource result must be canonical.'
    }
    $expectedProviderRequirement = if (
        [string]$Metadata.dependency_results.local_resource_absent -ceq
            'canonical-semantic-bytes' -and
        [string]$Metadata.dependency_results.local_resource_changed -ceq
            'canonical-semantic-bytes') {
        'not-required-by-observed-local-differential'
    }
    else {
        'required-or-stage-gated-by-observed-local-differential'
    }
    if (@(
            'not-required-by-observed-local-differential',
            'required-or-stage-gated-by-observed-local-differential') `
        -cnotcontains [string]$Metadata.dependency_results.provider_requirement -or
        [string]$Metadata.dependency_results.provider_requirement -cne
            $expectedProviderRequirement) {
        throw 'Provider requirement evidence label is invalid.'
    }

    Assert-ExactProperties -Value $Metadata.reliable_lifecycle -Allowed @(
        'semantic_queue_count_per_observed_run', 'observed_response_runs',
        'transport_transmissions', 'retransmission_runs',
        'acknowledgement_generation_zero_runs',
        'acknowledgement_generation_one_runs', 'exact_numeric_cover_runs',
        'later_numeric_cover_runs', 'server_semantic_handling_count_per_run') `
        -Label 'reliable lifecycle'
    $observedRuns = [int](Get-BoundedInteger `
        $Metadata.reliable_lifecycle.observed_response_runs `
        'observed response runs' 1 $maximumAcceptedRuns)
    if ((Get-BoundedInteger `
            $Metadata.reliable_lifecycle.semantic_queue_count_per_observed_run `
            'semantic queue count' 1 1) -ne 1 -or
        (Get-BoundedInteger `
            $Metadata.reliable_lifecycle.server_semantic_handling_count_per_run `
            'server semantic handling count' 1 1) -ne 1) {
        throw 'Projection reliable exact-once values are invalid.'
    }
    $generationZeroRuns = [int](Get-BoundedInteger `
        $Metadata.reliable_lifecycle.acknowledgement_generation_zero_runs `
        'generation-zero ACK runs' 1 $maximumAcceptedRuns)
    $generationOneRuns = [int](Get-BoundedInteger `
        $Metadata.reliable_lifecycle.acknowledgement_generation_one_runs `
        'generation-one ACK runs' 1 $maximumAcceptedRuns)
    $exactCoverRuns = [int](Get-BoundedInteger `
        $Metadata.reliable_lifecycle.exact_numeric_cover_runs `
        'exact numeric cover runs' 0 $maximumAcceptedRuns)
    $laterCoverRuns = [int](Get-BoundedInteger `
        $Metadata.reliable_lifecycle.later_numeric_cover_runs `
        'later numeric cover runs' 0 $maximumAcceptedRuns)
    $generationRuns = $generationZeroRuns + $generationOneRuns
    $coverRuns = $exactCoverRuns + $laterCoverRuns
    if ($generationRuns -ne $observedRuns -or $coverRuns -ne $observedRuns -or
        [int]$Metadata.reliable_lifecycle.retransmission_runs -lt 4) {
        throw 'Projection ACK lifecycle totals are inconsistent.'
    }

    Assert-ExactProperties -Value $Metadata.concurrent_tail -Allowed @(
        'classification', 'opcode_candidate', 'length_control_relation',
        'observed_tail_sizes', 'retransmitted_with_semantic_builder',
        'raw_values_projected') -Label 'concurrent tail'
    if ([string]$Metadata.concurrent_tail.classification -cne
            'descriptor-unselected-contemporaneous-payload' -or
        [int]$Metadata.concurrent_tail.opcode_candidate -ne 2 -or
        [string]$Metadata.concurrent_tail.length_control_relation -cne
            'byte1-equals-tail-bytes-minus-three' -or
        (Get-StrictBoolean `
            $Metadata.concurrent_tail.retransmitted_with_semantic_builder `
            'tail retransmitted with semantic builder') -or
        (Get-StrictBoolean $Metadata.concurrent_tail.raw_values_projected `
            'tail raw values projected')) {
        throw 'Projection concurrent-tail contract is invalid.'
    }
    foreach ($size in @($Metadata.concurrent_tail.observed_tail_sizes)) {
        [void](Get-BoundedInteger $size 'tail size' 3 $maximumConcurrentTailBytes)
    }
    $projectedTailSizes = @($Metadata.concurrent_tail.observed_tail_sizes)
    foreach ($requiredTailSize in @(11, 13, 15, 17)) {
        if ($projectedTailSizes -cnotcontains $requiredTailSize) {
            throw "Projection lacks confirmed tail size $requiredTailSize."
        }
    }

    Assert-ExactProperties -Value $Metadata.next_server_boundary -Allowed @(
        'observed_runs', 'opcode_offset', 'opcode_values',
        'minimum_payload_bytes', 'maximum_payload_bytes', 'body_unconsumed',
        'retransmission_runs', 'raw_payload_projected') `
        -Label 'next server boundary'
    if ([int]$Metadata.next_server_boundary.observed_runs -ne $observedRuns -or
        [int]$Metadata.next_server_boundary.opcode_offset -ne 0 -or
        [int]$Metadata.next_server_boundary.retransmission_runs -lt 2 -or
        -not (Get-StrictBoolean $Metadata.next_server_boundary.body_unconsumed `
            'next body unconsumed') -or
        (Get-StrictBoolean $Metadata.next_server_boundary.raw_payload_projected `
            'next raw payload projected')) {
        throw 'Projection next-server boundary flags are invalid.'
    }
    $projectedNextOpcodes = @($Metadata.next_server_boundary.opcode_values)
    if ($projectedNextOpcodes.Count -lt 1) {
        throw 'Projection lacks a next-server opcode boundary.'
    }
    foreach ($opcode in $projectedNextOpcodes) {
        [void](Get-BoundedInteger $opcode 'next server opcode' 0 255)
    }
    $minimumNext = [int](Get-BoundedInteger `
        $Metadata.next_server_boundary.minimum_payload_bytes `
        'minimum next payload' 1 $maximumPostResponsePayloadBytes)
    $maximumNext = [int](Get-BoundedInteger `
        $Metadata.next_server_boundary.maximum_payload_bytes `
        'maximum next payload' 1 $maximumPostResponsePayloadBytes)
    if ($minimumNext -gt $maximumNext) {
        throw 'Projection next-server payload range is inverted.'
    }

    $allowedProjectedScenarios = @($requiredScenarioMinimums.Keys) + @(
        'server-consistency-profile-a', 'server-consistency-profile-b')
    $scenarioTotal = 0
    foreach ($property in @($Metadata.scenario_counts.PSObject.Properties)) {
        if ($allowedProjectedScenarios -cnotcontains $property.Name) {
            throw "Projection contains unexpected scenario '$($property.Name)'."
        }
        $scenarioTotal += [int](Get-BoundedInteger $property.Value `
            "scenario count $($property.Name)" 0 $maximumAcceptedRuns)
    }
    foreach ($scenario in $requiredScenarioMinimums.Keys) {
        $property = $Metadata.scenario_counts.PSObject.Properties[$scenario]
        if ($null -eq $property -or
            [int](Get-BoundedInteger $property.Value `
                "scenario count $scenario" 0 $maximumAcceptedRuns) -lt
                (Get-ScenarioMinimum -Name $scenario)) {
            throw "Projection required scenario '$scenario' is incomplete."
        }
    }
    Assert-ExactProperties -Value $Metadata.map_counts -Allowed @(
        'boot_camp', 'crossfire', 'stalkyard') -Label 'projection map counts'
    $mapTotal = 0
    foreach ($map in @('boot_camp', 'crossfire', 'stalkyard')) {
        $property = $Metadata.map_counts.PSObject.Properties[$map]
        if ($null -eq $property -or
            [int](Get-BoundedInteger $property.Value `
                "map count $map" 0 $maximumAcceptedRuns) -lt 3) {
            throw "Projection map profile '$map' is incomplete."
        }
        $mapTotal += [int]$property.Value
    }

    Assert-ExactProperties -Value $Metadata.aggregate -Allowed @(
        'accepted_runs', 'response_observed_runs',
        'bounded_no_response_differentials', 'external_file_drift',
        'raw_carrier_bytes_projected', 'raw_tail_bytes_projected',
        'raw_provider_material_projected', 'raw_next_payload_projected',
        'authentication_material_projected', 'game_resource_names_projected') `
        -Label 'projection aggregate'
    $acceptedRuns = [int](Get-BoundedInteger $Metadata.aggregate.accepted_runs `
        'accepted runs' 1 $maximumAcceptedRuns)
    $boundedNoResponse = [int](Get-BoundedInteger `
        $Metadata.aggregate.bounded_no_response_differentials `
        'bounded no-response differentials' 0 $maximumAcceptedRuns)
    $expectedBoundedNoResponse = @(
        $Metadata.dependency_results.local_resource_absent,
        $Metadata.dependency_results.local_resource_changed |
            Where-Object { [string]$_ -ceq 'response-not-observed' }).Count
    if ($acceptedRuns -ne $observedRuns + $boundedNoResponse -or
        $acceptedRuns -ne $scenarioTotal -or $acceptedRuns -ne $mapTotal -or
        $boundedNoResponse -ne $expectedBoundedNoResponse -or
        [string]$Metadata.aggregate.external_file_drift -cne 'none') {
        throw 'Projection aggregate totals/drift are invalid.'
    }
    foreach ($field in @(
            'raw_carrier_bytes_projected', 'raw_tail_bytes_projected',
            'raw_provider_material_projected', 'raw_next_payload_projected',
            'authentication_material_projected',
            'game_resource_names_projected')) {
        if (Get-StrictBoolean $Metadata.aggregate.$field "aggregate $field") {
            throw "Projection aggregate requires $field=false."
        }
    }
}

function Get-KnownSteamRoots {
    $roots = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($registryPath in @(
            'HKCU:\Software\Valve\Steam',
            'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
            'HKLM:\SOFTWARE\Valve\Steam')) {
        if (-not (Test-Path -LiteralPath $registryPath -PathType Container `
                -ErrorAction Stop)) {
            continue
        }
        try {
            $value = Get-ItemProperty -LiteralPath $registryPath -ErrorAction Stop
        }
        catch {
            throw "Unable to inspect configured Steam registry key '$registryPath'."
        }
        foreach ($property in @('SteamPath', 'InstallPath')) {
            $entry = $value.PSObject.Properties[$property]
            if ($null -eq $entry -or
                [string]::IsNullOrWhiteSpace([string]$entry.Value)) {
                continue
            }
            try {
                [void]$roots.Add([IO.Path]::GetFullPath([string]$entry.Value))
            }
            catch {
                throw "Configured Steam registry value '$registryPath\\$property' is invalid."
            }
        }
    }
    foreach ($candidate in @(
            $(if (${env:ProgramFiles(x86)}) {
                Join-Path ${env:ProgramFiles(x86)} 'Steam'
            }),
            $(if ($env:ProgramFiles) {
                Join-Path $env:ProgramFiles 'Steam'
            }))) {
        if ($candidate -and
            (Test-Path -LiteralPath $candidate -PathType Container)) {
            [void]$roots.Add([IO.Path]::GetFullPath($candidate))
        }
    }
    foreach ($steamRoot in @($roots)) {
        $libraryFile = Join-Path $steamRoot 'steamapps/libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $libraryFile -PathType Leaf)) { continue }
        try {
            $text = Get-Content -Raw -LiteralPath $libraryFile
            foreach ($match in [regex]::Matches(
                    $text, '"path"\s+"(?<path>[^"]+)"',
                    [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
                $libraryPath = $match.Groups['path'].Value.Replace('\\', '\')
                if (-not [string]::IsNullOrWhiteSpace($libraryPath)) {
                    [void]$roots.Add([IO.Path]::GetFullPath($libraryPath))
                }
            }
        }
        catch {
            throw 'Unable to establish the configured Steam-library set.'
        }
    }
    return @($roots)
}

function Assert-IsolatedResearchRoot {
    param([string]$Path)
    $inputPath = [IO.Path]::GetFullPath($Path)
    Assert-NoReparsePointInExistingPath -Path $inputPath `
        -Label 'ResearchHalfLifeRoot'
    $resolved = Resolve-Path -LiteralPath $inputPath -ErrorAction Stop
    if ($resolved.Provider.Name -cne 'FileSystem') {
        throw 'ResearchHalfLifeRoot must be a filesystem directory.'
    }
    $root = [IO.Path]::GetFullPath($resolved.Path).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw 'ResearchHalfLifeRoot must be a directory.'
    }
    Assert-NoReparsePoint -Path $root -Label 'ResearchHalfLifeRoot'
    if ($root -match '(?i)(?:^|[\\/])steamapps(?:[\\/]|$)') {
        throw 'Primary or managed Steam-library roots are never accepted for research.'
    }
    foreach ($steamRoot in @(Get-KnownSteamRoots)) {
        $normalizedSteam = [IO.Path]::GetFullPath($steamRoot).TrimEnd('\', '/')
        if ($root.Equals(
                $normalizedSteam, [StringComparison]::OrdinalIgnoreCase) -or
            $root.StartsWith(
                $normalizedSteam + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'ResearchHalfLifeRoot resolves inside a configured Steam library.'
        }
        # Use lexical path composition here: a registered Steam library may
        # reside on a currently offline drive, and Join-Path would ask the
        # PowerShell provider to resolve that drive before this comparison.
        $primaryPath = [IO.Path]::Combine(
            $normalizedSteam, 'steamapps', 'common', 'Half-Life')
        $primary = [IO.Path]::GetFullPath($primaryPath).TrimEnd('\', '/')
        if ($root.Equals($primary, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Primary Half-Life installation is never accepted for research.'
        }
    }

    $nestedReparsePoints = @(Get-ChildItem -LiteralPath $root -Force -Recurse |
        Where-Object {
            ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        } | Select-Object -First 1)
    if ($nestedReparsePoints.Count -ne 0) {
        throw 'ResearchHalfLifeRoot contains a reparse point.'
    }

    $marker = Join-Path $root $isolationMarkerName
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw "Isolated research copy lacks required marker $isolationMarkerName."
    }
    Assert-NoReparsePoint -Path $marker -Label 'isolation marker'
    Assert-OnlyDefaultDataStream -Path $marker -Label 'isolation marker'
    if ((Get-Item -LiteralPath $marker -Force).Length -gt 128 -or
        (Get-Content -Raw -LiteralPath $marker).Trim() -cne
            $isolationMarkerText) {
        throw 'Isolated research marker content is invalid.'
    }

    foreach ($relative in @('hl.exe', 'hlds.exe', 'valve')) {
        $candidate = [IO.Path]::GetFullPath((Join-Path $root $relative))
        Assert-PathBelowRoot -Path $candidate -Root $root -Label $relative
        Assert-NoReparsePointInExistingPath -Path $candidate -Label $relative
        $pathType = if ($relative -ceq 'valve') { 'Container' } else { 'Leaf' }
        if (-not (Test-Path -LiteralPath $candidate -PathType $pathType)) {
            throw "Isolated research copy is missing $relative."
        }
        Assert-NoReparsePoint -Path $candidate -Label $relative
        if ($pathType -ceq 'Leaf') {
            Assert-OnlyDefaultDataStream -Path $candidate -Label $relative
        }
    }
    foreach ($relative in @(
            'valve/config.cfg', 'valve/userconfig.cfg',
            'valve/autoexec.cfg', 'valve/custom.hpk', 'valve/server.cfg',
            'valve/listenserver.cfg', 'valve/logs')) {
        $candidate = [IO.Path]::GetFullPath((Join-Path $root $relative))
        Assert-PathBelowRoot -Path $candidate -Root $root `
            -Label 'mutable research path'
        Assert-NoReparsePointInExistingPath -Path $candidate `
            -Label 'mutable research path'
    }

    $hl = Get-Item -LiteralPath (Join-Path $root 'hl.exe') -Force
    $hlds = Get-Item -LiteralPath (Join-Path $root 'hlds.exe') -Force
    if ($hl.VersionInfo.FileVersion -cne '1, 1, 1, 1' -or
        $hlds.VersionInfo.FileVersion -cne '4, 1, 1, 1') {
        throw 'Isolated research binaries do not match accepted stock versions.'
    }
    foreach ($binary in @($hl.FullName, $hlds.FullName)) {
        $signature = Get-AuthenticodeSignature -LiteralPath $binary
        if ($signature.Status -ne 'Valid' -or
            $null -eq $signature.SignerCertificate -or
            $signature.SignerCertificate.Subject -cnotmatch
                '^CN=Valve Corp\.(?:,|$)') {
            throw 'Isolated research binary is not validly Valve-signed.'
        }
    }
    return $true
}

$gitIgnorePath = Join-Path $repositoryRoot '.gitignore'
if (-not (Test-Path -LiteralPath $gitIgnorePath -PathType Leaf) -or
    (Get-Content -Raw -LiteralPath $gitIgnorePath) -cnotmatch
        '(?m)^/manual-artifacts/\s*$') {
    throw 'Verifier requires the repository-wide /manual-artifacts/ ignore rule.'
}

if ($PSCmdlet.ParameterSetName -eq 'Isolation') {
    [void](Assert-IsolatedResearchRoot -Path $ResearchHalfLifeRoot)
    Write-Output (
        'research-root-valid isolated-copy=true primary-steam=false ' +
        'processes-started=0 preflight-only=true')
    return
}

if ($PSCmdlet.ParameterSetName -eq 'Validate' -or
    $PSCmdlet.ParameterSetName -eq 'ValidateSet') {
    $path = if ($PSCmdlet.ParameterSetName -eq 'ValidateSet') {
        $root = [IO.Path]::GetFullPath($ValidateMetadataSetRoot)
        if ($root -cne $projectionRoot) {
            throw 'Projection-set validation requires the exact tracked evidence root.'
        }
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            throw 'Projection root is absent.'
        }
        Assert-NoReparsePoint -Path $root -Label 'projection root'
        Join-Path $root 'GOLDSRC_RESOURCE_CLIENT_RESPONSE_STOCK.json'
    }
    else { [IO.Path]::GetFullPath($ValidateMetadataPath) }
    if ($path -cne $projectionPath) {
        throw 'Metadata validation requires the exact tracked projection path.'
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-Item -LiteralPath $path).Length -gt $maximumProjectionBytes) {
        throw 'Projection metadata is absent or outside its byte bound.'
    }
    Assert-NoReparsePoint -Path $path -Label 'projection metadata'
    Assert-OnlyDefaultDataStream -Path $path -Label 'projection metadata'
    $metadata = Get-Content -Raw -LiteralPath $path |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ProjectionMetadata -Metadata $metadata
    Write-Output (
        'metadata-valid semantic=opcode5-neutral carrier=10/41/tail ' +
        'ack=covering next=exact-boundary external-file-drift=none')
    return
}

$metadata = Project-EvidenceSet
Assert-ProjectionMetadata -Metadata $metadata
Assert-NoReparsePointInExistingPath -Path $projectionRoot `
    -Label 'tracked projection root'
if (-not (Test-Path -LiteralPath $projectionRoot -PathType Container)) {
    [IO.Directory]::CreateDirectory($projectionRoot) | Out-Null
}
Assert-NoReparsePoint -Path $projectionRoot -Label 'projection root'
$temporaryPath = $projectionPath + '.tmp'
if (Test-Path -LiteralPath $temporaryPath) {
    throw 'Refusing to overwrite an unexpected temporary projection file.'
}
$json = $metadata | ConvertTo-Json -Depth 16
$encoding = New-Object Text.UTF8Encoding($false)
try {
    [IO.File]::WriteAllText($temporaryPath, $json + "`r`n", $encoding)
    if ((Get-Item -LiteralPath $temporaryPath).Length -gt $maximumProjectionBytes) {
        throw 'Generated projection exceeds its byte bound.'
    }
    $roundTrip = Get-Content -Raw -LiteralPath $temporaryPath |
        ConvertFrom-Json -ErrorAction Stop
    Assert-ProjectionMetadata -Metadata $roundTrip
    Move-Item -LiteralPath $temporaryPath -Destination $projectionPath -Force
}
finally {
    if (Test-Path -LiteralPath $temporaryPath) {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}
Write-Output (
    'projection-valid semantic=opcode5-neutral carrier=10/41/tail ' +
    'ack=covering next=exact-boundary external-file-drift=none')
