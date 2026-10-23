# Separate memory-only offline fixture run. Never use its CPU/GPU times as the
# clean benchmark: the parent polls OS process counters while this child runs.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BaselineExe,
    [Parameter(Mandatory)][string]$CandidateExe,
    [Parameter(Mandatory)][string]$OutputRoot,
    [ValidateRange(1, 6)][int]$Repeats=2,
    [ValidateRange(20, 500)][int]$PollMilliseconds=50
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell 7 required.'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($OutputRoot)
if(-not $out.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Generated evidence must stay inside the repository.'
}
$bins=[ordered]@{B0=[IO.Path]::GetFullPath($BaselineExe);B1=[IO.Path]::GetFullPath($CandidateExe)}
foreach($path in $bins.Values){
    if([IO.Path]::GetFileName($path) -in @('hlclient.exe','HLClientLauncher.exe','hl.exe','hlds.exe')) {
        throw 'Game/launcher/server executables are forbidden.'
    }
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing frozen fixture: $path"}
}
function Assert-HostCompilerIdle {
    try {
        $compilers=@(Get-CimInstance -ClassName Win32_Process -Filter "Name='MSBuild.exe' OR Name='cl.exe' OR Name='link.exe' OR Name='ninja.exe' OR Name='clang.exe' OR Name='clang++.exe'" -ErrorAction Stop)
    } catch {throw 'Host compiler inventory unavailable. Invoke on the host after build owners confirm completion; do not infer an idle host from sandbox Get-Process.'}
    if($compilers.Count -gt 0){
        $names=($compilers | ForEach-Object {"$($_.Name):$($_.ProcessId)"}) -join ', '
        throw "Host compiler is active ($names); no process was terminated."
    }
}
Assert-HostCompilerIdle
$dir=Join-Path $out ('memory-only-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
New-Item -ItemType Directory -Path $dir | Out-Null
$manifest=[ordered]@{
    schema='hlc-9de-memory-only-v1';status='running';created_utc=[DateTime]::UtcNow.ToString('o')
    hidden=$true;swap=$false;live_game_sessions=0;performance_acceptance=$false;GPU_memory='not_measured'
    measurement_scope='OS cumulative process peak counters observed through the last successful live sample; the unsampled final interval may be missed. Not guaranteed full-lifetime peak.'
    working_set_counter='System.Diagnostics.Process.PeakWorkingSet64'
    paged_memory_counter='System.Diagnostics.Process.PeakPagedMemorySize64'
    poll_period_ms=$PollMilliseconds;requested_msaa=8;view='9de_primary_fixed';timing_queries='off'
    profiles=@('none','one','many');repeats=$Repeats
    runner_sha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash
    bins=[ordered]@{};runs=[Collections.Generic.List[object]]::new()
}
foreach($key in $bins.Keys){$manifest.bins[$key]=[ordered]@{
    path=$bins[$key];sha256=(Get-FileHash -LiteralPath $bins[$key]).Hash;bytes=(Get-Item -LiteralPath $bins[$key]).Length}}
function Save-Manifest {$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $dir 'manifest.json') -Encoding utf8}
Save-Manifest
try {
    for($repeat=1;$repeat -le $Repeats;++$repeat){foreach($profile in $manifest.profiles){
        $order=@('B0','B1');if($repeat%2 -eq 0){[array]::Reverse($order)}
        foreach($variant in $order){
            Assert-HostCompilerIdle
            if((Get-FileHash -LiteralPath $bins[$variant]).Hash -ne $manifest.bins[$variant].sha256){throw 'Frozen fixture changed.'}
            $stem='{0:D3}-{1}-r{2}-aa8-primary-{3}' -f ($manifest.runs.Count+1),$variant,$repeat,$profile
            $stdout=Join-Path $dir ($stem+'.log');$stderr=Join-Path $dir ($stem+'.err')
            $vars=@{HLCLIENT_LOCAL_GAME_ROOT='D:\DEV\HLCLIENT-RESEARCH\Half-Life';HLCLIENT_71B97_VISIBILITY='all'
                HLCLIENT_22AD_VIEW='9de_primary_fixed';HLCLIENT_22AD_MSAA='8';HLCLIENT_22AD_RAW_W='1'
                HLCLIENT_9DE_LIGHT_PROFILE=$profile;HLCLIENT_9DE_TIMING='off';HLCLIENT_9DE_CAPTURE='local'
                HLCLIENT_51_OLD_VISIBILITY_DIFF=''}
            $run=[ordered]@{variant=$variant;repeat=$repeat;profile=$profile;environment=$vars
                argv=@('[gauss-71b97-camera]','--reporter','compact');executable_sha256=$manifest.bins[$variant].sha256
                stdout=[IO.Path]::GetFileName($stdout);stderr=[IO.Path]::GetFileName($stderr)
                started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;exit=$null
                observed_peak_working_set_bytes=$null;observed_peak_paged_memory_bytes=$null
                sample_count=0;sample_errors=0;last_successful_sample_utc=$null;last_successful_sample_elapsed_ms=$null
                duration_elapsed_ms=$null;unsampled_tail_ms=$null;samples=[Collections.Generic.List[object]]::new()}
            $manifest.runs.Add($run);Save-Manifest
            $watch=[Diagnostics.Stopwatch]::StartNew()
            $process=Start-Process -FilePath $bins[$variant] -ArgumentList $run.argv `
                -WorkingDirectory (Split-Path $bins[$variant]) -Environment $vars -WindowStyle Hidden `
                -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
            $run.owned_child_pid=$process.Id
            do {
                try {
                    if(-not $process.HasExited){
                        $process.Refresh();$working=[long]$process.PeakWorkingSet64;$paged=[long]$process.PeakPagedMemorySize64
                        if($working -gt 0){
                            ++$run.sample_count
                            $run.observed_peak_working_set_bytes=[Math]::Max([long]$run.observed_peak_working_set_bytes,$working)
                            $run.observed_peak_paged_memory_bytes=[Math]::Max([long]$run.observed_peak_paged_memory_bytes,$paged)
                            $run.last_successful_sample_utc=[DateTime]::UtcNow.ToString('o')
                            $run.last_successful_sample_elapsed_ms=$watch.Elapsed.TotalMilliseconds
                            $run.samples.Add([ordered]@{elapsed_ms=$run.last_successful_sample_elapsed_ms
                                peak_working_set_bytes=$working;peak_paged_memory_bytes=$paged})
                        }
                    }
                } catch {++$run.sample_errors}
                if($watch.Elapsed.TotalSeconds -gt 120 -and -not $process.HasExited){
                    # Only the child launched immediately above is owned here.
                    $run.timed_out=$true;$process.Kill();$process.WaitForExit();throw "Owned memory fixture exceeded 120 seconds: $stem"
                }
            } while(-not $process.WaitForExit($PollMilliseconds))
            $watch.Stop();$run.exit=$process.ExitCode;$run.ended_utc=[DateTime]::UtcNow.ToString('o')
            $run.duration_elapsed_ms=$watch.Elapsed.TotalMilliseconds
            if($null -ne $run.last_successful_sample_elapsed_ms){
                $run.unsampled_tail_ms=$run.duration_elapsed_ms-$run.last_successful_sample_elapsed_ms
            }
            $run.stdout_sha256=(Get-FileHash -LiteralPath $stdout).Hash
            $run.stderr_sha256=(Get-FileHash -LiteralPath $stderr).Hash
            Save-Manifest
            Write-Output "$stem exit=$($run.exit) samples=$($run.sample_count) observed_peak_WS=$($run.observed_peak_working_set_bytes)"
            if($run.exit -ne 0 -or $run.sample_count -eq 0){throw "Failed/unobserved memory fixture: $stem"}
        }
    }}
    $manifest.status='completed'
} catch {$manifest.status='failed';$manifest.error=$_.Exception.Message;throw}
finally {$manifest.completed_utc=[DateTime]::UtcNow.ToString('o');Save-Manifest;Write-Output "Memory-only evidence: $dir"}
