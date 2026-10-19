#requires -Version 7.0
# A bound, silent project-owned UDP peer: no server, game assets, Steam API
# library, authentication response, WFP or managed session is started.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$ToolPath,
      [Parameter(Mandatory)][string]$FixtureRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$scratch = [IO.Path]::GetFullPath((Join-Path $FixtureRoot ([guid]::NewGuid().ToString('N'))))
[void][IO.Directory]::CreateDirectory((Join-Path $scratch 'valve'))
$peer = [Net.Sockets.UdpClient]::new([Net.IPEndPoint]::new([Net.IPAddress]::Loopback, 0))
$process = [Diagnostics.Process]::new()
$stdout = ''
$stderr = ''
try {
    $port = ([Net.IPEndPoint]$peer.Client.LocalEndPoint).Port
    $spec = [Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($ToolPath))
    $spec.UseShellExecute = $false
    $spec.RedirectStandardOutput = $true
    $spec.RedirectStandardError = $true
    $spec.WorkingDirectory = Split-Path -Parent ([IO.Path]::GetFullPath($ToolPath))
    # The bounded production test hook disables real user startup config. This
    # deliberately high cap leaves the actual owning challenge deadline in
    # control; it is not an artificial wait or an early timeout substitute.
    $spec.Environment['HLCLIENT_SMOKE_TEST_FRAMES'] = '1000000'
    foreach ($argument in @('--renderer','opengl','--connect',"127.0.0.1:$port",
        '--stop-after','live-visual-control','--live-input','keyboard-mouse',
        # A finite managed attempt preserves this fixture's terminal exit2
        # contract. Unlimited interactive attempts now correctly remain at the
        # E33 recoverable error console instead of closing the application.
        '--prediction','reference','--live-session-seconds','30','--basedir',$scratch,
        '--game','valve','--auth-provider','steam','--steam-api-runtime',
        (Join-Path $scratch 'never-loaded-steam-api.dll'))) {
        $spec.ArgumentList.Add($argument)
    }
    $process.StartInfo = $spec
    if (-not $process.Start()) { throw 'Owned offline application did not start' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(30000)) { throw 'Owned offline application exceeded 30 second bound' }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 2) { throw "Missing peer must fail honestly: exit=$($process.ExitCode)" }
    if ($stdout -notmatch 'live_visual_startup phase=prepared handshake=idle') {
        throw 'Handshake was not idle after normal SDL/OpenGL/audio preparation'
    }
    if ($stdout -notmatch 'live_application_outcome result=error primary_error=timed_out error_domain=handshake failure_stage=handshake') {
        throw 'Owning challenge timeout was hidden by a prediction/coverage verdict'
    }
    if ($stdout -notmatch 'successful_presentations=(\d+)' -or [int]$Matches[1] -le 1) {
        throw 'Normal application stopped at the first frame instead of pumping the challenge deadline'
    }
    if ($stdout -match 'steam_api_initializing|steam_api_initialized|provider_begin' -or
        $stderr -match 'steam_api_initializing|steam_api_initialized|provider_begin') {
        throw 'Silent fixture reached authentication unexpectedly'
    }
    if ($peer.Client.Available -eq 0) { throw 'No actual challenge request reached the owned peer' }
    $from = [Net.IPEndPoint]::new([Net.IPAddress]::Any,0)
    $request = $peer.Receive([ref]$from)
    if ([Text.Encoding]::ASCII.GetString($request) -notmatch 'getchallenge steam') {
        throw 'Unexpected request to silent peer'
    }
    Write-Host 'PASS normal application: preparation before challenge, RX loop survives first frame, typed timeout, exit2 preserved; no game/Steam initialization/live sessions.'
} catch {
    # Preserve bounded child diagnostics in CTest output when an assertion
    # fails; an early application failure must not hide behind its first
    # missing milestone. This owned fixture never reaches authentication.
    foreach ($capture in @(@('stdout',$stdout),@('stderr',$stderr))) {
        $bounded = [string]$capture[1]
        if ($bounded.Length -gt 16384) { $bounded = $bounded.Substring($bounded.Length - 16384) }
        [Console]::Error.WriteLine("[startup-fixture:$($capture[0])] $bounded")
    }
    throw
} finally {
    if ($process.Id -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
    $peer.Dispose()
    # Delete only the fresh GUID fixture under the exact caller-provided root.
    $allowed = [IO.Path]::GetFullPath($FixtureRoot).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if (-not $scratch.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture cleanup' }
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
