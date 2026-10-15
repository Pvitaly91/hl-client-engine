# E26 — Tripmine placement, arming and server laser

Status: `tripmine_placement_arming_laser_integrated`; final offline acceptance passed.
`manual=not_run; new_live_sessions=0; staging/commit/push=none`.
E27 has not started. The earlier unlinked **«Протестив»** is not E26 acceptance.
The required sequence remains E25, E26 offline preparation, NEW user E26
manual confirmation, then E27. Existing E25/historical manual records are preserved.

## Read-only recovery audit

Authoritative worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. Index empty;249 prior dirty entries.
Origin is `Pvitaly91/hl-client-engine`. Read-only status, worktree list, all
branches and `git log --all --regexp-ignore-case --grep='E26\|Tripmine'`
found no E26 commit or implementation/completion record.

Seven registered worktrees were inspected only in ordinary source/include/docs/
tests (never their build/SDK trees): `HL-Client-engine`,
`HL-Client-engine-stock-campaign-eb9f297d`, `HLC-drift-aaf84d6e`,
`HLC-profile-3dc1f8eb`, `HLC-steamcfg-36649544`, `HLC-stock-5c4001dc` and this
worktree, all under `D:\DEV\CPP`. No tripfire mapping, profile, E26 tests or
`TRIPMINE_PLACEMENT_ARMING_LASER_E26.md` was found. Neighbor `HL-Client` has
origin `Pvitaly91/hl-client`, a different repository, and was not used.
Nothing was copied/merged from another worktree or historical checkpoint.

E26 is **implemented anew on the actual E25 source**, not recovered.
The existing snapshot mechanism saved1273 hash-verified ordinary project files
under `manual-artifacts/task-records/e26-tripmine-20261008/source/` and
`source-manifest.json`, including the previous camera correction and six
ordinary third-party build/reference support files (no submodule corpus).
Pre-edit Release E22/E24/E25:104 passed,3 explicit optional skips,
11189 assertions. Baseline normal Release SHA-256:
`EC638A47894365C01B330D979851DE5DCB8037A6C01969B9B13B0D189652B083`.

## Pinned clean-room reference

Actual official SDK HEAD is
`b1b5cf5892918535619b2937bb927e46cb097ba1`.
Inspected `dlls/tripmine.cpp`, `dlls/weapons.h`, `dlls/effects.cpp`,
`dlls/effects.h`, `dlls/client.cpp`, `cl_dll/ev_hldm.cpp` and
`network/delta.lst`. No SDK update or copied original implementation body.

- ID13 `weapon_tripmine`; ammo `Trip Mine`, carry max5/default give1,
  no clip, slot4/position2, LIMITINWORLD8 | EXHAUSTIBLE16. No secondary
  or magazine reload. Deploy is DRAW6; holster5. Idle roles0/1 and fidget4.
- The exact session-advertised `events/tripfire.sc` callback is an **attempt**,
  published before server acceptance. The pinned local callback is local-player
  only, uses local view height and a128-unit forward trace with PM hull2.
  `GetHullBounds(2)` explicitly defines a point hull, not this engine's duck
  collision hull ordinal. A hit (`fraction<1`) selects DRAW6, not an invented
  placement animation. It creates no world entity or firing effects.
- Server trace is gun position along view+punch direction for128 units;
  a hit entity must exist and must not have FL_CONVEYOR. No-hit/conveyor
  attempts do not consume ammo. Only server success creates a mine, at contact
  plus normal*8 with normal-derived angles. Primary attempt cadence is.3s.
- Server `monster_tripmine` uses **models/v_tripmine.mdl**, body3,
  WORLD7, frame0/framerate0. Pickup uses the same model with GROUND8/body3.
  There is no `w_tripmine.mdl`. Held model is v_tripmine/body0; peer p-model
  is `models/p_tripmine.mdl`. Normal world creation is FLY/SOLID_NOT,
  becoming BBOX only through server evidence.
- Arming2.5s (spawnflag1:1s), validation of support position/orientation,
  beam creation/break, delayed death/explosion and damage are server-private.
  Invalid support may remove the mine or produce a pickup without an explosion.
  Unlike Satchel, client planter death does not authorize mine removal.
