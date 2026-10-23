# Offline actual-assets fixture only; never launches the game or a server.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BaselineExe,
    [string]$CandidateExe,
    [Parameter(Mandatory)][string]$OutputRoot,
    [ValidateSet(0,2,4,8)][int[]]$Modes=@(8),
    [string[]]$Views=@('9de_primary_fixed'),
    [ValidateSet('none','one','far','many','moved','expired','transient','transition')]
    [string[]]$Profiles=@('none','one','far','many'),
    [ValidateSet('on','off')][string]$Timing='on',
    [ValidateRange(1,6)][int]$Repeats=3,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Tag='matrix'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'PowerShell 7 required'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($OutputRoot)
if(-not $out.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){
    throw 'Generated output must stay inside the source checkout'
}
$dir=Join-Path $out ($Tag+'-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
New-Item -ItemType Directory -Path $dir | Out-Null
$bins=[ordered]@{B0=[IO.Path]::GetFullPath($BaselineExe)}
if($CandidateExe){$bins.B1=[IO.Path]::GetFullPath($CandidateExe)}
$manifest=[ordered]@{schema='hlc-9de-dynamic-matrix-v1';status='running';created_utc=[DateTime]::UtcNow.ToString('o');
    hidden=$true;swap=$false;live_sessions=0;timing=$Timing;repetitions=$Repeats;bins=[ordered]@{};
    noise_rule='Per scene/mode: baseline repeated p50 span or 5% of baseline median, whichever larger, plus 0.1 ms. A gain/regression requires exceeding this envelope. Tails remain separately reported, never excluded.';
    correctness='Exact color and resolved world depth signatures; no tolerance. MSAA resolved depth is not per-sample depth.';
    runs=[Collections.Generic.List[object]]::new()}
foreach($key in $bins.Keys){
    if([IO.Path]::GetFileName($bins[$key]) -in @('hlclient.exe','HLClientLauncher.exe','hl.exe','hlds.exe')){throw 'Game/launcher/server forbidden'}
    $manifest.bins[$key]=@{path=$bins[$key];sha256=(Get-FileHash -LiteralPath $bins[$key]).Hash;bytes=(Get-Item -LiteralPath $bins[$key]).Length}
}
function Save-Manifest { $manifest | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $dir 'manifest.json') -Encoding utf8 }
function Assert-HostIdle {
    # A sandbox process list can omit host compilers. Refuse an unverified
    # inventory rather than label a contended run as an isolated benchmark.
    try { $processes=@(Get-CimInstance Win32_Process -ErrorAction Stop) }
    catch { throw 'Host process inventory unavailable; run this offline fixture in the approved host context.' }
    $busy=@($processes | Where-Object { $_.Name -in @(
        'MSBuild.exe','cl.exe','link.exe','ninja.exe','clang-cl.exe',
        'ctest.exe','hlclient_tests.exe','hlclient_gauss_71b97_tests.exe',
        'hlclient_core_api_tests.exe','hlclient.exe','hl.exe','hlds.exe') })
    if($busy.Count){throw 'A build, test, or game is running; keep benchmarks isolated, do not terminate it'}
}
Save-Manifest
try {
  for($r=1;$r -le $Repeats;$r++) { foreach($mode in $Modes) { foreach($view in $Views) { foreach($profile in $Profiles) {
    $order=@($bins.Keys);if($r%2 -eq 0){[array]::Reverse($order)}
    foreach($variant in $order){
      Assert-HostIdle
      if((Get-FileHash -LiteralPath $bins[$variant]).Hash -ne $manifest.bins[$variant].sha256){throw 'Frozen fixture executable changed'}
      $stem='{0:D3}-{1}-r{2}-aa{3}-{4}-{5}' -f ($manifest.runs.Count+1),$variant,$r,$mode,$view,$profile
      $stdout=Join-Path $dir ($stem+'.log');$stderr=Join-Path $dir ($stem+'.err')
      $vars=@{HLCLIENT_LOCAL_GAME_ROOT='D:\DEV\HLCLIENT-RESEARCH\Half-Life';HLCLIENT_71B97_VISIBILITY='all';
        HLCLIENT_22AD_VIEW=$view;HLCLIENT_22AD_MSAA=[string]$mode;HLCLIENT_22AD_RAW_W='1';
        HLCLIENT_9DE_LIGHT_PROFILE=$profile;HLCLIENT_9DE_TIMING=$Timing;HLCLIENT_9DE_CAPTURE='local';HLCLIENT_51_OLD_VISIBILITY_DIFF=''}
      $run=[ordered]@{variant=$variant;repeat=$r;mode=$mode;view=$view;profile=$profile;
        argv=@('[gauss-71b97-camera]','--reporter','compact');environment=$vars;stdout=[IO.Path]::GetFileName($stdout);stderr=[IO.Path]::GetFileName($stderr);exit=$null}
      $manifest.runs.Add($run);Save-Manifest
      $p=Start-Process -FilePath $bins[$variant] -ArgumentList $run.argv -WorkingDirectory (Split-Path $bins[$variant]) -Environment $vars -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -Wait
      $run.exit=$p.ExitCode;$run.stdout_sha256=(Get-FileHash -LiteralPath $stdout).Hash
      try { $peak=$p.PeakWorkingSet64; $run.process_peak_working_set_bytes=if($peak -gt 0){$peak}else{$null} } catch { $run.process_peak_working_set_bytes=$null }
      Save-Manifest
      Write-Output "$stem exit=$($p.ExitCode)"
      if($p.ExitCode -ne 0){throw "Fixture failed: $stdout"}
    }
  }}}}
  $manifest.status='completed'
} catch { $manifest.status='failed';$manifest.error=$_.Exception.Message;throw }
finally { $manifest.completed_utc=[DateTime]::UtcNow.ToString('o');Save-Manifest;Write-Output "Results: $dir" }
