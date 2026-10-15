# E28: Half-Life weapon-suite integration and peer disconnect

Task: HLC-E28-WEAPON-SUITE-INTEGRATION-AND-PEER-DISCONNECT.
Date: 2026-10-09. Authoritative worktree
`D:/DEV/CPP/HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`.

Historical offline handoff completion gate: **Blocked pending exact historical disconnect cause**.
`peer_disconnect_root_cause=not_established`.
`peer_disconnect_fix=not_verified` for the exact historical failure.
Known inactive-userinfo compatibility and lifecycle fixes are independently
testable; they are not a recovered historical packet or a new live reproduction.
Final automatic results and Release identity are recorded below.

Current post-manual audit (2026-10-09): **disconnect behavior accepted in the
reported scope**, `manual=passed`, `attribution=user_report`, not full E28 manual
acceptance. The unavailable old raw packet no longer blocks this scoped current
acceptance. Historical exact cause remains unknown; normal close has a separate
source-supported graceful-disconnect gap, and the retained runner verdict has a
reporting gap. See the appended post-manual audit and
[HL1_WEAPON_SUITE_E28_POST_MANUAL.json](evidence/HL1_WEAPON_SUITE_E28_POST_MANUAL.json).

## Preflight, acceptance and preservation

Git root/branch/HEAD/status/diff/worktrees inspected; index empty, large prior
modified/untracked implementation preserved. No applicable AGENTS found in
the worktree or its checked parents. Other registered worktrees not changed.
No hlclient/HLDS/MSBuild/cl process occupied outputs at preflight.
Pinned SDK remains `b1b5cf5892918535619b2937bb927e46cb097ba1`.
Existing VS2022/Win32, Debug/Release/core-only/ASan configurations retained.

The existing preparation/runner profile is
`steam-hlds-10210-no-mode-banner-v1`. Historical version observation
`ededd068a0444ff497d98204eacec8a4` records protocol 48, server engine 1.1.2.2,
build 10210, launcher 4.1.1.1 and Steam build 15961492. Current read-only E28
binary hashes are `hlds.exe`:
`BE96B878E5F4BE6BDD4E1AD21603C570D8AB4238542F36A7F84C4AB573CCCE8C`,
and `swds.dll`:
`64B6872F9C2875EA7DA88EB694A34E6E46F2AAC7CB35FB436744F462611C03AE`.
These current hashes are not retroactively assigned to run a9ccb57e, whose
manifest retained process/profile evidence rather than per-binary hashes.
No SDK update or stock binary change was made.

Accepted E27.1 client SHA matched actual Release at preflight:
`07BCCCB4A3E55607C3F0E5AD335E2FD461795EA6951954EA922CD22451E8B4C3`.
User reported "Протестував все ок";
`manual=passed`, `attribution=user_report`,
`scope=Snark blood-pop correction manual scenario`.
Historical rejected pop and E27.1 not-run-before-test history remain in E27.
This does not upgrade size/FPS/drop density/motion/sockets to stock-exact or
infer acceptance for E18, Satchel, water, platforms, reconnect or disconnect.

Before overwrite, 1283 ordinary source/config/docs/tests were copied and
SHA-verified, plus 34 existing Release files (49,001,987 bytes) in a rollback
runtime set. Source/rollback manifests reside at
`manual-artifacts/task-records/e28-weapon-suite-20261009/`.
Rollback is preserved data, not applied to the working checkout.
No old outputs were cleaned; no Valve assets/binaries/captures were staged.

Fresh relevant accepted-baseline run: 611 Catch cases, 592 passed/19 skipped,
210610 assertions passed. The filter `[server-audio]` matched no cases;
actual audio tests use other registered tags/names and are separately included
in the new E28 tests/final suite. Historical 3183/56 was not reused as a new run.

## Source and production inventory before edits

All E13–E27.1 stages are present in ordinary source and CMake. No cross-worktree
implementation import was needed. The detailed14-entry machine-readable
inventory is [HL1_WEAPON_SUITE_E28.json](evidence/HL1_WEAPON_SUITE_E28.json).

