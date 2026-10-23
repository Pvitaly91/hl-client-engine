# Summarizes the offline fixture evidence without discarding repeats or outliers.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$MatrixDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$directory=[IO.Path]::GetFullPath($MatrixDirectory)
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $directory.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Only repository-owned generated evidence is supported.'
}
$manifestPath=Join-Path $directory 'manifest.json'
$manifest=Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json -AsHashtable
function Get-Median([double[]]$Values) {
    $sorted=@($Values | Sort-Object);if($sorted.Count -eq 0){return $null}
    $middle=[int][Math]::Floor($sorted.Count/2)
    if($sorted.Count%2){return [double]$sorted[$middle]}
    return ([double]$sorted[$middle-1]+[double]$sorted[$middle])/2.0
}
function Get-OrderedSignature([string[]]$Values) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToHexString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([string]::Join("`n",$Values)))) }
    finally { $sha.Dispose() }
}
$runs=[Collections.Generic.List[object]]::new()
foreach($run in $manifest.runs) {
    $path=Join-Path $directory $run.stdout
    $perf=[Collections.Generic.List[object]]::new()
    $proofs=[Collections.Generic.List[object]]::new()
    $scenes=[Collections.Generic.List[object]]::new()
    $hardware=[Collections.Generic.List[object]]::new()
    $rawCount=0;$gpuMissing=0;$lightSignatures=[Collections.Generic.HashSet[string]]::new()
    $orderedLights=[Collections.Generic.List[string]]::new()
    foreach($line in Get-Content -LiteralPath $path) {
        if($line -match '^\[(71b97-perf|9de-equivalence|71b97-scene|71b97-hardware|22ad-world-raw)\] (\{.*\})$') {
            $kind=$Matches[1];$item=$Matches[2] | ConvertFrom-Json -AsHashtable
            switch($kind) {
                '71b97-perf' {$perf.Add($item)}
                '9de-equivalence' {$proofs.Add($item)}
                '71b97-scene' {$scenes.Add($item)}
                '71b97-hardware' {$hardware.Add($item)}
                '22ad-world-raw' {
                    ++$rawCount;if($null -eq $item.gpu_total_ms){++$gpuMissing}
                    if($item.ContainsKey('light_input_signature')){
                        [void]$lightSignatures.Add([string]$item.light_input_signature)
                        $orderedLights.Add(('{0}:{1}' -f $item.source_frame,$item.light_input_signature))
                    }
                }
            }
        }
    }
    if($run.exit -ne 0 -or $scenes.Count -ne 1 -or $hardware.Count -ne 1 -or $proofs.Count -eq 0) {
        throw "Incomplete/failed fixture (including skipped context): $path"
    }
    if($scenes[0].view -ne $run.view -or $scenes[0].msaa -ne $run.mode -or
        $scenes[0].light_profile -ne $run.profile -or $scenes[0].remote_capture -ne $false) {
        throw "Fixture scene does not match its manifest case: $path"
    }
    $runs.Add([ordered]@{
        variant=$run.variant;repeat=$run.repeat;mode=$run.mode;view=$run.view;profile=$run.profile
        timing=$manifest.timing;exit=$run.exit;stdout=$run.stdout
        stdout_sha256=(Get-FileHash -LiteralPath $path).Hash
        hardware=@($hardware);scenes=@($scenes);metrics=@($perf);equivalence=@($proofs)
        raw_samples=$rawCount;raw_gpu_missing=$gpuMissing;distinct_light_input_signatures=@($lightSignatures)
        ordered_raw_light_input_count=$orderedLights.Count
        ordered_raw_light_inputs_sha256=(Get-OrderedSignature -Values $orderedLights.ToArray())
    })
}
$groups=$runs | Group-Object { '{0}|{1}|{2}|{3}' -f $_.mode,$_.view,$_.profile,$_.timing }
$comparisons=[Collections.Generic.List[object]]::new()
foreach($group in $groups) {
    $baseline=@($group.Group | Where-Object variant -eq B0)
    $candidate=@($group.Group | Where-Object variant -eq B1)
    $metricRows=[Collections.Generic.List[object]]::new()
    foreach($scope in @('gpu_world_static_world','gpu_total','studio_prepare','viewmodel_scene_prepare','world_visibility','render_submit','owned_subset')) {
        $baselineValues=@(foreach($run in $baseline){foreach($metric in $run.metrics){if($metric.scope -eq $scope -and $metric.ContainsKey('p50')){$metric}}})
        $candidateValues=@(foreach($run in $candidate){foreach($metric in $run.metrics){if($metric.scope -eq $scope -and $metric.ContainsKey('p50')){$metric}}})
        $noise=$null;$baseMedian=$null;$candidateMedian=$null;$verdict='unavailable'
        if($baselineValues.Count -gt 0) {
            $values=@($baselineValues.p50 | Sort-Object)
            $baseMedian=Get-Median -Values $values
            $noise=[Math]::Max([double]$values[-1]-[double]$values[0],.05*$baseMedian)+.1
            if($candidateValues.Count -gt 0) {
                $values=@($candidateValues.p50 | Sort-Object)
                $candidateMedian=Get-Median -Values $values
                $delta=$candidateMedian-$baseMedian
                $verdict=if($delta -lt -$noise){'gain_exceeds_baseline_noise'}elseif($delta -gt $noise){'regression_exceeds_baseline_noise'}else{'within_baseline_noise'}
            } else {$verdict='baseline_only'}
        }
        $metricRows.Add([ordered]@{scope=$scope;baseline=@($baselineValues);candidate=@($candidateValues)
            baseline_p50_median_ms=$baseMedian;candidate_p50_median_ms=$candidateMedian
            baseline_noise_envelope_ms=$noise;verdict=$verdict
            quantile_comparisons=@(foreach($quantile in @('p50','p95','p99','max')) {
                $leftValues=@($baselineValues | ForEach-Object {$_[$quantile]})
                $rightValues=@($candidateValues | ForEach-Object {$_[$quantile]})
                $leftMedian=Get-Median -Values $leftValues;$rightMedian=Get-Median -Values $rightValues
                $envelope=$null;$quantileVerdict='unavailable'
                if($leftValues.Count -gt 0) {
                    $bounds=$leftValues | Measure-Object -Minimum -Maximum
                    $envelope=[Math]::Max($bounds.Maximum-$bounds.Minimum,.05*$leftMedian)+.1
                    if($rightValues.Count -gt 0) {
                        $delta=$rightMedian-$leftMedian
                        $quantileVerdict=if($delta -lt -$envelope){'gain_exceeds_baseline_noise'}
                            elseif($delta -gt $envelope){'regression_exceeds_baseline_noise'}else{'within_baseline_noise'}
                    } else {$quantileVerdict='baseline_only'}
                }
                [ordered]@{quantile=$quantile;baseline_median_ms=$leftMedian;candidate_median_ms=$rightMedian
                    baseline_noise_envelope_ms=$envelope;verdict=$quantileVerdict}
            })})
    }
    $proofRows=[Collections.Generic.List[object]]::new()
    $pairRows=[Collections.Generic.List[object]]::new()
    foreach($b1 in $candidate) {
        $b0=@($baseline | Where-Object repeat -eq $b1.repeat)
        if($b0.Count -ne 1){throw 'Candidate repeat lacks its exact baseline pair.'}
        $before=$b0[0];$left=$before.scenes[0];$right=$b1.scenes[0]
        $cameraExact=$true
        foreach($field in @('eye','yaw','pitch','pixel_eye','pixel_yaw','pixel_target',
            'vertical_fov_radians','near_plane','far_plane','width','height','msaa')) {
            if((ConvertTo-Json -InputObject $left[$field] -Compress) -cne
                (ConvertTo-Json -InputObject $right[$field] -Compress)){$cameraExact=$false}
        }
        $hardwareExact=$true
        foreach($field in @('vendor','device','version')) {
            if($before.hardware[0][$field] -cne $b1.hardware[0][$field]){$hardwareExact=$false}
        }
        $pairRows.Add([ordered]@{
            repeat=$b1.repeat
            full_scene_color_exact=($left.pixel_signature -eq $right.pixel_signature)
            world_draw_count_exact=($left.world_draws -eq $right.world_draws)
            world_triangle_count_exact=($left.world_triangles -eq $right.world_triangles)
            hardware_exact=$hardwareExact;camera_projection_exact=$cameraExact
            ordered_raw_light_inputs_exact=($before.ordered_raw_light_input_count -eq $b1.ordered_raw_light_input_count -and
                $before.ordered_raw_light_inputs_sha256 -eq $b1.ordered_raw_light_inputs_sha256 -and
                $before.ordered_raw_light_input_count -eq $left.frames -and $b1.ordered_raw_light_input_count -eq $right.frames)
            baseline_full_scene_color=$left.pixel_signature;candidate_full_scene_color=$right.pixel_signature
            baseline_draws=$left.world_draws;candidate_draws=$right.world_draws
            baseline_triangles=$left.world_triangles;candidate_triangles=$right.world_triangles
            baseline_light_inputs_sha256=$before.ordered_raw_light_inputs_sha256
            candidate_light_inputs_sha256=$b1.ordered_raw_light_inputs_sha256
        })
        if($b0[0].equivalence.Count -ne $b1.equivalence.Count){throw 'Paired runs have different equivalence proof counts.'}
        foreach($proof in $b1.equivalence) {
            $index=if($proof.ContainsKey('proof_index')){$proof.proof_index}else{0}
            $peers=@($b0[0].equivalence | Where-Object {($_.proof_index -eq $index) -and ($_.remote_capture -eq $proof.remote_capture)})
            if($peers.Count -ne 1){throw 'Candidate equivalence case lacks its exact baseline proof.'}
            $peer=$peers[0]
            if($proof.view -ne $peer.view -or $proof.requested_msaa -ne $peer.requested_msaa -or
                $proof.profile -ne $peer.profile -or $proof.proof_state -ne $peer.proof_state -or
                $proof.depth_scope -ne $peer.depth_scope){throw 'Paired proofs describe different inputs or observation scopes.'}
            $proofRows.Add([ordered]@{repeat=$b1.repeat;proof_index=$index;state=$proof.proof_state
                color_exact=($proof.color_signature -eq $peer.color_signature)
                depth_exact=($proof.depth_signature -eq $peer.depth_signature)
                light_inputs_exact=($proof.light_input_signature -eq $peer.light_input_signature)
                baseline_color=$peer.color_signature;candidate_color=$proof.color_signature
                baseline_depth=$peer.depth_signature;candidate_depth=$proof.depth_signature})
        }
    }
    $comparisons.Add([ordered]@{case=$group.Name;baseline_runs=$baseline.Count;candidate_runs=$candidate.Count
        metrics=@($metricRows);paired_scene_invariants=@($pairRows);equivalence=@($proofRows)})
}
$summary=[ordered]@{schema='hlc-9de-dynamic-summary-v1';manifest_sha256=(Get-FileHash -LiteralPath $manifestPath).Hash
    source_status=$manifest.status;noise_rule=$manifest.noise_rule;correctness=$manifest.correctness
    all_repeats_retained=$true;outliers_removed=0;runs=@($runs);comparisons=@($comparisons)}
