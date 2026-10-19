# High-FPS movement: c6d71ecc stop/snap and support audit

## Scope and preserved identities

Worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, original HEAD
`a2db33e83a1ce5c4eb5c13832c95d7bee0d2b8cf`. Existing uncommitted
E33.x, graphics, input and movement changes remain in place. The pre-edit
hash-verified rollback copy contains 1,465 ordinary source files and the
working client, launcher and SDL DLL, under
`manual-artifacts/task-records/movement-stop-snap-c6d71ecc-20261017`.
No staging, commit, push, reset, clean, restore, stash or branch switch.

Authoritative historical run: `c6d71ecc6c94e20810cc241ade1af279`, normal
client, `192.168.0.104:27016`, `maps/crossfire.bsp`, exit 0 / window close.
Exact original ZIP SHA-256:
`A67B7CDBE04EF326225EB37D586367ED2D614C5302BD7B01AE2B87D421564641`.
Exact original movement JSON SHA-256:
`B4DDE930BACEC179EBFAC253AE841702AB5FB2F976DECC8C29E5BC8F63F6A5E0`.
Manifest/report client SHA-256:
`AB306AD9CC864D5872D52AE934CB19AC18E87F7C83D2EEB4DE39DD0C5EC8C5F8`.
This was also the pre-edit working client. Original artifacts were not changed.
The supplied report and evidence JSON are derived analysis, not physics replay.

## A: first local stop and later correction

### Historical observations

Incident ordinal 6 is automatic, not F8. Its one-second bin has 116
controlled/alive frames, FPS-equivalent 115.940812, maximum interval 16.0812 ms,
and no interval over 20 ms. This is not proof of uniform presentation pacing.

| Frame/source | Physical/history observation |
| --- | --- |
| 3426 / 716 | Nonzero velocity approximately (-2.2001,336.1110,-7.8167). `collision_blocked` is a camera selection reason. |
| 3427 / 717 | Exact carrier-derived anchor 1288, authoritative velocity (10.625,326.125,0); replay command 1289 once. First observed predicted velocity zero. |
| 3428–3431 / 717 | Further commands advance; predicted origin and eye remain stopped. The last seed probe is not a fresh stopping trace. |
| 3432 / 718 | Anchor 1291 consumes that suffix, no remaining replay command. Authority velocity (.375,330.25,0), raw matched-boundary correction 19.876743. |

Command input [400,400,0] does not establish a free path. The original lacks
the first stopping slide/step trace, complete MoveVars/physics checkpoint and
the ordered clip planes, so an exact historical causal replay is unavailable.
Read-only Crossfire localization is not a replacement for that missing scene.

### Independently proven defect and production correction

In `goldsrc_local_movement.cpp`, the ordered solver can produce a tangential
binary32 velocity whose plane dot is a tiny negative residual. Repeating the
same projection need not alter it. The former safety branch then erased **all**
components. For example, normal (.0830454826,0,.996545732), entering velocity
(-12.25,330.25,-2), clipped velocity (-12,330.25,1), residual approximately
-5.96046448e-8: an independent 330.25-unit/s tangent was discarded.

The correction retains the original float-negative trigger and second clip.
Only its remaining tiny residual can select an immediately adjacent binary32
velocity. At most 26 neighboring vectors are considered; every retained plane
must accept the candidate in both float and double arithmetic, every component
must obey MoveVars velocity limits, and axes tangent to every plane are
unchanged. The nearest feasible candidate wins deterministically. If none
exists, the conservative stop remains. There are no new collision queries,
origin nudges, hull changes, trace overrides, tolerances or camera thresholds.

The first original-kernel test run failed 30 of 417 assertions (3 cases: 1
passed, 2 failed). The expanded normal-reference controls, with the old
zeroing policy temporarily restored, failed 105 of 965 assertions (6 cases:
2 passed, 4 failed). Reinstating the correction passed all 965 assertions in
6 cases, including subsequent physical progress. These are project-owned
mechanism reproductions, not the actual source-717 replay. Initial failures
and the intermediate synthetic-profile repeated-contact holds are retained;
the latter's endpoint policy was not altered to match reference walking.

### Why `anchor_before_history` occurs