| Stage | Actual production work | Tests/report |
| --- | --- | --- |
| E13 | remote_player/lifecycle projection, module remote_entities/remote_player/world_entity, approved Studio materializer | remote-player/death/respawn tests; E13 record |
| E14 | server_messages/multiplayer stage_record/commit_record/HUD/player directory | multiplayer/runtime tests; E14 record |
| E15–18 | local controller/firearm profiles, weapon visuals/audio, remote effects/viewmodel/camera | MP5/M203/Shotgun/Python suites and records |
| E19–20 | Crossbow/RPG profiles, generic server entities, RPG trail profile | Crossbow/RPG local/remote/entity/assets tests and records |
| E21–22 | Gauss/Egon module command/beam/audio/pose/lifetime work | dedicated Gauss/Egon tests and records |
| E23 | Hornet presentation/audio/recharge projection, generic server projectiles | Hornet suites and record |
| E24 | immutable command prime/release, generic server grenade/TE/audio | Hand Grenade suites and record |
| E25 | real satchel_presentation, dual model variants, server iuser1/radio state | Satchel suites and record |
| E26 | real tripmine_presentation, placement trace, deployed v_tripmine/body3/sequence7, server ENTITY_BEAM | Tripmine suites and record |
| E27–27.1 | real snark_presentation, original blood sprites/palette, shared TE/inferred-sound profile | Snark/temporary-effect/importer/GL suites and record |

Normal wiring: app creates HalfLifeClientModule→GameClientHost, binds imported
models, submits immutable commands, samples local visuals/audio/beam requests;
RuntimeReplaySession publishes committed entities/events/temporary observations.
Shared RuntimeReplayLocalAssets/renderer/audio execute the neutral outputs.
Neither a report nor precache alone was counted as executed production output.

Dependency boundary remains CMake-controlled: normal `hlclient` is the
composition target that links `hlclient::game_halflife`; that module depends on
`hlclient::game_api` and approved neutral mechanisms. The static
`hlclient_game_client_host` publicly depends only on the API, not Half-Life.
With `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`, the concrete module target/link is
absent; the alternate test module still uses the same host/API/mechanisms.

Roster: IDs 1 Crowbar, 2 Glock, 3 Python, 4 MP5, 6 Crossbow, 7 Shotgun,
8 RPG, 9 Gauss, 10 Egon, 11 Hornet, 12 Hand Grenade, 13 Tripmine,
14 Satchel, 15 Snark. Reserved ID 5 is excluded.
Ammo slots/model/event indices remain session-assigned, not semantic constants.
Manifest fields distinguish source/wiring, synthetic integration, approved
asset checks, historical user acceptance and unverified stock parity.

## P0 historical evidence and precise unresolved boundary

Immutable failed run `a9ccb57ef86349fdb708c447db4316f4`:
B PID6704 completed normally, exit0/close=requested; A PID38656 subsequently
exited2 on protocol_decoder/service_dispatch/decoder_failed/
user_info_update_failed, opcode13, record2381, source2393, payload350,
last committed publication2380. Server log reports B disconnected.
Individual HLDS/relay/guard exit codes were not retained independently.
Cleanup followed A's genuine error: owned_process_cleanup=exact,
scoped_restoration=scoped_exact, cleanup_error_count0. Fast mode is not full-tree
attestation. No orphan processes were observed in the historical record.

Retained software audio output spans are A 4679520 / 48000 = 97.49 seconds,
B 1803840 / 48000 = 37.58 seconds, a 59.91-second difference. The existing
backend continues rendering while focus-muted, so this is not a focus-pause
counter artifact. It is nevertheless output-span evidence, not exact wall-clock
ordering or proof of an `sv_timeout` setting. B reports close requested; A reports
close not requested. The manual repro therefore allows about 70 seconds after
B closure/server directory departure rather than a short 45-second session.

The old diagnostics mislabeled nested parser position as message start.
The retained failure_byte55 means outer reader consumed opcode at54.
Old message_start_byte60 was actually the nested string checkpoint54+6.
With the retained bounds/default limits this narrows rejection to
invalid_info_string/missing_leading_separator. It cannot distinguish EMPTY
userinfo with a retained positive ID from NONEMPTY malformed info without its
initial backslash. The latest relevant run directories contain JSON/redacted
logs only, no raw payload/record/capture to recover that distinction.

Older crossfire/unlimited run `9eda212791c94571bd641259b4915089` repeats
userinfo failure/A2/B0. Newer `4d9ea175de3e4a59947ea8356428af55` and
`aaa47ea11e4845c6ab157897a005b30f` have A0/B0 and successful restore but lack
independent voluntary-B-leave action/order evidence; they are not disconnect
fix proof. The pinned SDK ClientDisconnect SayText path is real, but opcode13
is the retained first error. SayText was not declared the root cause.

## Scoped production changes and failing-before evidence

1. Runtime-only inactive userinfo accepts exact empty string, slot0..31,
   nonnegative private int32 and16-byte suffix. Sign-on parser unchanged.
   Negative IDs, malformed nonempty info, truncation and unknown continuation
   stay fatal/transactional. The candidate fixture has54 valid preceding nops
   and matches the old safe cursor signature, but is explicitly independently
   authored—not captured record2381.
