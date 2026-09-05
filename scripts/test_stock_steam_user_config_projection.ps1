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
    Assert-True $promotedExact.eligible `
        'An explicitly promoted exact volatile path set was not accepted.'
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
Write-Output '[steam-user-config-test] protected-projection=match'
Write-Output '[steam-user-config-test] sensitive-values=absent'
Write-Output '[steam-user-config-test] result=success'