- Server mine deploy/charge/activate WAVs are distinct entity/channel evidence;
  activate uses pitch75. There is no duplicate local deployment cue.
- Beam is custom ENTITY_BEAM/BEAM_ENTPOINT: `sprites/laserbeam.spr`, point
  in origin, end entity in packed skin with attachment0. Server setters:
  width10, RGB0/214/198, scroll255, brightness64. The pinned custom delta
  POST multipliers produce decoded width1 and scroll25.5; opacity64/255.
  The existing generic E22 decoder/render path consumes these values.

## Ownership and production integration

`src/games/halflife/tripmine_presentation.cpp` extends the existing
LocalWeaponPresentationController; `tripmine_profile.hpp` owns the exact
weapon/model/role/cadence semantics. Inventory and HUD use the assigned server
AmmoX slot, not a guessed global index. Zero reserve disallows an exhaustible
selection and suppresses a retained ghost viewmodel until server switching.
A fresh later pickup restores selection normally.

The module stores one owning pending attempt, eight bounded receipts and16
event receipts. Immutable submitted command eye/direction are captured once.
Mapped local event-only callbacks use their source origin plus own observed
view offset; inherited HL player-model pitch is restored only for that source.
Remote events never animate the receiving viewmodel. Duplicate/conflicting/
stale callbacks, focus loss and generation/life/model boundaries are explicit.
Only fresh authoritative ammo consumption confirms a receipt; event/DRAW alone
does not. The1s visual reservation timeout is not a mine arming/fuse timer.
It retires unconfirmed presentation, not server ammo or entities.

GameClientAPI adds synchronous `prepare_presentation(IStaticEffectTrace,now)`
and a narrow neutral `placement_trace` request/result. Normal app preparation
occurs before sampling the weapon. RuntimeReplayLocalAssets borrows an owning
latest committed observation reference, reuses its already imported collision
package and builds the existing BSP/brush/BBOX union. Prediction player hulls
come from exact protocol use_hull, not model dimensions. Receiving actor is
excluded. Missing source/hull/solidity/bounds stays unavailable. The lower
BrushSceneMovementCollision now exposes a zero-size point wrapper around the
same sweep implementation; standing/duck/history/replay are unchanged.
The app target privately reuses `goldsrc_usercmd_session`'s neutral reference
collision builder. No module, native locator, SDL/GL handle or query pointer
is retained through the API. No new renderer, physics engine or loader.

Ordinary server Studio entities preserve model slot, body, sequence, frame,
orientation and origin, independent of weapon life. Existing generic p-model
merge, server spatial audio/pitch, TE_EXPLOSION/TE_SMOKE and E13/E14 handle
all world effects and outcomes. No local mine, laser, trigger, support AI,
arming countdown, owner-death removal, damage or fabricated explosion.

A neutral E22 endpoint-lifetime gap was also addressed: an old beam cannot
reattach after an observed parent removal/model/player-life boundary. At most64
binding receipts retain the resolved target identity; parent loss marks the
old binding broken. A removed/reintroduced beam, changed reference or explicit
new point/sprite geometry can bind afresh. Identical unobserved slot reuse
cannot be proven from missing wire evidence; it is not a universal binary-parity
claim. The renderer still knows no weapon ID, mine name or arming semantics.

## Scope and limits

Pinned local EV checks only fraction<1. This local-compatible profile additionally
suppresses invalid/startsolid/allsolid presentation; server evidence still wins.
FL_CONVEYOR is a server eligibility flag, not fabricated from client model/texture
names. A local hit may therefore show provisional DRAW while the server rejects;
unchanged authoritative ammo/no world entity is not a successful placement.
Coalesced consume+pickup with the same absolute count cannot by itself confirm
a particular attempt. Animation metadata is imported/validated, not guessed
against an arbitrary nine-sequence model. Server timing and exact sound/arming/
damage outcomes remain new-manual-test obligations, not offline/live claims.

Initial read-only v_tripmine import succeeded (46 bones,2 bodyparts,4 submodels,
9 sequences,9 textures,1 skin family). Detailed approved metadata/GL/PCM and
final build/ASan/core-only/regression results are recorded after completion.