$proofFailures=@(foreach($case in $comparisons){foreach($proof in $case.equivalence){
    if(-not $proof.color_exact -or -not $proof.depth_exact -or -not $proof.light_inputs_exact){$proof}
}})
$summary.equivalence_failures=$proofFailures.Count
$pairFailures=@(foreach($case in $comparisons){foreach($pair in $case.paired_scene_invariants){
    if(-not $pair.full_scene_color_exact -or -not $pair.world_draw_count_exact -or
        -not $pair.world_triangle_count_exact -or -not $pair.hardware_exact -or
        -not $pair.camera_projection_exact -or -not $pair.ordered_raw_light_inputs_exact){$pair}
}})
$summary.paired_scene_invariant_failures=$pairFailures.Count
$out=Join-Path $directory 'summary.json'
$summary | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath $out -Encoding utf8
Write-Output $out
foreach($case in $comparisons){
    foreach($metric in $case.metrics | Where-Object scope -in @('gpu_world_static_world','gpu_total','render_submit')){
        Write-Output ("{0}: {1} B0={2:N4} B1={3:N4} envelope={4:N4} {5}" -f $case.case,$metric.scope,
            $metric.baseline_p50_median_ms,$metric.candidate_p50_median_ms,$metric.baseline_noise_envelope_ms,$metric.verdict)
    }
}
if($proofFailures.Count -gt 0){throw "Exact equivalence failed in $($proofFailures.Count) retained paired proofs. See $out"}
if($pairFailures.Count -gt 0){throw "Exact scene invariants failed in $($pairFailures.Count) retained pairs. See $out"}
