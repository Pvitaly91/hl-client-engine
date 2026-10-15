#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$ModulePath = (Join-Path $PSScriptRoot 'gentest_multiplayer_map.psm1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# This test owns every byte and every filesystem fixture it uses. It never
# reads an installed map, launches a game, builds a target, or modifies Steam.
$module = Import-Module -Name $ModulePath -Force -PassThru
$script:PassedTests = 0

function Assert-Test {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "FAIL: $Message" }
}

function Get-OwnedHash {
    param([byte[]]$Bytes)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes))
}

function Assert-BytesEqual {
    param([byte[]]$Actual, [byte[]]$Expected, [string]$Message)
    Assert-Test ($Actual.Length -eq $Expected.Length) "$Message (length)"
    for ($index = 0; $index -lt $Expected.Length; ++$index) {
        Assert-Test ($Actual[$index] -eq $Expected[$index]) "$Message (byte $index)"
    }
}

function Assert-Rejected {
    param([string]$Name, [scriptblock]$Action)
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert-Test $rejected "accepted $Name"
    ++$script:PassedTests
}

$firstStart = @'
{
"origin" "-128 0 36"
"angles" "0 90 0"
"classname" "info_player_start"
}
'@
$secondStart = @'
{
"classname" "info_player_start"
"origin" "128 0 37"
"angle" "270"
}
'@
$ownedEntities = @'
{
"classname" "worldspawn"
"message" "owned braces { and } plus a Latin1 character: é"
"wad" "C:\owned-fixture\unused.wad"
}
'@ + "`n" + $firstStart + "`n" + @'
{
"classname" "owned_fixture_marker"
"message" "literal info_player_start and info_player_deathmatch are not classnames"
}
'@ + "`n" + $secondStart + "`n"

function New-OwnedBsp {
    param([AllowEmptyString()][string]$EntityText = $ownedEntities,
          [switch]$WithoutTerminalNul)
    [byte[]]$entityBytes = [Text.Encoding]::Latin1.GetBytes($EntityText)
    $entityLength = $entityBytes.Length + [int](-not $WithoutTerminalNul)
    $firstOtherOffset = 124 + $entityLength
    $firstOtherOffset += (4 - ($firstOtherOffset % 4)) % 4
    # Every non-entity lump has original test-owned bytes. The unaligned tail
    # makes the converter's append alignment observable independently.
    [byte[]]$bytes = [byte[]]::new($firstOtherOffset + (14 * 4) + 3)
    [Array]::Copy([BitConverter]::GetBytes([int]30), 0, $bytes, 0, 4)
    [Array]::Copy([BitConverter]::GetBytes([int]124), 0, $bytes, 4, 4)
    [Array]::Copy([BitConverter]::GetBytes([int]$entityLength), 0, $bytes, 8, 4)
    [Array]::Copy($entityBytes, 0, $bytes, 124, $entityBytes.Length)
    for ($lump = 1; $lump -lt 15; ++$lump) {
        $offset = $firstOtherOffset + (($lump - 1) * 4)
        [Array]::Copy([BitConverter]::GetBytes([int]$offset), 0, $bytes, 4 + ($lump * 8), 4)
        [Array]::Copy([BitConverter]::GetBytes([int]4), 0, $bytes, 8 + ($lump * 8), 4)
        for ($byte = 0; $byte -lt 4; ++$byte) { $bytes[$offset + $byte] = [byte](($lump * 13 + $byte) % 256) }
    }
    $bytes[$bytes.Length - 3] = 0xED
    $bytes[$bytes.Length - 2] = 0xFE
    $bytes[$bytes.Length - 1] = 0xDA
    return ,$bytes
}

function Set-OwnedInt32 {
    param([byte[]]$Bytes, [int]$Offset, [int]$Value)
    [byte[]]$changed = $Bytes.Clone()
    [Array]::Copy([BitConverter]::GetBytes($Value), 0, $changed, $Offset, 4)
    return ,$changed
}

function Assert-ConverterRejected {
    param([string]$Name, [byte[]]$Bytes)
    [byte[]]$before = $Bytes.Clone()
    Assert-Rejected $Name { ConvertTo-GentestMultiplayerBsp -SourceBytes $Bytes }
    Assert-BytesEqual $Bytes $before "rejected input mutated: $Name"
}

