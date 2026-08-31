#requires -Version 5.1

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$EligibilityToolPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$wrapper = Join-Path $PSScriptRoot 'validate_stock_runtime_candidate_source.ps1'
if (-not (Test-Path -LiteralPath $wrapper -PathType Leaf) -or
    -not (Test-Path -LiteralPath $EligibilityToolPath -PathType Leaf)) {
    throw 'Source-eligibility test prerequisites are unavailable.'
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$temporaryBase = [IO.Path]::GetFullPath((
    Join-Path $repositoryRoot 'build\test-artifacts'
)).TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar)
$fixture = [IO.Path]::GetFullPath((Join-Path $temporaryBase (
    'hlclient-source-eligibility-' + [Guid]::NewGuid().ToString('N')
)))
$temporaryPrefix = $temporaryBase + [IO.Path]::DirectorySeparatorChar
if (-not $fixture.StartsWith(
        $temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    -not ([IO.Path]::GetFileName($fixture)).StartsWith(
        'hlclient-source-eligibility-', [StringComparison]::Ordinal)) {
    throw 'Refusing to create an eligibility fixture outside the test root.'
}
[IO.Directory]::CreateDirectory($fixture) | Out-Null

$currentPowerShell = if ($PSVersionTable.PSEdition -ceq 'Core') {
    Join-Path $PSHOME 'pwsh.exe'
} else {
    Join-Path $PSHOME 'powershell.exe'
}
if (-not (Test-Path -LiteralPath $currentPowerShell -PathType Leaf)) {
    throw 'The current PowerShell executable is unavailable.'
}

function Write-FakeHelper {
    param(
        [string]$Path,
        [string[]]$Lines,
        [int]$ExitCode
    )
    $body = "@echo off`r`n"
    foreach ($line in $Lines) {
        $body += "echo $line`r`n"
    }
    $body += "exit /b $ExitCode`r`n"
    [IO.File]::WriteAllText($Path, $body, [Text.Encoding]::ASCII)
}

function Invoke-Wrapper {
    param(
        [string]$Tool,
        [string]$Source,
        [string]$Manifest
    )
    $lines = @(& $currentPowerShell `
            -NoLogo `
            -NoProfile `
            -NonInteractive `
            -ExecutionPolicy Bypass `
            -File $wrapper `
            -SourceHalfLifeRoot $Source `
            -AppManifestPath $Manifest `
            -ExpectedAppBuild 15961492 `
            -EligibilityToolPath $Tool 2>&1)
    return [pscustomobject]@{
        Lines = @($lines | ForEach-Object { [string]$_ })
        ExitCode = $LASTEXITCODE
    }
}

function Assert-Contains {
    param([string[]]$Lines, [string]$Expected)
    if (@($Lines | Where-Object { $_ -ceq $Expected }).Count -ne 1) {
        throw "Missing exact source-eligibility record: $Expected"
    }
}

function Assert-RejectedHelperContract {
    param(
        [string]$Name,
        [string[]]$Lines,
        [int]$NativeExitCode,
        [string]$Source,
        [string]$Manifest
    )
    $helper = Join-Path $fixture ($Name + '.cmd')
    Write-FakeHelper $helper $Lines $NativeExitCode
    $actual = Invoke-Wrapper $helper $Source $Manifest
    if ($actual.ExitCode -ne 1) {
        throw "The wrapper accepted invalid helper contract '$Name'."
    }
    Assert-Contains $actual.Lines '[stock-source] result=invalid_helper_output'
}

try {
    $placeholderSource = Join-Path $fixture 'source'
    $placeholderManifest = Join-Path $fixture 'appmanifest_70.acf'
    [IO.Directory]::CreateDirectory($placeholderSource) | Out-Null
    [IO.File]::WriteAllText($placeholderManifest, 'fixture')

    $eligibleHelper = Join-Path $fixture 'eligible.cmd'
    Write-FakeHelper $eligibleHelper @(
        '[stock-source] topology=safe',
        '[stock-source] escaped-targets=0',
        '[stock-source] dangling-targets=0',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=valid',
        '[stock-source] server-profile=valid',
        '[stock-source] app-profile=valid',
        '[stock-source] research-copy-eligible=true',
        '[stock-source] result=success') 0
    $eligible = Invoke-Wrapper `
        $eligibleHelper $placeholderSource $placeholderManifest
    if ($eligible.ExitCode -ne 0) {
        throw 'The eligible wrapper contract did not return exit zero.'
    }
    Assert-Contains $eligible.Lines `
        '[stock-source] research-copy-eligible=true'
    Assert-Contains $eligible.Lines '[stock-source] result=success'

    $danglingHelper = Join-Path $fixture 'dangling.cmd'
    Write-FakeHelper $danglingHelper @(
        '[stock-source] topology=unsafe',
        '[stock-source] escaped-targets=1',
        '[stock-source] dangling-targets=1',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=invalid',
        '[stock-source] server-profile=invalid',
        '[stock-source] app-profile=invalid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=dangling_target') 1
    $dangling = Invoke-Wrapper `
        $danglingHelper $placeholderSource $placeholderManifest
    if ($dangling.ExitCode -ne 1) {
        throw 'The ineligible wrapper contract did not preserve exit one.'
    }
    Assert-Contains $dangling.Lines '[stock-source] dangling-targets=1'
    Assert-Contains $dangling.Lines `
        '[stock-source] research-copy-eligible=false'

    $incompleteHelper = Join-Path $fixture 'incomplete-summary.cmd'
    Write-FakeHelper $incompleteHelper @(
        '[stock-source] topology=unsafe',
        '[stock-source] escaped-targets=0',
        '[stock-source] dangling-targets=0',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=invalid',
        '[stock-source] server-profile=invalid',
        '[stock-source] app-profile=invalid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=topology_incomplete') 1
    $incomplete = Invoke-Wrapper `
        $incompleteHelper $placeholderSource $placeholderManifest
    if ($incomplete.ExitCode -ne 1) {
        throw 'The wrapper rejected a canonical incomplete summary.'
    }
    Assert-Contains $incomplete.Lines `
        '[stock-source] result=topology_incomplete'

    $boundedCounterHelper = Join-Path $fixture 'bounded-counter.cmd'
    Write-FakeHelper $boundedCounterHelper @(
        '[stock-source] topology=unsafe',
        '[stock-source] escaped-targets=200000',
        '[stock-source] dangling-targets=0',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=invalid',
        '[stock-source] server-profile=invalid',
        '[stock-source] app-profile=invalid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=escaped_target') 1
    $boundedCounter = Invoke-Wrapper `
        $boundedCounterHelper $placeholderSource $placeholderManifest
    if ($boundedCounter.ExitCode -ne 1) {
        throw 'The wrapper rejected the inclusive counter bound.'
    }
    Assert-Contains $boundedCounter.Lines `
        '[stock-source] escaped-targets=200000'

    $hardDiagnosticHelper = Join-Path $fixture 'hard-diagnostic.cmd'
    Write-FakeHelper $hardDiagnosticHelper @(
        '[stock-source] result=topology_observation_failed') 1
    $hardDiagnostic = Invoke-Wrapper `
        $hardDiagnosticHelper $placeholderSource $placeholderManifest
    if ($hardDiagnostic.ExitCode -ne 1) {
        throw 'The wrapper rejected a canonical hard diagnostic.'
    }
    Assert-Contains $hardDiagnostic.Lines `
        '[stock-source] result=topology_observation_failed'

    $invalidArgumentHelper = Join-Path $fixture 'invalid-argument.cmd'
    Write-FakeHelper $invalidArgumentHelper @(
        '[stock-source] result=invalid_argument') 2
    $invalidArgument = Invoke-Wrapper `
        $invalidArgumentHelper $placeholderSource $placeholderManifest
    if ($invalidArgument.ExitCode -ne 2) {
        throw 'The wrapper rejected the canonical invalid-argument diagnostic.'
    }
    Assert-Contains $invalidArgument.Lines '[stock-source] result=invalid_argument'

    Assert-RejectedHelperContract `
        'one-line-success-exit-one' `
        @('[stock-source] result=success') `
        1 $placeholderSource $placeholderManifest
    Assert-RejectedHelperContract `
        'unknown-hard-status' `
        @('[stock-source] result=unknown_status') `
        1 $placeholderSource $placeholderManifest
    Assert-RejectedHelperContract `
        'invalid-argument-wrong-exit' `
        @('[stock-source] result=invalid_argument') `
        1 $placeholderSource $placeholderManifest
    Assert-RejectedHelperContract `
        'hard-diagnostic-wrong-exit' `
        @('[stock-source] result=topology_observation_failed') `
        2 $placeholderSource $placeholderManifest

    $nonCanonicalCounter = @(
        '[stock-source] topology=unsafe',
        '[stock-source] escaped-targets=01',
        '[stock-source] dangling-targets=1',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=invalid',
        '[stock-source] server-profile=invalid',
        '[stock-source] app-profile=invalid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=dangling_target')
    Assert-RejectedHelperContract `
        'leading-zero-counter' $nonCanonicalCounter `
        1 $placeholderSource $placeholderManifest

    $oversizedCounter = @(
        '[stock-source] topology=unsafe',
        '[stock-source] escaped-targets=200001',
        '[stock-source] dangling-targets=1',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=invalid',
        '[stock-source] server-profile=invalid',
        '[stock-source] app-profile=invalid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=dangling_target')
    Assert-RejectedHelperContract `
        'oversized-counter' $oversizedCounter `
        1 $placeholderSource $placeholderManifest

    $contradictoryDangling = @(
        '[stock-source] topology=unsafe',
        '[stock-source] escaped-targets=0',
        '[stock-source] dangling-targets=0',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=invalid',
        '[stock-source] server-profile=invalid',
        '[stock-source] app-profile=invalid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=dangling_target')
    Assert-RejectedHelperContract `
        'contradictory-dangling-status' $contradictoryDangling `
        1 $placeholderSource $placeholderManifest

    $contradictorySkippedProfile = @(
        '[stock-source] topology=unsafe',
        '[stock-source] escaped-targets=1',
        '[stock-source] dangling-targets=1',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=valid',
        '[stock-source] server-profile=invalid',
        '[stock-source] app-profile=invalid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=dangling_target')
    Assert-RejectedHelperContract `
        'profile-observed-after-topology-rejection' `
        $contradictorySkippedProfile `
        1 $placeholderSource $placeholderManifest

    $contradictoryTopology = @(
        '[stock-source] topology=unsafe',
        '[stock-source] escaped-targets=1',
        '[stock-source] dangling-targets=0',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=invalid',
        '[stock-source] server-profile=invalid',
        '[stock-source] app-profile=invalid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=topology_unsafe')
    Assert-RejectedHelperContract `
        'contradictory-topology-priority' $contradictoryTopology `
        1 $placeholderSource $placeholderManifest

    $contradictoryProfile = @(
        '[stock-source] topology=safe',
        '[stock-source] escaped-targets=0',
        '[stock-source] dangling-targets=0',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=invalid',
        '[stock-source] server-profile=valid',
        '[stock-source] app-profile=valid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=server_profile_invalid')
    Assert-RejectedHelperContract `
        'contradictory-profile-precedence' $contradictoryProfile `
        1 $placeholderSource $placeholderManifest

    $unknownSummaryStatus = @(
        '[stock-source] topology=unsafe',
        '[stock-source] escaped-targets=0',
        '[stock-source] dangling-targets=0',
        '[stock-source] unsupported-tags=0',
        '[stock-source] ads=0',
        '[stock-source] client-profile=invalid',
        '[stock-source] server-profile=invalid',
        '[stock-source] app-profile=invalid',
        '[stock-source] research-copy-eligible=false',
        '[stock-source] result=unknown_status')
    Assert-RejectedHelperContract `
        'unknown-summary-status' $unknownSummaryStatus `
        1 $placeholderSource $placeholderManifest

    $leakyHelper = Join-Path $fixture 'leaky.cmd'
    Write-FakeHelper $leakyHelper @(
        '[stock-source] topology=safe',
        ('private-path=' + $fixture)) 0
    $leaky = Invoke-Wrapper `
        $leakyHelper $placeholderSource $placeholderManifest
    if ($leaky.ExitCode -ne 1) {
        throw 'The wrapper accepted non-contract/private helper output.'
    }
    Assert-Contains $leaky.Lines '[stock-source] result=invalid_helper_output'
    if (($leaky.Lines -join "`n").Contains($fixture)) {
        throw 'The wrapper republished a private helper path.'
    }

    $actualSteamApps = Join-Path $fixture 'steamapps'
    $actualSource = Join-Path $actualSteamApps 'common\Half-Life'
    [IO.Directory]::CreateDirectory((Join-Path $actualSource 'valve')) |
        Out-Null
    [IO.File]::WriteAllText((Join-Path $actualSource 'hl.exe'), 'unsigned')
    [IO.File]::WriteAllText((Join-Path $actualSource 'hlds.exe'), 'unsigned')
    $actualManifest = Join-Path $actualSteamApps 'appmanifest_70.acf'
    [IO.File]::WriteAllText(
        $actualManifest,
        '"AppState"' + "`n{" + "`n" +
            '"appid" "70"' + "`n" +
            '"buildid" "15961492"' + "`n}" + "`n")
    $beforeClient = Get-FileHash -Algorithm SHA256 -LiteralPath (
        Join-Path $actualSource 'hl.exe')
    $beforeServer = Get-FileHash -Algorithm SHA256 -LiteralPath (
        Join-Path $actualSource 'hlds.exe')
    $actual = Invoke-Wrapper `
        ([IO.Path]::GetFullPath($EligibilityToolPath)) `
        $actualSource $actualManifest
    if ($actual.ExitCode -ne 1) {
        throw 'An unsigned candidate source was not rejected.'
    }
    Assert-Contains $actual.Lines '[stock-source] topology=safe'
    Assert-Contains $actual.Lines '[stock-source] client-profile=invalid'
    Assert-Contains $actual.Lines `
        '[stock-source] research-copy-eligible=false'

    $unrelatedManifest = Join-Path $fixture 'unrelated\appmanifest_70.acf'
    [IO.Directory]::CreateDirectory(
        [IO.Path]::GetDirectoryName($unrelatedManifest)) | Out-Null
    [IO.File]::WriteAllText(
        $unrelatedManifest,
        '"AppState"' + "`n{" + "`n" +
            '"appid" "70"' + "`n" +
            '"buildid" "15961492"' + "`n}" + "`n")
    $unrelated = Invoke-Wrapper `
        ([IO.Path]::GetFullPath($EligibilityToolPath)) `
        $actualSource $unrelatedManifest
    if ($unrelated.ExitCode -ne 1) {
        throw 'An unrelated app manifest was not rejected.'
    }
    Assert-Contains $unrelated.Lines `
        '[stock-source] result=app_profile_invalid'
    $afterClient = Get-FileHash -Algorithm SHA256 -LiteralPath (
        Join-Path $actualSource 'hl.exe')
    $afterServer = Get-FileHash -Algorithm SHA256 -LiteralPath (
        Join-Path $actualSource 'hlds.exe')
    if ($beforeClient.Hash -cne $afterClient.Hash -or
        $beforeServer.Hash -cne $afterServer.Hash) {
        throw 'Candidate validation modified a source launcher.'
    }
    if (Test-Path -LiteralPath (Join-Path $actualSource 'research-copy')) {
        throw 'Candidate validation created a destination.'
    }

    [IO.File]::WriteAllText(
        (Join-Path $actualSource '.hlclient-research-isolated'),
        'HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1')
    $prepared = Invoke-Wrapper `
        ([IO.Path]::GetFullPath($EligibilityToolPath)) `
        $actualSource $actualManifest
    if ($prepared.ExitCode -ne 1) {
        throw 'An already prepared source was not rejected.'
    }
    Assert-Contains $prepared.Lines `
        '[stock-source] result=source_already_prepared'

    Write-Output '[stock-source-test] eligible-contract=passed'
    Write-Output '[stock-source-test] ineligible-contract=passed'
    Write-Output '[stock-source-test] strict-status-contract=passed'
    Write-Output '[stock-source-test] canonical-counter-contract=passed'
    Write-Output '[stock-source-test] private-output-rejection=passed'
    Write-Output '[stock-source-test] manifest-binding=passed'
    Write-Output '[stock-source-test] read-only-native-check=passed'
    Write-Output '[stock-source-test] result=success'
} finally {
    if (Test-Path -LiteralPath $fixture) {
        $cleanupTarget = [IO.Path]::GetFullPath($fixture)
        if ($cleanupTarget -cne $fixture -or
            -not $cleanupTarget.StartsWith(
                $temporaryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not ([IO.Path]::GetFileName($cleanupTarget)).StartsWith(
                'hlclient-source-eligibility-',
                [StringComparison]::Ordinal)) {
            throw 'Refusing unsafe source-eligibility fixture cleanup.'
        }
        [IO.Directory]::Delete($cleanupTarget, $true)
    }
}

$global:LASTEXITCODE = 0