At frame 3432 the requested render sample 32.347268 s precedes command 1291's
32.353701 s deadline by 6.433 ms. No compatible predecessor is retained after
the hard correction. The velocity-backed bridge also correctly rejects this
case: projected interval 59.573398 ms, perpendicular error 2.830651 > 2 units.
Neither the 16/32-unit limits nor the perpendicular bound/duration is enlarged.
The inter-frame 19.876743 displacement and same-time camera correction are
different measurements, even when their numerical values coincide.

Exact deadline-ledger sampling, immutable replay suffixes, generation/life
rules, cache invalidation, conservative corner clearance and 20-ms render
delay remain unchanged. No separate general high-FPS interpolation defect was
established. Server authority still replaces physical history immediately.

## B: retained world-ceiling interception

| Observation | Historical evidence |
| --- | --- |
| 2790–2796 | Previously active support 67, derived ascent about 198–202 units/s; own velocity zero is appropriate. |
| 2797 / source 583 | Command 1011, 20 ms: z -1591.5341796875 to -1587.568359375. World ceiling normal (0,0,-1), distance 1588, source plane 2154. Fraction .8911598128539768 is the actual ceiling intersection. |
| 2800 / source 584 | A new authoritative seed is retried: z -1589.25 to -1586.2470703125, fraction .416260162601626, same real ceiling. |
| 2805 / source 585 | Newly observed reversal replaces ascent with -200.6685878962536 units/s. Anchor 1013 and one replay command recover in this update. |

Read-only map SHA-256
`6222243E0839022F3041E6B9E97776CE54D0CF87D4C3810E627B8B2815B555F5`
confirms compatible raw-plane and standing-hull empty-start/solid-end geometry.
The run does not retain a BSP content hash: this local map is read-only
localization, not hash-bound proof of the original run's map bytes or full scene.
The failure snapshot alone has no support entity; its identity must not be
backfilled from the later successful frame. Exact server pusher transforms
and blocked/crush decisions remain unavailable.

Failing atomically on this blocked carry is the established D4/E32 contract,
not a proven defective trace. Fractions are not converted to 1 or no-hit.
The 73.048-ms inactive span is between frame observations, not exact query
execution time; sticky frames do not represent eight distinct failed sweeps.
The source584 retry and source585 reversal demonstrate recovery already works.
There is no justified B physics patch or honest pre-fix-red B defect claim.

The existing owned ceiling regression is extended with two distinct blocked
seeds, unchanged rejected state signatures, fresh query endpoints/fractions,
then immediate reversal recovery to independently expected z=120.75. Free
carry, standing/duck, support removal/reuse, stop, jump/step-off and world-floor
handoff retain their existing negative controls.

## Diagnostic semantics and missing link

The opt-in report adds a bounded collection named
`selected_slide_velocity_stops`: the first nonzero-to-zero operation from the
**selected** slide candidate, not an unrelated seed probe and not final
canonical velocity after gravity/post-think. A discarded direct/step candidate
does not publish its tentative event. It owns the actual query, hit/plane/hull,
before/after values, immutable command and simulation interval. Publication
observation time, command deadline, svc-time and simulation time are explicit
separate clocks; nested query wall time is not invented.
`before_velocity` is the selected bump's input velocity; `after_velocity` is
its zero result. In a `tiny_residual` event it is not the intermediate operand
after the second projection. The reason and retained planes localize that
branch, but the report must not call its bump input an observed immediate
pre-zero residual. No complete instruction-level replay checkpoint is claimed.

Events include source/scene identity and freshness, history range and camera
predecessor decision. Sticky duplicate snapshots are deduplicated. The pool
holds the latest 16 events with explicit eviction/invalid counters. To retain
the strict 8-MiB budget, the recent ring changes minimally from 968 to 960
frames (four seconds at 240 Hz); eight incident windows of 512 frames and the
16-MiB encoded limit remain unchanged. No full collision journal, per-frame
flush, readback or live capture is introduced. Diagnostics Off does not reserve
this pool. An evicted stopping operation remains explicitly unavailable, not
reconstructed from a later sticky field. The pool is global latest-16, not
frozen per F8 incident; subsequent real wall stops can evict an earlier event.
F8 does not promise complete missing-link coverage when eviction is reported.

## Verification and result boundaries

