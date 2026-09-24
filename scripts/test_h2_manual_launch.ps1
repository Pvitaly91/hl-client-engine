#requires -Version 7.0
[CmdletBinding()]
param(
    [switch]$FixtureChild,
    [string]$FixtureRoot,
    [ValidateSet('reference', 'off')][string]$Mode = 'reference',
    [ValidateSet('normal', 'silent', 'throw', 'ambiguous')][string]$Case = 'normal'
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
if ($FixtureChild) {
    $configuration = Get-H2ManualConfiguration -Root $FixtureRoot -Mode $Mode -ResearchRoot (Join-Path $FixtureRoot 'research game') -SteamRoot (Join-Path $FixtureRoot 'Steam apps')
    $fakeRunner = Join-Path $FixtureRoot 'scripts\managed runner fixture.ps1'
    if ((Get-Content -LiteralPath $fakeRunner -TotalCount 1) -cne
        '# No-stock fixture: only validate arguments and publish a synthetic report.') {
        throw 'Fixture guard rejected runner; actual runner must never run in this test.'
    }
    $env:HLCLIENT_MANUAL_TEST_CASE = $Case
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
    @{Mode='reference'; Case='normal'; Exit=0},
    @{Mode='off'; Case='normal'; Exit=23},
    @{Mode='reference'; Case='silent'; Exit=9},
    @{Mode='reference'; Case='throw'; Exit=1},
    @{Mode='reference'; Case='ambiguous'; Exit=0}
)) {
    $output = @(& $pwsh -NoProfile -File $PSCommandPath -FixtureChild -FixtureRoot $scratch `
        -Mode $scenario.Mode -Case $scenario.Case 2>&1)
    $actualExit = $LASTEXITCODE
    $text = $output -join "`n"
    Assert-Test ($actualExit -eq $scenario.Exit) "child exit $actualExit instead of $($scenario.Exit)"
    Assert-Test ($text.Contains('fixture stderr visible')) 'stderr lost'
    Assert-Test ($text.Contains("exit code: $actualExit")) 'actual exit summary missing'
    if ($scenario.Case -eq 'normal') {
        Assert-Test ($text.Contains("fixture arguments verified: prediction=$($scenario.Mode)")) 'arguments/space paths lost'
        Assert-Test ($text.Contains('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\functional-smoke-wrapper.json')) 'exact report missing'
        Assert-Test (-not $text.Contains('bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\functional-smoke-wrapper.json')) 'unrelated report chosen'
    } else {
        Assert-Test ($text.Contains('Run report unavailable:')) 'missing/ambiguous ID did not fail closed'
    }
    Write-Host "PASS fake runner: prediction=$($scenario.Mode), case=$($scenario.Case), exit=$actualExit"
}

# Actual CheckOnly from a different cwd: parses contract/files, never invokes it.
Push-Location -LiteralPath $scratch
try {
    foreach ($extra in @(@(), @('-Prediction', 'off'))) {
        $output = @(& $pwsh -NoProfile -File $launcher -CheckOnly -SourceOnly @extra 2>&1)
        Assert-Test ($LASTEXITCODE -eq 0) 'CheckOnly failed'
        Assert-Test (($output -join "`n").Contains('CheckOnly passed:')) 'CheckOnly did not reach validation'
    }
} finally { Pop-Location }
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
Write-Host 'PASS syntax, CheckOnly default/off from another cwd, space paths, all runner arguments, stdout/stderr, exact reports, exit 0/23/9/1, missing-file/contract rejection. Live sessions=0.'
