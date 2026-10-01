# D1 runtime-failure diagnostics and manual handoff

Result: diagnostics ready; historical rejected payload unavailable. This does
not alter message grammar, gameplay, canonical hashing, input cadence, or the
fatal exit-2 policy. It is not a parser fix or a new live proof.

## Current state, not historical restoration

Historical run `d2382267b69343eb9e96461a9d2ba169` remains: 361 attempts,
360 commits, `runtime_record_failed`, client exit 2; native cleanup exact,
application cleanup complete, historical restoration unknown. Its missing
wrapper and rejected bytes cannot establish an exception or parser cause.
Existing confirmations remain `user_report` in
`MANUAL_USER_REPORT_AFTER_G1_20260927.md`; historical files are unchanged.

Read-only inspection found no game/orchestrator/build process before compiling
and no owners of UDP 27242/27243. Research files matched a retained backup
candidate: 4,604 entries, zero content/file-metadata differences. This candidate
is time-correlated, NOT bound to the historical run ID; it cannot upgrade
historical restoration to exact. No recovery or research/Steam writes occurred.
Retained backups were not deleted.

Strict original preparation inventory validation failed due to inventory drift.
The existing functional immutable-content projection passed as
`prepared_projection_content_verified`. Optional
`-ValidateFunctionalResearchRoot` exposes that same read-only preflight,
returning before Steam initialization, active isolation, or stock launches.
It adds no mandatory launch parameter and weakens no active validator.
Active physical/isolation evidence remains pending until the normal runner.

## Owning typed cause

| Boundary | Before | Now |
| --- | --- | --- |
| Service decoder → dispatcher | Nested control error discarded | Owning typed control error retained |
| HL1 handler → GameClientAPI | Text-only module rejection | Neutral owning GameMessageFailure with registration/source/body metadata |
| Replay → live owner | Generic code and limited fields | Owning replay cause, record/transport/life/publication metadata |
| Application → native → PowerShell | Limited terminal fields, last line chosen | First terminal cause, bounded inert metadata, nullable unavailable fields |
| Finalization → launcher | Final close/publication could prevent report | Secondary close isolated, one same-run incomplete fallback, actual report path |

Generic API/host know no Half-Life message meanings; HL1 owns body semantics.
The existing dispatcher bounds each body and passes an immutable borrowed view
valid only for stage_record. Retained failures own their names/context, with no
body/span/string_view. Host validates source/ID/body-size against actual input.
Existing atomic publication applies game state/effects only after the complete
record succeeds. Failed suffixes retain prior inventory/HUD/history commits.

LiveRuntimeStageError::retain_runtime_failure latches the first cause before
teardown. Attempt/commit counts are session-wide, separate from record ordinal,
sequence, generation, life epoch and publication revision. They do not affect
canonical state, respawn networking, movement replay, presentation or TX.

Exported metadata includes domain/stage/codes; generation/life context;
record identity/ordinal, sequence/ACK, reliability/reassembly/encoding;
payload size; record/message start and available exact failure byte/bit cursor;
registered name/ID and expected/actual body size; last publication and counts.
Opcode comes only from decoder-established framing. An unavailable failure
checkpoint is NOT replaced by the message-start cursor. Names use at most 63
inert ASCII characters; codes/numbers/tokens are bounded. Internal explanations,
raw bodies, auth material and arbitrary network text are not added to summary.

No opcode scan/resync, guessed skip, local pickup authority, GPU diagnostic
probe or second game/runtime implementation was introduced.

## Finalization and offline controls

Existing capability-bound atomic publication writes
functional-smoke-wrapper.json. On failure it tries exactly once to write
functional-smoke-wrapper.incomplete.json in the SAME run directory. Neither
path is overwritten. Fallback retains primary native cause, actual cleanup and
restoration plus a separate publication failure. Double failure reports
unavailable, not complete/exact. Launcher accepts only these reported paths for
its own exact run ID; it never selects a latest directory.