Final post-edit build/test counts, performance observations and executable
identities are recorded below. `prediction_active`
alone is not an endpoint/velocity or smoothness assertion. Tests separately
cover physical displacement, tangent velocity, genuine walls/ceilings,
history/camera behavior and lifecycle rejection.

Requested primary result `high_fps_movement_stop_snap_support_corrected`:
**not established for the historical run; partial offline result only**.
A's independently reproduced mechanism is corrected, its exact incident link
remains unconfirmed; B's reported obstruction/recovery is classified and
protected, not bypassed.

| Required result field | A | B |
| --- | --- | --- |
| root_cause | Proven tiny-residual tangent erasure in production source; exact historical first stopping operation unknown. | Recorded ceiling interception and correct fail-closed carry; no wrong operation demonstrated. |
| reproduction_scope | Owned synthetic mechanism and normal reference-profile controls, not exact LAN replay. | Source-backed blocked/retry/reversal invariant, not complete server pusher replay. |
| production_fix | Bounded velocity repair plus selected-operation diagnostics. | No collision weakening or speculative physics patch; stronger recovery regression. |
| offline_verification | Focused red/green and final gates below. | Existing and extended carry controls plus final gates below. |
| historical_run_verification | Exact original identities/timeline verified; causal link to corrected branch unconfirmed. | Two blocked attempts and same-update observed-reversal recovery verified from retained evidence. |
| manual_status | not_run | not_run |

New agent live/Steam/HLDS/capture sessions: **0**. Server, bots, maps, stock
assets, auth, rates, VSync, MSAA and resolution were not changed.
New build `manual_status=not_run` for both A and B. Manual handoff uses the
existing normal launcher and `192.168.0.104:27016`, retaining the diagnostic
checkbox. Only residual jerks need F8: the new missing-link fields identify
the selected stopping operation. No full arsenal retest is requested.

## Performance observations

Final measurements use the preserved old full-test executable and final new
full-test executable in old/new/new/old order, identical seed 424242 and warmed
E32 fixture (32 warm-up, 128 measured commands), with no compiler/linker/MSBuild
or CMake process present. The earlier `performance-pre-edit-baseline.log` is
not used: later process auditing could not establish its compiler-idle scope.
Final raw logs and both executable hashes are retained in the task record.

At 32 actors, diagnostics Off command-harness p95 ranges 294.4–299.8 us old
versus 314.1–322.9 us new; p99 333.7–374.5 versus 457.8–484.0 us. Diagnostics
On p95 ranges 305.2–382.1 versus 304.2–322.9 us, p99 342.2–408.3 versus
351.1–411.0 us. These retain observed variance and the modest Off increase;
they are not proof of no regression or of a LAN FPS improvement. The harness
includes assertions, not Studio/GPU/server work or end-to-end presentation.

All actor/mode runs retain 480 kernel traces, 80 replayed commands, zero raw
error, zero same-time camera jump, zero fallback and scratch high-water
7,340,144 bytes. There is no extra collision-query mechanism in the correction;
the rare velocity repair examines at most 26 neighbors against five planes.
Collection-only On p99 is 1.5–1.8 us old and 1.6–1.7 us new; Off p99 0.1 us.
This microbenchmark does not measure every live-stage capture or event encode.

Actual Win32 full-capacity diagnostic control: frame record 1,656 bytes,
event record 584 bytes, event pool 9,344 bytes, total retained 8,383,296 bytes
(below 8,388,608), dense encoded JSON 11,871,580 bytes (below 16,777,216).

## Retained failures and retries

No failed run is relabeled as final acceptance. Logs retain the original
30-assertion red reproduction, 105-assertion old-policy comparison and the
synthetic endpoint-policy mismatch. Initial full builds caught the new strict
memory-bound assertion and an optional-clock conversion; both were fixed
without raising limits. Initial core acceptance caught an empty-report size
regression; the original <4096 assertion remains, and empty extensions are
omitted instead. A guessed step-route test oracle failed four assertions;
it now independently checks committed step count zero and the selected direct
route, rather than changing production route selection.
The first complete Release attempt executed all 3,851 entries but timed out
the pre-existing E28.2 close-verdict reporting fixture at its unchanged
180-second limit. Its isolated rerun passed all 32 scenarios in 138.76 seconds.
That CPU-intensive repeated-AST/child-process fixture is now `RUN_SERIAL`
within CTest; assertions, cases and timeout are unchanged. This is test
scheduling only, not a reduction of game load or a prediction/FPS fix. The
entire final suite is rerun after the scheduling change; the initial timeout
and isolated output remain retained.