2. Correct userinfo cursor meanings and retain typed error/string-error,
   bounded length/empty/private-ID-presence booleans. Summary/publication emits
   no raw IDs, keys/values, private text, auth bytes or suffix hashes.
3. Registered SayText validates entity byte, negotiated client bounds, body2..255
   and a single final NUL. Parsed content is not executed, formatted, retained
   or presented. This is strict independent framing support, not a chat UI.
4. Explicit committed peer vacancy retires only that entity's playing/pending
   loops, even before first lifetime binding. Finite one-shots/A voices remain.
   Retired-slot state blocks late loop starts until a validated present update;
   generic userinfo update behavior is preserved.
5. Runner active wait now promptly detects actual owned-B nonzero/query failure,
   preserves its exit/cause and uses the existing bounded cleanup path. Normal
   B0 still allows A continuation, as it already did; no new policy flag.
   A/server wait outcomes are observed after peer-triggered Job termination
   using bounded waits; they are cleanup outcomes, not substituted root causes.
6. Explicit player-directory absence/occupant epoch gates remote player/p-model
   and old player scripted effects against retained/new entity publications.
   Unknown directory rows are not mistaken for absent players. New occupant
   presence reopens the slot; independent server world/proxy entities remain
   server-owned. No A netchan/prediction/global asset reset is introduced.
7. No-clip Crowbar HUD omits unavailable clip/reserve counters while canonical
   signed clip−1 remains unchanged. This fixes typed/display state, not ammo.

A further source-confirmed reporting defect was found in
`observe_functional_client_log`: the first loopback join selected a userid,
then *any* later connection marked it lost. In peer mode this could select B
first or falsely report A rejected on B's join. It does not explain A's actual
opcode13/exit2 failure. The scoped peer-mode observer now selects the configured
listener name from the existing `RemoteAudioPeerPlan`, then follows its userid.
It ignores B joins/leaves but retains A departure/reconnect boundaries.
Default single-client lifecycle behavior remains strict and unchanged.

An independently reproduced effect-source defect also existed: a retained raw
B entity could still bind a server custom beam after validated directory
absence, even though Studio/player presentation correctly rejected B.
Existing bound-beam epoch checks protected one path, but not initial binding or
geometry-reset rebind. The neutral application source list now honors the
existing `world_entity` availability intent while the module is bound; raw
transport/publication/history state and server-owned point/world-entity beams
remain unchanged. This is a concrete new ownership defect, not historical
packet-body recovery.
An explicit neutral unavailable-source marker also distinguishes this rejection
from an unknown initial actor awaiting data, so an old unbound beam cannot wait
through departure and attach to a replacement occupant. It carries only owned
entity IDs, is bounded by the validated packet-entity count, and does not
invent network flags or retain module pointers.

Files: runtime_control_decoder.hpp/.cpp; HL server_messages/multiplayer/
client_module/presentation; neutral multiplayer metadata; ServerAudio queue/
consumer; RuntimeReplaySession failure summary; native runner monitoring and
strict evidence publication. All remain ordinary project files.

| Logical set | Changed production files |
| --- | --- |
| Protocol and safe diagnostics | `include/hlclient/goldsrc/runtime_control_decoder.hpp`, `src/goldsrc/runtime_control_decoder.cpp`, `src/goldsrc/runtime_replay_session.cpp` |
| HL semantics and HUD | `src/games/halflife/server_messages.cpp`, `multiplayer.cpp`, `client_module.cpp`, `presentation.cpp` |
| Neutral directory metadata | `include/hlclient/game_api/multiplayer.hpp` |
| Source-attached audio ownership | `include/hlclient/goldsrc/server_audio.hpp`, `src/goldsrc/server_audio.cpp` |
| Neutral effect-source execution | `include/hlclient/app/runtime_replay_local_assets.hpp`, `src/app/runtime_replay_local_assets.cpp` |
| Owned runner and publication | `apps/hlclient_stock_runtime_orchestrator/main.cpp`, `scripts/capture_stock_runtime_state.ps1` |

`CMakeLists.txt` registers the ordinary E28 test files; no required implementation
is hidden in generated/manual-artifact code. Movement/server rules, game assets,
wire opcode numbers and shaders are unchanged; the exact grammar compatibility
extension is described separately above.

Literal saved pre-fix failures:

- Crowbar: 1 failed case, 6 assertions (2 passed/4 failed): typed clip/reserve and
  inappropriate draw counters. Text had dash placeholders; a literal drawn
  numeric−1 was not claimed without evidence.
- Existing E28 regressions: 5 cases, 1 passed/4 failed, 166 assertions/9 failures,
  including inactive userinfo and explicit-loop retirement.
- Peer replay/parser/SayText: 7 cases, 1 passed/6 failed, 193 assertions/8 failures.
- Native monitor contract: 1 failed case, 14 assertions/3 failures, old binary
  lacked monitor/voluntary-leave/crash guarantees.
