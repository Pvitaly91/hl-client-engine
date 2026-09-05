#requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'stock_external_drift.ps1')

function New-TestEntry {
    param(
        [string]$Scope,
        [string]$RelativePath,
        [string]$Kind = 'file',
        [string]$Identity = '00000001:0000000000000001',
        [Int64]$Size = 4,
        [string]$Sha256 = ('A' * 64),
        [Int64]$LastWriteTicks = 10,
        [Int64]$CreationTicks = 5,
        [Int64]$Attributes = 32,
        [string]$ReadStatus = 'readable')
    [pscustomobject]@{
        scope = $Scope
        relative_path = $RelativePath
        entry_kind = $(if ($ReadStatus -ceq 'readable') { $Kind } else { 'unavailable' })
        read_status = $ReadStatus
        identity = $(if ($ReadStatus -ceq 'readable') { $Identity } else { $null })
        size = $(if ($ReadStatus -ceq 'readable') { $Size } else { $null })
        sha256 = $(if ($ReadStatus -ceq 'readable') { $Sha256 } else { $null })
        last_write_ticks = $(if ($ReadStatus -ceq 'readable') { $LastWriteTicks } else { $null })
        creation_ticks = $(if ($ReadStatus -ceq 'readable') { $CreationTicks } else { $null })
        attributes = $(if ($ReadStatus -ceq 'readable') { $Attributes } else { $null })
        reparse_status = $(if ($ReadStatus -ceq 'readable') { 'absent' } else { 'unknown' })
        ads_status = $(if ($ReadStatus -ceq 'readable') { 'default-only' } else { 'unknown' })
    }
}

function Assert-True { param([bool]$Value, [string]$Message) if (-not $Value) { throw $Message } }

$app = New-TestEntry 'app_manifest' '.'
$hlfx = New-TestEntry 'steam_hlfx_tree' 'cl_dlls/client.dll'
$before = New-StockExternalStateSnapshot @($hlfx, $app) 'idle_control'
$reordered = New-StockExternalStateSnapshot @($app, $hlfx) 'idle_control'
Assert-True ($before.ManifestSha256 -ceq $reordered.ManifestSha256) `
    'Snapshot ordering is enumeration-sensitive.'
$none = Compare-StockExternalStateSnapshot $before $reordered 'idle_control'
Assert-True ($none.result -ceq 'none' -and $none.changed_scopes -eq 0) `
    'Equal external snapshots were classified as drift.'

$contentAfter = New-StockExternalStateSnapshot @(
    (New-TestEntry 'app_manifest' '.' -Sha256 ('B' * 64)), $hlfx) 'idle_control'
$content = Compare-StockExternalStateSnapshot $before $contentAfter 'idle_control'
Assert-True ($content.content_changes -eq 1 -and
    $content.scope_kinds -contains 'app_manifest|content_changed') `
    'App manifest content drift was not typed.'

$sizeAfter = New-StockExternalStateSnapshot @(
    (New-TestEntry 'app_manifest' '.' -Size 5 -Sha256 ('C' * 64)), $hlfx) `
    'idle_control'
$size = Compare-StockExternalStateSnapshot $before $sizeAfter 'idle_control'
Assert-True ($size.scope_kinds -contains 'app_manifest|size_changed') `
    'File size drift was not typed.'

$metadataAfter = New-StockExternalStateSnapshot @(
    $app, (New-TestEntry 'steam_hlfx_tree' 'cl_dlls/client.dll' `
        -LastWriteTicks 11)) 'standard_server_diagnostic'
$metadata = Compare-StockExternalStateSnapshot $before $metadataAfter `
    'standard_server_diagnostic'
Assert-True ($metadata.metadata_only_changes -eq 1 -and
    $metadata.scope_kinds -contains 'steam_hlfx_tree|last_write_changed') `
    'HLFx metadata-only drift was not typed.'

$directoryBefore = New-StockExternalStateSnapshot @(
    (New-TestEntry 'steam_other_monitored_entry' '.' -Kind 'directory' `
        -Size 0 -Sha256 '' -Attributes 16)) 'idle_control'
$directoryAfter = New-StockExternalStateSnapshot @(
    (New-TestEntry 'steam_other_monitored_entry' '.' -Kind 'directory' `
        -Size 0 -Sha256 '' -LastWriteTicks 11 -Attributes 16)) 'idle_control'
