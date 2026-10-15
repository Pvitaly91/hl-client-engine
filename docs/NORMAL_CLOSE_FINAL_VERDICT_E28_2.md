# E28.2 — consistent final verdict after normal close

Task: `HLC-E28-2-FINAL-VERDICT-AFTER-NORMAL-CLOSE`.
Scope: native reporting and PowerShell attestation/classification only. No E29,
arsenal, movement/prediction, Snark, audio/beam, disconnect framing, deadline,
netchan, socket or server changes. No new live/Steam/HLDS/capture sessions.

## Evidence and preserved history

Source run: `01993393570741b3b1b3dc638d7fe2dd`. Its original 31 artifacts are
unchanged. The wrapper's embedded native summary exactly matches the retained
`functional-smoke.staged.json`. Actual launch/current client SHA:
`F8A0050D1DC027B10AE3157EA8BE20CDF284A650D3573F2469C9CD89C846A75C`.
Unlike older E28 candidates, this run has a real launch-time binary binding.

Native exit0, `primary_failure=none`, application `completed/none`; A/B entered
Crossfire and their terminal receipts were ready. A/B exited0 by `window_close`,
sent one protocol disconnect each and observed transport ACK. Explicit server
disconnect confirmation was **not observed**. A's committed absence of entity2
was observed38.6624 ms after B's close intent: `(2037064256533 - 2037063869909)
/ 10000000 * 1000`. This interval is not packet delivery or an explicit server
response. Cleanup exact, restoration scoped_exact, managed files0, whole-tree
verification not_performed, HLDS/guard exits0.

Authentication remains `pending_or_unknown`, scripted coverage `not_evaluated`,
automatic `evidence_eligible=false`, and stock stability unknown/0ms. None of
those fields is promoted to authenticated, scripted verified, whole-tree
restored or globally evidence eligible. Historical wrapper blocked/exit1 is
retained; any new assessment is `retrospective_offline_reclassification`, not
a new live run or a rewrite of the original result.

## Exact causes

1. `observe_functional_client_log()` identified an owned player's
   disconnected/dropped/kicked/rejected lifecycle event as `connection_lost`.
   The native active loop assigned this terminal loss to
   `functional_connection_rejected`. `functional_connection_status()` then
   preferred rejected over actual accepted establishment. The normal server
   disconnect in this run therefore produced `connection_status=rejected`
   alongside true accepted/map/gameplay commitments. It did **not** fail native
   completion: the native orchestrator still exited0 and recorded no failure.
2. `apply_receipt_observation()` correctly restored the historical committed
   `world_published=true` from E28.1 receipts. The client terminal log's false
   value was its cleared current state after cancellation; both refer to
   different phases. PowerShell's keyboard-mouse branch still expected that
   old transient false value. `Assert-OrchestratorValue()` threw
   `Project orchestrator did not attest client-world-state-published=false.`
   The catch retained `$primaryError`, making `$functionalSuccess` false; the
   server-ready fallback chose `local_server_ready_client_blocked`, and the
   final throw yielded managed exit1.

`stable_duration_ms=0` was not the cause: the30s stock stable interval is not
the project manual-completion gate. The launcher propagated the PowerShell
failure rather than manufacturing it.

## Production correction

Native `apply_functional_client_observation()` now stores terminal loss in
`functional_connection_lost`, leaving explicit project rejection separate.
Establishment stays accepted after a normal disconnected/dropped event.
Stock stability still rejects connection loss; actual rejection, nonzero child
exit and runtime failure remain failures. Native JSON/stdout separately expose
`connection_establishment` and `session_terminal_state`; normal receipt-backed
close is `normal_close_completed`, not a connection rejection.

Receipt JSON additionally reports its actual run/role binding and optional
typed counters. It does not change journal schema, commits, packet bytes,
receiver generation or close behavior. The client executable is not rebuilt.

`Assert-ManualNormalCompletionAttestation()` is called by the actual
FunctionalSmoke path. After native owned cleanup and before restoration it
reads staged JSON and launch/process manifests through existing pinned,
size-bounded readers. It verifies native result/exit, accepted connection,
committed map/world/traffic/render facts, exact run/role/PID/creation/hash,
supported network epoch, actual common map epoch, milestone chain and healthy
typed close. Missing/corrupt/foreign/legacy evidence cannot pass from receipt
existence or A exit0 alone. Numeric identities/counters are integral and bounded;
Boolean/fractional/string values and unknown fact bits are rejected.

`Get-ProjectWorldPublicationExpectation()` allows historical true only after
that manual proof. Scripted live paths retain their previous true expectation.
No global assertion deletion, exception suppression or forced exit0 is used.

`Get-FunctionalSmokeFinalVerdict()` is the shared final production predicate
consumed by the wrapper's result and return/throw path. Scenario-specific
success/pending labels remain; normal manual completion selects the existing
`fresh_project_client_live_visual_control_integrated`. Integrity, server,
connection, runtime/parser, child/peer crash and evidence failures retain
failure precedence. The existing A-close-first policy also remains: native
may explicitly stop still-running B after its grace period, with bound map
evidence and exact cleanup. That B is reported as `runner_termination`, never
as a B window-close/protocol send/ACK.

`Get-FunctionalEvidenceEligibility()` unifies the existing Fast, test-health,
peer and explicit manual-timing exclusions across stdout, staged attestation
and wrapper JSON. Normal completion is separate from that eligibility.

Final summary shows establishment, terminal phase, actual committed-world
observation, A/B map/exit, send, transport ACK, explicit confirmation, verdict,
cleanup, scoped restoration/count and whole-tree verification. It does not
claim a committed world for early-close/legacy/missing-world paths.

Final runner-hash collection now precedes that shared classifier and summary.
A hash-read failure adds `orchestrator_hash_publication_failed` to the existing
integrity errors before classification; it cannot publish success with a
nonzero cleanup error count. This additional review finding was not the cause
of the historical run's false blocked result. Its regression executes the
exact narrow production hash block with a throwing offline hash reader and
checks failure verdict, summary and actual child exit1. A source-order guard
also checks hash collection -> classifier -> summary -> wrapper -> publication.

## Regression proof

Sanitized fixture `scripts/tests/normal_close_verdict_e282_fixture.json` contains
selected actual public numeric facts, receipts and binary bindings; no private
userinfo, tickets, raw packet dumps or native asset paths. Production native
codec/parser/predicates/serializer feed production PS attestation, classifier,
summary and actual PowerShell child exit codes.

Pre-fix wrapper probe evaluated the exact production AST expressions: native0,
A/B map true, expectedfalse/actualtrue, exact historical exception, production
blocked result, regression exit1. The native probe separately reproduced
success/ready/worldtrue with rejected establishment and regression exit1.

The first new native fixture attempt crashed with `0xC0000005` at identity
construction using a by-reference captured constexpr string_view. A captureless
literal construction with exactly the same identity removed that fixture fault.
No compiler-internal/ABI/production receipt root cause was established; those
crash attempts are preserved but not counted as the classifier failing proof.
No engine fix or ASan workaround was made for the fixture.

Post-fix cases include normal completion, marker eviction, owned B stop,
server not ready, real rejection, absent map, early close, late A crash,
runtime/parser failure, B crash, cleanup/restoration failure, foreign run/client/
generation, missing/corrupt/truncated/legacy receipts, missing gameplay/world,
null identities, malformed numeric values, invalid fact bits and runner-hash
publication failure:32 actual child cases. Send/ACK/server confirmation and
scoped restoration remain distinct.

## Verification and handoff

Exact final counts, hashes, executed checks and limitations are recorded in
`docs/evidence/NORMAL_CLOSE_FINAL_VERDICT_E28_2.json`. Logs and verified source/
Release rollback manifests are under
`manual-artifacts/task-records/e282-final-verdict-20261010`.

Final verification, after the last production reporting edit:

| Check | Result |
| --- | --- |
| VS2022/Win32 Debug and Release reporting targets | Built successfully: orchestrator and test executable |
| E28.2/E28.1 focused, each configuration |39 cases /1958 assertions passed |
| Full Release offline suite |3309 CTest entries:3253 passed,56 skipped,0 failed; driver exit0 |
| Direct Catch coverage within that suite |3187 discovered,3131 passed,56 skipped;1777135 executed assertions |
| Native -> PS reporting/summary/actual child exit |32 cases passed, including bounded-log eviction and negative controls |
| Core-only Release, Half-Life disabled |42/42 CTest;292 core cases /142940 assertions; architecture guard passed |
| Launcher CheckOnly and no-stock fixture |Passed; reference/off/damage-respawn contracts preserved |
| Source/Release rollback/historical run preservation |1303/34/31 files, zero hash mismatches |
| Final git diff --check / staged files |Passed /0 |

Skipped tests are explicit asset/device/WFP opt-ins or unavailable SDL hidden
capture, symlink/reparse and second-writable-volume capabilities. Mandatory
owned offline fixtures still run. These skips are not claimed as live asset,
real-device audio or interactive input acceptance. The final driver additionally
passed its no-stock launcher and source-only CheckOnly checks after CTest.

The first full suite attempt retained two failures: a policy test still searched
for the old duplicated eligibility expression, and the unchanged suspended
launch test observed immediate Job accounting1 rather than0. The policy test
now imports the same production eligibility helper and checks all32 original
policy combinations; assertions were not relaxed. The suspended-launch source
and assertion are unchanged. Its later bounded cleanup succeeded even in that
failed case, and the isolated retry plus complete retry passed. Exact transient
Job-reference/accounting root cause was not measured; this is not a claimed
production cleanup fix. Initial failed logs remain preserved.

Normal `hlclient.exe` was not rebuilt: its actual SHA remains
`F8A0050D1DC027B10AE3157EA8BE20CDF284A650D3573F2469C9CD89C846A75C`.
The unchanged launcher selects that `build/bin/Release/hlclient.exe` and the
updated Release orchestrator. Runner/source/scripts/fixture hashes are listed
separately in the evidence JSON; this is not a new client-binary build.

Working E28.1 graceful disconnect remains unchanged. ASan loader blocker
`0xC0000135` is outside this task; no new sanitizer runtime claim is made.
Reporting manual acceptance: `not_run` until the user checks the updated path.
New live/Steam/HLDS/capture sessions0; staging/commit/push none.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Narrow user check: connect A/B, close B normally, verify A continues and B is
removed by server state, then close A. Final summary should show accepted
establishment, observed map entries, distinct normal exits/send/ACK/confirmation,
normal-completion verdict and exact scoped cleanup/restoration with count0 —
without the false world-publication exception. No full arsenal retest is needed.
