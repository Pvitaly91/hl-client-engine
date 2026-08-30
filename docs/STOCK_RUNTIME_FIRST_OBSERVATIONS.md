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

One independently accepted run can publish only `single_observation`; it cannot
claim corpus stability. The corpus verifier establishes
`stable_observation` only from identical boundary alignment and candidate
value/prefix across accepted baseline runs with one identical stock version
profile and no contradictory complete run. A conflict is
`candidate_conflicting` and evidence publication is blocked.

## Campaign and threshold

The target campaign is:

| Scenario | Accepted runs |
|---|---:|
| boot_camp baseline | 6 |
| crossfire baseline | 4 |
| stalkyard baseline | 4 |
| idle-runtime | 4 |
| reconnect | 2 |
| drop server-to-client transport ordinal | 2 |
| duplicate server-to-client transport ordinal | 1 |
| reorder server-to-client transport ordinal | 1 |
| Total | 24 |

No keyboard/mouse/movement input is automated. Baselines require map entry,
practical 30-second duration, at least 100 sequenced S2C packets, exact boundary,
no wrong source/limit violation, continuous isolation and exact restoration.
Idle runs additionally require client readiness, at least 30 seconds of actual
owned-session duration, and a delivered sequenced S2C datagram during the last
five seconds of the run. This rejects a burst of initial traffic followed by a
dead connection. No input is injected. Transport-ordinal perturbation runs must retain
delivered sequenced server traffic and reach the same boundary without a wrong
source or incomplete journal.

`reconnect` is part of the required target matrix but is deliberately pending:
the current orchestrator owns one stock session only. The wrapper returns the
typed `reconnect_lifecycle_pending` result before backup, run creation or stock
launch. Both reconnect slots are ordered last, so the sequential campaign can
attempt the 22 currently supported baseline/idle/perturbation runs first. It
then stops at the first reconnect, reporting one pending attempt and one
unattempted reconnect; it cannot mislabel a one-session run as either required
observation. The 24-run evidence gate
therefore remains pending until a future in-scope implementation can prove
first-session cleanup, a distinct second generation and both exact boundaries.

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
  -AppManifestPath "D:\Steam\steamapps\appmanifest_70.acf" `
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

The committed metadata-only evidence file is permitted only after 24 accepted
runs, all three maps, at least 1,000 decoded S2C sequenced packets, at least 24
exact boundaries, one non-conflicting candidate and green version/isolation/
restoration gates. Before that,
`docs/evidence/GOLDSRC_STOCK_RUNTIME_FIRST_OBSERVATIONS.json` must remain absent.
This checkout currently claims no accepted real run and does not fabricate the
file.

If the gate passes, the evidence file has one exact allowlisted schema. It
contains the implementation commit; stock version and dynamic-isolation
profiles; accepted/rejected/incomplete counts; fixed map/scenario ordinals;
sequenced/reassembled/decompressed counts; the complete replay-payload,
observed, delivery, byte, bit, source-sequence, source-size and remaining-bit
cursor; neutral candidate representation, exact bit width and recurrence;
metadata-only transport/replay structural hash sets; and exact
restoration/drift status. Unknown properties fail validation. Raw or
auth-derived hashes, raw/auth bytes, identities, paths, addresses, ports,
configs, process logs, entity values and screenshots are forbidden.

M4.7.1.2—not this milestone—owns runtime message catalog, baseline/entity and
clientdata decoding. Stock usercmd remains M4.7.2.
