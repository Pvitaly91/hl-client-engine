#requires -Version 5.1

<#
.SYNOPSIS
Checks the PowerShell active opt-in/preflight boundary without stock launch.

.DESCRIPTION
Always exercises missing/wrong confirmation and forbidden-bypass contracts.
When elevated, it additionally exercises unsafe-root, primary-Steam-shaped root,
missing-marker and invalid-version paths using disposable fake files. Actual WFP,
signature, version/profile and canary capability cases are owned by the C++ pure
and capability tests; this script reports an explicit capability skip when the
host cannot execute them.
#>
[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string]$OrchestratorPath = '.\build\bin\Debug\hlclient_stock_runtime_orchestrator.exe'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
$capture = Join-Path $PSScriptRoot 'capture_stock_runtime_state.ps1'
$token = 'HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1'
$manualRoot = Join-Path $repositoryRoot 'manual-artifacts\stock-runtime'

function Get-PathObservation {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return 'absent' }
    if ($item.PSIsContainer) {
        return 'directory|' + $item.CreationTimeUtc.Ticks + '|' + $item.LastWriteTimeUtc.Ticks
    }
    return 'file|' + $item.Length + '|' + $item.CreationTimeUtc.Ticks + '|' +
        $item.LastWriteTimeUtc.Ticks + '|' +
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Invoke-ExpectedOptInFailure {
    param([string]$Confirmation)
    $output = [Collections.Generic.List[string]]::new()
    $message = ''
    try {
        $arguments = @{
            ResearchHalfLifeRoot = 'Z:\absent-research'
            ClientPath = 'Z:\absent-research\hl.exe'
            HldsPath = 'Z:\absent-research\hlds.exe'
            CaptureToolPath = 'Z:\absent-capture.exe'
            NetworkIsolationGuardPath = 'Z:\absent-guard.exe'
            AppManifestPath = 'Z:\appmanifest_70.acf'
            Game = 'valve'; Map = 'boot_camp'; Scenario = 'baseline'
        }
        if ($null -ne $Confirmation) {
            $arguments.EnableActiveCapture = $true
            $arguments.ConfirmActiveCapture = $Confirmation
        }
        & $capture @arguments | ForEach-Object { [void]$output.Add($_.ToString()) }
    } catch { $message = $_.Exception.Message }
    if ($message -cnotmatch '^Active stock-runtime capture requires the exact explicit confirmation token' -or
        $output -cnotcontains '[stock-runtime-capture] network-operations=0' -or
        $output -cnotcontains '[stock-runtime-capture] wfp-sessions-started=0') {
        throw 'Opt-in failure did not retain its zero-mutation contract.'
    }
}

function Test-IsElevatedAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        return [Security.Principal.WindowsPrincipal]::new($identity).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
    } finally { $identity.Dispose() }
}

$beforeManual = Get-PathObservation $manualRoot
Invoke-ExpectedOptInFailure $null
Invoke-ExpectedOptInFailure $token.ToLowerInvariant()
if ((Get-PathObservation $manualRoot) -cne $beforeManual) {
    throw 'Opt-in tests changed the capture root.'
}