Project-owned fixtures use production dispatcher → host → Half-Life module →
formatter: two commits then an unsupported opcode, truncated Health body or
invalid ItemPickup token. They check typed causes/cursors, RX destruction and
atomic inventory/HUD rollback. Production live-owner latch tests secondary
precedence; existing manual-outcome tests keep coverage separate from exit 2.
A held pipe writer checks bounded cancellation. Native JSON and PowerShell
normal/fallback/double-failure publication exercise the same production code.
These are controlled fixtures, NOT historical-run reproduction.

If native staged JSON is missing/invalid, fixed bounded stdout keys retain
the same cause in an explicitly `stdout_partial`, non-evidence-eligible summary.
The known stdout line-count budget grows only by the 41 whitelisted fields;
byte, per-line and token bounds remain enforced. It is not a new collector.

Shared Windows log-reader state now owns its buffer/pipe independently of its
capture owner. Finalization and destruction join only after native thread
termination has signaled within the grace bound; otherwise they safely detach
the owning state and report incomplete. No unbounded join or active-worker
mutex wait remains in finish. Tests exercise held writers, destructor and move
replacement; the pathological OS cancellation-failure branch is code-reviewed,
not claimed as an observed live failure. The affected reader library itself is
instrumented by the existing optional ASan CMake mechanism.

## Final offline gates (2026-09-27)

| Gate | Result |
| --- | --- |
| Affected normal Debug/Release builds | Passed, existing trees reused |
| Focused diagnostics/D1/B1/C/H3/H4/A1/input/replay, Debug/Release/ASan Debug | Each 216 passed + 1 optional-assets skip, 38,589 assertions |
| Additional service/packet decoder fixtures, Debug/Release/ASan | Each 31 passed, 1,591 assertions |
| Normal Debug/Release, ASan and core-only alternate-module/API | Each 93 passed, 1,318 assertions |
| Core-only final build, concrete HL1 OFF, dependency guard | Passed; actual 164 projects and 358 compiler read logs contain zero concrete module references |
| Actual Release OpenGL pickup HUD/B1/A1 controls | 5 passed, 390 assertions; no GL skip |
| Explicit network-free application replay process checks | 8/8 passed |
| Three production failure → real socket-free fake-child exit 2 → native JSON/stdout → PowerShell | Passed, original primary preserved; normal/incomplete/double-failure and unknown restoration checked |
| Existing startup timeout/typed early-exit fixture | Passed; stock launch absent |
| Existing launcher no-stock fixtures | Passed exit 0/23/9/1/2; exact/incomplete same-run report and foreign-path rejection |
| Absolute launcher CheckOnly, reference/off and damage-respawn configuration | Passed, no runner/game/Steam/WFP/socket started |
| Historical/source preservation, diff/new-file whitespace | Passed; 137 preserved dirty/untracked source snapshots, seven historical artifact hashes unchanged |

All Catch runs use seed 2532982175. The optional-assets test is skipped without
an opt-in installed game root; no private data is a build dependency. No blanket
CTest/live suite or repeated broad suite was run. Initial compile/preflight
failures remain in their separate logs; MSVC optional-instantiation diagnostics
were resolved by keeping the live-owner test in its existing host test unit,
without changing wire fixtures or expected values. ASan incremental-link
warnings are expected; no sanitizer failure was observed.

Branch/HEAD remain `codex/stock-runtime-campaign-5e48b7c1` /
`ea738a80e5a66a500c245f95b96eed32198e2d66`; index remains empty.
The prechange snapshot and detailed local command logs are under
`manual-artifacts/task-records/runtime-diagnostics-*` and root
`runtime-diagnostics-*`. Required code/tests/docs are ordinary project files.

Final normal `build/bin/Release/hlclient.exe` SHA-256:
`10A124BDDF88D762BFF358C4703389CC8A190AA3161AF3814A1C921035C0B6FD`.
Release/helpers and the existing launcher are prepared. Administrator and
current-port/isolation prerequisites remain checked by the actual user launch.
The historical root cause is unresolved; no parser acceptance was weakened.
New D1 live/manual validation remains `not_run`.

Result: `runtime_failure_diagnostics_ready_root_cause_unresolved`.

The next single user invocation is unchanged:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

New live launches, Steam initialization and WFP activation = 0;
capture/ETW/campaign = not_run; staging/commit/push = none.