$exports = @($module.ExportedFunctions.Keys | Sort-Object)
Assert-Test (($exports -join ',') -ceq 'ConvertTo-GentestMultiplayerBsp,New-GentestMultiplayerMap') 'unexpected public module surface'
$writerCommand = Get-Command New-GentestMultiplayerMap -Module $module.Name
Assert-Test (@($writerCommand.Parameters.Keys | Where-Object { $_ -match '(?i)hash|pin|sourcebytes' }).Count -eq 0) 'public source-pin bypass'
$defaultPin = & $module { $script:PinnedGentestSha256 }
Assert-Test ($defaultPin -ceq '9338A39EAA14332625A8F4935DED3345A1CD9EC3ABB141750590091A4D21D7A2') 'production source pin changed'

[byte[]]$source = New-OwnedBsp
[byte[]]$sourceBefore = $source.Clone()
$converted = ConvertTo-GentestMultiplayerBsp -SourceBytes $source
Assert-Test ($converted.Bytes -is [byte[]]) 'converter did not return owning bytes'
Assert-Test ($converted.EntityCountBefore -eq 4 -and $converted.EntityCountAfter -eq 6) 'entity counts do not include exactly two added entities'
Assert-Test ($converted.OriginalStartCount -eq 2 -and $converted.DeathmatchSpawnCount -eq 2) 'spawn counts incorrect'
Assert-Test ((@($converted.SpawnOrigins) -join '|') -ceq '-128 0 36|128 0 37') 'spawn origins changed or reordered'
Assert-BytesEqual $source $sourceBefore 'converter changed its source array'

[byte[]]$output = $converted.Bytes
$newOffset = [BitConverter]::ToInt32($output, 4)
$newLength = [BitConverter]::ToInt32($output, 8)
Assert-Test ($newOffset -ge $source.Length -and ($newOffset % 4) -eq 0) 'replacement entity lump not appended with four-byte alignment'
Assert-Test ($output.Length -eq $newOffset + $newLength) 'unexpected bytes after replacement entity lump'
for ($index = 0; $index -lt $source.Length; ++$index) {
    if ($index -ge 4 -and $index -lt 12) { continue }
    Assert-Test ($output[$index] -eq $source[$index]) "source prefix changed outside lump-zero pair at $index"
}
for ($index = $source.Length; $index -lt $newOffset; ++$index) {
    Assert-Test ($output[$index] -eq 0) 'append alignment padding not zero'
}
Assert-Test ($output[$newOffset + $newLength - 1] -eq 0) 'missing output terminal NUL'
$newEntities = [Text.Encoding]::Latin1.GetString($output, $newOffset, $newLength - 1)
Assert-Test ($newEntities.StartsWith($ownedEntities, [StringComparison]::Ordinal)) 'original entity text prefix changed'
$addedEntities = $newEntities.Substring($ownedEntities.Length)
Assert-Test (-not $addedEntities.Contains([char]0)) 'interior output NUL'
Assert-Test ([regex]::Matches($addedEntities, '"classname"\s+"info_player_deathmatch"').Count -eq 2) 'not exactly two new deathmatch classnames'
Assert-Test ([regex]::Matches($addedEntities, '"classname"\s+"info_player_start"').Count -eq 0) 'added single-player records'
Assert-Test ($addedEntities.Contains($firstStart.Replace('"info_player_start"', '"info_player_deathmatch"')) -and
             $addedEntities.Contains($secondStart.Replace('"info_player_start"', '"info_player_deathmatch"'))) 'spawn blocks changed beyond classname token'
Assert-BytesEqual (ConvertTo-GentestMultiplayerBsp -SourceBytes $source).Bytes $output 'converter nondeterministic'
++ $script:PassedTests