$reconnectOutput = [Collections.Generic.List[string]]::new()
$reconnectMessage = ''
try {
    & $capture -EnableActiveCapture -ConfirmActiveCapture $token `
        -ResearchHalfLifeRoot 'Z:\absent-research' `
        -ClientPath 'Z:\absent-research\hl.exe' `
        -HldsPath 'Z:\absent-research\hlds.exe' `
        -CaptureToolPath 'Z:\absent-capture.exe' `
        -NetworkIsolationGuardPath 'Z:\absent-guard.exe' `
        -AppManifestPath 'Z:\appmanifest_70.acf' `
        -Game valve -Map boot_camp -Scenario reconnect |
        ForEach-Object { [void]$reconnectOutput.Add($_.ToString()) }
} catch { $reconnectMessage = $_.Exception.Message }
if ($reconnectMessage -cnotmatch '^Reconnect requires two controlled stock sessions' -or
    $reconnectOutput -cnotcontains
        '[stock-runtime-capture] failure-category=reconnect_lifecycle_pending' -or
    $reconnectOutput -cnotcontains '[stock-runtime-capture] processes-started=0' -or
    $reconnectOutput -cnotcontains
        '[stock-runtime-capture] restoration-backups-created=0' -or
    (Get-PathObservation $manualRoot) -cne $beforeManual) {
    throw 'Single-session reconnect did not fail before path/backup/run mutation.'
}

$sourceText = Get-Content -Raw -LiteralPath $capture
foreach ($forbidden in @(
        'disable-isolation', 'trust-firewall', 'skip-signature',
        'skip-version-check', 'allow-external-network',
        'allow-primary-steam-root', 'ignore-restoration',
        'force-accepted-run', 'skip-runtime-boundary')) {
    if ($sourceText -cmatch ('(?m)^\s*\[(?:Alias|Parameter).*' + [regex]::Escape($forbidden)) -or
        $sourceText -cmatch ('(?m)^\s*\[string\]\$' + [regex]::Escape($forbidden))) {
        throw "Capture wrapper exposes forbidden bypass $forbidden."
    }
}
if ($sourceText -notmatch "HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1" -or
    $sourceText -match 'Env:\\.*HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE') {
    throw 'Exact confirmation token contract is absent or environment-backed.'
}

if (-not (Test-IsElevatedAdministrator)) {
    $output = [Collections.Generic.List[string]]::new()
    $message = ''
    try {
        & $capture -ValidateActiveCaptureEnvironment `
            -ResearchHalfLifeRoot 'Z:\absent-research' `
            -ClientPath 'Z:\absent-research\hl.exe' `
            -HldsPath 'Z:\absent-research\hlds.exe' `
            -CaptureToolPath 'Z:\absent-capture.exe' `
            -NetworkIsolationGuardPath 'Z:\absent-guard.exe' `
            -AppManifestPath 'Z:\appmanifest_70.acf' |
            ForEach-Object { [void]$output.Add($_.ToString()) }
    } catch { $message = $_.Exception.Message }
    if ($message -cnotmatch '^Active environment validation requires an elevated PowerShell' -or
        $output -cnotcontains
            '[stock-runtime-capture] failure-category=network_isolation_privilege_required') {
        throw 'Non-elevated active preflight did not fail with its typed privilege status.'
    }
    Write-Output '[stock-runtime-active-preflight-test] elevated-path-cases=capability-skipped-not-elevated'
} else {
    $testRoot = Join-Path ([IO.Path]::GetTempPath()) (
        'hlclient-stock-runtime-preflight-test-' + [Guid]::NewGuid().ToString('N'))
    try {
        [IO.Directory]::CreateDirectory($testRoot) | Out-Null
        $unsafeMessage = ''
        try {
            & $capture -ValidateActiveCaptureEnvironment `
                -ResearchHalfLifeRoot $repositoryRoot `
                -ClientPath (Join-Path $repositoryRoot 'hl.exe') `
                -HldsPath (Join-Path $repositoryRoot 'hlds.exe') `
                -CaptureToolPath 'Z:\absent-capture.exe' `
                -NetworkIsolationGuardPath 'Z:\absent-guard.exe' `
                -AppManifestPath 'Z:\appmanifest_70.acf' | Out-Null
        } catch { $unsafeMessage = $_.Exception.Message }
        if ($unsafeMessage -cnotmatch 'disjoint from repository') {
            throw 'Unsafe repository-root preflight case did not fail closed.'
        }

        $steamShaped = Join-Path $testRoot 'steamapps\common\Half-Life'
        [IO.Directory]::CreateDirectory($steamShaped) | Out-Null
        $steamMessage = ''
        try {
            & $capture -ValidateActiveCaptureEnvironment `
                -ResearchHalfLifeRoot $steamShaped `
                -ClientPath (Join-Path $steamShaped 'hl.exe') `
                -HldsPath (Join-Path $steamShaped 'hlds.exe') `
                -CaptureToolPath 'Z:\absent-capture.exe' `
                -NetworkIsolationGuardPath 'Z:\absent-guard.exe' `
                -AppManifestPath 'Z:\appmanifest_70.acf' | Out-Null
        } catch { $steamMessage = $_.Exception.Message }
        if ($steamMessage -cnotmatch 'disjoint from repository and Steam libraries') {
            throw 'Primary-Steam-shaped root preflight case did not fail closed.'
        }

        $missingMarker = Join-Path $testRoot 'missing-marker'
        [IO.Directory]::CreateDirectory((Join-Path $missingMarker 'valve')) | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $missingMarker 'hl.exe'), [byte[]](0))
        [IO.File]::WriteAllBytes((Join-Path $missingMarker 'hlds.exe'), [byte[]](0))
        $message = ''
        try {
            & $capture -ValidateActiveCaptureEnvironment `
                -ResearchHalfLifeRoot $missingMarker `
                -ClientPath (Join-Path $missingMarker 'hl.exe') `
                -HldsPath (Join-Path $missingMarker 'hlds.exe') `
                -CaptureToolPath 'Z:\absent-capture.exe' `
                -NetworkIsolationGuardPath 'Z:\absent-guard.exe' `
                -AppManifestPath 'Z:\appmanifest_70.acf' | Out-Null
        } catch { $message = $_.Exception.Message }
        if ($message -cnotmatch 'lacks the exact isolation marker') {
            throw 'Missing-marker preflight case did not fail at the marker gate.'
        }

        [IO.File]::WriteAllText(
            (Join-Path $missingMarker '.hlclient-research-isolated'),
            'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1',
            [Text.Encoding]::ASCII)
        $message = ''
        try {
            & $capture -ValidateActiveCaptureEnvironment `
                -ResearchHalfLifeRoot $missingMarker `
                -ClientPath (Join-Path $missingMarker 'hl.exe') `
                -HldsPath (Join-Path $missingMarker 'hlds.exe') `
                -CaptureToolPath 'Z:\absent-capture.exe' `
                -NetworkIsolationGuardPath 'Z:\absent-guard.exe' `
                -AppManifestPath 'Z:\appmanifest_70.acf' | Out-Null
        } catch { $message = $_.Exception.Message }
        if ($message -cnotmatch 'stock client version is not accepted') {
            throw 'Invalid-binary/version preflight case did not fail at the binary gate.'
        }
        Write-Output '[stock-runtime-active-preflight-test] elevated-root-marker-version-cases=success'
    } finally {
        if (Test-Path -LiteralPath $testRoot) {
            $expectedParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
            if ([IO.Path]::GetFullPath((Split-Path -Parent $testRoot)).TrimEnd('\', '/') -ine
                    $expectedParent -or
                [IO.Path]::GetFileName($testRoot) -cnotmatch
                    '^hlclient-stock-runtime-preflight-test-[0-9a-f]{32}$') {
                throw 'Preflight test cleanup target identity is invalid.'
            }
            [IO.Directory]::Delete($testRoot, $true)
        }
    }
}

$orchestrator = [IO.Path]::GetFullPath($OrchestratorPath)
if (Test-Path -LiteralPath $orchestrator -PathType Leaf) {
    $saved = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& $orchestrator --validate-config 2>&1 |
            ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    if ($exitCode -ne 0 -or
        $output -notcontains '[stock-runtime-orchestrator] result=success') {
        throw 'Project-owned all-green fake/config preflight failed.'
    }
    Write-Output '[stock-runtime-active-preflight-test] fake-preflight=success'
} else {
    Write-Output '[stock-runtime-active-preflight-test] fake-preflight=capability-skipped-orchestrator-absent'
}

Write-Output '[stock-runtime-active-preflight-test] invalid-signature=covered-by-binary-observer-tests'
Write-Output '[stock-runtime-active-preflight-test] wrong-appmanifest=covered-by-binary-observer-tests'
Write-Output '[stock-runtime-active-preflight-test] isolation-canary=covered-by-capability-tests'
Write-Output '[stock-runtime-active-preflight-test] processes-started=0'
Write-Output '[stock-runtime-active-preflight-test] result=success'
