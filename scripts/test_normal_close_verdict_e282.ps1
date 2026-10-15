#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$OrchestratorPath,
    [string]$FixturePath,
    [switch]$PreFixProbe,
    [switch]$ReportingHelpersOnly,
    [switch]$FixtureChild,
    [ValidateSet('normal', 'bounded_log_evicted', 'server_not_ready', 'connection_rejected',
        'map_not_entered', 'normal_close_before_map', 'client_crash_after_signon',
        'runtime_failure_after_gameplay', 'parser_failure_after_gameplay', 'peer_crash',
        'cleanup_failed', 'restoration_failed', 'foreign_run', 'foreign_client',
        'foreign_generation', 'receipt_missing', 'receipt_corrupt', 'receipt_truncated',
        'legacy_without_evidence', 'receipt_without_gameplay', 'zero_world_publication',
        'owned_peer_stop', 'null_pid', 'null_creation', 'false_exit', 'bool_counter',
        'fraction_counter', 'string_counter', 'string_epoch', 'unknown_fact_bit', 'string_close_timestamp',
        'hash_publication_failed')]
    [string]$Case = 'normal'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
if (-not $FixturePath) {
    $FixturePath = Join-Path $PSScriptRoot 'tests\normal_close_verdict_e282_fixture.json'
}
if (-not $OrchestratorPath) {
    $OrchestratorPath = Join-Path $root 'build\bin\Release\hlclient_stock_runtime_orchestrator.exe'
}
$fixtureFile = Get-Item -LiteralPath $FixturePath
if ($fixtureFile.Length -gt 32768) { throw 'E28.2 fixture exceeds its bounded public evidence contract.' }
$fixture = Get-Content -LiteralPath $fixtureFile.FullName -Raw | ConvertFrom-Json
if ($fixture.schema -cne 'hlclient.normal-close-verdict-fixture.v1' -or
    $fixture.evidence_source -cne 'retrospective_offline_reclassification' -or
    $fixture.source_run_id -cnotmatch '^[0-9a-f]{32}$' -or
    $fixture.client_sha256_at_launch -cnotmatch '^[0-9A-F]{64}$') {
    throw 'E28.2 fixture provenance or schema is invalid.'
}
$tokens = $null; $parseErrors = $null
$captureAst = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors -join "`n") }
function Import-E282ProductionFunction([string]$Name) {
    $definition = $captureAst.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $Name
    }, $true)
    if ($null -eq $definition) { throw "Missing production reporting function $Name" }
    # Global function scope is deliberate: never execute the script's top-level
    # capture path, snapshots, process launcher, or game/session machinery.
    . ([scriptblock]::Create($definition.Extent.Text.Replace(
        ('function ' + $Name), ('function global:' + $Name))))
}
Import-E282ProductionFunction 'Assert-OrchestratorValue'
function Assert-E282([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "E28.2 regression: $Message" }
}
function Convert-E282Values([object]$Value) {
    $values = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    foreach ($property in $Value.PSObject.Properties) { $values.Add($property.Name, [string]$property.Value) }
    return ,$values
}
if ($PreFixProbe) {
    # Read the precise old production expressions from the AST. This probe is
    # intentionally not an invented classifier or a copied false expectation.
    $expectations = @($captureAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.HashtableAst]
    }, $true) | ForEach-Object {
        foreach ($pair in $_.KeyValuePairs) {
            if ($pair.Item1.Extent.Text -ceq "'client-world-state-published'") { $pair.Item2 }
        }
    })
    Assert-E282 ($expectations.Count -eq 1) 'pre-fix world attestation expression is ambiguous'
    $projectClientVisualMode = $true
    $ProjectClientLiveInput = 'keyboard-mouse'
    $ProjectClientPrediction = 'reference'
    $result = [pscustomobject]@{ Values = (Convert-E282Values $fixture.native_values) }
    $expectation = & ([scriptblock]::Create($expectations[0].Extent.Text))
    $attestationFailure = $null
    try { Assert-OrchestratorValue $result 'client-world-state-published' $expectation }
    catch { $attestationFailure = $_ }
    $classifiers = @($captureAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left.Extent.Text -ceq '$functionalResult' -and
        $node.Extent.Text.Contains('local_server_ready_client_blocked')
    }, $true))
    Assert-E282 ($classifiers.Count -eq 1) 'pre-fix final classifier expression is ambiguous'
    $functionalSuccess = $null -eq $attestationFailure
    $ownedStopped = $true; $restorationSucceeded = $true
    $cleanupErrors = [Collections.Generic.List[string]]::new()
    $orchestratorExitCode = 0; $testHealthEvidence = $null
    $functionalStaged = $null; $serverReady = $true
    $projectClientStockSignonMode = $true; $projectClientUserCmdMode = $false
    $projectClientLiveRuntimeMode = $false; $orchestratorResult = $result
    . ([scriptblock]::Create($classifiers[0].Extent.Text))
    Write-Output "[e282-pre-fix] source_run_id=$($fixture.source_run_id)"
    Write-Output "[e282-pre-fix] native_exit_code=$($fixture.native_exit_code)"
    Write-Output "[e282-pre-fix] map_A=$($fixture.native_summary.session_receipts.A.map_entry_observed) map_B=$($fixture.native_summary.session_receipts.B.map_entry_observed)"
    Write-Output "[e282-pre-fix] expected_world=$expectation actual_world=$($result.Values['client-world-state-published'])"
    Write-Output "[e282-pre-fix] attestation_failure=$(if ($null -ne $attestationFailure) { $attestationFailure.Exception.Message } else { 'none' })"
    Write-Output "[e282-pre-fix] production_wrapper_verdict=$functionalResult"
    Assert-E282 ($functionalResult -ceq 'fresh_project_client_live_visual_control_integrated' -and
        $null -eq $attestationFailure) 'successful accepted session was blocked after normal close'
    return
}
foreach ($name in @('Get-ManualSummaryField', 'Get-ManualFinalSessionSummary',
        'Write-ManualFinalSessionSummary', 'Assert-ManualNormalCompletionAttestation',
        'Get-ProjectWorldPublicationExpectation', 'Get-FunctionalSmokeFinalVerdict',
        'Get-FunctionalEvidenceEligibility', 'Test-ManualUnsignedIntegral')) {
    Import-E282ProductionFunction $name
}
function Copy-E282Object([object]$Value) {
    return ($Value | ConvertTo-Json -Depth 20 -Compress | ConvertFrom-Json)
}
function Test-E282TopLevelAst([Management.Automation.Language.Ast]$Node) {
    for ($parent = $Node.Parent; $null -ne $parent; $parent = $parent.Parent) {
        if ($parent -is [Management.Automation.Language.FunctionDefinitionAst]) { return $false }
    }
    return $true
}
function Get-E282HashPublicationSource {
    $blocks = @($captureAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.IfStatementAst] -and
        (Test-E282TopLevelAst $node) -and
        $node.Extent.Text.Contains('Get-FileSha256 $orchestratorPath') -and
        $node.Extent.Text.Contains("'orchestrator_hash_publication_failed'") -and
        -not $node.Extent.Text.Contains('$functionalWrapper') -and
        ($node.Extent.EndOffset - $node.Extent.StartOffset) -le 1024
    }, $true))
    Assert-E282 ($blocks.Count -eq 1) 'exact narrow production hash-publication block is missing or ambiguous'
    $block = $blocks[0]
    $initializers = @($captureAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        (Test-E282TopLevelAst $node) -and
        $node.Left.Extent.Text -ceq '$orchestratorSha256' -and
        $node.Right.Extent.Text -ceq '$null' -and
        $node.Extent.EndOffset -le $block.Extent.StartOffset
    }, $true))
    Assert-E282 ($initializers.Count -eq 1) 'production final hash initializer is missing or ambiguous'
    return [pscustomobject]@{
        Block = $block
        Text = $initializers[0].Extent.Text + "`n" + $block.Extent.Text
    }
}
function Assert-E282FinalPublicationOrdering {
    $hash = Get-E282HashPublicationSource
    $verdicts = @($captureAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        (Test-E282TopLevelAst $node) -and
        $node.Left.Extent.Text -ceq '$functionalVerdict' -and
        $node.Extent.Text.Contains('Get-FunctionalSmokeFinalVerdict')
    }, $true))
    $summaries = @($captureAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        (Test-E282TopLevelAst $node) -and
        $node.Left.Extent.Text -ceq '$manualFinalSummary' -and
        $node.Extent.Text.Contains('Get-ManualFinalSessionSummary')
    }, $true))
    $wrappers = @($captureAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        (Test-E282TopLevelAst $node) -and
        $node.Left.Extent.Text -ceq '$functionalWrapper'
    }, $true))
    $publications = @($captureAst.FindAll({ param($node)
        $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        (Test-E282TopLevelAst $node) -and
        $node.Left.Extent.Text -ceq '$wrapperPublication' -and
        $node.Extent.Text.Contains('Publish-FunctionalWrapperResult $runRoot $functionalWrapper')
    }, $true))
    Assert-E282 ($verdicts.Count -eq 1 -and $summaries.Count -eq 1 -and
        $wrappers.Count -eq 1 -and $publications.Count -eq 1) 'production final reporting source owners are ambiguous'
    Assert-E282 ($hash.Block.Extent.EndOffset -lt $verdicts[0].Extent.StartOffset -and
        $verdicts[0].Extent.EndOffset -lt $summaries[0].Extent.StartOffset -and
        $summaries[0].Extent.EndOffset -lt $wrappers[0].Extent.StartOffset -and
        $wrappers[0].Extent.EndOffset -lt $publications[0].Extent.StartOffset) 'late hash failure can occur after production verdict/summary selection'
}
Assert-E282FinalPublicationOrdering
function Get-E282NativeProjection {
    if (-not (Test-Path -LiteralPath $OrchestratorPath -PathType Leaf)) {
        throw 'E28.2 normal native orchestrator fixture executable is unavailable.'
    }
    $PSNativeCommandUseErrorActionPreference = $false
    $global:LASTEXITCODE = $null
    $output = @(& $OrchestratorPath '--validate-normal-close-verdict' '--projection' 2>&1)
    $nativeExit = $global:LASTEXITCODE
    Assert-E282 ($null -ne $nativeExit) 'native projection returned no actual process exit'
    Assert-E282 ($output.Count -le 128) 'native projection exceeds bounded status count'
    $values = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    $receipts = [ordered]@{}
    foreach ($item in $output) {
        $line = [string]$item
        Assert-E282 ($line.Length -le 4096 -and
            $line -cmatch '^\[stock-runtime-orchestrator\] (?<key>[a-z0-9A-B-]+)=(?<value>.+)$') 'native projection emitted non-contract output'
        $key = $Matches.key; $value = $Matches.value
        if ($key -cin @('receipt-json-A', 'receipt-json-B')) {
            $role = $key.Substring($key.Length - 1)
            Assert-E282 (-not $receipts.Contains($role)) 'duplicate receipt projection'
            $receipts[$role] = $value | ConvertFrom-Json
        } else {
            Assert-E282 ($value -cmatch '^[A-Za-z0-9_.:/-]{1,128}$' -and
                -not $values.ContainsKey($key)) 'duplicate or non-contract native status'
            $values.Add($key, $value)
        }
    }
    Assert-E282 ($values['stock-processes-started'] -ceq '0' -and
        $values['capture-files-written'] -ceq '0') 'offline mode launched stock or captured a session'
    Assert-E282 ($values['normal-close-verdict-contract'] -ceq 'passed' -and
        $values['negative-controls'] -ceq 'passed') 'native classifier/negative controls failed'
    Assert-E282 ($receipts.Contains('A') -and $receipts.Contains('B')) 'native projection omitted actual codec receipts'
    return [pscustomobject]@{ ExitCode = [int]$nativeExit; Values = $values; Receipts = $receipts }
}
function New-E282EvaluationInput {
    if ($ReportingHelpersOnly) {
        # A separate, explicit PS-helper unit mode is useful before the native
        # fixture target is built. It never masquerades as native execution.
        $summary = Copy-E282Object $fixture.native_summary
        $summary.connection_status = 'accepted'
        $summary | Add-Member -NotePropertyName 'connection_establishment' -NotePropertyValue 'accepted'
        $summary | Add-Member -NotePropertyName 'session_terminal_state' -NotePropertyValue 'normal_close_completed'
        $values = Convert-E282Values $fixture.native_values
        $values['connection-status'] = 'accepted'
        $values['connection-establishment'] = 'accepted'
        $values['session-terminal-state'] = 'normal_close_completed'
        $context = [ordered]@{
            PrimaryError = $null; CleanupErrorCount = 0; RunExists = $true
            OwnedStopped = $true; RestorationSucceeded = $true
            ServerReady = $true; ClientReady = $true
            NativeExit = 0; ManualKeyboard = $true; ManualReceiptAttested = $false
            SuccessResult = 'fresh_project_client_live_visual_control_integrated'
            PendingResult = $null; TestHealthFailed = $false
        }
        return [pscustomobject]@{
            Native = [pscustomobject]@{ ExitCode = 0; Values = $values }
            Summary = $summary; Context = $context; Launch = (Copy-E282Object $fixture.launch_identity)
            Bindings = [ordered]@{ A = (Copy-E282Object $fixture.process_bindings.A); B = (Copy-E282Object $fixture.process_bindings.B) }
        }
    }
    $native = Get-E282NativeProjection
    Assert-E282 ($native.ExitCode -eq 0 -and $native.Values['result'] -ceq 'success' -and
        $native.Values['connection-status'] -ceq 'accepted') 'normal native completion still projects rejected/failure'
    foreach ($role in @('A', 'B')) {
        $expected = $fixture.native_summary.session_receipts.$role
        $actual = $native.Receipts[$role]
        foreach ($field in @('run_id', 'role', 'status', 'terminal_valid', 'process_id', 'process_creation_filetime',
                'network_generation', 'map_generation', 'generation_ordinal', 'map_identifier',
                'stage', 'fact_mask', 'receiving_entity', 'map_entry_observed', 'keyboard_readiness',
                'host_qpc_frequency')) {
            Assert-E282 ($actual.$field -ceq $expected.$field) "production $role receipt differs from actual sanitized $field"
        }
        foreach ($field in @('reason', 'outcome', 'requested', 'eligible', 'queued',
                'send_attempt_count', 'successful_send_count', 'ack_observed',
                'server_disconnect_observed', 'requested_process_ms', 'deadline_process_ms',
                'first_send_process_ms', 'last_send_process_ms', 'transport_closed_process_ms',
                'requested_host_qpc_ticks', 'transport_closed_host_qpc_ticks')) {
            Assert-E282 ($actual.close.$field -ceq $expected.close.$field) "production $role close differs from actual sanitized $field"
        }
        foreach ($field in @('new_usercmd_count', 'transmitted_usercmd_packet_count',
                'server_sample_count', 'usercmd_zero_observed')) {
            Assert-E282 ($actual.counters.$field -ceq $expected.counters.$field) "production $role counters differ from actual raw receipt $field"
        }
        for ($ordinal = 0; $ordinal -lt 5; $ordinal++) {
            foreach ($field in @('process_ms', 'publication_ordinal', 'host_qpc_ticks')) {
                Assert-E282 ($actual.milestones[$ordinal].$field -ceq $expected.milestones[$ordinal].$field) "production $role milestone $ordinal differs in $field"
            }
        }
    }
    $summary = Copy-E282Object $fixture.native_summary
    $summary.session_receipts.A = $native.Receipts.A
    $summary.session_receipts.B = $native.Receipts.B
    # Native projection is the source of these fields. The fixture is only an
    # independent expected-value oracle, not a positive-readiness substitute.
    $nativeFieldMap = [ordered]@{
        'connection-status' = 'connection_status'
        'connection-establishment' = 'connection_establishment'
        'session-terminal-state' = 'session_terminal_state'
        'connection-accepted' = 'connection_accepted'
        'client-map-entry-status' = 'client_map_entry'
        'client-world-state-published' = 'client_world_state_published'
        'usercmd-transmitted' = 'usercmd_transmitted'
        'live-visual-verified' = 'live_visual_verified'
        'usercmd-generated' = 'usercmd_generated'
        'usercmd-new' = 'usercmd_new'
        'usercmd-packets' = 'usercmd_packets'
        'usercmd-server-samples' = 'usercmd_server_samples'
        'client-exit-code' = 'client_exit_code'
        'remote-audio-peer-exit-code' = 'remote_audio_peer_exit_code'
        'server-exit-code' = 'server_exit_code'
        'guard-exit-code' = 'guard_exit_code'
        'serverinfo-map' = 'serverinfo_map'
    }
    foreach ($entry in $nativeFieldMap.GetEnumerator()) {
        Assert-E282 ($native.Values.ContainsKey($entry.Key)) "native projection omitted $($entry.Key)"
        $value = $native.Values[$entry.Key]
        if ($entry.Key -cin @('connection-accepted', 'client-world-state-published',
                'usercmd-transmitted', 'live-visual-verified')) {
            Assert-E282 ($value -cin @('true', 'false')) 'native projected invalid boolean'
            $value = $value -ceq 'true'
        } elseif ($entry.Key -cmatch '(?:count|generated|new|packets|samples|exit-code)$') {
            $value = [long]$value
        }
        if ($null -ne $summary.PSObject.Properties[$entry.Value]) { $summary.($entry.Value) = $value }
        else { $summary | Add-Member -NotePropertyName $entry.Value -NotePropertyValue $value }
    }
    $summary.application_outcome.result = $native.Values['application-result']
    $summary.application_outcome.primary_error = $native.Values['application-primary-error']
    $summary.application_outcome.scripted_coverage = $native.Values['application-scripted-coverage']
    $summary.authentication_status = $native.Values['authentication-status'].Replace('-', '_')
    $summary.primary_failure = $native.Values['failure-category']
    $bindings = [ordered]@{ A = (Copy-E282Object $fixture.process_bindings.A); B = (Copy-E282Object $fixture.process_bindings.B) }
    $context = [ordered]@{
        PrimaryError = $null; CleanupErrorCount = 0; RunExists = $true
        OwnedStopped = $true; RestorationSucceeded = $true
        ServerReady = ($native.Values['server-ready'] -ceq 'true')
        ClientReady = ($native.Values['client-ready'] -ceq 'true')
        NativeExit = $native.ExitCode; ManualKeyboard = $true
        ManualReceiptAttested = $false
        SuccessResult = 'fresh_project_client_live_visual_control_integrated'
        PendingResult = $null; TestHealthFailed = $false
    }
    return [pscustomobject]@{ Native = $native; Summary = $summary; Bindings = $bindings
        Launch = (Copy-E282Object $fixture.launch_identity); Context = $context }
}
function Set-E282NegativeCase([object]$InputValue, [string]$Name) {
    $native = $InputValue.Native; $summary = $InputValue.Summary
    $context = $InputValue.Context
    switch ($Name) {
        'normal' { }
        'bounded_log_evicted' { $summary.bounded_log_capture = 'incomplete' }
        'server_not_ready' { $context.ServerReady = $false; $native.Values['server-ready'] = 'false' }
        'connection_rejected' {
            $native.Values['connection-status'] = 'rejected'
            $native.Values['connection-establishment'] = 'rejected'
            $native.Values['connection-accepted'] = 'false'
            $summary.connection_status = 'rejected'; $summary.connection_accepted = $false
        }
        { $_ -cin @('map_not_entered', 'normal_close_before_map') } {
            $context.ClientReady = $false; $native.Values['client-ready'] = 'false'
            $native.Values['client-map-entry-status'] = 'not-observed'
            $summary.session_receipts.A.map_entry_observed = $false
            $summary.session_receipts.A.milestones[2] = $null
            $summary.session_receipts.A.keyboard_readiness = $false
            $summary.client_map_entry = 'not_observed'
        }
        'client_crash_after_signon' {
            $context.NativeExit = 1; $native.ExitCode = 1; $native.Values['result'] = 'failed'
            $native.Values['client-exit-code'] = '3221225477'; $summary.client_exit_code = 3221225477
            $summary.primary_failure = 'project-client-nonzero-exit'
        }
        { $_ -cin @('runtime_failure_after_gameplay', 'parser_failure_after_gameplay') } {
            $context.NativeExit = 1; $native.ExitCode = 1; $native.Values['result'] = 'failed'
            $native.Values['application-result'] = 'failed'
            $native.Values['application-primary-error'] = 'runtime_record_failed'
            $summary.application_outcome.result = 'failed'
            $summary.application_outcome.primary_error = 'runtime_record_failed'
            $summary.primary_failure = 'project-client-nonzero-exit'
            $summary.application_outcome.runtime_error = 'runtime_record_failed'
            if ($Name -ceq 'parser_failure_after_gameplay') { $summary.application_outcome.parser_error = 'unsupported_opcode' }
        }
        'peer_crash' {
            $context.NativeExit = 1; $native.ExitCode = 1; $native.Values['result'] = 'failed'
            $native.Values['remote-audio-peer-exit-code'] = '3221225477'
            $summary.remote_audio_peer_exit_code = 3221225477
            $summary.primary_failure = 'remote-audio-peer-nonzero-exit'
        }
        'cleanup_failed' { $context.OwnedStopped = $false; $context.CleanupErrorCount = 1 }
        'restoration_failed' { $context.RestorationSucceeded = $false }
        'foreign_run' { $InputValue.Bindings.A.run_id = ('f' * 32) }
        'foreign_client' { $InputValue.Bindings.A.process_id = 5700 }
        'foreign_generation' { $summary.session_receipts.B.map_generation = 3 }
        'receipt_missing' { $summary.session_receipts.A = $null }
        'receipt_corrupt' { $summary.session_receipts.A.status = 'decode_failed' }
        'receipt_truncated' {
            $summary.session_receipts.A.terminal_valid = $false
            $summary.session_receipts.A.stage = 'world_ready'
        }
        'legacy_without_evidence' { $summary.session_receipts = $null; $InputValue.Launch = $null }
        'receipt_without_gameplay' {
            $summary.session_receipts.A.fact_mask = 15
            $summary.session_receipts.A.milestones[4] = $null
            $summary.session_receipts.A.keyboard_readiness = $false
        }
        'zero_world_publication' {
            $native.Values['client-world-state-published'] = 'false'
            $summary.client_world_state_published = $false
        }
        'owned_peer_stop' {
            # Existing E28.1 policy: closing A can explicitly stop a still-live
            # ready B. Do not demand a fabricated voluntary B close/send.
            $summary.session_receipts.B.runner_terminated = $true
            $summary.session_receipts.B.terminal_valid = $false
            $summary.session_receipts.B.status = 'progress'
            $summary.session_receipts.B.stage = 'first_gameplay_update'
            $summary.session_receipts.B.close = $null
            $summary.peer_disconnect_timing.status = 'not_observed'
        }
        'null_pid' { $InputValue.Bindings.A.process_id = $null }
        'null_creation' { $summary.session_receipts.A.process_creation_filetime = $null }
        'false_exit' { $summary.client_exit_code = $false }
        'bool_counter' { $summary.session_receipts.A.counters.new_usercmd_count = $true }
        'fraction_counter' { $summary.session_receipts.A.counters.server_sample_count = 1.5 }
        'string_counter' { $summary.session_receipts.A.counters.transmitted_usercmd_packet_count = '7188' }
        'string_epoch' { $summary.session_receipts.A.map_generation = '2' }
        'unknown_fact_bit' { $summary.session_receipts.A.fact_mask = 262143 }
        'string_close_timestamp' { $summary.session_receipts.A.close.transport_closed_process_ms = '150311' }
        'hash_publication_failed' {
            # Execute only the AST-selected production initializer/if block.
            # This mock is function-local and cannot touch a real binary or
            # invoke the capture script's broad live/publication code.
            function Get-FileSha256([string]$Path) { throw 'e282_injected_final_hash_read_failure' }
            $runExists = $true
            $runDirectoryCapability = [pscustomobject]@{ OfflineFixture = $true }
            $cleanupErrors = [Collections.Generic.List[string]]::new()
            $source = Get-E282HashPublicationSource
            . ([scriptblock]::Create($source.Text))
            Assert-E282 ($cleanupErrors.Count -eq 1 -and
                $cleanupErrors[0] -ceq 'orchestrator_hash_publication_failed' -and
                $null -eq $orchestratorSha256) 'production hash failure was ignored or replaced'
            $context.CleanupErrorCount = $cleanupErrors.Count
            $InputValue | Add-Member -NotePropertyName 'HashPublicationErrors' -NotePropertyValue $cleanupErrors
        }
        default { throw 'Unknown E28.2 fixture mutation.' }
    }
}
function Invoke-E282Evaluation([object]$InputValue) {
    $attestationFailure = $null
    try {
        Assert-ManualNormalCompletionAttestation -Result $InputValue.Native `
            -NativeSummary $InputValue.Summary -RunId $fixture.source_run_id `
            -ExpectedMap 'maps/crossfire.bsp' -Peer $true -LaunchIdentity $InputValue.Launch `
            -ClientBindings $InputValue.Bindings
        $InputValue.Context.ManualReceiptAttested = $true
        $expectation = Get-ProjectWorldPublicationExpectation -ManualKeyboard $true -ManualReceiptAttested $true
        Assert-OrchestratorValue $InputValue.Native 'client-world-state-published' $expectation
    } catch { $attestationFailure = $_; $InputValue.Context.PrimaryError = $_ }
    $decision = Get-FunctionalSmokeFinalVerdict -Context $InputValue.Context
    $summary = Get-ManualFinalSessionSummary -RunId $fixture.source_run_id `
        -NativeSummary $InputValue.Summary -Verdict $decision.Result `
        -Cleanup $(if ($InputValue.Context.OwnedStopped) { 'exact' } else { 'incomplete' }) `
        -Restoration $(if ($InputValue.Context.RestorationSucceeded) { 'scoped_exact' } else { 'scoped_not_verified' }) `
        -Fast $true -ManagedScopeEntries 0 -Peer $true
    return [pscustomobject]@{ Decision = $decision; Summary = $summary; AttestationFailure = $attestationFailure }
}
if ($FixtureChild) {
    $inputValue = New-E282EvaluationInput
    Set-E282NegativeCase $inputValue $Case
    $evaluation = Invoke-E282Evaluation $inputValue
    Write-Output "[e282-fixture] case=$Case native-result=$($inputValue.Native.Values['result']) native-exit=$($inputValue.Context.NativeExit)"
    Write-Output "[e282-fixture] attestation=$(if ($null -eq $evaluation.AttestationFailure) { 'passed' } else { 'rejected' })"
    Write-Output "[e282-fixture] wrapper-result=$($evaluation.Decision.Result) exit-code=$($evaluation.Decision.ExitCode)"
    if ($Case -ceq 'hash_publication_failed') {
        Write-Output "[e282-fixture] hash-publication-errors=$($inputValue.HashPublicationErrors.Count) error=$($inputValue.HashPublicationErrors[0])"
    }
    Write-ManualFinalSessionSummary $evaluation.Summary 'offline-fixture-report-not-published'
    exit $evaluation.Decision.ExitCode
}
$baseline = New-E282EvaluationInput
$accepted = Invoke-E282Evaluation $baseline
Assert-E282 ($accepted.Decision.Success -and $accepted.Decision.ExitCode -eq 0 -and
    $accepted.Decision.Result -ceq 'fresh_project_client_live_visual_control_integrated' -and
    $null -eq $accepted.AttestationFailure) 'successful normal run failed production wrapper classifier/attestation'
Assert-E282 ($accepted.Summary.clients.A.map_entry -ceq 'observed' -and
    $accepted.Summary.clients.B.map_entry -ceq 'observed' -and
    $accepted.Summary.clients.A.exit_reason -ceq 'window_close' -and
    $accepted.Summary.clients.B.exit_reason -ceq 'window_close') 'normal terminal summary erased map or close reason'
Assert-E282 ($accepted.Summary.clients.A.server_confirmation -ceq 'not_observed' -and
    $accepted.Summary.clients.B.server_confirmation -ceq 'not_observed' -and
    $accepted.Summary.clients.A.transport_ack_observed -and
    $accepted.Summary.clients.B.transport_ack_observed) 'transport ACK was promoted to explicit server confirmation'
Assert-E282 ($accepted.Summary.restoration_result -ceq 'scoped_exact' -and
    $accepted.Summary.managed_scope_entries -eq 0 -and
    $accepted.Summary.full_tree_verification -ceq 'not_performed') 'empty scoped restoration claimed whole-tree verification'
Assert-E282 ($baseline.Summary.authentication_status -ceq 'pending_or_unknown' -and
    $baseline.Summary.application_outcome.scripted_coverage -ceq 'not_evaluated') 'manual completion promoted independent authentication/scripted coverage'
Assert-E282 ($accepted.Summary.B_close_to_A_postcommit_removal_ms -eq 38.6624) 'actual measured committed-removal delay changed'
Assert-E282 ($accepted.Summary.connection_establishment -ceq 'accepted' -and
    $accepted.Summary.session_terminal_state -ceq 'normal_close_completed' -and
    $accepted.Summary.world_publication_phase -ceq 'committed_during_session') 'summary conflates establishment, gameplay and terminal phases'
$untypedWorld = Copy-E282Object $baseline.Summary
$untypedWorld.client_world_state_published = 'true'
$untypedWorldSummary = Get-ManualFinalSessionSummary -RunId $fixture.source_run_id `
    -NativeSummary $untypedWorld -Verdict 'local_server_ready_client_blocked' -Cleanup 'exact' `
    -Restoration 'scoped_exact' -Fast $true -ManagedScopeEntries 0 -Peer $true
Assert-E282 ($untypedWorldSummary.world_publication_phase -ceq 'not_observed') 'string world state claimed typed committed publication'
$ownedPeerInput = New-E282EvaluationInput
Set-E282NegativeCase $ownedPeerInput 'owned_peer_stop'
$ownedPeer = Invoke-E282Evaluation $ownedPeerInput
Assert-E282 ($ownedPeer.Decision.Success -and $ownedPeer.Summary.clients.B.exit_reason -ceq 'runner_termination' -and
    $ownedPeer.Summary.clients.B.disconnect_request -ceq 'not_observed' -and
    $ownedPeer.Summary.clients.B.server_confirmation -ceq 'not_observed' -and
    $null -eq $ownedPeer.Summary.clients.B.transport_ack_observed -and
    $null -eq $ownedPeer.Summary.B_close_to_A_postcommit_removal_ms) 'owned A-close-first peer stop was rejected or relabeled voluntary protocol close'
foreach ($fast in @($false, $true)) {
    foreach ($peer in @($false, $true)) {
        foreach ($timing in @($false, $true)) {
            $eligible = Get-FunctionalEvidenceEligibility -ProjectClientStockSignonMode $true `
                -FastManualMode $fast -TestStartHealth 0 -Peer $peer -ManualSessionTiming $timing
            Assert-E282 ($eligible -eq (-not $fast -and -not $peer -and -not $timing)) 'evidence eligibility contradicts independent contract'
        }
    }
}
$manualProofRejected = $false
try { [void](Get-ProjectWorldPublicationExpectation -ManualKeyboard $true -ManualReceiptAttested $false) }
catch { $manualProofRejected = $true }
Assert-E282 $manualProofRejected 'manual world publication was attested without typed proof'
Assert-E282 ((Get-ProjectWorldPublicationExpectation -ManualKeyboard $false -ManualReceiptAttested $false) -ceq 'true') 'scripted world-publication expectation changed'
$PSNativeCommandUseErrorActionPreference = $false
$cases = @(
    @{Name = 'normal'; Exit = 0; Result = 'fresh_project_client_live_visual_control_integrated'},
    @{Name = 'bounded_log_evicted'; Exit = 0; Result = 'fresh_project_client_live_visual_control_integrated'},
    @{Name = 'owned_peer_stop'; Exit = 0; Result = 'fresh_project_client_live_visual_control_integrated'},
    @{Name = 'server_not_ready'; Exit = 1; Result = 'local_server_startup_failed'},
    @{Name = 'connection_rejected'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'map_not_entered'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'normal_close_before_map'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'client_crash_after_signon'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'runtime_failure_after_gameplay'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'parser_failure_after_gameplay'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'peer_crash'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'cleanup_failed'; Exit = 1; Result = 'local_smoke_integrity_failed'},
    @{Name = 'restoration_failed'; Exit = 1; Result = 'local_smoke_integrity_failed'},
    @{Name = 'foreign_run'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'foreign_client'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'foreign_generation'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'receipt_missing'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'receipt_corrupt'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'receipt_truncated'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'legacy_without_evidence'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'receipt_without_gameplay'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'zero_world_publication'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'null_pid'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'null_creation'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'false_exit'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'bool_counter'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'fraction_counter'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'string_counter'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'string_epoch'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'unknown_fact_bit'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'string_close_timestamp'; Exit = 1; Result = 'local_server_ready_client_blocked'},
    @{Name = 'hash_publication_failed'; Exit = 1; Result = 'local_smoke_integrity_failed'}
)
if ($ReportingHelpersOnly) { Write-Output 'E28.2 PS-helper fixtures only: native projection not executed.' }
foreach ($scenario in $cases) {
    [string[]]$helperArguments = if ($ReportingHelpersOnly) { @('-ReportingHelpersOnly') } else { @() }
    $output = @(& (Join-Path $PSHOME 'pwsh.exe') -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath `
        -OrchestratorPath $OrchestratorPath -FixturePath $FixturePath -FixtureChild -Case $scenario.Name @helperArguments 2>&1)
    $actualExit = $global:LASTEXITCODE
    $text = $output -join "`n"
    Assert-E282 ($actualExit -eq $scenario.Exit) "$($scenario.Name) child exit $actualExit instead of $($scenario.Exit): $text"
    Assert-E282 ($text.Contains("wrapper-result=$($scenario.Result) exit-code=$($scenario.Exit)")) "$($scenario.Name) final production verdict missing: $text"
    Assert-E282 ($text.Contains("Runner verdict: $($scenario.Result)")) "$($scenario.Name) concise final summary contradicts verdict"
    if ($scenario.Name -ceq 'hash_publication_failed') {
        Assert-E282 ($text.Contains('hash-publication-errors=1 error=orchestrator_hash_publication_failed')) 'late hash negative did not execute the actual production failure block'
    }
    if ($scenario.Exit -eq 0) {
        Assert-E282 ($text.Contains('attestation=passed') -and
            $text.Contains('server confirmation: not_observed') -and
            $text.Contains('Managed scope size: 0; full-tree verification: not_performed')) "$($scenario.Name) successful summary altered distinctions"
        Assert-E282 ($text.Contains('A disconnect request: sent; transport ACK: true; server confirmation: not_observed')) "$($scenario.Name) A transport ACK omitted or promoted to server confirmation"
        if ($scenario.Name -ceq 'owned_peer_stop') {
            Assert-E282 ($text.Contains('B disconnect request: not_observed; transport ACK: not_observed; server confirmation: not_observed')) 'owned B stop forged a protocol send or transport/server confirmation'
        } else {
            Assert-E282 ($text.Contains('B disconnect request: sent; transport ACK: true; server confirmation: not_observed')) "$($scenario.Name) B transport ACK omitted or promoted to server confirmation"
        }
    }
    Write-Output "PASS E28.2 $(if ($ReportingHelpersOnly) { 'PS-helper unit fixture' } else { 'production native -> PS' }) attestation -> final summary/actual child exit: $($scenario.Name)"
}
Write-Output "PASS E28.2 retrospective_offline_reclassification source_run_id=$($fixture.source_run_id) cases=$($cases.Count) native_projection_executed=$((-not $ReportingHelpersOnly).ToString().ToLowerInvariant()) new_live_sessions=0"