- Listener-reporting production fixtures: native exit 5; first-A, first-B and
  B-leave cases failed on the old observer, while actual A-leave/reconnect
  controls passed. Saved `pre-fix-listener-*` logs; not a missing-output-key test.
- Retained-absent endpoint: 1 failed Catch case, 50 assertions/4 failures;
  both late initial bind and changed-bound beam survived incorrectly (3 beams
  instead of 2). Server point/world-mine beams and canonical-state controls
  passed. Saved `pre-fix-endpoint-*` output measures actual materialized beam
  instances, not pixel or user-listening evidence.
- After the first source filter, the extended slot-reuse proof still failed:
  2 Catch cases, 1 passed/1 failed; 93 assertions, 89 passed/4 failed. Unknown
  initial source loading passed, but the old unbound/rebound beam returned on
  B replacement. The earlier positive-control setup error was an unsorted
  `[40,50,2]` entity fixture, repaired to the required sorted publication without
  changing assertions. Both logs remain preserved; the explicit marker is the
  subsequent production fix, not a weakening of the replacement assertion.

The later absent-directory resurrection regression follows the confirmed
pre-change source path (ordinary role after a new retained publication); final
passing output is reported separately. No unsupported raw historical body was
fabricated and no exit2 was converted to0.

## Integration coverage and resource ownership

182 directed legal idle transitions use all 14 actual module profiles. Requests
do not mutate selection before authoritative updates. Tests check typed HUD,
viewmodel/profile/sequence/body/FOV, pending action, loop/beam cleanup and
negative cross-profile effects. Each pair also runs the existing normal
approved asset materializer for source and target remote p-models: module
attachment slot, submitted child subinstance, manifest-selected resource ID
and pose identity must agree, and the target resource must change.
The 14 compatible p-model fixtures are independently authored; this is not a
claim that 14 installed p-model GPU renderings were individually measured.

Stateful tests cover Gauss charge, Egon run/stop, Shotgun reload, Python/Crossbow
zoom, grenade prime/holster block, guided RPG block and empty Satchel radio.
Shared assigned9mm/uranium pools, absolute AmmoX+pickup/replay/rejected-shot/
limited-reserve/full-reserve/exhaustible/Hornet cases preserve server authority.
Same command+server timeline is sampled at30/60/144/240FPS with a render stall.

Long scenarios are 900 simulated seconds, not 15 minutes wall-clock sleep:
module/effect/mixer ownership; 18k ordinary publications/history; project-owned
CPU/GPU fixtures through the approved pipeline, with p-model slot reuse and
custom-beam removal. Separate opt-in installed-asset checks are not conflated
with this mandatory no-stock long-run proof. Report measured
output spans/active counts/uploads after warm-up; do not infer leak freedom from
one process memory sample or claim unmeasured private cache allocation totals.

Fixed capacities: mixer128, ServerAudio128/queue512, LocalAudio32pending+
32persistent+32scheduled, shells32/120Hz/250ms catch-up, particle bursts32/output256,
decals64/tracers64, temp sprites32/models32/lights16/drops128/output160,
trails32x256points/output512, ordinary history16, socket mappings64.
Immutable cache persistence is expected. E27.1 profile/assets/dedup unchanged.

## Gap register and completion gate

| Gap | Evidence/impact | Severity / E28 blocker | Next step |
| --- | --- | --- | --- |
| Historical disconnect wire form | String-start failure established; empty vs malformed body unavailable | Historical P0 / does not block new scoped manual acceptance | Retain exact-cause uncertainty; no repeat whole-arsenal test |
| ScreenShake visual | Strict parsed state; no camera consumer | P2 / no | Separate neutral camera execution later |
| Server scorch | Exact decoded decal/BspDecal, not staged/materialized | P2 / no | Approved server-decal execution later |
| Water/bubbles | Parsed/staged, explicitly deferred | P2 / no | Bounded neutral visuals later |
| Dynamic-body hitscan/beam presentation | Normal presentation trace is BSP static only | P2 / no | Separate neutral dynamic query later |
| Ordinary Studio frame clock | Position smoothing exists; ordinary frame stays committed | P2 / no | Render-only sequence clock with continuity guard |
| Numeric stock parity | Particles/sockets/RNG/FOV/pop dynamics accepted only as scoped local-compatible | P2 / no | Specific stock/reference/manual comparison |
| ASan runtime | Current E28 ASan app compiles; fresh `--help` exits0xC0000135 with no output | P2 / capability skip, not pass | Separate exact dependency investigation; no PATH/DLL workaround |
| In-place reconnect | No normal reconnect UI/command route | P2 / no | Distinguish new peer launch from same-session reconnect |

