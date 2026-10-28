#requires -Version 7
param([switch]$Symbols)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$task=Join-Path $PSScriptRoot '../manual-artifacts/task-records/studio-rx-tail-causal-50b5ffe1-20261025'
. (Join-Path $task 'quality-common.ps1')
$env:HLC_COST_ACTORS='16';$env:HLC_COST_HISTORY='16';$env:HLC_COST_DIAG='1';$env:HLC_COST_PROFILE='0';$env:HLC_COST_FIXED='0';$env:HLC_COST_ACTUAL='1';$env:HLC_DETAIL_TIMERS='0';$env:HLC_DETAIL_STACKS='1';$env:HLC_FIXTURE_PAD_PID='1'
Set-QualityEnvironment 'HLC_FIXTURE_PID' $null
$root=New-QualityAttempt 'commit-allocation-profile'
$summaries=@{};$sites=@{};$receipts=@()
foreach($variant in @('B0','B1')){
 $name=$(if($Symbols){'rx_profile_symbols_'}else{'rx_profile_v2_'})+$variant.ToLower();$dir=Join-Path $task "isolated-fixtures/$variant/$name"
 $ready=Get-Content -LiteralPath (Join-Path $dir 'ready.json') -Raw|ConvertFrom-Json
 $exe=Join-Path $dir "bin/$name.exe"
 if((Get-QualityHash $exe) -ine $ready.executable_sha256){throw 'Profiler identity changed'}
 $receipts+=Invoke-QualityProcess -Name $variant -File $exe -Arguments @('[history-projection-cost]','--reporter','console') -WorkingDirectory $script:QualityRepository -ResultRoot $root
 $all=@(Get-Content -LiteralPath (Join-Path $root "$variant.stdout.log"))
 $summaries[$variant]=($all|Where-Object {$_ -like '[[]allocation-profile[]]*'}).Substring(21)|ConvertFrom-Json
 $sites[$variant]=@($all|Where-Object {$_ -like '[[]allocation-site[]]*'}|ForEach-Object {$_.Substring($_.IndexOf('{'))|ConvertFrom-Json})
 if($summaries[$variant].overflow_calls -or $summaries[$variant].tracked_live_after_fixture){throw 'Profile overflow or tracked allocations not released'}
}
$b=@($sites.B0|ForEach-Object{($_.stack -join '|')+':'+$_.calls+':'+$_.bytes+':'+$_.site_peak_live_bytes}|Sort-Object)
$c=@($sites.B1|ForEach-Object{($_.stack -join '|')+':'+$_.calls+':'+$_.bytes+':'+$_.site_peak_live_bytes}|Sort-Object)
$exact=($b.Count -eq $c.Count)
if($exact){for($i=0;$i -lt $b.Count;++$i){if($b[$i] -cne $c[$i]){$exact=$false}}}
[ordered]@{scope='Allocation stacks only for warmed observe_entities timers;1024 new RX +3072 noRX checks. Header-tagged requested live bytes, not RSS. Stacks and expanded headers perturb timing; all profile timing values ineligible.';status='completed';exact_sites_calls_bytes_peak=$exact;summaries=$summaries;sites=$sites;receipts=$receipts;live_sessions=0}|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $root 'profile-analysis.json')
$root
