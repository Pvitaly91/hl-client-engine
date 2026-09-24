#requires -Version 5.1

<#
.SYNOPSIS
Validates or runs the isolated stock-runtime capture transaction.

.DESCRIPTION
Active capture is disabled by default. It requires an explicit, case-sensitive
confirmation token and remains fail-closed unless the project-owned Windows
orchestrator reports a valid binary profile and a successful dynamic-WFP
isolation canary. PowerShell owns the research/external snapshots, exact
restoration and one-time final run-manifest publication; C++ owns every stock
process, socket and WFP lifecycle.

ValidateResearchRoot is read-only with respect to the repository and research
tree and starts no stock/game process. It invokes the read-only Windows
`subst.exe` listing to reject substituted drive aliases.
#>
[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'DirectoryCapabilityBootstrap')]
    [switch]$InitializeDirectoryCapability,

    [Parameter(Mandatory = $true, ParameterSetName = 'RestorationSelfTest')]
    [switch]$ValidateRestorationGuard,

    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [switch]$ValidateResearchRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [switch]$ValidateActiveCaptureEnvironment,

    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [switch]$DiagnoseServerProfile,

    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [switch]$PrivateDiagnoseServerProfile,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [switch]$FunctionalSmoke,

    [Parameter(ParameterSetName = 'FunctionalSmoke')]
    [switch]$ProjectClientStockSignon,

    [Parameter(ParameterSetName = 'FunctionalSmoke')]
    [ValidateSet('delta-schemas', 'live-runtime-state', 'live-usercmd-check', 'live-visual-control')]
    [string]$ProjectClientStop = 'delta-schemas',

    [Parameter(ParameterSetName = 'FunctionalSmoke')]
    [ValidateSet('scripted-check', 'scripted-side-check', 'scripted-jump-duck-check', 'scripted-speed-check', 'keyboard-mouse')]
    [string]$ProjectClientLiveInput = 'scripted-check',

    [Parameter(ParameterSetName = 'FunctionalSmoke')]
    [ValidateSet('off', 'reference')]
    [string]$ProjectClientPrediction = 'off',

    [Parameter(ParameterSetName = 'FunctionalSmoke')]
    [string]$SteamApiRuntimePath,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [switch]$FunctionalRuntimeCapture,

    [Parameter(Mandatory = $true, ParameterSetName = 'ExternalDriftControl')]
    [switch]$MeasureExternalDriftControl,

    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnosticSelfTest')]
    [switch]$ValidateServerProfileDiagnosticPolicy,

    [Parameter(Mandatory = $true, ParameterSetName = 'OrchestratorStartupBoundarySelfTest')]
    [switch]$ValidateOrchestratorStartupBoundary,

    [Parameter(Mandatory = $true, ParameterSetName = 'OrchestratorStartupBoundarySelfTest')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalPublicationRoundtripSelfTest')]
    [ValidateNotNullOrEmpty()]
    [string]$OrchestratorPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalPublicationRoundtripSelfTest')]
    [ValidateNotNullOrEmpty()]
    [string]$FakeClientPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalPublicationRoundtripSelfTest')]
    [ValidateNotNullOrEmpty()]
    [string]$FakeServerPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'RetainedBackupRecovery')]
    [switch]$RecoverRetainedBackup,

    [Parameter(Mandatory = $true, ParameterSetName = 'RetainedBackupRecoverySelfTest')]
    [switch]$ValidateRetainedBackupRecovery,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalFailureRetentionSelfTest')]
    [switch]$ValidateFunctionalFailureRetention,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalResearchProjectionSelfTest')]
    [switch]$ValidateFunctionalResearchProjection,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalPublicationRoundtripSelfTest')]
    [switch]$ValidateFunctionalPublicationRoundtrip,

    [Parameter(Mandatory = $true, ParameterSetName = 'RetainedBackupRecovery')]
    [ValidateNotNullOrEmpty()]
    [string]$RetainedBackupRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ExternalDriftControl')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'RetainedBackupRecovery')]
    [ValidateNotNullOrEmpty()]
    [string]$ResearchHalfLifeRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ExternalDriftControl')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateNotNullOrEmpty()]
    [string]$ClientPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Preflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ExternalDriftControl')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateNotNullOrEmpty()]
    [string]$HldsPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalPublicationRoundtripSelfTest')]
    [ValidateNotNullOrEmpty()]
    [string]$CaptureToolPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalPublicationRoundtripSelfTest')]
    [ValidateNotNullOrEmpty()]
    [string]$CheckerPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalPublicationRoundtripSelfTest')]
    [ValidateNotNullOrEmpty()]
    [string]$HlclientPath,

    [Parameter(ParameterSetName = 'Capture')]
    [switch]$EnableActiveCapture,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [AllowEmptyString()]
    [string]$ConfirmActiveCapture,

    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [AllowEmptyString()]
    [string]$ConfirmPrivateDiagnostic,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [AllowEmptyString()]
    [string]$ConfirmFunctionalSmoke,

    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [AllowEmptyString()]
    [string]$ConfirmFunctionalRuntimeCapture,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateNotNullOrEmpty()]
    [string]$NetworkIsolationGuardPath,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ActivePreflight')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ExternalDriftControl')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateNotNullOrEmpty()]
    [string]$AppManifestPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'ExternalDriftControl')]
    [ValidateSet('idle_control', 'post_cleanup_settle')]
    [string]$DriftPhase,

    [Parameter(Mandatory = $true, ParameterSetName = 'ExternalDriftControl')]
    [ValidateRange(0, 60)]
    [int]$DriftDelaySeconds,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'ActivePreflight')]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedCaptureToolSha256,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateSet('valve')]
    [string]$Game,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateSet('boot_camp', 'crossfire', 'stalkyard')]
    [string]$Map,

    [Parameter(Mandatory = $true, ParameterSetName = 'Capture')]
    [ValidateSet(
        'baseline', 'idle-runtime', 'forward', 'backward', 'left', 'right',
        'forward-right', 'jump', 'duck', 'duck-stand', 'yaw-positive',
        'yaw-negative', 'pitch-positive', 'pitch-negative', 'second-client',
        'reconnect', 'map-change', 'server-restart', 'respawn',
        'low-updaterate', 'high-updaterate', 'low-cmdrate', 'high-cmdrate',
        'drop-server-to-client-transport-ordinal',
        'duplicate-server-to-client-transport-ordinal',
        'reorder-server-to-client-transport-ordinal',
        'drop-server-runtime', 'drop-two-server-runtime',
        'duplicate-server-runtime', 'reorder-server-runtime',
        'drop-client-move', 'delay-client-move')]
    [string]$Scenario,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'ActivePreflight')]
    [Parameter(ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(ParameterSetName = 'FunctionalSmoke')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1024, 65534)]
    [int]$RelayPort = 27140,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'ActivePreflight')]
    [Parameter(ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(ParameterSetName = 'FunctionalSmoke')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1024, 65534)]
    [int]$ServerPort = 27141,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(Mandatory = $true, ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalSmoke')]
    [Parameter(Mandatory = $true, ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateNotNullOrEmpty()]
    [string]$OutputRoot = '.\manual-artifacts\stock-runtime',

    [Parameter(ParameterSetName = 'Capture')]
    [switch]$PreCampaignCanary,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(ParameterSetName = 'FunctionalSmoke')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(5, 300)]
    [int]$MaximumDurationSeconds = 45,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'ServerProfileDiagnostic')]
    [Parameter(ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [Parameter(ParameterSetName = 'FunctionalSmoke')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateSet('legacy-stdio-hlds-banner-v1',
        'steam-hlds-10210-no-mode-banner-v1')]
    [string]$ServerProfileId = 'legacy-stdio-hlds-banner-v1',

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1, 65536)]
    [int]$MaximumDatagrams = 8192,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1, 536870912)]
    [Int64]$MaximumTotalRawBytes = 67108864,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1, 65507)]
    [int]$MaximumPayloadBytes = 65507,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1, 67108864)]
    [int]$MaximumReassembledBytes = 8388608,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1, 268435456)]
    [int]$MaximumDecompressedBytes = 33554432,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1, 65536)]
    [int]$MaximumMessageCount = 8192,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1, 32768)]
    [int]$MaximumRuntimeFrames = 4096,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1, 65536)]
    [int]$MaximumClientPackets = 4096,

    [Parameter(ParameterSetName = 'Capture')]
    [Parameter(ParameterSetName = 'FunctionalRuntimeCapture')]
    [ValidateRange(1, 65536)]
    [int]$MaximumServerPackets = 4096,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65536)]
    [int]$MutationAfterClientPackets = 20,

    [Parameter(ParameterSetName = 'Capture')]
    [ValidateRange(1, 65536)]
    [int]$MutationAfterServerPackets = 20,

    [Parameter(ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [switch]$EnableWriterTraceHandoff,

    [Parameter(ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [string]$WriterTraceToolPath,

    [Parameter(ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedWriterTraceToolSha256,

    [Parameter(ParameterSetName = 'PrivateServerProfileDiagnostic')]
    [string]$WriterTraceTargetPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$manualRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'manual-artifacts')).TrimEnd('\', '/')
$requiredOutputRoot = [IO.Path]::GetFullPath((Join-Path $manualRoot 'stock-runtime')).TrimEnd('\', '/')
$requiredCanaryOutputRoot = [IO.Path]::GetFullPath(
    (Join-Path $manualRoot 'stock-runtime-canary')).TrimEnd('\', '/')
$requiredServerProfileDiagnosticRoot = [IO.Path]::GetFullPath(
    (Join-Path $manualRoot 'stock-runtime-server-profile-diagnostic')).TrimEnd('\', '/')
$requiredPrivateServerProfileDiagnosticRoot = [IO.Path]::GetFullPath(
    (Join-Path $manualRoot 'stock-runtime-server-profile-private')).TrimEnd('\', '/')
$requiredFunctionalSmokeRoot = [IO.Path]::GetFullPath(
    (Join-Path $manualRoot 'research-copy-smoke')).TrimEnd('\', '/')
$requiredFunctionalRuntimeCaptureRoot = [IO.Path]::GetFullPath(
    (Join-Path $manualRoot 'research-runtime-capture')).TrimEnd('\', '/')
$markerName = '.hlclient-research-isolated'
$markerText = 'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1'
$pendingMarkerName = '.hlclient-research-pending'
$preparationManifestName = '.hlclient-research-preparation.json'
$externalApprovalName = 'external-target-approval.json'
$activeCaptureToken = 'HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1'
$privateDiagnosticToken = 'HLCLIENT_PRIVATE_HLDS_BANNER_DIAGNOSTIC_V1'
$functionalSmokeToken = 'HLCLIENT_LOCAL_RESEARCH_COPY_SMOKE_V1'
$functionalRuntimeCaptureToken = 'HLCLIENT_FUNCTIONAL_RUNTIME_CAPTURE_V1'
$stockSteamRewritePolicyId = 'legacy-strict-v1'
$serverProfileDiagnosticMode =
    $PSCmdlet.ParameterSetName -ceq 'ServerProfileDiagnostic'
$privateServerProfileDiagnosticMode =
    $PSCmdlet.ParameterSetName -ceq 'PrivateServerProfileDiagnostic'
$anyServerProfileDiagnosticMode =
    $serverProfileDiagnosticMode -or $privateServerProfileDiagnosticMode
$functionalSmokeMode = $PSCmdlet.ParameterSetName -ceq 'FunctionalSmoke'
$projectClientStockSignonMode =
    $functionalSmokeMode -and [bool]$ProjectClientStockSignon
$projectClientLiveRuntimeMode = $projectClientStockSignonMode -and
    $ProjectClientStop -ceq 'live-runtime-state'
$projectClientUserCmdMode = $projectClientStockSignonMode -and
    $ProjectClientStop -ceq 'live-usercmd-check'
$projectClientVisualMode = $projectClientStockSignonMode -and
    $ProjectClientStop -ceq 'live-visual-control'
if ($projectClientVisualMode -and
    $ProjectClientLiveInput -ceq 'scripted-jump-duck-check') {
    Import-Module (Join-Path $PSScriptRoot 'g_jump_duck_summary.psm1') `
        -ErrorAction Stop
}
if ($projectClientVisualMode -and
    $ProjectClientLiveInput -ceq 'scripted-speed-check') {
    Import-Module (Join-Path $PSScriptRoot 'h1_speed_summary.psm1') `
        -ErrorAction Stop
}
$projectClientLiveMode = $projectClientLiveRuntimeMode -or
    $projectClientUserCmdMode -or $projectClientVisualMode
$functionalRuntimeCaptureMode =
    $PSCmdlet.ParameterSetName -ceq 'FunctionalRuntimeCapture'
$functionalResearchProjectionSelfTestMode =
    $PSCmdlet.ParameterSetName -ceq 'FunctionalResearchProjectionSelfTest'
$functionalPolicyMode = $functionalSmokeMode -or $functionalRuntimeCaptureMode -or
    $functionalResearchProjectionSelfTestMode
if ($projectClientStockSignonMode -ne
    (-not [string]::IsNullOrWhiteSpace($SteamApiRuntimePath))) {
    throw 'ProjectClientStockSignon and SteamApiRuntimePath must be supplied together.'
}
if (-not $projectClientStockSignonMode -and
    $PSBoundParameters.ContainsKey('ProjectClientStop')) {
    throw 'ProjectClientStop requires ProjectClientStockSignon.'
}
if ($PSBoundParameters.ContainsKey('ProjectClientLiveInput') -and
    -not $projectClientVisualMode) {
    throw 'ProjectClientLiveInput requires live-visual-control.'
}
if ($ProjectClientPrediction -ceq 'reference' -and -not $projectClientVisualMode) {
    throw 'ProjectClientPrediction reference requires live-visual-control.'
}
$writerTraceModulePath = Join-Path $PSScriptRoot 'stock_writer_trace_handoff.psm1'
if ($EnableWriterTraceHandoff) {
    if (-not $privateServerProfileDiagnosticMode -or
        [string]::IsNullOrWhiteSpace($WriterTraceToolPath) -or
        [string]::IsNullOrWhiteSpace($ExpectedWriterTraceToolSha256) -or
        [string]::IsNullOrWhiteSpace($WriterTraceTargetPath) -or
        -not (Test-Path -LiteralPath $writerTraceModulePath -PathType Leaf)) {
        throw 'writer_trace_handoff_request_invalid'
    }
    Import-Module $writerTraceModulePath -Force
} elseif ($PSBoundParameters.ContainsKey('WriterTraceToolPath') -or
    $PSBoundParameters.ContainsKey('ExpectedWriterTraceToolSha256') -or
    $PSBoundParameters.ContainsKey('WriterTraceTargetPath')) {
    throw 'writer_trace_handoff_parameters_without_enable'
}
$steamConfigProjectionImplementation = Join-Path $PSScriptRoot `
    'stock_steam_user_config_projection.ps1'
if (-not (Test-Path -LiteralPath $steamConfigProjectionImplementation `
        -PathType Leaf)) {
    throw 'Stock Steam user-config semantic projection is absent.'
}
. $steamConfigProjectionImplementation
$externalDriftImplementation = Join-Path $PSScriptRoot 'stock_external_drift.ps1'
if (-not (Test-Path -LiteralPath $externalDriftImplementation -PathType Leaf)) {
    throw 'Stock external drift implementation is absent.'
}
. $externalDriftImplementation
$maximumEntries = 199999
$maximumResearchBytes = [Int64]17179869184
$maximumSteamManifestBytes = 1048576
$protectedRoots = @(
    'steam_appid.txt',
    'config.cfg', 'userconfig.cfg', 'autoexec.cfg', 'custom.hpk',
    'qconsole.log', 'hlds.log', 'logs', 'screenshots', 'save', 'demo', 'demos',
    'valve/config.cfg', 'valve/voice_ban.dt',
    'valve/userconfig.cfg', 'valve/autoexec.cfg',
    'valve/custom.hpk', 'valve/qconsole.log', 'valve/hlds.log', 'valve/logs',
    'valve/screenshots', 'valve/save', 'valve/demo', 'valve/demos',
    'valve/config', 'platform/config')
$functionalMutablePaths = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($functionalMutablePath in @(
        'steam_appid.txt', 'valve/config.cfg', 'valve/voice_ban.dt',
        'platform/config/InGameDialogConfig.vdf',
        'platform/config/ServerBrowser.vdf')) {
    [void]$functionalMutablePaths.Add($functionalMutablePath)
}

# This is intentionally the first Capture-mode action. It does not resolve or
# inspect any caller-supplied path and occurs before output, backup, socket,
# process or WFP mutation. The confirmation value has no environment/config
# fallback and Ordinal comparison is case-sensitive.
if (($PSCmdlet.ParameterSetName -eq 'Capture' -and
        (-not $EnableActiveCapture -or
         $ConfirmActiveCapture -cne $activeCaptureToken)) -or
    ($PSCmdlet.ParameterSetName -eq 'ServerProfileDiagnostic' -and
        $ConfirmActiveCapture -cne $activeCaptureToken) -or
    ($PSCmdlet.ParameterSetName -eq 'PrivateServerProfileDiagnostic' -and
        $ConfirmPrivateDiagnostic -cne $privateDiagnosticToken) -or
    ($functionalSmokeMode -and
        $ConfirmFunctionalSmoke -cne $functionalSmokeToken) -or
    ($functionalRuntimeCaptureMode -and
        $ConfirmFunctionalRuntimeCapture -cne
            $functionalRuntimeCaptureToken)) {
    Write-Output '[stock-runtime-capture] active-capture=explicit-opt-in-required'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] files-written=0'
    Write-Output '[stock-runtime-capture] network-operations=0'
    Write-Output '[stock-runtime-capture] wfp-sessions-started=0'
    Write-Output '[stock-runtime-capture] capture-runs-created=0'
    Write-Output '[stock-runtime-capture] restoration-backups-created=0'
    throw 'Active stock-runtime capture requires the exact explicit confirmation token; no input path was resolved and no mutation occurred.'
}

function Test-PathAtOrBelow {
    param([string]$Path, [string]$Root)
    $pathValue = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $rootValue = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    return $pathValue.Equals($rootValue, [StringComparison]::OrdinalIgnoreCase) -or
        $pathValue.StartsWith(
            $rootValue + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)
}

function Get-PhysicalPathIdentity {
    param([string]$Path, [string]$Label)
    $canonical = [IO.Path]::GetFullPath(
        (Get-Item -LiteralPath $Path -Force -ErrorAction Stop).FullName
    ).TrimEnd('\', '/')
    if ($canonical -cnotmatch '^(?<drive>[A-Za-z]):\\(?<suffix>.*)$') {
        throw "$Label physical identity requires a drive-letter path."
    }
    $driveName = $Matches.drive.ToUpperInvariant()
    $relativeSuffix = $Matches.suffix
    if ($null -eq ('Hlclient.StockRuntimePathIdentity' -as [type])) {
        Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using System.Text;

namespace Hlclient
{
    public static class StockRuntimePathIdentity
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetVolumeNameForVolumeMountPoint(
            string volumeMountPoint,
            StringBuilder volumeName,
            int bufferLength);
    }
}
'@
    }
    $volumeName = [Text.StringBuilder]::new(64)
    if (-not [Hlclient.StockRuntimePathIdentity]::GetVolumeNameForVolumeMountPoint(
            ($driveName + ':\'), $volumeName, $volumeName.Capacity)) {
        $errorCode = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "$Label physical volume identity is unavailable (Win32 $errorCode)."
    }
    return [pscustomobject]@{
        Path = $canonical
        Volume = $volumeName.ToString().ToUpperInvariant()
        Relative = ([string]$relativeSuffix).TrimEnd('\', '/')
    }
}

function Test-PhysicalPathAtOrBelow {
    param([object]$PathIdentity, [object]$RootIdentity)
    if ($PathIdentity.Volume -cne $RootIdentity.Volume) { return $false }
    $path = $PathIdentity.Relative.TrimEnd('\', '/')
    $root = $RootIdentity.Relative.TrimEnd('\', '/')
    return $path.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
        $path.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Assert-PathBelowRoot {
    param([string]$Path, [string]$Root, [string]$Label)
    $pathValue = [IO.Path]::GetFullPath($Path)
    $rootValue = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not $pathValue.StartsWith(
            $rootValue + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be a canonical descendant of its exact root."
    }
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
        if (Test-Path -LiteralPath $current) {
            Assert-NoReparsePoint -Path $current -Label $Label
        }
    }
}

function Assert-OnlyDefaultDataStream {
    param([string]$Path, [string]$Label)
    Initialize-RestorationDirectoryCapabilityNative
    try {
        [Hlclient.StockRuntimeDirectoryCapability]::ValidateOnlyDefaultDataStream(
            [IO.Path]::GetFullPath($Path))
    } catch {
        throw [InvalidOperationException]::new(
            "$Label must contain only its default data stream.",
            $_.Exception)
    }
}

function Assert-NoHardLink {
    param([string]$Path, [string]$Label)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) { return }
    $property = $item.PSObject.Properties['LinkType']
    if ($null -eq $property) {
        throw "$Label hard-link state could not be established."
    }
    if (-not [string]::IsNullOrEmpty([string]$property.Value)) {
        throw "$Label must not be linked."
    }
}

function Get-BoundedItems {
    param([string]$Root)
    $items = [Collections.Generic.List[object]]::new()
    $queue = [Collections.Generic.Queue[IO.DirectoryInfo]]::new()
    $queue.Enqueue([IO.DirectoryInfo](Get-Item -LiteralPath $Root -Force))
    while ($queue.Count -ne 0) {
        $directory = $queue.Dequeue()
        Assert-NoReparsePoint -Path $directory.FullName -Label 'research tree'
        foreach ($item in @($directory.GetFileSystemInfos())) {
            if ($items.Count -ge $maximumEntries) {
                throw 'Research tree exceeds its entry bound.'
            }
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Research tree contains a reparse point.'
            }
            [void]$items.Add($item)
            if (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                $queue.Enqueue([IO.DirectoryInfo]$item)
            }
        }
    }
    return @($items)
}

function Get-FileSha256 {
    param([string]$Path)
    $stream = [IO.File]::Open(
        $Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    } finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Assert-ExternalApprovalDigestAvailable {
    param([string]$ExpectedSha256)

    $reviewParent = [IO.Path]::GetFullPath((Join-Path `
            $manualRoot 'stock-runtime-source-review')).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $reviewParent -PathType Container)) {
        throw 'Reviewed research copy lacks its local approval artifact.'
    }
    Assert-NoReparsePointInExistingPath $reviewParent `
        'external-target approval root'
    Assert-OnlyDefaultDataStream $reviewParent 'external-target approval root'

    $reviewRoots = @(Get-ChildItem -LiteralPath $reviewParent -Force `
            -Directory -ErrorAction Stop)
    if ($reviewRoots.Count -gt 1024) {
        throw 'External-target approval root exceeds its review bound.'
    }
    $digestMatchCount = 0
    foreach ($reviewRoot in $reviewRoots) {
        if ($reviewRoot.Name -cnotmatch '^[0-9a-f]{32}$') { continue }
        if (($reviewRoot.Attributes -band
                [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'External-target approval review root must not be a reparse point.'
        }
        $candidate = Join-Path $reviewRoot.FullName $externalApprovalName
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $item = Get-Item -LiteralPath $candidate -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
            $item.Length -lt 1 -or $item.Length -gt 65536) {
            throw 'External-target approval artifact is not a bounded ordinary file.'
        }
        Assert-OnlyDefaultDataStream $candidate `
            'external-target approval artifact'
        Assert-NoHardLink $candidate 'external-target approval artifact'
        $observed = (Get-FileSha256 $candidate).ToLowerInvariant()
        if ($observed -ceq $ExpectedSha256) { ++$digestMatchCount }
    }
    if ($digestMatchCount -ne 1) {
        throw 'Reviewed research copy approval digest is not backed by one exact local artifact.'
    }
}

function Read-BoundedAsciiMarker {
    param([string]$Path, [int]$MaximumBytes = 128)
    $stream = [IO.File]::Open(
        $Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 1 -or $stream.Length -gt $MaximumBytes) {
            throw 'Research marker length is outside its bound.'
        }
        $bytes = [byte[]]::new([int]$stream.Length)
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) { throw 'Research marker ended before its declared length.' }
            $offset += $read
        }
        if (@($bytes | Where-Object { $_ -gt 0x7F }).Count -ne 0) {
            throw 'Research marker must contain ASCII only.'
        }
        return [Text.Encoding]::ASCII.GetString($bytes)
    } finally {
        $stream.Dispose()
    }
}

function Get-RelativePath {
    param([string]$Path, [string]$Root)
    $prefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Research entry escaped its root.'
    }
    return $full.Substring($prefix.Length).Replace('\', '/')
}

function Get-ResearchSnapshot {
    param([string]$Root)
    # The root directory is not returned by Get-BoundedItems. Reject a named
    # stream here independently so a stream added after preparation (or after
    # the structural preflight) cannot be omitted from restoration evidence.
    Assert-OnlyDefaultDataStream -Path $Root -Label 'research snapshot root'
    $entries = [Collections.Generic.List[object]]::new()
    $rootItem = Get-Item -LiteralPath $Root -Force
    [void]$entries.Add([pscustomobject]@{
        RelativePath = '.'; Kind = 'directory'; Length = [Int64]0; Sha256 = ''
        CreationTicks = $rootItem.CreationTimeUtc.Ticks
        WriteTicks = $rootItem.LastWriteTimeUtc.Ticks
        Attributes = [Int64]$rootItem.Attributes
    })
    [Int64]$totalBytes = 0
    foreach ($item in @(Get-BoundedItems -Root $Root | Sort-Object FullName)) {
        $relative = Get-RelativePath -Path $item.FullName -Root $Root
        Assert-OnlyDefaultDataStream -Path $item.FullName -Label 'research entry'
        $isDirectory = ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0
        if (-not $isDirectory) { Assert-NoHardLink -Path $item.FullName -Label 'research file' }
        [Int64]$length = if ($isDirectory) { 0 } else { $item.Length }
        if ($length -lt 0 -or $totalBytes -gt ($maximumResearchBytes - $length)) {
            throw 'Research tree exceeds its byte bound.'
        }
        $totalBytes += $length
        [void]$entries.Add([pscustomobject]@{
            RelativePath = $relative
            Kind = $(if ($isDirectory) { 'directory' } else { 'file' })
            Length = $length
            Sha256 = $(if ($isDirectory) { '' } else { Get-FileSha256 $item.FullName })
            CreationTicks = $item.CreationTimeUtc.Ticks
            WriteTicks = $item.LastWriteTimeUtc.Ticks
            Attributes = [Int64]$item.Attributes
        })
    }
    $canonical = @($entries | ForEach-Object {
        '{0}|{1}|{2}|{3}|{4}|{5}|{6}' -f $_.RelativePath, $_.Kind,
            $_.Length, $_.Sha256, $_.CreationTicks, $_.WriteTicks, $_.Attributes
    }) -join "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($canonical)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $manifest = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
    return [pscustomobject]@{
        Entries = @($entries); EntryCount = $entries.Count
        TotalBytes = $totalBytes; ManifestSha256 = $manifest
    }
}

function Get-RetainedBackupRecoverySnapshot {
    param([string]$Root, [switch]$ExcludeBackupIdentityLock)
    Assert-OnlyDefaultDataStream $Root 'retained recovery snapshot root'
    $entries = [Collections.Generic.List[object]]::new()
    [Int64]$totalBytes = 0
    foreach ($item in @(Get-BoundedItems -Root $Root | Sort-Object FullName)) {
        $relative = Get-RelativePath -Path $item.FullName -Root $Root
        if ($ExcludeBackupIdentityLock -and
            $relative -ceq '.hlclient-restoration-identity-lock') {
            continue
        }
        Assert-OnlyDefaultDataStream $item.FullName `
            'retained recovery snapshot entry'
        $isDirectory =
            ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0
        if (-not $isDirectory) {
            Assert-NoHardLink $item.FullName 'retained recovery snapshot file'
        }
        [Int64]$length = if ($isDirectory) { 0 } else { $item.Length }
        if ($length -lt 0 -or
            $totalBytes -gt ($maximumResearchBytes - $length)) {
            throw 'Retained recovery snapshot exceeds its byte bound.'
        }
        $totalBytes += $length
        [void]$entries.Add([pscustomobject]@{
            RelativePath = $relative
            Kind = $(if ($isDirectory) { 'directory' } else { 'file' })
            Length = $length
            Sha256 = $(if ($isDirectory) { '' } else {
                    Get-FileSha256 $item.FullName
                })
            WriteTicks = $(if ($isDirectory) { [Int64]0 } else {
                    $item.LastWriteTimeUtc.Ticks
                })
            Attributes = [Int64]$item.Attributes
        })
    }
    $canonical = @($entries | ForEach-Object {
        '{0}|{1}|{2}|{3}|{4}|{5}' -f $_.RelativePath, $_.Kind,
            $_.Length, $_.Sha256, $_.WriteTicks, $_.Attributes
    }) -join "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($canonical)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $manifest =
            ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
    } finally { $sha.Dispose() }
    return [pscustomobject]@{
        EntryCount = $entries.Count
        TotalBytes = $totalBytes
        ManifestSha256 = $manifest
    }
}

function Invoke-RetainedBackupRecoveryInspection {
    param([string]$ResearchRoot, [string]$BackupRoot)
    $research = [IO.Path]::GetFullPath($ResearchRoot).TrimEnd('\', '/')
    $backup = [IO.Path]::GetFullPath($BackupRoot).TrimEnd('\', '/')
    $systemTemporaryRoot =
        [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $research -PathType Container)) {
        throw 'Retained recovery research root is absent.'
    }
    if (-not (Test-Path -LiteralPath $backup -PathType Container) -or
        [IO.Path]::GetDirectoryName($backup).TrimEnd('\', '/') -ine
            $systemTemporaryRoot -or
        [IO.Path]::GetFileName($backup) -cnotmatch
            '^hlclient-stock-runtime-restore-[0-9a-f]{32}$') {
        throw 'Retained recovery backup identity is invalid.'
    }
    if ((Test-PathAtOrBelow $backup $research) -or
        (Test-PathAtOrBelow $research $backup)) {
        throw 'Retained recovery paths are not disjoint.'
    }
    Assert-NoReparsePointInExistingPath $research `
        'retained recovery research root'
    Assert-NoReparsePointInExistingPath $backup `
        'retained recovery backup root'
    Assert-OnlyDefaultDataStream $research 'retained recovery research root'
    Assert-OnlyDefaultDataStream $backup 'retained recovery backup root'
    $backupItems = @(Get-ChildItem -LiteralPath $backup -Force)
    $data = Join-Path $backup 'data'
    if ($backupItems.Count -ne 1 -or
        $backupItems[0].Name -cne 'data' -or
        -not $backupItems[0].PSIsContainer -or
        -not (Test-Path -LiteralPath $data -PathType Container)) {
        throw 'Legacy retained recovery backup has an unexpected root shape.'
    }
    $identityLock = Join-Path $data '.hlclient-restoration-identity-lock'
    if (-not (Test-Path -LiteralPath $identityLock -PathType Container) -or
        @(Get-ChildItem -LiteralPath $identityLock -Force).Count -ne 0) {
        throw 'Legacy retained recovery backup identity lock is invalid.'
    }
    foreach ($marker in @($markerName, '.hlclient-research-pending',
            '.hlclient-research-preparation.json')) {
        $researchMarker = Join-Path $research $marker
        $backupMarker = Join-Path $data $marker
        if (-not (Test-Path -LiteralPath $researchMarker -PathType Leaf) -or
            -not (Test-Path -LiteralPath $backupMarker -PathType Leaf) -or
            (Get-FileSha256 $researchMarker) -cne
                (Get-FileSha256 $backupMarker)) {
            throw "Retained recovery provenance marker mismatch: $marker"
        }
    }
    $markerValue = Read-BoundedAsciiMarker (Join-Path $research $markerName)
    if ($markerValue -cne $markerText -and
        $markerValue -cne ($markerText + "`n") -and
        $markerValue -cne ($markerText + "`r`n")) {
        throw 'Retained recovery isolation marker is invalid.'
    }
    $preparation = Read-BoundedJson `
        (Join-Path $research '.hlclient-research-preparation.json') `
        65536 'retained recovery preparation manifest'
    if ([string]$preparation.schema -cne
            'hlclient.stock-runtime-research-preparation.v3' -or
        [string]$preparation.preparation_status -cne
            'exact-materialized-copy-verified') {
        throw 'Retained recovery preparation provenance is invalid.'
    }
    $current = Get-RetainedBackupRecoverySnapshot $research
    $before = Get-RetainedBackupRecoverySnapshot $data `
        -ExcludeBackupIdentityLock
    $unchanged = $current.EntryCount -eq $before.EntryCount -and
        $current.TotalBytes -eq $before.TotalBytes -and
        $current.ManifestSha256 -ceq $before.ManifestSha256
    return [pscustomobject]@{
        RecoveryStatus = $(if ($unchanged) {
                'no_restoration_needed_verified_unchanged'
            } else { 'recovery_unresolved' })
        RestorationStatus = 'restoration_not_attempted'
        BackupStatus = 'retained'
        CurrentEntryCount = $current.EntryCount
        BackupEntryCount = $before.EntryCount
        CurrentTotalBytes = $current.TotalBytes
        BackupTotalBytes = $before.TotalBytes
        CurrentManifestSha256 = $current.ManifestSha256
        BackupManifestSha256 = $before.ManifestSha256
    }
}

function Test-ProtectedPath {
    param([string]$RelativePath)
    $normalized = $RelativePath.Replace('\', '/').TrimStart('/')
    if ($normalized.EndsWith('.dem', [StringComparison]::OrdinalIgnoreCase)) { return $true }
    foreach ($root in $protectedRoots) {
        if ($normalized.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
            $normalized.StartsWith($root + '/', [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Initialize-RestorationDirectoryCapabilityNative {
    if ($null -ne ('Hlclient.StockRuntimeDirectoryCapability' -as [type])) {
        return
    }
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace Hlclient
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct StockRuntimeByHandleFileInformation
    {
        public uint FileAttributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct StockRuntimeIoStatusBlock
    {
        public IntPtr Status;
        public UIntPtr Information;
    }

    public sealed class StockRuntimeDirectoryCapability : IDisposable
    {
        private const uint GenericRead = 0x80000000;
        private const uint GenericWrite = 0x40000000;
        private const uint DeleteAccess = 0x00010000;
        private const uint FileReadAttributes = 0x80;
        private const uint FileShareRead = 0x1;
        private const uint FileShareWrite = 0x2;
        private const uint CreateNew = 1;
        private const uint OpenExisting = 3;
        private const uint FileAttributeTemporary = 0x100;
        private const uint FileFlagBackupSemantics = 0x02000000;
        private const uint FileFlagOpenReparsePoint = 0x00200000;
        private const uint FileFlagWriteThrough = 0x80000000;
        private const uint FileAttributeDirectory = 0x10;
        private const uint FileAttributeReparsePoint = 0x400;
        private const uint FileNameNormalized = 0x0;
        private const uint VolumeNameDos = 0x0;
        private const int FileRenameInfo = 3;
        private const int FileDispositionInfo = 4;
        private const int FileStreamInfo = 7;
        private const int NtFileStreamInformation = 22;
        private const int StatusNoMoreFiles = unchecked((int)0x80000006);
        private const uint FileBegin = 0;
        private const uint MoveFileReplaceExisting = 0x1;
        private const int MaximumPublicationBytes = 4 * 1024 * 1024;
        private const int MaximumStreamInformationBytes = 64 * 1024;
        private static readonly IntPtr InvalidHandle = new IntPtr(-1);

        private readonly List<IntPtr> handles = new List<IntPtr>();
        private readonly string canonicalPath;
        private readonly uint volumeSerial;
        private readonly ulong fileId;
        private bool disposed;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateFile(
            string fileName, uint desiredAccess, uint shareMode,
            IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandle(
            IntPtr handle, out StockRuntimeByHandleFileInformation information);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandleEx(
            IntPtr handle, int informationClass, IntPtr information,
            uint bufferSize);

        [DllImport("ntdll.dll")]
        private static extern int NtQueryInformationFile(
            IntPtr handle, out StockRuntimeIoStatusBlock ioStatus,
            IntPtr information, uint informationBytes,
            int informationClass);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandle(
            IntPtr handle, StringBuilder path, uint pathLength, uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WriteFile(
            IntPtr handle, byte[] buffer, uint bytesToWrite,
            out uint bytesWritten, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ReadFile(
            IntPtr handle, byte[] buffer, uint bytesToRead,
            out uint bytesRead, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FlushFileBuffers(IntPtr handle);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetFilePointerEx(
            IntPtr handle, long distance, out long newPointer, uint moveMethod);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetFileInformationByHandle(
            IntPtr handle, int informationClass,
            IntPtr information, uint bufferSize);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DeleteFile(string fileName);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool MoveFileEx(
            string existingFileName, string newFileName, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFileAttributes(string fileName);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);

        private StockRuntimeDirectoryCapability(string path, string anchorPath)
        {
            canonicalPath = Path.GetFullPath(path).TrimEnd('\\', '/');
            try
            {
                if (String.IsNullOrEmpty(Path.GetPathRoot(canonicalPath)))
                    throw new InvalidOperationException("Directory capability requires an absolute path.");
                string canonicalAnchor = Path.GetFullPath(anchorPath).TrimEnd('\\', '/');
                if (!canonicalAnchor.StartsWith(
                        canonicalPath + Path.DirectorySeparatorChar,
                        StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "Directory capability anchor must be a strict descendant.");
                OpenAndHold(canonicalPath, true);
                // Holding a descendant without FILE_SHARE_DELETE blocks
                // rename/replacement of the root and its ancestors on the
                // supported Windows profile. The self-test exercises both.
                OpenAndHold(canonicalAnchor, false);
                StockRuntimeByHandleFileInformation information = Information(RootHandle);
                volumeSerial = information.VolumeSerialNumber;
                fileId = ((ulong)information.FileIndexHigh << 32) | information.FileIndexLow;
                if (!Revalidate())
                    throw new InvalidOperationException("Directory capability identity changed during acquisition.");
            }
            catch
            {
                Dispose();
                throw;
            }
        }

        public static StockRuntimeDirectoryCapability Open(
            string path, string anchorPath)
        {
            StockRuntimeDirectoryCapability capability = null;
            try
            {
                capability = new StockRuntimeDirectoryCapability(path, anchorPath);
                return capability;
            }
            catch
            {
                if (capability != null) capability.Dispose();
                throw;
            }
        }

        // Windows PowerShell 5.1 does not reliably enumerate directory ADS
        // through Get-Item -Stream. Query FILE_STREAM_INFO through one exact,
        // retained no-follow handle so files and directories share the same
        // fail-closed stream gate on every supported PowerShell host.
        public static void ValidateOnlyDefaultDataStream(string path)
        {
            if (String.IsNullOrWhiteSpace(path))
                throw new InvalidOperationException(
                    "Default-stream validation path is invalid.");
            string canonical = Path.GetFullPath(path).TrimEnd('\\', '/');
            IntPtr handle = CreateFile(
                canonical, GenericRead | FileReadAttributes, FileShareRead,
                IntPtr.Zero, OpenExisting,
                FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Default-stream validation open failed.");
            try
            {
                StockRuntimeByHandleFileInformation before =
                    Information(handle);
                ulong size = ((ulong)before.FileSizeHigh << 32) |
                    before.FileSizeLow;
                if ((before.FileAttributes & FileAttributeReparsePoint) != 0 ||
                    !String.Equals(FinalPath(handle), canonical,
                        StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "Default-stream validation identity is invalid.");
                if ((before.FileAttributes & FileAttributeDirectory) != 0)
                    RequireOnlyDirectoryDataStreams(handle);
                else
                    RequireOnlyDefaultDataStream(handle, size);
                StockRuntimeByHandleFileInformation after =
                    Information(handle);
                if (before.FileAttributes != after.FileAttributes ||
                    before.VolumeSerialNumber != after.VolumeSerialNumber ||
                    before.FileSizeHigh != after.FileSizeHigh ||
                    before.FileSizeLow != after.FileSizeLow ||
                    before.NumberOfLinks != after.NumberOfLinks ||
                    before.FileIndexHigh != after.FileIndexHigh ||
                    before.FileIndexLow != after.FileIndexLow ||
                    before.CreationTime.dwHighDateTime !=
                        after.CreationTime.dwHighDateTime ||
                    before.CreationTime.dwLowDateTime !=
                        after.CreationTime.dwLowDateTime ||
                    before.LastWriteTime.dwHighDateTime !=
                        after.LastWriteTime.dwHighDateTime ||
                    before.LastWriteTime.dwLowDateTime !=
                        after.LastWriteTime.dwLowDateTime ||
                    !String.Equals(FinalPath(handle), canonical,
                        StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "Default-stream validation identity changed.");
                if ((after.FileAttributes & FileAttributeDirectory) != 0)
                    RequireOnlyDirectoryDataStreams(handle);
                else
                    RequireOnlyDefaultDataStream(handle, size);
            }
            finally
            {
                CloseHandle(handle);
            }
        }

        public static void WriteRestorationRootAdsProbe(string directoryPath)
        {
            string canonical = Path.GetFullPath(directoryPath).TrimEnd('\\', '/');
            const string probeName = ":hlclient-restoration-root-ads-probe";
            IntPtr handle = CreateFile(
                canonical + probeName, GenericWrite, FileShareRead,
                IntPtr.Zero, 2, 0x80, IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Restoration root ADS probe creation failed.");
            try
            {
                byte[] bytes = Encoding.ASCII.GetBytes("mutation");
                uint written;
                if (!WriteFile(handle, bytes, (uint)bytes.Length,
                        out written, IntPtr.Zero) || written != bytes.Length ||
                    !FlushFileBuffers(handle))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Restoration root ADS probe write failed.");
            }
            finally
            {
                CloseHandle(handle);
            }
        }

        public static void DeleteRestorationRootAdsProbe(string directoryPath)
        {
            string canonical = Path.GetFullPath(directoryPath).TrimEnd('\\', '/');
            const string probeName = ":hlclient-restoration-root-ads-probe";
            if (!DeleteFile(canonical + probeName))
            {
                int error = Marshal.GetLastWin32Error();
                if (error != 2)
                    throw new Win32Exception(error,
                        "Restoration root ADS probe cleanup failed.");
            }
        }

        // Removes only one exact, empty, lowercase-GUID child directory from
        // an otherwise empty retained parent. Both identities are opened with
        // FILE_FLAG_OPEN_REPARSE_POINT and without FILE_SHARE_DELETE before
        // the child is marked delete-on-close. Any content, alternate stream,
        // reparse identity, sibling or inaccessible inventory fails closed.
        public static void DeleteExactEmptyChildDirectory(
            string parentPath, string childLeaf)
        {
            if (String.IsNullOrWhiteSpace(parentPath) ||
                !ValidLowerHexRunId(childLeaf))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup parameters are invalid.");
            string parent = Path.GetFullPath(parentPath).TrimEnd('\\', '/');
            if (String.IsNullOrEmpty(Path.GetPathRoot(parent)))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup parent must be absolute.");
            DriveInfo drive = new DriveInfo(Path.GetPathRoot(parent));
            if (drive.DriveType != DriveType.Fixed)
                throw new InvalidOperationException(
                    "Exact empty-child cleanup requires a fixed local drive.");
            string child = Path.Combine(parent, childLeaf);
            if (!String.Equals(Path.GetDirectoryName(child), parent,
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup child is not direct.");

            IntPtr parentHandle = InvalidHandle;
            IntPtr childHandle = InvalidHandle;
            try
            {
                parentHandle = CreateFile(
                    parent, GenericRead | FileReadAttributes,
                    FileShareRead | FileShareWrite,
                    IntPtr.Zero, OpenExisting,
                    FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                    IntPtr.Zero);
                if (parentHandle == InvalidHandle)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Exact empty-child cleanup parent open failed.");
                StockRuntimeByHandleFileInformation parentBefore =
                    Information(parentHandle);
                RequireOrdinaryExactDirectory(
                    parentHandle, parent, parentBefore);
                RequireOnlyDirectoryDataStreams(parentHandle);
                RequireExactSingleChild(parent, child);

                childHandle = CreateFile(
                    child,
                    GenericRead | DeleteAccess | FileReadAttributes,
                    FileShareRead | FileShareWrite,
                    IntPtr.Zero, OpenExisting,
                    FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                    IntPtr.Zero);
                if (childHandle == InvalidHandle)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Exact empty-child cleanup child open failed.");
                StockRuntimeByHandleFileInformation childBefore =
                    Information(childHandle);
                RequireOrdinaryExactDirectory(
                    childHandle, child, childBefore);
                RequireOnlyDirectoryDataStreams(childHandle);
                RequireEmptyDirectory(child);

                RequireOrdinaryExactDirectory(
                    parentHandle, parent, parentBefore);
                RequireOnlyDirectoryDataStreams(parentHandle);
                RequireExactSingleChild(parent, child);
                RequireOrdinaryExactDirectory(
                    childHandle, child, childBefore);
                RequireOnlyDirectoryDataStreams(childHandle);
                RequireEmptyDirectory(child);

                if (!MarkDeleteOnClose(childHandle))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Exact empty-child cleanup disposition failed.");
                if (!CloseHandle(childHandle))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Exact empty-child cleanup close failed.");
                childHandle = IntPtr.Zero;
                if (GetFileAttributes(child) != UInt32.MaxValue)
                    throw new InvalidOperationException(
                        "Exact empty-child cleanup target remains present.");
                RequireOrdinaryExactDirectory(
                    parentHandle, parent, parentBefore);
                RequireOnlyDirectoryDataStreams(parentHandle);
                RequireEmptyDirectory(parent);
            }
            finally
            {
                if (childHandle != IntPtr.Zero &&
                    childHandle != InvalidHandle)
                    CloseHandle(childHandle);
                if (parentHandle != IntPtr.Zero &&
                    parentHandle != InvalidHandle)
                    CloseHandle(parentHandle);
            }
        }

        public bool Revalidate()
        {
            if (disposed || handles.Count == 0) return false;
            StockRuntimeByHandleFileInformation information = Information(RootHandle);
            return information.VolumeSerialNumber == volumeSerial &&
                ((((ulong)information.FileIndexHigh << 32) | information.FileIndexLow) == fileId) &&
                String.Equals(FinalPath(RootHandle), canonicalPath,
                    StringComparison.OrdinalIgnoreCase);
        }

        public bool VerifyRootSubstitutionBlocked()
        {
            if (disposed || !Revalidate()) return false;
            string moved = canonicalPath + ".hlclient-root-swap-probe";
            if (GetFileAttributes(moved) != UInt32.MaxValue) return false;
            bool moveBlocked = !MoveFileEx(canonicalPath, moved, 0);
            if (!moveBlocked)
            {
                // Best-effort self-test recovery; production publication never
                // attempts a path rename of its retained root.
                MoveFileEx(moved, canonicalPath, 0);
            }
            return moveBlocked && Revalidate() &&
                GetFileAttributes(moved) == UInt32.MaxValue;
        }

        public string IdentityCategory
        {
            get { return "retained-volume-and-file-id"; }
        }

        public string CanonicalPath
        {
            get { return canonicalPath; }
        }

        // Reads an existing bounded artifact through a single no-share-write/
        // no-share-delete handle. The exact ordinary-file identity is checked
        // before and after the read, so callers can validate and later publish
        // these same bytes without reopening a mutable path by name.
        public byte[] ReadExistingFile(string leafName, int maximumBytes)
        {
            if (disposed || !Revalidate())
                throw new InvalidOperationException(
                    "Read directory capability is no longer valid.");
            if (!ValidLeafName(leafName) || maximumBytes < 1 ||
                maximumBytes > MaximumPublicationBytes)
                throw new InvalidOperationException(
                    "Bounded retained-handle read parameters are invalid.");
            string path = Path.Combine(canonicalPath, leafName);
            IntPtr handle = CreateFile(
                path, GenericRead | FileReadAttributes,
                FileShareRead, IntPtr.Zero, OpenExisting,
                FileFlagOpenReparsePoint, IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Bounded retained-handle read open failed.");
            try
            {
                StockRuntimeByHandleFileInformation information =
                    Information(handle);
                ulong size = ((ulong)information.FileSizeHigh << 32) |
                    information.FileSizeLow;
                if (size == 0 || size > (ulong)maximumBytes)
                    throw new InvalidOperationException(
                        "Bounded retained-handle read size is invalid.");
                RequireOrdinaryExactFile(handle, path, size);
                RequireOnlyDefaultDataStream(handle, size);
                byte[] bytes = new byte[(int)size];
                uint read;
                if (!ReadFile(handle, bytes, (uint)bytes.Length,
                        out read, IntPtr.Zero) || read != (uint)bytes.Length)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Bounded retained-handle read failed.");
                byte[] extra = new byte[1];
                if (!ReadFile(handle, extra, 1, out read, IntPtr.Zero) ||
                    read != 0)
                    throw new InvalidOperationException(
                        "Bounded retained-handle read length changed.");
                RequireOrdinaryExactFile(handle, path, size);
                RequireOnlyDefaultDataStream(handle, size);
                if (!Revalidate())
                    throw new InvalidOperationException(
                        "Read directory identity changed.");
                return bytes;
            }
            finally
            {
                CloseHandle(handle);
            }
        }

        // Publishes a bounded byte string through one retained native handle:
        // CREATE_NEW random temporary, write/flush/read-back, no-replace
        // handle rename, then a second same-handle identity/read-back check.
        // At no point is a closed temporary path trusted or moved by name.
        public void PublishNewFile(string leafName, byte[] bytes)
        {
            PublishNewFiles(
                new string[] { leafName }, new byte[][] { bytes });
        }

        // A final evidence set is a single rollback-capable batch. All
        // temporary handles are prepared first and remain held until every
        // no-replace rename and same-handle verification succeeds. If any
        // member fails, every renamed member is deleted by its retained
        // handle before this method returns and no final leaf remains.
        public void PublishNewFiles(string[] leafNames, byte[][] payloads)
        {
            PublishNewFilesCore(leafNames, payloads, false, false, false);
        }

        // Replaces one fixed metadata leaf only when the exact previously
        // validated bytes are still present. The old file is held without
        // share-write/delete, renamed by handle to a private backup, and the
        // prepared replacement is then renamed no-replace. A substitution at
        // either name fails closed; no mutable pathname is overwritten.
        public void PublishReplacingFile(
            string leafName, byte[] expectedPrevious, byte[] bytes)
        {
            PublishReplacingFileIfExact(
                leafName, expectedPrevious, bytes, false, false);
        }

        public bool VerifyReplacingRollbackPreserved()
        {
            if (disposed || !Revalidate()) return false;
            string leaf = ".hlclient-replacing-rollback-" +
                Guid.NewGuid().ToString("N") + ".json";
            byte[] original = Encoding.ASCII.GetBytes("{\"generation\":1}");
            byte[] replacement = Encoding.ASCII.GetBytes("{\"generation\":2}");
            bool failed = false;
            try
            {
                PublishNewFile(leaf, original);
                try
                {
                    PublishReplacingFileIfExact(
                        leaf, original, replacement, false, true);
                }
                catch
                {
                    failed = true;
                }
                byte[] observed = ReadExistingFile(leaf, 1024);
                bool exact = observed.Length == original.Length;
                for (int index = 0;
                     exact && index < original.Length; ++index)
                    exact = observed[index] == original[index];
                return failed && exact && Revalidate();
            }
            finally
            {
                DeleteFile(Path.Combine(canonicalPath, leaf));
            }
        }

        public bool VerifyReplacingExpectedPriorMismatchBlocked()
        {
            if (disposed || !Revalidate()) return false;
            string leaf = ".hlclient-replacing-mismatch-" +
                Guid.NewGuid().ToString("N") + ".json";
            byte[] original = Encoding.ASCII.GetBytes("{\"generation\":1}");
            byte[] wrong = Encoding.ASCII.GetBytes("{\"generation\":0}");
            byte[] replacement = Encoding.ASCII.GetBytes("{\"generation\":2}");
            bool failed = false;
            try
            {
                PublishNewFile(leaf, original);
                try
                {
                    PublishReplacingFile(leaf, wrong, replacement);
                }
                catch
                {
                    failed = true;
                }
                byte[] observed = ReadExistingFile(leaf, 1024);
                return failed && ExactBytes(observed, original) && Revalidate();
            }
            finally
            {
                DeleteFile(Path.Combine(canonicalPath, leaf));
            }
        }

        public bool VerifyReplacingSubstitutionBlocked()
        {
            if (disposed || !Revalidate()) return false;
            string leaf = ".hlclient-replacing-substitution-" +
                Guid.NewGuid().ToString("N") + ".json";
            byte[] original = Encoding.ASCII.GetBytes("{\"generation\":1}");
            byte[] replacement = Encoding.ASCII.GetBytes("{\"generation\":2}");
            byte[] substitute = Encoding.ASCII.GetBytes("substitute");
            HashSet<string> priorBackups = new HashSet<string>(
                Directory.GetFiles(
                    canonicalPath, ".hlclient-stock-runtime-prior-*.tmp"),
                StringComparer.OrdinalIgnoreCase);
            bool failed = false;
            try
            {
                PublishNewFile(leaf, original);
                try
                {
                    PublishReplacingFileIfExact(
                        leaf, original, replacement, true, false);
                }
                catch
                {
                    failed = true;
                }
                byte[] observed = File.ReadAllBytes(
                    Path.Combine(canonicalPath, leaf));
                return failed && ExactBytes(observed, substitute) &&
                    Revalidate();
            }
            finally
            {
                DeleteFile(Path.Combine(canonicalPath, leaf));
                foreach (string backup in Directory.GetFiles(
                    canonicalPath, ".hlclient-stock-runtime-prior-*.tmp"))
                    if (!priorBackups.Contains(backup)) DeleteFile(backup);
            }
        }

        // Deterministically forces a rollback after the first retained-handle
        // rename, substitutes a new file only after every trusted handle was
        // closed, and proves rollback never path-deletes that replacement.
        // Four and five members exercise the baseline and reconnect shapes.
        public bool VerifyRollbackReplacementPreserved(int memberCount)
        {
            if (disposed || (memberCount != 4 && memberCount != 5))
                return false;
            string nonce = Guid.NewGuid().ToString("N");
            string[] leaves = new string[memberCount];
            byte[][] payloads = new byte[memberCount][];
            for (int index = 0; index < memberCount; ++index)
            {
                leaves[index] = ".hlclient-rollback-selftest-" + nonce +
                    "-" + index.ToString() + ".json";
                payloads[index] = Encoding.UTF8.GetBytes(
                    "{\"member\":" + index.ToString() + "}");
            }
            bool failed = false;
            try
            {
                PublishNewFilesCore(leaves, payloads, true, false, false);
            }
            catch
            {
                failed = true;
            }
            string replacement = Path.Combine(canonicalPath, leaves[0]);
            byte[] expected = RollbackReplacementBytes();
            bool preserved = false;
            try
            {
                byte[] observed = File.ReadAllBytes(replacement);
                preserved = observed.Length == expected.Length;
                for (int index = 0;
                    preserved && index < expected.Length; ++index)
                    preserved = observed[index] == expected[index];
                return failed && preserved && Revalidate();
            }
            finally
            {
                // These names exist only inside the private self-test root.
                // Production rollback never performs this path deletion.
                for (int index = 0; index < leaves.Length; ++index)
                    DeleteFile(Path.Combine(canonicalPath, leaves[index]));
            }
        }

        private static byte[] RollbackReplacementBytes()
        {
            return new byte[] { 0x72, 0x65, 0x70, 0x6c, 0x61, 0x63, 0x65 };
        }

        private void PublishNewFilesCore(
            string[] leafNames, byte[][] payloads,
            bool injectReplacementAfterClose, bool replaceExisting,
            bool injectFailureAfterReplace)
        {
            if (disposed || !Revalidate())
                throw new InvalidOperationException(
                    "Publication directory capability is no longer valid.");
            if (leafNames == null || payloads == null ||
                leafNames.Length == 0 || leafNames.Length > 16 ||
                leafNames.Length != payloads.Length)
                throw new InvalidOperationException(
                    "Publication batch shape is invalid.");

            HashSet<string> uniqueLeaves = new HashSet<string>(
                StringComparer.OrdinalIgnoreCase);
            for (int index = 0; index < leafNames.Length; ++index)
            {
                if (!ValidLeafName(leafNames[index]) ||
                    !uniqueLeaves.Add(leafNames[index]))
                    throw new InvalidOperationException(
                        "Publication requires unique safe bounded leaf names.");
                if (payloads[index] == null || payloads[index].Length == 0 ||
                    payloads[index].Length > MaximumPublicationBytes)
                    throw new InvalidOperationException(
                        "Publication payload is empty or outside its byte bound.");
            }

            string[] destinations = new string[leafNames.Length];
            string[] temporaryPaths = new string[leafNames.Length];
            IntPtr[] temporaries = new IntPtr[leafNames.Length];
            bool[] renamed = new bool[leafNames.Length];
            bool[] deleteMarked = new bool[leafNames.Length];
            for (int index = 0; index < temporaries.Length; ++index)
                temporaries[index] = InvalidHandle;
            bool complete = false;
            bool rollbackComplete = true;
            try
            {
                for (int index = 0; index < leafNames.Length; ++index)
                {
                    destinations[index] = Path.Combine(
                        canonicalPath, leafNames[index]);
                    temporaries[index] = CreateTemporaryFile(
                        out temporaryPaths[index]);
                    RequireOrdinaryExactFile(
                        temporaries[index], temporaryPaths[index], 0);

                    uint written;
                    byte[] bytes = payloads[index];
                    if (!WriteFile(
                            temporaries[index], bytes, (uint)bytes.Length,
                            out written, IntPtr.Zero) ||
                        written != (uint)bytes.Length)
                        throw new Win32Exception(Marshal.GetLastWin32Error(),
                            "Atomic publication write failed.");
                    if (!FlushFileBuffers(temporaries[index]))
                        throw new Win32Exception(Marshal.GetLastWin32Error(),
                            "Atomic publication flush failed.");
                    RequireOrdinaryExactFile(
                        temporaries[index], temporaryPaths[index],
                        (ulong)bytes.Length);
                    RequireExactBytes(temporaries[index], bytes);
                }

                for (int index = 0; index < leafNames.Length; ++index)
                {
                    RenameOpenFile(
                        temporaries[index], destinations[index],
                        replaceExisting);
                    renamed[index] = true;
                    RequireOrdinaryExactFile(
                        temporaries[index], destinations[index],
                        (ulong)payloads[index].Length);
                    RequireExactBytes(temporaries[index], payloads[index]);
                    if (injectReplacementAfterClose && index == 0)
                        throw new IOException(
                            "Forced retained-handle rollback self-test.");
                    if (injectFailureAfterReplace && index == 0)
                        throw new IOException(
                            "Forced replacing-publication rollback self-test.");
                }
                if (!Revalidate())
                    throw new InvalidOperationException(
                        "Publication directory identity changed after batch rename.");
                complete = true;
            }
            finally
            {
                if (!complete)
                {
                    for (int index = 0; index < temporaries.Length; ++index)
                    {
                        if (temporaries[index] != IntPtr.Zero &&
                            temporaries[index] != InvalidHandle)
                        {
                            deleteMarked[index] =
                                MarkDeleteOnClose(temporaries[index]);
                            if (!deleteMarked[index]) rollbackComplete = false;
                        }
                    }
                }
                for (int index = 0; index < temporaries.Length; ++index)
                {
                    if (temporaries[index] != IntPtr.Zero &&
                        temporaries[index] != InvalidHandle)
                        CloseHandle(temporaries[index]);
                }
                if (!complete && injectReplacementAfterClose &&
                    renamed.Length != 0 && renamed[0])
                {
                    // The trusted file was already dispositioned and its
                    // handle closed. This new object deliberately reuses only
                    // the pathname and must never be deleted by rollback.
                    File.WriteAllBytes(
                        destinations[0], RollbackReplacementBytes());
                }
                if (!complete)
                {
                    for (int index = 0; index < temporaries.Length; ++index)
                    {
                        string cleanupPath = renamed[index]
                            ? destinations[index] : temporaryPaths[index];
                        // Absence is only an observation. Never delete by path
                        // after the retained handle closes: that name may now
                        // denote an unrelated replacement object.
                        if (!String.IsNullOrEmpty(cleanupPath) &&
                            GetFileAttributes(cleanupPath) != UInt32.MaxValue)
                            rollbackComplete = false;
                    }
                }
                if (!complete && !rollbackComplete)
                    throw new IOException(
                        "Atomic publication batch rollback was incomplete.");
            }
        }

        private void PublishReplacingFileIfExact(
            string leafName, byte[] expectedPrevious, byte[] bytes,
            bool injectSubstitutionBeforeReplace,
            bool injectFailureAfterReplace)
        {
            if (disposed || !Revalidate())
                throw new InvalidOperationException(
                    "Replacing publication directory capability is invalid.");
            if (!ValidLeafName(leafName) || expectedPrevious == null ||
                expectedPrevious.Length == 0 ||
                expectedPrevious.Length > MaximumPublicationBytes ||
                bytes == null || bytes.Length == 0 ||
                bytes.Length > MaximumPublicationBytes)
                throw new InvalidOperationException(
                    "Replacing publication parameters are invalid.");

            string destination = Path.Combine(canonicalPath, leafName);
            string temporaryPath = null;
            string backupPath = null;
            IntPtr previous = InvalidHandle;
            IntPtr replacement = InvalidHandle;
            bool previousMoved = false;
            bool complete = false;
            bool rollbackComplete = true;
            Exception failure = null;
            try
            {
                previous = CreateFile(
                    destination, GenericRead | DeleteAccess | FileReadAttributes,
                    FileShareRead, IntPtr.Zero, OpenExisting,
                    FileFlagOpenReparsePoint, IntPtr.Zero);
                if (previous == InvalidHandle)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Expected prior publication is absent or busy.");
                RequireOrdinaryExactFile(
                    previous, destination, (ulong)expectedPrevious.Length);
                RequireExactBytes(previous, expectedPrevious);
                RequireOrdinaryExactFile(
                    previous, destination, (ulong)expectedPrevious.Length);

                replacement = CreateTemporaryFile(out temporaryPath);
                RequireOrdinaryExactFile(replacement, temporaryPath, 0);
                uint written;
                if (!WriteFile(replacement, bytes, (uint)bytes.Length,
                        out written, IntPtr.Zero) ||
                    written != (uint)bytes.Length)
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Replacing publication write failed.");
                if (!FlushFileBuffers(replacement))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Replacing publication flush failed.");
                RequireOrdinaryExactFile(
                    replacement, temporaryPath, (ulong)bytes.Length);
                RequireExactBytes(replacement, bytes);

                backupPath = Path.Combine(
                    canonicalPath, ".hlclient-stock-runtime-prior-" +
                    Guid.NewGuid().ToString("N") + ".tmp");
                RenameOpenFile(previous, backupPath, false);
                previousMoved = true;
                RequireOrdinaryExactFile(
                    previous, backupPath, (ulong)expectedPrevious.Length);
                RequireExactBytes(previous, expectedPrevious);

                if (injectSubstitutionBeforeReplace)
                    File.WriteAllBytes(destination,
                        Encoding.ASCII.GetBytes("substitute"));
                RenameOpenFile(replacement, destination, false);
                RequireOrdinaryExactFile(
                    replacement, destination, (ulong)bytes.Length);
                RequireExactBytes(replacement, bytes);
                if (injectFailureAfterReplace)
                    throw new IOException(
                        "Forced exact-prior replacement rollback self-test.");
                if (!Revalidate())
                    throw new InvalidOperationException(
                        "Replacing publication directory identity changed.");

                if (!MarkDeleteOnClose(previous))
                    throw new IOException(
                        "Prior publication could not be dispositioned.");
                if (!CloseHandle(previous))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Prior publication handle close failed.");
                previous = InvalidHandle;
                complete = true;
            }
            catch (Exception error)
            {
                failure = error;
            }
            finally
            {
                if (!complete)
                {
                    if (replacement != IntPtr.Zero &&
                        replacement != InvalidHandle)
                    {
                        if (!MarkDeleteOnClose(replacement))
                            rollbackComplete = false;
                        if (!CloseHandle(replacement))
                            rollbackComplete = false;
                        replacement = InvalidHandle;
                    }
                    if (previousMoved && previous != IntPtr.Zero &&
                        previous != InvalidHandle)
                    {
                        try
                        {
                            RenameOpenFile(previous, destination, false);
                            RequireOrdinaryExactFile(
                                previous, destination,
                                (ulong)expectedPrevious.Length);
                            RequireExactBytes(previous, expectedPrevious);
                        }
                        catch
                        {
                            rollbackComplete = false;
                        }
                    }
                }
                if (replacement != IntPtr.Zero &&
                    replacement != InvalidHandle)
                    CloseHandle(replacement);
                if (previous != IntPtr.Zero && previous != InvalidHandle)
                    CloseHandle(previous);
            }
            if (failure != null)
            {
                if (!rollbackComplete)
                    throw new IOException(
                        "Exact-prior replacing publication rollback was incomplete.",
                        failure);
                throw failure;
            }
        }

        // Exercises the same no-share-delete temporary-handle primitive used
        // by both final restoration and run-manifest publication. The test
        // succeeds only when delete and replacement-by-name are both blocked
        // while the trusted file handle remains open.
        public bool VerifyTemporarySubstitutionBlocked()
        {
            if (disposed || !Revalidate()) return false;
            string temporaryPath = null;
            string substitutePath = null;
            IntPtr temporary = InvalidHandle;
            try
            {
                temporary = CreateTemporaryFile(out temporaryPath);
                byte[] original = new byte[] { 0x31, 0x32, 0x33, 0x34 };
                uint written;
                if (!WriteFile(temporary, original, (uint)original.Length,
                        out written, IntPtr.Zero) ||
                    written != (uint)original.Length ||
                    !FlushFileBuffers(temporary))
                    return false;
                RequireOrdinaryExactFile(
                    temporary, temporaryPath, (ulong)original.Length);
                RequireExactBytes(temporary, original);

                substitutePath = temporaryPath + ".replacement";
                File.WriteAllBytes(substitutePath,
                    new byte[] { 0x61, 0x62, 0x63, 0x64 });
                bool deleteBlocked = !DeleteFile(temporaryPath);
                bool replacementBlocked = !MoveFileEx(
                    substitutePath, temporaryPath, MoveFileReplaceExisting);
                RequireOrdinaryExactFile(
                    temporary, temporaryPath, (ulong)original.Length);
                RequireExactBytes(temporary, original);
                return deleteBlocked && replacementBlocked && Revalidate();
            }
            catch
            {
                return false;
            }
            finally
            {
                if (temporary != IntPtr.Zero && temporary != InvalidHandle)
                {
                    MarkDeleteOnClose(temporary);
                    CloseHandle(temporary);
                }
                if (!String.IsNullOrEmpty(temporaryPath))
                    DeleteFile(temporaryPath);
                if (!String.IsNullOrEmpty(substitutePath))
                    DeleteFile(substitutePath);
            }
        }

        public void Dispose()
        {
            if (disposed) return;
            for (int index = handles.Count - 1; index >= 0; --index)
            {
                if (handles[index] != IntPtr.Zero && handles[index] != InvalidHandle)
                    CloseHandle(handles[index]);
            }
            handles.Clear();
            disposed = true;
        }

        private IntPtr RootHandle
        {
            get { return handles[0]; }
        }

        private void OpenAndHold(string path, bool requireDirectory)
        {
            IntPtr handle = CreateFile(
                path, FileReadAttributes, FileShareRead | FileShareWrite,
                IntPtr.Zero, OpenExisting,
                FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                IntPtr.Zero);
            if (handle == InvalidHandle)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Could not retain directory identity for " + path + ".");
            try
            {
                StockRuntimeByHandleFileInformation information = Information(handle);
                if ((requireDirectory &&
                        (information.FileAttributes & FileAttributeDirectory) == 0) ||
                    (information.FileAttributes & FileAttributeReparsePoint) != 0 ||
                    !String.Equals(FinalPath(handle),
                        Path.GetFullPath(path).TrimEnd('\\', '/'),
                        StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException(
                        "Directory capability encountered a reparse or path mismatch.");
                handles.Add(handle);
                handle = IntPtr.Zero;
            }
            finally
            {
                if (handle != IntPtr.Zero && handle != InvalidHandle)
                    CloseHandle(handle);
            }
        }

        private IntPtr CreateTemporaryFile(out string temporaryPath)
        {
            byte[] random = new byte[16];
            for (int attempt = 0; attempt < 16; ++attempt)
            {
                using (RandomNumberGenerator generator =
                        RandomNumberGenerator.Create())
                    generator.GetBytes(random);
                StringBuilder name = new StringBuilder(
                    ".hlclient-stock-runtime-");
                for (int index = 0; index < random.Length; ++index)
                    name.Append(random[index].ToString("x2"));
                name.Append(".tmp");
                temporaryPath = Path.Combine(canonicalPath, name.ToString());
                IntPtr handle = CreateFile(
                    temporaryPath,
                    GenericRead | GenericWrite | DeleteAccess |
                        FileReadAttributes,
                    FileShareRead, IntPtr.Zero, CreateNew,
                    FileAttributeTemporary | FileFlagOpenReparsePoint |
                        FileFlagWriteThrough,
                    IntPtr.Zero);
                if (handle != InvalidHandle) return handle;
                int error = Marshal.GetLastWin32Error();
                if (error != 80 && error != 183)
                    throw new Win32Exception(error,
                        "Atomic publication temporary creation failed.");
            }
            temporaryPath = null;
            throw new IOException(
                "Atomic publication exhausted random temporary names.");
        }

        private static bool ValidLeafName(string leafName)
        {
            if (String.IsNullOrEmpty(leafName) || leafName.Length > 160 ||
                leafName == "." || leafName == ".." ||
                !String.Equals(Path.GetFileName(leafName), leafName,
                    StringComparison.Ordinal) ||
                leafName.EndsWith(" ", StringComparison.Ordinal) ||
                leafName.EndsWith(".", StringComparison.Ordinal))
                return false;
            foreach (char value in leafName)
            {
                if (value < 0x20 || value == '/' || value == '\\' ||
                    value == ':' || value == '*' || value == '?' ||
                    value == '"' || value == '<' || value == '>' ||
                    value == '|')
                    return false;
            }
            return true;
        }

        private static bool ValidLowerHexRunId(string value)
        {
            if (String.IsNullOrEmpty(value) || value.Length != 32)
                return false;
            for (int index = 0; index < value.Length; ++index)
            {
                char character = value[index];
                if (!((character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f')))
                    return false;
            }
            return true;
        }

        private static void RequireOrdinaryExactDirectory(
            IntPtr handle, string expectedPath,
            StockRuntimeByHandleFileInformation expectedInformation)
        {
            StockRuntimeByHandleFileInformation observed =
                Information(handle);
            if ((observed.FileAttributes & FileAttributeDirectory) == 0 ||
                (observed.FileAttributes & FileAttributeReparsePoint) != 0 ||
                observed.VolumeSerialNumber !=
                    expectedInformation.VolumeSerialNumber ||
                observed.FileIndexHigh != expectedInformation.FileIndexHigh ||
                observed.FileIndexLow != expectedInformation.FileIndexLow ||
                !String.Equals(FinalPath(handle),
                    Path.GetFullPath(expectedPath).TrimEnd('\\', '/'),
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup directory identity is invalid.");
        }

        private static void RequireExactSingleChild(
            string parent, string expectedChild)
        {
            string[] entries = Directory.GetFileSystemEntries(parent);
            if (entries.Length != 1 ||
                !String.Equals(Path.GetFullPath(entries[0]), expectedChild,
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "Exact empty-child cleanup parent inventory is ambiguous.");
        }

        private static void RequireEmptyDirectory(string path)
        {
            if (Directory.GetFileSystemEntries(path).Length != 0)
                throw new InvalidOperationException(
                    "Exact empty-child cleanup refuses nonempty content.");
        }

        private static void RequireOrdinaryExactFile(
            IntPtr handle, string expectedPath, ulong expectedSize)
        {
            StockRuntimeByHandleFileInformation information =
                Information(handle);
            ulong size = ((ulong)information.FileSizeHigh << 32) |
                information.FileSizeLow;
            if ((information.FileAttributes &
                    (FileAttributeDirectory | FileAttributeReparsePoint)) != 0 ||
                information.NumberOfLinks != 1 || size != expectedSize ||
                !String.Equals(FinalPath(handle),
                    Path.GetFullPath(expectedPath).TrimEnd('\\', '/'),
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "Atomic publication file identity is invalid.");
        }

        // FILE_STREAM_INFO is queried through the same retained file handle as
        // the byte read. A fixed upper bound makes an adversarial stream list a
        // typed failure instead of an allocation request. The only accepted
        // entry is the unnamed NTFS data stream for the exact primary length.
        private static void RequireOnlyDefaultDataStream(
            IntPtr handle, ulong expectedSize)
        {
            IntPtr buffer = Marshal.AllocHGlobal(MaximumStreamInformationBytes);
            try
            {
                if (!GetFileInformationByHandleEx(
                        handle, FileStreamInfo, buffer,
                        MaximumStreamInformationBytes))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Bounded retained-handle stream inventory failed.");

                int offset = 0;
                int count = 0;
                while (true)
                {
                    if (offset < 0 ||
                        offset > MaximumStreamInformationBytes - 24)
                        throw new InvalidOperationException(
                            "Bounded retained-handle stream inventory is invalid.");
                    uint next = unchecked((uint)Marshal.ReadInt32(buffer, offset));
                    uint nameBytes = unchecked((uint)Marshal.ReadInt32(
                        buffer, offset + 4));
                    long streamSize = Marshal.ReadInt64(buffer, offset + 8);
                    if (nameBytes == 0 || (nameBytes & 1U) != 0U ||
                        nameBytes > (uint)(MaximumStreamInformationBytes -
                            offset - 24))
                        throw new InvalidOperationException(
                            "Bounded retained-handle stream inventory is invalid.");
                    string name = Marshal.PtrToStringUni(
                        IntPtr.Add(buffer, offset + 24),
                        checked((int)(nameBytes / 2U)));
                    ++count;
                    if (count != 1 ||
                        !String.Equals(name, "::$DATA",
                            StringComparison.Ordinal) ||
                        streamSize < 0 || (ulong)streamSize != expectedSize)
                        throw new InvalidOperationException(
                            "Bounded retained-handle read requires only the default data stream.");
                    if (next == 0U) break;
                    if (next < 24U + nameBytes ||
                        next > (uint)(MaximumStreamInformationBytes - offset))
                        throw new InvalidOperationException(
                            "Bounded retained-handle stream inventory is invalid.");
                    offset = checked(offset + (int)next);
                }
                if (count != 1)
                    throw new InvalidOperationException(
                        "Bounded retained-handle read requires only the default data stream.");
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }

        private static void RequireOnlyDirectoryDataStreams(IntPtr handle)
        {
            IntPtr buffer = Marshal.AllocHGlobal(MaximumStreamInformationBytes);
            try
            {
                StockRuntimeIoStatusBlock ioStatus;
                int status = NtQueryInformationFile(
                    handle, out ioStatus, buffer,
                    MaximumStreamInformationBytes,
                    NtFileStreamInformation);
                if (status == StatusNoMoreFiles) return;
                if (status != 0)
                    throw new InvalidOperationException(
                        "Bounded retained-directory stream inventory failed.");
                ulong used = ioStatus.Information.ToUInt64();
                if (used > MaximumStreamInformationBytes)
                    throw new InvalidOperationException(
                        "Bounded retained-directory stream inventory is invalid.");
                int offset = 0;
                int count = 0;
                while ((ulong)offset < used)
                {
                    if (offset < 0 ||
                        offset > MaximumStreamInformationBytes - 24)
                        throw new InvalidOperationException(
                            "Bounded retained-directory stream inventory is invalid.");
                    uint next = unchecked((uint)Marshal.ReadInt32(
                        buffer, offset));
                    uint nameBytes = unchecked((uint)Marshal.ReadInt32(
                        buffer, offset + 4));
                    if (nameBytes == 0 || (nameBytes & 1U) != 0U ||
                        nameBytes > (uint)(MaximumStreamInformationBytes -
                            offset - 24) ||
                        (ulong)offset + 24U + nameBytes > used)
                        throw new InvalidOperationException(
                            "Bounded retained-directory stream inventory is invalid.");
                    string name = Marshal.PtrToStringUni(
                        IntPtr.Add(buffer, offset + 24),
                        checked((int)(nameBytes / 2U)));
                    ++count;
                    if (count > 128 ||
                        (!String.Equals(name, "::$DATA",
                             StringComparison.Ordinal) &&
                         !String.Equals(name, "::$INDEX_ALLOCATION",
                             StringComparison.Ordinal)))
                        throw new InvalidOperationException(
                            "Bounded retained-handle read requires only the default data stream.");
                    if (next == 0U) break;
                    if (next < 24U + nameBytes || (next & 7U) != 0U ||
                        (ulong)offset + next >= used)
                        throw new InvalidOperationException(
                            "Bounded retained-directory stream inventory is invalid.");
                    offset = checked(offset + (int)next);
                }
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }

        private static bool ExactBytes(byte[] observed, byte[] expected)
        {
            if (observed == null || expected == null ||
                observed.Length != expected.Length) return false;
            for (int index = 0; index < expected.Length; ++index)
                if (observed[index] != expected[index]) return false;
            return true;
        }

        private static void RequireExactBytes(IntPtr handle, byte[] expected)
        {
            long position;
            if (!SetFilePointerEx(handle, 0, out position, FileBegin) ||
                position != 0)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Atomic publication seek failed.");
            byte[] observed = new byte[expected.Length];
            uint read;
            if (!ReadFile(handle, observed, (uint)observed.Length,
                    out read, IntPtr.Zero) ||
                read != (uint)observed.Length)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Atomic publication read-back failed.");
            for (int index = 0; index < expected.Length; ++index)
            {
                if (observed[index] != expected[index])
                    throw new InvalidOperationException(
                        "Atomic publication read-back bytes differ.");
            }
            byte[] extra = new byte[1];
            if (!ReadFile(handle, extra, 1, out read, IntPtr.Zero) || read != 0)
                throw new InvalidOperationException(
                    "Atomic publication read-back length differs.");
        }

        private static void RenameOpenFile(
            IntPtr handle, string destination, bool replaceExisting)
        {
            byte[] nameBytes = Encoding.Unicode.GetBytes(destination);
            int rootOffset = IntPtr.Size == 8 ? 8 : 4;
            int lengthOffset = rootOffset + IntPtr.Size;
            int fileNameOffset = lengthOffset + 4;
            int headerSize = fileNameOffset + 4;
            IntPtr information = Marshal.AllocHGlobal(
                headerSize + nameBytes.Length);
            try
            {
                for (int index = 0;
                     index < headerSize + nameBytes.Length; ++index)
                    Marshal.WriteByte(information, index, 0);
                Marshal.WriteByte(
                    information, 0, replaceExisting ? (byte)1 : (byte)0);
                Marshal.WriteIntPtr(information, rootOffset, IntPtr.Zero);
                Marshal.WriteInt32(
                    information, lengthOffset, nameBytes.Length);
                Marshal.Copy(
                    nameBytes, 0, IntPtr.Add(information, fileNameOffset),
                    nameBytes.Length);
                if (!SetFileInformationByHandle(
                        handle, FileRenameInfo, information,
                        (uint)(headerSize + nameBytes.Length)))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "Atomic no-replace publication rename failed.");
            }
            finally
            {
                Marshal.FreeHGlobal(information);
            }
        }

        private static bool MarkDeleteOnClose(IntPtr handle)
        {
            IntPtr disposition = Marshal.AllocHGlobal(4);
            try
            {
                Marshal.WriteInt32(disposition, 1);
                return SetFileInformationByHandle(
                    handle, FileDispositionInfo, disposition, 4);
            }
            finally
            {
                Marshal.FreeHGlobal(disposition);
            }
        }

        private static StockRuntimeByHandleFileInformation Information(IntPtr handle)
        {
            StockRuntimeByHandleFileInformation information;
            if (!GetFileInformationByHandle(handle, out information))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Directory identity query failed.");
            return information;
        }

        private static string FinalPath(IntPtr handle)
        {
            StringBuilder buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandle(
                handle, buffer, (uint)buffer.Capacity,
                FileNameNormalized | VolumeNameDos);
            if (length == 0 || length >= buffer.Capacity)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Directory final-path query failed.");
            string value = buffer.ToString();
            const string extendedPrefix = @"\\?\";
            if (value.StartsWith(extendedPrefix, StringComparison.Ordinal))
                value = value.Substring(extendedPrefix.Length);
            return value.TrimEnd('\\', '/');
        }
    }
}
'@
}

function New-RetainedDirectoryCapability {
    param([string]$Path, [string]$AnchorPath, [string]$Label)
    Initialize-RestorationDirectoryCapabilityNative
    try {
        return [Hlclient.StockRuntimeDirectoryCapability]::Open(
            $Path, $AnchorPath)
    } catch {
        throw "$Label retained directory identity could not be acquired: $($_.Exception.Message)"
    }
}

function Assert-RestorationDirectoryCapabilities {
    param([object]$Guard)
    foreach ($entry in @(
            @{ Capability = $Guard.ResearchDirectoryCapability; Label = 'research root' },
            @{ Capability = $Guard.BackupRootDirectoryCapability; Label = 'backup root' },
            @{ Capability = $Guard.BackupDataDirectoryCapability; Label = 'backup data root' })) {
        if ($null -eq $entry.Capability -or -not $entry.Capability.Revalidate()) {
            throw "Restoration $($entry.Label) retained identity changed."
        }
    }
}

function Close-RestorationBackupCapabilities {
    param([object]$Guard)
    foreach ($name in @('BackupDataDirectoryCapability',
            'BackupRootDirectoryCapability')) {
        if ($null -ne $Guard -and $null -ne $Guard.$name) {
            $Guard.$name.Dispose()
            $Guard.$name = $null
        }
    }
}

function Close-RestorationGuardCapabilities {
    param([object]$Guard)
    if ($null -eq $Guard) { return }
    Close-RestorationBackupCapabilities $Guard
    if ($null -ne $Guard.ResearchDirectoryCapability) {
        $Guard.ResearchDirectoryCapability.Dispose()
        $Guard.ResearchDirectoryCapability = $null
    }
}

function New-RunDirectoryCapability {
    param([string]$RunRoot)
    foreach ($leaf in @('raw', 'logs', 'version-observation.staged.json',
            'isolation-attestation.staged.json',
            'server-profile-diagnostic.staged.json',
            'functional-smoke.staged.json')) {
        $anchor = Join-Path $RunRoot $leaf
        if (Test-Path -LiteralPath $anchor) {
            Assert-NoReparsePointInExistingPath $anchor 'capture run identity anchor'
            return New-RetainedDirectoryCapability `
                -Path $RunRoot -AnchorPath $anchor -Label 'capture run root'
        }
    }
    throw 'Capture run has no approved identity anchor.'
}

function Assert-RunDirectoryCapability {
    param([object]$Capability, [string]$RunRoot)
    if ($null -eq $Capability -or -not $Capability.Revalidate() -or
        -not $Capability.CanonicalPath.Equals(
            [IO.Path]::GetFullPath($RunRoot).TrimEnd('\', '/'),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Capture run retained directory identity changed.'
    }
}

function New-RestorationGuard {
    param([string]$Root, [object]$Snapshot)
    $researchCapability = $null
    $backupRootCapability = $null
    $backupDataCapability = $null
    $guard = $null
    $temporary = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) (
        'hlclient-stock-runtime-restore-' + [Guid]::NewGuid().ToString('N'))))
    try {
        if ((Test-PathAtOrBelow $temporary $Root) -or
            (Test-PathAtOrBelow $temporary $repositoryRoot)) {
            throw 'Restoration backup root is not disjoint.'
        }
        [Int64]$headroom = 67108864
        if ([Int64]$Snapshot.TotalBytes -gt ([Int64]::MaxValue - $headroom)) {
            throw 'Restoration backup size overflowed.'
        }
        $temporaryDrive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($temporary))
        if (-not $temporaryDrive.IsReady -or
            $temporaryDrive.AvailableFreeSpace -lt ([Int64]$Snapshot.TotalBytes + $headroom)) {
            throw 'Temporary storage lacks space for a full transactional research backup.'
        }
        $researchAnchor = Join-Path $Root $markerName
        if (-not (Test-Path -LiteralPath $researchAnchor -PathType Leaf)) {
            throw 'Restoration research identity anchor is absent.'
        }
        $researchCapability = New-RetainedDirectoryCapability `
            -Path $Root -AnchorPath $researchAnchor -Label 'research root'
        Assert-NoReparsePointInExistingPath $temporary 'restoration backup path'
        [IO.Directory]::CreateDirectory($temporary) | Out-Null
        Assert-NoReparsePointInExistingPath $temporary 'restoration backup path'
        Assert-NoReparsePoint $temporary 'restoration backup root'
        $data = Join-Path $temporary 'data'
        [IO.Directory]::CreateDirectory($data) | Out-Null
        Assert-NoReparsePoint $data 'restoration backup data root'
        $backupIdentityLock = Join-Path $data '.hlclient-restoration-identity-lock'
        [IO.Directory]::CreateDirectory($backupIdentityLock) | Out-Null
        $backupRootCapability = New-RetainedDirectoryCapability `
            -Path $temporary -AnchorPath $data `
            -Label 'restoration backup root'
        $backupDataCapability = New-RetainedDirectoryCapability `
            -Path $data -AnchorPath $backupIdentityLock `
            -Label 'restoration backup data root'
        $backed = [Collections.Generic.List[object]]::new()
        $guard = [pscustomobject]@{
            Root = $Root; TemporaryRoot = $temporary; DataRoot = $data
            Before = $Snapshot; BackedEntries = @()
            ResearchDirectoryCapability = $researchCapability
            BackupRootDirectoryCapability = $backupRootCapability
            BackupDataDirectoryCapability = $backupDataCapability
        }
        # Back up the complete bounded tree. A whitelist-only backup can detect
        # drift outside known mutable paths but cannot restore it transactionally.
        foreach ($entry in @($Snapshot.Entries | Where-Object {
                    $_.RelativePath -ne '.'
                } | Sort-Object RelativePath)) {
            $source = Join-Path $Root $entry.RelativePath.Replace('/', '\')
            $destination = Join-Path $data $entry.RelativePath.Replace('/', '\')
            Assert-RestorationDirectoryCapabilities $guard
            Assert-PathBelowRoot $source $Root 'restoration source'
            Assert-PathBelowRoot $destination $data 'restoration destination'
            if ($entry.Kind -eq 'directory') {
                [IO.Directory]::CreateDirectory($destination) | Out-Null
            } else {
                [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
                [IO.File]::Copy($source, $destination, $false)
                if ((Get-FileSha256 $destination) -cne $entry.Sha256) {
                    throw 'Restoration backup digest mismatch.'
                }
            }
            [void]$backed.Add($entry)
        }
        Assert-RestorationDirectoryCapabilities $guard
        $guard.BackedEntries = @($backed)
        return $guard
    } catch {
        if ($null -ne $guard) {
            Close-RestorationGuardCapabilities $guard
        } else {
            foreach ($capability in @($backupDataCapability,
                    $backupRootCapability, $researchCapability)) {
                if ($null -ne $capability) { $capability.Dispose() }
            }
        }
        throw "Restoration backup failed; inspect '$temporary': $($_.Exception.Message)"
    }
}

function Remove-SafeEntry {
    param([string]$Path, [string]$Root)
    Assert-PathBelowRoot $Path $Root 'restoration removal target'
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return }
    $isDirectory = ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0
    if ($isDirectory -and
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 -and
        @($item.GetFileSystemInfos()).Count -ne 0) {
        throw 'Refusing to remove a non-empty restoration directory.'
    }
    $isReparse = ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
    if ($isReparse) {
        if ($isDirectory) { [IO.Directory]::Delete($Path, $false) }
        else { [IO.File]::Delete($Path) }
        return
    }
    if (-not $isDirectory) {
        $linkProperty = $item.PSObject.Properties['LinkType']
        if ($null -eq $linkProperty) {
            throw 'Restoration removal could not establish file link state.'
        }
        if ([string]::IsNullOrEmpty([string]$linkProperty.Value)) {
            $item.Attributes = [IO.FileAttributes]::Normal
        }
        # File.Delete removes one directory entry. In particular, it does not
        # open and rewrite a hostile hard-link target before unlinking it.
        [IO.File]::Delete($Path)
        return
    }
    $item.Attributes = [IO.FileAttributes]::Normal
    Remove-Item -LiteralPath $Path -Force
}

function Remove-SafeTree {
    param([string]$Path, [string]$Root)
    Assert-PathBelowRoot $Path $Root 'restoration tree target'
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return }
    if ((($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) -and
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
        foreach ($descendant in @(Get-CurrentItemsNoTraversalThroughLinks $Path |
                Sort-Object { $_.FullName.Split([IO.Path]::DirectorySeparatorChar).Count } `
                    -Descending)) {
            Remove-SafeEntry $descendant.FullName $Root
        }
    }
    Remove-SafeEntry $Path $Root
}

function Get-CurrentItemsNoTraversalThroughLinks {
    param([string]$Root)
    $items = [Collections.Generic.List[object]]::new()
    $queue = [Collections.Generic.Queue[IO.DirectoryInfo]]::new()
    $queue.Enqueue([IO.DirectoryInfo](Get-Item -LiteralPath $Root -Force))
    while ($queue.Count -ne 0) {
        foreach ($item in @($queue.Dequeue().GetFileSystemInfos())) {
            if ($items.Count -ge $maximumEntries) { throw 'Restoration enumeration bound exceeded.' }
            [void]$items.Add($item)
            if ((($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) -and
                ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
                $queue.Enqueue([IO.DirectoryInfo]$item)
            }
        }
    }
    return @($items)
}

function Restore-ResearchState {
    param([object]$Guard)
    Assert-RestorationDirectoryCapabilities $Guard
    $initial = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $initialKinds = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $Guard.Before.Entries) {
        [void]$initial.Add($entry.RelativePath)
        $initialKinds.Add($entry.RelativePath, $entry.Kind)
    }

    # Never traverse a replaced research root or one reached through a replaced
    # ancestor. Revalidate the backup chain before using any retained byte.
    Assert-NoReparsePointInExistingPath $Guard.Root 'research restoration root'
    Assert-PathBelowRoot $Guard.DataRoot $Guard.TemporaryRoot `
        'restoration backup data root'
    Assert-NoReparsePointInExistingPath $Guard.TemporaryRoot `
        'restoration backup root'
    Assert-NoReparsePointInExistingPath $Guard.DataRoot `
        'restoration backup data root'

    # Remove new entries deepest-first, without traversing any new reparse point.
    foreach ($item in @(Get-CurrentItemsNoTraversalThroughLinks $Guard.Root | Sort-Object {
                (Get-RelativePath $_.FullName $Guard.Root).Split('/').Count
            } -Descending)) {
        $relative = Get-RelativePath $item.FullName $Guard.Root
        if (-not $initial.Contains($relative)) {
            Assert-RestorationDirectoryCapabilities $Guard
            Remove-SafeEntry $item.FullName $Guard.Root
        }
    }

    # Use one no-follow inventory and remove hostile original-path reparses or
    # type conflicts shallowest-first. Descendants of a reparse point were not
    # enumerated, so no descendant operation can escape through that link.
    foreach ($item in @(Get-CurrentItemsNoTraversalThroughLinks $Guard.Root |
            Sort-Object { (Get-RelativePath $_.FullName $Guard.Root).Split('/').Count })) {
        $relative = Get-RelativePath $item.FullName $Guard.Root
        if ($initial.Contains($relative)) {
            $isReparse = ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
            $actualKind = if (-not $isReparse -and
                ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                'directory'
            } else { 'file' }
            if ($isReparse -or $actualKind -cne $initialKinds[$relative]) {
                Assert-RestorationDirectoryCapabilities $Guard
                Remove-SafeTree $item.FullName $Guard.Root
            }
        }
    }
    foreach ($entry in @($Guard.BackedEntries | Where-Object Kind -eq 'directory' |
            Sort-Object { $_.RelativePath.Split('/').Count })) {
        $target = Join-Path $Guard.Root $entry.RelativePath.Replace('/', '\')
        $parent = Split-Path -Parent $target
        Assert-RestorationDirectoryCapabilities $Guard
        Assert-NoReparsePointInExistingPath $parent 'restoration directory parent'
        [IO.Directory]::CreateDirectory($target) | Out-Null
        Assert-NoReparsePointInExistingPath $target 'restoration directory'
    }
    foreach ($entry in @($Guard.BackedEntries | Where-Object Kind -eq 'file' | Sort-Object RelativePath)) {
        $source = Join-Path $Guard.DataRoot $entry.RelativePath.Replace('/', '\')
        $target = Join-Path $Guard.Root $entry.RelativePath.Replace('/', '\')
        Assert-PathBelowRoot $source $Guard.DataRoot 'restoration backup file'
        Assert-NoReparsePointInExistingPath $source 'restoration backup file'
        Assert-OnlyDefaultDataStream $source 'restoration backup file'
        Assert-NoHardLink $source 'restoration backup file'
        if ((Get-FileSha256 $source) -cne $entry.Sha256) {
            throw 'Restoration backup file digest changed before restore.'
        }
        Assert-PathBelowRoot $target $Guard.Root 'restoration file'
        $parent = Split-Path -Parent $target
        Assert-RestorationDirectoryCapabilities $Guard
        Assert-NoReparsePointInExistingPath $parent 'restoration file parent'
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        $existing = Get-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
        if ($null -ne $existing) {
            Assert-RestorationDirectoryCapabilities $Guard
            Remove-SafeTree $target $Guard.Root
        }
        Assert-NoReparsePointInExistingPath $parent 'restoration file parent'
        # Never overwrite: a raced-in link or file makes Copy fail closed.
        Assert-RestorationDirectoryCapabilities $Guard
        [IO.File]::Copy($source, $target, $false)
        Assert-NoReparsePoint $target 'restored file'
        Assert-NoHardLink $target 'restored file'
        $item = Get-Item -LiteralPath $target -Force
        $item.CreationTimeUtc = [DateTime]::new([Int64]$entry.CreationTicks, [DateTimeKind]::Utc)
        $item.LastWriteTimeUtc = [DateTime]::new([Int64]$entry.WriteTicks, [DateTimeKind]::Utc)
        $item.Attributes = [IO.FileAttributes]([Int64]$entry.Attributes)
    }
    foreach ($entry in @($Guard.Before.Entries | Where-Object Kind -eq 'directory' |
            Sort-Object { $_.RelativePath.Split('/').Count } -Descending)) {
        $target = if ($entry.RelativePath -eq '.') { $Guard.Root } else {
            Join-Path $Guard.Root $entry.RelativePath.Replace('/', '\')
        }
        Assert-NoReparsePointInExistingPath $target 'restored directory'
        $item = Get-Item -LiteralPath $target -Force
        $item.CreationTimeUtc = [DateTime]::new([Int64]$entry.CreationTicks, [DateTimeKind]::Utc)
        $item.LastWriteTimeUtc = [DateTime]::new([Int64]$entry.WriteTicks, [DateTimeKind]::Utc)
        $item.Attributes = [IO.FileAttributes]([Int64]$entry.Attributes)
    }
    $after = Get-ResearchSnapshot $Guard.Root
    Assert-RestorationDirectoryCapabilities $Guard
    if ($after.EntryCount -ne $Guard.Before.EntryCount -or
        $after.TotalBytes -ne $Guard.Before.TotalBytes -or
        $after.ManifestSha256 -cne $Guard.Before.ManifestSha256) {
        throw 'Research restoration detected external-file drift.'
    }
    return $after
}

function Get-KnownSteamRoots {
    $roots = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($registryPath in @('HKCU:\Software\Valve\Steam',
            'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        if (-not (Test-Path -LiteralPath $registryPath)) { continue }
        $record = Get-ItemProperty -LiteralPath $registryPath
        foreach ($name in @('SteamPath', 'InstallPath')) {
            $property = $record.PSObject.Properties[$name]
            if ($null -ne $property -and -not [string]::IsNullOrWhiteSpace([string]$property.Value)) {
                [void]$roots.Add([IO.Path]::GetFullPath([string]$property.Value))
            }
        }
    }
    foreach ($root in @($roots)) {
        $libraries = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $libraries -PathType Leaf)) { continue }
        if ((Get-Item -LiteralPath $libraries).Length -gt $maximumSteamManifestBytes) {
            throw 'Steam library manifest exceeds its bound.'
        }
        foreach ($match in [regex]::Matches((Get-Content -Raw -LiteralPath $libraries),
                '"path"\s+"(?<path>[^"]+)"')) {
            [void]$roots.Add([IO.Path]::GetFullPath($match.Groups['path'].Value.Replace('\\', '\')))
        }
    }
    $canonicalRoots =
        [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($root in @($roots)) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        # Get-Item expands DOS/8.3 aliases (unlike Path.GetFullPath), so a
        # differently-spelled alias cannot evade the Steam-library comparison.
        $canonical = [IO.Path]::GetFullPath(
            (Get-Item -LiteralPath $root -Force -ErrorAction Stop).FullName
        ).TrimEnd('\', '/')
        [void]$canonicalRoots.Add($canonical)
    }
    return @($canonicalRoots)
}

function Assert-ExactJsonProperties {
    param(
        [object]$Value,
        [string[]]$Expected,
        [string]$Label
    )
    if ($null -eq $Value -or $Value -is [Array] -or $Value -is [string]) {
        throw "$Label must be one JSON object."
    }
    $actual = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    if ($actual.Count -ne $Expected.Count) {
        throw "$Label property set is not exact."
    }
    foreach ($name in $Expected) {
        if ($actual -cnotcontains $name) {
            throw "$Label property set is not exact."
        }
    }
    foreach ($name in $actual) {
        if ($Expected -cnotcontains $name) {
            throw "$Label property set is not exact."
        }
    }
}

function Get-BoundedJsonInteger {
    param(
        [object]$Value,
        [Int64]$Minimum,
        [Int64]$Maximum,
        [string]$Label
    )
    if (-not ($Value -is [byte] -or $Value -is [sbyte] -or
            $Value -is [Int16] -or $Value -is [UInt16] -or
            $Value -is [Int32] -or $Value -is [UInt32] -or
            $Value -is [Int64])) {
        throw "$Label must be an integer."
    }
    [Int64]$number = $Value
    if ($number -lt $Minimum -or $number -gt $Maximum) {
        throw "$Label is outside its bound."
    }
    return $number
}

function Assert-LowerSha256Reference {
    param([object]$Value, [string]$Label)
    if (-not ($Value -is [string]) -or
        [string]$Value -cnotmatch '^[0-9a-f]{64}$') {
        throw "$Label is not a private SHA-256 reference."
    }
}

function Assert-FunctionalResearchProjection {
    param([string]$Root, [object[]]$Items)
    if (-not $functionalPolicyMode -or
        [string]::IsNullOrWhiteSpace($AppManifestPath)) {
        throw 'Functional research projection is unavailable outside functional launch policy.'
    }
    $manifest = [IO.Path]::GetFullPath($AppManifestPath)
    if ([IO.Path]::GetFileName($manifest) -cne 'appmanifest_70.acf' -or
        -not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw 'Functional research projection requires appmanifest_70.acf.'
    }
    $sourceRoot = [IO.Path]::GetFullPath(
        (Join-Path (Split-Path -Parent $manifest) 'common\Half-Life')).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container) -or
        (Test-PathAtOrBelow $Root $sourceRoot) -or
        (Test-PathAtOrBelow $sourceRoot $Root)) {
        throw 'Functional Valve projection source is absent or overlaps research root.'
    }

    $researchEntries = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($item in $Items) {
        $relative = Get-RelativePath $item.FullName $Root
        if ($relative -ceq $markerName -or
            $relative -ceq $pendingMarkerName -or
            $relative -ceq $preparationManifestName) {
            continue
        }
        if ($researchEntries.ContainsKey($relative)) {
            throw 'Functional research projection contains an ambiguous path.'
        }
        $researchEntries.Add($relative, $item)
    }
    $sourceItems = @(Get-BoundedItems $sourceRoot)
    $sourceEntries = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($item in $sourceItems) {
        $relative = Get-RelativePath $item.FullName $sourceRoot
        if ($relative.Equals('hlfxmp', [StringComparison]::OrdinalIgnoreCase) -or
            $relative.StartsWith('hlfxmp/', [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        if ($sourceEntries.ContainsKey($relative)) {
            throw 'Functional Valve projection source contains an ambiguous path.'
        }
        $sourceEntries.Add($relative, $item)
    }

    foreach ($relative in $researchEntries.Keys) {
        if ($functionalMutablePaths.Contains($relative)) { continue }
        if (-not $sourceEntries.ContainsKey($relative)) {
            throw 'Functional research projection has an unapproved extra path.'
        }
        $researchItem = $researchEntries[$relative]
        $sourceItem = $sourceEntries[$relative]
        $researchDirectory = ($researchItem.Attributes -band
            [IO.FileAttributes]::Directory) -ne 0
        $sourceDirectory = ($sourceItem.Attributes -band
            [IO.FileAttributes]::Directory) -ne 0
        if ($researchDirectory -ne $sourceDirectory) {
            throw 'Functional research projection entry kind changed.'
        }
        if (-not $researchDirectory -and
            ($researchItem.Length -ne $sourceItem.Length -or
             (Get-FileSha256 $researchItem.FullName) -cne
                (Get-FileSha256 $sourceItem.FullName))) {
            throw 'Functional research projection immutable content changed.'
        }
    }
    # A v3 preparation manifest attests the exact source inventory at copy time.
    # A later file added to the primary installation is not part of that prepared
    # projection and must not invalidate it. The forward comparison above still
    # rejects every unapproved research-only path and verifies every immutable
    # research byte against the current source; critical launch/game identities
    # are additionally required below.
    foreach ($relative in $functionalMutablePaths) {
        if ($relative -ceq 'steam_appid.txt') {
            if (-not $researchEntries.ContainsKey($relative) -or
                ($researchEntries[$relative].Attributes -band
                    [IO.FileAttributes]::Directory) -ne 0) {
                throw 'Functional research projection lacks its local App ID marker.'
            }
            $appIdValue = Read-BoundedAsciiMarker `
                $researchEntries[$relative].FullName
            if ($appIdValue -cne '70' -and $appIdValue -cne "70`n" -and
                $appIdValue -cne "70`r`n") {
                throw 'Functional research projection has an invalid local App ID marker.'
            }
            continue
        }
        if (-not $researchEntries.ContainsKey($relative) -or
            -not $sourceEntries.ContainsKey($relative) -or
            ($researchEntries[$relative].Attributes -band
                [IO.FileAttributes]::Directory) -ne 0 -or
            ($sourceEntries[$relative].Attributes -band
                [IO.FileAttributes]::Directory) -ne 0) {
            throw 'Functional research projection mutable allowlist shape changed.'
        }
    }
    foreach ($critical in @(
            'hl.exe', 'hlds.exe', 'valve/cl_dlls/client.dll',
            'valve/dlls/hl.dll', 'valve/liblist.gam',
            'valve/maps/boot_camp.bsp')) {
        if (-not $researchEntries.ContainsKey($critical) -or
            -not $sourceEntries.ContainsKey($critical) -or
            (Get-FileSha256 $researchEntries[$critical].FullName) -cne
                (Get-FileSha256 $sourceEntries[$critical].FullName)) {
            throw 'Functional research projection critical identity changed.'
        }
    }
    return [pscustomobject]@{
        Status = 'prepared_projection_content_verified'
        MutablePathCount = [Int64]$functionalMutablePaths.Count
    }
}

function Get-ResearchPreparationInventory {
    param([string]$Root, [object[]]$Items)
    $v1Records = [Collections.Generic.List[string]]::new()
    $v2Paths = [Collections.Generic.List[string]]::new()
    $v2Records = [Collections.Generic.List[string]]::new()
    [Int64]$totalBytes = 0
    $clientSha256 = $null
    $serverSha256 = $null
    foreach ($item in $Items) {
        $relative = Get-RelativePath $item.FullName $Root
        if ($relative -ceq $markerName -or
            $relative -ceq $pendingMarkerName -or
            $relative -ceq $preparationManifestName) {
            continue
        }
        [void]$v2Paths.Add($relative)
        $isDirectory =
            ($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0
        if ($isDirectory) {
            [void]$v1Records.Add('d|' + $relative)
            [void]$v2Records.Add('d|' + $relative)
            continue
        }
        if ($item.Length -lt 0 -or
            $totalBytes -gt ($maximumResearchBytes - $item.Length)) {
            throw 'Research preparation inventory exceeds its byte bound.'
        }
        $totalBytes += $item.Length
        $sha256 = Get-FileSha256 $item.FullName
        [void]$v1Records.Add(
            ('f|{0}|{1}|{2}' -f $relative, $item.Length, $sha256))
        [void]$v2Records.Add(
            ('f|{0}|{1}|{2}' -f $relative, $item.Length,
                $sha256.ToLowerInvariant()))
        if ($relative -ceq 'hl.exe') { $clientSha256 = $sha256 }
        if ($relative -ceq 'hlds.exe') { $serverSha256 = $sha256 }
    }
    if ($v1Records.Count -lt 2 -or $null -eq $clientSha256 -or
        $null -eq $serverSha256) {
        throw 'Research preparation inventory lacks required launchers.'
    }
    $v1Canonical = @($v1Records | Sort-Object) -join "`n"
    $v2OrderedPaths = $v2Paths.ToArray()
    $v2Ordered = $v2Records.ToArray()
    # The native materializer orders structured inventory entries by their
    # relative path before adding the d|/f| record prefix. Sort parallel keys
    # and values here so the live-tree verifier consumes that exact contract;
    # sorting the finished records would incorrectly group every directory
    # ahead of every file.
    [Array]::Sort(
        $v2OrderedPaths, $v2Ordered, [StringComparer]::Ordinal)
    $v2Canonical = if ($v2Ordered.Count -eq 0) { '' } else {
        (@($v2Ordered) -join "`n") + "`n"
    }
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $utf8 = [Text.UTF8Encoding]::new($false, $true)
        $v1Sha256 = ([BitConverter]::ToString(
                $algorithm.ComputeHash($utf8.GetBytes($v1Canonical)))).Replace('-', '')
        $algorithm.Initialize()
        $v2Sha256 = ([BitConverter]::ToString(
                $algorithm.ComputeHash($utf8.GetBytes($v2Canonical)))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
    return [pscustomobject]@{
        EntryCount = $v1Records.Count
        ByteCount = $totalBytes
        V1Sha256 = $v1Sha256
        V2Sha256 = $v2Sha256
        ClientSha256 = $clientSha256
        ServerSha256 = $serverSha256
    }
}

function Assert-ResearchPendingMarker {
    param([string]$Root)
    $path = [IO.Path]::GetFullPath((Join-Path $Root $pendingMarkerName))
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw 'Research preparation v2 lacks its pending/commit state marker.'
    }
    $pending = Read-BoundedJson $path 4096 `
        'research preparation pending marker'
    Assert-ExactJsonProperties $pending @(
        'schema', 'category', 'paths_recorded') `
        'research preparation pending marker'
    if ($pending.schema -cne 'hlclient.stock-research-copy-pending.v1' -or
        $pending.category -cne 'awaiting_commit_marker' -or
        -not ($pending.paths_recorded -is [bool]) -or
        $pending.paths_recorded -ne $false) {
        throw 'Research preparation pending marker policy is invalid.'
    }
}

function Assert-ResearchPreparationManifest {
    param(
        [string]$Root,
        [object[]]$Items,
        [switch]$AllowFunctionalMutableDrift
    )
    $path = [IO.Path]::GetFullPath(
        (Join-Path $Root $preparationManifestName))
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw 'Research root lacks a preparation manifest.'
    }
    $manifest = Read-BoundedJson $path 32768 'research preparation manifest'
    if ($null -eq $manifest.PSObject.Properties['schema'] -or
        -not ($manifest.schema -is [string])) {
        throw 'Research preparation manifest schema is absent.'
    }
    $inventory = Get-ResearchPreparationInventory $Root $Items

    if ([string]$manifest.schema -ceq
        'hlclient.stock-runtime-research-preparation.v1') {
        $expected = @(
            'schema', 'marker', 'source_inventory_entries',
            'source_inventory_bytes', 'source_inventory_sha256',
            'client_sha256', 'server_launcher_sha256', 'paths_recorded',
            'preparation_status')
        Assert-ExactJsonProperties $manifest $expected `
            'research preparation manifest v1'
        if ($manifest.marker -cne $markerText -or
            -not ($manifest.paths_recorded -is [bool]) -or
            $manifest.paths_recorded -ne $false -or
            $manifest.preparation_status -cne 'exact-copy-verified') {
            throw 'Research preparation manifest v1 policy is invalid.'
        }
        [Int64]$entryCount = Get-BoundedJsonInteger `
            $manifest.source_inventory_entries 2 $maximumEntries `
            'v1 source entry count'
        [Int64]$byteCount = Get-BoundedJsonInteger `
            $manifest.source_inventory_bytes 1 $maximumResearchBytes `
            'v1 source byte count'
        foreach ($property in @(
                'source_inventory_sha256', 'client_sha256',
                'server_launcher_sha256')) {
            if (-not ($manifest.$property -is [string]) -or
                [string]$manifest.$property -cnotmatch '^[0-9A-F]{64}$') {
                throw "Research preparation manifest v1 $property is invalid."
            }
        }
        if ($entryCount -ne $inventory.EntryCount -or
            $byteCount -ne $inventory.ByteCount -or
            $manifest.source_inventory_sha256 -cne $inventory.V1Sha256 -or
            $manifest.client_sha256 -cne $inventory.ClientSha256 -or
            $manifest.server_launcher_sha256 -cne $inventory.ServerSha256) {
            throw 'Research preparation manifest v1 inventory disagrees with the tree.'
        }
        return [pscustomobject]@{
            Schema = [string]$manifest.schema
            ExternalTargetProfile = 'none'
            ExternalTargetCount = [Int64]0
            InventoryStatus = 'exact'
        }
    }

    if ([string]$manifest.schema -ceq
        'hlclient.stock-runtime-research-preparation.v3') {
        $expected = @(
            'schema', 'marker', 'preparation_profile',
            'source_root_identity_fingerprint',
            'source_inventory_entries', 'source_inventory_bytes',
            'source_inventory_sha256',
            'contained_materialized_link_count',
            'approved_external_materialized_link_count',
            'source_hardlink_count',
            'destination_entry_count', 'destination_byte_count',
            'destination_inventory_sha256',
            'destination_reparse_count', 'destination_hardlink_count',
            'destination_ads_count', 'external_approval_sha256',
            'external_classification_summary', 'executable_target_count',
            'mutable_state_target_count', 'source_unchanged_status',
            'external_targets_unchanged_status', 'evidence_eligibility',
            'external_target_profile',
            'client_binary_private_identity_reference',
            'server_binary_private_identity_reference', 'paths_recorded',
            'preparation_status')
        Assert-ExactJsonProperties $manifest $expected `
            'research preparation manifest v3'

        if ($manifest.marker -cne $markerText -or
            -not ($manifest.paths_recorded -is [bool]) -or
            $manifest.paths_recorded -ne $false -or
            $manifest.source_unchanged_status -cne 'verified' -or
            $manifest.external_targets_unchanged_status -cne 'verified') {
            throw 'Research preparation manifest v3 policy is invalid.'
        }

        # Evidence eligibility is checked before any active-capture tool or WFP
        # operation. Keep the public failure typed and path-free so an
        # ineligible private review cannot be converted into a canary launch.
        if (-not ($manifest.evidence_eligibility -is [string]) -or
            $manifest.evidence_eligibility -cne 'eligible') {
            throw 'research_copy_not_evidence_eligible'
        }

        [Int64]$sourceEntryCount = Get-BoundedJsonInteger `
            $manifest.source_inventory_entries 2 $maximumEntries `
            'v3 source entry count'
        [Int64]$sourceByteCount = Get-BoundedJsonInteger `
            $manifest.source_inventory_bytes 1 $maximumResearchBytes `
            'v3 source byte count'
        [Int64]$destinationEntryCount = Get-BoundedJsonInteger `
            $manifest.destination_entry_count 2 $maximumEntries `
            'v3 destination entry count'
        [Int64]$destinationByteCount = Get-BoundedJsonInteger `
            $manifest.destination_byte_count 1 $maximumResearchBytes `
            'v3 destination byte count'
        [Int64]$containedLinkCount = Get-BoundedJsonInteger `
            $manifest.contained_materialized_link_count 0 $sourceEntryCount `
            'v3 contained materialized link count'
        [Int64]$approvedExternalCount = Get-BoundedJsonInteger `
            $manifest.approved_external_materialized_link_count 0 `
            $sourceEntryCount 'v3 approved external materialized link count'
        [void](Get-BoundedJsonInteger $manifest.source_hardlink_count 0 `
            $sourceEntryCount 'v3 source hardlink count')
        [void](Get-BoundedJsonInteger $manifest.destination_reparse_count 0 0 `
            'v3 destination reparse count')
        [void](Get-BoundedJsonInteger $manifest.destination_hardlink_count 0 0 `
            'v3 destination hardlink count')
        [void](Get-BoundedJsonInteger $manifest.destination_ads_count 0 0 `
            'v3 destination alternate-data-stream count')
        [void](Get-BoundedJsonInteger $manifest.executable_target_count 0 0 `
            'v3 executable external target count')
        [void](Get-BoundedJsonInteger $manifest.mutable_state_target_count 0 0 `
            'v3 mutable-state external target count')
        if ($containedLinkCount -gt
            ($sourceEntryCount - $approvedExternalCount)) {
            throw 'Research preparation manifest v3 link counts are inconsistent.'
        }

        foreach ($property in @(
                'source_root_identity_fingerprint',
                'source_inventory_sha256', 'destination_inventory_sha256',
                'external_approval_sha256',
                'client_binary_private_identity_reference',
                'server_binary_private_identity_reference')) {
            Assert-LowerSha256Reference $manifest.$property `
                "research preparation manifest v3 $property"
        }

        $zeroSha256 = '0' * 64
        if ($manifest.preparation_profile -ceq 'ordinary-or-contained-v3') {
            if ($approvedExternalCount -ne 0 -or
                $manifest.external_approval_sha256 -cne $zeroSha256 -or
                $manifest.external_classification_summary -cne 'none' -or
                $manifest.external_target_profile -cne 'none' -or
                $manifest.preparation_status -cne
                    'exact-materialized-copy-verified') {
                throw 'Research preparation manifest v3 ordinary profile is invalid.'
            }
        } elseif ($manifest.preparation_profile -ceq
            'reviewed-external-targets-v1') {
            if ($approvedExternalCount -lt 1 -or
                $manifest.external_approval_sha256 -ceq $zeroSha256 -or
                $manifest.external_classification_summary -cne
                    'eligible_non_executable_asset_tree' -or
                $manifest.external_target_profile -cne
                    'reviewed-non-executable-v1' -or
                $manifest.preparation_status -cne
                    'exact-reviewed-materialized-copy-verified') {
                throw 'Research preparation manifest v3 reviewed profile is invalid.'
            }
            Assert-ExternalApprovalDigestAvailable `
                ([string]$manifest.external_approval_sha256)
        } else {
            throw 'Research preparation manifest v3 preparation profile is invalid.'
        }

        $inventoryExact = $sourceEntryCount -eq $inventory.EntryCount -and
            $sourceByteCount -eq $inventory.ByteCount -and
            $destinationEntryCount -eq $inventory.EntryCount -and
            $destinationByteCount -eq $inventory.ByteCount -and
            $manifest.source_inventory_sha256 -ceq $inventory.V2Sha256 -and
            $manifest.destination_inventory_sha256 -ceq $inventory.V2Sha256
        $functionalProjection = $null
        if (-not $inventoryExact -and $AllowFunctionalMutableDrift) {
            $functionalProjection = Assert-FunctionalResearchProjection `
                $Root $Items
        } elseif (-not $inventoryExact) {
            throw 'Research preparation manifest v3 inventory disagrees with the tree.'
        }

        # The manifest counts are attestations, not a substitute for checking
        # the exact destination. Re-screen every materialized entry before the
        # active caller can begin isolation or process work.
        foreach ($item in $Items) {
            Assert-OnlyDefaultDataStream $item.FullName 'v3 research entry'
            if (($item.Attributes -band [IO.FileAttributes]::Directory) -eq 0) {
                Assert-NoHardLink $item.FullName 'v3 research file'
            }
        }
        Assert-ResearchPendingMarker $Root
        return [pscustomobject]@{
            Schema = [string]$manifest.schema
            ExternalTargetProfile = [string]$manifest.external_target_profile
            ExternalTargetCount = $approvedExternalCount
            InventoryStatus = $(if ($inventoryExact) { 'exact' } else {
                    [string]$functionalProjection.Status
                })
        }
    }

    if ([string]$manifest.schema -cne
        'hlclient.stock-runtime-research-preparation.v2') {
        throw 'Research preparation manifest schema is unsupported.'
    }
    $expected = @(
        'schema', 'marker', 'topology_profile',
        'source_root_identity_fingerprint', 'entry_count', 'byte_count',
        'materialized_link_count', 'materialized_hardlink_count',
        'rejected_link_count', 'inventory_sha256',
        'client_binary_private_identity_reference',
        'server_binary_private_identity_reference',
        'destination_unlinked_status', 'source_unchanged_status',
        'paths_recorded', 'preparation_status')
    Assert-ExactJsonProperties $manifest $expected `
        'research preparation manifest v2'
    if ($manifest.marker -cne $markerText -or
        -not ($manifest.paths_recorded -is [bool]) -or
        $manifest.paths_recorded -ne $false -or
        $manifest.destination_unlinked_status -cne 'verified' -or
        $manifest.source_unchanged_status -cne 'verified' -or
        $manifest.preparation_status -cne
            'exact-materialized-copy-verified') {
        throw 'Research preparation manifest v2 policy is invalid.'
    }
    if (-not ($manifest.topology_profile -is [Array])) {
        throw 'Research preparation topology profile must be a JSON array.'
    }
    $topology = @($manifest.topology_profile)
    $allowedTopology = @(
        'ordinary_tree', 'source_path_ancestor_reparse',
        'source_root_reparse', 'source_internal_directory_junction',
        'source_internal_directory_symlink', 'source_file_hardlink')
    if ($topology.Count -lt 1 -or $topology.Count -gt $allowedTopology.Count) {
        throw 'Research preparation topology profile count is invalid.'
    }
    $seenTopology = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($category in $topology) {
        if (-not ($category -is [string]) -or
            $allowedTopology -cnotcontains [string]$category -or
            -not $seenTopology.Add([string]$category)) {
            throw 'Research preparation topology profile is invalid.'
        }
    }
    if ($seenTopology.Contains('ordinary_tree') -and $topology.Count -ne 1) {
        throw 'ordinary_tree cannot be combined with linked topology.'
    }

    [Int64]$entryCount = Get-BoundedJsonInteger $manifest.entry_count 2 `
        $maximumEntries 'v2 source entry count'
    [void](Get-BoundedJsonInteger $manifest.byte_count 1 `
        $maximumResearchBytes 'v2 source byte count')
    [Int64]$linkCount = Get-BoundedJsonInteger `
        $manifest.materialized_link_count 0 $entryCount `
        'v2 materialized link count'
    [Int64]$hardlinkCount = Get-BoundedJsonInteger `
        $manifest.materialized_hardlink_count 0 $entryCount `
        'v2 materialized hardlink count'
    [void](Get-BoundedJsonInteger $manifest.rejected_link_count 0 0 `
        'v2 rejected link count')
    if ($seenTopology.Contains('source_file_hardlink') -ne
        ($hardlinkCount -gt 0)) {
        throw 'Research preparation hardlink topology/count disagrees.'
    }
    $linkTopologyPresent =
        $seenTopology.Contains('source_root_reparse') -or
        $seenTopology.Contains('source_internal_directory_junction') -or
        $seenTopology.Contains('source_internal_directory_symlink')
    if ($linkTopologyPresent -ne ($linkCount -gt 0)) {
        throw 'Research preparation link topology/count disagrees.'
    }
    foreach ($property in @(
            'source_root_identity_fingerprint', 'inventory_sha256',
            'client_binary_private_identity_reference',
            'server_binary_private_identity_reference')) {
        Assert-LowerSha256Reference $manifest.$property `
            "research preparation manifest v2 $property"
    }
    if ($entryCount -ne $inventory.EntryCount -or
        [Int64]$manifest.byte_count -ne $inventory.ByteCount -or
        $manifest.inventory_sha256 -cne $inventory.V2Sha256) {
        throw 'Research preparation manifest v2 inventory disagrees with the tree.'
    }
    Assert-ResearchPendingMarker $Root
    return [pscustomobject]@{
        Schema = [string]$manifest.schema
        ExternalTargetProfile = 'none'
        ExternalTargetCount = [Int64]0
        InventoryStatus = 'exact'
    }
}

function Assert-ApprovedLocalDriveRoot {
    param([string]$Path, [string]$Label)
    if ($Path -cnotmatch '^(?<drive>[A-Za-z]):\\') {
        throw "$Label must use a local drive-letter path; UNC and volume aliases are rejected."
    }
    $driveName = $Matches.drive.ToUpperInvariant()
    $drive = [IO.DriveInfo]::new($driveName + ':\')
    if (-not $drive.IsReady -or $drive.DriveType -ne [IO.DriveType]::Fixed) {
        throw "$Label must reside on a ready fixed local drive."
    }
    $subst = Join-Path $env:SystemRoot 'System32\subst.exe'
    if (-not (Test-Path -LiteralPath $subst -PathType Leaf)) {
        throw 'Cannot prove research-root isolation because subst.exe is absent.'
    }
    $mappings = @(& $subst 2>$null | ForEach-Object { $_.ToString() })
    if ($LASTEXITCODE -ne 0) {
        throw 'Cannot prove research-root isolation because subst.exe failed.'
    }
    $prefix = '^\s*' + [regex]::Escape($driveName + ':\:') + '\s*=>'
    if (@($mappings | Where-Object { $_ -match $prefix }).Count -ne 0) {
        throw "$Label must not use a substituted drive alias."
    }
}

function Resolve-IsolatedResearchRoot {
    $requestedRoot = [IO.Path]::GetFullPath($ResearchHalfLifeRoot).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $requestedRoot -PathType Container)) {
        throw 'ResearchHalfLifeRoot must be an existing directory.'
    }
    Assert-ApprovedLocalDriveRoot $requestedRoot 'research root'
    # DirectoryInfo.FullName expands every existing DOS/8.3 path component.
    # Reparse ancestors, UNC/network roots, volume aliases, and substituted
    # drives are rejected separately so alternate spellings cannot bypass the
    # Steam-library comparison.
    $root = [IO.Path]::GetFullPath(
        (Get-Item -LiteralPath $requestedRoot -Force -ErrorAction Stop).FullName
    ).TrimEnd('\', '/')
    Assert-NoReparsePointInExistingPath $root 'research root'
    # Directory ADS are not members of the recursive file-system inventory.
    # Screen the exact root before accepting either preparation-manifest
    # schema; Get-ResearchSnapshot repeats this at every restoration boundary.
    Assert-OnlyDefaultDataStream $root 'research root'
    $canonicalRepositoryRoot = [IO.Path]::GetFullPath(
        (Get-Item -LiteralPath $repositoryRoot -Force -ErrorAction Stop).FullName
    ).TrimEnd('\', '/')
    $rootIdentity = Get-PhysicalPathIdentity $root 'research root'
    $repositoryIdentity =
        Get-PhysicalPathIdentity $canonicalRepositoryRoot 'repository root'
    if ((Test-PathAtOrBelow $root $canonicalRepositoryRoot) -or
        (Test-PathAtOrBelow $canonicalRepositoryRoot $root) -or
        (Test-PhysicalPathAtOrBelow $rootIdentity $repositoryIdentity) -or
        (Test-PhysicalPathAtOrBelow $repositoryIdentity $rootIdentity) -or
        $root -match '(?i)(?:^|[\\/])steamapps(?:[\\/]|$)') {
        throw 'Research root must be disjoint from repository and Steam libraries.'
    }
    foreach ($steamRoot in @(Get-KnownSteamRoots)) {
        $steamIdentity = Get-PhysicalPathIdentity $steamRoot 'Steam library root'
        if ((Test-PathAtOrBelow $root $steamRoot) -or
            (Test-PathAtOrBelow $steamRoot $root) -or
            (Test-PhysicalPathAtOrBelow $rootIdentity $steamIdentity) -or
            (Test-PhysicalPathAtOrBelow $steamIdentity $rootIdentity)) {
            throw 'Research root overlaps a configured Steam library.'
        }
    }
    $boundedResearchItems = @(Get-BoundedItems $root)
    $marker = [IO.Path]::GetFullPath((Join-Path $root $markerName))
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw 'Research root lacks the exact isolation marker.'
    }
    Assert-NoReparsePointInExistingPath $marker 'research marker'
    Assert-OnlyDefaultDataStream $marker 'research marker'
    Assert-NoHardLink $marker 'research marker'
    $markerValue = Read-BoundedAsciiMarker $marker
    if ($markerValue -cne $markerText -and
        $markerValue -cne ($markerText + "`n") -and
        $markerValue -cne ($markerText + "`r`n")) {
        throw 'Research root lacks the exact isolation marker.'
    }
    $preparationManifest = Assert-ResearchPreparationManifest `
        $root $boundedResearchItems `
        -AllowFunctionalMutableDrift:$functionalPolicyMode
    $client = [IO.Path]::GetFullPath($ClientPath)
    $server = [IO.Path]::GetFullPath($HldsPath)
    $expectedProjectClient = [IO.Path]::GetFullPath(
        (Join-Path $repositoryRoot 'build\bin\Release\hlclient.exe'))
    if (($projectClientStockSignonMode -and
         $client -ine $expectedProjectClient) -or
        (-not $projectClientStockSignonMode -and
         $client -ine (Join-Path $root 'hl.exe')) -or
        $server -ine (Join-Path $root 'hlds.exe')) {
        throw 'ClientPath and HldsPath must be the canonical root launchers.'
    }
    if ($projectClientStockSignonMode) {
        Assert-PathBelowRoot $client $repositoryRoot 'project client'
        Assert-NoReparsePointInExistingPath $client 'project client'
        Assert-OnlyDefaultDataStream $client 'project client'
        Assert-NoHardLink $client 'project client'
        if (-not (Test-Path -LiteralPath $client -PathType Leaf)) {
            throw 'Project client executable is absent.'
        }
    }
    $stockBinaryPairs = @()
    if ($projectClientStockSignonMode) {
        $stockBinaryPairs += ,@($server, '4.1.1.1', 'stock server')
    } else {
        $stockBinaryPairs += ,@($client, '1.1.1.1', 'stock client')
        $stockBinaryPairs += ,@($server, '4.1.1.1', 'stock server')
    }
    foreach ($pair in $stockBinaryPairs) {
        Assert-NoReparsePointInExistingPath $pair[0] $pair[2]
        Assert-OnlyDefaultDataStream $pair[0] $pair[2]
        Assert-NoHardLink $pair[0] $pair[2]
        $item = Get-Item -LiteralPath $pair[0] -Force
        $version = '{0}.{1}.{2}.{3}' -f $item.VersionInfo.FileMajorPart,
            $item.VersionInfo.FileMinorPart, $item.VersionInfo.FileBuildPart,
            $item.VersionInfo.FilePrivatePart
        if ($version -cne $pair[1]) { throw "$($pair[2]) version is not accepted." }
        $signature = Get-AuthenticodeSignature -LiteralPath $pair[0]
        if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
            $signature.SignerCertificate.Subject -cnotmatch '^CN=Valve Corp\.(?:,|$)') {
            throw "$($pair[2]) is not validly Valve-signed."
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $root 'valve') -PathType Container)) {
        throw 'Research root lacks the valve directory.'
    }
    return [pscustomobject]@{
        Root = $root
        Client = $client
        Server = $server
        PreparationManifestSchema = $preparationManifest.Schema
        ExternalTargetProfile = $preparationManifest.ExternalTargetProfile
        ExternalTargetCount = [Int64]$preparationManifest.ExternalTargetCount
        InventoryStatus = [string]$preparationManifest.InventoryStatus
    }
}

function Test-IsElevatedAdministrator {
    if ($env:OS -cne 'Windows_NT') { return $false }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        return $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
    } finally {
        $identity.Dispose()
    }
}

function Resolve-TrustedRepositoryTool {
    param([string]$Path, [string]$ExpectedName, [string]$Label)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw "$Label path is required." }
    $tool = [IO.Path]::GetFullPath($Path)
    Assert-PathBelowRoot $tool $repositoryRoot $Label
    Assert-NoReparsePointInExistingPath $tool $Label
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf) -or
        [IO.Path]::GetFileName($tool) -cne $ExpectedName) {
        throw "$Label must name the canonical repository-built $ExpectedName."
    }
    Assert-OnlyDefaultDataStream $tool $Label
    Assert-NoHardLink $tool $Label
    return $tool
}

function Resolve-AppManifest {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw 'AppManifestPath is required for active environment validation.'
    }
    $manifest = [IO.Path]::GetFullPath($Path)
    Assert-NoReparsePointInExistingPath $manifest 'Steam App 70 manifest'
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf) -or
        [IO.Path]::GetFileName($manifest) -cne 'appmanifest_70.acf') {
        throw 'AppManifestPath must name appmanifest_70.acf.'
    }
    Assert-OnlyDefaultDataStream $manifest 'Steam App 70 manifest'
    Assert-NoHardLink $manifest 'Steam App 70 manifest'
    $item = Get-Item -LiteralPath $manifest -Force
    if ($item.Length -lt 1 -or $item.Length -gt $maximumSteamManifestBytes) {
        throw 'Steam App 70 manifest length is outside its bound.'
    }
    return $manifest
}

function ConvertTo-WindowsCommandLineArgument {
    param([string]$Value)
    if ($Value.Length -ne 0 -and $Value -cnotmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * (($slashes * 2) + 1)))
            [void]$builder.Append('"')
            $slashes = 0
            continue
        }
        if ($slashes -ne 0) {
            [void]$builder.Append(('\' * $slashes))
            $slashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($slashes -ne 0) { [void]$builder.Append(('\' * ($slashes * 2))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Initialize-OrchestratorCapabilityNative {
    if ($null -ne ('Hlclient.StockRuntimeOrchestratorCapability' -as [type])) {
        return
    }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Hlclient
{
    [StructLayout(LayoutKind.Sequential)]
    public struct StockRuntimeSecurityAttributes
    {
        public int Length;
        public IntPtr SecurityDescriptor;
        [MarshalAs(UnmanagedType.Bool)] public bool InheritHandle;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct StockRuntimeJobAccounting
    {
        public long TotalUserTime;
        public long TotalKernelTime;
        public long ThisPeriodTotalUserTime;
        public long ThisPeriodTotalKernelTime;
        public uint TotalPageFaultCount;
        public uint TotalProcesses;
        public uint ActiveProcesses;
        public uint TotalTerminatedProcesses;
    }

    public static class StockRuntimeOrchestratorCapability
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr CreateEvent(
            ref StockRuntimeSecurityAttributes attributes,
            [MarshalAs(UnmanagedType.Bool)] bool manualReset,
            [MarshalAs(UnmanagedType.Bool)] bool initialState,
            string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SetEvent(IntPtr handle);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr CreateJobObject(
            ref StockRuntimeSecurityAttributes attributes, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool TerminateJobObject(
            IntPtr job, uint exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool QueryInformationJobObject(
            IntPtr job, int informationClass,
            out StockRuntimeJobAccounting information,
            uint informationLength, IntPtr returnLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CloseHandle(IntPtr handle);
    }
}
'@
}

function New-OrchestratorTransactionCapability {
    Initialize-OrchestratorCapabilityNative
    $security = [Hlclient.StockRuntimeSecurityAttributes]::new()
    $security.Length = [Runtime.InteropServices.Marshal]::SizeOf(
        [type][Hlclient.StockRuntimeSecurityAttributes])
    $security.SecurityDescriptor = [IntPtr]::Zero
    $security.InheritHandle = $true
    $handle = [Hlclient.StockRuntimeOrchestratorCapability]::CreateEvent(
        [ref]$security, $true, $false, $null)
    if ($handle -eq [IntPtr]::Zero) {
        $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Wrapper transaction capability creation failed (Win32 $nativeError)."
    }
    return $handle
}

function New-OrchestratorProcessJobCapability {
    Initialize-OrchestratorCapabilityNative
    $security = [Hlclient.StockRuntimeSecurityAttributes]::new()
    $security.Length = [Runtime.InteropServices.Marshal]::SizeOf(
        [type][Hlclient.StockRuntimeSecurityAttributes])
    $security.SecurityDescriptor = [IntPtr]::Zero
    $security.InheritHandle = $true
    $handle = [Hlclient.StockRuntimeOrchestratorCapability]::CreateJobObject(
        [ref]$security, $null)
    if ($handle -eq [IntPtr]::Zero) {
        $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        throw "Wrapper process Job creation failed (Win32 $nativeError)."
    }
    return $handle
}

function Confirm-OrchestratorProcessJobCleanup {
    param(
        [IntPtr]$JobHandle,
        [int]$TimeoutMilliseconds = 10000,
        [int]$GracePeriodMilliseconds = 0
    )
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $terminationRequested = $false
    while ($true) {
        $accounting = [Hlclient.StockRuntimeJobAccounting]::new()
        $accountingSize = [Runtime.InteropServices.Marshal]::SizeOf(
            [type][Hlclient.StockRuntimeJobAccounting])
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::QueryInformationJobObject(
                $JobHandle, 1, [ref]$accounting, [uint32]$accountingSize,
                [IntPtr]::Zero)) {
            $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "Wrapper process Job accounting failed (Win32 $nativeError)."
        }
        if ($accounting.ActiveProcesses -eq 0) { return $true }
        if (-not $terminationRequested -and
            $clock.ElapsedMilliseconds -ge $GracePeriodMilliseconds) {
            if (-not [Hlclient.StockRuntimeOrchestratorCapability]::TerminateJobObject(
                    $JobHandle, 120)) {
                $nativeError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                throw "Wrapper process Job termination failed (Win32 $nativeError)."
            }
            $terminationRequested = $true
        }
        if ($clock.ElapsedMilliseconds -ge $TimeoutMilliseconds) {
            throw 'Wrapper process Job did not reach exact zero-process accounting.'
        }
        Start-Sleep -Milliseconds 10
    }
}

function New-BoundedProcessStreamState {
    param([string]$Name, [IO.TextReader]$Reader)
    $buffer = [char[]]::new(2048)
    return [pscustomobject]@{
        Name = $Name
        Reader = $Reader
        Buffer = $buffer
        Builder = [Text.StringBuilder]::new()
        Bytes = [Int64]0
        Done = $false
        Task = $Reader.ReadAsync($buffer, 0, $buffer.Length)
    }
}

function Receive-BoundedProcessStream {
    param([object]$State)
    if ($null -eq $State -or $State.Done -or -not $State.Task.IsCompleted) {
        return
    }
    $read = $State.Task.GetAwaiter().GetResult()
    if ($read -eq 0) {
        $State.Done = $true
        return
    }
    $chunk = [string]::new($State.Buffer, 0, $read)
    $chunkBytes = [Text.Encoding]::UTF8.GetByteCount($chunk)
    if ($State.Bytes -gt (65536 - $chunkBytes)) {
        throw "Project orchestrator $($State.Name) exceeded its byte bound."
    }
    $State.Bytes += $chunkBytes
    [void]$State.Builder.Append($chunk)
    $State.Task = $State.Reader.ReadAsync(
        $State.Buffer, 0, $State.Buffer.Length)
}

function Complete-BoundedProcessStreams {
    param(
        [object]$StdoutState,
        [object]$StderrState,
        [int]$TimeoutMilliseconds = 5000
    )
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not ($StdoutState.Done -and $StderrState.Done)) {
        Receive-BoundedProcessStream $StdoutState
        Receive-BoundedProcessStream $StderrState
        if ($StdoutState.Done -and $StderrState.Done) { return }
        if ($clock.ElapsedMilliseconds -ge $TimeoutMilliseconds) {
            throw 'Project orchestrator output drain exceeded its bounded deadline.'
        }
        Start-Sleep -Milliseconds 10
    }
}

function Get-BoundedStartupDiagnostic {
    param([object]$State)
    if ($null -eq $State) { return '' }
    $value = $State.Builder.ToString().Replace("`r", ' ').Replace("`n", ' ').Trim()
    if ($value.Length -gt 1024) { return $value.Substring(0, 1024) }
    return $value
}

function Update-OrchestratorStartupCaptureState {
    param([object]$ExactExitState, [object]$StdoutState, [object]$StderrState)
    if ($null -eq $ExactExitState) { return }
    if ($null -ne $StdoutState) {
        $ExactExitState.StartupStdout = $StdoutState.Builder.ToString()
        $ExactExitState.StartupStdoutBytes = [Int64]$StdoutState.Bytes
    }
    if ($null -ne $StderrState) {
        $ExactExitState.StartupStderr = $StderrState.Builder.ToString()
        $ExactExitState.StartupStderrBytes = [Int64]$StderrState.Bytes
    }
}

function New-OrchestratorExitState {
    return [pscustomobject]@{
        Started = $false
        StartupStatus = 'not_started'
        StartupWaitResult = $null
        StartupNativeError = $null
        StartupExitCode = $null
        StartupExitCodeHex = $null
        StartupStdout = ''
        StartupStderr = ''
        StartupStdoutBytes = [Int64]0
        StartupStderrBytes = [Int64]0
        ExitConfirmed = $false
        ExitCode = $null
        NoOrchestratorProcessCreated = $false
        CleanupSignaled = $false
        CampaignJobCleanupConfirmed = $false
        GuardJobCleanupConfirmed = $false
        JobCleanupConfirmed = $false
        WriterTracePrelaunchReady = $false
        WriterTraceReady = $false
        WriterTraceLaunchReleased = $false
        WriterTraceStockProcessesStopped = $false
        WriterTraceStockProcessesStoppedUtc = $null
        WriterTraceFinalized = $false
        WriterTraceCollectorProcessId = $null
        WriterTraceCompleteness = 'not_started'
        WriterTraceFailure = $null
        WriterTraceOwner = $null
        PrimaryFailure = $null
        CleanupFailure = $null
        Failure = $null
    }
}

function Invoke-BoundedOrchestrator {
    param(
        [string]$Path,
        [string[]]$Arguments,
        [int]$TimeoutSeconds,
        [IntPtr]$CapabilityHandle = [IntPtr]::Zero,
        [IntPtr]$CleanupCapabilityHandle = [IntPtr]::Zero,
        [IntPtr]$JobHandle = [IntPtr]::Zero,
        [IntPtr]$GuardJobHandle = [IntPtr]::Zero,
        [IntPtr]$IsolationReleaseHandle = [IntPtr]::Zero,
        [object]$ExactExitState = $null,
        [IntPtr]$WriterTracePrelaunchReadyHandle = [IntPtr]::Zero,
        [IntPtr]$WriterTraceLaunchReleaseHandle = [IntPtr]::Zero,
        [IntPtr]$WriterTraceStockStoppedHandle = [IntPtr]::Zero,
        [object]$WriterTraceRequest = $null
    )
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Path
    $start.Arguments = (@($Arguments | ForEach-Object {
                ConvertTo-WindowsCommandLineArgument ([string]$_)
            }) -join ' ')
    $start.WorkingDirectory = $repositoryRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    $result = $null
    $invocationError = $null
    $cleanupFailure = $null
    $writerTraceOwner = $null
    $stdoutState = $null
    $stderrState = $null
    try {
        try {
            if (-not $process.Start()) {
                throw 'Process.Start returned false.'
            }
        } catch {
            if ($null -ne $ExactExitState) {
                $ExactExitState.StartupStatus = 'process_start_failed'
                $ExactExitState.PrimaryFailure = $_.Exception.Message
            }
            throw "orchestrator_process_start_failed: $($_.Exception.Message)"
        }
        $started = $true
        if ($null -ne $ExactExitState) { $ExactExitState.Started = $true }
        $stdoutState = New-BoundedProcessStreamState stdout $process.StandardOutput
        $stderrState = New-BoundedProcessStreamState stderr $process.StandardError
        $clock = [Diagnostics.Stopwatch]::StartNew()
        if ($CapabilityHandle -ne [IntPtr]::Zero) {
            while ($true) {
                Receive-BoundedProcessStream $stdoutState
                Receive-BoundedProcessStream $stderrState
                $capabilityWait =
                    [Hlclient.StockRuntimeOrchestratorCapability]::WaitForSingleObject(
                        $CapabilityHandle, 0)
                if ($capabilityWait -eq 0) {
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.StartupStatus = 'acknowledgement_received'
                        $ExactExitState.StartupWaitResult = [uint32]$capabilityWait
                    }
                    break
                }
                if ($capabilityWait -eq [uint32]::MaxValue) {
                    $nativeError =
                        [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.StartupStatus = 'wait_failed'
                        $ExactExitState.StartupWaitResult = [uint32]$capabilityWait
                        $ExactExitState.StartupNativeError = $nativeError
                    }
                    throw "orchestrator_startup_wait_failed: wait-result=$capabilityWait; win32=$nativeError"
                }
                if ($capabilityWait -ne 258) {
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.StartupStatus = 'wait_failed'
                        $ExactExitState.StartupWaitResult = [uint32]$capabilityWait
                    }
                    throw "orchestrator_startup_wait_failed: wait-result=$capabilityWait"
                }
                if ($process.HasExited) {
                    # The child can SetEvent and exit between the preceding
                    # zero-timeout poll and HasExited. Recheck the same
                    # inherited capability after observing exit so that this
                    # successful boundary is not misclassified as early exit.
                    $postExitWait =
                        [Hlclient.StockRuntimeOrchestratorCapability]::WaitForSingleObject(
                            $CapabilityHandle, 0)
                    if ($postExitWait -eq 0) {
                        if ($null -ne $ExactExitState) {
                            $ExactExitState.StartupStatus =
                                'acknowledgement_received'
                            $ExactExitState.StartupWaitResult =
                                [uint32]$postExitWait
                        }
                        break
                    }
                    Complete-BoundedProcessStreams $stdoutState $stderrState
                    $exitCode = [int]$process.ExitCode
                    $exitHex = '0x{0:X8}' -f ([uint32]$exitCode)
                    $stderrDiagnostic = Get-BoundedStartupDiagnostic $stderrState
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.StartupStatus = 'early_exit'
                        $ExactExitState.StartupWaitResult = [uint32]$capabilityWait
                        $ExactExitState.StartupExitCode = $exitCode
                        $ExactExitState.StartupExitCodeHex = $exitHex
                    }
                    throw "orchestrator_startup_early_exit: exit-code=$exitCode; exit-code-hex=$exitHex; stderr=$stderrDiagnostic"
                }
                if ($clock.ElapsedMilliseconds -ge 5000) {
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.StartupStatus = 'timeout'
                        $ExactExitState.StartupWaitResult = [uint32]$capabilityWait
                    }
                    throw 'orchestrator_startup_timeout: acknowledgement was not received within 5000 ms'
                }
                Start-Sleep -Milliseconds 10
            }
        }
        if ($null -ne $WriterTraceRequest) {
            if ($WriterTracePrelaunchReadyHandle -eq [IntPtr]::Zero -or
                $WriterTraceLaunchReleaseHandle -eq [IntPtr]::Zero -or
                $WriterTraceStockStoppedHandle -eq [IntPtr]::Zero) {
                throw 'writer_trace_handoff_capabilities_missing'
            }
            $prelaunchClock = [Diagnostics.Stopwatch]::StartNew()
            while ([Hlclient.StockRuntimeOrchestratorCapability]::WaitForSingleObject(
                    $WriterTracePrelaunchReadyHandle, 0) -ne 0) {
                if ($process.HasExited) {
                    throw 'writer_trace_prelaunch_not_reached'
                }
                if ($prelaunchClock.ElapsedMilliseconds -ge 60000) {
                    throw 'writer_trace_prelaunch_timeout'
                }
                Start-Sleep -Milliseconds 25
            }
            if ($null -ne $ExactExitState) {
                $ExactExitState.WriterTracePrelaunchReady = $true
            }
            Add-StockWriterTraceTimelineEvent $WriterTraceRequest.Timeline `
                prelaunch_ready -IpcEvidence orchestrator_prelaunch_event |
                Out-Null
            $writerTraceOwner = Start-StockWriterTraceCollector $WriterTraceRequest
            if ($null -ne $ExactExitState) {
                $ExactExitState.WriterTraceOwner = $writerTraceOwner
            }
            if (-not $writerTraceOwner.Started -or
                $null -eq $writerTraceOwner.ReadyReceipt) {
                throw $(if ($writerTraceOwner.Failure) {
                        $writerTraceOwner.Failure
                    } else { 'writer_trace_not_ready' })
            }
            if ($null -ne $ExactExitState) {
                $ExactExitState.WriterTraceReady = $true
                $ExactExitState.WriterTraceCollectorProcessId =
                    $writerTraceOwner.ProcessId
            }
            if (-not [Hlclient.StockRuntimeOrchestratorCapability]::SetEvent(
                    $WriterTraceLaunchReleaseHandle)) {
                $nativeError =
                    [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                throw "writer_trace_launch_release_failed_win32_$nativeError"
            }
            if ($null -ne $ExactExitState) {
                $ExactExitState.WriterTraceLaunchReleased = $true
            }
            Add-StockWriterTraceTimelineEvent $writerTraceOwner.Timeline `
                launch_released -IpcEvidence wrapper_release_event | Out-Null
        }
        # Read both redirected streams concurrently into fixed-size chunks.
        # ReadToEndAsync would eventually reject oversized output but could
        # buffer it without a bound first.  These builders never admit more
        # than 64 KiB per stream; finally kills this exact process on overflow
        # or timeout, which closes its owned Job boundary in active mode.
        while (-not ($stdoutState.Done -and $stderrState.Done)) {
            [Int64]$remainingMilliseconds =
                ([Int64]$TimeoutSeconds * 1000) - $clock.ElapsedMilliseconds
            if ($remainingMilliseconds -le 0) {
                throw 'Project orchestrator exceeded its bounded deadline.'
            }
            Receive-BoundedProcessStream $stdoutState
            Receive-BoundedProcessStream $stderrState
            if (-not ($stdoutState.Done -and $stderrState.Done)) {
                Start-Sleep -Milliseconds ([Math]::Min(10, [int]$remainingMilliseconds))
            }
        }
        [Int64]$remainingForExit =
            ([Int64]$TimeoutSeconds * 1000) - $clock.ElapsedMilliseconds
        if ($remainingForExit -le 0 -or
            -not $process.WaitForExit([int]$remainingForExit)) {
            throw 'Project orchestrator exceeded its bounded deadline.'
        }
        if ($null -ne $WriterTraceRequest) {
            $stockStopped =
                [Hlclient.StockRuntimeOrchestratorCapability]::WaitForSingleObject(
                    $WriterTraceStockStoppedHandle, 0)
            if ($stockStopped -ne 0) {
                throw 'writer_trace_stock_processes_stopped_not_attested'
            }
            if ($null -ne $ExactExitState) {
                $ExactExitState.WriterTraceStockProcessesStopped = $true
                $ExactExitState.WriterTraceStockProcessesStoppedUtc =
                    [DateTime]::UtcNow.ToString('o')
            }
            $writerTraceOwner.StockProcessesStoppedUtc =
                [DateTime]::UtcNow.ToString('o')
            $writerTraceOwner = Complete-StockWriterTraceCollector `
                $writerTraceOwner $WriterTraceRequest.TailMilliseconds
            if ($null -ne $ExactExitState) {
                $ExactExitState.WriterTraceFinalized =
                    $null -ne $writerTraceOwner.FinalReceipt
                $ExactExitState.WriterTraceCompleteness =
                    $writerTraceOwner.TraceCompleteness
                $ExactExitState.WriterTraceFailure = $writerTraceOwner.Failure
            }
        }
        $stdout = $stdoutState.Builder.ToString()
        $stderr = $stderrState.Builder.ToString()
        $stdoutLines = @($stdout -split '\r?\n' | Where-Object { $_.Length -ne 0 })
        $stderrLines = @($stderr -split '\r?\n' | Where-Object { $_.Length -ne 0 })
        if ($stdoutLines.Count -gt 128 -or $stderrLines.Count -gt 128 -or
            @($stdoutLines + $stderrLines | Where-Object { $_.Length -gt 1024 }).Count -ne 0) {
            throw 'Project orchestrator output exceeded its line bound.'
        }
        $allowedKeys = @(
            'active-environment', 'isolation-canary', 'binary-profile',
            'stock-processes-started', 'capture-files-written', 'result',
            'orchestrator', 'failure-category', 'processes-started',
            'relay-ready', 'server-ready', 'client-ready', 'job-cleanup',
            'persistent-rules', 'ipv4-loopback', 'ipv6-loopback',
            'non-loopback-canary', 'isolation-cleanup', 'client-file-version',
            'client-signature', 'server-launcher-version', 'server-signature',
            'steam-app-id', 'steam-build-id', 'server-engine-version',
            'protocol', 'server-build', 'unexpected-children',
            'bounded-transport-complete', 'run-id', 'journal-entries',
            'raw-datagrams', 'sequenced-c2s', 'sequenced-s2c', 'duration-ms',
            'preflight-schema', 'elevation-status', 'app-manifest',
            'wfp-session', 'timestamp-category', 'connection-generations',
            'generation-distinct', 'candidate-conflict',
            'server-profile-id', 'server-readiness-status',
            'server-readiness-endpoint-proof', 'server-readiness-map-proof',
            'server-readiness-owned-process',
            'server-readiness-process-identity',
            'server-readiness-endpoint-owner',
            'server-readiness-endpoint-address',
            'server-readiness-endpoint-port',
            'server-readiness-response-source', 'server-readiness-map',
            'server-readiness-game', 'server-readiness-query-attempts',
            'server-readiness-response-bytes',
            'server-profile-parse-status', 'server-profile-mismatch-field',
            'server-profile-engine-version-status',
            'server-profile-runtime-mode-status', 'server-profile-game-status',
            'server-profile-protocol-status', 'server-profile-build-status',
            'server-profile-endpoint-address-status',
            'server-profile-endpoint-port-status', 'server-profile-map-status',
            'server-profile-duplicate-fields',
            'server-profile-process-log-truncated',
            'server-profile-endpoint-address-category',
            'server-profile-runtime-mode-category',
            'server-profile-observed-byte-count',
            'server-profile-observed-line-count',
            'server-profile-observed-engine-version',
            'server-profile-observed-protocol',
            'server-profile-observed-build', 'server-profile-result',
            'writer-trace-prelaunch-ready',
            'writer-trace-launch-released',
            'writer-trace-stock-processes-stopped',
            'writer-trace-clock-frequency',
            'writer-trace-stock-process-created-ticks',
            'writer-trace-stock-processes-stopped-ticks',
            'startup-boundary', 'mode', 'purpose', 'evidence-eligible',
            'route', 'stable-duration-ms', 'client-exit-code',
            'client-exit-code-hex', 'client-wait-result',
            'client-wait-native-error',
            'server-exit-code', 'external-steam-state',
            'external-steam-state-policy', 'client-name-observed',
            'client-entered-game-observed',
            'client-running-at-readiness-deadline',
            'server-process-created', 'server-process-id',
            'client-process-created', 'client-process-id',
            'client-image-identity', 'client-resume-result',
            'client-initialized', 'connect-requested',
            'connection-status', 'client-map-entry-status',
            'map-entry-source', 'last-confirmed-stage',
            'client-steam-argument', 'server-logging',
            'client-connect-port',
            'steam-authentication-error-observed',
            'application-entry-observed', 'arguments-accepted',
            'provider-begin-observed', 'steam-api-init-attempted',
            'steam-api-initialized', 'fresh-material-acquired',
            'connect-sent', 'connection-accepted',
            'serverinfo-received', 'schema-registry-received',
            'authentication-status', 'serverinfo-protocol',
            'serverinfo-max-clients', 'serverinfo-game', 'serverinfo-map',
            'schema-count', 'schema-field-count',
            'resource-continuation-sent',
            'spawn-request-transmitted',
            'spawn-request-acknowledged',
            'signon-reply-transmitted',
            'signon-reply-acknowledged',
            'live-service-payloads-received',
            'client-world-state-published', 'usercmd-transmitted',
            'usercmd-movement-verified', 'live-visual-verified',
            'usercmd-generated',
            'usercmd-new', 'usercmd-backup', 'usercmd-packets',
            'usercmd-server-samples',
            'baseline-entity-count', 'service-payload-count',
            'applied-runtime-record-count', 'world-entity-count',
            'publication-revision', 'canonical-state-hash',
            'stable-runtime-interval-ms',
            'diagnostic-publication', 'lifecycle-clock', 'lifecycle-unit',
            'requested-maximum-duration-ms',
            'required-runtime-interval-ms',
            'client-map-entry-observed-ms',
            'functional-interval-completed-ms', 'shutdown-requested-ms',
            'relay-stop-requested-ms', 'relay-finalization-completed-ms',
            'stop-reason', 'stock-shutdown-method',
            'relay-phase', 'failed-operation', 'native-error-domain',
            'native-error-code', 'relay-exit-code',
            'relay-exit-code-hex', 'wait-result', 'stop-requested',
            'journal-publication-state', 'metadata-publication-state')
        if ($projectClientVisualMode -and
            $ProjectClientLiveInput -ceq 'scripted-jump-duck-check') {
            $allowedKeys += @(Get-GJumpDuckNativeStatusKeys)
        }
        if ($projectClientVisualMode -and
            $ProjectClientLiveInput -ceq 'scripted-speed-check') {
            $allowedKeys += @('speed-result')
        }
        if ($ProjectClientPrediction -ceq 'reference') {
            $allowedKeys += @('prediction-result')
        }
        $values = [Collections.Generic.Dictionary[string, string]]::new(
            [StringComparer]::Ordinal)
        foreach ($line in $stdoutLines) {
            if ($line -cnotmatch '^\[stock-runtime-orchestrator\] (?<key>[a-z0-9-]+)=(?<value>[A-Za-z0-9_.:/-]{1,128})$') {
                throw 'Project orchestrator emitted a non-contract stdout line.'
            }
            $key = $Matches.key
            if ($allowedKeys -cnotcontains $key -or $values.ContainsKey($key)) {
                throw 'Project orchestrator emitted an unknown or duplicate status key.'
            }
            $values.Add($key, $Matches.value)
        }
        if ($null -ne $writerTraceOwner) {
            foreach ($requiredTimelineKey in @(
                    'writer-trace-clock-frequency',
                    'writer-trace-stock-process-created-ticks',
                    'writer-trace-stock-processes-stopped-ticks')) {
                if (-not $values.ContainsKey($requiredTimelineKey)) {
                    throw "writer_trace_orchestrator_timeline_missing:$requiredTimelineKey"
                }
            }
            [Int64]$orchestratorFrequency =
                $values['writer-trace-clock-frequency']
            [Int64]$stockCreatedTicks =
                $values['writer-trace-stock-process-created-ticks']
            [Int64]$stockStoppedTicks =
                $values['writer-trace-stock-processes-stopped-ticks']
            if ($orchestratorFrequency -ne
                    [Int64]$writerTraceOwner.Timeline.ClockFrequency -or
                $stockCreatedTicks -le 0 -or $stockStoppedTicks -le 0) {
                throw 'writer_trace_orchestrator_timeline_domain_invalid'
            }
            Add-StockWriterTraceTimelineEvent $writerTraceOwner.Timeline `
                stock_process_created -NowTicks $stockCreatedTicks `
                -Source orchestrator `
                -IpcEvidence orchestrator_persisted_qpc | Out-Null
            Add-StockWriterTraceTimelineEvent $writerTraceOwner.Timeline `
                stock_processes_stopped -NowTicks $stockStoppedTicks `
                -Source orchestrator `
                -IpcEvidence orchestrator_cleanup_event | Out-Null
            if ($null -ne $ExactExitState) {
                $ExactExitState.WriterTraceOwner = $writerTraceOwner
            }
        }
        $result = [pscustomobject]@{
            ExitCode = $process.ExitCode
            Values = $values
            StderrLineCount = $stderrLines.Count
            WriterTrace = $writerTraceOwner
        }
    } catch {
        $invocationError = $_
        if ($null -ne $ExactExitState) {
            $ExactExitState.PrimaryFailure = $_.Exception.Message
        }
        Update-OrchestratorStartupCaptureState `
            $ExactExitState $stdoutState $stderrState
    } finally {
        # A pipeline stop/Ctrl+C can interrupt WaitForExit before the C++
        # boundary returns.  Terminating that exact owned orchestrator closes
        # its kill-on-close Job Object, which in turn stops only its verified
        # relay/guard/stock children.  Process.Dispose alone does not stop a
        # still-running child.
        if ($started) {
            try {
                if (-not $process.HasExited) {
                    try { $process.Kill() }
                    catch {
                        if (-not $process.HasExited) { throw }
                    }
                }
                if (-not $process.WaitForExit(5000) -or -not $process.HasExited) {
                    throw 'Exact orchestrator process exit was not confirmed after termination.'
                }
                if ($null -ne $stdoutState -and $null -ne $stderrState) {
                    Complete-BoundedProcessStreams $stdoutState $stderrState
                    Update-OrchestratorStartupCaptureState `
                        $ExactExitState $stdoutState $stderrState
                }
                if ($null -ne $ExactExitState) {
                    $ExactExitState.ExitConfirmed = $true
                    $ExactExitState.ExitCode = $process.ExitCode
                    $ExactExitState.NoOrchestratorProcessCreated = $false
                }
                if ($CleanupCapabilityHandle -ne [IntPtr]::Zero) {
                    $cleanupWait =
                        [Hlclient.StockRuntimeOrchestratorCapability]::WaitForSingleObject(
                            $CleanupCapabilityHandle, 0)
                    if ($cleanupWait -eq 0) {
                        if ($null -ne $ExactExitState) {
                            $ExactExitState.CleanupSignaled = $true
                        }
                    } elseif ($cleanupWait -ne 258) {
                        throw "Wrapper cleanup capability query failed (wait result $cleanupWait)."
                    }
                }
                if ($JobHandle -ne [IntPtr]::Zero) {
                    [void](Confirm-OrchestratorProcessJobCleanup $JobHandle)
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.CampaignJobCleanupConfirmed = $true
                    }
                }
                if ($IsolationReleaseHandle -ne [IntPtr]::Zero) {
                    if (-not [Hlclient.StockRuntimeOrchestratorCapability]::SetEvent(
                            $IsolationReleaseHandle)) {
                        $nativeError =
                            [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                        throw "Isolation guard release signal failed (Win32 $nativeError)."
                    }
                }
                if ($GuardJobHandle -ne [IntPtr]::Zero) {
                    [void](Confirm-OrchestratorProcessJobCleanup `
                        $GuardJobHandle 10000 5000)
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.GuardJobCleanupConfirmed = $true
                    }
                }
                if ($null -ne $ExactExitState -and
                    $JobHandle -ne [IntPtr]::Zero -and
                    $GuardJobHandle -ne [IntPtr]::Zero -and
                    $IsolationReleaseHandle -ne [IntPtr]::Zero) {
                    $ExactExitState.JobCleanupConfirmed = $true
                }
            } catch {
                $cleanupFailure = $_
                if ($null -ne $ExactExitState) {
                    $ExactExitState.Failure = $_.Exception.Message
                    $ExactExitState.CleanupFailure = $_.Exception.Message
                }
            }
        } else {
            # Process.Start failed before returning a process handle. No
            # orchestrator exists to signal cleanup, but the wrapper still
            # owns both exact Jobs and must prove them empty before allowing
            # restoration or releasing the isolation guard capability.
            try {
                if ($null -ne $ExactExitState) {
                    $ExactExitState.ExitConfirmed = $true
                    $ExactExitState.ExitCode = $null
                    $ExactExitState.NoOrchestratorProcessCreated = $true
                    $ExactExitState.Failure =
                        'orchestrator-process-not-created'
                }
                if ($JobHandle -ne [IntPtr]::Zero) {
                    [void](Confirm-OrchestratorProcessJobCleanup $JobHandle)
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.CampaignJobCleanupConfirmed = $true
                    }
                }
                if ($IsolationReleaseHandle -ne [IntPtr]::Zero -and
                    -not [Hlclient.StockRuntimeOrchestratorCapability]::SetEvent(
                        $IsolationReleaseHandle)) {
                    $nativeError =
                        [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                    throw "Isolation guard release signal failed (Win32 $nativeError)."
                }
                if ($GuardJobHandle -ne [IntPtr]::Zero) {
                    [void](Confirm-OrchestratorProcessJobCleanup `
                        $GuardJobHandle 10000 5000)
                    if ($null -ne $ExactExitState) {
                        $ExactExitState.GuardJobCleanupConfirmed = $true
                    }
                }
                if ($null -ne $ExactExitState -and
                    $JobHandle -ne [IntPtr]::Zero -and
                    $GuardJobHandle -ne [IntPtr]::Zero -and
                    $IsolationReleaseHandle -ne [IntPtr]::Zero) {
                    $ExactExitState.JobCleanupConfirmed = $true
                }
            } catch {
                $cleanupFailure = $_
                if ($null -ne $ExactExitState) {
                    $ExactExitState.Failure = $_.Exception.Message
                    $ExactExitState.CleanupFailure = $_.Exception.Message
                }
            }
        }
        if ($null -ne $writerTraceOwner -and
            $null -eq $writerTraceOwner.FinalReceipt) {
            $writerTraceOwner = Complete-StockWriterTraceCollector `
                $writerTraceOwner 0 $(if ($null -ne $invocationError) {
                        $invocationError.Exception.Message
                    } else { '' })
            if ($null -ne $ExactExitState) {
                $ExactExitState.WriterTraceFinalized =
                    $null -ne $writerTraceOwner.FinalReceipt
                $ExactExitState.WriterTraceCompleteness =
                    $writerTraceOwner.TraceCompleteness
                $ExactExitState.WriterTraceFailure = $writerTraceOwner.Failure
                $ExactExitState.WriterTraceOwner = $writerTraceOwner
            }
        }
        $process.Dispose()
    }
    if ($null -ne $cleanupFailure) {
        if ($null -ne $invocationError) {
            $invocationError.Exception.Data['cleanup-failure'] =
                $cleanupFailure.Exception.Message
            throw $invocationError
        }
        throw $cleanupFailure
    }
    if ($null -ne $invocationError) { throw $invocationError }
    return $result
}

function Assert-OrchestratorValue {
    param([object]$Result, [string]$Name, [string]$Expected)
    if (-not $Result.Values.ContainsKey($Name) -or
        $Result.Values[$Name] -cne $Expected) {
        throw "Project orchestrator did not attest $Name=$Expected."
    }
}

function Write-StockServerProfileDiagnosticPublicOutput {
    param([Collections.Generic.Dictionary[string, string]]$Values)
    Write-Output ("[stock-server-profile] parse-status={0}" -f
        $Values['server-profile-parse-status'])
    Write-Output ("[stock-server-profile] mismatch-field={0}" -f
        $Values['server-profile-mismatch-field'])
    foreach ($field in @(
            'engine-version', 'runtime-mode', 'game', 'protocol', 'build',
            'endpoint-address', 'endpoint-port', 'map')) {
        Write-Output ("[stock-server-profile] {0}-status={1}" -f $field,
            $Values['server-profile-' + $field + '-status'])
    }
    Write-Output ("[stock-server-profile] duplicate-fields={0}" -f
        $Values['server-profile-duplicate-fields'])
    if ($Values.ContainsKey('server-profile-observed-engine-version')) {
        Write-Output ("[stock-server-profile] observed-engine-version={0}" -f
            $Values['server-profile-observed-engine-version'])
    }
    if ($Values.ContainsKey('server-profile-observed-protocol')) {
        Write-Output ("[stock-server-profile] observed-protocol={0}" -f
            $Values['server-profile-observed-protocol'])
    }
    if ($Values.ContainsKey('server-profile-observed-build')) {
        Write-Output ("[stock-server-profile] observed-build={0}" -f
            $Values['server-profile-observed-build'])
    }
    Write-Output ("[stock-server-profile] result={0}" -f
        $Values['server-profile-result'])
}

function Add-ExternalStateTree {
    param(
        [Collections.Generic.List[object]]$Entries,
        [string]$Scope,
        [string]$Root,
        [string]$RelativePrefix)
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return }
    $rootEntry = New-StockExternalStateEntry $Scope $RelativePrefix $Root
    [void]$Entries.Add($rootEntry)
    if ($rootEntry.read_status -cne 'readable' -or
        $rootEntry.entry_kind -cne 'directory') { return }
    $queue = [Collections.Generic.Queue[object]]::new()
    $queue.Enqueue([pscustomobject]@{
            Directory = [IO.DirectoryInfo](Get-Item -LiteralPath $Root -Force)
            Entry = $rootEntry
        })
    $rootPrefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    while ($queue.Count -ne 0) {
        $queued = $queue.Dequeue()
        $directory = $queued.Directory
        try {
            [string[]]$children = @($directory.GetFileSystemInfos() |
                ForEach-Object FullName)
        } catch {
            Set-StockExternalStateEntryFailure $queued.Entry $_.Exception `
                enumeration $true $false $false
            continue
        }
        [Array]::Sort($children, [StringComparer]::OrdinalIgnoreCase)
        foreach ($childPath in $children) {
            if ($Entries.Count -ge 50000) {
                throw 'External state snapshot exceeds its entry bound.'
            }
            $relative = $childPath.Substring($rootPrefix.Length).Replace('\', '/')
            if ($RelativePrefix -cne '.') {
                $relative = $RelativePrefix.TrimEnd('/') + '/' + $relative
            }
            $entry = New-StockExternalStateEntry $Scope $relative $childPath
            [void]$Entries.Add($entry)
            if ($entry.read_status -ceq 'readable' -and
                $entry.entry_kind -ceq 'directory') {
                $queue.Enqueue([pscustomobject]@{
                        Directory = [IO.DirectoryInfo]$childPath
                        Entry = $entry
                    })
            }
        }
    }
}

function Get-ExternalSteamStateSnapshot {
    param(
        [string]$ManifestPath,
        [string]$ResearchRoot,
        [string]$Phase = 'standard_server_diagnostic')
    $entries = [Collections.Generic.List[object]]::new()
    [void]$entries.Add((New-StockExternalStateEntry `
            'app_manifest' '.' $ManifestPath))
    $steamApps = Split-Path -Parent $ManifestPath
    $steamRoot = Split-Path -Parent $steamApps
    $primary = Join-Path $steamApps 'common\Half-Life'
    if (Test-Path -LiteralPath $primary -PathType Container) {
        [void]$entries.Add((New-StockExternalStateEntry `
                'steam_other_monitored_entry' '.' $primary))
        foreach ($exact in @(
                [pscustomobject]@{ Scope = 'steam_half_life_launcher'; Relative = 'hl.exe'; Path = (Join-Path $primary 'hl.exe') },
                [pscustomobject]@{ Scope = 'steam_hlds_launcher'; Relative = 'hlds.exe'; Path = (Join-Path $primary 'hlds.exe') },
                [pscustomobject]@{ Scope = 'steam_valve_client_dll'; Relative = 'valve/cl_dlls/client.dll'; Path = (Join-Path $primary 'valve\cl_dlls\client.dll') },
                [pscustomobject]@{ Scope = 'steam_valve_server_dll'; Relative = 'valve/dlls/hl.dll'; Path = (Join-Path $primary 'valve\dlls\hl.dll') })) {
            if (Test-Path -LiteralPath $exact.Path -PathType Leaf) {
                [void]$entries.Add((New-StockExternalStateEntry `
                        $exact.Scope $exact.Relative $exact.Path))
            }
        }
        Add-ExternalStateTree $entries 'steam_hlfx_tree' `
            (Join-Path $primary 'hlfxmp') '.'
    }
    $userdata = Join-Path $steamRoot 'userdata'
    if (Test-Path -LiteralPath $userdata -PathType Container) {
        Assert-NoReparsePointInExistingPath $userdata 'Steam userdata root'
        [string[]]$accountPaths = @(Get-ChildItem -LiteralPath $userdata -Force `
                -Directory | ForEach-Object FullName)
        [Array]::Sort($accountPaths, [StringComparer]::OrdinalIgnoreCase)
        foreach ($accountPath in $accountPaths) {
            $account = Get-Item -LiteralPath $accountPath -Force
            if ($account.Name -cnotmatch '^[0-9]{1,20}$' -or
                ($account.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                continue
            }
            foreach ($relative in @('70', 'config')) {
                $candidate = Join-Path $account.FullName $relative
                Add-ExternalStateTree $entries 'steam_library_metadata' `
                    $candidate ($account.Name + '/' + $relative)
            }
        }
    }
    # Research is a distinct transactional scope: Get-ResearchSnapshot and
    # Restore-ResearchState compare its complete deterministic inventory.
    # Including restored files here would mislabel the deliberate copy-back
    # identity replacement as Steam/source drift.
    [void]$ResearchRoot
    return New-StockExternalStateSnapshot @($entries) $Phase
}

function Write-AtomicJsonNoOverwrite {
    param(
        [string]$Path,
        [object]$Value,
        [string]$Label,
        [object]$DirectoryCapability)
    if (Test-Path -LiteralPath $Path) { throw "$Label already exists." }
    $parent = Split-Path -Parent $Path
    Assert-RunDirectoryCapability $DirectoryCapability $parent
    Assert-NoReparsePointInExistingPath $parent "$Label parent"
    $json = ($Value | ConvertTo-Json -Depth 8) + "`r`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($json)
    $DirectoryCapability.PublishNewFile(
        [IO.Path]::GetFileName($Path), $bytes)
    Assert-RunDirectoryCapability $DirectoryCapability $parent
    Assert-NoReparsePoint $Path $Label
    Assert-NoHardLink $Path $Label
}

function Publish-FunctionalRuntimeCaptureArtifacts {
    param(
        [string]$RunRoot,
        [string]$RunId,
        [object]$DirectoryCapability,
        [bool]$PublishComplete,
        [bool]$OwnedProcessesStopped,
        [bool]$ResearchRestorationExact,
        [string]$BeforeManifestSha256,
        [string]$AfterManifestSha256,
        [bool]$RelayReady,
        [bool]$ServerReady,
        [bool]$ClientReady,
        [bool]$TransportComplete,
        [string]$GameName,
        [string]$MapName,
        [int]$MaximumDuration,
        [string]$ServerProfile,
        [object]$Lifecycle)

    $restorationDocument = [ordered]@{
        schema = 'hlclient.functional-runtime-restoration.v1'
        run_id = $RunId
        owned_process_cleanup = $(if ($OwnedProcessesStopped) {
                'exact'
            } else { 'incomplete' })
        research_restoration = $(if ($ResearchRestorationExact) {
                'exact'
            } else { 'not_exact' })
        before_manifest_sha256 = $BeforeManifestSha256
        after_manifest_sha256 = $AfterManifestSha256
    }
    Write-AtomicJsonNoOverwrite `
        (Join-Path $RunRoot 'restoration-attestation.functional.json') `
        $restorationDocument 'functional runtime restoration attestation' `
        $DirectoryCapability

    if (-not $PublishComplete) {
        return [pscustomobject]@{
            ManifestPublished = $false
            Result = 'functional_runtime_capture_incomplete'
        }
    }
    if (-not $OwnedProcessesStopped -or -not $ResearchRestorationExact -or
        -not $RelayReady -or -not $ServerReady -or -not $ClientReady -or
        -not $TransportComplete) {
        throw 'Functional complete publication prerequisites are inconsistent.'
    }
    if ($null -eq $Lifecycle) {
        throw 'Functional complete publication lacks lifecycle data.'
    }
    $lifecycleValues = @{}
    if ($Lifecycle -is [Collections.IDictionary]) {
        foreach ($key in $Lifecycle.Keys) {
            $lifecycleValues[[string]$key] = [string]$Lifecycle[$key]
        }
    } else {
        foreach ($property in $Lifecycle.PSObject.Properties) {
            $lifecycleValues[[string]$property.Name] = [string]$property.Value
        }
    }
    foreach ($key in @(
            'lifecycle-clock', 'lifecycle-unit',
            'requested-maximum-duration-ms',
            'required-runtime-interval-ms',
            'client-map-entry-observed-ms',
            'functional-interval-completed-ms', 'shutdown-requested-ms',
            'relay-stop-requested-ms', 'relay-finalization-completed-ms',
            'stop-reason', 'stock-shutdown-method')) {
        if (-not $lifecycleValues.ContainsKey($key)) {
            throw "Functional lifecycle field is missing: $key."
        }
    }
    [Int64]$requestedMaximumMs = $lifecycleValues[
        'requested-maximum-duration-ms']
    [Int64]$requiredIntervalMs = $lifecycleValues[
        'required-runtime-interval-ms']
    [Int64]$mapEntryMs = $lifecycleValues['client-map-entry-observed-ms']
    [Int64]$intervalCompleteMs = $lifecycleValues[
        'functional-interval-completed-ms']
    [Int64]$shutdownRequestedMs = $lifecycleValues['shutdown-requested-ms']
    [Int64]$relayStopMs = $lifecycleValues['relay-stop-requested-ms']
    [Int64]$relayFinalizedMs = $lifecycleValues[
        'relay-finalization-completed-ms']
    if ($lifecycleValues['lifecycle-clock'] -cnotin @(
            'steady-clock', 'scaled-steady-clock') -or
        $lifecycleValues['lifecycle-unit'] -cne 'milliseconds' -or
        $requestedMaximumMs -ne ([Int64]$MaximumDuration * 1000) -or
        $requiredIntervalMs -ne 15000 -or $mapEntryMs -lt 0 -or
        $intervalCompleteMs -lt ($mapEntryMs + $requiredIntervalMs) -or
        $shutdownRequestedMs -lt $intervalCompleteMs -or
        $relayStopMs -lt $shutdownRequestedMs -or
        $relayFinalizedMs -lt $relayStopMs -or
        $relayFinalizedMs -ge $requestedMaximumMs -or
        $lifecycleValues['stop-reason'] -cne
            'functional-interval-complete' -or
        $lifecycleValues['stock-shutdown-method'] -cne
            'owned-process-terminate') {
        throw 'Functional lifecycle ordering or identity is invalid.'
    }

    $fileBindings = [ordered]@{}
    foreach ($name in @(
            'capture-metadata.json', 'transport-journal.jsonl',
            'version-observation.staged.json',
            'isolation-attestation.staged.json')) {
        $path = Join-Path $RunRoot $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Functional publication input is missing: $name."
        }
        Assert-NoReparsePoint $path "functional publication input $name"
        Assert-NoHardLink $path "functional publication input $name"
        $fileBindings[$name] = [ordered]@{
            byte_length = (Get-Item -LiteralPath $path).Length
            sha256 = Get-FileSha256 $path
        }
    }
    $functionalManifest = [ordered]@{
        schema = 'hlclient.functional-runtime-capture.v1'
        purpose = 'functional_runtime_capture'
        campaign_evidence_eligible = $false
        run_id = $RunId
        recorded_by_stock_pair = $true
        validated_by_our_decoder = $false
        route = 'stock_client_loopback_relay_stock_hlds'
        relay_policy = 'byte_preserving_owning_session_no_perturbation'
        game = $GameName
        map = $MapName
        maximum_duration_seconds = $MaximumDuration
        output_role = 'functional-runtime-capture'
        server_profile_id = $ServerProfile
        client_steam_argument = 'present'
        external_steam_state = 'not_assessed'
        lifecycle_clock = $lifecycleValues['lifecycle-clock']
        lifecycle_unit = $lifecycleValues['lifecycle-unit']
        requested_maximum_duration_ms = $requestedMaximumMs
        required_runtime_interval_ms = $requiredIntervalMs
        client_map_entry_observed_ms = $mapEntryMs
        functional_interval_completed_ms = $intervalCompleteMs
        shutdown_requested_ms = $shutdownRequestedMs
        relay_stop_requested_ms = $relayStopMs
        relay_finalization_completed_ms = $relayFinalizedMs
        stop_reason = $lifecycleValues['stop-reason']
        stock_shutdown_method = $lifecycleValues['stock-shutdown-method']
        capture_status = 'complete'
        owned_process_cleanup = 'exact'
        research_restoration = 'exact'
        capture_metadata_byte_length =
            $fileBindings['capture-metadata.json'].byte_length
        capture_metadata_sha256 =
            $fileBindings['capture-metadata.json'].sha256
        transport_journal_byte_length =
            $fileBindings['transport-journal.jsonl'].byte_length
        transport_journal_sha256 =
            $fileBindings['transport-journal.jsonl'].sha256
        version_observation_byte_length =
            $fileBindings['version-observation.staged.json'].byte_length
        version_observation_sha256 =
            $fileBindings['version-observation.staged.json'].sha256
        isolation_attestation_byte_length =
            $fileBindings['isolation-attestation.staged.json'].byte_length
        isolation_attestation_sha256 =
            $fileBindings['isolation-attestation.staged.json'].sha256
        result = 'functional_runtime_capture_complete'
    }
    Write-AtomicJsonNoOverwrite `
        (Join-Path $RunRoot 'functional-runtime-capture.json') `
        $functionalManifest 'functional runtime capture manifest' `
        $DirectoryCapability
    return [pscustomobject]@{
        ManifestPublished = $true
        Result = 'functional_runtime_capture_complete'
    }
}

function Write-AtomicJsonBatchNoOverwrite {
    param(
        [object[]]$Publications,
        [object]$DirectoryCapability)
    if ($null -eq $Publications -or $Publications.Count -lt 1 -or
        $Publications.Count -gt 16) {
        throw 'Atomic JSON publication batch shape is invalid.'
    }
    $parent = $null
    [string[]]$leaves = [string[]]::new($Publications.Count)
    [byte[][]]$payloads = [byte[][]]::new($Publications.Count)
    for ($index = 0; $index -lt $Publications.Count; $index++) {
        $entry = $Publications[$index]
        if ($null -eq $entry -or [string]::IsNullOrEmpty([string]$entry.Path) -or
            [string]::IsNullOrEmpty([string]$entry.Label)) {
            throw 'Atomic JSON publication batch entry is invalid.'
        }
        $entryParent = Split-Path -Parent ([string]$entry.Path)
        if ($null -eq $parent) { $parent = $entryParent }
        elseif (-not $parent.Equals(
                $entryParent, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Atomic JSON publication batch spans multiple directories.'
        }
        if (Test-Path -LiteralPath ([string]$entry.Path)) {
            throw "$($entry.Label) already exists."
        }
        $leaves[$index] = [IO.Path]::GetFileName([string]$entry.Path)
        $bytesProperty = $entry.PSObject.Properties['Bytes']
        if ($null -ne $bytesProperty) {
            $payloads[$index] = [byte[]]$bytesProperty.Value
        } else {
            $json = ($entry.Value | ConvertTo-Json -Depth 8) + "`r`n"
            $payloads[$index] = [Text.UTF8Encoding]::new($false).GetBytes($json)
        }
    }
    Assert-RunDirectoryCapability $DirectoryCapability $parent
    Assert-NoReparsePointInExistingPath $parent 'atomic JSON batch parent'
    # PublishNewFiles performs every identity, hard-link, size and exact-byte
    # check while all file handles are retained. Its successful return is the
    # commit point; do not introduce a fallible name-based check afterward.
    $DirectoryCapability.PublishNewFiles($leaves, $payloads)
}

function Publish-AcceptedEvidenceTransaction {
    param(
        [string]$RunRoot,
        [object]$Version,
        [byte[]]$VersionBytes,
        [object]$Isolation,
        [byte[]]$IsolationBytes,
        [object]$Restoration,
        [byte[]]$RestorationBytes,
        [object]$ReconnectObservation,
        [object]$RunManifest,
        [bool]$OwnedJobsExact,
        [bool]$RestorationExact,
        [bool]$ExternalStateExact,
        [bool]$CheckerWalkerReady,
        [string]$FailureCategory,
        [object]$DirectoryCapability)
    if (-not $OwnedJobsExact -or -not $RestorationExact -or
        -not $ExternalStateExact -or -not $CheckerWalkerReady -or
        $FailureCategory -cne 'none') {
        throw 'Final evidence publication gates are incomplete.'
    }
    if ($null -eq $Version -or $null -eq $Isolation -or
        $null -eq $Restoration -or $null -eq $RunManifest -or
        $null -eq $VersionBytes -or $VersionBytes.Length -eq 0 -or
        $null -eq $IsolationBytes -or $IsolationBytes.Length -eq 0 -or
        $null -eq $RestorationBytes -or $RestorationBytes.Length -eq 0 -or
        -not [bool]$RunManifest.accepted_transport_run -or
        -not [bool]$RunManifest.accepted_evidence_run -or
        [string]$RunManifest.failure_category -cne 'none') {
        throw 'Accepted evidence publication payload is invalid.'
    }
    $reconnectRun = [string]$RunManifest.scenario -ceq 'reconnect'
    if ($reconnectRun -ne ($null -ne $ReconnectObservation)) {
        throw 'Reconnect evidence payload does not match the accepted scenario.'
    }
    # The accepted manifest is deliberately the last member. The native held-
    # handle batch publishes the scenario-specific set or rolls every renamed
    # member back before returning an error.
    $publications = [Collections.Generic.List[object]]::new()
    [void]$publications.Add(
        [pscustomobject]@{
            Path = Join-Path $RunRoot 'version-observation.json'
            Bytes = $VersionBytes
            Label = 'final version observation'
        })
    [void]$publications.Add(
        [pscustomobject]@{
            Path = Join-Path $RunRoot 'isolation-attestation.json'
            Bytes = $IsolationBytes
            Label = 'final isolation attestation'
        })
    [void]$publications.Add(
        [pscustomobject]@{
            Path = Join-Path $RunRoot 'restoration-attestation.json'
            Bytes = $RestorationBytes
            Label = 'final restoration attestation'
        })
    if ($reconnectRun) {
        [void]$publications.Add(
            [pscustomobject]@{
                Path = Join-Path $RunRoot 'reconnect-observation.json'
                Value = $ReconnectObservation
                Label = 'final reconnect observation'
            })
    }
    # The accepted run manifest is always the final member and therefore the
    # externally visible transaction commit point for both scenario shapes.
    [void]$publications.Add(
        [pscustomobject]@{
            Path = Join-Path $RunRoot 'research-run-metadata.json'
            Value = $RunManifest
            Label = 'accepted research run manifest'
        })
    Write-AtomicJsonBatchNoOverwrite -Publications $publications.ToArray() `
        -DirectoryCapability $DirectoryCapability
}

function ConvertTo-RejectedRunManifest {
    param([object]$RunManifest, [string]$FailureCategory)
    if ($null -eq $RunManifest -or
        [string]::IsNullOrEmpty($FailureCategory) -or
        $FailureCategory -ceq 'none') {
        throw 'Rejected run manifest requires a typed failure category.'
    }
    $rejected = [ordered]@{}
    foreach ($key in $RunManifest.Keys) {
        $rejected[$key] = $RunManifest[$key]
    }
    foreach ($status in @(
            'isolation_status', 'process_ownership_status',
            'version_profile_status', 'relay_status', 'client_ready_status',
            'restoration_status', 'external_drift_status',
            'offline_replay_status', 'post_resource_boundary_status',
            'first_observation_status')) {
        $rejected[$status] = 'not-accepted'
    }
    $rejected['accepted_transport_run'] = $false
    $rejected['accepted_evidence_run'] = $false
    $rejected['failure_category'] = $FailureCategory
    foreach ($reconnectOnly in @(
            'connection_generation_count', 'exact_boundary_count',
            'runtime_candidate_count', 'generation_distinct',
            'candidate_conflict')) {
        if ($rejected.Contains($reconnectOnly)) {
            $rejected.Remove($reconnectOnly)
        }
    }
    return $rejected
}

function Write-RejectedManifestAfterEvidencePublicationFailure {
    param(
        [string]$RunRoot,
        [object]$RunManifest,
        [object]$DirectoryCapability)
    Assert-RunDirectoryCapability $DirectoryCapability $RunRoot
    foreach ($finalLeaf in @(
            'version-observation.json', 'isolation-attestation.json',
            'restoration-attestation.json', 'reconnect-observation.json',
            'research-run-metadata.json')) {
        if (Test-Path -LiteralPath (Join-Path $RunRoot $finalLeaf)) {
            throw 'Accepted evidence batch rollback did not leave every final leaf absent.'
        }
    }
    $rejectedManifest = ConvertTo-RejectedRunManifest `
        $RunManifest 'evidence_publication_failed'
    Write-AtomicJsonNoOverwrite -Path `
        (Join-Path $RunRoot 'research-run-metadata.json') `
        -Value $rejectedManifest `
        -Label 'publication-failed research run manifest' `
        -DirectoryCapability $DirectoryCapability
}

function Write-StagedRestorationAttestation {
    param(
        [string]$RunRoot,
        [object]$Before,
        [object]$After,
        [object]$ExternalBefore,
        [object]$ExternalAfter,
        [object]$ExternalDifference,
        [int]$ExitCode,
        [bool]$OwnedProcessesStopped,
        [object]$DirectoryCapability
    )
    $externalStatus = if ($ExternalBefore.ManifestSha256 -ceq
        $ExternalAfter.ManifestSha256) { 'none' } else { 'changed' }
    $value = [ordered]@{
        schema = 'hlclient.stock-runtime-restoration.v2'
        external_file_drift = $externalStatus
        raw_external_state = $ExternalDifference.raw_external_state
        protected_projection = $ExternalDifference.protected_projection
        steam_rewrite_policy_id = $ExternalDifference.policy_id
        policy_decision = $ExternalDifference.policy_decision
        snapshot_entry_count = $Before.EntryCount
        pre_manifest_sha256 = $Before.ManifestSha256
        post_manifest_sha256 = $After.ManifestSha256
        external_snapshot_entry_count = $ExternalBefore.EntryCount
        external_pre_manifest_sha256 = $ExternalBefore.ManifestSha256
        external_post_manifest_sha256 = $ExternalAfter.ManifestSha256
        external_drift_phase = $ExternalDifference.phase
        external_changed_scope_count = $ExternalDifference.changed_scopes
        external_content_change_count = $ExternalDifference.content_changes
        external_metadata_only_count = $ExternalDifference.metadata_only_changes
        external_identity_replacement_count = $ExternalDifference.identity_replacements
        external_created_count = $ExternalDifference.created
        external_removed_count = $ExternalDifference.removed
        external_unreadable_count = $ExternalDifference.unreadable
        created_files_removed = $true
        protected_paths_included = $true
        owned_processes_stopped = $OwnedProcessesStopped
        input_automation_used = $false
        input_events_injected = 0
        orchestrator_exit_code = $ExitCode
        restoration_status = $(if ($Before.ManifestSha256 -ceq $After.ManifestSha256) {
                'exact'
            } else { 'mismatch' })
    }
    Write-AtomicJsonNoOverwrite -Path `
        (Join-Path $RunRoot 'restoration-attestation.staged.json') `
        -Value $value -Label 'staged restoration attestation' `
        -DirectoryCapability $DirectoryCapability
    return $value
}

function Read-BoundedJson {
    param([string]$Path, [int]$MaximumBytes, [string]$Label)
    $full = [IO.Path]::GetFullPath($Path)
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $full)).TrimEnd('\', '/')
    $capability = $null
    try {
        # The file itself is the strict descendant anchor. Root and leaf stay
        # retained without share-delete while one native handle validates and
        # reads the exact bounded ordinary-file bytes.
        $capability = New-RetainedDirectoryCapability `
            -Path $parent -AnchorPath $full -Label $Label
        [byte[]]$bytes = $capability.ReadExistingFile(
            [IO.Path]::GetFileName($full), $MaximumBytes)
        if ($bytes.Length -lt 2) {
            throw "$Label length is outside its bound."
        }
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
        return $text | ConvertFrom-Json -ErrorAction Stop
    } catch {
        if ($_.Exception.Message -ceq "$Label length is outside its bound.") {
            throw
        }
        $failure = $_.Exception
        while ($null -ne $failure.InnerException) {
            $failure = $failure.InnerException
        }
        throw "$Label retained-handle JSON read failed: $($failure.Message)"
    } finally {
        if ($null -ne $capability) { $capability.Dispose() }
    }
}

function Read-BoundedJsonWithRetainedBytes {
    param(
        [string]$Path,
        [int]$MaximumBytes,
        [string]$Label,
        [object]$DirectoryCapability)
    $parent = Split-Path -Parent $Path
    Assert-RunDirectoryCapability $DirectoryCapability $parent
    try {
        [byte[]]$bytes = $DirectoryCapability.ReadExistingFile(
            [IO.Path]::GetFileName($Path), $MaximumBytes)
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
        $value = $text | ConvertFrom-Json
        return [pscustomobject]@{ Value = $value; Bytes = $bytes }
    } catch {
        $failure = $_.Exception
        while ($null -ne $failure.InnerException) {
            $failure = $failure.InnerException
        }
        throw "$Label retained-handle JSON read failed: $($failure.Message)"
    }
}

function Convert-PrefixedOutputToValues {
    param(
        [string[]]$Lines,
        [string]$Prefix,
        [string[]]$AllowedKeys,
        [string]$Label
    )
    if ($Lines.Count -gt 128) { throw "$Label exceeded its line bound." }
    $values = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::Ordinal)
    foreach ($line in $Lines) {
        if ($line.Length -gt 1024 -or
            $line -cnotmatch ('^' + [regex]::Escape($Prefix) +
                '(?<key>[a-z0-9-]+)=(?<value>[A-Za-z0-9_.:/-]{1,256})$')) {
            throw "$Label emitted a non-contract line."
        }
        $key = $Matches.key
        if ($AllowedKeys -cnotcontains $key -or $values.ContainsKey($key)) {
            throw "$Label emitted an unknown or duplicate key."
        }
        $values.Add($key, $Matches.value)
    }
    return $values
}

function Invoke-FirstObservationChecker {
    param([string]$CheckerPath, [string]$RunRoot)
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $CheckerPath --capture-root $RunRoot --scenario first-observation `
            --publication-stage prepublication 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($lines.Count -gt 128 -or
        [Text.Encoding]::UTF8.GetByteCount(($lines -join "`n")) -gt 65536) {
        throw 'First-observation checker output exceeded its bound.'
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Lines = $lines }
}

function Invoke-IndependentTransportWalker {
    param(
        [string]$WalkerPath,
        [string]$RunRoot,
        [Collections.Generic.Dictionary[string, string]]$CheckerValues,
        [bool]$Reconnect)
    $arguments = @(
        '-CaptureRoot', $RunRoot,
        '-BoundaryPayloadOrdinal', $CheckerValues['boundary-payload-ordinal'],
        '-BoundaryObservedOrdinal', $CheckerValues['boundary-observed-ordinal'],
        '-BoundaryDeliveryOrdinal', $CheckerValues['boundary-delivery-ordinal'],
        '-BoundaryByteOffset', $CheckerValues['boundary-byte-offset'],
        '-BoundaryBitOffset', $CheckerValues['boundary-bit-offset'],
        '-BoundarySourceSequence', $CheckerValues['boundary-source-sequence'],
        '-BoundarySourcePayloadBytes',
            $CheckerValues['boundary-source-payload-bytes'],
        '-BoundarySourcePayloadBits',
            $CheckerValues['boundary-source-payload-bits'],
        '-BoundaryNextUnconsumedBits',
            $CheckerValues['boundary-next-unconsumed-bits'],
        '-BoundaryReassembled', $CheckerValues['boundary-reassembled'],
        '-BoundaryDecompressed', $CheckerValues['boundary-decompressed'],
        '-CandidateBitWidth', $CheckerValues['candidate-bit-width'],
        '-FirstCandidate', $CheckerValues['first-candidate'])
    if ($Reconnect) {
        $arguments += @(
            '-GenerationBBoundaryPayloadOrdinal',
                $CheckerValues['generation-b-boundary-payload-ordinal'],
            '-GenerationBBoundaryObservedOrdinal',
                $CheckerValues['generation-b-boundary-observed-ordinal'],
            '-GenerationBBoundaryDeliveryOrdinal',
                $CheckerValues['generation-b-boundary-delivery-ordinal'],
            '-GenerationBBoundaryByteOffset',
                $CheckerValues['generation-b-boundary-byte-offset'],
            '-GenerationBBoundaryBitOffset',
                $CheckerValues['generation-b-boundary-bit-offset'],
            '-GenerationBBoundarySourceSequence',
                $CheckerValues['generation-b-boundary-source-sequence'],
            '-GenerationBBoundarySourcePayloadBytes',
                $CheckerValues['generation-b-boundary-source-payload-bytes'],
            '-GenerationBBoundarySourcePayloadBits',
                $CheckerValues['generation-b-boundary-source-payload-bits'],
            '-GenerationBBoundaryNextUnconsumedBits',
                $CheckerValues['generation-b-boundary-next-unconsumed-bits'],
            '-GenerationBBoundaryReassembled',
                $CheckerValues['generation-b-boundary-reassembled'],
            '-GenerationBBoundaryDecompressed',
                $CheckerValues['generation-b-boundary-decompressed'],
            '-GenerationBCandidateBitWidth',
                $CheckerValues['generation-b-candidate-bit-width'],
            '-GenerationBFirstCandidate',
                $CheckerValues['generation-b-first-candidate'])
    }
    $lines = @(& $WalkerPath @arguments 2>&1 |
        ForEach-Object { $_.ToString() })
    if ($lines.Count -gt 128 -or
        [Text.Encoding]::UTF8.GetByteCount(($lines -join "`n")) -gt 65536) {
        throw 'Independent transport walker output exceeded its bound.'
    }
    return ,$lines
}

function New-ReconnectCandidateObservation {
    param(
        [Collections.Generic.Dictionary[string, string]]$Values,
        [string]$Prefix)
    $candidate = $Values[$Prefix + 'first-candidate']
    $isPrefix = $candidate.StartsWith('bit-prefix:')
    [Int64]$numeric = [Int64]($candidate -replace '^bit-prefix:', '')
    return [ordered]@{
        observed = $true
        candidate_bit_width = [Int64]$Values[$Prefix + 'candidate-bit-width']
        numeric_candidate = $(if ($isPrefix) { $null } else { $numeric })
        bounded_bit_prefix = $(if ($isPrefix) { $numeric } else { $null })
        byte_aligned = $Values[$Prefix + 'boundary-byte-aligned'] -ceq 'true'
        body_consumed = $false
        semantic_category_assigned = $false
    }
}

function New-ReconnectFinalObservation {
    param([Collections.Generic.Dictionary[string, string]]$Values)
    $generations = [Collections.Generic.List[object]]::new()
    foreach ($identity in @(
            [pscustomobject]@{
                Label = 'a'; Ordinal = 1
                Process = 'owned_client_generation_a'
                Endpoint = 'research_client_generation_a'
                EndpointDistinct = $false; Shutdown = $true; Quiet = $true
            },
            [pscustomobject]@{
                Label = 'b'; Ordinal = 2
                Process = 'owned_client_generation_b'
                Endpoint = 'research_client_generation_b'
                EndpointDistinct = $true; Shutdown = $false; Quiet = $false
            })) {
        $prefix = 'generation-' + $identity.Label + '-'
        [void]$generations.Add([ordered]@{
            generation_ordinal = $identity.Ordinal
            profile_identity = $Values['profile']
            owned_client_process_role_identity = $identity.Process
            learned_client_endpoint_role_identity = $identity.Endpoint
            fresh_owned_client_process = $true
            learned_client_endpoint_observed = $true
            learned_client_endpoint_distinct_from_previous =
                $identity.EndpointDistinct
            first_observed_ordinal = [Int64]$Values[$prefix + 'first-observed-ordinal']
            last_observed_ordinal = [Int64]$Values[$prefix + 'last-observed-ordinal']
            connectionless_exchange_count =
                [Int64]$Values[$prefix + 'connectionless-exchanges']
            connect_observed = $true
            accept_observed = $true
            first_sequenced_packet_ordinal =
                [Int64]$Values[$prefix + 'first-sequenced-packet-ordinal']
            client_to_server_packet_count =
                [Int64]$Values[$prefix + 'client-to-server-packets']
            server_to_client_packet_count =
                [Int64]$Values[$prefix + 'server-to-client-packets']
            controlled_client_shutdown_observed = $identity.Shutdown
            retired_client_endpoint_quiet = $identity.Quiet
            exact_post_resource_boundary = [ordered]@{
                observed = $true
                replay_payload_ordinal =
                    [Int64]$Values[$prefix + 'boundary-payload-ordinal']
                corpus_observed_ordinal =
                    [Int64]$Values[$prefix + 'boundary-observed-ordinal']
                delivery_ordinal =
                    [Int64]$Values[$prefix + 'boundary-delivery-ordinal']
                byte_offset = [Int64]$Values[$prefix + 'boundary-byte-offset']
                bit_offset = [Int64]$Values[$prefix + 'boundary-bit-offset']
                source_payload_byte_count =
                    [Int64]$Values[$prefix + 'boundary-source-payload-bytes']
                source_payload_bit_count =
                    [Int64]$Values[$prefix + 'boundary-source-payload-bits']
                next_unconsumed_bit_count =
                    [Int64]$Values[$prefix + 'boundary-next-unconsumed-bits']
            }
            candidate_observation = New-ReconnectCandidateObservation `
                -Values $Values -Prefix $prefix
        })
    }
    return [ordered]@{
        schema = 'hlclient.stock-runtime-reconnect-observation.v1'
        connection_generation_count = 2
        exact_boundary_count = 2
        runtime_candidate_count = 2
        generation_distinct = $true
        candidate_conflict = $false
        guard_continuity = $true
        server_continuity = $true
        relay_continuity = $true
        cleanup_exact = $true
        restoration_exact = $true
        candidate_body_consumed = $false
        candidate_semantic_category_assigned = $false
        retired_generation_a_tail_sink = 'routing_only'
        retired_generation_a_server_tail_packet_count =
            [Int64]$Values['retired-generation-a-server-tail-packets']
        generation_b_sequenced_after_fresh_accept = $true
        generations = $generations.ToArray()
    }
}

function Get-RestorationSelfTestExternalObservation {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSIsContainer) {
        return 'directory|{0}|{1}|{2}' -f $item.CreationTimeUtc.Ticks,
            $item.LastWriteTimeUtc.Ticks, [Int64]$item.Attributes
    }
    return 'file|{0}|{1}|{2}|{3}|{4}' -f $item.Length,
        $item.CreationTimeUtc.Ticks, $item.LastWriteTimeUtc.Ticks,
        [Int64]$item.Attributes, (Get-FileSha256 $Path)
}

if ($PSCmdlet.ParameterSetName -eq 'DirectoryCapabilityBootstrap') {
    Initialize-RestorationDirectoryCapabilityNative
    Write-Output '[stock-runtime-capture] directory-capability=initialized'
    Write-Output '[stock-runtime-capture] files-written=0'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] result=success'
    return
}

if ($PSCmdlet.ParameterSetName -eq 'FunctionalResearchProjectionSelfTest') {
    $systemTemporaryRoot =
        [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $testRoot = [IO.Path]::GetFullPath((Join-Path $systemTemporaryRoot (
        'hlclient-functional-research-projection-' +
        [Guid]::NewGuid().ToString('N'))))
    $researchRoot = Join-Path $testRoot 'research'
    $sourceRoot = Join-Path $testRoot 'steam\steamapps\common\Half-Life'
    $script:AppManifestPath = Join-Path $testRoot `
        'steam\steamapps\appmanifest_70.acf'

    function Write-FunctionalProjectionFixtureFile {
        param([string]$Base, [string]$Relative, [string]$Content)
        $path = Join-Path $Base ($Relative.Replace('/', '\'))
        [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
        [IO.File]::WriteAllText(
            $path, $Content, [Text.UTF8Encoding]::new($false))
    }

    try {
        [IO.Directory]::CreateDirectory(
            (Split-Path -Parent $script:AppManifestPath)) | Out-Null
        [IO.File]::WriteAllText(
            $script:AppManifestPath, 'fixture',
            [Text.UTF8Encoding]::new($false))
        $projectionFiles = @(
            'hl.exe', 'hlds.exe', 'valve/cl_dlls/client.dll',
            'valve/dlls/hl.dll', 'valve/liblist.gam',
            'valve/maps/boot_camp.bsp', 'steam_appid.txt',
            'valve/config.cfg', 'valve/voice_ban.dt',
            'platform/config/InGameDialogConfig.vdf',
            'platform/config/ServerBrowser.vdf')
        foreach ($relative in $projectionFiles) {
            $content = if ($relative -ceq 'steam_appid.txt') { "70`r`n" }
                else { 'same:' + $relative }
            Write-FunctionalProjectionFixtureFile `
                $sourceRoot $relative $content
            Write-FunctionalProjectionFixtureFile `
                $researchRoot $relative $content
        }

        # This represents a file installed after the v3 research copy was
        # prepared. It must not retroactively become part of that projection.
        Write-FunctionalProjectionFixtureFile $sourceRoot `
            'valve/post-preparation-source-only.cfg' 'later-source-file'
        $accepted = Assert-FunctionalResearchProjection `
            $researchRoot @(Get-BoundedItems $researchRoot)
        if ($accepted.Status -cne 'prepared_projection_content_verified') {
            throw 'Later source-only file did not preserve the prepared projection.'
        }

        # Exercise the real App ID contract; generic fixture text is not a
        # valid marker, even when its bytes match the source installation.
        Write-FunctionalProjectionFixtureFile $researchRoot 'steam_appid.txt' '480'
        $wrongAppIdRejected = $false
        try {
            [void](Assert-FunctionalResearchProjection `
                $researchRoot @(Get-BoundedItems $researchRoot))
        } catch {
            if ($_.Exception.Message -ceq
                    'Functional research projection has an invalid local App ID marker.') {
                $wrongAppIdRejected = $true
            } else { throw }
        }
        if (-not $wrongAppIdRejected) { throw 'Wrong local App ID was not rejected.' }
        Write-FunctionalProjectionFixtureFile $researchRoot 'steam_appid.txt' "70`r`n"

        Write-FunctionalProjectionFixtureFile $researchRoot `
            'valve/unapproved-research-only.cfg' 'research-only-file'
        $researchOnlyRejected = $false
        try {
            [void](Assert-FunctionalResearchProjection `
                $researchRoot @(Get-BoundedItems $researchRoot))
        } catch {
            if ($_.Exception.Message -ceq
                    'Functional research projection has an unapproved extra path.') {
                $researchOnlyRejected = $true
            } else { throw }
        }
        if (-not $researchOnlyRejected) {
            throw 'Unapproved research-only file was not rejected.'
        }
        [IO.File]::Delete((Join-Path $researchRoot `
            'valve\unapproved-research-only.cfg'))

        Write-FunctionalProjectionFixtureFile $researchRoot 'hl.exe' `
            'changed:hl.exe'
        $changedCriticalRejected = $false
        try {
            [void](Assert-FunctionalResearchProjection `
                $researchRoot @(Get-BoundedItems $researchRoot))
        } catch {
            if ($_.Exception.Message -ceq
                    'Functional research projection immutable content changed.') {
                $changedCriticalRejected = $true
            } else { throw }
        }
        if (-not $changedCriticalRejected) {
            throw 'Changed critical research binary was not rejected.'
        }
        Write-FunctionalProjectionFixtureFile $researchRoot 'hl.exe' 'same:hl.exe'

        $requiredMapPath = Join-Path $researchRoot 'valve\maps\boot_camp.bsp'
        [IO.File]::Delete($requiredMapPath)
        $missingCriticalRejected = $false
        try {
            [void](Assert-FunctionalResearchProjection `
                $researchRoot @(Get-BoundedItems $researchRoot))
        } catch {
            if ($_.Exception.Message -ceq
                    'Functional research projection critical identity changed.') {
                $missingCriticalRejected = $true
            } else { throw }
        }
        if (-not $missingCriticalRejected) {
            throw 'Missing required research map was not rejected.'
        }
        Write-FunctionalProjectionFixtureFile $researchRoot `
            'valve/maps/boot_camp.bsp' 'same:valve/maps/boot_camp.bsp'

        Write-FunctionalProjectionFixtureFile $researchRoot `
            'valve/config.cfg' 'allowed-runtime-mutation'
        $mutableAccepted = Assert-FunctionalResearchProjection `
            $researchRoot @(Get-BoundedItems $researchRoot)
        if ($mutableAccepted.Status -cne 'prepared_projection_content_verified') {
            throw 'Approved functional runtime mutation was not accepted.'
        }
        Write-FunctionalProjectionFixtureFile $researchRoot `
            'valve/config.cfg' 'same:valve/config.cfg'
        if ((Get-FileSha256 (Join-Path $researchRoot 'valve\config.cfg')) -cne
                (Get-FileSha256 (Join-Path $sourceRoot 'valve\config.cfg'))) {
            throw 'Approved functional runtime mutation was not restored exactly.'
        }

        Write-Output '[stock-runtime-projection-test] later-source-addition=accepted'
        Write-Output '[stock-runtime-projection-test] wrong-local-app-id=rejected'
        Write-Output '[stock-runtime-projection-test] research-only-addition=rejected'
        Write-Output '[stock-runtime-projection-test] changed-critical-binary=rejected'
        Write-Output '[stock-runtime-projection-test] missing-required-map=rejected'
        Write-Output '[stock-runtime-projection-test] mutable-path=accepted-and-restored'
        Write-Output '[stock-runtime-projection-test] strict-policy=separate'
        Write-Output '[stock-runtime-projection-test] stock-launch=absent'
        Write-Output '[stock-runtime-projection-test] result=success'
    } finally {
        if (Test-Path -LiteralPath $testRoot) {
            Remove-SafeTree $testRoot $systemTemporaryRoot
        }
    }
    return
}

if ($PSCmdlet.ParameterSetName -eq 'FunctionalPublicationRoundtripSelfTest') {
    if ($env:OS -cne 'Windows_NT') {
        throw 'Functional publication roundtrip requires Windows.'
    }
    $captureFixtureTool = Resolve-TrustedRepositoryTool $CaptureToolPath `
        'hlclient_stock_runtime_capture.exe' 'stock runtime capture fixture'
    $functionalChecker = Resolve-TrustedRepositoryTool $CheckerPath `
        'hlclient_stock_runtime_check.exe' 'stock runtime functional checker'
    $functionalHlclient = Resolve-TrustedRepositoryTool $HlclientPath `
        'hlclient.exe' 'functional replay application'
    $lifecycleOrchestrator = Resolve-TrustedRepositoryTool $OrchestratorPath `
        'hlclient_stock_runtime_orchestrator.exe' `
        'functional lifecycle orchestrator'
    $lifecycleFakeClient = Resolve-TrustedRepositoryTool $FakeClientPath `
        'hlclient_stock_runtime_fake_client.exe' `
        'functional lifecycle fake client'
    $lifecycleFakeServer = Resolve-TrustedRepositoryTool $FakeServerPath `
        'hlclient_stock_runtime_fake_server.exe' `
        'functional lifecycle fake server'
    [Collections.Generic.List[object]]$createdRuns = @()

    function Invoke-FunctionalPublicationTool {
        param([string]$Path, [string[]]$Arguments, [string]$Label)
        $savedPreference = $ErrorActionPreference
        $locationPushed = $false
        try {
            $ErrorActionPreference = 'Continue'
            Push-Location -LiteralPath $repositoryRoot
            $locationPushed = $true
            [string[]]$lines = @(& $Path @Arguments 2>&1 |
                    ForEach-Object { $_.ToString() })
            $exitCode = $LASTEXITCODE
        } finally {
            if ($locationPushed) { Pop-Location }
            $ErrorActionPreference = $savedPreference
        }
        if ($lines.Count -gt 256 -or
            [Text.Encoding]::UTF8.GetByteCount(($lines -join "`n")) -gt 131072) {
            throw "$Label output exceeded its bound."
        }
        return [pscustomobject]@{ ExitCode = $exitCode; Lines = $lines }
    }

    function New-FunctionalProducerFixture {
        param(
            [int]$AuxiliaryCount,
            [string]$Role = 'functional-runtime-capture',
            [switch]$ChangedAuxiliary,
            [switch]$AuxiliaryBeforeOwning)
        $parent = if ($Role -ceq 'functional-runtime-capture') {
            $requiredFunctionalRuntimeCaptureRoot
        } else { $requiredOutputRoot }
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
            [IO.Directory]::CreateDirectory($parent) | Out-Null
        }
        $runId = [Guid]::NewGuid().ToString('N')
        $runRoot = Join-Path $parent $runId
        [IO.Directory]::CreateDirectory($runRoot) | Out-Null
        [void]$createdRuns.Add([pscustomobject]@{
                Root = $runRoot
                Parent = $parent
            })
        $arguments = @(
            '--validate-functional-publication-fixture',
            '--output-run-root', $runRoot,
            '--output-role', $Role,
            '--fixture-auxiliary-count', [string]$AuxiliaryCount,
            '--precreated-empty-run-root')
        if ($ChangedAuxiliary) { $arguments += '--fixture-change-auxiliary' }
        if ($AuxiliaryBeforeOwning) {
            $arguments += '--fixture-auxiliary-before-owning'
        }
        $result = Invoke-FunctionalPublicationTool `
            $captureFixtureTool $arguments 'functional producer fixture'
        return [pscustomobject]@{
            RunId = $runId
            RunRoot = $runRoot
            Result = $result
        }
    }

    function Add-FunctionalFixturePublicationInputs {
        param([string]$RunRoot)
        [IO.Directory]::CreateDirectory((Join-Path $RunRoot 'logs')) | Out-Null
        $capability = New-RunDirectoryCapability $RunRoot
        try {
            $version = [ordered]@{
                schema = 'hlclient.stock-runtime-version-observation.v1'
                map_category = 'boot_camp'
                client_file_version = '1.1.1.1'
                client_pe_machine = 'x86'
                client_signature = 'valid'
                client_profile_fingerprint = ('a' * 64)
                server_launcher_version = '4.1.1.1'
                server_pe_machine = 'x86'
                server_signature = 'valid'
                server_profile_fingerprint = ('b' * 64)
                steam_app_id = 70
                steam_build_id = 15961492
                server_engine_version = '1.1.2.2'
                protocol = 48
                server_build = 10210
                evidence_status = 'observed'
            }
            $isolation = [ordered]@{
                schema = 'hlclient.stock-runtime-isolation-attestation.v1'
                session_type = 'dynamic'
                persistent_rule_count = 0
                ipv4_loopback = 'allowed'
                ipv6_loopback = 'capability_unavailable'
                non_loopback_canary = 'denied_os_classified'
                cleanup_status = 'exact'
                evidence_status = 'observed'
            }
            Write-AtomicJsonNoOverwrite `
                (Join-Path $RunRoot 'version-observation.staged.json') `
                $version 'functional fixture version observation' $capability
            Write-AtomicJsonNoOverwrite `
                (Join-Path $RunRoot 'isolation-attestation.staged.json') `
                $isolation 'functional fixture isolation attestation' $capability
        } finally {
            $capability.Dispose()
        }
    }

    function Complete-FunctionalFixturePublication {
        param([object]$Fixture)
        Add-FunctionalFixturePublicationInputs $Fixture.RunRoot
        $fixtureLifecycle = [ordered]@{
            'lifecycle-clock' = 'scaled-steady-clock'
            'lifecycle-unit' = 'milliseconds'
            'requested-maximum-duration-ms' = '90000'
            'required-runtime-interval-ms' = '15000'
            'client-map-entry-observed-ms' = '15000'
            'functional-interval-completed-ms' = '30000'
            'shutdown-requested-ms' = '30000'
            'relay-stop-requested-ms' = '30001'
            'relay-finalization-completed-ms' = '30002'
            'stop-reason' = 'functional-interval-complete'
            'stock-shutdown-method' = 'owned-process-terminate'
        }
        $capability = New-RunDirectoryCapability $Fixture.RunRoot
        try {
            $publication = Publish-FunctionalRuntimeCaptureArtifacts `
                $Fixture.RunRoot $Fixture.RunId $capability `
                $true $true $true ('c' * 64) ('c' * 64) `
                $true $true $true $true valve boot_camp 90 `
                'steam-hlds-10210-no-mode-banner-v1' $fixtureLifecycle
            if (-not [bool]$publication.ManifestPublished) {
                throw 'Functional fixture manifest was not published.'
            }
        } finally {
            $capability.Dispose()
        }
    }

    function Assert-FunctionalLoaderAndReplay {
        param([object]$Fixture, [int]$AuxiliaryCount)
        $checked = Invoke-FunctionalPublicationTool $functionalChecker @(
            '--capture-root', $Fixture.RunRoot,
            '--scenario', 'netchan',
            '--publication-stage', 'functional') `
            'functional corpus checker'
        if ($checked.ExitCode -ne 0 -or
            $checked.Lines -cnotcontains '[stock-runtime] transport-valid=true' -or
            $checked.Lines -cnotcontains (
                '[stock-runtime] auxiliary-observed=' + $AuxiliaryCount) -or
            $checked.Lines -cnotcontains '[stock-runtime] delivered-datagrams=5' -or
            $checked.Lines -cnotcontains '[stock-runtime] result=netchan') {
            throw ('Production loader or transport replay rejected a published ' +
                'fixture: exit=' + $checked.ExitCode + '; output=' +
                ($checked.Lines -join '|'))
        }
        return $checked
    }

    $stockNames = @('hl', 'hlds', 'Steam', 'hlfx')
    $stockBefore = @(Get-Process -Name $stockNames -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Id | Sort-Object)
    try {
        $lifecycleRunId = [Guid]::NewGuid().ToString('N')
        $lifecycleRunRoot = Join-Path $requiredFunctionalRuntimeCaptureRoot `
            $lifecycleRunId
        [void]$createdRuns.Add([pscustomobject]@{
                Root = $lifecycleRunRoot
                Parent = $requiredFunctionalRuntimeCaptureRoot
            })
        $firstSocket = [Net.Sockets.UdpClient]::new(0)
        try {
            $lifecycleRelayPort = `
                ([Net.IPEndPoint]$firstSocket.Client.LocalEndPoint).Port
        } finally { $firstSocket.Dispose() }
        do {
            $secondSocket = [Net.Sockets.UdpClient]::new(0)
            try {
                $lifecycleServerPort = `
                    ([Net.IPEndPoint]$secondSocket.Client.LocalEndPoint).Port
            } finally { $secondSocket.Dispose() }
        } while ($lifecycleServerPort -eq $lifecycleRelayPort)
        $lifecycleClock = [Diagnostics.Stopwatch]::StartNew()
        $lifecycle = Invoke-FunctionalPublicationTool `
            $lifecycleOrchestrator @(
                '--validate-functional-lifecycle',
                '--run-root', $lifecycleRunRoot,
                '--client', $lifecycleFakeClient,
                '--server', $lifecycleFakeServer,
                '--relay', $captureFixtureTool,
                '--game', 'valve', '--map', 'boot_camp',
                '--scenario', 'idle-runtime',
                '--output-role', 'functional-runtime-capture',
                '--relay-port', [string]$lifecycleRelayPort,
                '--server-port', [string]$lifecycleServerPort,
                '--max-duration-seconds', '90') `
            'functional lifecycle orchestration fixture'
        $lifecycleClock.Stop()
        foreach ($requiredLine in @(
                '[functional-lifecycle-test] requested-maximum-duration-ms=90000',
                '[functional-lifecycle-test] required-runtime-interval-ms=15000',
                '[functional-lifecycle-test] stop-reason=functional-interval-complete',
                '[functional-lifecycle-test] stock-shutdown-method=owned-process-terminate',
                '[functional-lifecycle-test] peer-shutdown-after-relay-finalization=true',
                '[functional-lifecycle-test] result=success')) {
            if ($lifecycle.Lines -cnotcontains $requiredLine) {
                throw "Functional lifecycle output is missing: $requiredLine"
            }
        }
        if ($lifecycle.ExitCode -ne 0 -or
            $lifecycleClock.ElapsedMilliseconds -ge 10000 -or
            -not (Test-Path -LiteralPath (Join-Path $lifecycleRunRoot `
                    'transport-journal.jsonl') -PathType Leaf) -or
            -not (Test-Path -LiteralPath (Join-Path $lifecycleRunRoot `
                    'capture-metadata.json') -PathType Leaf)) {
            throw 'Production functional lifecycle did not stop and flush early.'
        }
        $lifecycleFixture = [pscustomobject]@{
            RunId = $lifecycleRunId
            RunRoot = $lifecycleRunRoot
        }
        $lifecycleMetadata = Get-Content -Raw -LiteralPath `
            (Join-Path $lifecycleRunRoot 'capture-metadata.json') |
            ConvertFrom-Json
        if ([Int64]$lifecycleMetadata.auxiliary_observed_datagrams -ne 2 -or
            [Int64]$lifecycleMetadata.emitted_datagrams -ne
                ([Int64]$lifecycleMetadata.observed_datagrams - 2)) {
            throw 'Lifecycle auxiliary observations were delivered or lost.'
        }
        Complete-FunctionalFixturePublication $lifecycleFixture
        $lifecycleLoaded = Invoke-FunctionalPublicationTool `
            $functionalChecker @(
                '--capture-root', $lifecycleRunRoot,
                '--scenario', 'transport',
                '--publication-stage', 'functional') `
            'functional lifecycle corpus loader'
        if ($lifecycleLoaded.ExitCode -ne 0 -or
            $lifecycleLoaded.Lines -cnotcontains `
                '[stock-runtime] transport-valid=true') {
            throw 'Lifecycle producer output was rejected by the production loader.'
        }

        $limitRunId = [Guid]::NewGuid().ToString('N')
        $limitRunRoot = Join-Path $requiredFunctionalRuntimeCaptureRoot $limitRunId
        [void]$createdRuns.Add([pscustomobject]@{
                Root = $limitRunRoot
                Parent = $requiredFunctionalRuntimeCaptureRoot
            })
        $firstSocket = [Net.Sockets.UdpClient]::new(0)
        try {
            $limitRelayPort = `
                ([Net.IPEndPoint]$firstSocket.Client.LocalEndPoint).Port
        } finally { $firstSocket.Dispose() }
        do {
            $secondSocket = [Net.Sockets.UdpClient]::new(0)
            try {
                $limitServerPort = `
                    ([Net.IPEndPoint]$secondSocket.Client.LocalEndPoint).Port
            } finally { $secondSocket.Dispose() }
        } while ($limitServerPort -eq $limitRelayPort)
        $limitLifecycle = Invoke-FunctionalPublicationTool `
            $lifecycleOrchestrator @(
                '--validate-functional-lifecycle',
                '--validate-functional-lifecycle-limit',
                '--run-root', $limitRunRoot,
                '--client', $lifecycleFakeClient,
                '--server', $lifecycleFakeServer,
                '--relay', $captureFixtureTool,
                '--game', 'valve', '--map', 'boot_camp',
                '--scenario', 'idle-runtime',
                '--output-role', 'functional-runtime-capture',
                '--relay-port', [string]$limitRelayPort,
                '--server-port', [string]$limitServerPort,
                '--max-duration-seconds', '90',
                '--max-client-packets', '4') `
            'functional lifecycle limit fixture'
        foreach ($requiredLine in @(
                '[functional-lifecycle-limit-test] stop-reason=client-packet-limit',
                '[functional-lifecycle-limit-test] journal=preserved',
                '[functional-lifecycle-limit-test] complete-manifest=absent',
                '[functional-lifecycle-limit-test] result=success')) {
            if ($limitLifecycle.Lines -cnotcontains $requiredLine) {
                throw "Functional lifecycle limit output is missing: $requiredLine"
            }
        }
        $limitMetadata = Get-Content -Raw -LiteralPath `
            (Join-Path $limitRunRoot 'capture-metadata.json') | ConvertFrom-Json
        $limitJournal = @(Get-Content -LiteralPath `
            (Join-Path $limitRunRoot 'transport-journal.jsonl') |
            ForEach-Object { $_ | ConvertFrom-Json })
        $limitRaw = @(Get-ChildItem -LiteralPath (Join-Path $limitRunRoot 'raw') `
            -File)
        $limitObservedOrdinals = @($limitJournal | ForEach-Object {
                [Int64]$_.observed_ordinal
            })
        if ($limitLifecycle.ExitCode -ne 0 -or
            [bool]$limitMetadata.bounded_transport_complete -or
            [Int64]$limitMetadata.client_packets -ne 4 -or
            $limitJournal.Count -lt 1 -or
            $limitJournal.Count -ne [Int64]$limitMetadata.observed_datagrams -or
            $limitRaw.Count -ne $limitJournal.Count -or
            (Compare-Object -ReferenceObject @(0..($limitJournal.Count - 1)) `
                -DifferenceObject $limitObservedOrdinals) -or
            (Test-Path -LiteralPath (Join-Path $limitRunRoot `
                    'functional-runtime-capture.json'))) {
            throw 'Controlled capture limit did not preserve an exact incomplete journal.'
        }
        $limitLoader = Invoke-FunctionalPublicationTool `
            $functionalChecker @(
                '--capture-root', $limitRunRoot,
                '--scenario', 'transport',
                '--publication-stage', 'functional') `
            'functional incomplete limit corpus rejection'
        if ($limitLoader.ExitCode -eq 0) {
            throw 'Functional loader accepted a limit-truncated capture as complete.'
        }

        $writerFailureRunId = [Guid]::NewGuid().ToString('N')
        $writerFailureRunRoot = Join-Path `
            $requiredFunctionalRuntimeCaptureRoot $writerFailureRunId
        [void]$createdRuns.Add([pscustomobject]@{
                Root = $writerFailureRunRoot
                Parent = $requiredFunctionalRuntimeCaptureRoot
            })
        $firstSocket = [Net.Sockets.UdpClient]::new(0)
        try {
            $writerFailureRelayPort =
                ([Net.IPEndPoint]$firstSocket.Client.LocalEndPoint).Port
        } finally { $firstSocket.Dispose() }
        do {
            $secondSocket = [Net.Sockets.UdpClient]::new(0)
            try {
                $writerFailureServerPort =
                    ([Net.IPEndPoint]$secondSocket.Client.LocalEndPoint).Port
            } finally { $secondSocket.Dispose() }
        } while ($writerFailureServerPort -eq $writerFailureRelayPort)
        $writerFailureLifecycle = Invoke-FunctionalPublicationTool `
            $lifecycleOrchestrator @(
                '--validate-functional-lifecycle',
                '--validate-functional-lifecycle-writer-failure',
                '--run-root', $writerFailureRunRoot,
                '--client', $lifecycleFakeClient,
                '--server', $lifecycleFakeServer,
                '--relay', $captureFixtureTool,
                '--game', 'valve', '--map', 'boot_camp',
                '--scenario', 'idle-runtime',
                '--output-role', 'functional-runtime-capture',
                '--relay-port', [string]$writerFailureRelayPort,
                '--server-port', [string]$writerFailureServerPort,
                '--max-duration-seconds', '90') `
            'functional lifecycle writer failure fixture'
        foreach ($requiredLine in @(
                '[functional-lifecycle-writer-failure-test] writer-error=distinct',
                '[functional-lifecycle-writer-failure-test] metadata=published-incomplete',
                '[functional-lifecycle-writer-failure-test] complete-manifest=absent',
                '[functional-lifecycle-writer-failure-test] peer-shutdown-after-relay-finalization=true',
                '[functional-lifecycle-writer-failure-test] result=success')) {
            if ($writerFailureLifecycle.Lines -cnotcontains $requiredLine) {
                throw "Functional writer failure output is missing: $requiredLine"
            }
        }
        $writerFailureMetadata = Get-Content -Raw -LiteralPath `
            (Join-Path $writerFailureRunRoot 'capture-metadata.json') |
            ConvertFrom-Json
        if ($writerFailureLifecycle.ExitCode -ne 0 -or
            [bool]$writerFailureMetadata.bounded_transport_complete -or
            -not (Test-Path -LiteralPath (Join-Path $writerFailureRunRoot `
                    'transport-journal.jsonl') -PathType Container) -or
            (Test-Path -LiteralPath (Join-Path $writerFailureRunRoot `
                    'functional-runtime-capture.json'))) {
            throw 'Writer failure was promoted or did not retain incomplete metadata.'
        }
        $writerFailureLoader = Invoke-FunctionalPublicationTool `
            $functionalChecker @(
                '--capture-root', $writerFailureRunRoot,
                '--scenario', 'transport',
                '--publication-stage', 'functional') `
            'functional writer failure corpus rejection'
        if ($writerFailureLoader.ExitCode -eq 0) {
            throw 'Functional loader accepted a writer-failed capture as complete.'
        }

        foreach ($injectedFailure in @('receive', 'send')) {
            $injectedRunId = [Guid]::NewGuid().ToString('N')
            $injectedRunRoot = Join-Path `
                $requiredFunctionalRuntimeCaptureRoot $injectedRunId
            [void]$createdRuns.Add([pscustomobject]@{
                    Root = $injectedRunRoot
                    Parent = $requiredFunctionalRuntimeCaptureRoot
                })
            $firstSocket = [Net.Sockets.UdpClient]::new(0)
            try {
                $injectedRelayPort =
                    ([Net.IPEndPoint]$firstSocket.Client.LocalEndPoint).Port
            } finally { $firstSocket.Dispose() }
            do {
                $secondSocket = [Net.Sockets.UdpClient]::new(0)
                try {
                    $injectedServerPort =
                        ([Net.IPEndPoint]$secondSocket.Client.LocalEndPoint).Port
                } finally { $secondSocket.Dispose() }
            } while ($injectedServerPort -eq $injectedRelayPort)
            $injectedLifecycle = Invoke-FunctionalPublicationTool `
                $lifecycleOrchestrator @(
                    '--validate-functional-lifecycle',
                    '--validate-functional-lifecycle-injected-failure',
                    $injectedFailure,
                    '--run-root', $injectedRunRoot,
                    '--client', $lifecycleFakeClient,
                    '--server', $lifecycleFakeServer,
                    '--relay', $captureFixtureTool,
                    '--game', 'valve', '--map', 'boot_camp',
                    '--scenario', 'idle-runtime',
                    '--output-role', 'functional-runtime-capture',
                    '--relay-port', [string]$injectedRelayPort,
                    '--server-port', [string]$injectedServerPort,
                    '--max-duration-seconds', '90') `
                "functional lifecycle injected $injectedFailure fixture"
            foreach ($requiredLine in @(
                    "[functional-lifecycle-injected-failure-test] variant=$injectedFailure",
                    '[functional-lifecycle-injected-failure-test] primary-error=retained',
                    '[functional-lifecycle-injected-failure-test] journal=published-incomplete',
                    '[functional-lifecycle-injected-failure-test] metadata=published-incomplete',
                    '[functional-lifecycle-injected-failure-test] complete-manifest=absent',
                    '[functional-lifecycle-injected-failure-test] peers-alive-through-relay-terminal=true',
                    '[functional-lifecycle-injected-failure-test] result=success')) {
                if ($injectedLifecycle.Lines -cnotcontains $requiredLine) {
                    throw "Injected failure output is missing: $requiredLine"
                }
            }
            $injectedMetadata = Get-Content -Raw -LiteralPath `
                (Join-Path $injectedRunRoot 'capture-metadata.json') |
                ConvertFrom-Json
            $injectedJournal = @(Get-Content -LiteralPath `
                (Join-Path $injectedRunRoot 'transport-journal.jsonl') |
                ForEach-Object { $_ | ConvertFrom-Json })
            if ($injectedLifecycle.ExitCode -ne 0 -or
                [bool]$injectedMetadata.bounded_transport_complete -or
                $injectedJournal.Count -lt 1 -or
                (Test-Path -LiteralPath (Join-Path $injectedRunRoot `
                        'functional-runtime-capture.json'))) {
                throw "Injected $injectedFailure failure lost its incomplete corpus."
            }
            if ($injectedFailure -ceq 'send' -and
                @($injectedJournal[0].emitted_ordinals).Count -ne 0) {
                throw 'Injected send failure fabricated an emitted ordinal.'
            }
            $injectedLoader = Invoke-FunctionalPublicationTool `
                $functionalChecker @(
                    '--capture-root', $injectedRunRoot,
                    '--scenario', 'transport',
                    '--publication-stage', 'functional') `
                "functional injected $injectedFailure corpus rejection"
            if ($injectedLoader.ExitCode -eq 0) {
                throw "Functional loader accepted injected $injectedFailure failure."
            }
        }

        foreach ($auxiliaryCount in @(0, 1, 3)) {
            $fixture = New-FunctionalProducerFixture $auxiliaryCount
            if ($fixture.Result.ExitCode -ne 0) {
                throw 'Production capture writer fixture failed.'
            }
            Complete-FunctionalFixturePublication $fixture
            $checked = Assert-FunctionalLoaderAndReplay `
                $fixture $auxiliaryCount
            if ($checked.Lines -cnotcontains (
                    '[stock-runtime] observed-datagrams=' +
                    (5 + $auxiliaryCount)) -or
                $fixture.Result.Lines -cnotcontains (
                    '[stock-runtime-capture-fixture] auxiliary-observed=' +
                    $auxiliaryCount)) {
                throw 'Functional observed/auxiliary cardinality is inconsistent.'
            }
        }

        $strict = New-FunctionalProducerFixture 1 'normal-campaign-run'
        if ($strict.Result.ExitCode -ne 13 -or
            (Test-Path -LiteralPath (Join-Path $strict.RunRoot `
                    'functional-runtime-capture.json'))) {
            throw 'Strict publication accepted functional auxiliary traffic.'
        }
        $changed = New-FunctionalProducerFixture 1 `
            -ChangedAuxiliary
        if ($changed.Result.ExitCode -ne 13 -or
            $changed.Result.Lines -cnotcontains `
                '[stock-runtime-capture-fixture] auxiliary-observed=0') {
            throw 'Changed auxiliary query was not rejected.'
        }
        $beforeOwning = New-FunctionalProducerFixture 1 `
            -AuxiliaryBeforeOwning
        if ($beforeOwning.Result.ExitCode -ne 13 -or
            $beforeOwning.Result.Lines -cnotcontains `
                '[stock-runtime-capture-fixture] auxiliary-observed=0') {
            throw 'Auxiliary query captured an owning endpoint.'
        }

        $withoutManifest = New-FunctionalProducerFixture 0
        Add-FunctionalFixturePublicationInputs $withoutManifest.RunRoot
        $missingManifestCheck = Invoke-FunctionalPublicationTool `
            $functionalChecker @('--capture-root', $withoutManifest.RunRoot,
                '--scenario', 'transport', '--publication-stage', 'functional') `
            'missing functional manifest check'
        if ($missingManifestCheck.ExitCode -eq 0) {
            throw 'Functional loader accepted a missing final manifest.'
        }

        $missingJournal = New-FunctionalProducerFixture 0
        [IO.File]::Delete((Join-Path $missingJournal.RunRoot `
                'transport-journal.jsonl'))
        Add-FunctionalFixturePublicationInputs $missingJournal.RunRoot
        $capability = New-RunDirectoryCapability $missingJournal.RunRoot
        $publicationFailed = $false
        try {
            try {
                [void](Publish-FunctionalRuntimeCaptureArtifacts `
                    $missingJournal.RunRoot $missingJournal.RunId $capability `
                    $true $true $true ('c' * 64) ('c' * 64) `
                    $true $true $true $true valve boot_camp 90 `
                    'steam-hlds-10210-no-mode-banner-v1' ([ordered]@{
                        'lifecycle-clock' = 'scaled-steady-clock'
                        'lifecycle-unit' = 'milliseconds'
                        'requested-maximum-duration-ms' = '90000'
                        'required-runtime-interval-ms' = '15000'
                        'client-map-entry-observed-ms' = '15000'
                        'functional-interval-completed-ms' = '30000'
                        'shutdown-requested-ms' = '30000'
                        'relay-stop-requested-ms' = '30001'
                        'relay-finalization-completed-ms' = '30002'
                        'stop-reason' = 'functional-interval-complete'
                        'stock-shutdown-method' = 'owned-process-terminate'
                    }))
            } catch { $publicationFailed = $true }
        } finally { $capability.Dispose() }
        if (-not $publicationFailed -or
            (Test-Path -LiteralPath (Join-Path $missingJournal.RunRoot `
                    'functional-runtime-capture.json'))) {
            throw 'Finalization failure left a false complete manifest.'
        }

        $rawMismatch = New-FunctionalProducerFixture 0
        Complete-FunctionalFixturePublication $rawMismatch
        $rawPath = Join-Path $rawMismatch.RunRoot 'raw\00000000-c2s.bin'
        $rawBytes = [IO.File]::ReadAllBytes($rawPath)
        $rawBytes[0] = $rawBytes[0] -bxor 1
        [IO.File]::WriteAllBytes($rawPath, $rawBytes)
        $rawMismatchCheck = Invoke-FunctionalPublicationTool `
            $functionalChecker @('--capture-root', $rawMismatch.RunRoot,
                '--scenario', 'transport', '--publication-stage', 'functional') `
            'raw mismatch check'
        if ($rawMismatchCheck.ExitCode -eq 0) {
            throw 'Functional loader accepted changed raw bytes.'
        }

        $semanticFailure = New-FunctionalProducerFixture 1
        Complete-FunctionalFixturePublication $semanticFailure
        $beforeCaptureHash = Get-FileSha256 `
            (Join-Path $semanticFailure.RunRoot 'capture-metadata.json')
        $beforeJournalHash = Get-FileSha256 `
            (Join-Path $semanticFailure.RunRoot 'transport-journal.jsonl')
        $application = Invoke-FunctionalPublicationTool $functionalHlclient @(
            '--renderer', 'null', '--runtime-replay-capture',
            $semanticFailure.RunRoot) 'functional replay application'
        if ($application.ExitCode -eq 0 -or
            $beforeCaptureHash -cne (Get-FileSha256 `
                (Join-Path $semanticFailure.RunRoot 'capture-metadata.json')) -or
            $beforeJournalHash -cne (Get-FileSha256 `
                (Join-Path $semanticFailure.RunRoot 'transport-journal.jsonl')) -or
            -not (Test-Path -LiteralPath (Join-Path $semanticFailure.RunRoot `
                    'functional-runtime-capture.json'))) {
            throw 'Semantic replay failure changed or deleted a valid capture.'
        }

        $stockAfter = @(Get-Process -Name $stockNames -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty Id | Sort-Object)
        if (Compare-Object -ReferenceObject $stockBefore `
                -DifferenceObject $stockAfter) {
            throw 'Functional publication roundtrip changed stock-process inventory.'
        }
        Write-Output '[functional-publication-test] producer=production-writer'
        Write-Output '[functional-publication-test] lifecycle=interval-stop-flush-loaded'
        Write-Output '[functional-publication-test] lifecycle-auxiliary=observed-not-delivered'
        Write-Output '[functional-publication-test] limit=incomplete-journal-preserved'
        Write-Output '[functional-publication-test] consumer=production-functional-loader'
        Write-Output '[functional-publication-test] transport-replay=complete'
        Write-Output '[functional-publication-test] auxiliary-accounting=observed-not-delivered'
        Write-Output '[functional-publication-test] strict-policy=separate'
        Write-Output '[functional-publication-test] invalid-context=rejected'
        Write-Output '[functional-publication-test] missing-artifacts=rejected'
        Write-Output '[functional-publication-test] raw-integrity=rejected-on-change'
        Write-Output '[functional-publication-test] finalization-failure=no-complete-manifest'
        Write-Output '[functional-publication-test] semantic-failure=capture-retained'
        Write-Output '[functional-publication-test] stock-launch=absent'
        Write-Output '[functional-publication-test] result=success'
    } finally {
        foreach ($created in $createdRuns) {
            if (Test-Path -LiteralPath $created.Root -PathType Container) {
                Remove-SafeTree $created.Root $created.Parent
            }
        }
    }
    return
}

if ($PSCmdlet.ParameterSetName -eq 'FunctionalFailureRetentionSelfTest') {
    $systemTemporaryRoot =
        [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $testRoot = [IO.Path]::GetFullPath((Join-Path $systemTemporaryRoot (
        'hlclient-functional-failure-retention-' +
        [Guid]::NewGuid().ToString('N'))))
    $researchRoot = Join-Path $testRoot 'research'
    $outputRoot = Join-Path $testRoot 'manual-artifacts\research-copy-smoke'
    $runRoot = Join-Path $outputRoot ([Guid]::NewGuid().ToString('N'))
    $restorationGuard = $null
    $runCapability = $null
    try {
        [IO.Directory]::CreateDirectory($researchRoot) | Out-Null
        [IO.Directory]::CreateDirectory($runRoot) | Out-Null
        [IO.File]::WriteAllText(
            (Join-Path $researchRoot $markerName), $markerText,
            [Text.UTF8Encoding]::new($false))
        $statePath = Join-Path $researchRoot 'state.bin'
        [IO.File]::WriteAllText(
            $statePath, 'before', [Text.UTF8Encoding]::new($false))
        $before = Get-ResearchSnapshot $researchRoot
        $restorationGuard = New-RestorationGuard $researchRoot $before
        [IO.File]::WriteAllText(
            $statePath, 'mutated', [Text.UTF8Encoding]::new($false))

        $stagedPath = Join-Path $runRoot 'functional-smoke.staged.json'
        $staged = [ordered]@{
            schema = 'hlclient.local-research-copy-smoke.v2'
            mode = 'local_research_copy_smoke_v1'
            purpose = 'functional_smoke'
            evidence_eligible = $false
            connection_status = 'unknown'
            client_map_entry = 'unknown'
            last_confirmed_stage = 'connect_requested'
            primary_failure = 'client-connect-state-unknown'
            owned_process_cleanup = 'exact'
            restoration_status = 'wrapper_pending'
            publication_status = 'staged_after_process_cleanup'
        }
        [IO.File]::WriteAllText(
            $stagedPath, (($staged | ConvertTo-Json -Depth 4) + "`r`n"),
            [Text.UTF8Encoding]::new($false))

        $after = Restore-ResearchState $restorationGuard
        if ($after.ManifestSha256 -cne $before.ManifestSha256 -or
            -not (Test-Path -LiteralPath $stagedPath -PathType Leaf)) {
            throw 'Functional failure summary did not survive exact restoration.'
        }
        $runCapability = New-RunDirectoryCapability $runRoot
        $wrapperPath = Join-Path $runRoot 'functional-smoke-wrapper.json'
        Write-AtomicJsonNoOverwrite $wrapperPath ([ordered]@{
                schema = 'hlclient.local-research-copy-smoke-wrapper.v2'
                evidence_eligible = $false
                native_summary_retained_across_restoration = $true
                owned_process_cleanup = 'exact'
                restoration_status = 'exact'
                result = 'local_server_ready_client_blocked'
            }) 'functional failure retention wrapper' $runCapability
        $wrapper = Read-BoundedJsonWithRetainedBytes `
            $wrapperPath 65536 'functional failure retention wrapper' `
            $runCapability
        if (-not [bool]$wrapper.Value.native_summary_retained_across_restoration -or
            [string]$wrapper.Value.restoration_status -cne 'exact' -or
            [string]$wrapper.Value.result -cne
                'local_server_ready_client_blocked') {
            throw 'Functional failure final summary contract regressed.'
        }
        Write-Output '[functional-failure-retention-test] failed-connection-summary=retained'
        Write-Output '[functional-failure-retention-test] restoration=exact'
        Write-Output '[functional-failure-retention-test] final-summary=published-after-restoration'
        Write-Output '[functional-failure-retention-test] evidence-eligible=false'
        Write-Output '[functional-failure-retention-test] result=success'
    } finally {
        if ($null -ne $runCapability) {
            $runCapability.Dispose()
            $runCapability = $null
        }
        if ($null -ne $restorationGuard) {
            $backupPath = $restorationGuard.TemporaryRoot
            Close-RestorationGuardCapabilities $restorationGuard
            if (Test-Path -LiteralPath $backupPath -PathType Container) {
                Remove-SafeTree $backupPath $systemTemporaryRoot
            }
        }
        if (Test-Path -LiteralPath $testRoot -PathType Container) {
            Remove-SafeTree $testRoot $systemTemporaryRoot
        }
    }
    return
}

if ($PSCmdlet.ParameterSetName -eq 'RetainedBackupRecovery') {
    $recovery = Invoke-RetainedBackupRecoveryInspection `
        $ResearchHalfLifeRoot $RetainedBackupRoot
    Write-Output '[stock-runtime-recovery] mode=legacy-retained-backup-inspection'
    Write-Output ("[stock-runtime-recovery] recovery-status={0}" -f
        $recovery.RecoveryStatus)
    Write-Output ("[stock-runtime-recovery] restoration-status={0}" -f
        $recovery.RestorationStatus)
    Write-Output ("[stock-runtime-recovery] backup-status={0}" -f
        $recovery.BackupStatus)
    Write-Output ("[stock-runtime-recovery] current-entries={0}" -f
        $recovery.CurrentEntryCount)
    Write-Output ("[stock-runtime-recovery] backup-entries={0}" -f
        $recovery.BackupEntryCount)
    Write-Output ("[stock-runtime-recovery] comparison={0}" -f
        $(if ($recovery.RecoveryStatus -ceq
                'no_restoration_needed_verified_unchanged') {
                'exact'
            } else { 'different' }))
    if ($recovery.RecoveryStatus -cne
            'no_restoration_needed_verified_unchanged') {
        throw 'recovery_unresolved: legacy retained backup differs and lacks a persisted exact metadata manifest'
    }
    return
}

if ($PSCmdlet.ParameterSetName -eq 'RetainedBackupRecoverySelfTest') {
    $systemTemporaryRoot =
        [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $testRoot = [IO.Path]::GetFullPath((Join-Path $systemTemporaryRoot (
        'hlclient-retained-recovery-test-' + [Guid]::NewGuid().ToString('N'))))
    $backupRoot = [IO.Path]::GetFullPath((Join-Path $systemTemporaryRoot (
        'hlclient-stock-runtime-restore-' + [Guid]::NewGuid().ToString('N'))))
    try {
        [IO.Directory]::CreateDirectory($testRoot) | Out-Null
        $data = Join-Path $backupRoot 'data'
        [IO.Directory]::CreateDirectory($data) | Out-Null
        [IO.Directory]::CreateDirectory(
            (Join-Path $data '.hlclient-restoration-identity-lock')) | Out-Null
        $preparation = [ordered]@{
            schema = 'hlclient.stock-runtime-research-preparation.v3'
            preparation_status = 'exact-materialized-copy-verified'
        } | ConvertTo-Json
        foreach ($entry in @(
                @($markerName, $markerText),
                @('.hlclient-research-pending', 'synthetic-pending'),
                @('.hlclient-research-preparation.json', $preparation),
                @('state.bin', 'before'))) {
            [IO.File]::WriteAllText(
                (Join-Path $testRoot $entry[0]), $entry[1],
                [Text.UTF8Encoding]::new($false))
            [IO.File]::Copy(
                (Join-Path $testRoot $entry[0]),
                (Join-Path $data $entry[0]), $false)
        }
        $unchanged = Invoke-RetainedBackupRecoveryInspection `
            $testRoot $backupRoot
        if ($unchanged.RecoveryStatus -cne
                'no_restoration_needed_verified_unchanged' -or
            $unchanged.RestorationStatus -cne 'restoration_not_attempted' -or
            $unchanged.BackupStatus -cne 'retained') {
            throw 'Legacy retained backup unchanged classification regressed.'
        }
        [IO.File]::WriteAllText(
            (Join-Path $testRoot 'state.bin'), 'different',
            [Text.UTF8Encoding]::new($false))
        $differentHash = Get-FileSha256 (Join-Path $testRoot 'state.bin')
        $unresolved = Invoke-RetainedBackupRecoveryInspection `
            $testRoot $backupRoot
        if ($unresolved.RecoveryStatus -cne 'recovery_unresolved' -or
            (Get-FileSha256 (Join-Path $testRoot 'state.bin')) -cne
                $differentHash) {
            throw 'Legacy retained backup difference did not fail closed.'
        }
        Write-Output '[stock-runtime-recovery-test] unchanged=no_restoration_needed_verified_unchanged'
        Write-Output '[stock-runtime-recovery-test] difference=recovery_unresolved'
        Write-Output '[stock-runtime-recovery-test] blanket-copy=absent'
        Write-Output '[stock-runtime-recovery-test] backup=retained'
        Write-Output '[stock-runtime-recovery-test] result=success'
    } finally {
        foreach ($path in @($backupRoot, $testRoot)) {
            if (Test-Path -LiteralPath $path) {
                Remove-SafeTree $path $systemTemporaryRoot
            }
        }
    }
    return
}

if ($PSCmdlet.ParameterSetName -eq 'OrchestratorStartupBoundarySelfTest') {
    if ($env:OS -cne 'Windows_NT' -or [IntPtr]::Size -ne 8) {
        throw 'Orchestrator startup boundary test requires x64 Windows PowerShell.'
    }
    $orchestrator = Resolve-TrustedRepositoryTool $OrchestratorPath `
        'hlclient_stock_runtime_orchestrator.exe' 'stock runtime orchestrator'
    $systemTemporaryRoot =
        [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $fixtureRoot = [IO.Path]::GetFullPath((Join-Path $systemTemporaryRoot (
        'hlclient-stock-startup-boundary-' + [Guid]::NewGuid().ToString('N'))))
    $restorationGuard = $null
    $capabilities = $null

    function New-StartupBoundaryCapabilities {
        return [pscustomobject]@{
            Startup = New-OrchestratorTransactionCapability
            Cleanup = New-OrchestratorTransactionCapability
            Job = New-OrchestratorProcessJobCapability
            GuardJob = New-OrchestratorProcessJobCapability
            IsolationRelease = New-OrchestratorTransactionCapability
        }
    }

    function Close-StartupBoundaryCapabilities {
        param([object]$Capabilities)
        if ($null -eq $Capabilities) { return }
        foreach ($name in @('Startup', 'Cleanup', 'Job', 'GuardJob',
                'IsolationRelease')) {
            $handle = [IntPtr]$Capabilities.$name
            if ($handle -ne [IntPtr]::Zero) {
                if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                        $handle)) {
                    throw "Startup boundary $name capability close failed."
                }
                $Capabilities.$name = [IntPtr]::Zero
            }
        }
    }

    function New-StartupBoundaryArguments {
        param(
            [object]$Capabilities,
            [switch]$Probe,
            [switch]$UnknownArgument,
            [switch]$InvalidCleanupHandle,
            [switch]$FunctionalRuntimeCapture
        )
        $cleanupValue = if ($InvalidCleanupHandle) { '65535' } else {
            ([IntPtr]$Capabilities.Cleanup).ToInt64().ToString(
                [Globalization.CultureInfo]::InvariantCulture)
        }
        $arguments = @(
            $(if ($FunctionalRuntimeCapture) {
                    '--confirmation-token'
                } else { '--functional-confirmation-token' }),
            $(if ($FunctionalRuntimeCapture) {
                    $activeCaptureToken
                } else { $functionalSmokeToken }),
            '--wrapper-capability-handle',
            ([IntPtr]$Capabilities.Startup).ToInt64().ToString(
                [Globalization.CultureInfo]::InvariantCulture),
            '--wrapper-cleanup-capability-handle', $cleanupValue,
            '--wrapper-job-handle',
            ([IntPtr]$Capabilities.Job).ToInt64().ToString(
                [Globalization.CultureInfo]::InvariantCulture),
            '--wrapper-guard-job-handle',
            ([IntPtr]$Capabilities.GuardJob).ToInt64().ToString(
                [Globalization.CultureInfo]::InvariantCulture),
            '--isolation-release-handle',
            ([IntPtr]$Capabilities.IsolationRelease).ToInt64().ToString(
                [Globalization.CultureInfo]::InvariantCulture),
            '--wrapper-process-id', [string]$PID,
            '--run-root', (Join-Path $fixtureRoot (
                'manual-artifacts\research-copy-smoke\' +
                [Guid]::Empty.ToString('N'))),
            '--research-root', $fixtureRoot,
            '--client', (Join-Path $fixtureRoot 'never-launch-hl.exe'),
            '--server', (Join-Path $fixtureRoot 'never-launch-hlds.exe'),
            '--relay', (Join-Path $fixtureRoot 'never-launch-relay.exe'),
            '--isolation-guard', (Join-Path $fixtureRoot 'never-launch-guard.exe'),
            '--app-manifest', (Join-Path $fixtureRoot 'never-read-appmanifest.acf'),
            '--game', 'valve', '--map', 'boot_camp',
            '--relay-port', '27140', '--server-port', '27141',
            '--server-profile-id', 'steam-hlds-10210-no-mode-banner-v1',
            '--max-duration-seconds', '5',
            '--max-datagrams', '8192',
            '--max-total-raw-bytes', '67108864',
            '--max-payload-bytes', '65507',
            '--max-reassembled-bytes', '8388608',
            '--max-decompressed-bytes', '33554432',
            '--max-message-count', '8192',
            '--max-runtime-frames', '4096',
            '--max-client-packets', '4096',
            '--max-server-packets', '4096',
            '--mutation-after-client-packets', '20',
            '--mutation-after-server-packets', '20')
        if ($FunctionalRuntimeCapture) {
            $arguments += @(
                '--output-role', 'functional-runtime-capture',
                '--scenario', 'idle-runtime')
        } else {
            $arguments += '--functional-smoke'
        }
        if ($Probe) { $arguments += '--validate-wrapper-startup' }
        if ($UnknownArgument) {
            $arguments += @('--definitely-unknown-startup-argument', '1')
        }
        return $arguments
    }

    $stockNames = @('hl', 'hlds', 'hlclient_stock_runtime_isolation_guard',
        'hlclient_stock_runtime_capture')
    $stockBefore = @(Get-Process -Name $stockNames -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Id | Sort-Object)
    try {
        [IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
        [IO.File]::WriteAllText(
            (Join-Path $fixtureRoot $markerName), $markerText,
            [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText(
            (Join-Path $fixtureRoot 'state.bin'), 'before',
            [Text.UTF8Encoding]::new($false))

        $capabilities = New-StartupBoundaryCapabilities
        $successState = New-OrchestratorExitState
        $success = Invoke-BoundedOrchestrator $orchestrator `
            @(New-StartupBoundaryArguments $capabilities -Probe) 15 `
            $capabilities.Startup $capabilities.Cleanup $capabilities.Job `
            $capabilities.GuardJob $capabilities.IsolationRelease $successState
        if ($success.ExitCode -ne 0 -or
            $successState.StartupStatus -cne 'acknowledgement_received' -or
            -not $successState.JobCleanupConfirmed -or
            -not $successState.CleanupSignaled) {
            throw 'Functional startup probe did not complete its exact handshake.'
        }
        Assert-OrchestratorValue $success startup-boundary acknowledged
        Assert-OrchestratorValue $success processes-started 0
        Assert-OrchestratorValue $success capture-files-written 0
        Assert-OrchestratorValue $success evidence-eligible false
        Assert-OrchestratorValue $success result success
        Close-StartupBoundaryCapabilities $capabilities
        $capabilities = $null

        $capabilities = New-StartupBoundaryCapabilities
        $captureState = New-OrchestratorExitState
        $captureStartup = Invoke-BoundedOrchestrator $orchestrator `
            @(New-StartupBoundaryArguments $capabilities -Probe `
                -FunctionalRuntimeCapture) 15 `
            $capabilities.Startup $capabilities.Cleanup $capabilities.Job `
            $capabilities.GuardJob $capabilities.IsolationRelease $captureState
        if ($captureStartup.ExitCode -ne 0 -or
            $captureState.StartupStatus -cne 'acknowledgement_received' -or
            -not $captureState.JobCleanupConfirmed -or
            -not $captureState.CleanupSignaled) {
            throw 'Functional runtime capture startup probe did not complete its exact handshake.'
        }
        Assert-OrchestratorValue $captureStartup startup-boundary acknowledged
        Assert-OrchestratorValue $captureStartup mode `
            functional_runtime_capture_v1
        Assert-OrchestratorValue $captureStartup purpose `
            functional_runtime_capture_startup_probe
        Assert-OrchestratorValue $captureStartup processes-started 0
        Assert-OrchestratorValue $captureStartup capture-files-written 0
        Assert-OrchestratorValue $captureStartup evidence-eligible false
        Assert-OrchestratorValue $captureStartup result success
        Close-StartupBoundaryCapabilities $capabilities
        $capabilities = $null

        $before = Get-ResearchSnapshot $fixtureRoot
        $restorationGuard = New-RestorationGuard $fixtureRoot $before
        [IO.File]::WriteAllText(
            (Join-Path $fixtureRoot 'state.bin'), 'mutated',
            [Text.UTF8Encoding]::new($false))
        $capabilities = New-StartupBoundaryCapabilities
        $earlyState = New-OrchestratorExitState
        $earlyError = $null
        try {
            [void](Invoke-BoundedOrchestrator $orchestrator `
                @(New-StartupBoundaryArguments $capabilities -UnknownArgument) 15 `
                $capabilities.Startup $capabilities.Cleanup $capabilities.Job `
                $capabilities.GuardJob $capabilities.IsolationRelease $earlyState)
        } catch { $earlyError = $_ }
        if ($null -eq $earlyError -or $earlyState.StartupStatus -cne 'early_exit' -or
            $earlyState.StartupExitCode -ne 2 -or
            $earlyState.StartupExitCodeHex -cne '0x00000002' -or
            $earlyState.StartupStderr -cnotmatch '^Usage:' -or
            -not $earlyState.JobCleanupConfirmed -or
            $earlyError.Exception.Message -notmatch
                '^orchestrator_startup_early_exit:') {
            throw 'Early parser exit was not preserved as a typed startup result.'
        }
        $afterRecovery = Restore-ResearchState $restorationGuard
        if ($afterRecovery.ManifestSha256 -cne $before.ManifestSha256) {
            throw 'Pre-acknowledgement failure recovery was not exact.'
        }
        Close-StartupBoundaryCapabilities $capabilities
        $capabilities = $null

        $capabilities = New-StartupBoundaryCapabilities
        $invalidState = New-OrchestratorExitState
        $invalidError = $null
        try {
            [void](Invoke-BoundedOrchestrator $orchestrator `
                @(New-StartupBoundaryArguments $capabilities -InvalidCleanupHandle) 15 `
                $capabilities.Startup $capabilities.Cleanup $capabilities.Job `
                $capabilities.GuardJob $capabilities.IsolationRelease $invalidState)
        } catch { $invalidError = $_ }
        if ($null -eq $invalidError -or
            $invalidState.StartupStatus -cne 'early_exit' -or
            $invalidState.StartupExitCode -ne 3 -or
            $invalidState.StartupStdout -cnotmatch
                'failure-category=wrapper_transaction_capability_required') {
            throw 'Invalid inherited handle was not rejected before startup acknowledgment.'
        }
        Close-StartupBoundaryCapabilities $capabilities
        $capabilities = $null

        $stockAfter = @(Get-Process -Name $stockNames -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty Id | Sort-Object)
        if (Compare-Object -ReferenceObject $stockBefore -DifferenceObject $stockAfter) {
            throw 'Startup boundary fixture changed the stock-process inventory.'
        }
        Write-Output '[stock-runtime-startup-test] powershell-bitness=x64'
        Write-Output '[stock-runtime-startup-test] functional-arguments=accepted'
        Write-Output '[stock-runtime-startup-test] functional-runtime-capture-arguments=accepted'
        Write-Output '[stock-runtime-startup-test] wrapper-acknowledgement=received'
        Write-Output '[stock-runtime-startup-test] acknowledged-child-exit-code=0'
        Write-Output '[stock-runtime-startup-test] acknowledged-child-exit-code-hex=0x00000000'
        Write-Output '[stock-runtime-startup-test] stock-launch=absent'
        Write-Output '[stock-runtime-startup-test] unknown-argument=typed-early-exit'
        Write-Output '[stock-runtime-startup-test] unknown-argument-exit-code=2'
        Write-Output '[stock-runtime-startup-test] unknown-argument-exit-code-hex=0x00000002'
        Write-Output '[stock-runtime-startup-test] invalid-handle=rejected'
        Write-Output '[stock-runtime-startup-test] invalid-handle-exit-code=3'
        Write-Output '[stock-runtime-startup-test] invalid-handle-exit-code-hex=0x00000003'
        Write-Output '[stock-runtime-startup-test] startup-stderr=retained-bounded'
        Write-Output '[stock-runtime-startup-test] pre-ack-recovery=exact'
        Write-Output '[stock-runtime-startup-test] tracing-capabilities=not-required'
        Write-Output '[stock-runtime-startup-test] evidence-eligible=false'
        Write-Output '[stock-runtime-startup-test] result=success'
    } finally {
        if ($null -ne $capabilities) {
            Close-StartupBoundaryCapabilities $capabilities
        }
        if ($null -ne $restorationGuard) {
            Close-RestorationBackupCapabilities $restorationGuard
            if (Test-Path -LiteralPath $restorationGuard.TemporaryRoot) {
                Remove-SafeTree $restorationGuard.TemporaryRoot $systemTemporaryRoot
            }
            Close-RestorationGuardCapabilities $restorationGuard
        }
        if (Test-Path -LiteralPath $fixtureRoot) {
            Remove-SafeTree $fixtureRoot $systemTemporaryRoot
        }
    }
    return
}

if ($PSCmdlet.ParameterSetName -eq 'RestorationSelfTest') {
    $systemTemporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $selfTestRoot = [IO.Path]::GetFullPath((Join-Path $systemTemporaryRoot (
        'hlclient-stock-runtime-selftest-' + [Guid]::NewGuid().ToString('N'))))
    $guard = $null
    $publicationCapability = $null
    $reconnectPublicationCapability = $null
    try {
        Assert-PathBelowRoot $selfTestRoot $systemTemporaryRoot 'restoration self-test root'
        Assert-NoReparsePointInExistingPath $selfTestRoot 'restoration self-test path'
        [IO.Directory]::CreateDirectory($selfTestRoot) | Out-Null
        Assert-NoReparsePointInExistingPath $selfTestRoot 'restoration self-test path'

        $testResearch = Join-Path $selfTestRoot 'research'
        [IO.Directory]::CreateDirectory($testResearch) | Out-Null
        $original = Join-Path $testResearch 'original.bin'
        $external = Join-Path $selfTestRoot 'external-sentinel.bin'
        $originalDirectory = Join-Path $testResearch 'original-directory'
        $externalDirectory = Join-Path $selfTestRoot 'external-directory'
        [IO.Directory]::CreateDirectory($originalDirectory) | Out-Null
        [IO.Directory]::CreateDirectory($externalDirectory) | Out-Null
        $originalNested = Join-Path $originalDirectory 'nested.bin'
        $externalNested = Join-Path $externalDirectory 'external-nested.bin'
        [IO.File]::WriteAllText(
            (Join-Path $testResearch $markerName), $markerText,
            [Text.Encoding]::ASCII)
        [IO.File]::WriteAllBytes($original, [byte[]](0x10, 0x20, 0x30, 0x40))
        [IO.File]::WriteAllBytes($external, [byte[]](0xA1, 0xB2, 0xC3, 0xD4))
        [IO.File]::WriteAllBytes($originalNested, [byte[]](0x41, 0x42, 0x43))
        [IO.File]::WriteAllBytes($externalNested, [byte[]](0xE1, 0xE2, 0xE3))
        $externalBefore = Get-RestorationSelfTestExternalObservation $external
        $externalDirectoryBefore =
            Get-RestorationSelfTestExternalObservation $externalDirectory
        $externalNestedBefore =
            Get-RestorationSelfTestExternalObservation $externalNested

        $before = Get-ResearchSnapshot $testResearch
        $guard = New-RestorationGuard $testResearch $before

        # A root-directory ADS is invisible to child enumeration. Prove that a
        # post-snapshot mutation is rejected by the independent restoration
        # snapshot gate, then remove only this self-test-owned stream.
        $rootAdsName = 'hlclient-restoration-root-ads-probe'
        $rootAdsRejected = $false
        try {
            [Hlclient.StockRuntimeDirectoryCapability]::WriteRestorationRootAdsProbe(
                $testResearch)
            try { [void](Get-ResearchSnapshot $testResearch) }
            catch {
                if ($_.Exception.Message -cmatch
                    '^research snapshot root must contain only its default data stream\.$') {
                    $rootAdsRejected = $true
                } else { throw }
            }
        } finally {
            [Hlclient.StockRuntimeDirectoryCapability]::DeleteRestorationRootAdsProbe(
                $testResearch)
        }
        if (-not $rootAdsRejected) {
            throw 'Restoration self-test did not reject a research-root ADS mutation.'
        }

        $researchSwap = Join-Path $selfTestRoot 'research-swapped'
        $researchSwapBlocked = $false
        try { [IO.Directory]::Move($testResearch, $researchSwap) }
        catch { $researchSwapBlocked = $true }
        if (-not $researchSwapBlocked -or
            -not (Test-Path -LiteralPath $testResearch -PathType Container) -or
            (Test-Path -LiteralPath $researchSwap)) {
            throw 'Restoration self-test research-root swap was not blocked.'
        }
        $backupSwap = $guard.TemporaryRoot + '-swapped'
        $backupSwapBlocked = $false
        try { [IO.Directory]::Move($guard.TemporaryRoot, $backupSwap) }
        catch { $backupSwapBlocked = $true }
        if (-not $backupSwapBlocked -or
            -not (Test-Path -LiteralPath $guard.TemporaryRoot -PathType Container) -or
            (Test-Path -LiteralPath $backupSwap)) {
            throw 'Restoration self-test backup-root swap was not blocked.'
        }

        [IO.File]::Delete($original)
        [void](New-Item -ItemType HardLink -Path $original -Target $external -ErrorAction Stop)
        [IO.Directory]::Delete($originalDirectory, $true)
        [void](New-Item -ItemType Junction -Path $originalDirectory `
            -Target $externalDirectory -ErrorAction Stop)
        [IO.File]::WriteAllBytes((Join-Path $testResearch 'created.bin'), [byte[]](0x55, 0x66))

        $after = Restore-ResearchState $guard
        if ((Get-RestorationSelfTestExternalObservation $external) -cne
            $externalBefore) {
            throw 'Restoration self-test changed an external hard-link target.'
        }
        if ((Get-RestorationSelfTestExternalObservation $externalDirectory) -cne
                $externalDirectoryBefore -or
            (Get-RestorationSelfTestExternalObservation $externalNested) -cne
                $externalNestedBefore) {
            throw 'Restoration self-test traversed an external junction target.'
        }
        if ($after.ManifestSha256 -cne $before.ManifestSha256 -or
            $after.EntryCount -ne $before.EntryCount -or
            $after.TotalBytes -ne $before.TotalBytes) {
            throw 'Restoration self-test did not recover the exact research snapshot.'
        }

        $publicationRun = Join-Path $selfTestRoot 'publication-run'
        [IO.Directory]::CreateDirectory(
            (Join-Path $publicationRun 'logs')) | Out-Null
        $publicationCapability = New-RunDirectoryCapability $publicationRun
        if (-not $publicationCapability.VerifyRootSubstitutionBlocked()) {
            throw 'Retained publication root substitution was not blocked.'
        }
        if (-not $publicationCapability.VerifyTemporarySubstitutionBlocked()) {
            throw 'Atomic publication temporary substitution was not blocked.'
        }
        if (-not $publicationCapability.VerifyRollbackReplacementPreserved(4) -or
            -not $publicationCapability.VerifyRollbackReplacementPreserved(5)) {
            throw 'Atomic publication rollback deleted a substituted pathname.'
        }
        $replaceLeaf = '.hlclient-replacing-publication-selftest.json'
        $replaceInitial = [Text.Encoding]::ASCII.GetBytes('{"generation":1}')
        $replaceFinal = [Text.Encoding]::ASCII.GetBytes('{"generation":2}')
        $publicationCapability.PublishNewFile($replaceLeaf, $replaceInitial)
        $publicationCapability.PublishReplacingFile(
            $replaceLeaf, $replaceInitial, $replaceFinal)
        $replaceObserved = $publicationCapability.ReadExistingFile(
            $replaceLeaf, 1024)
        if ([BitConverter]::ToString($replaceObserved) -cne
            [BitConverter]::ToString($replaceFinal)) {
            throw 'Retained-handle replacing publication changed its exact bytes.'
        }
        if (-not $publicationCapability.VerifyReplacingRollbackPreserved()) {
            throw 'Replacing publication did not preserve the prior manifest on rollback.'
        }
        if (-not $publicationCapability.VerifyReplacingExpectedPriorMismatchBlocked()) {
            throw 'Replacing publication accepted mismatched prior bytes.'
        }
        if (-not $publicationCapability.VerifyReplacingSubstitutionBlocked()) {
            throw 'Replacing publication overwrote a concurrent substituted leaf.'
        }

        $testVersion = [ordered]@{ schema = 'version-self-test' }
        $testIsolation = [ordered]@{ schema = 'isolation-self-test' }
        $testRestoration = [ordered]@{ schema = 'restoration-self-test' }
        $testVersionBytes = [Text.UTF8Encoding]::new($false).GetBytes(
            (($testVersion | ConvertTo-Json -Depth 8) + "`r`n"))
        $testIsolationBytes = [Text.UTF8Encoding]::new($false).GetBytes(
            (($testIsolation | ConvertTo-Json -Depth 8) + "`r`n"))
        $testRestorationBytes = [Text.UTF8Encoding]::new($false).GetBytes(
            (($testRestoration | ConvertTo-Json -Depth 8) + "`r`n"))
        $testManifest = [ordered]@{
            scenario = 'baseline'
            external_target_profile = 'none'
            external_target_count = 0
            accepted_transport_run = $true
            accepted_evidence_run = $true
            failure_category = 'none'
        }
        $gateCases = @(
            @{ Owned = $false; Restored = $true; External = $true; Checked = $true },
            @{ Owned = $true; Restored = $false; External = $true; Checked = $true },
            @{ Owned = $true; Restored = $true; External = $false; Checked = $true },
            @{ Owned = $true; Restored = $true; External = $true; Checked = $false })
        foreach ($gateCase in $gateCases) {
            $gateRejected = $false
            try {
                Publish-AcceptedEvidenceTransaction -RunRoot $publicationRun `
                    -Version $testVersion -VersionBytes $testVersionBytes `
                    -Isolation $testIsolation -IsolationBytes $testIsolationBytes `
                    -Restoration $testRestoration `
                    -RestorationBytes $testRestorationBytes `
                    -RunManifest $testManifest `
                    -OwnedJobsExact ([bool]$gateCase.Owned) `
                    -RestorationExact ([bool]$gateCase.Restored) `
                    -ExternalStateExact ([bool]$gateCase.External) `
                    -CheckerWalkerReady ([bool]$gateCase.Checked) `
                    -FailureCategory 'none' `
                    -DirectoryCapability $publicationCapability
            } catch {
                $gateRejected = $true
            }
            if (-not $gateRejected) {
                throw 'Final publication accepted an incomplete transaction gate.'
            }
            foreach ($finalLeaf in @(
                    'version-observation.json', 'isolation-attestation.json',
                    'restoration-attestation.json', 'research-run-metadata.json')) {
                if (Test-Path -LiteralPath (Join-Path $publicationRun $finalLeaf)) {
                    throw 'Failed transaction gate left a final evidence leaf.'
                }
            }
        }

        $startCapability = [IntPtr]::Zero
        $cleanupCapability = [IntPtr]::Zero
        $emptyCampaignJob = [IntPtr]::Zero
        $emptyGuardJob = [IntPtr]::Zero
        $emptyRelease = [IntPtr]::Zero
        try {
            $startCapability = New-OrchestratorTransactionCapability
            $cleanupCapability = New-OrchestratorTransactionCapability
            $emptyCampaignJob = New-OrchestratorProcessJobCapability
            $emptyGuardJob = New-OrchestratorProcessJobCapability
            $emptyRelease = New-OrchestratorTransactionCapability
            $startFailureState = [pscustomobject]@{
                Started = $false
                ExitConfirmed = $false
                ExitCode = $null
                NoOrchestratorProcessCreated = $false
                CleanupSignaled = $false
                CampaignJobCleanupConfirmed = $false
                GuardJobCleanupConfirmed = $false
                JobCleanupConfirmed = $false
                Failure = $null
            }
            $startFailureObserved = $false
            try {
                [void](Invoke-BoundedOrchestrator `
                    (Join-Path $selfTestRoot 'absent-orchestrator.exe') @() 1 `
                    $startCapability $cleanupCapability $emptyCampaignJob `
                    $emptyGuardJob $emptyRelease $startFailureState)
            } catch {
                $startFailureObserved = $true
            }
            if (-not $startFailureObserved -or $startFailureState.Started -or
                -not $startFailureState.ExitConfirmed -or
                -not $startFailureState.NoOrchestratorProcessCreated -or
                -not $startFailureState.CampaignJobCleanupConfirmed -or
                -not $startFailureState.GuardJobCleanupConfirmed -or
                -not $startFailureState.JobCleanupConfirmed -or
                $startFailureState.CleanupSignaled -or
                [string]$startFailureState.Failure -cne
                    'orchestrator-process-not-created' -or
                [Hlclient.StockRuntimeOrchestratorCapability]::WaitForSingleObject(
                    $emptyRelease, 0) -ne 0) {
                throw 'Orchestrator launch failure did not attest exact empty-Job cleanup.'
            }
        } finally {
            foreach ($emptyHandle in @(
                    $startCapability, $cleanupCapability, $emptyCampaignJob,
                    $emptyGuardJob, $emptyRelease)) {
                if ($emptyHandle -ne [IntPtr]::Zero) {
                    [void][Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                        $emptyHandle)
                }
            }
        }

        $collisionPath = Join-Path $publicationRun 'collision.json'
        [IO.File]::WriteAllText(
            $collisionPath, '{"owner":"external"}',
            [Text.UTF8Encoding]::new($false))
        [string[]]$rollbackLeaves = @(
            'partial-restoration.json', 'collision.json')
        [byte[][]]$rollbackPayloads = [byte[][]]::new(2)
        $rollbackPayloads[0] = [Text.Encoding]::UTF8.GetBytes(
            '{"schema":"partial-restoration"}')
        $rollbackPayloads[1] = [Text.Encoding]::UTF8.GetBytes(
            '{"owner":"wrapper"}')
        $rollbackObserved = $false
        try {
            $publicationCapability.PublishNewFiles(
                $rollbackLeaves, $rollbackPayloads)
        } catch {
            $rollbackObserved = $true
        }
        if (-not $rollbackObserved -or
            (Test-Path -LiteralPath (Join-Path $publicationRun `
                    'partial-restoration.json')) -or
            [IO.File]::ReadAllText($collisionPath) -cne '{"owner":"external"}') {
            throw 'Atomic publication failure left a partial final evidence set.'
        }
        Write-RejectedManifestAfterEvidencePublicationFailure `
            $publicationRun $testManifest $publicationCapability
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json')) {
            if (Test-Path -LiteralPath (Join-Path $publicationRun $finalLeaf)) {
                throw 'Failed accepted batch published a final evidence leaf.'
            }
        }
        $rejectedPublication = Read-BoundedJson `
            (Join-Path $publicationRun 'research-run-metadata.json') `
            65536 'publication-failed research run manifest'
        if ([bool]$rejectedPublication.accepted_transport_run -or
            [bool]$rejectedPublication.accepted_evidence_run -or
            [string]$rejectedPublication.failure_category -cne
                'evidence_publication_failed') {
            throw 'Failed accepted batch did not publish a typed rejected manifest.'
        }
        [IO.File]::Delete(
            (Join-Path $publicationRun 'research-run-metadata.json'))
        [IO.File]::Delete($collisionPath)

        Publish-AcceptedEvidenceTransaction -RunRoot $publicationRun `
            -Version $testVersion -VersionBytes $testVersionBytes `
            -Isolation $testIsolation -IsolationBytes $testIsolationBytes `
            -Restoration $testRestoration `
            -RestorationBytes $testRestorationBytes `
            -RunManifest $testManifest `
            -OwnedJobsExact $true -RestorationExact $true `
            -ExternalStateExact $true -CheckerWalkerReady $true `
            -FailureCategory 'none' `
            -DirectoryCapability $publicationCapability
        [byte[]]$retainedVersionBytes =
            $publicationCapability.ReadExistingFile(
                'version-observation.json', 65536)
        if ([Convert]::ToBase64String($retainedVersionBytes) -cne
            [Convert]::ToBase64String($testVersionBytes)) {
            throw 'Retained-handle publication read-back changed exact bytes.'
        }
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json', 'research-run-metadata.json')) {
            if (-not (Test-Path -LiteralPath `
                    (Join-Path $publicationRun $finalLeaf) -PathType Leaf)) {
                throw 'Successful baseline transaction omitted a final evidence leaf.'
            }
        }
        if (Test-Path -LiteralPath (
                Join-Path $publicationRun 'reconnect-observation.json')) {
            throw 'Successful baseline transaction published a reconnect-only leaf.'
        }
        $publicationCapability.Dispose()
        $publicationCapability = $null

        # Exercise the scenario-dependent five-member commit independently.
        # These values satisfy the exact reconnect-observation v1 shape while
        # remaining synthetic, path-free and body-unconsumed.
        $reconnectValues = [Collections.Generic.Dictionary[string, string]]::new(
            [StringComparer]::Ordinal)
        $reconnectValues.Add(
            'profile', 'stock_protocol_48_build_10210_evidence_pending')
        $reconnectValues.Add(
            'retired-generation-a-server-tail-packets', '0')
        foreach ($generation in @(
                [pscustomobject]@{ Label = 'a'; First = 0; Last = 9 },
                [pscustomobject]@{ Label = 'b'; First = 10; Last = 19 })) {
            $prefix = 'generation-' + $generation.Label + '-'
            $values = [ordered]@{
                'first-observed-ordinal' = [string]$generation.First
                'last-observed-ordinal' = [string]$generation.Last
                'connectionless-exchanges' = '2'
                'first-sequenced-packet-ordinal' =
                    [string]($generation.First + 2)
                'client-to-server-packets' = '3'
                'server-to-client-packets' = '5'
                'boundary-payload-ordinal' = '0'
                'boundary-observed-ordinal' =
                    [string]($generation.First + 4)
                'boundary-delivery-ordinal' = '3'
                'boundary-byte-offset' = '0'
                'boundary-bit-offset' = '0'
                'boundary-source-payload-bytes' = '1'
                'boundary-source-payload-bits' = '8'
                'boundary-next-unconsumed-bits' = '8'
                'boundary-byte-aligned' = 'true'
                'candidate-bit-width' = '8'
                'first-candidate' = '5'
            }
            foreach ($key in $values.Keys) {
                $reconnectValues.Add($prefix + $key, $values[$key])
            }
        }
        $testReconnectObservation =
            New-ReconnectFinalObservation -Values $reconnectValues
        $testReconnectManifest = [ordered]@{
            scenario = 'reconnect'
            external_target_profile = 'none'
            external_target_count = 0
            accepted_transport_run = $true
            accepted_evidence_run = $true
            failure_category = 'none'
            connection_generation_count = 2
            exact_boundary_count = 2
            runtime_candidate_count = 2
            generation_distinct = $true
            candidate_conflict = $false
        }
        $reconnectPublicationRun = Join-Path $selfTestRoot `
            'reconnect-publication-run'
        [IO.Directory]::CreateDirectory(
            (Join-Path $reconnectPublicationRun 'logs')) | Out-Null
        $reconnectPublicationCapability =
            New-RunDirectoryCapability $reconnectPublicationRun

        $reconnectGateRejected = $false
        try {
            Publish-AcceptedEvidenceTransaction `
                -RunRoot $reconnectPublicationRun `
                -Version $testVersion -VersionBytes $testVersionBytes `
                -Isolation $testIsolation -IsolationBytes $testIsolationBytes `
                -Restoration $testRestoration `
                -RestorationBytes $testRestorationBytes `
                -ReconnectObservation $testReconnectObservation `
                -RunManifest $testReconnectManifest `
                -OwnedJobsExact $true -RestorationExact $true `
                -ExternalStateExact $true -CheckerWalkerReady $false `
                -FailureCategory 'none' `
                -DirectoryCapability $reconnectPublicationCapability
        } catch {
            $reconnectGateRejected = $true
        }
        if (-not $reconnectGateRejected) {
            throw 'Reconnect publication accepted an incomplete transaction gate.'
        }
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json', 'reconnect-observation.json',
                'research-run-metadata.json')) {
            if (Test-Path -LiteralPath (
                    Join-Path $reconnectPublicationRun $finalLeaf)) {
                throw 'Rejected reconnect gate left a final evidence leaf.'
            }
        }

        Publish-AcceptedEvidenceTransaction `
            -RunRoot $reconnectPublicationRun `
            -Version $testVersion -VersionBytes $testVersionBytes `
            -Isolation $testIsolation -IsolationBytes $testIsolationBytes `
            -Restoration $testRestoration `
            -RestorationBytes $testRestorationBytes `
            -ReconnectObservation $testReconnectObservation `
            -RunManifest $testReconnectManifest `
            -OwnedJobsExact $true -RestorationExact $true `
            -ExternalStateExact $true -CheckerWalkerReady $true `
            -FailureCategory 'none' `
            -DirectoryCapability $reconnectPublicationCapability
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json', 'reconnect-observation.json',
                'research-run-metadata.json')) {
            if (-not (Test-Path -LiteralPath (
                        Join-Path $reconnectPublicationRun $finalLeaf) `
                    -PathType Leaf)) {
                throw 'Successful reconnect transaction omitted a final evidence leaf.'
            }
        }
        $publishedReconnect = Read-BoundedJson `
            (Join-Path $reconnectPublicationRun 'reconnect-observation.json') `
            65536 'self-test reconnect observation'
        if ([string]$publishedReconnect.schema -cne
                'hlclient.stock-runtime-reconnect-observation.v1' -or
            [Int64]$publishedReconnect.connection_generation_count -ne 2 -or
            @($publishedReconnect.generations).Count -ne 2) {
            throw 'Published reconnect observation does not retain its exact schema.'
        }
        $reconnectPublicationCapability.Dispose()
        $reconnectPublicationCapability = $null
        Write-Output '[stock-runtime-capture] hardlink-overwrite=blocked'
        Write-Output '[stock-runtime-capture] junction-traversal=blocked'
        Write-Output '[stock-runtime-capture] directory-swap=blocked'
        Write-Output '[stock-runtime-capture] publication-root-swap=blocked'
        Write-Output '[stock-runtime-capture] temporary-substitution=blocked'
        Write-Output '[stock-runtime-capture] orchestrator-start-failure-cleanup=exact'
        Write-Output '[stock-runtime-capture] failed-publication-rollback=exact'
        Write-Output '[stock-runtime-capture] rollback-replacement=preserved'
        Write-Output '[stock-runtime-capture] replacing-publication=retained-handle-exact'
        Write-Output '[stock-runtime-capture] replacing-rollback=prior-manifest-preserved'
        Write-Output '[stock-runtime-capture] replacing-prior-mismatch=blocked'
        Write-Output '[stock-runtime-capture] replacing-substitution=blocked'
        Write-Output '[stock-runtime-capture] final-evidence-batch=exact'
        Write-Output '[stock-runtime-capture] retained-handle-json-read=exact'
        Write-Output '[stock-runtime-capture] restoration-directory-identity=retained-volume-and-file-id'
        Write-Output '[stock-runtime-capture] external-sentinel-metadata=unchanged'
        Write-Output '[stock-runtime-capture] junction-target-metadata=unchanged'
        Write-Output '[stock-runtime-capture] restoration=exact'
        Write-Output '[stock-runtime-capture] result=restoration-self-test-success'
    } finally {
        if ($null -ne $reconnectPublicationCapability) {
            $reconnectPublicationCapability.Dispose()
            $reconnectPublicationCapability = $null
        }
        if ($null -ne $publicationCapability) {
            $publicationCapability.Dispose()
            $publicationCapability = $null
        }
        Close-RestorationGuardCapabilities $guard
        if ($null -ne $guard -and (Test-Path -LiteralPath $guard.TemporaryRoot)) {
            if ([IO.Path]::GetFileName($guard.TemporaryRoot) -notmatch
                '^hlclient-stock-runtime-restore-[0-9a-f]{32}$') {
                throw 'Restoration self-test backup identity is invalid.'
            }
            Assert-PathBelowRoot $guard.TemporaryRoot $systemTemporaryRoot `
                'restoration self-test backup cleanup'
            Assert-NoReparsePointInExistingPath $guard.TemporaryRoot `
                'restoration self-test backup cleanup'
            Remove-SafeTree $guard.TemporaryRoot $systemTemporaryRoot
        }
        if (Test-Path -LiteralPath $selfTestRoot) {
            if ([IO.Path]::GetFileName($selfTestRoot) -notmatch
                '^hlclient-stock-runtime-selftest-[0-9a-f]{32}$') {
                throw 'Restoration self-test root identity is invalid.'
            }
            Assert-PathBelowRoot $selfTestRoot $systemTemporaryRoot `
                'restoration self-test cleanup'
            Assert-NoReparsePointInExistingPath $selfTestRoot `
                'restoration self-test cleanup'
            Remove-SafeTree $selfTestRoot $systemTemporaryRoot
        }
    }
    return
}

if ($PSCmdlet.ParameterSetName -eq 'ServerProfileDiagnosticSelfTest') {
    $values = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::Ordinal)
    foreach ($entry in @(
            @('server-profile-parse-status', 'profile-mismatch'),
            @('server-profile-mismatch-field', 'build'),
            @('server-profile-engine-version-status', 'match'),
            @('server-profile-runtime-mode-status', 'match'),
            @('server-profile-game-status', 'match'),
            @('server-profile-protocol-status', 'match'),
            @('server-profile-build-status', 'mismatch'),
            @('server-profile-endpoint-address-status', 'absent'),
            @('server-profile-endpoint-port-status', 'absent'),
            @('server-profile-map-status', 'absent'),
            @('server-profile-duplicate-fields', '0'),
            @('server-profile-observed-engine-version', '1.1.2.2'),
            @('server-profile-observed-protocol', '48'),
            @('server-profile-observed-build', '10211'),
            @('server-profile-result', 'stock_server_profile_not_supported'))) {
        $values.Add($entry[0], $entry[1])
    }
    $lines = @(Write-StockServerProfileDiagnosticPublicOutput $values)
    $expectedKeys = @(
        'parse-status', 'mismatch-field', 'engine-version-status',
        'runtime-mode-status', 'game-status', 'protocol-status',
        'build-status', 'endpoint-address-status', 'endpoint-port-status',
        'map-status', 'duplicate-fields', 'observed-engine-version',
        'observed-protocol', 'observed-build', 'result')
    $seen = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($line in $lines) {
        if ($line -cnotmatch
            '^\[stock-server-profile\] (?<key>[a-z-]+)=(?<value>[A-Za-z0-9_.-]+)$' -or
            $expectedKeys -cnotcontains $Matches.key -or
            -not $seen.Add($Matches.key)) {
            throw 'Server profile public diagnostic output escaped its allowlist.'
        }
    }
    if ($seen.Count -ne $expectedKeys.Count -or
        $lines -contains
            '[stock-server-profile] mismatch-field=profile-mismatch' -or
        $lines -contains
            '[stock-server-profile] result=server-profile-profile-mismatch' -or
        $lines -notcontains '[stock-server-profile] mismatch-field=build' -or
        $lines -notcontains
            '[stock-server-profile] result=stock_server_profile_not_supported' -or
        ($lines -join "`n") -match
            '(?i)Exe version|Server IP address|map     :|Jan [0-9]|:\\') {
        throw 'Server profile public diagnostic contract is incomplete or leaked raw output.'
    }
    $failureValues = [Collections.Generic.Dictionary[string, string]]::new(
        $values, [StringComparer]::Ordinal)
    $failureValues['server-profile-result'] = 'external_steam_state_changed'
    $failureLines = @(
        Write-StockServerProfileDiagnosticPublicOutput $failureValues)
    if ($failureLines -notcontains
            '[stock-server-profile] result=external_steam_state_changed' -or
        $failureLines -contains
            '[stock-server-profile] result=stock_server_profile_not_supported') {
        throw 'Server profile transaction failure did not replace the profile result.'
    }
    if ($requiredServerProfileDiagnosticRoot -ieq $requiredOutputRoot -or
        $requiredServerProfileDiagnosticRoot -ieq $requiredCanaryOutputRoot -or
        [IO.Path]::GetFileName($requiredServerProfileDiagnosticRoot) -cne
            'stock-runtime-server-profile-diagnostic') {
        throw 'Server profile diagnostic root is campaign/evidence eligible.'
    }
    if ($requiredPrivateServerProfileDiagnosticRoot -ieq $requiredOutputRoot -or
        $requiredPrivateServerProfileDiagnosticRoot -ieq
            $requiredCanaryOutputRoot -or
        $requiredPrivateServerProfileDiagnosticRoot -ieq
            $requiredServerProfileDiagnosticRoot -or
        [IO.Path]::GetFileName($requiredPrivateServerProfileDiagnosticRoot) -cne
            'stock-runtime-server-profile-private') {
        throw 'Private server profile diagnostic root is campaign/evidence eligible.'
    }
    Write-Output '[stock-server-profile-test] typed-mismatch=propagated'
    Write-Output '[stock-server-profile-test] redundant-category=absent'
    Write-Output '[stock-server-profile-test] raw-output=contained'
    Write-Output '[stock-server-profile-test] diagnostic-root=separate'
    Write-Output '[stock-server-profile-test] private-root=separate'
    Write-Output '[stock-server-profile-test] result=success'
    return
}

if ($PSCmdlet.ParameterSetName -eq 'ExternalDriftControl') {
    $research = Resolve-IsolatedResearchRoot
    [void](Get-ResearchSnapshot $research.Root)
    $manifestPath = Resolve-AppManifest $AppManifestPath
    $externalBefore = Get-ExternalSteamStateSnapshot `
        $manifestPath $research.Root $DriftPhase
    if ($DriftDelaySeconds -ne 0) {
        Start-Sleep -Seconds $DriftDelaySeconds
    }
    $externalAfter = Get-ExternalSteamStateSnapshot `
        $manifestPath $research.Root $DriftPhase
    $externalDifference = Compare-StockExternalStateSnapshot `
        $externalBefore $externalAfter $DriftPhase `
        -SteamRewritePolicyId $stockSteamRewritePolicyId
    Write-StockExternalDriftPublicOutput $externalDifference
    Write-Output '[stock-runtime-capture] stock-processes-started=0'
    Write-Output '[stock-runtime-capture] wfp-sessions-started=0'
    Write-Output '[stock-runtime-capture] capture-files-written=0'
    if ([string]$externalDifference.result -cne 'none') {
        throw 'ambient_external_state_drift'
    }
    Write-Output '[stock-runtime-capture] result=success'
    return
}

if ($PSCmdlet.ParameterSetName -eq 'Preflight') {
    $research = Resolve-IsolatedResearchRoot
    [void](Get-ResearchSnapshot $research.Root)
    Write-Output ("[stock-runtime-capture] preparation-manifest={0}" -f
        $research.PreparationManifestSchema)
    Write-Output ("[stock-runtime-capture] external-target-profile={0}" -f
        $research.ExternalTargetProfile)
    Write-Output ("[stock-runtime-capture] external-target-count={0}" -f
        $research.ExternalTargetCount)
    Write-Output '[stock-runtime-capture] research-root=policy-screened-copy-physical-identity-pending'
    Write-Output '[stock-runtime-capture] client-version=1.1.1.1'
    Write-Output '[stock-runtime-capture] server-launcher-version=4.1.1.1'
    Write-Output '[stock-runtime-capture] stock-processes-started=0'
    Write-Output '[stock-runtime-capture] read-only-helper-processes-started=1'
    Write-Output '[stock-runtime-capture] files-written=0'
    Write-Output '[stock-runtime-capture] result=preflight-structural-success-isolation-evidence-pending'
    return
}

if ($PSCmdlet.ParameterSetName -eq 'ActivePreflight') {
    if (-not (Test-IsElevatedAdministrator)) {
        Write-Output '[stock-runtime-capture] active-environment=invalid'
        Write-Output '[stock-runtime-capture] failure-category=network_isolation_privilege_required'
        Write-Output '[stock-runtime-capture] stock-processes-started=0'
        Write-Output '[stock-runtime-capture] capture-files-written=0'
        throw 'Active environment validation requires an elevated PowerShell; automatic elevation is forbidden.'
    }
    try {
        $research = Resolve-IsolatedResearchRoot
    } catch {
        if ($_.Exception.Message -ceq
            'research_copy_not_evidence_eligible') {
            Write-Output '[stock-runtime-capture] active-environment=invalid'
            Write-Output '[stock-runtime-capture] failure-category=research_copy_not_evidence_eligible'
            Write-Output '[stock-runtime-capture] stock-processes-started=0'
            Write-Output '[stock-runtime-capture] capture-files-written=0'
            Write-Output '[stock-runtime-capture] wfp-sessions-started=0'
        }
        throw
    }
    [void](Get-ResearchSnapshot $research.Root)
    Write-Output ("[stock-runtime-capture] preparation-manifest={0}" -f
        $research.PreparationManifestSchema)
    Write-Output ("[stock-runtime-capture] external-target-profile={0}" -f
        $research.ExternalTargetProfile)
    Write-Output ("[stock-runtime-capture] external-target-count={0}" -f
        $research.ExternalTargetCount)
    $tool = Resolve-TrustedRepositoryTool $CaptureToolPath `
        'hlclient_stock_runtime_capture.exe' 'stock runtime relay'
    if ($ExpectedCaptureToolSha256 -and
        (Get-FileSha256 $tool) -cne $ExpectedCaptureToolSha256.ToUpperInvariant()) {
        throw 'Capture tool SHA-256 does not match the reviewed value.'
    }
    $guardPath = Resolve-TrustedRepositoryTool $NetworkIsolationGuardPath `
        'hlclient_stock_runtime_isolation_guard.exe' 'network isolation guard'
    $orchestratorPath = Resolve-TrustedRepositoryTool `
        (Join-Path (Split-Path -Parent $tool) 'hlclient_stock_runtime_orchestrator.exe') `
        'hlclient_stock_runtime_orchestrator.exe' 'stock runtime orchestrator'
    $manifestPath = Resolve-AppManifest $AppManifestPath
    $externalBefore = Get-ExternalSteamStateSnapshot `
        $manifestPath $research.Root 'wfp_preflight'
    $arguments = @(
        '--validate-environment', '--research-root', $research.Root,
        '--client', $research.Client, '--server', $research.Server,
        '--relay', $tool, '--isolation-guard', $guardPath,
        '--app-manifest', $manifestPath, '--game', 'valve', '--map', 'boot_camp',
        '--relay-port', [string]$RelayPort, '--server-port', [string]$ServerPort)
    $result = Invoke-BoundedOrchestrator $orchestratorPath $arguments 90
    $externalAfter = Get-ExternalSteamStateSnapshot `
        $manifestPath $research.Root 'wfp_preflight'
    $externalDifference = Compare-StockExternalStateSnapshot `
        $externalBefore $externalAfter 'wfp_preflight' `
        -SteamRewritePolicyId $stockSteamRewritePolicyId
    Write-StockExternalDriftPublicOutput $externalDifference
    if ([string]$externalDifference.result -cne 'none') {
        throw 'external_steam_state_changed'
    }
    if ($result.ExitCode -ne 0) {
        $category = 'active_environment_validation_failed'
        if ($result.Values.ContainsKey('failure-category')) {
            $category = $result.Values['failure-category']
        }
        Write-Output '[stock-runtime-capture] active-environment=invalid'
        Write-Output "[stock-runtime-capture] failure-category=$category"
        Write-Output '[stock-runtime-capture] stock-processes-started=0'
        Write-Output '[stock-runtime-capture] capture-files-written=0'
        throw "Active environment validation failed with typed category $category."
    }
    Assert-OrchestratorValue $result active-environment valid
    Assert-OrchestratorValue $result preflight-schema `
        hlclient.stock-active-capture-preflight-attestation.v1
    Assert-OrchestratorValue $result elevation-status verified
    Assert-OrchestratorValue $result isolation-canary success
    Assert-OrchestratorValue $result binary-profile valid
    Assert-OrchestratorValue $result app-manifest valid
    Assert-OrchestratorValue $result wfp-session dynamic
    Assert-OrchestratorValue $result ipv4-loopback allowed
    if (-not $result.Values.ContainsKey('ipv6-loopback') -or
        @('allowed', 'capability-unavailable') -cnotcontains
            $result.Values['ipv6-loopback']) {
        throw 'Project orchestrator emitted an invalid IPv6 canary status.'
    }
    Assert-OrchestratorValue $result non-loopback-canary denied-os-classified
    Assert-OrchestratorValue $result isolation-cleanup exact
    Assert-OrchestratorValue $result timestamp-category current-session
    Assert-OrchestratorValue $result stock-processes-started 0
    Assert-OrchestratorValue $result capture-files-written 0
    Assert-OrchestratorValue $result result success
    Write-Output '[stock-runtime-capture] active-environment=valid'
    Write-Output '[stock-runtime-capture] preflight-schema=hlclient.stock-active-capture-preflight-attestation.v1'
    Write-Output '[stock-runtime-capture] elevation-status=verified'
    Write-Output '[stock-runtime-capture] isolation-canary=success'
    Write-Output '[stock-runtime-capture] binary-profile=valid'
    Write-Output '[stock-runtime-capture] app-manifest=valid'
    Write-Output '[stock-runtime-capture] wfp-session=dynamic'
    Write-Output '[stock-runtime-capture] ipv4-loopback=allowed'
    Write-Output ("[stock-runtime-capture] ipv6-loopback={0}" -f
        $result.Values['ipv6-loopback'])
    Write-Output '[stock-runtime-capture] non-loopback-canary=denied-os-classified'
    Write-Output '[stock-runtime-capture] isolation-cleanup=exact'
    Write-Output '[stock-runtime-capture] timestamp-category=current-session'
    Write-Output '[stock-runtime-capture] stock-processes-started=0'
    Write-Output '[stock-runtime-capture] capture-files-written=0'
    Write-Output '[stock-runtime-capture] result=success'
    return
}

$scenarioAliases = @{
    'drop-server-runtime' = 'drop-server-to-client-transport-ordinal'
    'duplicate-server-runtime' = 'duplicate-server-to-client-transport-ordinal'
    'reorder-server-runtime' = 'reorder-server-to-client-transport-ordinal'
}
$canonicalScenario = if ($functionalSmokeMode) {
    'local_research_copy_smoke_v1'
} elseif ($functionalRuntimeCaptureMode) {
    'idle-runtime'
} elseif ($anyServerProfileDiagnosticMode) {
    'server-profile-diagnostic'
} elseif ($scenarioAliases.ContainsKey($Scenario)) {
    $scenarioAliases[$Scenario]
} else { $Scenario }
$orchestratorScenario = switch ($canonicalScenario) {
    'drop-server-to-client-transport-ordinal' { 'drop-server-runtime'; break }
    'duplicate-server-to-client-transport-ordinal' { 'duplicate-server-runtime'; break }
    'reorder-server-to-client-transport-ordinal' { 'reorder-server-runtime'; break }
    default { $canonicalScenario }
}
if (-not $functionalPolicyMode -and -not $anyServerProfileDiagnosticMode -and
    $ServerProfileId -cne 'legacy-stdio-hlds-banner-v1') {
    throw 'Observed HLDS profile is production-inactive until the verified rewrite threshold and activation gates pass.'
}

if (-not $functionalPolicyMode -and -not $anyServerProfileDiagnosticMode -and
    ($canonicalScenario -ceq 'baseline' -or
        $canonicalScenario -ceq 'idle-runtime') -and
    $MaximumDurationSeconds -lt 30) {
    Write-Output '[stock-runtime-capture] active-capture=blocked'
    Write-Output '[stock-runtime-capture] failure-category=minimum_observation_duration_required'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] files-written=0'
    throw 'Accepted baseline and idle-runtime observations require a requested duration of at least 30 seconds.'
}

if (-not $functionalPolicyMode -and -not $anyServerProfileDiagnosticMode -and
    $canonicalScenario -ceq 'reconnect' -and
    $MaximumDurationSeconds -lt 60) {
    Write-Output '[stock-runtime-capture] active-capture=blocked'
    Write-Output '[stock-runtime-capture] failure-category=minimum_reconnect_duration_required'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] files-written=0'
    throw 'A two-generation reconnect observation requires at least 60 seconds.'
}

$research = $null
$steamApiRuntime = $null
if ($functionalPolicyMode) {
    # Copy validation is read-only and does not require WFP privileges. Run it
    # before the elevation gate so a non-elevated caller can distinguish an
    # invalid research copy from an otherwise launch-ready environment.
    $research = Resolve-IsolatedResearchRoot
    Write-Output '[stock-runtime-capture] functional-research-validation=success'
    Write-Output ("[stock-runtime-capture] research-inventory-status={0}" -f
        $research.InventoryStatus)
}
if ($projectClientStockSignonMode) {
    $steamApiRuntime = [IO.Path]::GetFullPath($SteamApiRuntimePath)
    if ([IO.Path]::GetFileName($steamApiRuntime) -cne 'steam_api.dll' -or
        -not (Test-Path -LiteralPath $steamApiRuntime -PathType Leaf)) {
        throw 'SteamApiRuntimePath must name an existing official steam_api.dll.'
    }
    Assert-NoReparsePointInExistingPath $steamApiRuntime 'Steam API runtime'
    Assert-OnlyDefaultDataStream $steamApiRuntime 'Steam API runtime'
    Assert-NoHardLink $steamApiRuntime 'Steam API runtime'
    $steamApiSignature = Get-AuthenticodeSignature -LiteralPath $steamApiRuntime
    if ($steamApiSignature.Status -ne 'Valid' -or
        $null -eq $steamApiSignature.SignerCertificate -or
        $steamApiSignature.SignerCertificate.Subject -cnotmatch
            '^CN=Valve Corp\.(?:,|$)') {
        throw 'Steam API runtime is not validly Valve-signed.'
    }
    Write-Output '[stock-runtime-capture] steam-api-runtime=validated-official-x86-candidate'
}

if (-not (Test-IsElevatedAdministrator)) {
    Write-Output '[stock-runtime-capture] active-capture=blocked'
    Write-Output '[stock-runtime-capture] failure-category=network_isolation_privilege_required'
    Write-Output '[stock-runtime-capture] processes-started=0'
    Write-Output '[stock-runtime-capture] files-written=0'
    Write-Output '[stock-runtime-capture] network-operations=0'
    throw 'Active capture requires an elevated PowerShell; automatic elevation is forbidden.'
}

if ($RelayPort -eq $ServerPort -or $MaximumPayloadBytes -gt $MaximumTotalRawBytes -or
    $MaximumDecompressedBytes -lt $MaximumReassembledBytes -or
    $MaximumRuntimeFrames -gt $MaximumMessageCount -or
    $MaximumClientPackets -gt $MaximumDatagrams -or
    $MaximumServerPackets -gt $MaximumDatagrams) {
    throw 'Capture limits violate cross-field policy.'
}
$activeScenarios = @(
    'baseline', 'idle-runtime', 'reconnect',
    'drop-server-to-client-transport-ordinal',
    'duplicate-server-to-client-transport-ordinal',
    'reorder-server-to-client-transport-ordinal')
if (-not $functionalPolicyMode -and -not $anyServerProfileDiagnosticMode -and
    $activeScenarios -cnotcontains $canonicalScenario) {
    throw 'The requested scenario is outside the M4.7.1.1 active-capture allowlist; no run was started.'
}
try {
    if ($null -eq $research) {
        $research = Resolve-IsolatedResearchRoot
    }
} catch {
    if ($_.Exception.Message -ceq 'research_copy_not_evidence_eligible') {
        Write-Output '[stock-runtime-capture] active-capture=blocked'
        Write-Output '[stock-runtime-capture] failure-category=research_copy_not_evidence_eligible'
        Write-Output '[stock-runtime-capture] processes-started=0'
        Write-Output '[stock-runtime-capture] stock-processes-started=0'
        Write-Output '[stock-runtime-capture] files-written=0'
        Write-Output '[stock-runtime-capture] capture-files-written=0'
        Write-Output '[stock-runtime-capture] network-operations=0'
        Write-Output '[stock-runtime-capture] wfp-sessions-started=0'
        Write-Output '[stock-runtime-capture] capture-runs-created=0'
        Write-Output '[stock-runtime-capture] restoration-backups-created=0'
    }
    throw
}
Write-Output ("[stock-runtime-capture] preparation-manifest={0}" -f
    $research.PreparationManifestSchema)
Write-Output ("[stock-runtime-capture] external-target-profile={0}" -f
    $research.ExternalTargetProfile)
Write-Output ("[stock-runtime-capture] external-target-count={0}" -f
    $research.ExternalTargetCount)
Write-Output ("[stock-runtime-capture] research-inventory-status={0}" -f
    $research.InventoryStatus)
$output = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
if ($serverProfileDiagnosticMode) {
    if ($output -ine $requiredServerProfileDiagnosticRoot) {
        throw 'ServerProfileDiagnostic requires the exact repository manual-artifacts/stock-runtime-server-profile-diagnostic root.'
    }
} elseif ($privateServerProfileDiagnosticMode) {
    if ($output -ine $requiredPrivateServerProfileDiagnosticRoot) {
        throw 'PrivateServerProfileDiagnostic requires the exact repository manual-artifacts/stock-runtime-server-profile-private root.'
    }
} elseif ($functionalSmokeMode) {
    if ($output -ine $requiredFunctionalSmokeRoot) {
        throw 'FunctionalSmoke requires the exact repository manual-artifacts/research-copy-smoke root.'
    }
} elseif ($functionalRuntimeCaptureMode) {
    if ($output -ine $requiredFunctionalRuntimeCaptureRoot) {
        throw 'FunctionalRuntimeCapture requires the exact repository manual-artifacts/research-runtime-capture root.'
    }
} elseif ($PreCampaignCanary) {
    if ($canonicalScenario -cne 'baseline' -or $Map -cne 'boot_camp' -or
        $output -ine $requiredCanaryOutputRoot) {
        throw 'PreCampaignCanary requires exact boot_camp/baseline and the repository manual-artifacts/stock-runtime-canary root.'
    }
} elseif ($output -ine $requiredOutputRoot) {
    throw 'OutputRoot must be the exact repository manual-artifacts/stock-runtime root.'
}
Assert-NoReparsePointInExistingPath $output 'stock runtime output root'
$tool = Resolve-TrustedRepositoryTool $CaptureToolPath `
    'hlclient_stock_runtime_capture.exe' 'stock runtime relay'
if ($ExpectedCaptureToolSha256 -and
    (Get-FileSha256 $tool) -cne $ExpectedCaptureToolSha256.ToUpperInvariant()) {
    throw 'Capture tool SHA-256 does not match the reviewed value.'
}
$guardPath = Resolve-TrustedRepositoryTool $NetworkIsolationGuardPath `
    'hlclient_stock_runtime_isolation_guard.exe' 'network isolation guard'
$toolDirectory = Split-Path -Parent $tool
$orchestratorPath = Resolve-TrustedRepositoryTool `
    (Join-Path $toolDirectory 'hlclient_stock_runtime_orchestrator.exe') `
    'hlclient_stock_runtime_orchestrator.exe' 'stock runtime orchestrator'
$checkerPath = Resolve-TrustedRepositoryTool `
    (Join-Path $toolDirectory 'hlclient_stock_runtime_check.exe') `
    'hlclient_stock_runtime_check.exe' 'stock runtime checker'
$manifestPath = Resolve-AppManifest $AppManifestPath
$walkerPath = Join-Path $PSScriptRoot 'walk_stock_runtime_transport.ps1'
if (-not (Test-Path -LiteralPath $walkerPath -PathType Leaf)) {
    throw 'Independent transport walker is absent.'
}

# Prove the binary profile and dynamic-isolation canary before creating a
# restoration backup or allowing the orchestrator to create a run directory.
# The active orchestrator repeats these checks inside the owned transaction;
# this first pass is the mutation-free-to-game-files environment gate.
$activeValidationArguments = @(
    '--validate-environment', '--research-root', $research.Root,
    '--client', $research.Client, '--server', $research.Server,
    '--relay', $tool, '--isolation-guard', $guardPath,
    '--app-manifest', $manifestPath, '--game', 'valve', '--map', $Map,
    '--relay-port', [string]$RelayPort, '--server-port', [string]$ServerPort)
if ($projectClientStockSignonMode) {
    $activeValidationArguments += @(
        '--project-client-stock-signon', '--steam-api-runtime',
        $steamApiRuntime, '--project-client-stop', $ProjectClientStop)
    if ($projectClientVisualMode) {
        $activeValidationArguments += @(
            '--project-client-live-input', $ProjectClientLiveInput)
        if ($ProjectClientPrediction -ceq 'reference') {
            $activeValidationArguments += @(
                '--project-client-prediction', 'reference')
        }
    }
}
$activeValidation = Invoke-BoundedOrchestrator $orchestratorPath `
    $activeValidationArguments 90
if ($activeValidation.ExitCode -ne 0) {
    $category = 'active_environment_validation_failed'
    if ($activeValidation.Values.ContainsKey('failure-category')) {
        $category = $activeValidation.Values['failure-category']
    }
    Write-Output '[stock-runtime-capture] active-capture=blocked'
    Write-Output "[stock-runtime-capture] failure-category=$category"
    Write-Output '[stock-runtime-capture] stock-processes-started=0'
    Write-Output '[stock-runtime-capture] capture-files-written=0'
    Write-Output '[stock-runtime-capture] restoration-backups-created=0'
    throw "Active environment validation failed before backup/run creation: $category."
}
Assert-OrchestratorValue $activeValidation active-environment valid
Assert-OrchestratorValue $activeValidation preflight-schema `
    hlclient.stock-active-capture-preflight-attestation.v1
Assert-OrchestratorValue $activeValidation elevation-status verified
Assert-OrchestratorValue $activeValidation isolation-canary success
Assert-OrchestratorValue $activeValidation binary-profile valid
Assert-OrchestratorValue $activeValidation app-manifest valid
Assert-OrchestratorValue $activeValidation wfp-session dynamic
Assert-OrchestratorValue $activeValidation ipv4-loopback allowed
if (-not $activeValidation.Values.ContainsKey('ipv6-loopback') -or
    @('allowed', 'capability-unavailable') -cnotcontains
        $activeValidation.Values['ipv6-loopback']) {
    throw 'Active preflight emitted an invalid IPv6 canary status.'
}
Assert-OrchestratorValue $activeValidation non-loopback-canary denied-os-classified
Assert-OrchestratorValue $activeValidation isolation-cleanup exact
Assert-OrchestratorValue $activeValidation timestamp-category current-session
Assert-OrchestratorValue $activeValidation stock-processes-started 0
Assert-OrchestratorValue $activeValidation capture-files-written 0
Assert-OrchestratorValue $activeValidation result success

$before = Get-ResearchSnapshot $research.Root
$externalDriftPhase = if ($privateServerProfileDiagnosticMode) {
    'private_server_diagnostic'
} elseif ($serverProfileDiagnosticMode) {
    'standard_server_diagnostic'
} else { 'standard_server_diagnostic' }
$externalBefore = if ($functionalPolicyMode) { $null } else {
    Get-ExternalSteamStateSnapshot `
        $manifestPath $research.Root $externalDriftPhase
}
$guard = New-RestorationGuard $research.Root $before
$runId = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $output $runId
$orchestratorResult = $null
$orchestratorExitCode = 255
$wrapperCapability = [IntPtr]::Zero
$wrapperCleanupCapability = [IntPtr]::Zero
$wrapperJob = [IntPtr]::Zero
$wrapperGuardJob = [IntPtr]::Zero
$isolationReleaseCapability = [IntPtr]::Zero
$writerTracePrelaunchReadyCapability = [IntPtr]::Zero
$writerTraceLaunchReleaseCapability = [IntPtr]::Zero
$writerTraceStockStoppedCapability = [IntPtr]::Zero
$writerTraceRequest = $null
if ($EnableWriterTraceHandoff) {
    $writerTraceRequest = New-StockWriterTraceRequest `
        -TransactionId $runId -ToolPath $WriterTraceToolPath `
        -ExpectedToolSha256 $ExpectedWriterTraceToolSha256 `
        -TargetPath $WriterTraceTargetPath `
        -OutputPath (Join-Path $output ("writer-trace-$runId.json")) `
        -TraceReadyTimeoutMilliseconds 5000 -TailMilliseconds 15000 `
        -DrainTimeoutMilliseconds 5000 -HardTraceDeadlineMilliseconds 60000
}
$orchestratorExitState = New-OrchestratorExitState
$primaryError = $null
$cleanupErrors = [Collections.Generic.List[string]]::new()
$after = $null
$externalAfter = $null
$runDirectoryCapability = $null
try {
    # This inheritable, one-use event is created only after the complete
    # restoration backup exists.  The active C++ orchestrator verifies the
    # actual parent PID and signals the first event before any environment
    # mutation or process launch.  A distinct event is signalled only after
    # typed Job cleanup reaches zero, independently of stdout parsing.
    $wrapperCapability = New-OrchestratorTransactionCapability
    $wrapperCleanupCapability = New-OrchestratorTransactionCapability
    $wrapperJob = New-OrchestratorProcessJobCapability
    $wrapperGuardJob = New-OrchestratorProcessJobCapability
    $isolationReleaseCapability = New-OrchestratorTransactionCapability
    if ($EnableWriterTraceHandoff) {
        $writerTracePrelaunchReadyCapability =
            New-OrchestratorTransactionCapability
        $writerTraceLaunchReleaseCapability =
            New-OrchestratorTransactionCapability
        $writerTraceStockStoppedCapability =
            New-OrchestratorTransactionCapability
    }
    $arguments = @(
        $(if ($functionalSmokeMode) {
                '--functional-confirmation-token'
            } elseif ($privateServerProfileDiagnosticMode) {
                '--private-diagnostic-token'
            } else { '--confirmation-token' }),
        $(if ($functionalSmokeMode) {
                $functionalSmokeToken
            } elseif ($privateServerProfileDiagnosticMode) {
                $privateDiagnosticToken
            } else { $activeCaptureToken }),
        '--wrapper-capability-handle', $wrapperCapability.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--wrapper-cleanup-capability-handle',
        $wrapperCleanupCapability.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--wrapper-job-handle', $wrapperJob.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--wrapper-guard-job-handle', $wrapperGuardJob.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--isolation-release-handle',
        $isolationReleaseCapability.ToInt64().ToString(
            [Globalization.CultureInfo]::InvariantCulture),
        '--wrapper-process-id', [string]$PID,
        '--run-root', $runRoot, '--research-root', $research.Root,
        '--client', $research.Client, '--server', $research.Server,
        '--relay', $tool, '--isolation-guard', $guardPath,
        '--app-manifest', $manifestPath, '--game', $Game, '--map', $Map,
        '--relay-port', [string]$RelayPort,
        '--server-port', [string]$ServerPort,
        '--server-profile-id', $ServerProfileId,
        '--max-duration-seconds', [string]$MaximumDurationSeconds,
        '--max-datagrams', [string]$MaximumDatagrams,
        '--max-total-raw-bytes', [string]$MaximumTotalRawBytes,
        '--max-payload-bytes', [string]$MaximumPayloadBytes,
        '--max-reassembled-bytes', [string]$MaximumReassembledBytes,
        '--max-decompressed-bytes', [string]$MaximumDecompressedBytes,
        '--max-message-count', [string]$MaximumMessageCount,
        '--max-runtime-frames', [string]$MaximumRuntimeFrames,
        '--max-client-packets', [string]$MaximumClientPackets,
        '--max-server-packets', [string]$MaximumServerPackets,
        '--mutation-after-client-packets', [string]$MutationAfterClientPackets,
        '--mutation-after-server-packets', [string]$MutationAfterServerPackets)
    if ($functionalSmokeMode) {
        $arguments += '--functional-smoke'
        if ($projectClientStockSignonMode) {
            $arguments += @(
                '--project-client-stock-signon', '--steam-api-runtime',
                $steamApiRuntime, '--project-client-stop', $ProjectClientStop)
            if ($projectClientVisualMode) {
                $arguments += @(
                    '--project-client-live-input', $ProjectClientLiveInput)
                if ($ProjectClientPrediction -ceq 'reference') {
                    $arguments += @(
                        '--project-client-prediction', 'reference')
                }
            }
        }
    } else {
        $arguments += @('--output-role', $(if ($privateServerProfileDiagnosticMode) {
                    'server-profile-private-diagnostic'
                } elseif ($serverProfileDiagnosticMode) {
                    'server-profile-diagnostic'
                } elseif ($functionalRuntimeCaptureMode) {
                    'functional-runtime-capture'
                } elseif ($PreCampaignCanary) {
                    'pre-campaign-canary'
                } else {
                    'normal-campaign-run'
                }))
    }
    if ($EnableWriterTraceHandoff) {
        $arguments += @(
            '--writer-trace-prelaunch-ready-handle',
            $writerTracePrelaunchReadyCapability.ToInt64().ToString(
                [Globalization.CultureInfo]::InvariantCulture),
            '--writer-trace-launch-release-handle',
            $writerTraceLaunchReleaseCapability.ToInt64().ToString(
                [Globalization.CultureInfo]::InvariantCulture),
            '--writer-trace-stock-stopped-handle',
            $writerTraceStockStoppedCapability.ToInt64().ToString(
                [Globalization.CultureInfo]::InvariantCulture))
    }
    if ($functionalSmokeMode) {
        # Functional smoke deliberately has no capture scenario or evidence role.
    } elseif ($anyServerProfileDiagnosticMode) {
        $arguments += $(if ($privateServerProfileDiagnosticMode) {
                '--private-diagnose-server-profile'
            } else { '--diagnose-server-profile' })
    } else {
        $arguments += @('--scenario', $orchestratorScenario)
    }
    $orchestratorResult = Invoke-BoundedOrchestrator $orchestratorPath $arguments `
        ($MaximumDurationSeconds + 90) $wrapperCapability `
        $wrapperCleanupCapability $wrapperJob $wrapperGuardJob `
        $isolationReleaseCapability $orchestratorExitState `
        $writerTracePrelaunchReadyCapability `
        $writerTraceLaunchReleaseCapability `
        $writerTraceStockStoppedCapability $writerTraceRequest
    $orchestratorExitCode = $orchestratorResult.ExitCode
    if ($functionalSmokeMode -or $functionalRuntimeCaptureMode) {
        foreach ($diagnosticKey in @(
                'processes-started', 'server-ready', 'client-ready',
                'server-process-created', 'server-process-id',
                'client-process-created', 'client-process-id',
                'client-image-identity', 'client-resume-result',
                'client-initialized', 'connect-requested',
                'connection-status', 'client-map-entry-status',
                'map-entry-source', 'last-confirmed-stage',
                'client-steam-argument', 'server-logging',
                'client-connect-port',
                'steam-authentication-error-observed',
                'application-entry-observed', 'arguments-accepted',
                'provider-begin-observed', 'steam-api-init-attempted',
                'steam-api-initialized', 'fresh-material-acquired',
                'connect-sent', 'connection-accepted',
                'serverinfo-received', 'schema-registry-received',
                'authentication-status', 'serverinfo-protocol',
                'serverinfo-max-clients', 'serverinfo-game', 'serverinfo-map',
                'schema-count', 'schema-field-count',
                'client-name-observed', 'client-entered-game-observed',
                'client-running-at-readiness-deadline',
                'stable-duration-ms', 'client-exit-code',
                'client-exit-code-hex', 'client-wait-result',
                'client-wait-native-error',
                'server-exit-code', 'diagnostic-publication',
                'jump-duck-result', 'speed-result', 'prediction-result', 'jump-observed',
                'descent-observed', 'duck-observed',
                'release-response-observed',
                'jump-new-submitted', 'duck-new-submitted',
                'relay-phase', 'failed-operation', 'native-error-domain',
                'native-error-code', 'relay-exit-code',
                'relay-exit-code-hex', 'wait-result', 'stop-requested',
                'journal-publication-state',
                'metadata-publication-state')) {
            if ($orchestratorResult.Values.ContainsKey($diagnosticKey)) {
                Write-Output ("[stock-runtime-capture] {0}={1}" -f
                    $diagnosticKey,
                    $orchestratorResult.Values[$diagnosticKey])
            }
        }
        if ($functionalRuntimeCaptureMode) {
            foreach ($requiredRelayDiagnosticKey in @(
                    'relay-phase', 'failed-operation',
                    'native-error-domain', 'native-error-code',
                    'relay-exit-code', 'relay-exit-code-hex',
                    'wait-result', 'stop-requested',
                    'journal-publication-state',
                    'metadata-publication-state')) {
                if (-not $orchestratorResult.Values.ContainsKey(
                        $requiredRelayDiagnosticKey)) {
                    throw "orchestrator_relay_diagnostic_missing:$requiredRelayDiagnosticKey"
                }
            }
        }
    }
    if ($orchestratorExitCode -ne 0) {
        $category = 'orchestrator_failed'
        if ($orchestratorResult.Values.ContainsKey('failure-category')) {
            $category = $orchestratorResult.Values['failure-category']
        }
        throw "Stock runtime orchestrator failed: $category."
    }
    Assert-OrchestratorValue $orchestratorResult orchestrator success
    Assert-OrchestratorValue $orchestratorResult failure-category none
    Assert-OrchestratorValue $orchestratorResult result success
    Assert-OrchestratorValue $orchestratorResult job-cleanup exact
    if ($EnableWriterTraceHandoff) {
        Assert-OrchestratorValue $orchestratorResult `
            writer-trace-prelaunch-ready true
        Assert-OrchestratorValue $orchestratorResult `
            writer-trace-launch-released true
        Assert-OrchestratorValue $orchestratorResult `
            writer-trace-stock-processes-stopped true
        if (-not $orchestratorExitState.WriterTraceFinalized) {
            throw 'writer_trace_not_finalized_before_post_run_inventory'
        }
    } else {
        Assert-OrchestratorValue $orchestratorResult `
            writer-trace-prelaunch-ready false
        Assert-OrchestratorValue $orchestratorResult `
            writer-trace-launch-released false
        Assert-OrchestratorValue $orchestratorResult `
            writer-trace-stock-processes-stopped false
    }
    Assert-OrchestratorValue $orchestratorResult server-profile-id `
        $ServerProfileId
    if ($functionalSmokeMode) {
        foreach ($entry in ([ordered]@{
                mode = $(if ($projectClientStockSignonMode) {
                        if ($projectClientVisualMode) {
                            'project_client_live_visual_control_v1'
                        } elseif ($projectClientUserCmdMode) {
                            'project_client_live_usercmd_check_v1'
                        } elseif ($projectClientLiveRuntimeMode) {
                            'project_client_live_runtime_state_v1'
                        } else { 'project_client_stock_signon_v1' }
                    } else { 'local_research_copy_smoke_v1' })
                purpose = $(if ($projectClientStockSignonMode) {
                        if ($projectClientVisualMode) {
                            'fresh_project_client_live_visual_control'
                        } elseif ($projectClientUserCmdMode) {
                            'fresh_project_client_usercmd_server_motion'
                        } elseif ($projectClientLiveRuntimeMode) {
                            'fresh_project_client_live_runtime_state'
                        } else { 'fresh_project_client_stock_signon' }
                    } else { 'functional_smoke' })
                'evidence-eligible' = $(if ($projectClientStockSignonMode) {
                        'true'
                    } else { 'false' })
                route = 'direct_loopback'
                'external-steam-state' = $(if ($projectClientStockSignonMode) {
                        'current_user_session_used'
                    } else { 'not_assessed' })
                'external-steam-state-policy' = $(if (
                        $projectClientStockSignonMode) {
                        'provider_runtime_only_no_credentials_or_material_retained'
                    } else { 'functional_observation_only' })
                'relay-ready' = 'false'
                'server-ready' = 'true'
                'client-ready' = 'true'
                'bounded-transport-complete' = 'false'
            }).GetEnumerator()) {
            Assert-OrchestratorValue $orchestratorResult $entry.Key $entry.Value
        }
        if ($projectClientStockSignonMode) {
            foreach ($entry in ([ordered]@{
                    'steam-api-initialized' = 'true'
                    'fresh-material-acquired' = 'true'
                    'connect-sent' = 'true'
                    'connection-accepted' = 'true'
                    'serverinfo-received' = 'true'
                    'schema-registry-received' = 'true'
                    'authentication-status' = 'pending-or-unknown'
                    'last-confirmed-stage' = $(if (
                            $projectClientVisualMode) {
                            if ($ProjectClientPrediction -ceq 'reference') {
                                'live_local_prediction_and_reconciliation_verified'
                            } elseif ($ProjectClientLiveInput -ceq
                                    'scripted-jump-duck-check') {
                                'live_jump_duck_server_verified'
                            } elseif ($ProjectClientLiveInput -ceq
                                    'scripted-speed-check') {
                                'live_normal_speed_and_shift_walk_verified'
                            } else { 'live_visual_control_verified' }
                        } elseif ($projectClientUserCmdMode) {
                            'live_usercmd_server_motion_verified'
                        } elseif ($projectClientLiveRuntimeMode) {
                            'live_runtime_state_ready'
                        } else { 'delta_schemas_ready' })
                }).GetEnumerator()) {
                Assert-OrchestratorValue $orchestratorResult `
                    $entry.Key $entry.Value
            }
            if ($projectClientLiveMode) {
                foreach ($entry in ([ordered]@{
                        'resource-continuation-sent' = 'true'
                        'spawn-request-transmitted' = 'true'
                        'live-service-payloads-received' = 'true'
                        'client-world-state-published' = $(if (
                                $projectClientVisualMode -and
                                $ProjectClientLiveInput -ceq 'keyboard-mouse') {
                                'false'
                            } else { 'true' })
                    }).GetEnumerator()) {
                    Assert-OrchestratorValue $orchestratorResult `
                        $entry.Key $entry.Value
                }
                if ($projectClientLiveRuntimeMode) {
                    Assert-OrchestratorValue $orchestratorResult `
                        'usercmd-transmitted' '0'
                } elseif ($projectClientVisualMode) {
                    foreach ($entry in ([ordered]@{
                            'usercmd-transmitted' = 'true'
                            'live-visual-verified' = 'true'
                        }).GetEnumerator()) {
                        Assert-OrchestratorValue $orchestratorResult `
                            $entry.Key $entry.Value
                    }
                    foreach ($countKey in @(
                            'usercmd-generated', 'usercmd-new',
                            'usercmd-packets', 'usercmd-server-samples')) {
                        [Int64]$observedCount = 0
                        if (-not $orchestratorResult.Values.ContainsKey($countKey) -or
                            -not [Int64]::TryParse(
                                $orchestratorResult.Values[$countKey],
                                [ref]$observedCount) -or $observedCount -le 0) {
                            throw "Live visual control did not report a positive $countKey."
                        }
                    }
                    if ($ProjectClientPrediction -ceq 'reference') {
                        Assert-OrchestratorValue $orchestratorResult `
                            'prediction-result' `
                            'live_local_prediction_and_reconciliation_verified'
                    } elseif ($ProjectClientLiveInput -ceq
                            'scripted-jump-duck-check') {
                        Assert-GJumpDuckNativeSummary `
                            -Values $orchestratorResult.Values
                    } elseif ($ProjectClientLiveInput -ceq
                            'scripted-speed-check') {
                        Assert-H1SpeedNativeSummary `
                            -Values $orchestratorResult.Values
                    }
                } else {
                    foreach ($entry in ([ordered]@{
                            'usercmd-transmitted' = 'true'
                            'usercmd-movement-verified' = 'true'
                        }).GetEnumerator()) {
                        Assert-OrchestratorValue $orchestratorResult `
                            $entry.Key $entry.Value
                    }
                    foreach ($countKey in @(
                            'usercmd-generated', 'usercmd-new',
                            'usercmd-packets', 'usercmd-server-samples')) {
                        [Int64]$observedCount = 0
                        if (-not $orchestratorResult.Values.ContainsKey($countKey) -or
                            -not [Int64]::TryParse(
                                $orchestratorResult.Values[$countKey],
                                [ref]$observedCount) -or $observedCount -le 0) {
                            throw "Live usercmd check did not report a positive $countKey."
                        }
                    }
                }
            }
        } else {
            [Int64]$stableDuration = 0
            if (-not $orchestratorResult.Values.ContainsKey('stable-duration-ms') -or
                -not [Int64]::TryParse(
                    $orchestratorResult.Values['stable-duration-ms'],
                    [ref]$stableDuration) -or $stableDuration -lt 30000) {
                throw 'Functional smoke did not retain a stable 30-second map session.'
            }
        }
    } elseif ($anyServerProfileDiagnosticMode) {
        Assert-OrchestratorValue $orchestratorResult relay-ready false
        Assert-OrchestratorValue $orchestratorResult client-ready false
        Assert-OrchestratorValue $orchestratorResult bounded-transport-complete false
        $requiredProfileKeys = @(
            'server-profile-parse-status', 'server-profile-mismatch-field',
            'server-profile-engine-version-status',
            'server-profile-runtime-mode-status', 'server-profile-game-status',
            'server-profile-protocol-status', 'server-profile-build-status',
            'server-profile-endpoint-address-status',
            'server-profile-endpoint-port-status', 'server-profile-map-status',
            'server-profile-duplicate-fields',
            'server-profile-process-log-truncated',
            'server-profile-endpoint-address-category',
            'server-profile-runtime-mode-category',
            'server-profile-observed-byte-count',
            'server-profile-observed-line-count', 'server-profile-result')
        foreach ($profileKey in $requiredProfileKeys) {
            if (-not $orchestratorResult.Values.ContainsKey($profileKey)) {
                throw "Server profile diagnostic omitted $profileKey."
            }
        }
        if (@('valid', 'profile-mismatch', 'malformed', 'duplicate-field') -cnotcontains
                $orchestratorResult.Values['server-profile-parse-status'] -or
            @('match', 'mismatch', 'absent', 'malformed') -cnotcontains
                $orchestratorResult.Values['server-profile-engine-version-status'] -or
            @('match', 'mismatch', 'absent', 'malformed') -cnotcontains
                $orchestratorResult.Values['server-profile-runtime-mode-status'] -or
            @('match', 'mismatch', 'absent', 'malformed') -cnotcontains
                $orchestratorResult.Values['server-profile-game-status'] -or
            @('match', 'mismatch', 'absent', 'malformed') -cnotcontains
                $orchestratorResult.Values['server-profile-protocol-status'] -or
            @('match', 'mismatch', 'absent', 'malformed') -cnotcontains
                $orchestratorResult.Values['server-profile-build-status'] -or
            @('match', 'mismatch', 'absent', 'malformed') -cnotcontains
                $orchestratorResult.Values['server-profile-endpoint-address-status'] -or
            @('match', 'mismatch', 'absent', 'malformed') -cnotcontains
                $orchestratorResult.Values['server-profile-endpoint-port-status'] -or
            @('match', 'mismatch', 'absent', 'malformed') -cnotcontains
                $orchestratorResult.Values['server-profile-map-status'] -or
            @('supported', 'stock_server_profile_not_supported', 'malformed',
                'duplicate-field') -cnotcontains
                $orchestratorResult.Values['server-profile-result']) {
            throw 'Server profile diagnostic emitted an invalid typed status.'
        }
        if ($ServerProfileId -ceq 'steam-hlds-10210-no-mode-banner-v1') {
            foreach ($readinessKey in @(
                    'server-readiness-status', 'server-readiness-endpoint-proof',
                    'server-readiness-map-proof', 'server-readiness-owned-process',
                    'server-readiness-process-identity',
                    'server-readiness-endpoint-owner',
                    'server-readiness-endpoint-address',
                    'server-readiness-endpoint-port',
                    'server-readiness-response-source', 'server-readiness-map',
                    'server-readiness-game', 'server-readiness-query-attempts',
                    'server-readiness-response-bytes')) {
                if (-not $orchestratorResult.Values.ContainsKey($readinessKey)) {
                    throw "Observed HLDS readiness omitted $readinessKey."
                }
            }
            Assert-OrchestratorValue $orchestratorResult `
                server-readiness-status ready
        }
    } else {
        Assert-OrchestratorValue $orchestratorResult relay-ready true
        Assert-OrchestratorValue $orchestratorResult server-ready true
        Assert-OrchestratorValue $orchestratorResult client-ready true
        Assert-OrchestratorValue $orchestratorResult bounded-transport-complete true
    }
    if (-not $anyServerProfileDiagnosticMode -and
        $canonicalScenario -ceq 'reconnect') {
        Assert-OrchestratorValue $orchestratorResult connection-generations 2
        Assert-OrchestratorValue $orchestratorResult generation-distinct true
        Assert-OrchestratorValue $orchestratorResult candidate-conflict evidence-pending
    } elseif (-not $anyServerProfileDiagnosticMode) {
        Assert-OrchestratorValue $orchestratorResult connection-generations 1
        Assert-OrchestratorValue $orchestratorResult generation-distinct false
        Assert-OrchestratorValue $orchestratorResult candidate-conflict not-applicable
    }
    [Int64]$orchestratorDuration = 0
    if (-not $orchestratorResult.Values.ContainsKey('duration-ms') -or
        -not [Int64]::TryParse(
            $orchestratorResult.Values['duration-ms'], [ref]$orchestratorDuration) -or
        $orchestratorDuration -lt 0 -or
        $orchestratorDuration -gt (($MaximumDurationSeconds + 90) * 1000)) {
        throw 'Stock runtime orchestrator duration is absent or outside its bound.'
    }
    if (-not $orchestratorResult.Values.ContainsKey('processes-started') -or
        [Int64]$orchestratorResult.Values['processes-started'] -lt 2) {
        throw 'Stock runtime orchestrator did not attest its owned process count.'
    }
} catch {
    $primaryError = $_
} finally {
    if ($orchestratorExitState.ExitConfirmed) {
        $orchestratorExitCode = [int]$orchestratorExitState.ExitCode
    }
    if ($wrapperCapability -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $wrapperCapability)) {
            [void]$cleanupErrors.Add('Wrapper transaction capability close failed.')
        }
        $wrapperCapability = [IntPtr]::Zero
    }
    if ($wrapperCleanupCapability -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $wrapperCleanupCapability)) {
            [void]$cleanupErrors.Add('Wrapper cleanup capability close failed.')
        }
        $wrapperCleanupCapability = [IntPtr]::Zero
    }
    if ($wrapperJob -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $wrapperJob)) {
            [void]$cleanupErrors.Add('Wrapper process Job close failed.')
        }
        $wrapperJob = [IntPtr]::Zero
    }
    if ($wrapperGuardJob -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $wrapperGuardJob)) {
            [void]$cleanupErrors.Add('Wrapper isolation guard Job close failed.')
        }
        $wrapperGuardJob = [IntPtr]::Zero
    }
    if ($isolationReleaseCapability -ne [IntPtr]::Zero) {
        if (-not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $isolationReleaseCapability)) {
            [void]$cleanupErrors.Add('Isolation release capability close failed.')
        }
        $isolationReleaseCapability = [IntPtr]::Zero
    }
    foreach ($traceCapability in @(
            $writerTracePrelaunchReadyCapability,
            $writerTraceLaunchReleaseCapability,
            $writerTraceStockStoppedCapability)) {
        if ($traceCapability -ne [IntPtr]::Zero -and
            -not [Hlclient.StockRuntimeOrchestratorCapability]::CloseHandle(
                $traceCapability)) {
            [void]$cleanupErrors.Add(
                'Writer trace lifecycle capability close failed.')
        }
    }
    $writerTracePrelaunchReadyCapability = [IntPtr]::Zero
    $writerTraceLaunchReleaseCapability = [IntPtr]::Zero
    $writerTraceStockStoppedCapability = [IntPtr]::Zero
    if (Test-Path -LiteralPath $runRoot -PathType Container) {
        try { $runDirectoryCapability = New-RunDirectoryCapability $runRoot }
        catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    }
    $cleanupAttested = $orchestratorExitState.ExitConfirmed -and
        $orchestratorExitState.JobCleanupConfirmed
    if ($cleanupAttested) {
        try {
            if ($null -ne $orchestratorExitState.WriterTraceOwner) {
                Add-StockWriterTraceTimelineEvent `
                    $orchestratorExitState.WriterTraceOwner.Timeline `
                    restoration_started | Out-Null
            }
            $after = Restore-ResearchState $guard
        }
        catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    } else {
        [void]$cleanupErrors.Add(
            'Owned process cleanup was not attested; research restoration was not started.')
    }
    try {
        if ($null -ne $orchestratorExitState.WriterTraceOwner) {
            Add-StockWriterTraceTimelineEvent `
                $orchestratorExitState.WriterTraceOwner.Timeline `
                post_inventory_started | Out-Null
        }
        if (-not $functionalPolicyMode) {
            $externalAfter = Get-ExternalSteamStateSnapshot `
                $manifestPath $research.Root $externalDriftPhase
        }
    }
    catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    finally {
        if ($null -ne $orchestratorExitState.WriterTraceOwner) {
            try {
                Add-StockWriterTraceTimelineEvent `
                    $orchestratorExitState.WriterTraceOwner.Timeline `
                    post_inventory_finished | Out-Null
            } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
        }
    }
    if ($null -ne $after) {
        try {
            Assert-RestorationDirectoryCapabilities $guard
            Close-RestorationBackupCapabilities $guard
            if ([IO.Path]::GetFileName($guard.TemporaryRoot) -notmatch
                '^hlclient-stock-runtime-restore-[0-9a-f]{32}$') {
                throw 'Restoration backup identity is invalid.'
            }
            $systemTemporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
            Assert-PathBelowRoot $guard.TemporaryRoot $systemTemporaryRoot 'restoration backup cleanup'
            Assert-NoReparsePointInExistingPath $guard.TemporaryRoot 'restoration backup cleanup'
            Remove-SafeTree $guard.TemporaryRoot $systemTemporaryRoot
        } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    } else {
        Write-Warning "Restoration backup retained for recovery at '$($guard.TemporaryRoot)'."
    }
    Close-RestorationGuardCapabilities $guard
}

$runExists = Test-Path -LiteralPath $runRoot -PathType Container
if ($runExists) {
    try { Assert-RunDirectoryCapability $runDirectoryCapability $runRoot }
    catch {
        $runExists = $false
        [void]$cleanupErrors.Add($_.Exception.Message)
    }
}
$ownedStopped = $cleanupAttested
$restorationExact = $null -ne $after -and
    $after.ManifestSha256 -ceq $before.ManifestSha256
if ($functionalSmokeMode) {
    $orchestratorValues = if ($null -ne $orchestratorResult) {
        $orchestratorResult.Values
    } else { $null }
    $functionalStaged = $null
    $functionalStagedPath = Join-Path $runRoot 'functional-smoke.staged.json'
    if ($runExists -and $null -ne $runDirectoryCapability -and
        (Test-Path -LiteralPath $functionalStagedPath -PathType Leaf)) {
        try {
            $functionalStagedRecord = Read-BoundedJsonWithRetainedBytes `
                $functionalStagedPath 131072 `
                'staged functional smoke summary' $runDirectoryCapability
            $functionalStaged = $functionalStagedRecord.Value
            $expectedFunctionalMode = if ($projectClientStockSignonMode) {
                if ($projectClientVisualMode) {
                    'project_client_live_visual_control_v1'
                } elseif ($projectClientUserCmdMode) {
                    'project_client_live_usercmd_check_v1'
                } elseif ($projectClientLiveRuntimeMode) {
                    'project_client_live_runtime_state_v1'
                } else { 'project_client_stock_signon_v1' }
            } else { 'local_research_copy_smoke_v1' }
            $expectedFunctionalPurpose = if ($projectClientStockSignonMode) {
                if ($projectClientVisualMode) {
                    'fresh_project_client_live_visual_control'
                } elseif ($projectClientUserCmdMode) {
                    'fresh_project_client_usercmd_server_motion'
                } elseif ($projectClientLiveRuntimeMode) {
                    'fresh_project_client_live_runtime_state'
                } else { 'fresh_project_client_stock_signon' }
            } else { 'functional_smoke' }
            $expectedClientSteamArgument = if ($projectClientStockSignonMode) {
                'not_applicable'
            } else { 'present' }
            $expectedClientWorkingDirectoryRole =
                if ($projectClientStockSignonMode) {
                    'repository_build_directory'
                } else { 'research_root' }
            $expectedActualClientArgvProfile =
                if ($projectClientVisualMode) {
                    "renderer=opengl;auth-provider=steam;stop-after=live-visual-control;live-input=$ProjectClientLiveInput;basedir=research-root;game=valve$(if ($ProjectClientPrediction -ceq 'reference') {';prediction=reference'})"
                } elseif ($projectClientUserCmdMode) {
                    'renderer=null;auth-provider=steam;stop-after=live-usercmd-check;resource-advertisement=empty'
                } elseif ($projectClientLiveRuntimeMode) {
                    'renderer=null;auth-provider=steam;stop-after=live-runtime-state;resource-advertisement=empty'
                } elseif ($projectClientStockSignonMode) {
                    'renderer=null;auth-provider=steam;stop-after=delta-schemas'
                } else { 'stock-steam-windowed-connect' }
            if ([string]$functionalStaged.schema -cne
                    'hlclient.local-research-copy-smoke.v2' -or
                [string]$functionalStaged.mode -cne $expectedFunctionalMode -or
                [string]$functionalStaged.purpose -cne
                    $expectedFunctionalPurpose -or
                [bool]$functionalStaged.evidence_eligible -ne
                    $projectClientStockSignonMode -or
                [string]$functionalStaged.route -cne 'direct_loopback' -or
                [string]$functionalStaged.game -cne 'valve' -or
                [string]$functionalStaged.map -cne $Map -or
                [Int64]$functionalStaged.server_port -ne $ServerPort -or
                [string]$functionalStaged.client_steam_argument -cne
                    $expectedClientSteamArgument -or
                [string]$functionalStaged.client_working_directory_role -cne
                    $expectedClientWorkingDirectoryRole -or
                [string]$functionalStaged.actual_client_argv_profile -cne
                    $expectedActualClientArgvProfile -or
                [string]$functionalStaged.server_working_directory_role -cne
                    'research_root' -or
                [string]$functionalStaged.server_logging -cne
                    'enabled_before_map' -or
                [string]$functionalStaged.restoration_status -cne
                    'wrapper_pending' -or
                [string]$functionalStaged.publication_status -cne
                    'staged_after_process_cleanup') {
                throw 'Staged functional smoke summary contract is invalid.'
            }
            if ($projectClientStockSignonMode) {
                foreach ($projectField in @(
                        'image_identity_verified', 'resume_result',
                        'application_entry_observed', 'arguments_accepted',
                        'provider_begin_observed', 'steam_api_init_attempted',
                        'steam_api_initialized', 'fresh_material_acquired',
                        'connect_sent', 'connection_accepted',
                        'serverinfo_received', 'schema_registry_received',
                        'authentication_status', 'client_exit_code_hex',
                        'child_wait_result', 'child_wait_native_error',
                        'serverinfo_protocol', 'serverinfo_max_clients',
                        'serverinfo_game', 'serverinfo_map', 'schema_count',
                        'schema_field_count', 'resource_continuation_sent',
                        'spawn_request_transmitted',
                        'spawn_request_acknowledged',
                        'signon_reply_transmitted',
                        'signon_reply_acknowledged',
                        'live_service_payloads_received',
                        'client_world_state_published', 'usercmd_transmitted',
                        'usercmd_movement_verified', 'live_visual_verified',
                        'jump_duck_result', 'speed_result',
                        'prediction_result', 'jump_observed',
                        'descent_observed', 'duck_observed',
                        'release_response_observed',
                        'jump_new_submitted', 'duck_new_submitted',
                        'usercmd_generated',
                        'usercmd_new', 'usercmd_backup', 'usercmd_packets',
                        'usercmd_server_samples',
                        'rx_progress_post_input',
                        'baseline_entity_count', 'service_payload_count',
                        'applied_runtime_record_count', 'world_entity_count',
                        'publication_revision', 'canonical_state_hash',
                        'stable_runtime_interval_ms', 'protocol_progress',
                        'transition_failure')) {
                    if ($functionalStaged.PSObject.Properties.Name -cnotcontains
                            $projectField) {
                        throw "Staged project-client sign-on summary omitted $projectField."
                    }
                }
                if (@('not_reached', 'failed', 'pending_or_unknown') -cnotcontains
                        [string]$functionalStaged.authentication_status) {
                    throw 'Staged project-client authentication status is invalid.'
                }
                if ($projectClientVisualMode -and
                    $ProjectClientLiveInput -ceq
                        'scripted-jump-duck-check') {
                    Assert-GJumpDuckStagedContract `
                        -Summary $functionalStaged
                } elseif ($projectClientVisualMode -and
                    $ProjectClientLiveInput -ceq 'scripted-speed-check' -and
                    $ProjectClientPrediction -cne 'reference') {
                    Assert-H1SpeedStagedContract `
                        -Summary $functionalStaged
                }
                $progressFields = @(
                    'steam_initialization', 'fresh_material',
                    'connect_transmission', 'accept', 'serverinfo',
                    'schema_registry', 'movevars', 'user_info',
                    'sendres_queued', 'sendres_transmitted',
                    'sendres_acknowledged', 'resource_transition',
                    'resource_list', 'resource_response_queued',
                    'resource_response_transmitted',
                    'resource_response_acknowledged', 'resource_response',
                    'spawn_queued', 'spawn_transmitted',
                    'spawn_acknowledged', 'baselines',
                    'runtime_publication', 'operational_interval')
                foreach ($progressField in $progressFields) {
                    if ($functionalStaged.protocol_progress.PSObject.Properties.Name `
                            -cnotcontains $progressField -or
                        @('not_reached', 'pending', 'succeeded', 'failed',
                            'unknown') -cnotcontains
                            [string]$functionalStaged.protocol_progress.$progressField) {
                        throw "Staged project-client progress field is invalid: $progressField."
                    }
                }
                foreach ($transitionField in @(
                        'present', 'stage', 'profile', 'expected_opcode',
                        'actual_opcode', 'cursor_byte_value', 'cursor',
                        'boundary', 'payload_ordinal',
                        'payload_ordinal_scope', 'direction',
                        'source_sequence', 'source_acknowledgement',
                        'source_reliable', 'reassembled', 'encoding',
                        'wire_size', 'decoded_size', 'pending_suffix_start',
                        'sendres_queued', 'sendres_transmitted',
                        'sendres_acknowledged',
                        'request_reliable_generation',
                        'request_transmit_sequence',
                        'request_acknowledgement_sequence', 'last_category',
                        'last_scope', 'last_cursor', 'parser_error',
                        'primary_error')) {
                    if ($functionalStaged.transition_failure.PSObject.Properties.Name `
                            -cnotcontains $transitionField) {
                        throw "Staged transition diagnostic omitted $transitionField."
                    }
                }
            }
        } catch {
            $functionalStaged = $null
            [void]$cleanupErrors.Add($_.Exception.Message)
        }
    } else {
        [void]$cleanupErrors.Add(
            'Staged functional smoke summary was not retained before restoration.')
    }
    $serverReady = $null -ne $orchestratorValues -and
        $orchestratorValues.ContainsKey('server-ready') -and
        $orchestratorValues['server-ready'] -ceq 'true'
    $clientReady = $null -ne $orchestratorValues -and
        $orchestratorValues.ContainsKey('client-ready') -and
        $orchestratorValues['client-ready'] -ceq 'true'
    $functionalSuccess = $null -eq $primaryError -and
        $cleanupErrors.Count -eq 0 -and $runExists -and $ownedStopped -and
        $restorationExact -and $serverReady -and $clientReady -and
        $orchestratorExitCode -eq 0
    $functionalResult = if ($functionalSuccess) {
        if ($projectClientStockSignonMode) {
            if ($projectClientVisualMode) {
                if ($ProjectClientLiveInput -ceq
                        'scripted-jump-duck-check') {
                    'fresh_project_client_jump_duck_server_verified'
                } elseif ($ProjectClientPrediction -ceq 'reference') {
                    'live_local_prediction_and_reconciliation_verified'
                } elseif ($ProjectClientLiveInput -ceq
                        'scripted-speed-check') {
                    'live_normal_speed_and_shift_walk_verified'
                } else {
                    'fresh_project_client_live_visual_control_integrated'
                }
            } elseif ($projectClientUserCmdMode) {
                'fresh_project_client_usercmd_server_motion_verified'
            } elseif ($projectClientLiveRuntimeMode) {
                'fresh_project_client_live_runtime_state_verified'
            } else { 'fresh_project_client_stock_signon_verified' }
        } else { 'local_client_server_smoke_passed' }
    } elseif (-not $ownedStopped -or -not $restorationExact -or
        $cleanupErrors.Count -ne 0) {
        'local_smoke_integrity_failed'
    } elseif ($serverReady) {
        'local_server_ready_client_blocked'
    } else {
        'local_server_startup_failed'
    }
    if ($runExists -and $null -ne $runDirectoryCapability) {
        $orchestratorSha256 = Get-FileSha256 $orchestratorPath
        $functionalWrapper = [ordered]@{
            schema = 'hlclient.local-research-copy-smoke-wrapper.v2'
            mode = $(if ($projectClientStockSignonMode) {
                    if ($projectClientVisualMode) {
                        'project_client_live_visual_control_v1'
                    } elseif ($projectClientUserCmdMode) {
                        'project_client_live_usercmd_check_v1'
                    } elseif ($projectClientLiveRuntimeMode) {
                        'project_client_live_runtime_state_v1'
                    } else { 'project_client_stock_signon_v1' }
                } else { 'local_research_copy_smoke_v1' })
            purpose = $(if ($projectClientStockSignonMode) {
                    if ($projectClientVisualMode) {
                        'fresh_project_client_live_visual_control'
                    } elseif ($projectClientUserCmdMode) {
                        'fresh_project_client_usercmd_server_motion'
                    } elseif ($projectClientLiveRuntimeMode) {
                        'fresh_project_client_live_runtime_state'
                    } else { 'fresh_project_client_stock_signon' }
                } else { 'functional_smoke' })
            evidence_eligible = $projectClientStockSignonMode
            run_id = $runId
            route = 'direct_loopback'
            launch = [ordered]@{
                configuration = 'Release'
                orchestrator_path = $orchestratorPath
                orchestrator_sha256 = $orchestratorSha256
                research_root = $research.Root
                server_path = $research.Server
                client_path = $research.Client
                game = $Game
                map = $Map
                server_port = $ServerPort
                client_steam_argument = $(if ($projectClientStockSignonMode) {
                        'not_applicable'
                    } else { 'present' })
                authentication_provider = $(if ($projectClientStockSignonMode) {
                        'steam_legacy_initiate_game_connection'
                    } else { 'stock_client' })
                client_working_directory = $(if ($projectClientStockSignonMode) {
                        Split-Path -Parent $research.Client
                    } else { $research.Root })
                connect_argument = "127.0.0.1:$ServerPort"
            }
            startup = [ordered]@{
                status = $orchestratorExitState.StartupStatus
                exit_code = $orchestratorExitCode
                stdout_bytes = $orchestratorExitState.StartupStdoutBytes
                stderr_bytes = $orchestratorExitState.StartupStderrBytes
            }
            native_summary_retained_across_restoration =
                ($null -ne $functionalStaged)
            native_summary = $functionalStaged
            external_steam_state = $(if ($projectClientStockSignonMode) {
                    'current_user_session_used'
                } else { 'not_assessed' })
            external_steam_state_policy = $(if ($projectClientStockSignonMode) {
                    'provider_runtime_only_no_credentials_or_material_retained'
                } else { 'functional_observation_only' })
            owned_process_cleanup = $(if ($ownedStopped) { 'exact' } else { 'incomplete' })
            restoration_status = $(if ($restorationExact) { 'exact' } else { 'not_exact' })
            result = $functionalResult
        }
        try {
            Write-AtomicJsonNoOverwrite `
                (Join-Path $runRoot 'functional-smoke-wrapper.json') `
                $functionalWrapper 'functional smoke wrapper result' `
                $runDirectoryCapability
        } catch {
            $functionalSuccess = $false
            $functionalResult = 'local_smoke_integrity_failed'
            [void]$cleanupErrors.Add($_.Exception.Message)
        }
    }
    Write-Output ("[research-copy-smoke] mode={0}" -f $(if (
            $projectClientStockSignonMode) { if (
                $projectClientVisualMode) {
                    'project_client_live_visual_control_v1'
                } elseif ($projectClientUserCmdMode) {
                    'project_client_live_usercmd_check_v1'
                } elseif ($projectClientLiveRuntimeMode) {
                    'project_client_live_runtime_state_v1'
                } else { 'project_client_stock_signon_v1' } }
            else { 'local_research_copy_smoke_v1' }))
    Write-Output ("[research-copy-smoke] purpose={0}" -f $(if (
            $projectClientStockSignonMode) { if (
                $projectClientVisualMode) {
                    'fresh_project_client_live_visual_control'
                } elseif ($projectClientUserCmdMode) {
                    'fresh_project_client_usercmd_server_motion'
                } elseif ($projectClientLiveRuntimeMode) {
                    'fresh_project_client_live_runtime_state'
                } else { 'fresh_project_client_stock_signon' } }
            else { 'functional_smoke' }))
    Write-Output ("[research-copy-smoke] evidence_eligible={0}" -f
        $projectClientStockSignonMode.ToString().ToLowerInvariant())
    Write-Output ("[research-copy-smoke] external_steam_state={0}" -f $(if (
            $projectClientStockSignonMode) { 'current_user_session_used' }
            else { 'not_assessed' }))
    Write-Output ("[research-copy-smoke] run_id={0}" -f $runId)
    Write-Output ("[research-copy-smoke] run_root={0}" -f $runRoot)
    Write-Output ("[research-copy-smoke] server_ready={0}" -f
        $serverReady.ToString().ToLowerInvariant())
    Write-Output ("[research-copy-smoke] client_map_entry={0}" -f
        $clientReady.ToString().ToLowerInvariant())
    foreach ($summaryKey in @(
            'client-process-created', 'client-image-identity',
            'client-resume-result', 'client-initialized',
            'connect-requested', 'connection-status',
            'client-map-entry-status', 'map-entry-source',
            'last-confirmed-stage', 'stable-duration-ms',
            'application-entry-observed', 'arguments-accepted',
            'provider-begin-observed', 'steam-api-init-attempted',
            'steam-api-initialized', 'fresh-material-acquired', 'connect-sent',
            'connection-accepted', 'serverinfo-received',
            'schema-registry-received', 'authentication-status',
            'resource-continuation-sent', 'spawn-request-transmitted',
            'spawn-request-acknowledged', 'signon-reply-transmitted',
            'signon-reply-acknowledged', 'live-service-payloads-received',
            'client-world-state-published', 'usercmd-transmitted',
            'usercmd-movement-verified', 'live-visual-verified',
            'jump-duck-result', 'speed-result', 'prediction-result', 'jump-observed', 'descent-observed',
            'duck-observed', 'release-response-observed',
            'jump-new-submitted', 'duck-new-submitted',
            'usercmd-generated', 'usercmd-new',
            'usercmd-backup', 'usercmd-packets', 'usercmd-server-samples',
            'serverinfo-protocol', 'serverinfo-max-clients',
            'serverinfo-game', 'serverinfo-map', 'schema-count',
            'schema-field-count', 'baseline-entity-count',
            'service-payload-count', 'applied-runtime-record-count',
            'world-entity-count', 'publication-revision',
            'canonical-state-hash', 'stable-runtime-interval-ms',
            'client-exit-code', 'client-exit-code-hex',
            'client-wait-result', 'client-wait-native-error',
            'server-exit-code',
            'diagnostic-publication')) {
        $summaryValue = if ($null -ne $orchestratorValues -and
            $orchestratorValues.ContainsKey($summaryKey)) {
            $orchestratorValues[$summaryKey]
        } else { 'unknown' }
        Write-Output ("[research-copy-smoke] {0}={1}" -f
            $summaryKey, $summaryValue)
    }
    Write-Output ("[research-copy-smoke] diagnostic_root={0}" -f $runRoot)
    Write-Output ("[research-copy-smoke] owned_process_cleanup={0}" -f
        $(if ($ownedStopped) { 'exact' } else { 'incomplete' }))
    Write-Output ("[research-copy-smoke] restoration_status={0}" -f
        $(if ($restorationExact) { 'exact' } else { 'not_exact' }))
    Write-Output ("[research-copy-smoke] result={0}" -f $functionalResult)
    if ($null -ne $runDirectoryCapability) {
        $runDirectoryCapability.Dispose()
        $runDirectoryCapability = $null
    }
    if ($functionalSuccess) { return }
    if ($null -ne $primaryError) { throw $primaryError }
    throw "Functional smoke failed: $functionalResult."
}
if ($functionalRuntimeCaptureMode) {
    $orchestratorValues = if ($null -ne $orchestratorResult) {
        $orchestratorResult.Values
    } else { $null }
    $relayReady = $null -ne $orchestratorValues -and
        $orchestratorValues.ContainsKey('relay-ready') -and
        $orchestratorValues['relay-ready'] -ceq 'true'
    $serverReady = $null -ne $orchestratorValues -and
        $orchestratorValues.ContainsKey('server-ready') -and
        $orchestratorValues['server-ready'] -ceq 'true'
    $clientReady = $null -ne $orchestratorValues -and
        $orchestratorValues.ContainsKey('client-ready') -and
        $orchestratorValues['client-ready'] -ceq 'true'
    $transportComplete = $null -ne $orchestratorValues -and
        $orchestratorValues.ContainsKey('bounded-transport-complete') -and
        $orchestratorValues['bounded-transport-complete'] -ceq 'true'
    $functionalCaptureSuccess = $null -eq $primaryError -and
        $cleanupErrors.Count -eq 0 -and $runExists -and $ownedStopped -and
        $restorationExact -and $relayReady -and $serverReady -and
        $clientReady -and $transportComplete -and
        $orchestratorExitCode -eq 0
    $functionalCaptureResult = if ($functionalCaptureSuccess) {
        'functional_runtime_capture_complete'
    } elseif (-not $ownedStopped -or -not $restorationExact -or
        $cleanupErrors.Count -ne 0) {
        'functional_runtime_capture_integrity_failed'
    } elseif ($runExists) {
        'functional_runtime_capture_incomplete'
    } else {
        'functional_runtime_capture_not_created'
    }
    if ($runExists -and $null -ne $runDirectoryCapability) {
        try {
            $publication = Publish-FunctionalRuntimeCaptureArtifacts `
                $runRoot $runId $runDirectoryCapability `
                $functionalCaptureSuccess $ownedStopped $restorationExact `
                $before.ManifestSha256 `
                $(if ($null -ne $after) { $after.ManifestSha256 } else { $null }) `
                $relayReady $serverReady $clientReady $transportComplete `
                $Game $Map $MaximumDurationSeconds $ServerProfileId `
                $orchestratorValues
            if ($functionalCaptureSuccess -and
                -not [bool]$publication.ManifestPublished) {
                throw 'Functional publication did not reach its commit point.'
            }
        } catch {
            $functionalCaptureSuccess = $false
            $functionalCaptureResult =
                'functional_runtime_capture_integrity_failed'
            [void]$cleanupErrors.Add($_.Exception.Message)
        }
    }
    Write-Output '[functional-runtime-capture] purpose=functional_runtime_capture'
    Write-Output '[functional-runtime-capture] campaign_evidence_eligible=false'
    Write-Output '[functional-runtime-capture] external_steam_state=not_assessed'
    Write-Output '[functional-runtime-capture] route=stock_client_loopback_relay_stock_hlds'
    Write-Output ("[functional-runtime-capture] run_id={0}" -f $runId)
    Write-Output ("[functional-runtime-capture] run_root={0}" -f $runRoot)
    Write-Output ("[functional-runtime-capture] relay_ready={0}" -f
        $relayReady.ToString().ToLowerInvariant())
    Write-Output ("[functional-runtime-capture] server_ready={0}" -f
        $serverReady.ToString().ToLowerInvariant())
    Write-Output ("[functional-runtime-capture] client_map_entry={0}" -f
        $clientReady.ToString().ToLowerInvariant())
    Write-Output ("[functional-runtime-capture] transport_complete={0}" -f
        $transportComplete.ToString().ToLowerInvariant())
    Write-Output ("[functional-runtime-capture] owned_process_cleanup={0}" -f
        $(if ($ownedStopped) { 'exact' } else { 'incomplete' }))
    Write-Output ("[functional-runtime-capture] restoration_status={0}" -f
        $(if ($restorationExact) { 'exact' } else { 'not_exact' }))
    Write-Output ("[functional-runtime-capture] result={0}" -f
        $functionalCaptureResult)
    if ($null -ne $runDirectoryCapability) {
        $runDirectoryCapability.Dispose()
        $runDirectoryCapability = $null
    }
    if ($functionalCaptureSuccess) { return }
    if ($null -ne $primaryError) { throw $primaryError }
    throw "Functional runtime capture failed: $functionalCaptureResult."
}
$externalDifference = if ($null -ne $externalAfter) {
    Compare-StockExternalStateSnapshot `
        $externalBefore $externalAfter $externalDriftPhase `
        -SteamRewritePolicyId $stockSteamRewritePolicyId
} else { $null }
$externalExact = $null -ne $externalDifference -and
    [string]$externalDifference.result -ceq 'none'
$writerTraceTerminalReceipt = $null
if ($EnableWriterTraceHandoff -and
    $null -ne $orchestratorExitState.WriterTraceOwner) {
    $writerSnapshotResult = if ($null -eq $externalDifference) {
        'incomplete'
    } elseif ([string]$externalDifference.result -ceq 'none') {
        'unchanged'
    } else { 'changed' }
    try {
        $writerTraceTerminalReceipt = Write-StockWriterTraceTerminalReceipt `
            $orchestratorExitState.WriterTraceOwner $writerSnapshotResult
    } catch {
        [void]$cleanupErrors.Add(
            'writer_trace_terminal_receipt_publication_failed:' +
            $_.Exception.Message)
    }
}
$version = $null
$isolation = $null
$restoration = $null
$versionBytes = $null
$isolationBytes = $null
$restorationBytes = $null

if ($runExists -and $restorationExact -and $null -ne $externalAfter -and
    $null -ne $externalDifference) {
    try {
        $restoration = Write-StagedRestorationAttestation $runRoot $before `
            $after $externalBefore $externalAfter $externalDifference `
            $orchestratorExitCode `
            $ownedStopped $runDirectoryCapability
        if ($anyServerProfileDiagnosticMode) {
            Write-AtomicJsonNoOverwrite `
                (Join-Path $runRoot 'external-drift-private.json') `
                $externalDifference 'private external drift attribution' `
                $runDirectoryCapability
        }
    } catch {
        [void]$cleanupErrors.Add($_.Exception.Message)
    }
}

if ($null -ne $externalDifference) {
    Write-StockExternalDriftPublicOutput $externalDifference
}

if ($anyServerProfileDiagnosticMode) {
    if ($null -ne $primaryError -or $cleanupErrors.Count -ne 0 -or
        -not $runExists -or -not $ownedStopped -or -not $restorationExact -or
        -not $externalExact -or $null -eq $restoration -or
        $null -eq $orchestratorResult) {
        if ($null -ne $runDirectoryCapability) {
            $runDirectoryCapability.Dispose()
            $runDirectoryCapability = $null
        }
        $category = if ($null -ne $primaryError -and
            $null -ne $orchestratorResult -and
            $orchestratorResult.Values.ContainsKey('failure-category')) {
            $orchestratorResult.Values['failure-category']
        } elseif (-not $ownedStopped) {
            'owned_process_cleanup_not_exact'
        } elseif (-not $restorationExact) {
            'research_restoration_failed'
        } elseif (-not $externalExact) {
            'external_steam_state_changed'
        } elseif ($cleanupErrors.Count -ne 0) {
            'transaction_cleanup_failed'
        } elseif (-not $runExists) {
            'diagnostic_run_not_created'
        } else { 'server_profile_diagnostic_transaction_failed' }
        if ($null -ne $orchestratorResult -and
            $orchestratorResult.Values.ContainsKey(
                'server-profile-parse-status')) {
            $failureValues = [Collections.Generic.Dictionary[string, string]]::new(
                $orchestratorResult.Values, [StringComparer]::Ordinal)
            $failureValues['server-profile-result'] = $category
            Write-StockServerProfileDiagnosticPublicOutput $failureValues
        } else {
            Write-Output "[stock-server-profile] result=$category"
        }
        if ($null -ne $primaryError) { throw $primaryError }
        throw 'Server profile diagnostic cleanup/restoration transaction was not exact.'
    }
    Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
    $prepublicationItems = @(Get-ChildItem -LiteralPath $runRoot -Force)
    $prepublicationNames = @($prepublicationItems | Select-Object -ExpandProperty Name)
    $expectedPrepublicationCount = if ($privateServerProfileDiagnosticMode) {
        6
    } else { 3 }
    if ($prepublicationItems.Count -ne $expectedPrepublicationCount -or
        $prepublicationNames -cnotcontains
            'restoration-attestation.staged.json' -or
        $prepublicationNames -cnotcontains
            'server-profile-diagnostic.staged.json' -or
        $prepublicationNames -cnotcontains 'external-drift-private.json' -or
        ($privateServerProfileDiagnosticMode -and
         ($prepublicationNames -cnotcontains 'server-stdout.bin' -or
          $prepublicationNames -cnotcontains 'server-stderr.bin' -or
          $prepublicationNames -cnotcontains
              'server-banner-shape-private.json')) -or
        @($prepublicationItems | Where-Object PSIsContainer).Count -ne 0) {
        throw 'Server profile diagnostic root contains an invalid bounded publication set.'
    }
    $stagedDiagnostic = Read-BoundedJsonWithRetainedBytes `
        (Join-Path $runRoot 'server-profile-diagnostic.staged.json') `
        65536 'staged server profile diagnostic' $runDirectoryCapability
    $staged = $stagedDiagnostic.Value
    $fieldMappings = [ordered]@{
        parse_status = 'server-profile-parse-status'
        mismatch_field = 'server-profile-mismatch-field'
        engine_version_status = 'server-profile-engine-version-status'
        runtime_mode_status = 'server-profile-runtime-mode-status'
        runtime_mode_category = 'server-profile-runtime-mode-category'
        game_status = 'server-profile-game-status'
        protocol_status = 'server-profile-protocol-status'
        build_status = 'server-profile-build-status'
        endpoint_address_status = 'server-profile-endpoint-address-status'
        endpoint_address_category = 'server-profile-endpoint-address-category'
        endpoint_port_status = 'server-profile-endpoint-port-status'
        map_status = 'server-profile-map-status'
    }
    if ([string]$staged.schema -cne
            'hlclient.stock-runtime-server-profile-diagnostic-staged.v2' -or
        [string]$staged.profile_id -cne $ServerProfileId) {
        throw 'Staged server profile diagnostic schema is invalid.'
    }
    $expectedReadinessStatus = if ($ServerProfileId -ceq
        'steam-hlds-10210-no-mode-banner-v1') {
        $orchestratorResult.Values['server-readiness-status']
    } else { 'not-applicable' }
    $expectedEndpointProof = if ($ServerProfileId -ceq
        'steam-hlds-10210-no-mode-banner-v1') {
        $orchestratorResult.Values['server-readiness-endpoint-proof']
    } else { 'absent' }
    $expectedMapProof = if ($ServerProfileId -ceq
        'steam-hlds-10210-no-mode-banner-v1') {
        $orchestratorResult.Values['server-readiness-map-proof']
    } else { 'absent' }
    if ([string]$staged.readiness_status -cne $expectedReadinessStatus -or
        [string]$staged.endpoint_proof_source -cne $expectedEndpointProof -or
        [string]$staged.map_proof_source -cne $expectedMapProof) {
        throw 'Staged server readiness proof disagrees with the bounded process result.'
    }
    foreach ($property in $fieldMappings.Keys) {
        if ([string]$staged.$property -cne
            $orchestratorResult.Values[$fieldMappings[$property]]) {
            throw "Staged server profile diagnostic field $property disagrees with the bounded process result."
        }
    }
    foreach ($mapping in @(
            @('duplicate_field_count', 'server-profile-duplicate-fields'),
            @('observed_byte_count', 'server-profile-observed-byte-count'),
            @('observed_line_count', 'server-profile-observed-line-count'))) {
        if ([Int64]$staged.($mapping[0]) -ne
            [Int64]$orchestratorResult.Values[$mapping[1]]) {
            throw "Staged server profile diagnostic count $($mapping[0]) disagrees with the bounded process result."
        }
    }
    if ([bool]$staged.process_log_truncated -ne
        ($orchestratorResult.Values['server-profile-process-log-truncated'] -ceq
            'true')) {
        throw 'Staged server profile truncation state disagrees with the bounded process result.'
    }
    foreach ($mapping in @(
            @('observed_engine_version',
                'server-profile-observed-engine-version'),
            @('observed_protocol', 'server-profile-observed-protocol'),
            @('observed_build', 'server-profile-observed-build'))) {
        $propertyPresent = $null -ne $staged.PSObject.Properties[$mapping[0]]
        $valuePresent = $orchestratorResult.Values.ContainsKey($mapping[1])
        if ($propertyPresent -ne $valuePresent -or
            ($propertyPresent -and
             [string]$staged.($mapping[0]) -cne
                $orchestratorResult.Values[$mapping[1]])) {
            throw "Staged server profile diagnostic public value $($mapping[0]) disagrees with the bounded process result."
        }
    }
    foreach ($countKey in @(
            'server-profile-duplicate-fields',
            'server-profile-observed-byte-count',
            'server-profile-observed-line-count')) {
        [Int64]$count = 0
        if (-not [Int64]::TryParse(
                $orchestratorResult.Values[$countKey], [ref]$count) -or
            $count -lt 0 -or $count -gt 1048576) {
            throw "Server profile diagnostic count $countKey is invalid."
        }
    }
    if ($orchestratorResult.Values.ContainsKey(
            'server-profile-observed-engine-version') -and
        $orchestratorResult.Values['server-profile-observed-engine-version'] -cnotmatch
            '^[0-9]{1,5}(?:\.[0-9]{1,5}){3}$') {
        throw 'Observed server engine version is malformed.'
    }
    foreach ($numericKey in @(
            'server-profile-observed-protocol',
            'server-profile-observed-build')) {
        if ($orchestratorResult.Values.ContainsKey($numericKey)) {
            [Int64]$number = 0
            if (-not [Int64]::TryParse(
                    $orchestratorResult.Values[$numericKey], [ref]$number) -or
                $number -lt 0 -or $number -gt [UInt32]::MaxValue) {
                throw "Observed server value $numericKey is malformed."
            }
        }
    }
    $diagnosticManifest = [ordered]@{
        schema = if ($privateServerProfileDiagnosticMode) {
            'hlclient.stock-runtime-server-profile-private-diagnostic.v2'
        } else { 'hlclient.stock-runtime-server-profile-diagnostic.v2' }
        run_id = $runId
        role = if ($privateServerProfileDiagnosticMode) {
            'server-profile-private-diagnostic'
        } else { 'server-profile-diagnostic' }
        evidence_eligible = $false
        profile_id = $ServerProfileId
        parse_status = $orchestratorResult.Values['server-profile-parse-status']
        mismatch_field = $orchestratorResult.Values['server-profile-mismatch-field']
        engine_version_status = $orchestratorResult.Values['server-profile-engine-version-status']
        runtime_mode_status = $orchestratorResult.Values['server-profile-runtime-mode-status']
        runtime_mode_category = $orchestratorResult.Values['server-profile-runtime-mode-category']
        game_status = $orchestratorResult.Values['server-profile-game-status']
        protocol_status = $orchestratorResult.Values['server-profile-protocol-status']
        build_status = $orchestratorResult.Values['server-profile-build-status']
        endpoint_address_status = $orchestratorResult.Values['server-profile-endpoint-address-status']
        endpoint_address_category = $orchestratorResult.Values['server-profile-endpoint-address-category']
        endpoint_port_status = $orchestratorResult.Values['server-profile-endpoint-port-status']
        map_status = $orchestratorResult.Values['server-profile-map-status']
        duplicate_field_count = [Int64]$orchestratorResult.Values['server-profile-duplicate-fields']
        process_log_truncated =
            $orchestratorResult.Values['server-profile-process-log-truncated'] -ceq 'true'
        observed_byte_count = [Int64]$orchestratorResult.Values['server-profile-observed-byte-count']
        observed_line_count = [Int64]$orchestratorResult.Values['server-profile-observed-line-count']
        readiness_status = $expectedReadinessStatus
        endpoint_proof_source = $expectedEndpointProof
        map_proof_source = $expectedMapProof
        stock_client_launched = $false
        udp_corpus_created = $false
        restoration_status = 'exact'
        external_file_drift = 'none'
        process_cleanup = 'exact'
        result = $orchestratorResult.Values['server-profile-result']
        drift_phase = $externalDifference.phase
        drift_changed_scopes = $externalDifference.changed_scopes
        drift_content_changes = $externalDifference.content_changes
        drift_digest_changes = $externalDifference.digest_changes
        drift_size_changes = $externalDifference.size_changes
        drift_metadata_only_changes = $externalDifference.metadata_only_changes
        drift_identity_replacements = $externalDifference.identity_replacements
        drift_timestamp_changes = $externalDifference.timestamp_changes
        drift_created = $externalDifference.created
        drift_removed = $externalDifference.removed
        drift_unreadable = $externalDifference.unreadable
        critical_external_drift = $externalDifference.critical_external_drift
        raw_external_state = $externalDifference.raw_external_state
        protected_projection = $externalDifference.protected_projection
        steam_rewrite_policy_id = $externalDifference.policy_id
        policy_decision = $externalDifference.policy_decision
        steam_user_config_rewrite = $externalDifference.steam_user_config_rewrite
        steam_user_config_projection = $externalDifference.steam_user_config_projection
        steam_user_config_volatile_classes =
            $externalDifference.steam_user_config_volatile_classes
        steam_user_config_unknown_changes =
            $externalDifference.steam_user_config_unknown_changes
        steam_user_config_fatal_changes =
            $externalDifference.steam_user_config_fatal_changes
        steam_user_config_non_monotonic_changes =
            $externalDifference.steam_user_config_non_monotonic_changes
    }
    if ($orchestratorResult.Values.ContainsKey(
            'server-profile-observed-engine-version')) {
        $diagnosticManifest['observed_engine_version'] =
            $orchestratorResult.Values['server-profile-observed-engine-version']
    }
    if ($orchestratorResult.Values.ContainsKey(
            'server-profile-observed-protocol')) {
        $diagnosticManifest['observed_protocol'] =
            [Int64]$orchestratorResult.Values['server-profile-observed-protocol']
    }
    if ($orchestratorResult.Values.ContainsKey('server-profile-observed-build')) {
        $diagnosticManifest['observed_build'] =
            [Int64]$orchestratorResult.Values['server-profile-observed-build']
    }
    $privateShape = $null
    if ($privateServerProfileDiagnosticMode) {
        $stdoutPath = Join-Path $runRoot 'server-stdout.bin'
        $stderrPath = Join-Path $runRoot 'server-stderr.bin'
        $stdoutItem = Get-Item -LiteralPath $stdoutPath -Force
        $stderrItem = Get-Item -LiteralPath $stderrPath -Force
        if ($stdoutItem.PSIsContainer -or $stderrItem.PSIsContainer -or
            $stdoutItem.Length -gt 65536 -or $stderrItem.Length -gt 65536) {
            throw 'Private server profile streams exceed their exact bounds.'
        }
        $privateShape = Read-BoundedJson `
            (Join-Path $runRoot 'server-banner-shape-private.json') `
            65536 'private server banner shape'
        if ([string]$privateShape.schema -cne
                'hlclient.stock-runtime-server-banner-shape-private.v1' -or
            [Int64]$privateShape.stdout.byte_count -ne $stdoutItem.Length -or
            [Int64]$privateShape.stderr.byte_count -ne $stderrItem.Length -or
            @('stdout', 'stderr', 'split', 'absent') -cnotcontains
                [string]$privateShape.stream_attribution) {
            throw 'Private server profile shape metadata is invalid.'
        }
        $diagnosticManifest['private_raw_streams'] = $true
        $diagnosticManifest['stdout_byte_count'] = [Int64]$stdoutItem.Length
        $diagnosticManifest['stderr_byte_count'] = [Int64]$stderrItem.Length
        $diagnosticManifest['stream_attribution'] =
            [string]$privateShape.stream_attribution
    }
    $diagnosticManifestName = if ($privateServerProfileDiagnosticMode) {
        'private-diagnostic-manifest.json'
    } else { 'server-profile-diagnostic-manifest.json' }
    Write-AtomicJsonNoOverwrite `
        (Join-Path $runRoot $diagnosticManifestName) `
        ([pscustomobject]$diagnosticManifest) `
        'server profile diagnostic manifest' $runDirectoryCapability
    Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
    $runDirectoryCapability.Dispose()
    $runDirectoryCapability = $null
    Write-Output "[stock-runtime-capture] run-id=$runId"
    if ($privateServerProfileDiagnosticMode) {
        $stdoutShape = $privateShape.stdout
        $stderrShape = $privateShape.stderr
        Write-Output "[stock-server-private] run-id=$runId"
        Write-Output "[stock-server-private] stdout-bytes=$($stdoutShape.byte_count)"
        Write-Output "[stock-server-private] stderr-bytes=$($stderrShape.byte_count)"
        Write-Output "[stock-server-private] stdout-complete-lines=$($stdoutShape.complete_line_count)"
        Write-Output "[stock-server-private] stderr-complete-lines=$($stderrShape.complete_line_count)"
        Write-Output ("[stock-server-private] trailing-partial-lines={0}" -f
            ([Int64]$stdoutShape.trailing_partial_line_count +
             [Int64]$stderrShape.trailing_partial_line_count))
        Write-Output ("[stock-server-private] nul-bytes={0}" -f
            ([Int64]$stdoutShape.nul_byte_count + [Int64]$stderrShape.nul_byte_count))
        Write-Output ("[stock-server-private] escape-bytes={0}" -f
            ([Int64]$stdoutShape.escape_byte_count + [Int64]$stderrShape.escape_byte_count))
        Write-Output ("[stock-server-private] backspace-bytes={0}" -f
            ([Int64]$stdoutShape.backspace_byte_count +
             [Int64]$stderrShape.backspace_byte_count))
        Write-Output ("[stock-server-private] high-bit-bytes={0}" -f
            ([Int64]$stdoutShape.high_bit_byte_count +
             [Int64]$stderrShape.high_bit_byte_count))
        Write-Output ("[stock-server-private] utf16-pattern={0}" -f
            (([bool]$stdoutShape.utf16_like -or
              [bool]$stderrShape.utf16_like).ToString().ToLowerInvariant()))
        Write-Output ("[stock-server-private] repeated-carriage-return={0}" -f
            (([bool]$stdoutShape.repeated_carriage_return -or
              [bool]$stderrShape.repeated_carriage_return).ToString().ToLowerInvariant()))
        Write-Output "[stock-server-private] stream-attribution=$($privateShape.stream_attribution)"
        Write-Output '[stock-server-private] result=success'
    }
    Write-StockServerProfileDiagnosticPublicOutput $orchestratorResult.Values
    return
}

$publicationReady = $false
$walkerValues = $null
$checkerValues = $null
$reconnectObservation = $null
$failureCategory = 'none'
$publicationFailureCategory = 'first_observation_publication_not_ready'
if ($null -ne $primaryError) {
    $failureCategory = 'orchestrator_failed'
    if ($null -ne $orchestratorResult -and
        $orchestratorResult.Values.ContainsKey('failure-category')) {
        $failureCategory = $orchestratorResult.Values['failure-category']
    }
} elseif (-not $restorationExact) {
    $failureCategory = 'research_restoration_failed'
} elseif (-not $externalExact) {
    $failureCategory = 'external_steam_state_changed'
} elseif ($cleanupErrors.Count -ne 0) {
    $failureCategory = 'transaction_cleanup_failed'
} elseif (-not $runExists) {
    $failureCategory = 'capture_run_not_created'
} else {
    try {
        Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
        $versionRecord = Read-BoundedJsonWithRetainedBytes `
            (Join-Path $runRoot 'version-observation.staged.json') `
            65536 'staged version observation' $runDirectoryCapability
        $version = $versionRecord.Value
        [byte[]]$versionBytes = $versionRecord.Bytes
        $isolationRecord = Read-BoundedJsonWithRetainedBytes `
            (Join-Path $runRoot 'isolation-attestation.staged.json') `
            65536 'staged isolation attestation' $runDirectoryCapability
        $isolation = $isolationRecord.Value
        [byte[]]$isolationBytes = $isolationRecord.Bytes
        $restorationRecord = Read-BoundedJsonWithRetainedBytes `
            (Join-Path $runRoot 'restoration-attestation.staged.json') `
            65536 'staged restoration attestation' $runDirectoryCapability
        $restoration = $restorationRecord.Value
        [byte[]]$restorationBytes = $restorationRecord.Bytes
        if ([string]$version.schema -cne 'hlclient.stock-runtime-version-observation.v1' -or
            [string]$version.map_category -cne $Map -or
            [string]$version.client_file_version -cne '1.1.1.1' -or
            [string]$version.client_pe_machine -cne 'x86' -or
            [string]$version.client_signature -cne 'valid' -or
            [string]$version.server_launcher_version -cne '4.1.1.1' -or
            [string]$version.server_pe_machine -cne 'x86' -or
            [string]$version.server_signature -cne 'valid' -or
            [Int64]$version.steam_app_id -ne 70 -or
            [Int64]$version.steam_build_id -ne 15961492 -or
            [string]$version.server_engine_version -cne '1.1.2.2' -or
            [Int64]$version.protocol -ne 48 -or [Int64]$version.server_build -ne 10210 -or
            [string]$version.client_profile_fingerprint -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$version.server_profile_fingerprint -cnotmatch '^[0-9a-f]{64}$' -or
            [string]$version.evidence_status -cne 'observed') {
            throw 'Version observation is not accepted.'
        }
        if ([string]$isolation.schema -cne 'hlclient.stock-runtime-isolation-attestation.v1' -or
            [string]$isolation.session_type -cne 'dynamic' -or
            [Int64]$isolation.persistent_rule_count -ne 0 -or
            [string]$isolation.ipv4_loopback -cne 'allowed' -or
            @('allowed', 'capability_unavailable') -cnotcontains
                [string]$isolation.ipv6_loopback -or
            [string]$isolation.non_loopback_canary -cne 'denied_os_classified' -or
            [string]$isolation.cleanup_status -cne 'exact' -or
            [string]$isolation.evidence_status -cne 'observed') {
            throw 'Isolation attestation is not accepted.'
        }
        $restorationPolicyAccepted =
            ([string]$restoration.schema -ceq
                'hlclient.stock-runtime-restoration.v2') -and
            (([string]$restoration.raw_external_state -ceq 'unchanged' -and
              [string]$restoration.external_file_drift -ceq 'none' -and
              [string]$restoration.protected_projection -ceq 'none' -and
              @('legacy-strict-v1', 'steam-appinfo-change-number-v1') -ccontains
                [string]$restoration.steam_rewrite_policy_id -and
              [string]$restoration.policy_decision -ceq 'strict_pass') -or
             ([string]$restoration.raw_external_state -ceq 'changed' -and
              [string]$restoration.external_file_drift -ceq 'changed' -and
              [string]$restoration.protected_projection -ceq 'match' -and
              [string]$restoration.steam_rewrite_policy_id -ceq
                'steam-appinfo-change-number-v1' -and
              [string]$restoration.policy_decision -ceq
                'explicit_advisory'))
        if (-not $restorationPolicyAccepted -or
            [string]$restoration.restoration_status -cne 'exact' -or
            -not [bool]$restoration.created_files_removed -or
            -not [bool]$restoration.protected_paths_included -or
            -not [bool]$restoration.owned_processes_stopped -or
            [bool]$restoration.input_automation_used -or
            [Int64]$restoration.input_events_injected -ne 0 -or
            [string]$restoration.pre_manifest_sha256 -cne
                [string]$restoration.post_manifest_sha256 -or
            (([string]$restoration.raw_external_state -ceq 'unchanged') -ne
             ([string]$restoration.external_pre_manifest_sha256 -ceq
                [string]$restoration.external_post_manifest_sha256))) {
            throw 'Restoration attestation is not accepted.'
        }
        foreach ($finalLeaf in @(
                'version-observation.json', 'isolation-attestation.json',
                'restoration-attestation.json', 'research-run-metadata.json')) {
            if (Test-Path -LiteralPath (Join-Path $runRoot $finalLeaf)) {
                throw 'Final evidence exists before publication review.'
            }
        }
        $first = Invoke-FirstObservationChecker $checkerPath $runRoot
        $second = Invoke-FirstObservationChecker $checkerPath $runRoot
        Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
        if ($first.ExitCode -ne 0 -or $second.ExitCode -ne 0 -or
            ($first.Lines -join "`n") -cne ($second.Lines -join "`n")) {
            throw 'Prepublication checker did not produce two identical successful runs.'
        }
        $checkerKeys = @(
            'profile', 'transport-valid', 'sequenced-c2s', 'sequenced-s2c',
            'fragments', 'duplicate-packets', 'old-packets',
            'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
            'delivered-fragment-datagrams', 'reassembled', 'decompressed',
            'signon-replay',
            'post-resource-boundary', 'boundary-payload-ordinal',
            'boundary-observed-ordinal', 'boundary-delivery-ordinal',
            'boundary-byte-offset', 'boundary-bit-offset',
            'boundary-source-sequence', 'boundary-source-payload-bytes',
            'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
            'boundary-reassembled', 'boundary-decompressed',
            'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
            'candidate-recurrence', 'candidate-stability', 'accepted-run',
            'publication-ready', 'result', 'structural-hash',
            'replay-structural-hash')
        $reconnectGenerationSuffixes = @(
            'first-observed-ordinal', 'last-observed-ordinal',
            'connectionless-exchanges', 'first-sequenced-packet-ordinal',
            'client-to-server-packets', 'server-to-client-packets',
            'boundary-payload-ordinal', 'boundary-observed-ordinal',
            'boundary-delivery-ordinal', 'boundary-byte-offset',
            'boundary-bit-offset', 'boundary-source-sequence',
            'boundary-source-payload-bytes', 'boundary-source-payload-bits',
            'boundary-next-unconsumed-bits', 'boundary-reassembled',
            'boundary-decompressed', 'boundary-byte-aligned',
            'candidate-bit-width', 'first-candidate',
            'candidate-body-consumed',
            'candidate-semantic-category-assigned',
            'replay-structural-hash')
        if ($canonicalScenario -ceq 'reconnect') {
            $checkerKeys += @(
                'connection-generation-count', 'exact-boundary-count',
                'runtime-candidate-count', 'generation-distinct',
                'candidate-conflict', 'retired-generation-a-tail-sink',
                'retired-generation-a-server-tail-packets',
                'generation-b-sequenced-after-fresh-accept')
            foreach ($label in @('a', 'b')) {
                foreach ($suffix in $reconnectGenerationSuffixes) {
                    $checkerKeys += 'generation-' + $label + '-' + $suffix
                }
            }
        }
        $checkerValues = Convert-PrefixedOutputToValues $first.Lines `
            '[stock-runtime] ' $checkerKeys 'first-observation checker'
        $expectedCandidateRecurrence = if ($canonicalScenario -ceq 'reconnect') {
            '2'
        } else { '1' }
        $expectedCandidateStability = if ($canonicalScenario -ceq 'reconnect') {
            'stable_observation'
        } else { 'single_observation' }
        if ($checkerValues['profile'] -cne
                'stock_protocol_48_build_10210_evidence_pending' -or
            $checkerValues['transport-valid'] -cne 'true' -or
            $checkerValues['signon-replay'] -cne 'complete' -or
            $checkerValues['post-resource-boundary'] -cne 'observed' -or
            $checkerValues['boundary-reassembled'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['boundary-decompressed'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['boundary-byte-aligned'] -cnotmatch '^(?:true|false)$' -or
            $checkerValues['first-candidate'] -cnotmatch
                '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
            [int]($checkerValues['first-candidate'] -replace '^bit-prefix:', '') -gt 255 -or
            $checkerValues['candidate-recurrence'] -cne
                $expectedCandidateRecurrence -or
            $checkerValues['candidate-stability'] -cne
                $expectedCandidateStability -or
            $checkerValues['structural-hash'] -cnotmatch '^[0-9a-f]{64}$' -or
            $checkerValues['replay-structural-hash'] -cnotmatch '^[0-9a-f]{64}$' -or
            $checkerValues['publication-ready'] -cne 'true' -or
            $checkerValues['accepted-run'] -cne 'false' -or
            $checkerValues['result'] -cne 'first-observation') {
            throw 'Prepublication checker did not reach publication readiness.'
        }
        if ($canonicalScenario -ceq 'reconnect' -and
            ($checkerValues['connection-generation-count'] -cne '2' -or
             $checkerValues['exact-boundary-count'] -cne '2' -or
             $checkerValues['runtime-candidate-count'] -cne '2' -or
             $checkerValues['generation-distinct'] -cne 'true' -or
             $checkerValues['candidate-conflict'] -cne 'false' -or
             $checkerValues['retired-generation-a-tail-sink'] -cne
                'routing_only' -or
             $checkerValues['generation-b-sequenced-after-fresh-accept'] -cne
                'true')) {
            throw 'Reconnect checker did not prove the exact A/B lifecycle.'
        }
        foreach ($countKey in @('sequenced-c2s', 'sequenced-s2c', 'fragments',
                'duplicate-packets', 'old-packets',
                'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
                'delivered-fragment-datagrams', 'reassembled', 'decompressed',
                'boundary-payload-ordinal',
                'boundary-observed-ordinal', 'boundary-delivery-ordinal',
                'boundary-byte-offset', 'boundary-source-sequence',
                'boundary-source-payload-bytes', 'boundary-source-payload-bits',
                'boundary-next-unconsumed-bits', 'candidate-bit-width')) {
            [Int64]$countValue = 0
            if (-not [Int64]::TryParse($checkerValues[$countKey], [ref]$countValue) -or
                $countValue -lt 0 -or $countValue -gt 268435456) {
                throw "Prepublication checker count $countKey is outside its bound."
            }
        }
        [Int64]$boundaryBitOffset = 0
        if (-not [Int64]::TryParse(
                $checkerValues['boundary-bit-offset'], [ref]$boundaryBitOffset) -or
            $boundaryBitOffset -lt 0 -or $boundaryBitOffset -gt 7) {
            throw 'Prepublication checker boundary bit offset is invalid.'
        }
        [Int64]$sourcePayloadBytes = $checkerValues['boundary-source-payload-bytes']
        [Int64]$sourcePayloadBits = $checkerValues['boundary-source-payload-bits']
        [Int64]$boundaryByteOffset = $checkerValues['boundary-byte-offset']
        [Int64]$remainingBits = $checkerValues['boundary-next-unconsumed-bits']
        [Int64]$candidateBitWidth = $checkerValues['candidate-bit-width']
        if ($sourcePayloadBits -ne ($sourcePayloadBytes * 8) -or
            (($boundaryByteOffset * 8) + $boundaryBitOffset + $remainingBits) -ne
                $sourcePayloadBits -or
            ($checkerValues['boundary-byte-aligned'] -ceq 'true') -ne
                ($boundaryBitOffset -eq 0) -or
            $candidateBitWidth -lt 1 -or $candidateBitWidth -gt 8 -or
            $candidateBitWidth -gt $remainingBits -or
            (($checkerValues['boundary-byte-aligned'] -ceq 'true') -and
                $candidateBitWidth -ne 8) -or
            ($checkerValues['first-candidate'].StartsWith('bit-prefix:') -and
                [int]($checkerValues['first-candidate'].Substring(11)) -ge
                    [Math]::Pow(2, $candidateBitWidth))) {
            throw 'Prepublication checker cursor/candidate geometry is inconsistent.'
        }
        if ($canonicalScenario -ceq 'reconnect') {
            $generationNumbers = @{}
            foreach ($label in @('a', 'b')) {
                $prefix = 'generation-' + $label + '-'
                $numbers = @{}
                foreach ($suffix in @(
                        'first-observed-ordinal', 'last-observed-ordinal',
                        'connectionless-exchanges',
                        'first-sequenced-packet-ordinal',
                        'client-to-server-packets', 'server-to-client-packets',
                        'boundary-payload-ordinal', 'boundary-observed-ordinal',
                        'boundary-delivery-ordinal', 'boundary-byte-offset',
                        'boundary-bit-offset', 'boundary-source-sequence',
                        'boundary-source-payload-bytes',
                        'boundary-source-payload-bits',
                        'boundary-next-unconsumed-bits',
                        'candidate-bit-width')) {
                    [Int64]$number = 0
                    if (-not [Int64]::TryParse(
                            $checkerValues[$prefix + $suffix], [ref]$number) -or
                        $number -lt 0 -or $number -gt 268435456) {
                        throw "Reconnect checker $prefix$suffix is outside its bound."
                    }
                    $numbers[$suffix] = $number
                }
                $candidate = $checkerValues[$prefix + 'first-candidate']
                if ($checkerValues[$prefix + 'boundary-reassembled'] -cnotmatch
                        '^(?:true|false)$' -or
                    $checkerValues[$prefix + 'boundary-decompressed'] -cnotmatch
                        '^(?:true|false)$' -or
                    $checkerValues[$prefix + 'boundary-byte-aligned'] -cnotmatch
                        '^(?:true|false)$' -or
                    $checkerValues[$prefix + 'candidate-body-consumed'] -cne
                        'false' -or
                    $checkerValues[$prefix +
                        'candidate-semantic-category-assigned'] -cne 'false' -or
                    $checkerValues[$prefix + 'replay-structural-hash'] -cnotmatch
                        '^[0-9a-f]{64}$' -or
                    $candidate -cnotmatch
                        '^(?:[0-9]{1,3}|bit-prefix:[0-9]{1,3})$' -or
                    [int]($candidate -replace '^bit-prefix:', '') -gt 255) {
                    throw "Reconnect checker generation $label metadata is invalid."
                }
                $firstOrdinal = [Int64]$numbers['first-observed-ordinal']
                $lastOrdinal = [Int64]$numbers['last-observed-ordinal']
                $firstSequence =
                    [Int64]$numbers['first-sequenced-packet-ordinal']
                $observedBoundary =
                    [Int64]$numbers['boundary-observed-ordinal']
                $generationBytes =
                    [Int64]$numbers['boundary-source-payload-bytes']
                $generationBits =
                    [Int64]$numbers['boundary-source-payload-bits']
                $generationByteOffset =
                    [Int64]$numbers['boundary-byte-offset']
                $generationBitOffset =
                    [Int64]$numbers['boundary-bit-offset']
                $generationRemaining =
                    [Int64]$numbers['boundary-next-unconsumed-bits']
                $generationCandidateWidth =
                    [Int64]$numbers['candidate-bit-width']
                $generationAligned =
                    $checkerValues[$prefix + 'boundary-byte-aligned'] -ceq 'true'
                $candidateIsPrefix = $candidate.StartsWith('bit-prefix:')
                if ($firstOrdinal -gt $lastOrdinal -or
                    $firstSequence -lt $firstOrdinal -or
                    $firstSequence -gt $lastOrdinal -or
                    $observedBoundary -lt $firstOrdinal -or
                    $observedBoundary -gt $lastOrdinal -or
                    $numbers['connectionless-exchanges'] -lt 1 -or
                    $numbers['client-to-server-packets'] -lt 1 -or
                    $numbers['server-to-client-packets'] -lt 1 -or
                    $generationBitOffset -gt 7 -or $generationBytes -lt 1 -or
                    $generationBits -ne ($generationBytes * 8) -or
                    (($generationByteOffset * 8) + $generationBitOffset +
                        $generationRemaining) -ne $generationBits -or
                    $generationRemaining -lt 1 -or
                    $generationCandidateWidth -lt 1 -or
                    $generationCandidateWidth -gt 8 -or
                    $generationCandidateWidth -gt $generationRemaining -or
                    $generationAligned -ne ($generationBitOffset -eq 0) -or
                    $candidateIsPrefix -eq $generationAligned -or
                    ($generationAligned -and $generationCandidateWidth -ne 8) -or
                    ($candidateIsPrefix -and
                        [int]$candidate.Substring(11) -ge
                            [Math]::Pow(2, $generationCandidateWidth))) {
                    throw "Reconnect checker generation $label geometry is inconsistent."
                }
                $generationNumbers[$label] = $numbers
            }
            if ([Int64]$generationNumbers['a']['last-observed-ordinal'] -ge
                    [Int64]$generationNumbers['b']['first-observed-ordinal'] -or
                $checkerValues['generation-a-first-candidate'] -cne
                    $checkerValues['generation-b-first-candidate'] -or
                $checkerValues['generation-a-candidate-bit-width'] -cne
                    $checkerValues['generation-b-candidate-bit-width'] -or
                $checkerValues['generation-a-boundary-bit-offset'] -cne
                    $checkerValues['generation-b-boundary-bit-offset']) {
                throw 'Reconnect checker generations overlap or expose conflicting candidates.'
            }
            foreach ($suffix in @(
                    'boundary-payload-ordinal', 'boundary-observed-ordinal',
                    'boundary-delivery-ordinal', 'boundary-byte-offset',
                    'boundary-bit-offset', 'boundary-source-sequence',
                    'boundary-source-payload-bytes',
                    'boundary-source-payload-bits',
                    'boundary-next-unconsumed-bits', 'boundary-reassembled',
                    'boundary-decompressed', 'boundary-byte-aligned',
                    'candidate-bit-width', 'first-candidate')) {
                if ($checkerValues[$suffix] -cne
                    $checkerValues['generation-a-' + $suffix]) {
                    throw 'Reconnect checker aggregate representative is not generation A.'
                }
            }
            [Int64]$retiredTailPackets = 0
            if (-not [Int64]::TryParse(
                    $checkerValues['retired-generation-a-server-tail-packets'],
                    [ref]$retiredTailPackets) -or
                $retiredTailPackets -lt 0 -or
                $retiredTailPackets -gt $MaximumDatagrams) {
                throw 'Reconnect retired-generation A tail count is outside its bound.'
            }
        }
        [Int64]$replayAcceptedSequenced =
            [Int64]$checkerValues['sequenced-c2s'] +
            [Int64]$checkerValues['sequenced-s2c']
        [Int64]$replaySuppressedSequenced =
            [Int64]$checkerValues['duplicate-packets'] +
            [Int64]$checkerValues['old-packets']
        [Int64]$deliveredSequenced =
            [Int64]$checkerValues['delivered-sequenced-c2s'] +
            [Int64]$checkerValues['delivered-sequenced-s2c']
        [Int64]$routingOnlyTail = if ($canonicalScenario -ceq 'reconnect') {
            [Int64]$checkerValues['retired-generation-a-server-tail-packets']
        } else { 0 }
        if ([Int64]$checkerValues['sequenced-c2s'] -gt
                [Int64]$checkerValues['delivered-sequenced-c2s'] -or
            [Int64]$checkerValues['sequenced-s2c'] -gt
                [Int64]$checkerValues['delivered-sequenced-s2c'] -or
            [Int64]$checkerValues['fragments'] -gt
                [Int64]$checkerValues['delivered-fragment-datagrams'] -or
            ($replayAcceptedSequenced + $replaySuppressedSequenced +
                $routingOnlyTail) -ne
                $deliveredSequenced) {
            throw 'Replay accepted/suppressed accounting disagrees with delivered transport counts.'
        }
        [Int64]$sequencedServerPackets =
            $(if ($canonicalScenario -ceq 'reconnect') {
                $checkerValues['sequenced-s2c']
            } else {
                $checkerValues['delivered-sequenced-s2c']
            })
        if (($canonicalScenario -ceq 'baseline' -or
                $canonicalScenario -ceq 'idle-runtime') -and
            $orchestratorDuration -lt 30000) {
            $publicationFailureCategory = 'minimum_observation_duration_not_met'
            throw 'Baseline and idle-runtime acceptance require at least 30 seconds of actual owned-session duration.'
        }
        if ($sequencedServerPackets -lt 100) {
            $publicationFailureCategory =
                'per_run_server_packet_threshold_not_met'
            throw 'Every accepted scenario requires at least 100 generation-attributed sequenced server-to-client packets.'
        }
        $walkerFirst = Invoke-IndependentTransportWalker `
            -WalkerPath $walkerPath -RunRoot $runRoot `
            -CheckerValues $checkerValues `
            -Reconnect ($canonicalScenario -ceq 'reconnect')
        $walkerSecond = Invoke-IndependentTransportWalker `
            -WalkerPath $walkerPath -RunRoot $runRoot `
            -CheckerValues $checkerValues `
            -Reconnect ($canonicalScenario -ceq 'reconnect')
        Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
        if (($walkerFirst -join "`n") -cne ($walkerSecond -join "`n")) {
            throw 'Independent transport walker did not produce two identical runs.'
        }
        $walkerLines = $walkerFirst
        $walkerKeys = @(
            'run-id', 'journal-entries', 'raw-datagrams', 'raw-bytes',
            'observed-c2s', 'observed-s2c', 'delivered-c2s', 'delivered-s2c',
            'observed-connectionless-c2s', 'observed-connectionless-s2c',
            'observed-sequenced-c2s', 'observed-sequenced-s2c',
            'observed-fragment-datagrams', 'observed-reliable-datagrams',
            'delivered-connectionless-c2s', 'delivered-connectionless-s2c',
            'delivered-sequenced-c2s', 'delivered-sequenced-s2c',
            'delivered-fragment-datagrams', 'delivered-reliable-datagrams',
            'wrong-source-datagrams', 'emitted-datagrams', 'transport-complete',
            'last-observed-timestamp-us', 'last-delivered-sequenced-s2c-timestamp-us',
            'observed-delivered-policy', 'final-manifest',
            'post-resource-boundary', 'boundary-payload-ordinal',
            'boundary-observed-ordinal', 'boundary-delivery-ordinal',
            'boundary-byte-offset', 'boundary-bit-offset',
            'boundary-source-sequence', 'boundary-source-payload-bytes',
            'boundary-source-payload-bits', 'boundary-next-unconsumed-bits',
            'boundary-reassembled', 'boundary-decompressed',
            'boundary-byte-aligned', 'candidate-bit-width', 'first-candidate',
            'replay-structural-hash', 'result')
        if ($canonicalScenario -ceq 'reconnect') {
            $walkerKeys += @(
                'connection-generation-count', 'exact-boundary-count',
                'runtime-candidate-count', 'generation-distinct',
                'candidate-conflict', 'candidate-recurrence',
                'candidate-stability', 'retired-generation-a-tail-sink',
                'retired-generation-a-server-tail-packets',
                'generation-b-sequenced-after-fresh-accept')
            foreach ($label in @('a', 'b')) {
                foreach ($suffix in $reconnectGenerationSuffixes) {
                    $walkerKeys += 'generation-' + $label + '-' + $suffix
                }
            }
        }
        $walkerValues = Convert-PrefixedOutputToValues $walkerLines `
            '[stock-runtime-walk] ' $walkerKeys 'independent transport walker'
        if ($walkerValues['result'] -cne 'success' -or
            $walkerValues['run-id'] -cne $runId -or
            $walkerValues['final-manifest'] -cne 'absent-prepublication' -or
            $walkerValues['wrong-source-datagrams'] -cne '0' -or
            $walkerValues['transport-complete'] -cne 'true' -or
            $walkerValues['delivered-sequenced-c2s'] -cne $checkerValues['delivered-sequenced-c2s'] -or
            $walkerValues['delivered-sequenced-s2c'] -cne $checkerValues['delivered-sequenced-s2c'] -or
            $walkerValues['delivered-fragment-datagrams'] -cne $checkerValues['delivered-fragment-datagrams'] -or
            $walkerValues['boundary-payload-ordinal'] -cne $checkerValues['boundary-payload-ordinal'] -or
            $walkerValues['boundary-observed-ordinal'] -cne $checkerValues['boundary-observed-ordinal'] -or
            $walkerValues['boundary-delivery-ordinal'] -cne $checkerValues['boundary-delivery-ordinal'] -or
            $walkerValues['boundary-byte-offset'] -cne $checkerValues['boundary-byte-offset'] -or
            $walkerValues['boundary-bit-offset'] -cne $checkerValues['boundary-bit-offset'] -or
            $walkerValues['boundary-source-sequence'] -cne $checkerValues['boundary-source-sequence'] -or
            $walkerValues['boundary-source-payload-bytes'] -cne $checkerValues['boundary-source-payload-bytes'] -or
            $walkerValues['boundary-source-payload-bits'] -cne $checkerValues['boundary-source-payload-bits'] -or
            $walkerValues['boundary-next-unconsumed-bits'] -cne $checkerValues['boundary-next-unconsumed-bits'] -or
            $walkerValues['boundary-reassembled'] -cne $checkerValues['boundary-reassembled'] -or
            $walkerValues['boundary-decompressed'] -cne $checkerValues['boundary-decompressed'] -or
            $walkerValues['boundary-byte-aligned'] -cne $checkerValues['boundary-byte-aligned'] -or
            $walkerValues['candidate-bit-width'] -cne $checkerValues['candidate-bit-width'] -or
            $walkerValues['first-candidate'] -cne $checkerValues['first-candidate'] -or
            $walkerValues['replay-structural-hash'] -cne $checkerValues['replay-structural-hash']) {
            throw 'Independent walker and production structural summaries disagree.'
        }
        if ($canonicalScenario -ceq 'reconnect') {
            foreach ($key in @(
                    'connection-generation-count', 'exact-boundary-count',
                    'runtime-candidate-count', 'generation-distinct',
                    'candidate-conflict', 'candidate-recurrence',
                    'candidate-stability', 'retired-generation-a-tail-sink',
                    'retired-generation-a-server-tail-packets',
                    'generation-b-sequenced-after-fresh-accept')) {
                if ($walkerValues[$key] -cne $checkerValues[$key]) {
                    throw "Reconnect checker/walker aggregate $key disagrees."
                }
            }
            foreach ($label in @('a', 'b')) {
                foreach ($suffix in $reconnectGenerationSuffixes) {
                    $key = 'generation-' + $label + '-' + $suffix
                    if ($walkerValues[$key] -cne $checkerValues[$key]) {
                        throw "Reconnect checker/walker generation $key disagrees."
                    }
                }
            }
            $reconnectObservation =
                New-ReconnectFinalObservation -Values $checkerValues
        }
        if ($canonicalScenario -ceq 'baseline' -or
            $canonicalScenario -ceq 'idle-runtime') {
            [Int64]$lastObservedTransportUs = 0
            if (-not [Int64]::TryParse(
                    $walkerValues['last-observed-timestamp-us'],
                    [ref]$lastObservedTransportUs) -or
                $lastObservedTransportUs -lt 30000000) {
                $publicationFailureCategory = 'minimum_capture_duration_not_met'
                throw 'Baseline and idle-runtime acceptance require at least 30 seconds on the capture transport clock.'
            }
        }
        if ($canonicalScenario -ceq 'idle-runtime') {
            [Int64]$lastLiveS2cUs = 0
            if (-not [Int64]::TryParse(
                    $walkerValues['last-delivered-sequenced-s2c-timestamp-us'],
                    [ref]$lastLiveS2cUs)) {
                throw 'Independent walker did not publish the idle live-through timestamp.'
            }
            [Int64]$minimumLiveThroughUs = [Math]::Max(
                25000000, ([Int64]$orchestratorDuration - 5000) * 1000)
            if ($lastLiveS2cUs -lt 30000000 -or
                $lastLiveS2cUs -lt $minimumLiveThroughUs) {
                $publicationFailureCategory = 'idle_runtime_did_not_remain_live_through_end'
                throw 'Idle-runtime acceptance requires delivered sequenced server traffic through the final five seconds of a 30-second-or-longer run.'
            }
        }
        $publicationReady = $true
    } catch {
        $failureCategory = $publicationFailureCategory
        $primaryError = $_
    }
}

if ($runExists) {
    $duration = $null
    if ($null -ne $orchestratorResult -and
        $orchestratorResult.Values.ContainsKey('duration-ms')) {
        $duration = [Int64]$orchestratorResult.Values['duration-ms']
    }
    $rawCount = $null
    $journalCount = $null
    if ($null -ne $walkerValues) {
        $rawCount = [Int64]$walkerValues['raw-datagrams']
        $journalCount = [Int64]$walkerValues['journal-entries']
    }
    $runManifest = [ordered]@{
        schema = 'hlclient.stock-runtime-research-run.v2'
        run_id = $runId
        scenario = $canonicalScenario
        map_category = $Map
        external_target_profile = $research.ExternalTargetProfile
        external_target_count = [Int64]$research.ExternalTargetCount
        duration_ms = $duration
        isolation_status = $(if ($publicationReady) { 'verified' } else { 'not-accepted' })
        process_ownership_status = $(if ($ownedStopped) { 'verified-cleanup' } else { 'not-accepted' })
        version_profile_status = $(if ($publicationReady) { 'verified' } else { 'not-accepted' })
        relay_status = $(if ($null -ne $orchestratorResult -and
                $orchestratorResult.Values.ContainsKey('relay-ready')) {
                $orchestratorResult.Values['relay-ready']
            } else { 'not-observed' })
        client_ready_status = $(if ($null -ne $orchestratorResult -and
                $orchestratorResult.Values.ContainsKey('client-ready')) {
                $orchestratorResult.Values['client-ready']
            } else { 'not-observed' })
        restoration_status = $(if ($restorationExact) { 'exact' } else { 'not-exact' })
        external_drift_status = $(if ($externalExact) { 'none' } elseif (
                $null -ne $externalDifference) { 'changed' } else { 'unavailable' })
        raw_external_state = $(if ($null -ne $externalDifference) {
                $externalDifference.raw_external_state
            } else { 'incomplete' })
        protected_projection = $(if ($null -ne $externalDifference) {
                $externalDifference.protected_projection
            } else { 'incomplete' })
        steam_rewrite_policy_id = $stockSteamRewritePolicyId
        policy_decision = $(if ($null -ne $externalDifference) {
                $externalDifference.policy_decision
            } else { 'reject' })
        raw_datagram_count = $rawCount
        journal_entry_count = $journalCount
        delivered_sequenced_c2s_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['delivered-sequenced-c2s']
            } else { $null })
        delivered_sequenced_s2c_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['delivered-sequenced-s2c']
            } else { $null })
        delivered_fragment_datagram_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['delivered-fragment-datagrams']
            } else { $null })
        reassembled_payload_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['reassembled']
            } else { $null })
        decompressed_payload_count = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['decompressed']
            } else { $null })
        offline_replay_status = $(if ($publicationReady) { 'success' } else { 'not-accepted' })
        post_resource_boundary_status = $(if ($publicationReady) { 'observed' } else { 'not-accepted' })
        post_resource_replay_payload_ordinal = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-payload-ordinal'] } else { $null })
        post_resource_corpus_observed_ordinal = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-observed-ordinal'] } else { $null })
        post_resource_delivery_ordinal = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-delivery-ordinal'] } else { $null })
        post_resource_byte_offset = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-byte-offset'] } else { $null })
        post_resource_bit_offset = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-bit-offset'] } else { $null })
        post_resource_source_sequence = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-source-sequence'] } else { $null })
        post_resource_source_payload_bytes = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-source-payload-bytes'] } else { $null })
        post_resource_source_payload_bits = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-source-payload-bits'] } else { $null })
        post_resource_next_unconsumed_bits = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['boundary-next-unconsumed-bits'] } else { $null })
        post_resource_reassembled = $(if ($null -ne $checkerValues) {
                $checkerValues['boundary-reassembled'] -ceq 'true' } else { $null })
        post_resource_decompressed = $(if ($null -ne $checkerValues) {
                $checkerValues['boundary-decompressed'] -ceq 'true' } else { $null })
        post_resource_boundary_byte_aligned = $(if ($null -ne $checkerValues) {
                $checkerValues['boundary-byte-aligned'] -ceq 'true'
            } else { $null })
        first_observation_status = $(if ($publicationReady) { 'observed' } else { 'not-accepted' })
        first_candidate = $(if ($null -ne $checkerValues) {
                $checkerValues['first-candidate']
            } else { $null })
        first_candidate_bit_width = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['candidate-bit-width'] } else { $null })
        first_candidate_recurrence = $(if ($null -ne $checkerValues) {
                [Int64]$checkerValues['candidate-recurrence'] } else { $null })
        transport_structural_sha256 = $(if ($null -ne $checkerValues) {
                $checkerValues['structural-hash'] } else { $null })
        replay_structural_sha256 = $(if ($null -ne $checkerValues) {
                $checkerValues['replay-structural-hash'] } else { $null })
        last_delivered_sequenced_s2c_timestamp_us = $(if ($null -ne $walkerValues) {
                [Int64]$walkerValues['last-delivered-sequenced-s2c-timestamp-us']
            } else { $null })
        last_observed_transport_timestamp_us = $(if ($null -ne $walkerValues) {
                [Int64]$walkerValues['last-observed-timestamp-us']
            } else { $null })
        candidate_stability = $(if ($null -ne $checkerValues) {
                $checkerValues['candidate-stability']
            } else { $null })
        accepted_transport_run = $publicationReady
        accepted_evidence_run = $publicationReady
        failure_category = $(if ($publicationReady) { 'none' } else { $failureCategory })
    }
    if ($publicationReady -and $canonicalScenario -ceq 'reconnect') {
        $runManifest['connection_generation_count'] = 2
        $runManifest['exact_boundary_count'] = 2
        $runManifest['runtime_candidate_count'] = 2
        $runManifest['generation_distinct'] = $true
        $runManifest['candidate_conflict'] = $false
    }
    try {
        Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
        if ($publicationReady) {
            Publish-AcceptedEvidenceTransaction -RunRoot $runRoot `
                -Version $version -VersionBytes $versionBytes `
                -Isolation $isolation -IsolationBytes $isolationBytes `
                -Restoration $restoration `
                -RestorationBytes $restorationBytes `
                -ReconnectObservation $reconnectObservation `
                -RunManifest $runManifest `
                -OwnedJobsExact $ownedStopped `
                -RestorationExact $restorationExact `
                -ExternalStateExact $externalExact `
                -CheckerWalkerReady $publicationReady `
                -FailureCategory $failureCategory `
                -DirectoryCapability $runDirectoryCapability
        } else {
            # A rejected run may retain its typed accepted=false manifest and
            # staged candidates, but never any final evidence leaf.
            foreach ($finalLeaf in @(
                    'version-observation.json',
                    'isolation-attestation.json',
                    'restoration-attestation.json',
                    'reconnect-observation.json')) {
                if (Test-Path -LiteralPath (Join-Path $runRoot $finalLeaf)) {
                    throw 'Rejected run contains a final evidence leaf.'
                }
            }
            Write-AtomicJsonNoOverwrite -Path `
                (Join-Path $runRoot 'research-run-metadata.json') `
                -Value $runManifest -Label 'rejected research run manifest' `
                -DirectoryCapability $runDirectoryCapability
        }
    } catch {
        $publicationError = $_
        if ($publicationReady) {
            $failureCategory = 'evidence_publication_failed'
            $publicationReady = $false
            try {
                Write-RejectedManifestAfterEvidencePublicationFailure `
                    $runRoot $runManifest $runDirectoryCapability
            } catch {
                [void]$cleanupErrors.Add(
                    'Rejected publication manifest failed: ' +
                    $_.Exception.Message)
            }
        }
        [void]$cleanupErrors.Add($publicationError.Exception.Message)
    }
}

if ($cleanupErrors.Count -ne 0) {
    if ($null -ne $runDirectoryCapability) {
        $runDirectoryCapability.Dispose()
        $runDirectoryCapability = $null
    }
    if ($failureCategory -ceq 'none') { $failureCategory = 'transaction_cleanup_failed' }
    Write-Output '[stock-runtime-capture] active-capture=failed'
    Write-Output "[stock-runtime-capture] failure-category=$failureCategory"
    Write-Output '[stock-runtime-capture] accepted-evidence-run=false'
    Write-Output '[stock-runtime-capture] result=failed'
    $prefix = if ($null -ne $primaryError) { $primaryError.Exception.Message + '; ' } else { '' }
    throw ($prefix + ($cleanupErrors -join '; '))
}
if ($null -ne $primaryError) {
    if ($null -ne $runDirectoryCapability) {
        $runDirectoryCapability.Dispose()
        $runDirectoryCapability = $null
    }
    Write-Output '[stock-runtime-capture] active-capture=failed'
    Write-Output "[stock-runtime-capture] failure-category=$failureCategory"
    Write-Output '[stock-runtime-capture] accepted-evidence-run=false'
    Write-Output '[stock-runtime-capture] result=failed'
    throw $primaryError
}
if (-not $publicationReady) {
    if ($null -ne $runDirectoryCapability) {
        $runDirectoryCapability.Dispose()
        $runDirectoryCapability = $null
    }
    Write-Output '[stock-runtime-capture] active-capture=failed'
    Write-Output "[stock-runtime-capture] failure-category=$failureCategory"
    Write-Output '[stock-runtime-capture] accepted-evidence-run=false'
    Write-Output '[stock-runtime-capture] result=failed'
    throw "Stock runtime capture did not reach accepted publication: $failureCategory."
}

Assert-RunDirectoryCapability $runDirectoryCapability $runRoot
$runDirectoryCapability.Dispose()
$runDirectoryCapability = $null
Write-Output "[stock-runtime-capture] run-id=$runId"
Write-Output '[stock-runtime-capture] active-capture=completed'
Write-Output "[stock-runtime-capture] stock-processes-started=$($orchestratorResult.Values['processes-started'])"
Write-Output "[stock-runtime-capture] owned-processes-started=$($orchestratorResult.Values['processes-started'])"
Write-Output '[stock-runtime-capture] relay-started=true'
Write-Output '[stock-runtime-capture] client-ready=true'
Write-Output '[stock-runtime-capture] bounded-transport-complete=true'
Write-Output '[stock-runtime-capture] restoration=exact'
Write-Output '[stock-runtime-capture] external-file-drift=none'
Write-Output '[stock-runtime-capture] post-resource-boundary=observed'
Write-Output '[stock-runtime-capture] first-observation=observed'
if ($canonicalScenario -ceq 'reconnect') {
    Write-Output '[stock-runtime-capture] connection-generations=2'
    Write-Output '[stock-runtime-capture] post-resource-boundaries=2'
    Write-Output '[stock-runtime-capture] runtime-candidates=2'
    Write-Output '[stock-runtime-capture] generation-distinct=true'
    Write-Output '[stock-runtime-capture] candidate-conflict=false'
}
Write-Output '[stock-runtime-capture] accepted-evidence-run=true'
Write-Output '[stock-runtime-capture] result=success'