At the original offline handoff, the task remained Blocked under its then-current
gate because the historical cause was not established. The post-manual request
explicitly accepts current behavior separately from that unavailable packet;
the historical cause is not promoted to established. Full E28 manual coverage
is still not claimed, and the newly audited graceful-close/reporting gaps are
separate narrow next work.

## Manual matrix at offline handoff (historical)

- [ ] Two clients connect; HUD/movement/remote player work.
- [ ] One primary/implemented secondary/reload/charge/deploy for every available
  weapon; unavailable weapons explicitly not_run.
- [ ] Glock→MP5→Shotgun→Python→Crossbow→RPG→Gauss→Egon→Hornet→Hand Grenade→
  Satchel→Tripmine→Snark→Crowbar; follow server-allowed/blocked transitions.
- [ ] Shared9mm and uranium counters agree across weapon switches/pickups.
- [ ] Stateful charge/run/reload/zoom/prime/radio transitions clear correctly.
- [ ] Multiple death→respawn: spawn direction/FOV/score/kill feed and no stale
  model/player-loop/beam; world projectiles keep their server lifetime.
- [ ] Accepted E27.1 Snark pop remains unchanged without TE/fallback duplication.
- [ ] Exact disconnect repro: close B normally while A remains open;
  continue moving/firing in A until the server removes B from the directory.
  Allow about 70 seconds after closing B to cover delayed departure; a short
  timed session can miss this boundary. Verify B model/p-model/loops/attachments
  gone and record any A parser/runtime error. Do not close A immediately after B.
- [ ] Close main A and await bounded owned cleanup/restoration.

Default crossfire command (never launched by this task):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Optional existing gentest_mp is suitable for missing arsenal items after its
read-only CheckOnly; no cheats/give-all/map download/server edit is applied.
New E28 build manual=not_run, new live/Steam/HLDS/stock/captures=0,
staging/commit/push none. E29 not started.

## Final verification and Release identity

Final full-suite wrapper and adjunct checks completed successfully after the
last production edit. Historical totals are not substituted.

Full post-last-edit Release CTest completed in 541.78 seconds:
3269 entries, 3213 passed / 56 skipped / 0 failed. The outer build-source
wrapper's trailing launcher/no-stock checks also completed with exit 0.

| Unit / exact scope | Cases or entries | Assertions | Skips / failures |
| --- | --- | --- | --- |
| Full direct `hlclient_tests.exe` Catch summaries | 3148 cases, 3092 passed | 1775177 | 56 / 0 |
| Separate `hlclient_core_api_tests.exe` in one CTest entry | 274 cases | 141233 | 0 / 0 |
| CLI/non-Catch portion | 120 CTest entries | not Catch assertions | 0 failures |
| Overall registered Release run | 3269 CTest entries | not a single assertion count | 56 / 0 |

`catch-aggregate-postlast.json` selects only exact top-level executable blocks
and the final Catch summary per block, excluding nested child output. The focused
30 E28 cases below are a subset, not 30 additional full-suite cases. Compared
with the historical 3239-entry reference, 30 entries were added; no decrease.
Skipped entries are not counted as passed.

After the last PowerShell production edit, Release and Debug each passed all
30 E28 Catch cases / 691334 assertions, with zero failures or skips
(`release-e28-postlast*`, `debug-e28-postlast*`). Actual dispatcher producer:
1 case / 16 assertions; its typed failure passed native JSON-only and stdout
publication, then PowerShell exact-restoration retention
(`postlast-userinfo-*`). Genuine fake-client exit2 and owned cleanup remain
intact. The final selftest now compares the notification count against its
native source (0 for this no-pickup producer, 1 for the older pickup fixture),
not a fabricated pickup or weakened retention check.

Measured 900-second resource bounds after those edits:

| Observation | High-water / completion |
| --- | --- |
| Mixer voices / shells / temporary sprites | 3 / 1 / 17; final voices and shells 0 |
| Beam output | 2 ribbons, Egon max2 and 9 run replacements; Gauss max0 in this long mix, positive Gauss actions covered separately |
| Accepted E27.1 pops / death world survival | 180 / 60 |
| PCM fixture instances | 2, released to fixture-owner references only |
| Ordinary publications / history / lifetimes | 18000 / max16 / max1; reset history0 |
| Fixture CPU imported Studio / sprites | 4 / 2, unchanged through 180 cycles |
| GPU Studio / sprite uploads | warm4/2, final4/2; no repeated immutable upload growth |

These are owning-resource and output bounds, not a process-memory leak proof.
Process memory/heap allocator totals were not instrumented. The independent
no-stock GPU fixture run is separate from the original installed-asset pixel
checks and from human listening.

