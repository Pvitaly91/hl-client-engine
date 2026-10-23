[CmdletBinding()]
param([Parameter(Mandatory)][string]$RecordDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$record=[IO.Path]::GetFullPath($RecordDirectory)
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $record.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence destination must remain inside the repository.'
}
function Stats([double[]]$Values) {
    $sorted=@($Values | Sort-Object)
    if($sorted.Count -eq 0){return $null}
    $mid=[int][Math]::Floor($sorted.Count/2)
    $median=if($sorted.Count%2){$sorted[$mid]}else{($sorted[$mid-1]+$sorted[$mid])/2.0}
    return [ordered]@{count=$sorted.Count;minimum=$sorted[0];median=$median;maximum=$sorted[-1]}
}
$matrices=[Collections.Generic.List[object]]::new()
$cases=[Collections.Generic.List[object]]::new()
$regressions=[Collections.Generic.List[object]]::new()
$independent=[Collections.Generic.List[object]]::new()
$allRuns=[Collections.Generic.List[object]]::new()
foreach($dir in Get-ChildItem -LiteralPath $record -Directory -Filter 'AB-*' | Sort-Object Name) {
    $manifest=Get-Content -LiteralPath (Join-Path $dir.FullName 'manifest.json') -Raw | ConvertFrom-Json -AsHashtable
    $summary=Get-Content -LiteralPath (Join-Path $dir.FullName 'summary.json') -Raw | ConvertFrom-Json -AsHashtable
    if($manifest.status -ne 'completed' -or $summary.equivalence_failures -ne 0 -or $summary.paired_scene_invariant_failures -ne 0) {
        throw "Matrix is incomplete or failed exact checks: $($dir.Name)"
    }
    foreach($run in $summary.runs){$allRuns.Add($run)}
    $cold=[ordered]@{}
    foreach($variant in @('B0','B1')) {
        $variantRuns=@($summary.runs | Where-Object variant -eq $variant)
        $cold[$variant]=Stats -Values @($variantRuns | ForEach-Object {$_.scenes[0].cold_render_cpu_ms})
    }
    $matrices.Add([ordered]@{name=$dir.Name;manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $dir.FullName 'manifest.json')).Hash
        summary_sha256=(Get-FileHash -LiteralPath (Join-Path $dir.FullName 'summary.json')).Hash
        run_count=$summary.runs.Count;timing=$manifest.timing;bins=$manifest.bins
        cold_first_render_cpu_ms=$cold})
    foreach($case in $summary.comparisons) {
        $cases.Add([ordered]@{matrix=$dir.Name;case=$case.case;metrics=$case.metrics
            paired_scene_invariants=$case.paired_scene_invariants;equivalence=$case.equivalence})
        foreach($metric in $case.metrics){foreach($q in $metric.quantile_comparisons){
            if($q.verdict -eq 'regression_exceeds_baseline_noise') {
                $regressions.Add([ordered]@{matrix=$dir.Name;case=$case.case;scope=$metric.scope
                    quantile=$q.quantile;baseline_ms=$q.baseline_median_ms;candidate_ms=$q.candidate_median_ms
                    envelope_ms=$q.baseline_noise_envelope_ms})
            }
        }}
    }
    # Independent direct element-by-element check: not the summarizer's SHA.
    foreach($candidate in $manifest.runs | Where-Object variant -eq 'B1') {
        $matching=@($manifest.runs | Where-Object {$_.variant -eq 'B0' -and $_.repeat -eq $candidate.repeat -and
            $_.mode -eq $candidate.mode -and $_.view -eq $candidate.view -and $_.profile -eq $candidate.profile})
        if($matching.Count -ne 1){throw 'Independent audit requires a unique exact baseline case.'}
        $sequences=@{}
        foreach($pair in @(@('B0',$matching[0]),@('B1',$candidate))) {
            $rows=[Collections.Generic.List[object]]::new()
            foreach($line in Get-Content -LiteralPath (Join-Path $dir.FullName $pair[1].stdout)) {
                if($line -match '^\[22ad-world-raw\] (\{.*\})$') {
                    $raw=$Matches[1] | ConvertFrom-Json -AsHashtable
                    $rows.Add(@([string]$raw.source_frame,[string]$raw.light_input_signature))
                }
            }
            $sequences[$pair[0]]=$rows
        }
        if($sequences.B0.Count -ne 192 -or $sequences.B1.Count -ne 192){throw 'Expected exactly 192 raw input witnesses per run.'}
        for($i=0;$i -lt 192;++$i) {
            if($sequences.B0[$i][0] -cne $sequences.B1[$i][0] -or $sequences.B0[$i][1] -cne $sequences.B1[$i][1]) {
                throw "Ordered light witness mismatch at sample $i in $($dir.Name)"
            }
        }
        $independent.Add([ordered]@{matrix=$dir.Name;repeat=$candidate.repeat;mode=$candidate.mode
            view=$candidate.view;profile=$candidate.profile;compared_samples=192;exact=$true})
    }
}
$proofCount=0;$pairCount=0
foreach($case in $cases){$proofCount+=$case.equivalence.Count;$pairCount+=$case.paired_scene_invariants.Count}
$timingOn=@($allRuns | Where-Object timing -eq 'on');$timingOff=@($allRuns | Where-Object timing -eq 'off')
$quality=[ordered]@{
    timing_on_runs=$timingOn.Count;timing_off_runs=$timingOff.Count
    timing_on_raw_samples=($timingOn.raw_samples | Measure-Object -Sum).Sum
    timing_on_raw_gpu_missing=($timingOn.raw_gpu_missing | Measure-Object -Sum).Sum
    timing_off_raw_gpu_missing_expected=($timingOff.raw_gpu_missing | Measure-Object -Sum).Sum
    observed_gpu_samples=(@($timingOn | ForEach-Object {$_.scenes[0].gpu_sample_count}) | Measure-Object -Sum).Sum
    gpu_invalid=(@($allRuns | ForEach-Object {$_.scenes[0].timing_invalid_samples}) | Measure-Object -Sum).Sum
    gpu_omitted=(@($allRuns | ForEach-Object {$_.scenes[0].timing_omitted_samples}) | Measure-Object -Sum).Sum
    gpu_unobserved=(@($allRuns | ForEach-Object {$_.scenes[0].unobserved_frame_ids}) | Measure-Object -Sum).Sum
    slot_wait_timeouts=(@($allRuns | ForEach-Object {$_.scenes[0].test_slot_wait_timeouts}) | Measure-Object -Sum).Sum
}
$memory=[ordered]@{
    GPU_resident_bytes='not_measured';process_peak_memory='not_measured'
    measurement_scope='timed benchmark runs only; separate memory-only observer records, if present, are reported independently'
    target_allocations_per_run=@($allRuns | ForEach-Object {$_.scenes[0].target_allocations} | Sort-Object -Unique)
    prepared_draw_bytes=@($allRuns | ForEach-Object {$_.scenes[0].prepared_draw_bytes} | Sort-Object -Unique)
    warm_world_upload_counts=@($allRuns | ForEach-Object {$_.scenes[0].world_uploads} | Sort-Object -Unique)
    note='These bounded allocation/upload counters are not a measured resident-memory high-water value.'
}
$result=[ordered]@{schema='hlc-9de-dynamic-aggregate-v1';run_count=$allRuns.Count;paired_cases=$pairCount
    exact_world_proof_pairs=$proofCount;exact_full_scene_pairs=$pairCount;sample_quality=$quality
    independent_ordered_light_element_audit=@($independent);regressions_beyond_envelope=@($regressions)
    resource_observations=$memory;matrices=@($matrices);cases=@($cases)
    limitations=@('Controlled synthetic lights, not a replay of historical dynamic state.',
        'Depth comparison is resolved world depth, not per-sample MSAA depth.',
        'Cold CPU render includes resource preparation and driver work; it does not isolate shader compilation.',
        'No GPU timing is claimed when diagnostic queries are disabled.')}
$path=Join-Path $record 'aggregate-metrics.json'
$result | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $path -Encoding utf8
Write-Output $path
Write-Output "Runs=$($allRuns.Count) pairs=$pairCount worldProofPairs=$proofCount directOrderedLightChecks=$($independent.Count)"
$quality | ConvertTo-Json -Compress
$regressions | ConvertTo-Json -Depth 5
