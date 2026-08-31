# Stock runtime first observations

## Neutral observation

The only permitted name is
`first_post_resource_runtime_candidate`. It has no assigned service semantic.
It is not called packet entities, clientdata, time, baseline, frame, delta or
any `svc_*` name.

`StockRuntimeFirstObservationState` retains the exact reconstructed cursor,
candidate bit width, source-payload geometry, alignment, recurrence/cross-run
metadata and evidence profile. At a byte boundary it reads exactly one numeric
candidate byte. At a non-byte boundary it retains only a bounded next-bit
prefix. In either case the body is not consumed, skipped or searched for a
later apparent opcode.

A normal accepted run publishes `single_observation`. An accepted reconnect run
publishes `stable_observation` only because generation A and generation B are
split by their independently attested observed-ordinal ranges, reset and replayed
separately, and yield identical alignment, width and candidate value/prefix.
Campaign stability additionally requires the same neutral observation across all
accepted runs under one exact stock profile. A conflict is
`candidate_conflicting` and evidence publication is blocked.

## Campaign and threshold

The target campaign is:

| Scenario | Accepted runs |
|---|---:|
| boot_camp baseline | 6 |
| crossfire baseline | 4 |
| stalkyard baseline | 4 |
| crossfire idle-runtime | 4 |
| boot_camp reconnect | 2 |
| boot_camp drop server-to-client transport ordinal | 2 |
| crossfire duplicate server-to-client transport ordinal | 1 |
| stalkyard reorder server-to-client transport ordinal | 1 |
| Total | 24 |

No keyboard/mouse/movement input is automated. Every accepted run requires at
least 100 threshold-eligible sequenced S2C packets, an exact boundary, no wrong
source/limit violation, continuous isolation and exact restoration. Normal
slots use the peer-delivered population; reconnect uses the sum of independently
replayed A+B populations so the retired-A tail is excluded. Baseline/idle runs
additionally require map entry and a practical 30-second duration.
Idle runs additionally require client readiness, at least 30 seconds of actual
owned-session duration, and a delivered sequenced S2C datagram during the last
five seconds of the run. This rejects a burst of initial traffic followed by a
dead connection. Transport-ordinal perturbation runs reach the same boundary
from replay-accepted traffic without a wrong source or incomplete journal.

`reconnect` retains one guard, relay and HLDS while owning two sequential stock
client processes. The strict staged leaves are
`reconnect-transport-observation.staged.json` and
`reconnect-orchestration.staged.json`. Generation A is shut down only after the
private send-only retired-tail sink is ready; any exact-HLDS sequenced tail
before B's fresh ACCEPT is preserved in the journal and routed only to that
retired sink. It is excluded from B proof, both generation counters and campaign
packet thresholds. Checker and independent walker split the journal into
non-overlapping A/B views and replay each from reset state. Only after both agree
on two exact boundaries, two identical neutral candidates, distinct lifecycle,
cleanup and restoration may `reconnect-observation.json` and a final accepted
manifest be published. The accepted manifest carries exactly
`connection_generation_count=2`, `exact_boundary_count=2`,
`runtime_candidate_count=2`, `generation_distinct=true` and
`candidate_conflict=false` in addition to the normal fields.

Resume is bounded and deterministic: an accepted run fills the first missing
matching matrix slot. A `bounded-session-incomplete` run is retained, never
overwritten or reinterpreted, and does not consume a slot. Any rejected/fatal
run is retained for diagnosis and blocks every later resume attempt; it cannot
be bypassed by rerunning the same command. `campaign-manifest.json` is an atomic sanitized progress
summary, not evidence. An existing summary must exactly match an independently
reconstructed state before the next stock process starts. After a run, its
exact previously validated bytes are held as the compare-and-swap predecessor;
the old leaf is moved by handle and the new leaf is published no-replace, so a
mutation or concurrent substitution fails closed. First publication is also
no-replace. Every accepted run and the final campaign summary are checked twice
for deterministic output. Resume reconstruction also reruns the independent
PowerShell walker twice for every slot and reconciles all transport, boundary,
candidate and reconnect-generation fields before counting it. The C++ campaign
summary never invents that proof: the runner/verifier passes only the bounded
set of run IDs validated during that invocation; without those explicit
capabilities, an accepted publication cannot satisfy the campaign gate.

The one deliberately fail-closed resume limitation is a process/power loss
after a run directory is final but before its successor progress manifest is
published. Because that state is indistinguishable from deletion or mutation of
the previously validated manifest, the next invocation reports
`campaign_progress_manifest_missing_or_unsafe`, starts zero new campaign runs
and does not reconstruct or repair the leaf. Existing run directories remain
the authoritative ignored record; no process/restoration cleanup is deferred to
the progress manifest.

