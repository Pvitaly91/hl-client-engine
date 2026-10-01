#requires -Version 7.0
# Project-owned, inert files only. Does not enter the runner's top-level path.
[CmdletBinding()]
param([string]$CrashFixtureRoot, [switch]$LegacyFourFileTransaction)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repositoryRoot=Split-Path -Parent $PSScriptRoot
$runner=Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($runner,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors -join "`n") }
foreach ($node in $ast.EndBlock.Statements) {
    if ($node -is [Management.Automation.Language.FunctionDefinitionAst]) {
        . ([scriptblock]::Create($node.Extent.Text))
    }
}
. (Join-Path $PSScriptRoot 'stock_manual_fast.ps1')
$markerName='.hlclient-research-isolated'
$markerText='HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
$pendingMarkerName='.hlclient-research-pending'
$preparationManifestName='.hlclient-research-preparation.json'
$maximumEntries=199999; $maximumResearchBytes=[long]17179869184
function Assert-Test([bool]$Good,[string]$Label) { if (-not $Good) { throw "FAIL: $Label" } }
function Test-StockGameDllPath([string]$Path) {
    # Independent model of the installed swds.dll loader's path gate, not
    # ReHLDS or the Windows LoadLibrary path grammar. Never execute a DLL.
    return $Path -cnotmatch '^[/\\]|:' -and
        ([regex]::Matches($Path,'\.\.').Count -lt 2) -and
        [IO.Path]::GetExtension($Path) -ieq '.dll'
}
function Write-Fixture([string]$Path,[byte[]]$Bytes) {
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllBytes($Path,$Bytes)
}
function Fixture-Plan([string]$Root) {
    $components=Join-Path (Split-Path -Parent $Root) 'HLC-steamcfg-5e48b7c1/components'
    $meta=Join-Path $components 'metamod.dll'
    Get-FastTestHealthPlan $Root @{Meta=$meta;MetaSha=(Get-FileHash $meta).Hash;Helper=(Join-Path $components 'helper.dll')}
}
if ($CrashFixtureRoot) {
    # A real wrapper-process exit, not a hand-edited owner PID/record.
    $lease=Open-FastManualLease $CrashFixtureRoot
    $plan=Fixture-Plan $CrashFixtureRoot
    if ($LegacyFourFileTransaction) {
        $plan.Remove('valve/addons/metamod/hlclient_test50_metamod.dll')
        $plan['valve/liblist.gam']=[Text.Encoding]::ASCII.GetBytes('gamedll "D:/old/HLC-steamcfg/metamod.dll"')
    }
    $guard=New-FastManualGuard $CrashFixtureRoot ([guid]::NewGuid().ToString('N')) $plan $lease
    Set-FastManagedFiles $guard $plan
    exit 23
}
$scratch=Join-Path ([IO.Path]::GetTempPath()) ('hlclient-fast-no-stock-'+[guid]::NewGuid().ToString('N'))
$root=Join-Path $scratch 'research'
[void][IO.Directory]::CreateDirectory($root)
$ascii=[Text.Encoding]::ASCII
Write-Fixture (Join-Path $root $markerName) ($ascii.GetBytes($markerText))
Write-Fixture (Join-Path $root $preparationManifestName) ($ascii.GetBytes('{"schema":"hlclient.stock-runtime-research-preparation.v3","preparation_status":"exact-materialized-copy-verified"}'))
Write-Fixture (Join-Path $root $pendingMarkerName) ($ascii.GetBytes('{"schema":"hlclient.stock-research-copy-pending.v1","category":"awaiting_commit_marker","paths_recorded":false}'))
Write-Fixture (Join-Path $root 'steam_appid.txt') ($ascii.GetBytes('70'))
Write-Fixture (Join-Path $root 'valve/dlls/hl.dll') ($ascii.GetBytes('inert, never executed'))
Write-Fixture (Join-Path $root 'valve/liblist.gam') ($ascii.GetBytes("game `"fixture`"`r`ngamedll `"dlls/hl.dll`"`r`n"))
Write-Fixture (Join-Path $root 'valve/addons/metamod/config.ini') ($ascii.GetBytes('// original config'))
$metaFixture=[byte[]]::new(226304); $metaFixture[0]=37; $metaFixture[-1]=71 # deliberately not a PE image
Write-Fixture (Join-Path $scratch 'HLC-steamcfg-5e48b7c1/components/metamod.dll') $metaFixture
Write-Fixture (Join-Path $scratch 'HLC-steamcfg-5e48b7c1/components/helper.dll') ($ascii.GetBytes('inert'))
Write-Fixture (Join-Path $root 'valve/logs/historical.log') ($ascii.GetBytes('historical untouched'))
foreach ($name in @('maps/a.bsp','maps/b.bsp','models/a.mdl','sprites/a.spr','sound/a.wav','a.wad','b.wad','c.wad')) {
    $bytes=[byte[]]::new(1048576); $bytes[0]=37; $bytes[-1]=71
    Write-Fixture (Join-Path $root "valve/$name") $bytes
}
$asset=Join-Path $root 'valve/maps/a.bsp'
$assetHash=(Get-FileHash -LiteralPath $asset).Hash
$assetTime=(Get-Item -LiteralPath $asset).LastWriteTimeUtc.Ticks
$original=[IO.File]::ReadAllBytes((Join-Path $root 'valve/liblist.gam'))
$metadata=Get-Item -LiteralPath (Join-Path $root 'valve/liblist.gam')
$ticks=$metadata.LastWriteTimeUtc.Ticks
Initialize-RestorationDirectoryCapabilityNative # warm the common one-time compiler
[void](Assert-FastResearchPreparation $root)
$plan=Fixture-Plan $root
Assert-Test (-not (Test-StockGameDllPath 'D:/DEV/CPP/HLC-steamcfg-5e48b7c1/metamod.dll') -and
    -not (Test-StockGameDllPath '//server/share/metamod.dll') -and
    -not (Test-StockGameDllPath '../../outside.dll') -and
    (Test-StockGameDllPath 'addons/metamod/hlclient_test50_metamod.dll')) 'stock loader rejection model'
$plannedGameDll=[regex]::Match($ascii.GetString($plan['valve/liblist.gam']),'(?m)^gamedll "([^"]+)"').Groups[1].Value
Assert-Test (Test-StockGameDllPath $plannedGameDll) 'stock rejects planned gamedll path'
Assert-Test ($plan.Count -eq 5) 'test50 scope'
Assert-Test ((Get-FastTestHealthPlan $root $null).Count -eq 0) 'ordinary launch activated helper'
# Complement the native console regression: the path is absent from HLDS
# argv, but must still select the verified staged DLL through the scoped files.
$expectedComponents=([IO.Path]::GetFullPath((Join-Path $scratch 'HLC-steamcfg-5e48b7c1/components'))).Replace('\','/')
Assert-Test ($ascii.GetString($plan['valve/liblist.gam']) -ceq
    "game `"fixture`"`r`ngamedll `"addons/metamod/hlclient_test50_metamod.dll`"") 'relative gamedll selection lost'
Assert-Test ($ascii.GetString($plan['valve/addons/metamod/hlclient_test50_plugins.ini']) -ceq
    "win32 $expectedComponents/helper.dll`n") 'prepared helper not reused'
Assert-Test ($ascii.GetString($plan['valve/addons/metamod/config.ini']) -ceq
    "gamedll dlls/hl.dll`nplugins_file addons/metamod/hlclient_test50_plugins.ini`nexec_cfg addons/metamod/hlclient_test50_empty.cfg`n") 'original game/default config selection lost'
$stagedName='valve/addons/metamod/hlclient_test50_metamod.dll'
$stagedPath=Join-Path $root $stagedName
Assert-Test ([Convert]::ToHexString($plan[$stagedName]) -ceq [Convert]::ToHexString($metaFixture)) 'staged bytes not from prepared component'

# A receipt checked earlier is insufficient if the source subsequently changes.
$rejected=$false
try { [void](Get-FastTestHealthPlan $root @{Meta="$expectedComponents/metamod.dll";MetaSha=('0'*64);Helper="$expectedComponents/helper.dll"}) }
catch { $rejected=$_.Exception.Message.Contains('fast_test50_metamod_source_changed') }
Assert-Test $rejected 'changed prepared source accepted'
Write-Fixture (Join-Path $scratch 'oversized.dll') ([byte[]]::new(262145))
$rejected=$false
try { [void](Get-FastTestHealthPlan $root @{Meta=(Join-Path $scratch 'oversized.dll');MetaSha=('0'*64);Helper="$expectedComponents/helper.dll"}) }
catch { $rejected=$_.Exception.Message.Contains('fast_test50_metamod_size_bound') }
Assert-Test $rejected 'unbounded binary source read'
Assert-Test (-not (Test-Path $stagedPath) -and -not (Test-Path (Join-Path $root '.hlclient-manual-fast.pending.json'))) 'planning failure mutated research'
Assert-Test ((Get-FastManagedByteLimit $stagedName) -eq 262144 -and
    (Get-FastManagedByteLimit 'valve/liblist.gam') -eq 65536) 'per-path bounds widened'
$rejected=$false
try { [void](Get-FastManagedByteLimit 'valve/addons/metamod/other.dll') } catch { $rejected=$true }
Assert-Test $rejected 'arbitrary DLL added to scope'
$limitRoot=Join-Path $scratch 'bounds'
foreach ($case in @(@{Name='valve/liblist.gam';Length=65537},@{Name=$stagedName;Length=262145})) {
    Write-Fixture (Join-Path $limitRoot $case.Name) ([byte[]]::new($case.Length))
    $rejected=$false
    try { [void](Assert-FastManagedFile $limitRoot $case.Name) } catch { $rejected=$_.Exception.Message.Contains('fast_managed_file_shape') }
    Assert-Test $rejected 'oversized managed file accepted'
}

$offLease=Open-FastManualLease $root
$offGuard=New-FastManualGuard $root ([guid]::NewGuid().ToString('N')) ([ordered]@{}) $offLease
try {
    Assert-Test ($offGuard.Before.Entries.Count -eq 0 -and $offGuard.BackedEntries.Count -eq 0) 'ordinary launch backed game files'
    Set-FastManagedFiles $offGuard ([ordered]@{})
    [void](Restore-ScopedResearchState $offGuard)
} finally { Close-RestorationGuardCapabilities $offGuard; Close-FastManualLease $offLease $true }

# Same fixture and same common guard. Before: full snapshot/backup (old path).
$script:ManualFileWork=[ordered]@{copied_bytes=[long]0;hashed_bytes=[long]0;tree_scans=0}
$clock=[Diagnostics.Stopwatch]::StartNew()
$full=Get-ResearchSnapshot $root
$fullGuard=New-RestorationGuard $root $full
$baseline=[ordered]@{elapsed_ms=$clock.ElapsedMilliseconds;copied_bytes=$script:ManualFileWork.copied_bytes;hashed_bytes=$script:ManualFileWork.hashed_bytes;tree_scans=$script:ManualFileWork.tree_scans}
Close-RestorationGuardCapabilities $fullGuard

$script:ManualFileWork=[ordered]@{copied_bytes=[long]0;hashed_bytes=[long]0;tree_scans=0}
$clock.Restart()
$lease=Open-FastManualLease $root
$guard=New-FastManualGuard $root ([guid]::NewGuid().ToString('N')) $plan $lease
$fast=[ordered]@{elapsed_ms=$clock.ElapsedMilliseconds;copied_bytes=$script:ManualFileWork.copied_bytes;hashed_bytes=$script:ManualFileWork.hashed_bytes;tree_scans=$script:ManualFileWork.tree_scans}
Assert-Test ($fast.tree_scans -eq 0 -and $fast.copied_bytes -eq $guard.Before.TotalBytes -and $fast.copied_bytes -lt 1024 -and $fast.hashed_bytes -lt 4096) 'Fast copied/hashed assets'
Assert-Test ($baseline.copied_bytes -gt 8388608 -and $baseline.hashed_bytes -gt 16777216 -and $baseline.tree_scans -eq 1) 'baseline instrumentation missing'
$busy=$false
try { $other=Open-FastManualLease $root; Close-FastManualLease $other $false } catch { $busy=$true }
Assert-Test $busy 'concurrent wrapper lease allowed'
$stageCopyBefore=$script:ManualFileWork.copied_bytes
$stageHashBefore=$script:ManualFileWork.hashed_bytes
Set-FastManagedFiles $guard $plan
$activation=[ordered]@{staged_binary_bytes=($script:ManualFileWork.copied_bytes-$stageCopyBefore);
    hashed_bytes=($script:ManualFileWork.hashed_bytes-$stageHashBefore)}
Assert-Test ($activation.staged_binary_bytes -eq 226304 -and $activation.hashed_bytes -ge 226304) 'DLL I/O omitted from accounting'
Assert-Test ((Get-FileHash $stagedPath).Hash -ceq (Get-FileHash "$expectedComponents/metamod.dll").Hash) 'staged DLL differs'
$offRejected=$false
try { [void](Get-FastTestHealthPlan $root $null) } catch { $offRejected=$_.Exception.Message.Contains('fast_original_gamedll_invalid') }
Assert-Test $offRejected 'off silently used stale helper'
Write-Fixture (Join-Path $root 'valve/addons/metamod/foreign.log') ($ascii.GetBytes('new foreign'))
[void](Restore-ScopedResearchState $guard)
Close-RestorationGuardCapabilities $guard
Close-FastManualLease $lease $true
Assert-Test ([Convert]::ToHexString([IO.File]::ReadAllBytes((Join-Path $root 'valve/liblist.gam'))) -ceq [Convert]::ToHexString($original)) 'original bytes'
Assert-Test ((Get-Item -LiteralPath (Join-Path $root 'valve/liblist.gam')).LastWriteTimeUtc.Ticks -eq $ticks) 'metadata restore'
Assert-Test (-not (Test-Path -LiteralPath (Join-Path $root 'valve/addons/metamod/hlclient_test50_plugins.ini'))) 'owned new file retained'
Assert-Test (-not (Test-Path -LiteralPath $stagedPath)) 'owned absent-before DLL retained'
Assert-Test ([IO.File]::ReadAllText((Join-Path $root 'valve/addons/metamod/foreign.log')) -ceq 'new foreign') 'foreign changed'
Assert-Test ([IO.File]::ReadAllText((Join-Path $root 'valve/logs/historical.log')) -ceq 'historical untouched') 'history changed'

$PSNativeCommandUseErrorActionPreference=$false
& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File $PSCommandPath -CrashFixtureRoot $root
Assert-Test ($LASTEXITCODE -eq 23) 'crash fixture status'
$lease=Open-FastManualLease $root # exact journal/backup, dead PID, no latest-folder guess
Close-FastManualLease $lease $true
Assert-Test ([IO.File]::ReadAllText((Join-Path $root 'valve/liblist.gam')).Contains('"dlls/hl.dll"')) 'pending recovery failed'
Assert-Test (-not (Test-Path $stagedPath)) 'interrupted absent-before DLL retained'
& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File $PSCommandPath -CrashFixtureRoot $root -LegacyFourFileTransaction
Assert-Test ($LASTEXITCODE -eq 23) 'legacy crash fixture status'
$pending=Get-Content (Join-Path $root '.hlclient-manual-fast.pending.json') -Raw | ConvertFrom-Json
Assert-Test ($pending.entries.Count -eq 4) 'not a legacy four-file transaction'
$lease=Open-FastManualLease $root
Close-FastManualLease $lease $true
Assert-Test ([IO.File]::ReadAllText((Join-Path $root 'valve/liblist.gam')).Contains('"dlls/hl.dll"')) 'legacy journal no longer recoverable'
& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File $PSCommandPath -CrashFixtureRoot $root
Write-Fixture (Join-Path $root 'valve/liblist.gam') ($ascii.GetBytes('foreign after interrupted wrapper'))
$refused=$false
try { $lease=Open-FastManualLease $root; Close-FastManualLease $lease $false } catch { $refused=$_.Exception.Message.Contains('scoped_foreign_change_preserved') }
Assert-Test $refused 'foreign overwrite on recovery'
Assert-Test ([IO.File]::ReadAllText((Join-Path $root 'valve/liblist.gam')) -ceq 'foreign after interrupted wrapper') 'foreign loss'
Write-Fixture (Join-Path $root 'valve/liblist.gam') $plan['valve/liblist.gam'] # fixture conflict resolved explicitly
$lease=Open-FastManualLease $root
Close-FastManualLease $lease $true
Assert-Test ((Get-FileHash -LiteralPath $asset).Hash -ceq $assetHash -and (Get-Item -LiteralPath $asset).LastWriteTimeUtc.Ticks -eq $assetTime) 'asset touched'

# Five pre-existing files, including a >64 KiB binary, must roundtrip bytes
# and metadata. Foreign post-crash DLL edits must be retained, never deleted.
$priorDll=[byte[]]::new(226304); $priorDll[0]=81; $priorDll[-1]=93
Write-Fixture $stagedPath $priorDll
Write-Fixture (Join-Path $root 'valve/addons/metamod/hlclient_test50_plugins.ini') ($ascii.GetBytes('original plugins'))
Write-Fixture (Join-Path $root 'valve/addons/metamod/hlclient_test50_empty.cfg') ($ascii.GetBytes('original cfg'))
$priorDllTime=(Get-Item $stagedPath).LastWriteTimeUtc.Ticks
$priorDllHash=(Get-FileHash $stagedPath).Hash
& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -File $PSCommandPath -CrashFixtureRoot $root
Assert-Test ($LASTEXITCODE -eq 23) 'five-existing crash fixture status'
$pending=Get-Content (Join-Path $root '.hlclient-manual-fast.pending.json') -Raw | ConvertFrom-Json
Assert-Test ($pending.before.Entries.Count -eq 5) 'binary was not backed up'
Write-Fixture $stagedPath ($ascii.GetBytes('foreign DLL bytes'))
$refused=$false
try { $lease=Open-FastManualLease $root; Close-FastManualLease $lease $false }
catch { $refused=$_.Exception.Message.Contains('scoped_foreign_change_preserved') }
Assert-Test ($refused -and [IO.File]::ReadAllText($stagedPath) -ceq 'foreign DLL bytes') 'foreign DLL overwritten/deleted'
Write-Fixture $stagedPath $plan[$stagedName] # resolve only this inert fixture's known conflict
$lease=Open-FastManualLease $root
Close-FastManualLease $lease $true
Assert-Test ((Get-FileHash $stagedPath).Hash -ceq $priorDllHash -and
    (Get-Item $stagedPath).LastWriteTimeUtc.Ticks -eq $priorDllTime) 'preexisting DLL bytes/metadata not restored'

# Exercise the existing bounded process runner and the same scoped guard on
# startup failure, handled nonzero exit, and a hung inert child. No stock paths.
$projectClientVisualMode=$true; $projectClientLiveRuntimeMode=$false
$projectClientUserCmdMode=$false; $ProjectClientLiveInput='keyboard-mouse'; $ProjectClientPrediction='reference'
$script:ManualProgressEnabled=$true
$pwsh=Join-Path $PSHOME 'pwsh.exe'
$foreignSpec=[Diagnostics.ProcessStartInfo]::new($pwsh,'-NoProfile -Command "Start-Sleep -Seconds 8"')
$foreignSpec.UseShellExecute=$false; $foreignSpec.CreateNoWindow=$true
$foreign=[Diagnostics.Process]::Start($foreignSpec)
try {
    foreach ($case in @('timeout','startup','handled')) {
        $lease=Open-FastManualLease $root
        $guard=New-FastManualGuard $root ([guid]::NewGuid().ToString('N')) $plan $lease
        $state=New-OrchestratorExitState
        $result=$null; $failure=$null
        $clock.Restart()
        try {
            Set-FastManagedFiles $guard $plan
            if ($case -eq 'startup') {
                $result=Invoke-BoundedOrchestrator (Join-Path $scratch 'missing.exe') @() 1 -ExactExitState $state
            } elseif ($case -eq 'timeout') {
                $result=Invoke-BoundedOrchestrator $pwsh @('-NoProfile','-Command','Start-Sleep -Seconds 30') 1 -ExactExitState $state
            } else {
                $command="Write-Output '[stock-runtime-orchestrator] phase-client-startup-ms=7'; Write-Output '[stock-runtime-orchestrator] application-primary-error=runtime_record_failed'; exit 17"
                $result=Invoke-BoundedOrchestrator $pwsh @('-NoProfile','-Command',$command) 5 -ExactExitState $state
            }
        } catch { $failure=$_.Exception.Message }
        finally {
            Assert-Test $state.ExitConfirmed 'owned fake process exit unconfirmed'
            [void](Restore-ScopedResearchState $guard)
            Close-RestorationGuardCapabilities $guard
            Close-FastManualLease $lease $true
        }
        if ($case -eq 'timeout') {
            Assert-Test ($failure.Contains('bounded deadline') -and $clock.Elapsed.TotalSeconds -lt 10) 'unbounded timeout'
            Assert-Test (-not $foreign.HasExited) 'unrelated process killed'
        } elseif ($case -eq 'startup') {
            Assert-Test ($failure.Contains('orchestrator_process_start_failed') -and $state.NoOrchestratorProcessCreated) 'startup cause lost'
        } else {
            Assert-Test ($null -eq $failure -and $result.ExitCode -eq 17 -and $result.Values['application-primary-error'] -ceq 'runtime_record_failed') 'child status/typed error changed'
            Assert-Test ($result.Values['phase-client-startup-ms'] -ceq '7') 'phase allowlist/roundtrip'
        }
        Assert-Test ([IO.File]::ReadAllText((Join-Path $root 'valve/liblist.gam')).Contains('"dlls/hl.dll"')) 'failure path restoration'
    }
    Assert-Test ($foreign.WaitForExit(10000)) 'inert unrelated fixture did not exit naturally'
} finally { $foreign.Dispose() }

# No game timer is not an unbounded startup or cleanup. Pure clock controls
# cover long durations without waiting, then an inert child proves the actual
# stream/wait path can outlive the old outer deadline and finish normally.
Assert-Test ((Get-ManualOrchestratorRemainingMilliseconds 180000 180 -NoTimeLimit) -eq 0) 'unlimited startup lost its bound'
Assert-Test ((Get-ManualOrchestratorRemainingMilliseconds 86400000 180 -NoTimeLimit -RuntimeObserved $true) -eq [long]::MaxValue) 'unlimited gameplay still times out'
Assert-Test ((Get-ManualOrchestratorRemainingMilliseconds 86400000 180) -lt 0) 'ordinary outer bound bypassed'
Assert-Test ((Get-ManualOrchestratorRemainingMilliseconds 91000 180 -NoTimeLimit -RuntimeObserved $true -CleanupStartedMilliseconds 1000) -eq 0) 'unlimited cleanup lost its bound'
foreach ($case in @('user-ended','startup-timeout')) {
    $state=New-OrchestratorExitState
    $result=$null; $failure=$null
    $command=if ($case -eq 'user-ended') {
        "Write-Output '[stock-runtime-orchestrator] phase-runtime-observed-ms=7'; Start-Sleep -Seconds 2; Write-Output '[stock-runtime-orchestrator] phase-owned-cleanup-started-ms=1000000000'; exit 0"
    } else { 'Start-Sleep -Seconds 30' }
    try {
        $result=Invoke-BoundedOrchestrator $pwsh @('-NoProfile','-Command',$command) 1 -ExactExitState $state -ManualNoTimeLimit
    } catch { $failure=$_.Exception.Message }
    Assert-Test $state.ExitConfirmed 'no-timer inert process exit not confirmed'
    if ($case -eq 'user-ended') {
        Assert-Test ($null -eq $failure -and $result.ExitCode -eq 0) 'no-timer child was killed at old deadline'
        Assert-Test ($result.Values['phase-owned-cleanup-started-ms'] -ceq '1000000000') 'long-uptime cleanup transition not retained'
    } else {
        Assert-Test ($failure.Contains('bounded deadline')) 'no-timer bypassed startup failure'
    }
}

# Actual publisher/readback, with distinct scoped and full-tree verdicts.
$reportRoot=Join-Path $scratch ([guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory((Join-Path $reportRoot 'logs'))
$cap=New-RunDirectoryCapability $reportRoot
try {
    $value=[ordered]@{validation_mode='fast';backup_scope='managed_mutable_files';full_tree_backup=$false;
        full_asset_hash_scan=$false;restoration_status='scoped_exact';scoped_restoration_status='scoped_exact';full_tree_restoration='not_verified';
        publication_status='complete';cleanup_error_count=0;result='fixture_failed';primary_error='runtime_record_failed';client_exit_code=17}
    $published=Publish-FunctionalWrapperResult $reportRoot $value $cap
    $read=Read-BoundedJsonWithRetainedBytes $published.Path 65536 'fast report fixture' $cap
    Assert-Test ($published.Status -ceq 'complete' -and $read.Value.full_tree_backup -is [bool] -and
        -not $read.Value.full_tree_backup -and $read.Value.restoration_status -ceq 'scoped_exact' -and
        $read.Value.full_tree_restoration -ceq 'not_verified' -and $read.Value.client_exit_code -eq 17 -and
        $read.Value.primary_error -ceq 'runtime_record_failed') 'scope/primary-error publication roundtrip'
} finally { $cap.Dispose() }
Write-Host ('NO_STOCK_PREPARATION '+([ordered]@{before_full=$baseline;after_fast_backup=$fast;fast_activation=$activation;asset_bytes=8388608;live_startup_timing='not_measured'} | ConvertTo-Json -Compress))
Write-Host "PASS stock relative DLL path, staged hash/accounting/bounds, five-file and legacy recovery, existing/absent/foreign DLL, scoped bytes/metadata, foreign/history preservation, busy lease, profile on/off, startup/timeout/exit17, phase and summary roundtrip; retained=$scratch; live=not_run"