The first post-edit focused Release run is preserved as `release-e28.log`:
26 cases, 22 passed/4 failed; 576014 assertions, 576003 passed/11 failed.
Failures came from invalid independently authored Egon fixture metadata:
normalization cleared sequence indices 5–8 (FIRE1–4) loop flags required by
`egon_sequences`.
The fixture now supplies valid continuous-loop roles and checks that production
validator explicitly; production Egon code/assertions were not weakened.
Passing reruns are required separately. The first Release build
also stopped on a signed/unsigned test comparison under unchanged `/WX`;
the explicit expected-type repair and subsequent compile passed.

The first post-marker focused run (`release-e28-final.log`) was also retained:
30 cases, 28 passed/2 failed; 582968 assertions with 365 failures. The new remote
matrix's 364 status failures were `culled` under its fixture camera, despite
matching materialized child/resource/pose identities. The returned-frame API
also promotes transient `submitted` to `visible` or `culled`, so its original
literal `submitted` expectation was invalid at that phase. The repaired fixture
places the observer/actor in a valid air-leaf/PVS setup and strengthens the check
to `visible` plus the actual child visibility decision. Culling is not disabled.
The long
scenario passed no optional muzzle observation to `beam_effects`; the actual
Egon executor requires this observation before producing its ribbons. It now
uses a clearly named project-owned muzzle, matching the existing API tests,
with legal authoritative run confirmation/release. Positive beam/ribbon/run
assertions and bounds stay intact. No invented firing event or relaxed authority
is required. These are not final passing totals.
Actual producer→native→PowerShell roundtrip additionally caught new selftest
print rows preceding the required JSON. The native formatter branch now guards
those rows outside runtime-failure fixture mode to preserve JSON-only output;
no output-stripping workaround is used.

Post-last-edit read-only installed-resource group (`release-actual-assets-postlast.log`):
52 Catch cases passed, 421253 assertions passed, exit 0. The first attempt's
two failures were required artifact-directory setup, not asset/render rejection;
the failed log remains preserved and a scoped task output directory was created
before the successful rerun. Coverage uses existing original-resource importer,
model/audio and production GL tests across the roster; it does not claim that
each listed resource has a unique positive pixel/listening test.
Silent real SDL backend startup/output passed (1920 output frames, 480 queued,
zero voices). This is device availability/output-path evidence, not user
listening confirmation or an isolated weapon-sound audibility test.

Current E28 ASan Debug normal `hlclient` target compiled successfully
(`asan-build.log`, exit 0). The freshly built executable's `--help` still exits
`-1073741515` / `0xC0000135`, with zero output bytes
(`asan-help-final-status.json`). Runtime is unavailable, not passed; the earlier
E27.1 probe is not substituted for this current build. No dependency/PATH/DLL
workaround was applied.

Final adjuncts: core-only GameOFF Release 42/42 CTest entries, zero failures or
skips, 18.62 seconds; same-host alternate module and architecture guard passed.
Normal Release/Debug, native runner and current ASan app compile successfully.
Full Release covers registered Null-renderer, prediction and E13–E27.1 cases.
Launcher reference/off, Scenario damage-respawn-check, external-map/no-stock
and exact/incomplete/foreign-report contracts passed without stock launches.
The actual unchanged base command plus `-CheckOnly` passed (exit 0), including
peer-exit/cleanup contracts, and selected the ordinary Release executable.
Fast CheckOnly is not a full game-tree content attestation or live readiness test.

Current Release:
`D:/DEV/CPP/HLC-steamcfg-5e48b7c1/build/bin/Release/hlclient.exe`
(3,934,208 bytes), SHA-256:

```text
73D412ACE92B0EB2C3432B305DB61AA8EBFE9790159D2FDF65C11D290CB2B3C2
```

Current native runner SHA-256:
`F78AE37FB4F9DD9FADF24041AE33EFEAA7A372E047C169E7F155C2EEC7B2CDD7`.
The launcher requires no hidden feature flag, demo executable or manual asset
copy for these changes. Crossfire default and optional existing gentest_mp
remain unchanged.

Final read-only Git checks retain the same HEAD/branch and empty index;
`git diff --check` passed (LF→CRLF advisory notices only). Escalated process
inventory found no running hlclient/HLDS/cmake/MSBuild/cl at handoff.
Final preservation audit (`source-preservation-verified.json`, exit 0):
all 1283 before-source copies present and SHA-matching; zero current original
files missing. The 19 changed originals are exactly the E28-owned source/tests/
scripts/CMake and appended scoped E27.1 acceptance. All 34 rollback files retain
their sizes/hashes, with zero missing/mismatched files. Four historical a9ccb
wrapper/numeric/redacted log hashes remain unchanged. No rollback was applied.
Final Git inventory is 284 entries (163 unstaged, 121 untracked, zero staged),
including preserved earlier work; this is not a new-change count.

