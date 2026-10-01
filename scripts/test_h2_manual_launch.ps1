#requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$FixtureChild,
    [string]$FixtureRoot,
    [ValidateSet('reference', 'off')][string]$Mode = 'reference',
    [ValidateSet('Fast','Strict')][string]$ValidationMode = 'Fast',
    [string]$SelectedMap,
    [ValidateSet(0,50)][int]$TestStartHealth = 0,
    [switch]$MuteGlockFireSound,
    [switch]$RemoteAudioPeer,
    [Nullable[int]]$DurationSeconds,
    [switch]$NoTimeLimit,
    [ValidateSet('manual', 'damage-respawn-check')][string]$SelectedScenario = 'manual',
    [ValidateSet('normal', 'silent', 'throw', 'ambiguous', 'incomplete', 'foreign-report')][string]$Case = 'normal'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$launcher = Join-Path $root 'Start-HLClient-H2-Manual.ps1'
$pwsh = Join-Path $PSHOME 'pwsh.exe'
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($launcher, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors -join "`n") }
# Load only named function definitions, never the launcher's top-level live path.
foreach ($name in @('Get-H2ManualConfiguration', 'Assert-H2ManualFilesAndContract', 'Invoke-H2ManualManagedRunner')) {
    $definition = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    if ($null -eq $definition) { throw "Missing function $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}
. (Join-Path $root 'scripts\external_manual_map.ps1')
if ($FixtureChild) {
    $mapArgument = @{}
    if ($SelectedMap) { $mapArgument.SelectedMap = $SelectedMap }
    if ($null -ne $DurationSeconds) { $mapArgument.SelectedDurationSeconds = $DurationSeconds }
    $configuration = Get-H2ManualConfiguration -Root $FixtureRoot -Mode $Mode -ResearchRoot (Join-Path $FixtureRoot 'research game') -SteamRoot (Join-Path $FixtureRoot 'Steam apps') -SelectedScenario $SelectedScenario -SelectedTestStartHealth $TestStartHealth -SelectedMuteGlockFireSound:$MuteGlockFireSound -SelectedRemoteAudioPeer:$RemoteAudioPeer -SelectedNoTimeLimit:$NoTimeLimit -SelectedValidationMode $ValidationMode @mapArgument
    $env:HLCLIENT_MANUAL_TEST_VALIDATION = $ValidationMode
    $fakeRunner = Join-Path $FixtureRoot 'scripts\managed runner fixture.ps1'
    if ((Get-Content -LiteralPath $fakeRunner -TotalCount 1) -cne
        '# No-stock fixture: only validate arguments and publish a synthetic report.') {
        throw 'Fixture guard rejected runner; actual runner must never run in this test.'
    }
    $env:HLCLIENT_MANUAL_TEST_CASE = $Case
    $env:HLCLIENT_MANUAL_TEST_START_HEALTH = [string]$TestStartHealth
    $env:HLCLIENT_MANUAL_TEST_MUTE_FIRE = if ($MuteGlockFireSound) { 'true' } else { 'false' }
    $env:HLCLIENT_MANUAL_TEST_REMOTE_PEER = if ($RemoteAudioPeer) { 'true' } else { 'false' }
    $env:HLCLIENT_MANUAL_TEST_UNLIMITED = if ($NoTimeLimit) { 'true' } else { 'false' }
    $env:HLCLIENT_MANUAL_TEST_DURATION = if ($configuration.Contains('ProjectClientDurationSeconds')) { [string]$configuration.ProjectClientDurationSeconds } else { '0' }
    $env:HLCLIENT_MANUAL_TEST_MAP = if ($SelectedMap) { $SelectedMap } else { 'crossfire' }
    $env:HLCLIENT_MANUAL_TEST_INPUT = if ($SelectedScenario -eq 'damage-respawn-check') {
        'scripted-damage-respawn-check' } else { 'keyboard-mouse' }
    $LASTEXITCODE = 117 # Deliberately stale; child status must replace it.
    $code = Invoke-H2ManualManagedRunner -PowerShell $pwsh -Runner $fakeRunner -Configuration $configuration
    exit $code
}

function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
}
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('H2 manual path with spaces ' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path (Join-Path $scratch 'scripts'))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests\h2_manual_runner_fixture.ps1') `
    -Destination (Join-Path $scratch 'scripts\managed runner fixture.ps1')
Write-Host "No-stock fixture root (retained): $scratch"
$PSNativeCommandUseErrorActionPreference = $false
foreach ($scenario in @(
    @{Mode='reference'; Case='normal'; Exit=0; Map=''; Scenario='manual'},
    @{Mode='reference'; Case='normal'; Exit=0; Map=''; Scenario='manual'; MuteFire=$true},
    @{Mode='reference'; Case='normal'; Exit=0; Map=''; Scenario='manual'; Peer=$true},
    @{Mode='off'; Case='normal'; Exit=23; Map=''; Scenario='manual'; Peer=$true},
    @{Mode='reference'; Case='normal'; Exit=0; Map=''; Scenario='manual'; Peer=$true; Duration=300},
    @{Mode='off'; Case='normal'; Exit=23; Map=''; Scenario='manual'; Peer=$true; Unlimited=$true},
    @{Mode='reference'; Case='normal'; Exit=0; Map=''; Scenario='manual'; Duration=86400; Validation='Strict'},
    @{Mode='reference'; Case='normal'; Exit=0; Map=''; Scenario='manual'; Unlimited=$true},
    @{Mode='off'; Case='normal'; Exit=23; Map=''; Scenario='manual'},
    @{Mode='reference'; Case='silent'; Exit=9; Map=''; Scenario='manual'},
    @{Mode='reference'; Case='throw'; Exit=1; Map=''; Scenario='manual'},
    @{Mode='reference'; Case='ambiguous'; Exit=0; Map=''; Scenario='manual'},
    @{Mode='reference'; Case='incomplete'; Exit=2; Map=''; Scenario='manual'},
    @{Mode='reference'; Case='foreign-report'; Exit=2; Map=''; Scenario='manual'},
    @{Mode='reference'; Case='normal'; Exit=0; Map='crossfire'; Scenario='manual'},
    @{Mode='off'; Case='normal'; Exit=23; Map='boot_camp'; Scenario='manual'},
    @{Mode='reference'; Case='normal'; Exit=0; Map='stalkyard'; Scenario='manual'},
    @{Mode='reference'; Case='normal'; Exit=0; Map='crossfire'; Scenario='damage-respawn-check'},
    @{Mode='reference'; Case='normal'; Exit=0; Map='crossfire'; Scenario='manual'; Health=50},
    @{Mode='reference'; Case='normal'; Exit=0; Map='crossfire'; Scenario='manual'; Health=50; Validation='Strict'}
)) {
    $health = if ($scenario.ContainsKey('Health')) { $scenario.Health } else { 0 }
    $validation = if ($scenario.ContainsKey('Validation')) { $scenario.Validation } else { 'Fast' }
    $muteFire = $scenario.ContainsKey('MuteFire') -and $scenario.MuteFire
    $peer = $scenario.ContainsKey('Peer') -and $scenario.Peer
    $timing = @{}
    if ($scenario.ContainsKey('Duration')) { $timing.DurationSeconds = $scenario.Duration }
    $unlimited = $scenario.ContainsKey('Unlimited') -and $scenario.Unlimited
    $output = @(& $pwsh -NoProfile -File $PSCommandPath -FixtureChild -FixtureRoot $scratch `
        -Mode $scenario.Mode -Case $scenario.Case -SelectedMap $scenario.Map -SelectedScenario $scenario.Scenario -TestStartHealth $health -MuteGlockFireSound:$muteFire -RemoteAudioPeer:$peer -NoTimeLimit:$unlimited -ValidationMode $validation @timing 2>&1)
    $actualExit = $LASTEXITCODE
    $text = $output -join "`n"
    Assert-Test ($actualExit -eq $scenario.Exit) "child exit $actualExit instead of $($scenario.Exit)"
    Assert-Test ($text.Contains('fixture stderr visible')) 'stderr lost'
    Assert-Test ($text.Contains("exit code: $actualExit")) 'actual exit summary missing'
    if ($scenario.Case -eq 'normal') {
        Assert-Test ($text.Contains("fixture arguments verified: prediction=$($scenario.Mode)")) 'arguments/space paths lost'
        $expectedMap = if ($scenario.Map) { $scenario.Map } else { 'crossfire' }
        Assert-Test ($text.Contains("map=$expectedMap")) 'selected/default map lost'
        Assert-Test ($text.Contains("mute-fire=$muteFire")) 'mute-fire diagnostic switch lost'
        $seconds = if ($scenario.Scenario -ne 'manual' -or $unlimited) { 0 } elseif ($timing.Count) { $scenario.Duration } else { 45 }
        Assert-Test ($text.Contains("fixture timing verified: seconds=$seconds unlimited=$unlimited")) 'manual timing lost'
        Assert-Test ($text.Contains('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\functional-smoke-wrapper.json')) 'exact report missing'
        Assert-Test (-not $text.Contains('bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\functional-smoke-wrapper.json')) 'unrelated report chosen'
    } elseif ($scenario.Case -eq 'incomplete') {
        Assert-Test ($text.Contains('Run report:') -and
            $text.Contains('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\functional-smoke-wrapper.incomplete.json')) 'actual incomplete report missing'
        Assert-Test ($text.Contains('application_control_error=unsupported_opcode')) 'runtime cause not forwarded'
        Assert-Test ($text.Contains('restoration_status=unknown')) 'unknown restoration overwritten'
    } elseif ($scenario.Case -eq 'foreign-report') {
        Assert-Test (-not $text.Contains('Run report:')) 'foreign run report accepted'
        Assert-Test ($text.Contains('did not publish an unambiguous transaction-bound wrapper report')) 'foreign path did not fail closed'
    } else {
        Assert-Test ($text.Contains('Run report unavailable:')) 'missing/ambiguous ID did not fail closed'
    }
    Write-Host "PASS fake runner: prediction=$($scenario.Mode), case=$($scenario.Case), exit=$actualExit"
}

# Actual CheckOnly from a different cwd: parses contract/files, never invokes it.
Push-Location -LiteralPath $scratch
try {
    foreach ($extra in @(@(), @('-Prediction', 'off'), @('-Map', 'crossfire'),
        @('-Map', 'boot_camp'), @('-Map', 'stalkyard'),
        @('-Scenario', 'damage-respawn-check'), @('-MuteGlockFireSound'), @('-RemoteAudioPeer'),
        @('-RemoteAudioPeer','-DurationSeconds','300'), @('-RemoteAudioPeer','-NoTimeLimit'),
        @('-DurationSeconds','86400'), @('-NoTimeLimit','-Prediction','off'))) {
        $output = @(& $pwsh -NoProfile -File $launcher -CheckOnly -SourceOnly @extra 2>&1)
        Assert-Test ($LASTEXITCODE -eq 0) 'CheckOnly failed'
        Assert-Test (($output -join "`n").Contains('CheckOnly passed:')) 'CheckOnly did not reach validation'
        Assert-Test (($output -join "`n").Contains('map=')) 'selected map banner missing'
    }
} finally { Pop-Location }

$runner = Join-Path $root 'scripts\capture_stock_runtime_state.ps1'
$runnerAst = [Management.Automation.Language.Parser]::ParseFile($runner, [ref]$tokens, [ref]$parseErrors)
Assert-Test ($parseErrors.Count -eq 0) 'runner syntax errors'
foreach ($name in @('Get-FunctionalMapEvidence', 'Resolve-SelectedResearchMap',
    'Assert-PathBelowRoot', 'Test-PathAtOrBelow')) {
    $definition = $runnerAst.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    Assert-Test ($null -ne $definition) "missing runner function $name"
    . ([scriptblock]::Create($definition.Extent.Text))
}
$mapDefault = $ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'Map' }
Assert-Test ($mapDefault.DefaultValue.Value -ceq 'crossfire') 'top-level default not crossfire'
$defaultConfiguration = Get-H2ManualConfiguration -Root $root -Mode reference
Assert-Test ($defaultConfiguration.ValidationMode -ceq 'Fast') 'manual default is not Fast'
$validationDefault = $runnerAst.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'ValidationMode' }
Assert-Test ($validationDefault.DefaultValue.Value -ceq 'Strict') 'standalone runner no longer defaults Strict'
Assert-Test (-not $defaultConfiguration.Contains('TestStartHealth')) 'ordinary launcher enables helper'
Assert-Test (-not $defaultConfiguration.Contains('ProjectClientMuteGlockFireSound')) 'ordinary launcher mutes Glock fire'
Assert-Test (-not $defaultConfiguration.Contains('RemoteAudioPeer')) 'ordinary launcher creates peer'
Assert-Test ($defaultConfiguration.ProjectClientDurationSeconds -eq 45) 'default duration changed'
foreach ($bad in @(@{SelectedDurationSeconds=0}, @{SelectedDurationSeconds=86401},
    @{SelectedDurationSeconds=45;SelectedNoTimeLimit=$true},
    @{SelectedScenario='damage-respawn-check';SelectedDurationSeconds=300},
    @{SelectedScenario='damage-respawn-check';SelectedNoTimeLimit=$true})) {
    $rejected=$false
    try { [void](Get-H2ManualConfiguration -Root $root -Mode reference @bad) } catch { $rejected=$true }
    Assert-Test $rejected 'invalid/conflicting/scripted timing accepted'
}
foreach($mode in @('reference','off')) {
    $peerConfig=Get-H2ManualConfiguration -Root $root -Mode $mode -SelectedRemoteAudioPeer
    Assert-Test $peerConfig.RemoteAudioPeer 'peer configuration lost'
    Assert-Test ($peerConfig.ServerPort -eq $defaultConfiguration.ServerPort) 'peer did not share owned endpoint'
}
foreach($bad in @(@{SelectedScenario='damage-respawn-check'},@{SelectedMap='boot_camp'},@{SelectedTestStartHealth=50})) {
    $rejected=$false
    try { [void](Get-H2ManualConfiguration -Root $root -Mode reference -SelectedRemoteAudioPeer @bad) } catch { $rejected=$true }
    Assert-Test $rejected 'peer unsafe combined profile accepted'
}
$mutedConfiguration = Get-H2ManualConfiguration -Root $root -Mode reference -SelectedMuteGlockFireSound
Assert-Test ($mutedConfiguration.ProjectClientMuteGlockFireSound -eq $true) 'manual diagnostic not configured'
$muteRejected = $false
try { [void](Get-H2ManualConfiguration -Root $root -Mode reference -SelectedScenario damage-respawn-check -SelectedMuteGlockFireSound) }
catch { $muteRejected=$_.Exception.Message.Contains('MuteGlockFireSound requires') }
Assert-Test $muteRejected 'diagnostic leaked into scripted scenario'
foreach ($badSetup in @(@{Mode='off';Map='crossfire';Scenario='manual'},
    @{Mode='reference';Map='boot_camp';Scenario='manual'},
    @{Mode='reference';Map='crossfire';Scenario='damage-respawn-check'})) {
    $rejected=$false
    try { [void](Get-H2ManualConfiguration -Root $root -Mode $badSetup.Mode -SelectedMap $badSetup.Map -SelectedScenario $badSetup.Scenario -SelectedTestStartHealth 50) }
    catch { $rejected=$_.Exception.Message.Contains('TestStartHealth requires') }
    Assert-Test $rejected 'incompatible start health accepted'
}
Assert-Test ($defaultConfiguration.Map -ceq 'crossfire') 'configuration default not crossfire'
# Generated inert prerequisite files are inspected only, never executed.
$research = Join-Path $scratch 'selected map research'
$steam = Join-Path $scratch 'selected map Steam apps'
foreach ($relative in @('hlds.exe', '.hlclient-research-isolated', 'valve/maps/boot_camp.bsp')) {
    $path = Join-Path $research $relative
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $path))
    [IO.File]::WriteAllText($path, 'no-stock prerequisite fixture')
}
foreach ($relative in @('appmanifest_70.acf', 'common/Half-Life/steam_api.dll')) {
    $path = Join-Path $steam $relative
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $path))
    [IO.File]::WriteAllText($path, 'no-stock prerequisite fixture')
}
$configuration = Get-H2ManualConfiguration -Root $root -Mode reference -ResearchRoot $research -SteamRoot $steam
$rejected = $false
try { Assert-H2ManualFilesAndContract $runner $configuration }
catch { $rejected = $_.Exception.Message.Contains('crossfire.bsp') }
Assert-Test $rejected 'missing selected crossfire silently fell back to existing boot_camp'
$rejected = $false
try { [void](Resolve-SelectedResearchMap $research 'crossfire') }
catch { $rejected = $_.Exception.Message.Contains('Selected map is missing: valve/maps/crossfire.bsp') }
Assert-Test $rejected 'runner missing selected map error/fallback'
# Positive inverse: no boot_camp prerequisite when only crossfire is selected.
$fixtureBootCamp = Join-Path $research 'valve/maps/boot_camp.bsp'
[IO.File]::Delete($fixtureBootCamp)
[IO.File]::WriteAllText((Join-Path $research 'valve/maps/crossfire.bsp'), 'no-stock prerequisite fixture')
Assert-H2ManualFilesAndContract $runner $configuration
[IO.File]::WriteAllText($fixtureBootCamp, 'no-stock prerequisite fixture')
foreach ($map in @('crossfire', 'boot_camp', 'stalkyard')) {
    $path = Join-Path $research "valve/maps/$map.bsp"
    [IO.File]::WriteAllText($path, 'no-stock prerequisite fixture')
    foreach ($mode in @('reference', 'off')) {
        foreach ($scenario in @('manual', 'damage-respawn-check')) {
            $configuration = Get-H2ManualConfiguration -Root $root -Mode $mode -ResearchRoot $research -SteamRoot $steam -SelectedMap $map -SelectedScenario $scenario
            Assert-H2ManualFilesAndContract $runner $configuration
            Assert-Test ($configuration.Map -ceq $map) 'map changed during validation'
        }
    }
}
foreach ($bad in @('../crossfire', 'maps/crossfire.bsp', 'C:\crossfire', 'crossfire;quit', 'crossfire +quit', 'unknown')) {
    $rejected = $false
    try { [void](Get-H2ManualConfiguration -Root $root -Mode reference -SelectedMap $bad) }
    catch { $rejected = $true }
    Assert-Test $rejected "unsafe/unsupported map accepted: $bad"
    $rejected = $false
    try { [void](Resolve-SelectedResearchMap $research $bad) }
    catch { $rejected = $_.Exception.Message.Contains('Invalid Map') }
    Assert-Test $rejected 'runner unsafe map accepted'
    $output = @(& $pwsh -NoProfile -File $launcher -CheckOnly -SourceOnly -Map $bad 2>&1)
    Assert-Test ($LASTEXITCODE -ne 0) 'top-level unsafe map accepted'
}
foreach ($observed in @($null, 'maps/crossfire.bsp', 'maps/boot_camp.bsp')) {
    $evidence = Get-FunctionalMapEvidence 'crossfire' $observed
    Assert-Test ($evidence.requested_map -ceq 'crossfire') 'request altered by observation'
    Assert-Test ($evidence.expected_serverinfo_map -ceq 'maps/crossfire.bsp') 'wrong expected ServerInfo map'
    Assert-Test ($evidence.observed_serverinfo_map -ceq $observed) 'observed map fabricated/overwritten'
    $expected = if ($null -eq $observed) { 'not_observed' }
        elseif ($observed -ceq 'maps/crossfire.bsp') { 'match' } else { 'mismatch' }
    Assert-Test ($evidence.comparison -ceq $expected) 'map comparison conflates requested and observed'
}
# External selection is a separate, explicit manual-only path. The tiny BSP
# is project-owned test data, not a third-party map and is never launched.
$externalName = 'qa_manual'
$externalBsp = Join-Path $research "valve/maps/$externalName.bsp"
$entityBytes = [Text.Encoding]::ASCII.GetBytes('{"classname" "worldspawn" "wad" "C:\\tools\\HALFLIFE.WAD;custom.wad"}' + "`0")
$fixtureBytes = [byte[]]::new(124 + $entityBytes.Length)
[BitConverter]::GetBytes([int]30).CopyTo($fixtureBytes, 0)
[BitConverter]::GetBytes([int]124).CopyTo($fixtureBytes, 4)
[BitConverter]::GetBytes([int]$entityBytes.Length).CopyTo($fixtureBytes, 8)
$entityBytes.CopyTo($fixtureBytes, 124)
[IO.File]::WriteAllBytes($externalBsp, $fixtureBytes)
$externalConfiguration = Get-H2ManualConfiguration -Root $root -Mode reference `
    -ResearchRoot $research -SteamRoot $steam -SelectedExternalMapBsp $externalBsp
Assert-Test ($externalConfiguration.Map -ceq $externalName -and
    $externalConfiguration.ExternalManualMap) 'external map was not bound to FunctionalSmoke'
Assert-H2ManualFilesAndContract $runner $externalConfiguration
$declaredWads = @(Get-ExternalMapDeclaredWads $externalBsp)
Assert-Test ($declaredWads.Count -eq 2 -and $declaredWads[0] -ceq 'HALFLIFE.WAD' -and
    $declaredWads[1] -ceq 'custom.wad') 'bounded BSP worldspawn WAD inventory failed'
$resPath = Join-Path $research "valve/maps/$externalName.res"
[IO.File]::WriteAllText($resPath, "`"sound/test.wav`"`n")
$resInventory = @(Get-ExternalMapResInventory $research $externalName)
Assert-Test ($resInventory.Count -eq 1 -and $resInventory[0].Name -ceq 'sound/test.wav' -and
    -not $resInventory[0].Present) 'optional RES inventory did not flag missing local resource'
foreach ($candidate in @('../qa_manual.bsp', (Join-Path $scratch 'qa_manual.bsp'),
    'C:\outside\qa_manual.bsp')) {
    $rejected = $false
    try { [void](Resolve-ExternalManualMapBsp $research $candidate $root) } catch { $rejected = $true }
    Assert-Test $rejected "unsafe external path accepted: $candidate"
}
$rejected = $false
try { [void](Get-H2ManualConfiguration -Root $root -Mode reference -ResearchRoot $research `
    -SelectedExternalMapBsp $externalBsp -SelectedValidationMode Strict) } catch { $rejected = $true }