Approved read-only control subsequently passed55917 assertions, through the
existing importer, body selection, normal OpenGL world pass and isolated
production mixer for each server sample. Actual v metadata:46 bones,
0 controllers/attachments,2 bodyparts,1 skin; sequences
idle1 91@30, idle2 61@30, arm1 61@60, **place 11@30** (SDK ARM2 role),
fidget101@30, holster16@30, draw16@30, world76@30, ground76@30.
All body values0/1/2/3 are supported/selectable. The profile validates actual
`place` label but never substitutes sequence3 for the event's DRAW6.
p_tripmine has12 bones,0 controllers/attachments,1 bodypart/skin,
idle1frame@30. World sequence metadata contains76 frames; the server's
framerate0 correctly freezes WORLD at its authoritative frame, not a new
client arming animation. No installed bytes were copied into the repository.

Decoded WAV controls: mine_deploy22050Hz mono5801 samples (peak1),
mine_charge11025Hz mono44167 samples (peak1), mine_activate22050Hz mono5913
samples (peak.96875). Each separate ready sample starts one spatial voice and
produces finite nonzero mixed PCM; activate uses server pitch75. This is
import/mix/GPU proof, not user listening or live arming evidence.

Project-owned focused Debug after the latest production edit:19 passed,
1 explicit optional asset skip,322 assertions; includes actual host/API,
world body3, generic remote p-model merge, canonical beam-parent removal
with and without an intervening render frame, reuse, GL pixels, point query
and isolated authority/lifecycle tests. Broader Debug E13–E26/movement/
prediction:640 passed,14 optional skips,450107 assertions. No existing
assertions were weakened; the new synthetic MDL was corrected to have legal
non-aliased animation/geometry blocks, and its BSP/culling/render cameras
were made coherent rather than bypassing strict import or visibility.

Core-only Release `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`:41/41 tests passed,
including architecture guard and the shared point/standing query comparison;
zero concrete Half-Life references in generated core projects. The current
Debug/Release/ASan final totals and ready executable are appended at completion.

Current supported ASan workflow was actually exercised in existing
`build-asan`, Debug/Win32, serial incremental `/MP1`: targets
`hlclient_core_api_tests` and `hlclient` compiled/linked successfully.
`asan_build=passed`; `asan_core_api=blocked_runtime_0xC0000135`;
`asan_application_runtime=blocked_runtime_0xC0000135`.
Both the core API executable and actual `hlclient --renderer null` exited
with signed code-1073741515, unsigned0xC0000135, before useful runtime output.
The old runtime blocker is not fixed and ASan execution is not called passed.
No DLL search/copy/install or unrelated ASan infrastructure repair was done.

Ordinary source scope (besides this record/ARCHITECTURE): game-owned new
`tripmine_profile.hpp` and `tripmine_presentation.cpp`; existing Half-Life
client_module/inventory/presentation/local_weapon_presentation code and headers;
neutral game_api beam_effects/game_client_module/game_client_host declarations
and host dispatch; app main frame preparation and RuntimeReplayLocalAssets;
shared LocalMovementCollision point wrapper; CMake target/test registrations;
new tripmine local/world/approved-assets tests and two owning fixture headers;
existing shared collision test gets the core-only regression. No required
implementation is hidden in artifacts/generated files; fixtures are reproducible
project source. Existing E25 and other dirty edits remain intact.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

NEW manual checklist: pickup/select Tripmine; correct held/peer models and MINES
HUD without clip; place against a suitable wall; DRAW once; mine mounted with
world body3; server deploy/charge/activate sounds; delayed teal laser; cross or
shoot it and observe server explosion/damage/score. Try no-hit/rejected placement
without false ammo/world changes; several mines, planter death/respawn, final
mine retirement and later pickup. Recheck E25 charges/camera, other weapons and
remote death/score; close both windows and allow managed cleanup to finish.
All E26 manual checks start `not_run`. Do not promote the old unlinked report.

## Final acceptance — 2026-10-08

After the last production edit (canonical beam removal at atomic publication):

- VS2022/v143 Win32 normal affected Debug and all Release targets built
  incrementally/serially `/MP1`, with no clean or old build restoration.
  Focused Debug and Release each:19 passed,1 optional installed-resource skip,
  322 assertions /20 cases. Real asset control separately:1 passed,
  55917 assertions, including normal GL body3 and three isolated PCM windows.