Offline-handoff gate before the following manual report: weapon-suite automatic
checks were offline verified, but **whole E28 remained Blocked**.
Historical `peer_disconnect_root_cause=not_established` and
`peer_disconnect_fix=not_verified`; exact candidate compatibility/absence,
ownership and runner fixes are independently offline verified. New build
`manual=not_run`; new live/Steam/HLDS/stock/capture sessions=0;
staging=none, commit=none, push=none. E29 was not started.

## Post-manual run audit — 2026-10-09

Task: HLC-E28-POST-MANUAL-RUN-AUDIT. Read-only code/run audit; only this report
and evidence records were updated. No production edit, build, live/Steam/HLDS/
capture session, timeout change, cleanup action, staging, commit or push.

### Manual acceptance and identity limits

User reports two disconnect scenarios: both clients/TAB initially worked; after
normal B window closure, A remained open; B world/scoreboard presence disappeared
at approximately 70 seconds; A movement/fire/selection continued; the repeat
after Egon/Gauss left no persistent peer loop/attached beam. These four current
behavior scopes are `passed`, `attribution=user_report`. Approximately70s is a
human estimate, not telemetry. Cleanup is attributed to artifacts below, not
to that report. The whole arsenal/E28 manual matrix is not upgraded to passed.

The actual launcher root is `manual-artifacts/research-copy-smoke`. All wrapper
manifests were checked: exactly two record the final E28 runner SHA
`F78AE37FB4F9DD9FADF24041AE33EFEAA7A372E047C169E7F155C2EEC7B2CDD7`.
Both select the normal Release path, crossfire/direct loopback27243,
reference/keyboard-mouse, peer enabled and unlimited duration. Selection is not
based on mtime alone. Neither records the client executable SHA; the user-supplied
`73D412...B2B3C2` and the unchanged current full hash agree, but this is not a
per-run binary-hash attestation.

| Candidate run | Wrapper-owner start UTC / local+03 | A / B / HLDS PID | A / B / runner exit |
| --- | --- | --- | --- |
| `d4d53289a56b4cc9941fee7e6d325529` | 19:30:33.0899032Z /22:30:33.0899032 | 25524 /27076 /38484 | 0 /0 /1 |
| `b85665337b8f4486a0d4b00b36a59d09` | 19:37:15.3860987Z /22:37:15.3860987 | 25556 /18192 /19892 | 0 /0 /1 |

These are strong E28-compatible candidates, not a proved one-to-one mapping of
the user's basic vs Egon/Gauss scenarios. The second retains only about58s of A
continuation after B's last server sample and no server removal row. Do not force
the approximate70s report onto either timeline or infer which weapon repeat was
which. Both A outcomes are `completed`/`primary_error=none`, wait `exited`; B logs
also retain `close=requested`, `cleanup=complete`, completed/none. A's exact close
request row is not retained, so its precise close cause is `not_verified` beyond
normal exit0/application completion.

### Cleanup/restoration, separately for both candidates

Both actual wrappers publish `owned_process_cleanup=exact`,
`cleanup_error_count=0`, `restoration_status=scoped_exact`,
`scoped_restoration_status=scoped_exact`, `publication_status=complete` and
`native_summary_retained_across_restoration=true`. Native cleanup intervals:
d4d +227924→228187ms (263ms); b856 +95658→95941ms (283ms), measured relative to
each native stopwatch, not absolute wall-clock anchors.

The recorded exact Job barriers support zero owned campaign/guard children and
session-owned isolation release under the existing contract. Individual HLDS/
guard exit codes/reasons are absent: actual server status is
`owned_cleanup_without_individual_exit_code`, `server_exit_code=null`.
Do not substitute120 or use today's process inventory as historical evidence.
Route is direct_loopback: no relay session was launched (relay exit not applicable).
Dynamic WFP provider/sublayer/session ownership is supported by source and exact
Job cleanup; there is no separate final filter-enumeration attestation here.

Exact-run `scoped-transaction.json` journals remain in the corresponding Temp
`hlclient-stock-runtime-restore-<runid>` directories. Both have empty managed
entries/created directories and before EntryCount0/TotalBytes0. Thus scoped_exact
is verified for an **empty managed-mutation scope**, not restoration of all
research/Steam files. Both retain `full_tree_restoration=not_verified`,
`full_tree_backup=false`, `full_asset_hash_scan=false`; copied0/hashed657448/tree
scans0. No restoration or isolation operation was rerun during this audit.

### Available timeline and the approximately70-second delay