Assert-Test $rejected 'external map unexpectedly accepted Strict inventory mode'
$rejected = $false
try { [void](Get-H2ManualConfiguration -Root $root -Mode reference -ResearchRoot $research `
    -SelectedExternalMapBsp $externalBsp -SelectedTestStartHealth 50) } catch { $rejected = $true }
Assert-Test $rejected 'external map unexpectedly accepted test-health profile'
Write-Host 'PASS no-stock external BSP selection, runner contract, WAD/RES inventory, unsafe path and incompatible-profile rejection.'
# Static checks complement the real fake PowerShell argv checks. No native
# environment-validation mode is invoked: that mode activates WFP.
$runnerText = [IO.File]::ReadAllText($runner)
Assert-Test ($runnerText.Contains("'--game', `$Game, '--map', `$Map")) 'runner/native map forwarding contract changed'
$nativeText = [IO.File]::ReadAllText((Join-Path $root 'apps/hlclient_stock_runtime_orchestrator/main.cpp'))
Assert-Test ($nativeText.Contains('client_spec.arguments.push_back(L"--mute-glock-fire-sound")')) 'native client forwarding missing'
Assert-Test ($nativeText.Contains('options.map = *token;')) 'native map argument parsing missing'
Assert-Test ($nativeText.Contains('L"+map", to_wide_ascii(options.map)')) 'owned HLDS map forwarding changed'
Write-Host 'PASS default/explicit maps, selected required BSP, missing-map no fallback, unsafe maps, all map/prediction/Scenario contracts, requested/observed evidence, native forwarding source audit.'
# Missing file and unknown parameter both fail before any child process is created.
$configuration = Get-H2ManualConfiguration -Root $root -Mode reference
$configuration.ClientPath = Join-Path $scratch 'missing client.exe'
$rejected = $false
try { Assert-H2ManualFilesAndContract (Join-Path $root 'scripts\capture_stock_runtime_state.ps1') $configuration -SourceOnly }
catch { $rejected = $_.Exception.Message.Contains('Required file is missing:') }
Assert-Test $rejected 'missing file did not fail closed'
$configuration = Get-H2ManualConfiguration -Root $root -Mode reference
$configuration['NotARealRunnerFlag'] = 'bad'
$rejected = $false
try { Assert-H2ManualFilesAndContract (Join-Path $root 'scripts\capture_stock_runtime_state.ps1') $configuration -SourceOnly }
catch { $rejected = $_.Exception.Message.Contains('does not support') }
Assert-Test $rejected 'unknown parameter did not fail closed'
Write-Host 'PASS syntax, CheckOnly reference/off, space paths, arguments, stdout/stderr, exact/incomplete same-run reports, foreign-path rejection, exit 0/23/9/1/2, missing-file/contract rejection. Live sessions=0.'
