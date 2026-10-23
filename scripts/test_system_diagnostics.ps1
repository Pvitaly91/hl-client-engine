param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',
    [switch] $KeepOnFailure,
    [string] $ScratchParent = '',
    [string] $HelperExecutable = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourceRoot = Split-Path -Parent $PSScriptRoot
if (-not $ScratchParent) { $ScratchParent = Join-Path $sourceRoot 'manual-artifacts' }
if (-not (Test-Path -LiteralPath $ScratchParent -PathType Container)) {
    throw "Offline test scratch parent is missing: $ScratchParent"
}
$helper = if ($HelperExecutable) { [IO.Path]::GetFullPath($HelperExecutable) } else { Join-Path $sourceRoot "build/bin/$Configuration/HLClientSystemDiagnostics.exe" }
if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) {
    throw "Build HLClientSystemDiagnostics first: $helper"
}

$scratch = Join-Path $ScratchParent ('hlclient-system-diagnostics-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -ErrorAction Stop | Out-Null
$checks = 0
$completed = $false

function Assert([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

function Write-Utf8([string] $Path, [string] $Content) {
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

function Start-TestProcess([int] $SleepMilliseconds, [int] $ExitCode) {
    $command = "Start-Sleep -Milliseconds $SleepMilliseconds; exit $ExitCode"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $process = Start-Process -FilePath (Join-Path $PSHOME 'pwsh.exe') -ArgumentList @('-NoProfile', '-EncodedCommand', $encoded) -PassThru -WindowStyle Hidden
    $process.Refresh()
    return $process
}

function Start-Helper([string[]] $Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $helper
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    foreach ($argument in $Arguments) { [void] $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    if ($null -eq $process) { throw 'Could not start system diagnostics helper.' }
    return $process
}

function Wait-Bounded([Diagnostics.Process] $Process, [int] $Milliseconds, [string] $Label) {
    if (-not $Process.WaitForExit($Milliseconds)) {
        $Process.Kill()
        $Process.WaitForExit()
        throw "$Label exceeded offline test timeout."
    }
    return $Process.ExitCode
}

function Assert-Package([string] $Name, [string] $Run, [string] $RunId, [int] $ProcessId, [uint64] $Creation, [bool] $HasClient) {
    $zip = Join-Path $Run 'system-diagnostics.zip'
    $manifest = Join-Path $Run 'system-diagnostics/manifest.json'
    $report = Join-Path $Run 'system-diagnostics/report.txt'
    Assert (Test-Path -LiteralPath $zip -PathType Leaf) "$Name ZIP missing."
    Assert (Test-Path -LiteralPath $manifest -PathType Leaf) "$Name manifest missing."
    Assert (Test-Path -LiteralPath $report -PathType Leaf) "$Name report missing."
    $decoded = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    Assert ($decoded.run_id -ceq $RunId) "$Name Run ID mismatch."
    Assert ($decoded.sources.temperature.status -ceq 'unsupported') "$Name must not invent sensor data."
    Assert ($decoded.sources.thermal_limit.status -ceq 'unsupported') "$Name must mark thermal limits unverified."
    Assert ($decoded.sources.wpr.trace_status -ceq 'not_started') "$Name unexpectedly claimed ETL capture."
    Assert ($decoded.time.frequency_hz -gt 0) "$Name has no monotonic clock frequency."
    if ($HasClient) {
        Assert ($decoded.process_id -eq $ProcessId) "$Name PID mismatch."
        Assert ($decoded.process_creation_filetime_100ns -eq $Creation) "$Name process creation mismatch."
    }
    Assert ((Get-Item -LiteralPath $zip).Length -gt 0) "$Name empty ZIP."
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $entryNames = @($archive.Entries | ForEach-Object { $_.FullName })
        foreach ($required in @('system-diagnostics/samples.jsonl', 'system-diagnostics/system.csv', 'system-diagnostics/processes.csv', 'system-diagnostics/manifest.json', 'system-diagnostics/report.txt')) {
            Assert ($entryNames -ccontains $required) "$Name ZIP missing $required."
        }
        if ($HasClient) { Assert ($entryNames -ccontains 'external-run-report.json') "$Name ZIP missing the game report." }
        Assert (-not ($entryNames -ccontains 'launch-config.json')) "$Name ZIP included launcher configuration."
        Assert (-not ($entryNames -ccontains 'arbitrary-private.txt')) "$Name ZIP included an arbitrary file."
        Assert ($archive.Entries.Count -le 24) "$Name ZIP has excessive entries."
    } finally { $archive.Dispose() }
    Write-Output "[system-diagnostics-test] $Name run_id=$RunId zip=$((Get-Item -LiteralPath $zip).Length)"
}

function Wait-Ready([Diagnostics.Process] $Monitor, [string] $Run, [string] $RunId) {
    $ready = Join-Path $Run 'system-helper-ready.marker'
    for ($attempt = 0; $attempt -lt 150; $attempt++) {
        if (Test-Path -LiteralPath $ready -PathType Leaf) {
            $decoded = Get-Content -LiteralPath $ready -Raw | ConvertFrom-Json
            Assert ($decoded.schema -ceq 'hlclient.system-helper-ready.v1') 'Helper ready marker schema mismatch.'
            Assert ($decoded.run_id -ceq $RunId) 'Helper ready marker Run ID mismatch.'
            return
        }
        if ($Monitor.HasExited) { throw "Helper exited before ready marker with code $($Monitor.ExitCode)." }
        Start-Sleep -Milliseconds 100
    }
    throw 'Helper did not publish ready marker before fake client launch.'
}

function Run-AwaitCase([string] $Name, [bool] $LaunchChild) {
    $runId = [guid]::NewGuid().ToString('N')
    $run = Join-Path $scratch $runId
    New-Item -ItemType Directory -Path $run -ErrorAction Stop | Out-Null
    Write-Utf8 (Join-Path $run 'launch-config.json') '{"secret":"must-not-package"}'
    Write-Utf8 (Join-Path $run 'arbitrary-private.txt') 'must-not-package'
    $monitor = Start-Helper @('--await-client', '--run-root', $run, '--run-id', $runId)
    $child = $null
    try {
        Wait-Ready $monitor $run $runId
        Assert (-not (Test-Path -LiteralPath (Join-Path $run 'process-manifest.json'))) "$Name unexpectedly started a client before ready."
        if ($LaunchChild) {
            $child = Start-TestProcess -SleepMilliseconds 1800 -ExitCode 0
            $creation = [uint64] $child.StartTime.ToUniversalTime().ToFileTimeUtc()
            $identity = @{ schema = 'hlclient.external-process-identity.v1'; run_id = $runId; process_id = $child.Id; process_creation = $creation; client_sha256 = 'offline-fixture' }
            Write-Utf8 (Join-Path $run 'process-manifest.json') (($identity | ConvertTo-Json -Compress) + "`n")
            [void] (Wait-Bounded $child 15000 "$Name fake child")
            $gameReport = @{ schema = 'hlclient.external-run.v1'; run_id = $runId; client_exit = 0; terminal_reason = $Name }
            Write-Utf8 (Join-Path $run 'external-run-report.json') (($gameReport | ConvertTo-Json -Compress) + "`n")
            $marker = @{ run_id = $runId; pid = $child.Id; creation_filetime = $creation; report_publication = $true }
        } else {
            $creation = [uint64] 0
            $marker = @{ run_id = $runId; report_publication = $false }
        }
        Write-Utf8 (Join-Path $run 'launcher-finished.marker') (($marker | ConvertTo-Json -Compress) + "`n")
        $monitorExit = Wait-Bounded $monitor 30000 "$Name helper"
        Assert ($monitorExit -eq 0) "$Name did not produce a bounded package (helper exit=$monitorExit, run=$run)."
        Assert-Package $Name $run $runId $(if ($LaunchChild) { $child.Id } else { 0 }) $creation $LaunchChild
    } finally {
        if ($null -ne $child) {
            if (-not $child.HasExited) { $child.Kill(); $child.WaitForExit() }
            $child.Dispose()
        }
        if (-not $monitor.HasExited) { $monitor.Kill(); $monitor.WaitForExit() }
        $monitor.Dispose()
    }
}

function Run-Case([string] $Name, [int] $ChildExit, [bool] $Cancel, [bool] $WrongIdentity) {
    $runId = [guid]::NewGuid().ToString('N')
    $run = Join-Path $scratch $runId
    New-Item -ItemType Directory -Path $run -ErrorAction Stop | Out-Null
    Write-Utf8 (Join-Path $run 'launch-config.json') '{"secret":"must-not-package"}'
    Write-Utf8 (Join-Path $run 'arbitrary-private.txt') 'must-not-package'
    if ($Name -ceq 'normal_close') {
        [IO.File]::WriteAllBytes((Join-Path $run 'external-diagnostics.json'), [byte[]]::new(8 * 1024 * 1024 + 1))
    }
    $child = Start-TestProcess -SleepMilliseconds $(if ($Cancel) { 5000 } else { 1800 }) -ExitCode $ChildExit
    $creation = [uint64] $child.StartTime.ToUniversalTime().ToFileTimeUtc()
    $boundPid = if ($WrongIdentity) { $child.Id + 1 } else { $child.Id }
    $identity = @{ schema = 'hlclient.external-process-identity.v1'; run_id = $runId; process_id = $boundPid; process_creation = $creation; client_sha256 = 'offline-fixture' }
    Write-Utf8 (Join-Path $run 'process-manifest.json') (($identity | ConvertTo-Json -Compress) + "`n")
    $args = @('--monitor', '--run-root', $run, '--run-id', $runId, '--pid', [string] $child.Id, '--creation-filetime', [string] $creation)
    $monitor = Start-Helper $args
    try {
        if ($WrongIdentity) {
            Assert ((Wait-Bounded $monitor 15000 "$Name helper") -eq 2) "$Name must reject mismatched PID identity."
            Assert (-not (Test-Path -LiteralPath (Join-Path $run 'system-diagnostics.zip'))) "$Name must not publish an archive."
            return
        }
        if ($Cancel) {
            Start-Sleep -Milliseconds 400
            if (-not $child.HasExited) { $child.Kill() }
        }
        [void] (Wait-Bounded $child 15000 "$Name fake child")
        $gameReport = @{ schema = 'hlclient.external-run.v1'; run_id = $runId; client_exit = $child.ExitCode; terminal_reason = $Name }
        Write-Utf8 (Join-Path $run 'external-run-report.json') (($gameReport | ConvertTo-Json -Compress) + "`n")
        $marker = @{ run_id = $runId; pid = $child.Id; creation_filetime = $creation; report_publication = $true }
        Write-Utf8 (Join-Path $run 'launcher-finished.marker') (($marker | ConvertTo-Json -Compress) + "`n")
        $monitorExit = Wait-Bounded $monitor 30000 "$Name helper"
        Assert ($monitorExit -eq 0) "$Name did not produce a partial/full package (helper exit=$monitorExit, run=$run)."
        Assert-Package $Name $run $runId $child.Id $creation $true
        if ($Name -ceq 'normal_close') {
            $decoded = Get-Content -LiteralPath (Join-Path $run 'system-diagnostics/manifest.json') -Raw | ConvertFrom-Json
            Assert ($decoded.archive.omitted -ccontains 'external-diagnostics.json:size_budget') 'Oversized source was not recorded as omitted.'
            $archive = [IO.Compression.ZipFile]::OpenRead((Join-Path $run 'system-diagnostics.zip'))
            try { Assert (-not (@($archive.Entries | ForEach-Object FullName) -ccontains 'external-diagnostics.json')) 'Oversized source entered ZIP.' }
            finally { $archive.Dispose() }
        }
    } finally {
        if (-not $child.HasExited) { $child.Kill(); $child.WaitForExit() }
        if (-not $monitor.HasExited) { $monitor.Kill(); $monitor.WaitForExit() }
        $child.Dispose()
        $monitor.Dispose()
    }
}

try {
    Run-Case 'normal_close' 0 $false $false
    Run-Case 'child_failure' 7 $false $false
    Run-Case 'launcher_cancel' 0 $true $false
    Run-Case 'pid_identity_mismatch' 0 $false $true
    Run-AwaitCase 'ready_before_client' $true
    Run-AwaitCase 'launcher_failed_before_client' $false
    Write-Output "PASS system-diagnostics offline fake-child checks=$checks live_game=0"
    $completed = $true
} finally {
    $scratchFull = [IO.Path]::GetFullPath($scratch)
    $tempFull = [IO.Path]::GetFullPath($ScratchParent).TrimEnd('\')
    if ((-not $KeepOnFailure -or $completed) -and
        $scratchFull.StartsWith($tempFull + '\', [StringComparison]::OrdinalIgnoreCase) -and
        $scratchFull -ne $tempFull -and
        [IO.Path]::GetFileName($scratchFull).StartsWith('hlclient-system-diagnostics-test-', [StringComparison]::Ordinal)) {
        Remove-Item -LiteralPath $scratchFull -Recurse -Force
    }
}