| Observation / clock | d4d… | b856… |
| --- | --- | --- |
| Startup research log local+03 | 22:30:41 (`L1009014.log`) | 22:37:19 (`L1009016.log`) |
| Runtime observed, native-relative | +5972ms | +3274ms |
| Last B received server sample | server_time48.1292, publication1108 | server_time37.4212, publication843 |
| B window/application closure | request/complete/exit0 retained; exact timestamp unavailable | same |
| A later received server sample | server_time225.642, publication5577 | server_time95.3589, publication2296 |
| Same-server sample continuation | 177.5128s | 57.9377s |
| Independent scheduler continuation | 177.5380495s | 57.9656407s |
| Server departure evidence | disconnected + explicit dropped-B row; time/reason redacted/not retained | no departure/drop/timeout row retained |
| A completed / campaign cleanup | exit0; cleanup+227924→228187ms | exit0; cleanup+95658→95941ms |

Server samples and scheduler rows demonstrate later A activity; they are not
exact B-exit/removal timestamps. Scoped startup research logs contain only two
startup rows; adjacent `L1009015.log`/`L1009017.log` are empty. No retained server
timeout reason, per-run `sv_timeout` configuration, final disconnect packet
receipt, committed-removal timestamp or scoreboard-removal timestamp was found.
Consequently `timeout_confirmed` and `graceful_disconnect_confirmed` are both
not verified; exact cause/duration of removal is `insufficient_evidence`.

Source independently supports `shutdown_ordering_defect_supported`:
SDL quit in `apps/hlclient/main.cpp:4968–4971` breaks before normal `session.update`
(4997–4998/5189). Final6506–6507 performs local session.cancel. Coordinator cancel
(`connect_request_stage.cpp:1345–1349`) enters live-stage cancel/cleanup
(`live_runtime_stage.cpp:957–969,4022–4047`); driver terminal cleanup clears queues,
ACK work and lifetime (`netchan_driver.cpp:2274–2287`). There is no production
outgoing graceful-disconnect builder/queue/send path. The socket is still owned
during final reporting and closes later in destruction (`udp_socket.cpp:86–90`).
This is **missing final disconnect**, not evidence that an already queued packet
was discarded or that a relay closed too early. OS sendto success would also not
prove server receipt/processing. Delayed server detection is a supported
explanation, but neither a timeout reason nor any default timeout number is
asserted for these runs.

### Runner verdict discrepancy and smallest next scope

Preserve both actual outcomes: wrapper `local_server_ready_client_blocked`,
failure `client-map-entry-not-observed`, runner exit1, `bounded_log_capture=incomplete`.
This is not A/B crash: both exits0/application completed, with sustained verified
visuals and thousands of commands/packets/server samples. The native gate fails
the four one-time spawn/sendents transmit+ACK flags (`complete()`2242–2245).
Its fresh parser scans retained bytes and non-monotonically overwrites the flags
(2314–2355,3238–3251). Bounded capture evicts middle progress rows; A source logs
observed4,036,247bytes/34,236lines and1,752,381bytes/14,718lines, both
line_count_truncated. Actual usercmd activation requires sendents ACK, so those
reported false flags cannot be treated as proof the handoff never occurred.
Exact missing-row eviction times are unavailable. The separate redacted16KiB
publication ceiling obscures terminal audit rows, but did not cause the earlier
native verdict. Terminal `client_world_state_published=false` can also come from
cancel resetting the runtime result before final printing; keyboard-mode
completion explicitly bypasses that field.

No production change is made here. The smallest next authorized production task
is a typed intentional-close operation with verified stock Protocol48 grammar,
bounded driver/socket delivery before cancel, and distinct queued/OS-sent/
ack-or-server-observed/deadline receipts. Do not guess a command string or infer
processed from sent. A second narrow reporting fix should latch validated
generation-scoped sign-on milestones across rolling-log eviction and reserve
terminal rows, with an eviction regression; do not equate exit0 with ready or
rewrite recorded verdicts. No `sv_timeout` change, local B hiding, force-kill
replacement, isolation disablement or whole-arsenal retest is proposed.

Current disconnect behavior is accepted in user_report scope despite the unknown
historical opcode13 body. Full E28 manual remains partial/not_run outside that
scope; historical exact cause is still `not_established`. No new manual test is
requested by this audit. E29 is not started. Detailed per-run hashes/provenance
are in the post-manual JSON evidence record.

### E28.1 follow-up (separate evidence)

The subsequent narrow production close/reporting fix is documented in
[GRACEFUL_DISCONNECT_RECEIPTS_E28_1.md](GRACEFUL_DISCONNECT_RECEIPTS_E28_1.md).
Its new build/manual status does not rewrite the historical audit or its runs.