Before that matrix starts, the same command owns one separate
`boot_camp`/`baseline` canary under
`manual-artifacts\stock-runtime-canary`. Its exact local manifest binds the
implementation commit, run ID, stock profile, checker output and transport/
replay structural hashes, records at least 100 delivered sequenced S2C packets,
and states `counted_in_campaign=false`. The sibling may contain exactly one run
and one manifest. If power is lost after the accepted run but before the
manifest, that unbound run is quarantined and is never recovered or rebound.
Preserve it for diagnosis, remove only the exact ignored sibling, and rerun the
same campaign command to capture a fresh bound canary. Multiple, incomplete or
mutated entries fail closed. Both the runner and final verifier execute the
checker twice and the independent walker twice, require zero exits and identical
output, and reconcile the complete boundary/candidate/replay facts before the
canary is accepted. Canary failure prevents creation of the 24-slot campaign
state.

For a research copy produced through M4.7.1.1.3 external-target approval,
capture preflight first validates the path-free v3 manifest, live prepared
destination and exactly one repository-local ignored approval artifact whose
fresh SHA-256 matches `external_approval_sha256`. The run, canary and
campaign-resume contracts then retain the sanitized
`external_target_profile` and `external_target_count`; mismatched
profiles/counts cannot be mixed or rebound. Approval expiration and live
source/target mutation are checked by materialization before that immutable
copy is published, while private review paths and identities never enter run
evidence. This provenance is a filesystem safety gate only: it adds no
candidate meaning and contributes nothing to the 24-run evidence threshold.

Run the campaign from elevated PowerShell with the explicit token:

```powershell
.\scripts\run_stock_runtime_first_observations.ps1 `
  -EnableActiveCapture `
  -ConfirmActiveCapture HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1 `
  -ResearchHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life" `
  -ClientPath "D:\DEV\HLCLIENT-RESEARCH\Half-Life\hl.exe" `
  -HldsPath "D:\DEV\HLCLIENT-RESEARCH\Half-Life\hlds.exe" `
  -CaptureToolPath ".\build\bin\Debug\hlclient_stock_runtime_capture.exe" `
  -NetworkIsolationGuardPath `
    ".\build\bin\Debug\hlclient_stock_runtime_isolation_guard.exe" `
  -CheckerPath ".\build\bin\Debug\hlclient_stock_runtime_check.exe" `
  -AppManifestPath "F:\SteamLibrary\steamapps\appmanifest_70.acf" `
  -OutputRoot ".\manual-artifacts\stock-runtime"
```

Verify independently:

```powershell
.\scripts\verify_stock_runtime_first_observations.ps1 `
  -CaptureRoot ".\manual-artifacts\stock-runtime" `
  -CheckerPath ".\build\bin\Debug\hlclient_stock_runtime_check.exe" `
  -MinimumAcceptedRuns 24 `
  -MinimumSequencedServerPackets 1000
```

Run the fake-only resume and threshold mutation checks in CI or locally. The
resume test uses and removes one metadata-only fixture under the exact ignored
campaign root; it contains no raw/auth bytes and never launches stock:

```powershell
.\scripts\test_stock_runtime_campaign_resume.ps1 `
  -TestExecutable .\build\bin\Debug\hlclient_tests.exe `
  -CheckerPath .\build\bin\Debug\hlclient_stock_runtime_check.exe
.\scripts\test_stock_runtime_evidence_threshold.ps1 `
  -TestExecutable .\build\bin\Debug\hlclient_tests.exe
```

The committed metadata-only evidence file is permitted only after the distinct
canary plus exact matrix have 25 accepted observations in total (24 matrix
runs), at least four reconnect generations, at least
1,000 threshold-eligible sequenced S2C packets, at least 26 exact boundaries and 26
matching candidates, every run passes its 100-S2C floor, and all version,
isolation and restoration gates are green. Before that,
`docs/evidence/GOLDSRC_STOCK_RUNTIME_FIRST_OBSERVATIONS.json` must remain absent.
This checkout currently claims no accepted real run and does not fabricate the
file.

If the gate passes, the evidence file has one exact allowlisted schema. It
contains an implementation commit whose exact subject is
`Complete stock runtime capture campaign lifecycle` and which is an ancestor of
the verified checkout; stock version and dynamic-isolation
profiles; accepted/rejected/incomplete counts; fixed map/scenario ordinals;
threshold-eligible sequenced/reassembled/decompressed counts; exact
boundary/candidate/reconnect totals; the representative complete replay-payload,
observed, delivery, byte, bit, source-sequence, source-size and remaining-bit
cursor; neutral candidate representation, exact bit width and recurrence;
metadata-only transport/replay structural hash sets; and exact
restoration/drift status. Unknown properties fail validation. Raw or
auth-derived hashes, raw/auth bytes, identities, paths, addresses, ports,
configs, process logs, entity values and screenshots are forbidden.

M4.7.1.2—not this milestone—owns runtime message catalog, baseline/entity and
clientdata decoding. Stock usercmd remains M4.7.2.