foreach ($case in @(
    @{ Name='empty header'; Bytes=[byte[]]::new(0) },
    @{ Name='truncated header'; Bytes=[byte[]]::new(123) },
    @{ Name='wrong version'; Bytes=(Set-OwnedInt32 $source 0 29) },
    @{ Name='negative entity offset'; Bytes=(Set-OwnedInt32 $source 4 -1) },
    @{ Name='negative entity length'; Bytes=(Set-OwnedInt32 $source 8 -1) },
    @{ Name='empty entity lump'; Bytes=(Set-OwnedInt32 $source 8 0) },
    @{ Name='entity overlaps header'; Bytes=(Set-OwnedInt32 $source 4 123) },
    @{ Name='entity offset outside source'; Bytes=(Set-OwnedInt32 $source 4 ([int]::MaxValue)) },
    @{ Name='entity length outside source'; Bytes=(Set-OwnedInt32 $source 8 ([int]::MaxValue)) },
    @{ Name='other lump overlaps header'; Bytes=(Set-OwnedInt32 $source 12 123) },
    @{ Name='other lump negative length'; Bytes=(Set-OwnedInt32 $source 16 -1) },
    @{ Name='other lump overlaps entities'; Bytes=(Set-OwnedInt32 $source 12 124) },
    @{ Name='other lump outside source'; Bytes=(Set-OwnedInt32 $source 12 $source.Length) },
    @{ Name='other lumps overlap'; Bytes=(Set-OwnedInt32 $source 20 ([BitConverter]::ToInt32($source,12))) },
    @{ Name='missing terminal NUL'; Bytes=(New-OwnedBsp -WithoutTerminalNul) },
    @{ Name='multiple terminal NULs'; Bytes=(New-OwnedBsp ($ownedEntities + [char]0)) },
    @{ Name='interior NUL'; Bytes=(New-OwnedBsp ($ownedEntities.Replace('owned_fixture_marker', "owned$([char]0)fixture"))) }
)) { Assert-ConverterRejected $case.Name $case.Bytes }