- Wider Debug E13–E26/movement/prediction:640 passed,14 explicit optional
  skips,450107 assertions. Existing remote Studio/corpse/respawn, scoreboard,
  p-model, audio, weapon, H3/H4 and E25 clustered-step checks passed.
- Core-only existing `build-core-g1`, Release/gameOFF:41/41 tests,
  architecture guard passed, zero concrete Half-Life project references.
  No installed game/private model/capture is a required fixture dependency.
- **One full final Release offline suite:**3104 passed,52 explicit optional
  skips,0 failures /3156 registered;558.15s. Normal OpenGL application
  startup passed(8.32s); E26 world/beam/reuse/remote-model/query/audio/GL and
  all E13–E25 registrations ran. Optional local resources, SDL/WFP/link
  capabilities are skips, not new live evidence. WFP opt-in remained disabled.
- Subsequent version/help/Null, transport self-test, native/PowerShell summary
  roundtrip and full no-stock launcher fixtures passed. The final log ends
  `Source build/offline verification passed: Release`, exit0.
- Actual unchanged launcher reference/peer/unlimited CheckOnly passed:
  process-launches0, no runner/game/Steam/WFP/socket activation. The wrapper
  selects the exact normal Release below, without a hidden E26 flag or binary
  copy. `git diff --check` passed; index empty, branch/HEAD unchanged.
- ASan build passed, but both core API and Null application runtime still
  terminate0xC0000135 as recorded above. This remains a known runtime skip/
  blocker, not successful ASan execution or an infrastructure fix.

Current Release: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
3892224 bytes; UTC build timestamp2026-10-08T11:08:31. SHA-256:
`804BE96331350629468ADE5EE03A8E4956DC5EC65051E6D60946C650E9ABF2DF`.
Launcher SHA remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.

`E26 manual=not_run; new_live_sessions=0; staging=none; commit=none; push=none`.
No E27 implementation or promotion of the unlinked manual report. Source
snapshot1273 files remains the pre-edit authority;19 existing ordinary files
changed plus8 new E26 source/header/test/record files (27 total in this task),
with no missing pre-existing file. Installed/Steam/server assets/configs,
other worktrees, foreign processes and old builds were not modified/cleaned.

The prepared result is offline integration, not stock-binary parity or a live
mine/arming/damage guarantee. Shared generic ENTITY_BEAM rasterization,
bounded receipt recovery and conservative local invalid/startsolid rejection
remain local-compatible mechanisms. Authoritative surface/support/arming/
beam-break/removal/damage/score behavior is always HLDS; a NEW two-client
manual test is required before E27.

## Follow-up — retained baseline decoder failure, 2026-10-08

New manual report (`attribution=user_report`): intermittent client exit when
placing Tripmines on walls. This is failure evidence, not E26 manual acceptance.
The latest matching run `c857f3d4951244148eea6d2ed48ed31b` started
12:51:12 UTC (15:51 Kyiv), gentest, reference prediction, one client, unlimited;
server PID2072/client PID14568. The previous gentest run
`1f54127136974f8aa9f1c9910569c68c` repeats the same decoder boundary.
Both report exit2, `runtime_record_failed/decoder_failed/schema_mismatch`,
opcode40 (`svc_packetentities`), entity31,
`packet_schema_boundary=baseline_generation_or_schema`; latest record427,
source sequence438, payload414 bytes, message-start cursor296:3,
decoder cursor2371 bits.
Latest log observes active weapon13 and authoritative reserve2→1 before failure.
The prediction coverage verdict and nonfatal unsupported audio sentence are not
the primary failure. The retained redacted diagnostic window does not include
the raw packet/custom bit or entity31's exact baseline descriptors; a newly
created Tripmine beam in a reused ordinary signon slot is the plausible trigger,
not a claimed exact captured-packet replay.

Wrapper cleanup is `exact`, scoped restoration `scoped_exact`, zero errors;
the final report was retained. No target processes remained at inspection.
Fast-mode full-tree restoration is `not_verified`, not upgraded to full proof.
The matching scoped transactions contain no managed-file entries.