During orchestration an explicit test build overlapped the task-owned ALL
build in the same cache. Only that verified duplicate process tree was stopped.
One linker-identified corrupt `test_satchel_world.obj` was copied into evidence
and removed from its exact build path, then rebuilt. No broad clean, source
deletion or unrelated build termination occurred. The old-test-binary focused
invocation is retained but is not counted as new-feature verification.

Final Release ALL build is `build-release-final-04.log`; after the last
test-only oracle correction, `build-release-tests-final-05.log` rebuilds both
drivers. Final Debug build is `build-debug-final-01.log`. Source/API and
working-binary rollback copies remain available; original run hashes are
unchanged. The unchanged launcher source was not intentionally modified: ALL
incidentally relinked it, so the original verified launcher bytes were put back
for delivery, with the incidental binary retained separately in evidence.

## Final acceptance gates

| Gate | Cases/entries | Assertions | Skips / result |
| --- | --- | --- | --- |
| Focused normal Release, A/B + selected-operation reporting | 24 | 20,609 | 0; passed |
| Same focused normal Debug | 24 | 20,609 | 0; passed |
| Debug complete compact host/history/camera/lift driver | 102 | 515,620 | 0; passed |
| Debug movement/effects driver | 17 | 24,518 | 0; passed |
| ASan Debug movement/effects driver | 17 | 24,518 | 0; passed with child-only runtime path |
| HL1-OFF API / alternate module aggregate | 455 total, 454 passed | 410,328 | 1 explicit read-only installed-sky opt-in skip |
| Final core CTest, including architecture boundary/alias/graph guards | 45 | Per-driver counts above | 0 failed; passed |
| Normal Debug Null bootstrap | 1 process control | Not a Catch case | exit 0, one frame |
| Full final normal Release CTest | 3,851 entries: 3,779 passed | Separate focused counts above | 72 explicit skips, 0 failures; 463.75 s |
| Audited Release post-checks: version/help/Null, loopback move self-test, reporting contracts, no-stock wrapper and CheckOnly/SourceOnly | Script exit 0 | Not conflated with Catch counts | passed; live sessions 0 |

Normal Release and Debug use VS2022/v143/Win32 with warnings as errors. The
existing independent core cache has `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` and
warnings-as-errors Off (unchanged); its result is not presented as a second
warnings-as-errors build. The ASan cache builds the affected movement-focused
target, not a sanitized complete graphical-client acceptance. Default child
PATH fails loader initialization with 0xC0000135; adding the matching x86 MSVC
runtime to that child only succeeds with exit 0, empty stderr and all 17 cases.
The previous broader loader blocker is not reclassified as a full-client pass.
The 72 full-suite skips are explicit installed-resource/provider, filesystem
capability, interactive SDL capture/focus and opt-in WFP gates, not successful
tests. Exact skipped names are retained in `release-offline-summary-final.json`.
Actual project-owned OpenGL effects/camera controls passed; no graphics setting
was lowered. Final suite and post-check evidence is
`release-offline-suite-final-02.log` with its matching preserved LastTest log.
All 1,465 saved source hashes remain valid and every original source path
exists. This task changes 12 existing files and adds the numeric event header,
tangent regression file and this report. HEAD/branch and empty index are
unchanged; final `git diff --check` passes (only the pre-existing script EOL
notice). The SDK remains pinned to
`b1b5cf5892918535619b2937bb927e46cb097ba1`.

Ready normal client:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`
SHA-256 `F7829D7374522CE96548FA72A362ED75985E6FCBDA959940639F496CB6CE0715`.
Delivered unchanged launcher:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\HLClientLauncher.exe`
SHA-256 `4954BD85FF7B661D0AE9CFDC807CD01141AA90D0FCEE2E1D08F73C2ECEDD5688`.
GUI source resolves `hlclient.exe` beside its own module, not a test driver or
old worktree; that sibling is the client hashed above. No new configuration or
mandatory console command is required. New graphical/manual launch is not run.
