#requires -Version 5.1

<#
.SYNOPSIS
Performs the read-only, path-free eligibility preflight for a Half-Life source.

.DESCRIPTION
The native helper performs the bounded topology, binary and appmanifest checks.
This wrapper creates no destination or artifact and starts no process other
than that helper. It does not configure networking or Windows Filtering
Platform state.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$SourceHalfLifeRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$AppManifestPath,

    [ValidateRange(1, [long]::MaxValue)]
    [long]$ExpectedAppBuild = 15961492,

    [ValidateNotNullOrEmpty()]
    [string]$EligibilityToolPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Stop-InvalidArgument {
    Write-Output '[stock-source] result=invalid_argument'
    $global:LASTEXITCODE = 2
    exit 2
}

function Resolve-EligibilityTool {
    if ($EligibilityToolPath) {
        try {
            $selected = [IO.Path]::GetFullPath($EligibilityToolPath)
        } catch {
            Stop-InvalidArgument
        }
        if (-not (Test-Path -LiteralPath $selected -PathType Leaf)) {
            Stop-InvalidArgument
        }
        return $selected
    }

    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    foreach ($candidate in @(
            (Join-Path $repositoryRoot `
                'build\bin\Debug\hlclient_stock_source_eligibility_check.exe'),
            (Join-Path $repositoryRoot `
                'build\bin\Release\hlclient_stock_source_eligibility_check.exe'),
            (Join-Path $repositoryRoot `
                'build-asan\bin\Release\hlclient_stock_source_eligibility_check.exe'))) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    Stop-InvalidArgument
}

try {
    $source = [IO.Path]::GetFullPath($SourceHalfLifeRoot)
    $manifest = [IO.Path]::GetFullPath($AppManifestPath)
} catch {
    Stop-InvalidArgument
}

$tool = Resolve-EligibilityTool
$savedErrorActionPreference = $ErrorActionPreference
try {
    # Windows PowerShell 5.1 promotes a native process' redirected stderr to a
    # terminating NativeCommandError when the caller uses Stop. Hard validator
    # failures intentionally publish their one-line result on stderr, so keep
    # that stream capturable and validate it through the same strict contract.
    $ErrorActionPreference = 'Continue'
    $nativeOutput = @(& $tool `
            --source-root $source `
            --app-manifest $manifest `
            --expected-app-build ([string]$ExpectedAppBuild) 2>&1)
    $nativeExitCode = $LASTEXITCODE
} catch {
    Write-Output '[stock-source] result=helper_launch_failed'
    $global:LASTEXITCODE = 1
    exit 1
} finally {
    $ErrorActionPreference = $savedErrorActionPreference
}

$lines = [Collections.Generic.List[string]]::new()
foreach ($item in $nativeOutput) {
    $line = [string]$item
    if ([string]::IsNullOrWhiteSpace($line) -or
        $line.Length -gt 160 -or
        $line -cnotmatch `
            '^\[stock-source\] [a-z-]+=[A-Za-z0-9_]+$') {
        Write-Output '[stock-source] result=invalid_helper_output'
        $global:LASTEXITCODE = 1
        exit 1
    }
    $lines.Add($line)
}
if ($lines.Count -lt 1 -or $lines.Count -gt 10) {
    Write-Output '[stock-source] result=invalid_helper_output'
    $global:LASTEXITCODE = 1
    exit 1
}

$records = @{}
foreach ($line in $lines) {
    if ($line -cnotmatch '^\[stock-source\] ([a-z-]+)=([A-Za-z0-9_]+)$') {
        Write-Output '[stock-source] result=invalid_helper_output'
        $global:LASTEXITCODE = 1
        exit 1
    }
    if ($records.ContainsKey($Matches[1])) {
        Write-Output '[stock-source] result=invalid_helper_output'
        $global:LASTEXITCODE = 1
        exit 1
    }
    $records[$Matches[1]] = $Matches[2]
}

if ($records.Count -eq 1 -and $records.ContainsKey('result')) {
    $hardExitOneResults = @(
        'topology_observation_failed',
        'topology_incomplete',
        'topology_unsafe',
        'app_profile_invalid',
        'source_changed')
    $hardResult = $records['result']
    $hardResultValid =
        ($nativeExitCode -eq 1 -and
            $hardResult -cin $hardExitOneResults) -or
        ($nativeExitCode -eq 2 -and
            $hardResult -ceq 'invalid_argument')
    if (-not $hardResultValid) {
        Write-Output '[stock-source] result=invalid_helper_output'
        $global:LASTEXITCODE = 1
        exit 1
    }
} else {
    $required = @(
        'topology',
        'escaped-targets',
        'dangling-targets',
        'unsupported-tags',
        'ads',
        'client-profile',
        'server-profile',
        'app-profile',
        'research-copy-eligible',
        'result')
    if ($records.Count -ne $required.Count) {
        Write-Output '[stock-source] result=invalid_helper_output'
        $global:LASTEXITCODE = 1
        exit 1
    }
    foreach ($name in $required) {
        if (-not $records.ContainsKey($name)) {
            Write-Output '[stock-source] result=invalid_helper_output'
            $global:LASTEXITCODE = 1
            exit 1
        }
    }
    if ($records['topology'] -cnotin @('safe', 'unsafe') -or
        $records['client-profile'] -cnotin @('valid', 'invalid') -or
        $records['server-profile'] -cnotin @('valid', 'invalid') -or
        $records['app-profile'] -cnotin @('valid', 'invalid') -or
        $records['research-copy-eligible'] -cnotin @('true', 'false')) {
        Write-Output '[stock-source] result=invalid_helper_output'
        $global:LASTEXITCODE = 1
        exit 1
    }
    $counts = @{}
    foreach ($name in @(
            'escaped-targets', 'dangling-targets', 'unsupported-tags', 'ads')) {
        [uint64]$count = 0
        if ($records[$name] -cnotmatch '^(?:0|[1-9][0-9]*)$' -or
            -not [uint64]::TryParse(
                $records[$name],
                [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture,
                [ref]$count) -or $count -gt 200000) {
            Write-Output '[stock-source] result=invalid_helper_output'
            $global:LASTEXITCODE = 1
            exit 1
        }
        $counts[$name] = $count
    }

    if ($nativeExitCode -eq 0) {
        if ($records['topology'] -cne 'safe' -or
            $records['escaped-targets'] -cne '0' -or
            $records['dangling-targets'] -cne '0' -or
            $records['unsupported-tags'] -cne '0' -or
            $records['ads'] -cne '0' -or
            $records['client-profile'] -cne 'valid' -or
            $records['server-profile'] -cne 'valid' -or
            $records['app-profile'] -cne 'valid' -or
            $records['research-copy-eligible'] -cne 'true' -or
            $records['result'] -cne 'success') {
            Write-Output '[stock-source] result=invalid_helper_output'
            $global:LASTEXITCODE = 1
            exit 1
        }
    } elseif ($nativeExitCode -eq 1) {
        $summaryResult = $records['result']
        $allCountsZero =
            $counts['escaped-targets'] -eq 0 -and
            $counts['dangling-targets'] -eq 0 -and
            $counts['unsupported-tags'] -eq 0 -and
            $counts['ads'] -eq 0
        $allProfilesInvalid =
            $records['client-profile'] -ceq 'invalid' -and
            $records['server-profile'] -ceq 'invalid' -and
            $records['app-profile'] -ceq 'invalid'
        $summaryConsistent = $false
        if ($records['research-copy-eligible'] -ceq 'false') {
            if ($summaryResult -ceq 'topology_incomplete') {
                $summaryConsistent =
                    $records['topology'] -ceq 'unsafe' -and
                    $allProfilesInvalid
            } elseif ($summaryResult -ceq 'dangling_target') {
                $summaryConsistent =
                    $records['topology'] -ceq 'unsafe' -and
                    $counts['dangling-targets'] -gt 0 -and
                    $allProfilesInvalid
            } elseif ($summaryResult -ceq 'unsupported_reparse_tag') {
                $summaryConsistent =
                    $records['topology'] -ceq 'unsafe' -and
                    $counts['dangling-targets'] -eq 0 -and
                    $counts['unsupported-tags'] -gt 0 -and
                    $allProfilesInvalid
            } elseif ($summaryResult -ceq 'escaped_target') {
                $summaryConsistent =
                    $records['topology'] -ceq 'unsafe' -and
                    $counts['dangling-targets'] -eq 0 -and
                    $counts['unsupported-tags'] -eq 0 -and
                    $counts['escaped-targets'] -gt 0 -and
                    $allProfilesInvalid
            } elseif ($summaryResult -ceq 'alternate_data_stream') {
                $summaryConsistent =
                    $records['topology'] -ceq 'unsafe' -and
                    $counts['dangling-targets'] -eq 0 -and
                    $counts['unsupported-tags'] -eq 0 -and
                    $counts['escaped-targets'] -eq 0 -and
                    $counts['ads'] -gt 0 -and
                    $allProfilesInvalid
            } elseif ($summaryResult -ceq 'topology_unsafe') {
                $summaryConsistent =
                    $records['topology'] -ceq 'unsafe' -and
                    $allCountsZero -and
                    $allProfilesInvalid
            } elseif ($summaryResult -ceq 'source_already_prepared') {
                $summaryConsistent =
                    $records['topology'] -ceq 'safe' -and
                    $allCountsZero
            } elseif ($summaryResult -ceq 'client_profile_invalid') {
                $summaryConsistent =
                    $records['topology'] -ceq 'safe' -and
                    $allCountsZero -and
                    $records['client-profile'] -ceq 'invalid'
            } elseif ($summaryResult -ceq 'server_profile_invalid') {
                $summaryConsistent =
                    $records['topology'] -ceq 'safe' -and
                    $allCountsZero -and
                    $records['client-profile'] -ceq 'valid' -and
                    $records['server-profile'] -ceq 'invalid'
            } elseif ($summaryResult -ceq 'app_profile_invalid') {
                $summaryConsistent =
                    $records['topology'] -ceq 'safe' -and
                    $allCountsZero -and
                    $records['client-profile'] -ceq 'valid' -and
                    $records['server-profile'] -ceq 'valid' -and
                    $records['app-profile'] -ceq 'invalid'
            }
        }
        if (-not $summaryConsistent) {
            Write-Output '[stock-source] result=invalid_helper_output'
            $global:LASTEXITCODE = 1
            exit 1
        }
    } else {
        Write-Output '[stock-source] result=invalid_helper_output'
        $global:LASTEXITCODE = 1
        exit 1
    }
}

foreach ($line in $lines) {
    Write-Output $line
}
$global:LASTEXITCODE = $nativeExitCode
exit $nativeExitCode