The shared decoder previously projected normal/custom common members only for
an intra-message base, while a retained signon entity baseline required exact
category/schema equality. A signon slot is not a permanent entity-type identity.
The fix shares that existing descriptor-checked projection with retained
entity-number baselines: common exact names/offsets/storage retain decoded
values (including angle/time/float values without requantization); absent
members use typed protocol-zero defaults. A narrower inherited integer may use
a temporary default only when the bounded, non-consuming mask proves this exact
member is overwritten and the real decoder validates the complete value/tail.
The signon baseline itself stays immutable and baseline provenance is retained.
Wrong generations, alternate slots, same-category descriptor mismatch and
silent category changes of an existing delta entity remain rejected. No
Half-Life semantics, wire fields, authority, mine timer or exit code are changed.

Pre-edit source snapshot:1281 hash-verified ordinary files in
`manual-artifacts/task-records/e26-baseline-crash-fix-20261008/source/`, with
`source-manifest.json`. Existing dirty work and historical reports are retained.
The fix and its fixtures are ordinary project source, not artifact-only code.
The new build remains `manual=not_run`; new live launches, staging/commit/push0.

### Follow-up verification and handoff

- VS2022/v143 Win32 incremental serial Debug/Release normal client, test/core
  test and Release helper targets built. Focused final Debug and Release each:
  47 passed,1 explicit installed-resource opt-in skip,3784 assertions /48 cases.
  Three new baseline-reuse cases:318 assertions in both core API binaries,
  without a concrete game/module/installed-resource dependency. Tests include
  projection before a malformed suffix, bidirectional reuse, later delta,
  immutable baseline, signed skin conversion, complete/omitted/truncated narrow
  replacements, incompatible type/offset/storage/generation, unsupported custom
  grammar, strict alternate slots and same-category descriptor mismatch.
- Existing `build-core-g1` Release/gameOFF rebuilt:41/41 passed (21.67s),
  architecture guard passed; no concrete Half-Life references in generated
  projects. New decoder regressions are compiled into that same core target.
- One final full Release offline suite after the last production edit:
  **3106 passed,52 explicit skips,1 timeout /3159**,254.93s. Preserve the failed
  result in `release-offline-final.log`; it is not an all-green full-suite claim.
  The old `Production M4.4 CPU scene survives dropped response and ACK
  continuations 20 of 20` hit its unchanged30s limit with only filter/seed output.
  Isolated unchanged-timeout CTest retry passed(1.34s), and direct retry with the
  exact failing RNG seed2650345687 passed42820 assertions. This scenario runs40
  world-spatial-scene UDP/resource/import flows and does not execute the changed
  packet decoder; the timed-out iteration/stage and cause remain unlocalized.
  Scheduling/filesystem pressure is plausible, not proven. No timeout/assertion
  was relaxed, and no second full suite hides the first result.
- Full-suite normal OpenGL deferred-handshake startup passed(8.70s). Existing
  project-owned GL/PCM regressions passed; installed-resource and opt-in real
  SDL playback controls remain explicit skips, not audible/device proof.
- The remaining version/help/Null, fake transport, native/powershell summaries
  and complete no-stock launcher fixtures passed separately after the suite
  wrapper stopped at the timeout. Actual gentest/reference/unlimited CheckOnly
  passed and reports the exact new Release SHA below. No game/runner/Steam API,
  WFP or socket is started by that preflight. Launcher bytes are unchanged.
- ASan normal application rebuilt successfully; offline Null startup still
  exits-1073741515/0xC0000135 before useful output. Runtime remains blocked;
  no ASan infrastructure or DLL installation was changed.
- Source preservation check:1281 snapshot entries,0 missing source files,
  exactly5 changed files (shared decoder, its existing test file, this record,
  ARCHITECTURE and GOLDSRC_ENTITY_SNAPSHOTS). `git diff --check` passed;
  index empty, branch/HEAD unchanged, staging/commit/push none.

