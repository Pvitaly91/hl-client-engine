# Comprehensive diagnostics — verification receipt

The integration and Session f990 receipts below are historical. The latest
**Session de731 camera/ground repair** at the end records the current follow-up;
older binary hashes are not the identity of that newer client.

Verified 2026-10-19 in the authoritative dirty local worktree
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1`. Historic HEAD is not the identity of the
tested executable. The verified pre-change copies, first failures/retries,
final logs, synthetic examples and terminal hash receipt are retained in
`manual-artifacts/task-records/comprehensive-diagnostics-20261018/`.

## Result and limits

```text
comprehensive_multiplayer_diagnostics_integrated=normal_release_opt_in
diagnostics_coverage=production_owners_connected;14_profiles_70_mode_fixture_rows;bounded_detail
movement=existing_exact_witnesses_and_shared_lane_verified_offline
remote=source_sample_pose_support_and_ordinary_studio_verified_offline
weapons=registry_driven_hook_fixtures;live_coverage_not_claimed
world=read_only_bsp_proxy_and_observed_lifecycle;not_a_complete_trigger_journal
wire=actual_command_membership_and_lerp_contract_verified_offline
server_evidence=observed_source_metadata_only;no_invented_execution_or_hit
reproduced_defects_fixed=wire_lerp_zero_despite_actual_reserve;diagnostic_retention_timing_and_clock_ownership
remaining_unverified_causes=live_FPS_remote_lift_prediction_and_hit_registration_incidents
server_visibility_limits=execution_rewind_per_shot_damage_and_hit_miss_ids_unknown
manual=not_run
new_agent_live_Steam_HLDS_sessions=0
staging_commit_push=none
```

No server, saved endpoint, launcher preference, Steam/auth material or gameplay
authority was changed. The previously saved endpoint remains
`45.83.244.193:27020` and its saved `movement_diagnostics=true` preference is
preserved. New configurations remain default OFF. No original-HL parity, zero
latency, guaranteed hit or elimination of all visible jumps is claimed.

The pinned public Valve/ReHLDS contract and the independently reproduced wire
defect are documented in `MULTIPLAYER_USERCMD_TIMELINE_CONTRACT.md`.
`lerp-before.log` retains the pre-fix failure; `lerp-after.log` passes 253
assertions. Final network/full suites exercise the corrected implementation.
Only the actual presentation reserve is sent; no arbitrary configured 100 ms,
zero-interpolation policy or server lag-compensation change was introduced.

## Final verification

All rows below are offline: project-owned/fake peers, actual production CPU/GL
paths or source-only checks. A real GL context is not a live game session.

| Gate | Final result | Evidence file |
| --- | --- | --- |
| Full normal Release build, all targets | PASS | `final-release-build.log` |
| Full Release C++ suite | 3,989 cases: **3,914 passed, 75 skipped**; **4,718,804 assertions passed** | `full-release-suite-final.log` |
| Focused comprehensive Release | **67 cases, 80,909 assertions passed**, no skips | `comprehensive-release-final.log` |
| Focused comprehensive Debug build/tests | **67 cases, 80,906 assertions passed**, no skips | `final-debug-build.log`, `comprehensive-debug-final.log` |
| Network codec/transaction/history/clock, Release | 263 cases: **262 passed, 1 skipped**; 1,401,851 assertions passed | `network-release-final.log` |
| Same network suite, Debug | 263 cases: **262 passed, 1 skipped**; 1,401,829 assertions passed | `network-debug-final.log` |
| Explicit actual-GL/camera controls | 112 cases: **108 passed, 4 skipped**; 908,316 assertions passed; final full suite also covers the updated geometry controls | `opengl-release.log`, final full suite |
| Launcher offline UI, normal Null, three runtime replays, core API, architecture guard | **7/7 passed** | `final-offline-smokes.log` |
| `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`: core-only client and neutral/alternate module | Build PASS; **2/2 core API / boundary tests passed** | `core-only-final-build.log`, `core-only-final-tests.log` |
| Analyzer and actual C++ JSON → PowerShell seam | **316 checks passed** | `analyzer-validated-final.log`, `pipeline-release-v3.json` |
| Native → PS close/attestation/verdict, no stock | **32/32 cases passed**, zero new live sessions | `normal-close-no-stock-final.log` |
| Manual wrapper `-CheckOnly -SourceOnly` | PASS, explicitly no managed game/Steam/WFP/sockets | `final-checkonly.log` |
| `git diff --check` | PASS; existing LF/CRLF conversion warnings only | `final-diff-check.log` |
| ASan runtime probe | **UNAVAILABLE**, loader exit `0xC0000135`; not a pass | `asan-status.json` |
| New-code ASan build/test | **not_run**; runtime installation/DLL workarounds not attempted | `asan-status.json` |
| New manual gameplay / stock comparison | **not_run** | No new live evidence |

Skips are not passes. They include unprovided opt-in original assets/live or
privileged capabilities and unavailable filesystem symlink fixtures. The single
network skip is the file-symlink capability case. Assertion counts are observed
per configuration, not forced equal across timing-dependent fixture branches.
The generated 70-row coverage table retains its original passing snapshot and
does not retroactively borrow assertions from later test runs.

Initial compile warnings/errors and stale-library test failures were retained,
not replaced with passing logs. The transient launcher linker lock, OFF report
size regressions and shadow-variable compile warning were resolved before the
final builds. The synthetic report's first `-RunDirectory` attempt correctly
rejected a nonmatching folder/run identity; explicit `-ReportPath` publication
then passed. The original synthetic JSON was not edited to bypass that check.

## Bounded-cost measurements

Same warm Release fixtures, no serialization or I/O inside measured calls:

| Fixture scope | OFF | ON |
| --- | --- | --- |
| Existing frame collector, 4,000 samples | p95 0.1 µs | p95 1.9 µs; p99 2.1 µs |
| Shared immutable-command observation, 20,000 events | 59.22 ns/event | 79.56 ns/event |
| Production completed-frame observer + accepted-action geometry, 32 actors / two rays, 1,000 samples | p95 0.1 µs | p50 15.8 µs; p95 17.9 µs; p99 22.4 µs; maximum 89.7 µs |

These are CPU fixture scopes, **not** total gameplay overhead, GPU cost,
input-to-photon latency or a live FPS guarantee. SDL/transport/simulation and
other pre-existing game work are outside the geometry fixture. Debug timings
are retained in its log but are not used as Release performance claims.

Storage/publication tests cover the existing local 8 MiB limit, separate bounded
2 MiB shared event component, 16 MiB whole-report limit, 100 ms shared terminal
encoding deadline, independent exact/action/carrier pools and delayed F8 under
5,000 cosmetic events. Encoding may omit detail with explicit counters. One
shared last-completed scene reference has the separate documented asset-lifetime
bound; buffer bytes do not pretend to measure the existing imported asset tree.

## Ready binaries

All paths below are under `D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\`.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `hlclient.exe` | 5,466,112 | `179C37AED917D0ADD3083022B9C922EAF22DBF7207278366451DAB93B776AAEB` |
| `HLClientLauncher.exe` | 286,720 | `5F3A4BDDEE4DCE1D051CC418BF876405369A83016EFFFE3E12D8528F4142466C` |
| `SDL3.dll` | 2,285,568 (unchanged) | `AD833F09FAD8D99EADFDB1A765A76F9F20872A3AB6FADBEF648F09334F2FAC87` |

`final-worktree-receipt.json` records exact final paths, sizes and hashes relative
to the SHA-verified **local-worktree** baseline (`prechange.json`, 1,483 copied
ordinary source/binary files). `before/` and `bin-before/` remain available; no
Git restore/reset/stash/clean was used.

## Examples and next manual evidence

`synthetic-example/external-movement.json` and its 54,211-byte
`multiplayer-analysis.html` are explicitly **synthetic**, run ID
`0123456789abcdef0123456789abcdef`, PID 123. They demonstrate local publication,
not a live server result. Positive/intentional-negative controls are documented
in `MULTIPLAYER_TIMELINE_SCHEMA.md` and the coverage matrix, including exact
frame/GPU ordering, stale pose, authority misuse and world-blocker controls.

Use the four-step user workflow in `COMPREHENSIVE_MULTIPLAYER_DIAGNOSTICS.md`:
one checkbox, normal play, F8 after an incident, normal exit and **Відкрити звіт**.
Unobserved live weapon modes, unmapped server/local clocks, absent per-step IDs,
unknown server execution/rewind and missing causal links stay unavailable.

## Session f990 repair verification — 2026-10-19

The user-played session `f990692ff3d7510f15fd88bb63c7a80e` on the external
server was read only. Its `external-movement.json` SHA-256 remains
`46C409187810DEEAA8D05006980C04D74325F11816E29F9BD1A2543D383C1E87`.
There were no F8 marks, so the retained incidents are not claimed to be exact
user-marked moments. No new live, Steam or HLDS sessions were started.

Four independently reproduced defects were repaired:

- A refreshed rising-pusher trajectory could intersect a previously generated
  contact and suspend prediction at `support_clearance`; an upper-floor world
  contact could similarly fail the first zero-length occupancy query. Recovery
  now proves the original contact/source scene, exact support identity and
  source plane, bounded upward displacement, full final occupancy and swept
  clearance through all other solids. Raw server seeds, jump/duck transitions,
  real ceilings and unrelated entity collisions are not repaired away. Render
  sampling resolves the actual transformed BSP plane instead of applying the
  new velocity to an older contact plane.
- Gradual server-clock phase drift could pin remote rendering to the moving
  oldest history entry after eviction. Only that global history-clamp
  displacement now repairs the clock anchor. Delay, newest clamp, per-actor
  PVS/lifetime holds, teleports, no-interpolation boundaries and immutable
  snapshots are unchanged.
- The analyzer's lowercase-only capability-command parser rejected the actual
  standard MP5 token `weapon_9mmAR`. A separately bounded ASCII command parser
  preserves its case without relaxing capture-reason enum validation. The
  original report now yields all 70 capability rows without being rewritten.
- Game-host diagnostic actions used module reset-map epoch `1` while actual
  commands and completed scenes used canonical map `263`. A diagnostic-only
  receipt provider now qualifies host events with the canonical map in the
  matching network/reset lifetime. Unknown or drifting ownership stays unknown;
  module gameplay identities are unchanged. Old cross-map facts are never
  retrospectively relabelled or joined by timestamp.

All follow-up evidence lives in
`manual-artifacts/task-records/session-f990-fixes-20261019/`. Its new
`prechange.json` covers **1,498 copied and SHA-verified local-worktree files**;
`before/` and `bin-before/` preserve this task's starting state. The older
integration snapshot and prior dirty user changes remain intact. Git staging,
commit, push, reset and cleanup were not used.

The pre-fix evidence is retained, including `lift-before.log`,
`lift-top-before.log`, `remote-before.log`, `identity-before.log` and
`analyzer-before.log`. The first full parallel build failed from compiler/.NET
memory exhaustion; `release-after-build.log` remains a failure. A stale core
test binary also failed in `identity-core-after.log`; rebuilding it produced
`identity-core-fresh-after.log` (34 assertions passed). Neither failed run is
counted as a pass. The final rebuild uses bounded compiler concurrency rather
than changing the machine's pagefile or terminating unrelated programs.

### Follow-up verification gates

| Gate | Result | Evidence file in the follow-up directory |
| --- | --- | --- |
| All normal Release targets | PASS | `release-final-build.log` |
| Full Release C++ suite | **3,998 cases: 3,923 passed, 75 skipped; 4,752,066 assertions passed** | `full-release-final.log` |
| New lift refresh / quantized source-clock / top-handoff controls | **4 cases, 490 assertions passed** | `lift-final-after.log` |
| Complete movement/brush/lift suite | **157 cases, 540,771 assertions passed** | `movement-release-final.log` |
| Remote history, clock, PVS/lifetime and teleport controls | **21 cases, 65,629 assertions passed** | `remote-suite-after.log` |
| Actual command transmission, backup membership and canonical action/frame identity | **3 cases, 189 assertions passed** | `identity-tx-release-final.log` |
| Neutral/alternate game API suite | **510 cases: 509 passed, one skipped; 499,099 assertions passed** | `core-api-release-final.log` |
| Server entity presentation | **203 cases: 190 passed, 13 skipped; 76,490 assertions passed** | `server-presentation-release-final.log` |
| Comprehensive diagnostic storage/geometry/identity controls | **67 cases, 80,906 assertions passed** | `comprehensive-release-final.log` |
| Analyzer including actual C++ JSON | **331 checks passed** | `analyzer-final.log`, `pipeline-release.json` |
| Focused Debug build: movement, remote history and core API | PASS | `debug-final-build.log` |
| Complete Debug movement / brush / lift suite | **157 cases, 540,786 assertions passed** | `movement-debug-final.log` |
| Complete Debug remote-history suite | **21 cases, 65,629 assertions passed** | `remote-debug-final.log` |
| Debug canonical identity, reset, unknown owner and callback lifetime | **one case, 34 assertions passed** | `identity-debug-final.log` |
| Launcher offline UI, normal Null, three built-in replays and architecture guard | **6/6 passed** | `offline-smokes-final.log` |
| `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` | Client and core API build PASS; two-frame Null PASS; canonical identity 34 assertions PASS; boundary guard PASS | `core-only-final-build.log`, `core-only-null-final.log`, `core-only-identity-final.log`, `core-only-boundary-final.log` |
| Original user report re-read without publication or modification | PASS; 70 capability rows; same before/after SHA-256 | `original-report-readonly.json` |
| `git diff --check` | PASS; existing line-ending warnings only | `final-diff-check.log` |

These are offline controls, not stock-game parity or live gameplay evidence.
Skipped original-asset and privileged/unsupported capability controls remain
skips. Timing output collected while other local verification work was active
is retained but is not used for a new FPS or whole-game overhead claim.

The current ready client is
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
**5,470,720 bytes**, SHA-256
`454F67CC3C9BFA23F2BFB0621FD06B3C8F97AB2EE1FF29577DDA8A5EA580038A`.
The launcher and SDL DLL remain byte-identical to the initial integration
receipt. `final-worktree-receipt.json` in the follow-up directory records all
three current binary identities and changes against this task's verified local
baseline, not against the historical Git HEAD.

This follow-up targets the witnessed lift and remote-history mechanisms, not
every possible FPS hitch, hit impulse or server teleport. CPU-stage peaks in
the original log are not proof of a single GPU or logger cause. Live gameplay
confirmation of the new build remains **not_run**; absent server execution,
rewind and exact damage IDs remain unavailable. The earlier ASan loader
limitation (`0xC0000135`) remains unresolved, and no new-code ASan pass is claimed.

## Session de731 camera/ground repair — 2026-10-19

The user requested implementation after analysis of
`de7316dce96ae658f2570352eafc703b` (02:24–02:29 local time). The actual
pre-change client SHA-256 was
`454F67CC3C9BFA23F2BFB0621FD06B3C8F97AB2EE1FF29577DDA8A5EA580038A`.
The new rollback manifest and evidence are under
`manual-artifacts/task-records/session-de731-camera-ground-20261019/`, with
**1,498 ordinary source/binary files copied and SHA-verified** before edits.
No previous dirty changes or earlier task snapshots were removed.

### Scope and reproduced failures

The original session witnesses 7117/7129 had raw corrections32.368/34.995 units
and same-time eye jumps51.043/64.605 units. Both source displacement vectors
were aligned with the newly observed velocity difference over about41.75ms,
followed by two exact retained commands totaling40ms. This is not proof of a
particular weapon, shot or damage event.

The production fake-peer regression independently authors those two displacement
and velocity differences on a real BSP floor. Before correction it failed12 of
808 assertions, with50.767/57.415-unit camera jumps (`camera-before.log`). The
fixture preserves the original server/command values rather than copying the
predicted result as its oracle.

The independent native-ground regression reproduced `ground_flag_disagreement`
for authoritative `ONGROUND` plus upward velocity547.375, without a local
weapon-impulse witness. `ground-before.log` records three cases with two failing
cases/five assertions. The first ground-test build had two test-only type/
immutable-assignment compile errors; its failure log remains retained and was
not counted as a reproduced runtime failure.

### Implemented policy

- The generic, unexplained16-unit camera snap policy is unchanged. Only the
  existing verified velocity lane admits a raw discrepancy up to64 units.
  A new visual residual above32 additionally requires equal requested/selected
  presentation time, equal collision/hull/mode/view-offset context, and the
  bound `raw_error + |delta_velocity| * exact_suffix_seconds + 2`, capped at96.
  Suffix duration is summed in integer milliseconds and must be at most100.
  This avoids confusing a history-time clamp with a velocity correction.
- The bridge changes only camera presentation. Canonical server state, command
  values, actual velocity, collision and wire delivery are unchanged. Repeated
  ACKs retain the original100ms deadline and admissible offset bound. Real
  no-interpolation/setangle/life/pose/PVS discontinuities and occupied or blocked
  paths are not smoothed through. Losing focus/capture cancels this lane only.
- Native v2 can perform the existing short full-hull support probe for an
  authoritative grounded endpoint with upward velocity, even without a local
  predicted weapon effect. Free occupancy, dry contents, real walkable contact
  and valid hull are still required. Velocity is preserved; the next real PM
  command categorizes upward motion as airborne before friction. Legacy v1
  remains strict. No weapon/server-specific condition or extra impulse exists.

Review caught a new raw-pointer lifetime risk before post-fix tests: velocity
delta is now copied while the old history still owns its matched boundary,
before installing the rebased suffix. The first focused run also exposed that
selected/requested owner-time metadata was diagnostic-only. Cheap conversions
of already-owned times are now available with diagnostics OFF too; no clock
read, logging or file I/O was introduced for this check. The same run exposed
two invalid focus-test inputs, repaired to neutral axes rather than relaxing
production input validation. Failed intermediate logs are retained.

The added live-stage ground fixture sends the server endpoint through the real
ACK/rebase owner, then runs the next immutable20ms command. It checks active
prediction without an added fallback, unchanged canonical velocity/flags and
the independent airborne/gravity trajectory. A normal forward-only command
notification is expected; it is not a weapon action. The fixture separately
checks no attack/jump bits, no second notification and no same-time duplicate.
An intermediate assertion incorrectly rejected this legitimate command; its
failed run remains in `focused-final-release.log`, not as a final passing gate.

The first full Release run (`full-release-final.log`) reported 24 failed
assertions in the pre-existing upward post-step test. They required a local
exact command hint even when an authoritative native grounded endpoint had
real BSP support: the obsolete contract behind the requested short fallback.
The test now verifies that absent, unrelated and mismatched hints neither add
velocity nor block this supported server endpoint. For every hint variant it
still rejects an endpoint without support and the strict legacy profile. It
checks unchanged canonical origin/velocity/flags, exact rebase identity and
one gravity step without repeating the impulse. An independent read-only
review found no weakened support, legacy or once-only-impulse control. This
failed run is retained, not relabelled as a pass.

### Camera/ground final verification

All evidence below is in the new `session-de731-camera-ground-20261019`
task directory. The original positive displacement/velocity fixtures were
retained from the failing run. Their final same-time camera displacement is
zero in both directions, with diagnostics OFF and ON; the bounded bridge
converges without extending its original deadline.

| Gate | Result | Evidence file |
| --- | --- | --- |
| All normal Release targets | PASS | `release-complete-build.log` |
| Full Release C++ suite | **4,006 cases: 3,931 passed, 75 skipped; 4,755,637 assertions passed** | `full-release-complete.log` |
| Complete Release movement/brush/lift/post-effect suite | **202 cases, 566,542 assertions passed** | `movement-release-complete.log` |
| Final Debug movement driver build | PASS | `debug-complete-build.log` |
| Complete Debug movement/brush/lift/post-effect suite | **202 cases, 566,571 assertions passed** | `movement-debug-complete.log` |
| Neutral core API post-effect, immutable command and replay controls | **7 cases, 687 assertions passed** | `post-effect-core-release-complete.log` |
| Launcher offline UI, normal Null, three built-in replays and architecture guard | **6/6 passed; same final production client identity** | `offline-smokes-final.log` |
| `git diff --check` | PASS; existing line-ending warnings only | `final-diff-check.log` |

The skipped full-suite controls remain skips, not successful tests. Concurrent
offline verification timing is not used as an FPS or gameplay performance claim.
The original user `external-movement.json` was re-read without modification:
SHA-256 `0018C8DF32ABBA8FC5E88854E8CB4D2BFEA237E95B51DC72E65491CDB9F27263`
is unchanged. Launcher settings retain their original 02:24:06 local write time.

The ready Release client is
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
**5,472,256 bytes**, SHA-256
`D330C784622E0F5A70D1C51FFA7FBF6EEB1758D5FB22E097EAC42C84B2F0B797`.
The launcher remains SHA-256
`5F3A4BDDEE4DCE1D051CC418BF876405369A83016EFFFE3E12D8528F4142466C`
and SDL3 remains
`AD833F09FAD8D99EADFDB1A765A76F9F20872A3AB6FADBEF648F09334F2FAC87`.
`final-worktree-receipt.json` records source/binary changes against this task's
verified dirty-worktree snapshot, not the historical Git HEAD. The current
normal executable is ready for a subsequent user-controlled gameplay test;
these offline gates do not claim new live-session confirmation.

This task does not claim to fix CPU/GPU frame pacing or every unexplained
correction, server teleport or real impact. New manual gameplay, stock parity
and new-code ASan verification are **not_run**; no new live/Steam/HLDS session,
server/settings change, Git staging, commit or push was performed.
