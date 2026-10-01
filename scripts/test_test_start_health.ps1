#requires -Version 7.0
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $PSScriptRoot 'test_start_health_profile.psm1') -Force
[void](Get-TestStartHealthComponents $root)
$scratch=Join-Path ([IO.Path]::GetTempPath()) ('hlclient-test-health-evidence-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory((Join-Path $scratch 'logs'))
$run='0123456789abcdef0123456789abcdef'
$server="[hlclient-test-health] run=$run requested_start_health=50 server_setup_applied=true max_health=100 max_health_source=server_entvars`n"
$client="[test-start-health] requested_start_health=50 client_observed_health=50 source=fresh_clientdata result=ready`n"
function Assert-Health([bool]$Yes,[string]$Message) { if(-not $Yes) { throw $Message } }
foreach($case in 0..5) {
    $s=if($case -eq 0 -or $case -eq 2) { '' } elseif($case -eq 4) { $server+$server } else { $server }
    $c=if($case -eq 0 -or $case -eq 1) { '' } elseif($case -eq 5) { '[test-start-health] result=failed reason=fresh_server_50_not_observed client_observed_health=100' } else { $client }
    [IO.File]::WriteAllText((Join-Path $scratch 'logs/server-diagnostic-redacted.log'),$s)
    [IO.File]::WriteAllText((Join-Path $scratch 'logs/client-diagnostic-redacted.log'),$c)
    $e=Get-TestStartHealthEvidence $scratch $run
    Assert-Health (($e.result -eq 'confirmed') -eq ($case -eq 3)) 'parameter/helper/notification alone became success'
    if($case -eq 3) { Assert-Health ($e.max_health -eq 100 -and $e.max_health_source -eq 'server_entvars') 'maximum source fabricated/reduced' }
    if($case -eq 5) { Assert-Health ($e.failure_reason -eq 'fresh_server_50_not_observed') '100 failure lost' }
}
$foreign=Get-TestStartHealthEvidence $scratch ('b'*32)
Assert-Health (-not $foreign.server_setup_applied) 'another run accepted'
[void][IO.Directory]::CreateDirectory((Join-Path $scratch 'research/valve'))
[IO.File]::WriteAllText((Join-Path $scratch 'research/valve/liblist.gam'),'gamedll "unknown.dll"')
$rejected=$false
try { [void](New-TestStartHealthProfile $root (Join-Path $scratch 'research') $run) }
catch { $rejected=$_.Exception.Message.Contains('original_gamedll_contract_invalid') }
Assert-Health $rejected 'unsupported preparation became success'
Assert-Health (-not (Test-Path (Join-Path $scratch 'research/valve/addons'))) 'rejected preparation wrote addon'
[IO.File]::WriteAllText((Join-Path $scratch 'research/valve/liblist.gam'),'gamedll "dlls\hl.dll"')
$profile=New-TestStartHealthProfile $root (Join-Path $scratch 'research') $run
$startup=Join-Path $scratch 'research/valve/addons/metamod/config.ini'
$scoped=Join-Path $scratch "research/valve/addons/hlclient_test50/$run/config.ini"
Assert-Health ((Get-FileHash $startup).Hash -ceq (Get-FileHash $scoped).Hash) 'early loader configuration missing'
Assert-Health ([IO.File]::ReadAllText($startup).Contains("plugins_file addons/hlclient_test50/$run/plugins.ini")) 'startup chose another plugin set'
$startupHash=(Get-FileHash $startup).Hash
$rejected=$false
try { [void](New-TestStartHealthProfile $root (Join-Path $scratch 'research') ('c'*32)) }
catch { $rejected=$true }
Assert-Health $rejected 'preexisting configuration overwritten'
Assert-Health ((Get-FileHash $startup).Hash -ceq $startupHash) 'existing startup bytes changed'
$failure="[hlclient-test-health-stage] stage=attached`n[hlclient-test-health-stage] stage=server_activated configured=true`n[hlclient-test-health] run=$run requested_start_health=50 server_setup_applied=spawn_validation_failed max_health=unavailable max_health_source=unavailable`n"
[IO.File]::WriteAllText((Join-Path $scratch 'logs/server-diagnostic-redacted.log'),$failure)
[IO.File]::WriteAllText((Join-Path $scratch 'logs/client-diagnostic-redacted.log'),'')
$e=Get-TestStartHealthEvidence $scratch $run
Assert-Health ($e.helper_attached -and $e.helper_server_activated -and -not $e.server_setup_applied -and $e.failure_reason -eq 'spawn_validation_failed') 'loader stages or exact helper failure lost'
[IO.File]::WriteAllText((Join-Path $scratch 'logs/client-diagnostic-redacted.log'),'[test-start-health] requested_start_health=50 client_observed_health=100 source=fresh_clientdata result=pending')
$e=Get-TestStartHealthEvidence $scratch $run
Assert-Health ($e.client_observed_health -eq 100 -and $e.result -ne 'confirmed') 'fresh 100 hidden or became success'
foreach ($reason in @('run_missing','run_invalid','profile_missing','profile_mismatch','globals_unavailable',
        'deathmatch_unavailable','client_limit_invalid','map_mismatch','ready')) {
    [IO.File]::WriteAllText((Join-Path $scratch 'logs/server-diagnostic-redacted.log'),"[hlclient-test-health-config] reason=$reason`n")
    [IO.File]::WriteAllText((Join-Path $scratch 'logs/client-diagnostic-redacted.log'),'')
    $e=Get-TestStartHealthEvidence $scratch $run
    Assert-Health ($e.helper_configuration_reason -ceq $reason -and $e.result -ne 'confirmed') 'configuration diagnostic lost or became health proof'
    if ($reason -ne 'ready') {
        Assert-Health ($e.failure_reason -ceq "helper_configuration_$reason") 'configuration failure lost'
    }
}
[IO.File]::WriteAllText((Join-Path $scratch 'logs/server-diagnostic-redacted.log'),"[hlclient-test-health-config] reason=profile_mismatch_private_value`n")
$e=Get-TestStartHealthEvidence $scratch $run
Assert-Health ($e.helper_configuration_reason -ceq 'unavailable') 'unbounded reason accepted'
[IO.File]::WriteAllText((Join-Path $scratch 'logs/server-diagnostic-redacted.log'),"[hlclient-test-health-config] reason=profile_missing`n[hlclient-test-health-config] reason=ready`n$server")
[IO.File]::WriteAllText((Join-Path $scratch 'logs/client-diagnostic-redacted.log'),$client)
$e=Get-TestStartHealthEvidence $scratch $run
Assert-Health ($e.helper_configuration_reason -ceq 'ready' -and $e.result -ceq 'confirmed') 'initial incomplete configuration hid final confirmation'
Write-Host "PASS independent test-health evidence/failure fixtures; retained=$scratch; live=not_run"
