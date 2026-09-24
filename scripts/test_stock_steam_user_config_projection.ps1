#requires -Version 5.1

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'stock_steam_user_config_projection.ps1')

function Assert-True {
    param([bool]$Value, [string]$Message)
    if (-not $Value) { throw $Message }
}

function Convert-TestText {
    param([string]$Text)
    [Text.UTF8Encoding]::new($false, $true).GetBytes($Text)
}

$beforeText = @'
"UserLocalConfigStore"
{
    "AppInfoChangeNumber" "500"
    "Software"
    {
        "Valve"
        {
            "Steam"
            {
                "apps"
                {
                    "70"
                    {
                        "LastPlayed" "100"
                        "Playtime" "200"
                        "Unchanged" "private-value"
                    }
                    "90"
                    {
                        "LastPlayed" "300"
                    }
                }
            }
        }
    }
}
'@
$afterText = $beforeText.Replace('"100"', '"101"').Replace('"200"', '"203"')
$beforeBytes = Convert-TestText $beforeText
$afterBytes = Convert-TestText $afterText
$before = ConvertFrom-StockValveKeyValuesBytes $beforeBytes
$after = ConvertFrom-StockValveKeyValuesBytes $afterBytes
[Array]::Clear($beforeBytes, 0, $beforeBytes.Length)
[Array]::Clear($afterBytes, 0, $afterBytes.Length)
$serializedProjection = $before | ConvertTo-Json -Depth 8 -Compress
Assert-True ($serializedProjection -notmatch
    'private-value|UserLocalConfigStore|"value"\s*:') `
    'The parser API serialized a raw Steam user-config key or leaf.'
$difference = Compare-StockSteamUserConfigProjection $before $after
Assert-True ($before.entry_class -ceq 'global_steam_user_config') `
    'The bounded parser did not recognize the exact app-70 projection.'
Assert-True ($difference.status -ceq 'match' -and
    $difference.candidate_eligible -and -not $difference.eligible -and
    $difference.unknown_changes -eq 0 -and
    $difference.fatal_changes -eq 0 -and
    $difference.changed_leaf_count -eq 2) `
    'The unpromoted volatile projection policy was not fail-closed.'
Assert-True ($difference.volatile_classes -contains 'playtime_counter' -and
    $difference.volatile_classes -contains 'last_launch_timestamp') `
    'The volatile semantic classes were not retained.'
$originalAcceptedPathSet = $script:StockSteamAcceptedVolatilePathSetSha256
try {
    $script:StockSteamAcceptedVolatilePathSetSha256 =
        $difference.changed_path_set_sha256
    $promotedExact = Compare-StockSteamUserConfigProjection $before $after
    Assert-True (-not $promotedExact.eligible) `
        'Legacy strict behavior trusted an arbitrary injected fingerprint.'
} finally {
    $script:StockSteamAcceptedVolatilePathSetSha256 = $originalAcceptedPathSet
}

$appInfoAfterText = $beforeText.Replace(
    '"AppInfoChangeNumber" "500"', '"AppInfoChangeNumber" "501"')
$appInfoDifference = Compare-StockSteamUserConfigProjection $before `
    (ConvertFrom-StockValveKeyValuesBytes (Convert-TestText $appInfoAfterText))
Assert-True ($appInfoDifference.candidate_eligible -and
    $appInfoDifference.volatile_classes -contains
        'unrelated_steam_metadata' -and
    $appInfoDifference.non_monotonic_changes -eq 0) `
    'The exact global app-info generation counter was not projected.'
$appInfoPolicy = Compare-StockSteamUserConfigProjection $before `
    (ConvertFrom-StockValveKeyValuesBytes (Convert-TestText $appInfoAfterText)) `
    -PolicyId steam-appinfo-change-number-v1
Assert-True ($appInfoPolicy.eligible -and
    $appInfoPolicy.policy_id -ceq 'steam-appinfo-change-number-v1' -and
    $appInfoPolicy.policy_decision -ceq 'explicit_advisory' -and
    $appInfoPolicy.changed_leaf_count -eq 1 -and
    $appInfoPolicy.narrow_numeric_shape_valid -and
    $appInfoPolicy.bounded_source_complete) `
    'The exact versioned AppInfoChangeNumber policy was not accepted.'
$mixedAppInfoText = $appInfoAfterText.Replace('"Playtime" "200"',
    '"Playtime" "201"')
$mixedAppInfo = Compare-StockSteamUserConfigProjection $before `
    (ConvertFrom-StockValveKeyValuesBytes (Convert-TestText $mixedAppInfoText)) `
    -PolicyId steam-appinfo-change-number-v1
Assert-True (-not $mixedAppInfo.eligible -and
    $mixedAppInfo.policy_decision -ceq 'reject' -and
    $mixedAppInfo.changed_leaf_count -eq 2) `
    'The exact-key policy admitted an additional Steam field.'
$unknownPolicyRejected = $false
try {
    [void](Compare-StockSteamUserConfigProjection $before $after `
        -PolicyId steam-unreviewed-v9)
} catch { $unknownPolicyRejected = $true }
Assert-True $unknownPolicyRejected 'An unknown Steam policy ID was accepted.'
$appInfoReverseText = $beforeText.Replace(
    '"AppInfoChangeNumber" "500"', '"AppInfoChangeNumber" "499"')