foreach ($case in @(
    @{ Name='missing closing brace'; Text=$ownedEntities.TrimEnd().Substring(0,$ownedEntities.TrimEnd().Length - 1) },
    @{ Name='stray closing brace'; Text=$ownedEntities + '}' },
    @{ Name='bare token'; Text=$ownedEntities + '{ classname "owned" }' },
    @{ Name='dangling quoted key'; Text=$ownedEntities + '{ "classname" }' },
    @{ Name='unterminated quote'; Text=$ownedEntities + '{ "classname" "owned_extra" "message" "unclosed' },
    @{ Name='escaped quote is unsupported'; Text=$ownedEntities + '{ "classname" "owned_extra" "message" "a\"b" }' },
    @{ Name='empty key'; Text=$ownedEntities + '{ "classname" "owned_extra" "" "owned" }' },
    @{ Name='control byte in key'; Text=$ownedEntities + "{ `"classname`" `"owned_extra`" `"bad$([char]1)key`" `"owned`" }" },
    @{ Name='first entity not worldspawn'; Text=$ownedEntities.Replace('"classname" "worldspawn"','"classname" "owned_world"') },
    @{ Name='duplicate world classname'; Text=$ownedEntities.Replace('"classname" "worldspawn"', '"classname" "worldspawn" "CLASSNAME" "worldspawn"') },
    @{ Name='missing start'; Text=$ownedEntities.Replace($secondStart, '') },
    @{ Name='third start'; Text=$ownedEntities + $secondStart.Replace('128 0 37','384 0 37') },
    @{ Name='existing deathmatch spawn'; Text=$ownedEntities.Replace('"classname" "owned_fixture_marker"','"classname" "info_player_deathmatch"') },
    @{ Name='duplicate start classname'; Text=$ownedEntities.Replace($firstStart, $firstStart.Replace('"classname" "info_player_start"','"classname" "info_player_start" "CLASSNAME" "info_player_start"')) },
    @{ Name='duplicate start origin'; Text=$ownedEntities.Replace('"origin" "-128 0 36"','"origin" "-128 0 36" "ORIGIN" "-128 0 36"') },
    @{ Name='missing start origin'; Text=$ownedEntities.Replace('"origin" "-128 0 36"','"angles" "0 90 0"') },
    @{ Name='triggered or targetnamed start'; Text=$ownedEntities.Replace('"origin" "-128 0 36"','"origin" "-128 0 36" "targetname" "owned_target"') },
    @{ Name='master-conditioned start'; Text=$ownedEntities.Replace('"origin" "-128 0 36"','"origin" "-128 0 36" "master" "owned_master"') },
    @{ Name='NaN origin'; Text=$ownedEntities.Replace('-128 0 36','NaN 0 36') },
    @{ Name='infinite origin'; Text=$ownedEntities.Replace('-128 0 36','Infinity 0 36') },
    @{ Name='origin outside supported bounds'; Text=$ownedEntities.Replace('-128 0 36','-32769 0 36') },
    @{ Name='comma origin'; Text=$ownedEntities.Replace('-128 0 36','-128,0,36') },
    @{ Name='short origin'; Text=$ownedEntities.Replace('-128 0 36','-128 0') },
    @{ Name='extra origin component'; Text=$ownedEntities.Replace('-128 0 36','-128 0 36 7') },
    @{ Name='too-close XY origins'; Text=$ownedEntities.Replace('128 0 37','127.999 0 37') },
    @{ Name='height does not replace XY separation'; Text=$ownedEntities.Replace('128 0 37','-128 0 1000') },
    @{ Name='invalid angles'; Text=$ownedEntities.Replace('0 90 0','0 NaN 0') },
    @{ Name='invalid angle'; Text=$ownedEntities.Replace('"angle" "270"','"angle" "Infinity"') }
)) { Assert-ConverterRejected $case.Name (New-OwnedBsp $case.Text) }

# Filesystem cases are confined to a unique retained temporary tree. The fake
# project and fake research root are siblings, never the real repository/root.
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('hlclient gentest multiplayer no-stock ' + [guid]::NewGuid().ToString('N'))
$fakeProject = Join-Path $scratch 'project'
[void][IO.Directory]::CreateDirectory($fakeProject)
$ownedPin = Get-OwnedHash $source

function New-OwnedResearch {
    param([string]$Name, [string]$RootOverride)
    $research = if ($RootOverride) { $RootOverride } else { Join-Path $scratch $Name }
    [void][IO.Directory]::CreateDirectory((Join-Path $research 'valve\maps'))
    [IO.File]::WriteAllBytes((Join-Path $research 'valve\maps\gentest.bsp'), $source)
    [IO.File]::WriteAllText((Join-Path $research '.hlclient-research-isolated'), 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1')
    [IO.File]::WriteAllText((Join-Path $research '.hlclient-research-preparation.json'),
        '{"schema":"hlclient.stock-runtime-research-preparation.v3","preparation_status":"exact-materialized-copy-verified"}')
    return $research
}

function Get-OwnedInventory {
    param([string]$Root)
    return @((Get-ChildItem -LiteralPath $Root -File -Recurse -Force | Sort-Object FullName | ForEach-Object {
        $_.FullName.Substring($Root.Length).Replace('\','/') + '|' + $_.Length + '|' +
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash + '|' + $_.LastWriteTimeUtc.Ticks
    })) -join "`n"
}

function Invoke-OwnedWriter {
    param([string]$ResearchRoot, [string]$RepositoryRoot = $fakeProject, [switch]$CheckOnly)
    # Scoped dependency injection is intentionally private to the imported test
    # module. The real entry point has no pin/hash-bypass parameter or switch.
    & $module {
        param($root, $repository, $check, $pin)
        $savedPin = $script:PinnedGentestSha256
        try {
            $script:PinnedGentestSha256 = $pin
            New-GentestMultiplayerMap -ResearchRoot $root -RepositoryRoot $repository -CheckOnly:$check
        } finally { $script:PinnedGentestSha256 = $savedPin }
    } $ResearchRoot $RepositoryRoot ([bool]$CheckOnly) $ownedPin
}

$wrongPinRoot = New-OwnedResearch 'wrong pin'
$before = Get-OwnedInventory $wrongPinRoot
Assert-Rejected 'unapproved source hash' { New-GentestMultiplayerMap -ResearchRoot $wrongPinRoot -RepositoryRoot $fakeProject }
Assert-Test ((Get-OwnedInventory $wrongPinRoot) -ceq $before) 'wrong-pin rejection changed fixture files'

$research = New-OwnedResearch 'approved research'
$before = Get-OwnedInventory $research
Invoke-OwnedWriter -ResearchRoot $research -CheckOnly | Out-Null
Assert-Test ((Get-OwnedInventory $research) -ceq $before) 'CheckOnly wrote or changed a fixture'
++ $script:PassedTests

$destination = Join-Path $research 'valve\maps\gentest_mp.bsp'
Invoke-OwnedWriter -ResearchRoot $research | Out-Null
Assert-Test (Test-Path -LiteralPath $destination -PathType Leaf) 'writer did not create derived fixture'
Assert-BytesEqual ([IO.File]::ReadAllBytes($destination)) $output 'writer output differs from pure converter'
Assert-BytesEqual ([IO.File]::ReadAllBytes((Join-Path $research 'valve\maps\gentest.bsp'))) $source 'writer changed original source'
$afterCreation = Get-OwnedInventory $research
$remaining = @($afterCreation -split "`n" | Where-Object { -not $_.StartsWith('/valve/maps/gentest_mp.bsp|', [StringComparison]::Ordinal) }) -join "`n"
Assert-Test ($remaining -ceq $before) 'writer changed existing fixture files or created unexpected files'
++ $script:PassedTests
Invoke-OwnedWriter -ResearchRoot $research | Out-Null
Invoke-OwnedWriter -ResearchRoot $research -CheckOnly | Out-Null
Assert-Test ((Get-OwnedInventory $research) -ceq $afterCreation) 'already-ready path overwrote or changed files'
++ $script:PassedTests

$conflictRoot = New-OwnedResearch 'destination conflict'
$conflictPath = Join-Path $conflictRoot 'valve\maps\gentest_mp.bsp'
[IO.File]::WriteAllBytes($conflictPath, [byte[]]@(1,2,3,4))
$conflictBefore = Get-OwnedInventory $conflictRoot
foreach ($check in @($true,$false)) {
    Assert-Rejected "conflicting destination (CheckOnly=$check)" { Invoke-OwnedWriter -ResearchRoot $conflictRoot -CheckOnly:$check }
    Assert-Test ((Get-OwnedInventory $conflictRoot) -ceq $conflictBefore) 'conflicting destination was overwritten'
}

$nestedRoot = New-OwnedResearch -RootOverride (Join-Path $fakeProject 'research forbidden')
Assert-Rejected 'research inside repository' { Invoke-OwnedWriter -ResearchRoot $nestedRoot }
Assert-Rejected 'repository inside research' { Invoke-OwnedWriter -ResearchRoot $research -RepositoryRoot (Join-Path $research 'valve') }
$steamRoot = New-OwnedResearch -RootOverride (Join-Path $scratch 'steamapps\common\owned research')
Assert-Rejected 'research inside Steam tree' { Invoke-OwnedWriter -ResearchRoot $steamRoot }
Assert-Rejected 'relative research root' { Invoke-OwnedWriter -ResearchRoot 'relative-owned-research' }
Assert-Rejected 'missing research root' { Invoke-OwnedWriter -ResearchRoot (Join-Path $scratch 'does not exist') }

$unprepared = New-OwnedResearch 'wrong preparation marker'
[IO.File]::WriteAllText((Join-Path $unprepared '.hlclient-research-isolated'), 'owned but unprepared')
Assert-Rejected 'wrong preparation marker' { Invoke-OwnedWriter -ResearchRoot $unprepared }

if ($IsWindows) {
    $junctionTarget = New-OwnedResearch 'junction target'
    $junctionPath = Join-Path $scratch 'research junction'
    [void](New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget)
    $junctionBefore = Get-OwnedInventory $junctionTarget
    Assert-Rejected 'research-root junction' { Invoke-OwnedWriter -ResearchRoot $junctionPath }
    Assert-Test ((Get-OwnedInventory $junctionTarget) -ceq $junctionBefore) 'junction rejection changed target'

    $linkedSource = New-OwnedResearch 'source hardlink'
    $ownedLinkTarget = Join-Path $scratch 'owned hardlink target.bsp'
    [IO.File]::WriteAllBytes($ownedLinkTarget, $source)
    # Remove only the exact newly-created test fixture before replacing it with
    # a link; no recursive cleanup of any tree is performed.
    $linkedSourcePath = Join-Path $linkedSource 'valve\maps\gentest.bsp'
    Remove-Item -LiteralPath $linkedSourcePath
    [void](New-Item -ItemType HardLink -Path $linkedSourcePath -Target $ownedLinkTarget)
    Assert-Rejected 'source hardlink' { Invoke-OwnedWriter -ResearchRoot $linkedSource }
    Assert-BytesEqual ([IO.File]::ReadAllBytes($ownedLinkTarget)) $source 'hardlink target changed'

    $linkedDestination = New-OwnedResearch 'destination hardlink'
    $ownedOutputTarget = Join-Path $scratch 'owned output hardlink target.bsp'
    [IO.File]::WriteAllBytes($ownedOutputTarget, $output)
    [void](New-Item -ItemType HardLink -Path (Join-Path $linkedDestination 'valve\maps\gentest_mp.bsp') -Target $ownedOutputTarget)
    Assert-Rejected 'destination hardlink' { Invoke-OwnedWriter -ResearchRoot $linkedDestination }
    Assert-BytesEqual ([IO.File]::ReadAllBytes($ownedOutputTarget)) $output 'output hardlink target changed'
}

Assert-Test ((& $module { $script:PinnedGentestSha256 }) -ceq $defaultPin) 'test pin injection leaked'
Write-Output "PASS gentest multiplayer: $script:PassedTests no-stock cases; exact entity-prefix/header-only preservation, bounded grammar/spawns, pinned/no-overwrite/read-only paths. Stock launches=0; builds=0."
Write-Output "Retained test-owned fixtures: $scratch"