$directory = Compare-StockExternalStateSnapshot $directoryBefore $directoryAfter `
    'idle_control'
Assert-True ($directory.scope_kinds -contains
    'steam_other_monitored_entry|directory_metadata_changed') `
    'Directory metadata was mislabeled as content drift.'

$identityAfter = New-StockExternalStateSnapshot @(
    (New-TestEntry 'app_manifest' '.' `
        -Identity '00000001:0000000000000002'), $hlfx) 'wfp_preflight'
$identity = Compare-StockExternalStateSnapshot $before $identityAfter 'wfp_preflight'
Assert-True ($identity.identity_replacements -eq 1) `
    'Identity replacement was not detected.'

$identityAndDigestAfter = New-StockExternalStateSnapshot @(
    (New-TestEntry 'app_manifest' '.' `
        -Identity '00000001:0000000000000003' -Sha256 ('D' * 64) `
        -LastWriteTicks 12), $hlfx) 'wfp_preflight'
$identityAndDigest = Compare-StockExternalStateSnapshot `
    $before $identityAndDigestAfter 'wfp_preflight'
Assert-True ($identityAndDigest.identity_replacements -eq 1 -and
    $identityAndDigest.digest_changes -eq 1 -and
    $identityAndDigest.content_changes -eq 1 -and
    $identityAndDigest.timestamp_changes -eq 1 -and
    $identityAndDigest.changes[0].identity_changed -and
    $identityAndDigest.changes[0].content_digest_changed) `
    'Identity replacement hid an orthogonal digest or timestamp change.'

$presenceAfter = New-StockExternalStateSnapshot @(
    $app, (New-TestEntry 'diagnostic_output' 'ignored/private.json')) `
    'private_server_diagnostic'
$presence = Compare-StockExternalStateSnapshot `
    (New-StockExternalStateSnapshot @($app, $hlfx) 'private_server_diagnostic') `
    $presenceAfter 'private_server_diagnostic'
Assert-True ($presence.created -eq 1 -and $presence.removed -eq 1 -and
    $presence.scope_kinds -contains 'diagnostic_output|created') `
    'Created/removed scope attribution is incomplete.'

$unreadableAfter = New-StockExternalStateSnapshot @(
    (New-TestEntry 'app_manifest' '.' -ReadStatus 'unreadable'), $hlfx) `
    'post_cleanup_settle'
$unreadable = Compare-StockExternalStateSnapshot $before $unreadableAfter `
    'post_cleanup_settle'
Assert-True ($unreadable.result -ceq 'incomplete' -and
    $unreadable.unreadable -eq 1) 'Unreadable entry was fabricated as zero.'

$researchBefore = New-StockExternalStateSnapshot @(
    (New-TestEntry 'research_protected_entry' 'valve/config.cfg')) `
    'standard_server_diagnostic'
$researchAfter = New-StockExternalStateSnapshot @(
    (New-TestEntry 'research_protected_entry' 'valve/config.cfg' `
        -Identity '00000001:0000000000000002')) `
    'standard_server_diagnostic'
$researchDifference = Compare-StockExternalStateSnapshot `
    $researchBefore $researchAfter 'standard_server_diagnostic'
Assert-True ($researchDifference.scope_kinds -contains
    'research_protected_entry|identity_replaced') `
    'Research restoration identity was not classified separately.'

$public = @(Write-StockExternalDriftPublicOutput $metadata)
Assert-True ($public -contains '[stock-drift] phase=standard_server_diagnostic') `
    'Public drift phase is absent.'
Assert-True (($public -join "`n") -notmatch 'client\.dll|[A-F0-9]{64}|:\\') `
    'Public drift output leaked a path or digest.'
Assert-True ($public -contains '[stock-drift] digest-changes=0' -and
    $public -contains '[stock-drift] timestamp-changes=1') `
    'Public orthogonal drift counters are absent.'

Write-Output '[stock-drift-test] deterministic-order=success'
Write-Output '[stock-drift-test] typed-kinds=success'
Write-Output '[stock-drift-test] diagnostic-output-separated=success'
Write-Output '[stock-drift-test] public-paths=absent'
Write-Output '[stock-drift-test] result=success'