$appInfoReverse = Compare-StockSteamUserConfigProjection $before `
    (ConvertFrom-StockValveKeyValuesBytes (Convert-TestText $appInfoReverseText))
Assert-True (-not $appInfoReverse.candidate_eligible -and
    $appInfoReverse.non_monotonic_changes -eq 1 -and
    $appInfoReverse.fatal_changes -gt 0) `
    'A non-monotonic global app-info generation change was accepted.'

$otherApplicationAfterText = $beforeText.Replace(
    '"LastPlayed" "300"', '"LastPlayed" "301"')
$otherApplicationDifference = Compare-StockSteamUserConfigProjection $before `
    (ConvertFrom-StockValveKeyValuesBytes (
        Convert-TestText $otherApplicationAfterText))
Assert-True ($otherApplicationDifference.candidate_eligible -and
    $otherApplicationDifference.volatile_classes -contains
        'last_launch_timestamp') `
    'A typed volatile field under another numeric application was not classified.'

$fatalAfterText = $beforeText.Replace(
    '"Unchanged" "private-value"', '"LaunchOptions" "-unsafe"')
$fatal = Compare-StockSteamUserConfigProjection $before `
    (ConvertFrom-StockValveKeyValuesBytes (Convert-TestText $fatalAfterText))
Assert-True (-not $fatal.candidate_eligible -and $fatal.fatal_changes -gt 0) `
    'A fatal semantic branch change was accepted.'

$unknownAfterText = $beforeText.Replace(
    '"Unchanged" "private-value"', '"Unchanged" "changed-private-value"')
$unknown = Compare-StockSteamUserConfigProjection $before `
    (ConvertFrom-StockValveKeyValuesBytes (Convert-TestText $unknownAfterText))
Assert-True ($unknown.unknown_changes -eq 1 -and
    -not $unknown.candidate_eligible) 'An unknown leaf change was accepted.'

# A sanitized analogue of run 20: four existing string leaves under one
# root-global parent change values while their order changes.  Topology alone
# must not hide the value changes, and non-monotonicity is not applicable to
# unknown strings.
$fourUnknownBeforeText = @'
"UserLocalConfigStore"
{
    "Software" { "Valve" { "Steam" { "apps" { "70" { "Stable" "same" } } } } }
    "AuxiliaryState"
    {
        "OpaqueA" "alpha"
        "OpaqueB" "bravo"
        "OpaqueC" "charlie"
        "OpaqueD" "delta"
    }
}
'@
$fourUnknownAfterText = @'
"UserLocalConfigStore"
{
    "Software" { "Valve" { "Steam" { "apps" { "70" { "Stable" "same" } } } } }
    "AuxiliaryState"
    {
        "OpaqueD" "changed-delta"
        "OpaqueC" "changed-charlie"
        "OpaqueB" "changed-bravo"
        "OpaqueA" "changed-alpha"
    }
}
'@
$fourUnknownBefore = ConvertFrom-StockValveKeyValuesBytes `
    (Convert-TestText $fourUnknownBeforeText)
$fourUnknownAfter = ConvertFrom-StockValveKeyValuesBytes `
    (Convert-TestText $fourUnknownAfterText)
$fourUnknownLegacy = Compare-StockSteamUserConfigProjection `
    $fourUnknownBefore $fourUnknownAfter
$fourUnknownNarrow = Compare-StockSteamUserConfigProjection `
    $fourUnknownBefore $fourUnknownAfter `
    -PolicyId steam-appinfo-change-number-v1