New normal Release: `build/bin/Release/hlclient.exe`,3893248 bytes,
UTC2026-10-08T13:21:33.8035477Z. SHA-256:
`508AFEC01246300CD12FBD3D91F3E043B8A9473BFC49BBD2FC7CEC81A47DCE9A`.
This supersedes the pre-fix executable above, not the historical test results.
The existing signed-skin/narrow-member conservative limits remain unchanged;
this is not a universal arbitrary-mod or stock-binary parity claim.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\gentest.bsp" -NoTimeLimit
```

Repeat wall placements, wait for server arming/laser, move away and place more
mines after pickups have freed slots. Confirm no exit2, normal server ammo,
laser/explosion/removal and cleanup after closing the client. No new switches
are needed. `new_build_manual=not_run; new_live_sessions=0; E27=not_started`.

## Follow-up — jump contact at a foreign wall mine, 2026-10-08

New `attribution=user_report`: jumping tightly beside/onto another player's
wall-mounted mine produces own-player prediction jerks. Latest matching run
`1fd8213be9694d6ca21b67d5691eed69`, gentest_mp/reference/two clients/unlimited,
server23368/A33732/B3660, completed with both exit0 and exact/scoped_exact cleanup.
Peer retained diagnostics report maximum raw prediction error20.746375,
maximum camera correction jump11.263593,12 fallbacks,16 jump presses and
`collision_blocked_frames=0`. Terminal support is server box entity20.
These aggregate maxima do not identify the exact worst collision packet or prove
which interval caused the live jerk. The earlier E25 camera-step correction is
not treated as this measured cause: it changes eye presentation, not physics.

A new owned deterministic reproduction establishes a physical query defect.
The server BBOX slab sweep used `enter > leave` rejection despite treating
box boundaries as free for position tests. It therefore accepted an entry and
exit at the same instant: a corner tangent with no positive-duration solid
interior. For SDK-sized +/-8 mine bounds centered at(0,0,32), the standing
expanded box is x/y[-24,24],z[-12,76]. Rising around its top-left corner and
reversing that route falsely hit X/Z planes; a jump departing a horizontal
corner at t0 falsely clips X. Independent outward-face literal BSP geometry
and every sampled position remain free on those exact paths.

Pre-fix shared query regression:540 failures/14700 assertions (false fraction
.4999995 or0). Normal Half-Life GameClientHost policy plus authoritative
SOLID_BBOX/owner/bounds and real20ms reference commands reproduced376
failures/2737 assertions in2 cases: rising X velocity100→0, descending Z
velocity-208→-8, and accumulated jump/landing state mismatch. Evidence is saved
in `query-repro-before.log` and `physical-repro-before.log` under
`manual-artifacts/task-records/e26-tripmine-jump-20261008/`.

Production fix is only `src/goldsrc/movement/local_movement_collision.cpp`
inside the existing `BrushSceneMovementCollision::trace_shape`: reject
`enter >= leave`. True positive-length penetrations and boundary-to-interior
support still collide. Bounds, owner policy, immutable frame/identity, tolerance
1e-6, plane choice, inside/startsolid/allsolid handling, reference command
semantics, correction/replay, B1/life/ammo/damage and H3 camera routes are
unchanged. No mine-specific name/weapon/asset check enters the shared mechanism.
Accepted zero-thickness source boxes now have no unexpanded point interior;
an explicit test checks that contract while standing/duck expansion still
collides. This is not a universal stock DIST_EPSILON/physics parity claim.

Tests extend existing ordinary files, not a parallel collision engine:
`test_local_movement_collision.cpp` authors an independent BSP floor/wall/box
oracle, checking point/standing/duck, both directions, t0/t.5 tangency, ULP
outside/inside, real penetration/support, eighth-unit translations, immutability
and zero-width point/player behavior. `test_local_player_movement_integration.cpp`
uses the normal Half-Life movement policy and server-observed boxes for world/
foreign owners, upward/downward20ms air ticks and80 commands of jump/BSP-wall
slide/floor landing. Physical states match the independent mine-free BSP
control at every tick, including complete deterministic state signatures.
No old assertion was relaxed; fixture-only Catch INFO macro syntax was repaired.

Pre-edit snapshot1284 hash-verified ordinary source files is under
`manual-artifacts/task-records/e26-tripmine-jump-20261008/source/`.
Final focused Debug and Release each pass9 cases/18229 assertions; the same
shared query matrix in the core API executable passes2 cases/14754 assertions.
Broader Debug movement/prediction/E25/E26/GameAPI:342 passed,3 explicit opt-in
installed-resource skips,305149 assertions. The H3/H4 behavior is exercised
through the existing movement/prediction tests; no standalone `[h3]`/`[h4]`
tag cases exist. Final suite/core-only/ASan/binary handoff is appended below.
New build `manual=not_run`; new live/HLDS/stock/Steam/WFP/capture sessions0,
staging/commit/push none; E27 not started.

Final jump-contact verification (after the last production edit):

- VS2022/v143 Win32 normal Debug and all existing Release/helper targets built
  incrementally/serially. Focused Debug/Release:9 passed,18229 assertions; core
  query matrix2 passed,14754 assertions. Wider Debug342 passed,3 explicit asset
  opt-in skips,305149 assertions. Old box penetration/support, clustered-charge
  camera, movement, authority, life and weapon assertions remain unchanged.
- Existing core-only Release/gameOFF rebuilt:42/42 CTest passed(23.58s), including
  architecture guard and the no-stock map fixture. No installed game resource
  is a required dependency for the new collision/jump regressions.
- One final full Release offline suite:3111 passed,52 explicit capability/
  resource skips,1 failure /3164,317.04s. The single failure was old
  `hlclient_stock_runtime_evidence_pending`: its global `hl`/`hlds` process
  snapshot changed across the read-only verifier. The case already has
  RUN_SERIAL; no assertion/timeout/registration was relaxed. A subsequent
  process observation found3 `hl` processes(PIDs14588/26836/29040); a later
  read-only enumeration no longer found them. Their exact ownership/source
  of transition was not established, and none was terminated by this task.
  Isolated unchanged test passed(1.50s). Preserve initial full-suite failure
  in `release-offline-final.log`, not an all-green full-suite claim.
- Full-suite normal OpenGL deferred-handshake startup passed(7.52s); relevant
  project-owned GL/PCM regressions passed. Installed-resource/device listening/
  WFP opt-ins remain skips or not_run, not positive live evidence. The prior
  M4.4 fake-peer timeout test passed in this suite without weakening it.
- Remaining version/help/Null, fake transport, native/PowerShell summary and
  complete no-stock launcher checks passed separately after the suite wrapper
  stopped at the failure. Actual gentest_mp/reference/peer/unlimited CheckOnly
  passed and selected the exact new normal client below. Launcher/map assets,
  SDK/server/Steam/configuration and other worktrees are unchanged.
- ASan normal application compiled successfully, but offline Null startup still
  exits-1073741515/0xC0000135 before meaningful runtime output. Runtime is not
  confirmed; no DLL/ASan infrastructure repair or acceptance promotion.
- Snapshot1284 files remain present,0 missing; exactly4 changed ordinary files
  (shared collision implementation, its2 existing test files, this task record).
  `git diff --check` passed, branch/HEAD unchanged, index empty.

Prepared normal Release `build/bin/Release/hlclient.exe`,3893248 bytes,
UTC2026-10-08T16:07:19.2803753Z. SHA-256:
`7B4C831957CF5110861230CCE8D17A1C630F59E0EE844F9519C6B0EB2B5E4488`.
No extra feature flag or alternative executable is required.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\gentest_mp.bsp" -RemoteAudioPeer -NoTimeLimit
```

Have B mount a wall mine; use A to approach/jump beside its front/top corners
in both directions, land on top and jump away. Recheck genuine blocking,
ordinary wall sliding, Shift/duck, own/foreign charges, mine laser/explosion,
death/respawn and prior movement. Exact live interval association and absence
of remaining unrelated prediction errors require this fresh user test;
`new_build_manual=not_run; new_live_sessions=0; staging/commit/push=none`.

## New user acceptance before E27 (2026-10-08)

`E26 manual=passed; attribution=user_report`. The subsequent E27 request
explicitly confirms all19/19 required E26 scenarios. This is a new acceptance
record, not a rewrite of the historical build/manual=not_run reports above.
Optional moving-platform/conveyor coverage remains not_run unless separately
reported by the user. E27 itself has not been manually tested.
