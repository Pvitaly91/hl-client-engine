#requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$FixtureChild,
    [string]$FixtureRoot,
    [string]$FixtureResearchRoot,
    [ValidateSet('reference', 'off')][string]$Mode = 'reference',
    [ValidateSet('Fast','Strict')][string]$ValidationMode = 'Fast',
    [string]$SelectedMap,
    [string]$SelectedExternalMapBsp,
    [ValidateSet(0,50)][int]$TestStartHealth = 0,
    [switch]$MuteGlockFireSound,
    [switch]$DebugFreezeTracer,
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
    if ($SelectedExternalMapBsp) { $mapArgument.SelectedExternalMapBsp = $SelectedExternalMapBsp }
    if ($null -ne $DurationSeconds) { $mapArgument.SelectedDurationSeconds = $DurationSeconds }
    $fixtureResearch = if ($FixtureResearchRoot) { $FixtureResearchRoot } else { Join-Path $FixtureRoot 'research game' }
    $configuration = Get-H2ManualConfiguration -Root $FixtureRoot -Mode $Mode -ResearchRoot $fixtureResearch -SteamRoot (Join-Path $FixtureRoot 'Steam apps') -SelectedScenario $SelectedScenario -SelectedTestStartHealth $TestStartHealth -SelectedMuteGlockFireSound:$MuteGlockFireSound -SelectedDebugFreezeTracer:$DebugFreezeTracer -SelectedRemoteAudioPeer:$RemoteAudioPeer -SelectedNoTimeLimit:$NoTimeLimit -SelectedValidationMode $ValidationMode @mapArgument
    $env:HLCLIENT_MANUAL_TEST_VALIDATION = $ValidationMode
    $fakeRunner = Join-Path $FixtureRoot 'scripts\managed runner fixture.ps1'
    if ((Get-Content -LiteralPath $fakeRunner -TotalCount 1) -cne
        '# No-stock fixture: only validate arguments and publish a synthetic report.') {
        throw 'Fixture guard rejected runner; actual runner must never run in this test.'
    }
    $env:HLCLIENT_MANUAL_TEST_CASE = $Case
    $env:HLCLIENT_MANUAL_TEST_START_HEALTH = [string]$TestStartHealth
    $env:HLCLIENT_MANUAL_TEST_MUTE_FIRE = if ($MuteGlockFireSound) { 'true' } else { 'false' }
    $env:HLCLIENT_MANUAL_TEST_FREEZE_TRACER = if ($DebugFreezeTracer) { 'true' } else { 'false' }
    $env:HLCLIENT_MANUAL_TEST_REMOTE_PEER = if ($RemoteAudioPeer) { 'true' } else { 'false' }
    $env:HLCLIENT_MANUAL_TEST_EXTERNAL_MAP = if ($SelectedExternalMapBsp) { 'true' } else { 'false' }
    $env:HLCLIENT_MANUAL_TEST_RESEARCH_ROOT = $fixtureResearch
    $env:HLCLIENT_MANUAL_TEST_UNLIMITED = if ($NoTimeLimit) { 'true' } else { 'false' }
    $env:HLCLIENT_MANUAL_TEST_DURATION = if ($configuration.Contains('ProjectClientDurationSeconds')) { [string]$configuration.ProjectClientDurationSeconds } else { '0' }
    $env:HLCLIENT_MANUAL_TEST_MAP = $configuration.Map
    $env:HLCLIENT_MANUAL_TEST_INPUT = if ($SelectedScenario -eq 'damage-respawn-check') {
        'scripted-damage-respawn-check' } else { 'keyboard-mouse' }
    $LASTEXITCODE = 117 # Deliberately stale; child status must replace it.
    $code = Invoke-H2ManualManagedRunner -PowerShell $pwsh -Runner $fakeRunner -Configuration $configuration
    exit $code
}