Assert-True (-not $fourUnknownBefore.duplicate_path_ambiguity -and
    -not $fourUnknownAfter.duplicate_path_ambiguity -and
    $fourUnknownBefore.node_count -eq $fourUnknownAfter.node_count) `
    'The four-unknown fixture did not retain equal unambiguous topology.'
foreach ($fourUnknownDifference in @($fourUnknownLegacy, $fourUnknownNarrow)) {
    Assert-True ($fourUnknownDifference.status -ceq 'mismatch' -and
        $fourUnknownDifference.changed_leaf_count -eq 4 -and
        $fourUnknownDifference.unknown_changes -eq 4 -and
        $fourUnknownDifference.fatal_changes -eq 4 -and
        $fourUnknownDifference.non_monotonic_changes -eq 0 -and
        $fourUnknownDifference.volatile_classes.Count -eq 0 -and
        -not $fourUnknownDifference.candidate_eligible -and
        -not $fourUnknownDifference.eligible) `
        'Four unknown value changes were lost or treated as numeric evidence.'
}
Assert-True ($fourUnknownNarrow.policy_decision -ceq 'reject') `
    'The narrow policy admitted four root-global unknown leaves.'

$duplicateInsertion = '"Playtime" "200"' + "`r`n" + '"Playtime" "201"'
$duplicateText = $beforeText.Replace('"Playtime" "200"', $duplicateInsertion)
$duplicate = ConvertFrom-StockValveKeyValuesBytes (Convert-TestText $duplicateText)
Assert-True ($duplicate.duplicate_path_ambiguity) `
    'Duplicate keys were not retained as ambiguity.'

foreach ($invalid in @(
        '"root" { "unterminated" "value"',
        '"root" { "bad\q" "value" }',
        '"root" { "key" }')) {
    $failed = $false
    try { [void](ConvertFrom-StockValveKeyValuesBytes (Convert-TestText $invalid)) }
    catch { $failed = $true }
    Assert-True $failed 'Malformed KeyValues input did not fail closed.'
}

$originalDepth = $script:StockSteamConfigMaximumDepth
$originalNodes = $script:StockSteamConfigMaximumNodes
$originalToken = $script:StockSteamConfigMaximumTokenLength
try {
    $script:StockSteamConfigMaximumDepth = 1
    $depthFailed = $false
    try {
        [void](ConvertFrom-StockValveKeyValuesBytes (Convert-TestText `
            '"root" { "nested" { "leaf" "1" } }'))
    } catch { $depthFailed = $true }
    Assert-True $depthFailed 'The KeyValues nesting bound was not enforced.'

    $script:StockSteamConfigMaximumDepth = $originalDepth
    $script:StockSteamConfigMaximumNodes = 1
    $nodeFailed = $false
    try {
        [void](ConvertFrom-StockValveKeyValuesBytes (Convert-TestText `
            '"root" { "one" "1" "two" "2" }'))
    } catch { $nodeFailed = $true }
    Assert-True $nodeFailed 'The KeyValues node bound was not enforced.'

    $script:StockSteamConfigMaximumNodes = $originalNodes
    $script:StockSteamConfigMaximumTokenLength = 3
    $tokenFailed = $false
    try {
        [void](ConvertFrom-StockValveKeyValuesBytes (Convert-TestText `
            '"root" "1"'))
    } catch { $tokenFailed = $true }
    Assert-True $tokenFailed 'The KeyValues token bound was not enforced.'
} finally {
    $script:StockSteamConfigMaximumDepth = $originalDepth
    $script:StockSteamConfigMaximumNodes = $originalNodes
    $script:StockSteamConfigMaximumTokenLength = $originalToken
}

$oversized = New-Object byte[] 1025
$sizeFailed = $false
try { [void](ConvertFrom-StockValveKeyValuesBytes $oversized 1024) }
catch { $sizeFailed = $true }
Assert-True $sizeFailed 'The parser source-size bound was not enforced.'

$public = @(
    "[steam-user-config] entry-class=global_steam_user_config",
    "[steam-user-config] projection=$($difference.status)",
    "[steam-user-config] volatile-classes=$($difference.volatile_classes.Count)",
    "[steam-user-config] unknown-changes=$($difference.unknown_changes)")
Assert-True (($public -join "`n") -notmatch
    'private-value|-unsafe|UserLocalConfigStore|Steam\\apps|[A-F0-9]{64}') `
    'Redacted semantic output leaked a path, value, or digest.'

Write-Output '[steam-user-config-test] bounded-parser=success'
Write-Output '[steam-user-config-test] duplicate-keys=retained'
Write-Output '[steam-user-config-test] four-unknown-leaves=fail-closed'
Write-Output '[steam-user-config-test] protected-projection=match'
Write-Output '[steam-user-config-test] sensitive-values=absent'
Write-Output '[steam-user-config-test] result=success'
