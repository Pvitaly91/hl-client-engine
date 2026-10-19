#requires -Version 7.0
<#
.SYNOPSIS
Bounded normal-client idle presentation without assets, sockets or user config.
.DESCRIPTION
The existing frame hook selects the production client's isolated offline idle
policy. No fixture server, Steam, installed assets or settings writes are used.
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$ToolPath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$process = [Diagnostics.Process]::new()
$started = $false
try {
    $tool = [IO.Path]::GetFullPath($ToolPath)
    $spec = [Diagnostics.ProcessStartInfo]::new($tool)
    $spec.UseShellExecute = $false
    $spec.CreateNoWindow = $true
    $spec.RedirectStandardOutput = $true
    $spec.RedirectStandardError = $true
    $spec.WorkingDirectory = Split-Path -Parent $tool
    $spec.ArgumentList.Add('--open-client')
    $spec.Environment['HLCLIENT_SMOKE_TEST_FRAMES'] = '2'
    $process.StartInfo = $spec
    if (-not $process.Start()) { throw 'Owned offline idle client did not start' }
    $started = $true
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(30000)) { throw 'Owned offline idle client exceeded its 30 second bound' }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) { throw "Offline idle client failed: exit=$($process.ExitCode); $stderr" }
    $output = $stdout + "`n" + $stderr
    if ($output -notmatch '\[console-application\] first_idle_frame_presented=true start_idle=true') {
        throw 'Normal application did not confirm its first idle presentation'
    }
    if ($output -match '\[net\]|getchallenge|live_visual_startup|steam_api_initializ|provider_begin|\[session-close\]|Half-Life game directory validated|Asset pipeline initialized|console_background=') {
        throw 'Offline idle verification unexpectedly entered networking, authentication, assets or a gameplay attempt'
    }
    if ($output -match '\[(fatal|error)\]') { throw "Offline idle client reported an error: $output" }
    Write-Host 'PASS normal --open-client: first fallback frame, bounded idle exit0; no user config, asset import, socket, Steam or gameplay attempt. Live sessions=0.'
} finally {
    if ($started -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