function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL: $Message" }
}
$summaryAst = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors -join "`n") }
foreach ($name in @('Get-ManualSummaryField', 'Get-ManualFinalSessionSummary', 'Write-ManualFinalSessionSummary')) {
    $definition = $summaryAst.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    if ($null -eq $definition) { throw "Missing reporting function $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}
$summaryNative = [pscustomobject]@{
    client_sha256_at_launch = ('A' * 64)
    client_sha256_after_run = ('B' * 64)
    client_binary_change_status = 'changed'
    client_exit_code = 0
    remote_audio_peer_exit_code = 0
    primary_failure = 'none'
    server_exit_code = $null
    server_exit_status = 'owned_cleanup_without_individual_exit_code'
    guard_exit_code = $null
    guard_exit_status = 'owned_job_cleanup_without_individual_exit_code'
    peer_disconnect_timing = [pscustomobject]@{ status='observed'; close_to_postcommit_removal_ms=12.5 }
    session_receipts = [pscustomobject]@{
        A = [pscustomobject]@{ status='ready'; map_entry_observed=$true; runner_terminated=$false
            close=[pscustomobject]@{ reason='window_close'; eligible=$true; successful_send_count=1
                server_disconnect_observed=$false; ack_observed=$true; outcome='transport_acknowledged' } }
        B = [pscustomobject]@{ status='ready'; map_entry_observed=$true; runner_terminated=$false
            close=[pscustomobject]@{ reason='window_close'; eligible=$true; successful_send_count=2
                server_disconnect_observed=$false; ack_observed=$false; outcome='sent_but_unconfirmed' } }
    }
}
$summary = Get-ManualFinalSessionSummary -RunId ('0' * 32) -NativeSummary $summaryNative `
    -Verdict 'client-executable-changed-during-run' -Cleanup 'exact' -Restoration 'scoped_exact' `
    -Fast $true -ManagedScopeEntries 0 -Peer $true
Assert-Test ($summary.client_sha256_at_launch -ceq ('A' * 64) -and
    $summary.client_sha256_after_run -ceq ('B' * 64)) 'launch hash overwritten with end hash'
Assert-Test ($summary.clients.A.map_entry -ceq 'observed' -and
    $summary.clients.B.map_entry -ceq 'observed') 'bound map milestones not retained'
Assert-Test ($summary.clients.A.server_confirmation -ceq 'not_observed' -and
    $summary.clients.A.transport_ack_observed) 'transport ACK promoted to server confirmation'
Assert-Test ($summary.managed_scope_entries -eq 0 -and
    $summary.full_tree_verification -ceq 'not_performed') 'empty scoped restoration promoted to full tree'
Assert-Test ($null -eq $summary.server_exit_code -and $null -eq $summary.guard_exit_code) 'Job cleanup forged auxiliary exit codes'
Assert-Test ($summary.B_close_to_A_postcommit_removal_ms -eq 12.5 -and
    $summary.peer_removal_timing_status -ceq 'observed' -and
    $summary.clients.B.server_confirmation -ceq 'not_observed') 'measured removal delay lost or promoted to delivery confirmation'
$summaryLines = @(Write-ManualFinalSessionSummary $summary 'D:\fixture\functional-smoke-wrapper.json')
Assert-Test (($summaryLines -join "`n").Contains('Managed scope size: 0; full-tree verification: not_performed')) 'concise scope summary missing'
Assert-Test (($summaryLines -join "`n").Contains('Client build SHA:')) 'binary identity summary missing'
Assert-Test (($summaryLines -join "`n").Contains('B close intent -> A committed absence observation: 12.5 ms')) 'measured removal interval not visible'
$summaryNative.peer_disconnect_timing.status = 'shared_monotonic_clock_unavailable'
$missingTiming = Get-ManualFinalSessionSummary ('0' * 32) $summaryNative 'failed' 'exact' 'scoped_exact' $true 0 $true
Assert-Test ($null -eq $missingTiming.B_close_to_A_postcommit_removal_ms) 'mismatched clock produced guessed delay'
$summaryNative.client_exit_code = 2
$summaryNative.primary_failure = 'runtime_record_failed'
$crashSummary = Get-ManualFinalSessionSummary ('0' * 32) $summaryNative 'failed' 'exact' 'scoped_exact' $true 0 $true
Assert-Test ($crashSummary.clients.A.map_entry -ceq 'observed' -and
    $crashSummary.clients.A.exit_code -eq 2 -and $crashSummary.runner_verdict -ceq 'failed' -and
    $crashSummary.clients.A.exit_reason -ceq 'runtime_record_failed') 'later crash lost or map evidence erased'
$summaryNative.session_receipts.A.map_entry_observed = $false
$summaryNative.client_exit_code = 0
$missingMapSummary = Get-ManualFinalSessionSummary ('0' * 32) $summaryNative 'client-map-entry-not-observed' 'incomplete' 'scoped_not_verified' $true 0 $true
Assert-Test ($missingMapSummary.clients.A.map_entry -ceq 'not_observed' -and
    $missingMapSummary.runner_verdict -ceq 'client-map-entry-not-observed' -and
    $missingMapSummary.cleanup_result -ceq 'incomplete' -and
    $missingMapSummary.restoration_result -ceq 'scoped_not_verified') 'exit0 or cleanup promoted missing map evidence'
$legacy = Get-ManualFinalSessionSummary ('0' * 32) ([pscustomobject]@{ client_exit_code=0 }) 'not_run' 'exact' 'scoped_exact' $true 0 $false
Assert-Test ($legacy.client_sha256_at_launch -ceq 'not_recorded' -and
    $legacy.clients.A.receipt_status -ceq 'legacy_evidence_unavailable' -and
    $legacy.clients.A.map_entry -ceq 'not_observed') 'legacy report was retroactively supplied evidence'
Write-Host 'PASS E28.1 final summary: launch/end identity, durable map, crash, missing map, ACK distinction, legacy and empty scope controls'
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('H2 manual path with spaces ' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path (Join-Path $scratch 'scripts'))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests\h2_manual_runner_fixture.ps1') `
    -Destination (Join-Path $scratch 'scripts\managed runner fixture.ps1')
Write-Host "No-stock fixture root (retained): $scratch"
$PSNativeCommandUseErrorActionPreference = $false
foreach ($scenario in @(
    @{Mode='reference'; Case='normal'; Exit=0; Map=''; Scenario='manual'},
    @{Mode='reference'; Case='normal'; Exit=0; Map=''; Scenario='manual'; MuteFire=$true},
    @{Mode='reference'; Case='normal'; Exit=0; Map=''; Scenario='manual'; FreezeTracer=$true; Unlimited=$true},
    @{Mode='off'; Case='normal'; Exit=23; Map=''; Scenario='manual'; FreezeTracer=$true; Peer=$true},
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
    $freezeTracer = $scenario.ContainsKey('FreezeTracer') -and $scenario.FreezeTracer
    $peer = $scenario.ContainsKey('Peer') -and $scenario.Peer
    $timing = @{}
    if ($scenario.ContainsKey('Duration')) { $timing.DurationSeconds = $scenario.Duration }
    $unlimited = $scenario.ContainsKey('Unlimited') -and $scenario.Unlimited
    $output = @(& $pwsh -NoProfile -File $PSCommandPath -FixtureChild -FixtureRoot $scratch `
        -Mode $scenario.Mode -Case $scenario.Case -SelectedMap $scenario.Map -SelectedScenario $scenario.Scenario -TestStartHealth $health -MuteGlockFireSound:$muteFire -DebugFreezeTracer:$freezeTracer -RemoteAudioPeer:$peer -NoTimeLimit:$unlimited -ValidationMode $validation @timing 2>&1)
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
        Assert-Test ($text.Contains("freeze-tracer=$freezeTracer")) 'freeze-tracer diagnostic switch lost'
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
        @('-DebugFreezeTracer','-NoTimeLimit'), @('-DebugFreezeTracer','-Prediction','off'),
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
Assert-Test (-not $defaultConfiguration.Contains('ProjectClientDebugFreezeTracer')) 'ordinary launcher freezes tracer'
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
foreach($prediction in @('reference','off')) {
    $frozenConfig=Get-H2ManualConfiguration -Root $root -Mode $prediction -SelectedDebugFreezeTracer -SelectedNoTimeLimit
    Assert-Test $frozenConfig.ProjectClientDebugFreezeTracer 'frozen tracer configuration lost'
    Assert-Test $frozenConfig.ProjectClientNoTimeLimit 'frozen tracer unlimited configuration lost'
}
$freezeRejected=$false
try { [void](Get-H2ManualConfiguration -Root $root -Mode reference -SelectedScenario damage-respawn-check -SelectedDebugFreezeTracer) }
catch { $freezeRejected=$_.Exception.Message.Contains('DebugFreezeTracer requires') }
Assert-Test $freezeRejected 'frozen tracer diagnostic leaked into scripted scenario'
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
# Keep the owned research fixture outside the fake project and Steam roots,
# exactly as the real external-map safety contract requires.
$externalChildRoot = Join-Path $scratch 'external peer project with spaces'
[void](New-Item -ItemType Directory -Path (Join-Path $externalChildRoot 'scripts'))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests\h2_manual_runner_fixture.ps1') `
    -Destination (Join-Path $externalChildRoot 'scripts\managed runner fixture.ps1')
foreach ($mode in @('reference', 'off')) {
    foreach ($unlimited in @($false, $true)) {
        $peerExternal = Get-H2ManualConfiguration -Root $root -Mode $mode `
            -ResearchRoot $research -SteamRoot $steam -SelectedExternalMapBsp $externalBsp `
            -SelectedRemoteAudioPeer -SelectedNoTimeLimit:$unlimited
        Assert-Test ($peerExternal.RemoteAudioPeer -and $peerExternal.ExternalManualMap -and
            $peerExternal.Map -ceq $externalName) 'external peer did not retain verified map'
        Assert-Test ($peerExternal.ServerPort -eq $externalConfiguration.ServerPort -and
            $peerExternal.ResearchHalfLifeRoot -ceq $externalConfiguration.ResearchHalfLifeRoot -and
            $peerExternal.ClientPath -ceq $externalConfiguration.ClientPath) 'external peer changed server/root/normal client'
        Assert-H2ManualFilesAndContract $runner $peerExternal
        $output = @(& $pwsh -NoProfile -File $PSCommandPath -FixtureChild -FixtureRoot $externalChildRoot `
            -FixtureResearchRoot $research `
            -Mode $mode -SelectedExternalMapBsp $externalBsp -RemoteAudioPeer -NoTimeLimit:$unlimited 2>&1)
        $actualExit = $LASTEXITCODE
        $text = $output -join "`n"
        $expectedExit = if ($mode -ceq 'off') { 23 } else { 0 }
        Assert-Test ($actualExit -eq $expectedExit) "external peer child exit $actualExit"
        Assert-Test ($text.Contains("map=$externalName") -and
            $text.Contains('peer=True') -and $text.Contains('external=True')) 'external map/peer runner arguments lost'
        $seconds = if ($unlimited) { 0 } else { 45 }
        Assert-Test ($text.Contains("fixture timing verified: seconds=$seconds unlimited=$unlimited")) 'external peer timing changed'
    }
}
foreach ($bad in @(@{SelectedValidationMode='Strict'},
    @{SelectedScenario='damage-respawn-check'}, @{SelectedTestStartHealth=50},
    @{SelectedMap='boot_camp'})) {
    $rejected = $false
    try { [void](Get-H2ManualConfiguration -Root $root -Mode reference -ResearchRoot $research `
        -SelectedExternalMapBsp $externalBsp -SelectedRemoteAudioPeer @bad) } catch { $rejected = $true }
    Assert-Test $rejected 'external peer accepted incompatible profile'
}
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
# Execute only the real expected-stage expression and its assertion, never the
# runner's top-level game/process path. Run 36147b9b completed with limited
# prediction coverage but was falsely failed by this independent wrapper gate.
$runnerAst = [Management.Automation.Language.Parser]::ParseFile($runner, [ref]$tokens, [ref]$parseErrors)
Assert-Test ($parseErrors.Count -eq 0) 'runner parse failure'
$stageTable = $runnerAst.Find({ param($node)
    $node -is [Management.Automation.Language.HashtableAst] -and
    $node.Extent.Text.Contains("'steam-api-initialized' = 'true'") -and
    $node.Extent.Text.Contains("'last-confirmed-stage' =")
}, $true)
Assert-Test ($null -ne $stageTable) 'functional stage assertion table missing'
$stageValue = @($stageTable.KeyValuePairs | Where-Object { $_.Item1.Value -ceq 'last-confirmed-stage' })
Assert-Test ($stageValue.Count -eq 1) 'ambiguous functional stage assertion'
$expectedStage = [scriptblock]::Create($stageValue[0].Item2.Extent.Text)
$assertValue = $runnerAst.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -ceq 'Assert-OrchestratorValue'
}, $true)
. ([scriptblock]::Create($assertValue.Extent.Text))
$projectClientVisualMode=$true; $projectClientUserCmdMode=$false; $projectClientLiveRuntimeMode=$false
foreach ($inputMode in @('keyboard-mouse','scripted-check','scripted-jump-duck-check','scripted-damage-respawn-check')) {
    foreach ($predictionMode in @('reference','off')) {
        foreach ($coverage in @('live_prediction_active_accuracy_or_coverage_limited',
            'live_local_prediction_and_reconciliation_verified','live_prediction_integrated_live_pending','unavailable')) {
            $ProjectClientLiveInput=$inputMode; $ProjectClientPrediction=$predictionMode
            $orchestratorResult=[pscustomobject]@{Values=@{
                'prediction-result'=$coverage
                'damage-respawn-result'='live_death_respawn_verified_damage_pending'
            }}
            $expected=if ($inputMode -ceq 'scripted-damage-respawn-check') {
                'live_death_respawn_verified_damage_pending'
            } elseif ($inputMode -ceq 'scripted-jump-duck-check') {
                if ($predictionMode -ceq 'reference') { 'live_jump_duck_crouchwalk_prediction_verified' }
                else { 'live_jump_duck_server_verified' }
            } elseif ($predictionMode -ceq 'reference' -and
                ($inputMode -cne 'keyboard-mouse' -or $coverage -ceq 'live_local_prediction_and_reconciliation_verified')) {
                'live_local_prediction_and_reconciliation_verified'
            } else { 'live_visual_control_verified' }
            $actual=& $expectedStage
            Assert-Test ($actual -ceq $expected) "wrong expected stage: $inputMode/$predictionMode/$coverage"
            $orchestratorResult.Values['last-confirmed-stage']=$expected
            Assert-OrchestratorValue $orchestratorResult 'last-confirmed-stage' $actual
            $orchestratorResult.Values['last-confirmed-stage']='live_service_payloads_received'
            $rejected=$false
            try { Assert-OrchestratorValue $orchestratorResult 'last-confirmed-stage' $actual } catch { $rejected=$true }
            Assert-Test $rejected 'manual stage accepted incomplete visual startup'
            if ($inputMode -ceq 'scripted-check' -and $predictionMode -ceq 'reference') {
                $orchestratorResult.Values['last-confirmed-stage']='live_visual_control_verified'
                $rejected=$false
                try { Assert-OrchestratorValue $orchestratorResult 'last-confirmed-stage' $actual } catch { $rejected=$true }
                Assert-Test $rejected 'scripted prediction coverage was weakened'
            }
            Assert-Test ($orchestratorResult.Values['prediction-result'] -ceq $coverage) 'coverage verdict rewritten'
        }
    }
}
Write-Host 'PASS actual wrapper stage gate: manual limited/pending/verified coverage, reference/off, scripted and damage-respawn strictness; no coverage promotion or live processes.'
Assert-Test ($runnerText.Contains("'--game', `$Game, '--map', `$Map")) 'runner/native map forwarding contract changed'
$nativeText = [IO.File]::ReadAllText((Join-Path $root 'apps/hlclient_stock_runtime_orchestrator/main.cpp'))
Assert-Test ($nativeText.Contains('client_spec.arguments.push_back(L"--mute-glock-fire-sound")')) 'native client forwarding missing'
Assert-Test ($nativeText.Contains('client_spec.arguments.push_back(L"--debug-freeze-tracer")')) 'native frozen tracer forwarding missing'
Assert-Test ($runnerText.Contains("'--project-client-debug-freeze-tracer'")) 'runner frozen tracer forwarding missing'
Assert-Test ($runnerText.Contains("if (`$ExternalManualMap) { `$arguments += '--external-manual-map' }")) 'external opt-in not forwarded to native parser'
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
