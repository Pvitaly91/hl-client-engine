# E25 — Satchel deploy, radio and remote detonation

Status: `satchel_deploy_radio_detonate_integrated`; final offline acceptance passed.
`manual=not_run; new_live_sessions=0; staging/commit/push=none`.

## Source and previous evidence

Authoritative worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`, origin
`https://github.com/Pvitaly91/hl-client-engine.git`.
Index empty; previous E11–E24/BSP work retained. No reset/clean/restore/stash,
rebase/amend, branch change, cleanup or other-worktree edit. Existing snapshot
mechanism copied and SHA-256 verified 1264 ordinary tracked/untracked source
files under `manual-artifacts/task-records/e25-satchel-20261007/source/`, with
`source-manifest.json`. Pre-edit Release `[e24],[e23],[e22],[e16]`: 161 cases,
155 passed, 6 optional skips, 18655 assertions.
Baseline Release SHA: `491745E1770109013F67E6AB7B3ED9AD79E8506B68D37B2739F725404B78B8FD`.

E24 **«Протестував все ок»** is recorded separately in its existing task record:
`manual=passed; attribution=user_report`, for the performed Hand Grenade scenario.
No exact timings, isolated hearing measurements or binary hash are attributed
to that report. Historical preparation/manual=not_run fields remain unchanged.
Server scorch visual remains unsupported.

## Initial audit

| Concern before E25 | Existing support / missing policy |
| --- | --- |
| Satchel catalogue/ammo | generic WeaponList/AmmoX and ownership existed; no Satchel max/no-clip/select-empty profile |
| chargeReady / iuser1 | weapon iuser1 not projected; auxiliary integers 2/3 and E24 floats existed |
| Charge/radio viewmodels | generic server model import existed; local controller lacked two-model transitions; same-weapon model guard rejected the transition |
| Remote p-models | E11 generic server weaponmodel + bone merge already supported the model pair |
| Server Satchel | ordinary BOUNCE/Studio interpolation existed; no new projectile subsystem required |
| Owner field | decoded neutral object field existed, but observation projection did not expose it |
| Audio/explosion/smoke | committed svc_sound, TE_EXPLOSION/TE_SMOKE already existed |
| Retirement | E24 exhaustible grenade policy existed; its timed/cook behavior cannot be used for Satchel |

## Pinned reference facts

Actual official SDK revision verified:
`b1b5cf5892918535619b2937bb927e46cb097ba1`. Read `dlls/satchel.cpp`,
`dlls/ggrenade.cpp`, `dlls/weapons.h`, `dlls/client.cpp`,
`dlls/multiplay_gamerules.cpp`, `cl_dll/hl/hl_weapons.cpp`, `cl_dll/view.cpp`
and `network/delta.lst`. No SDK update, copied implementation bodies or other
engine/proprietary sources used.

ID14 `weapon_satchel`, ammo `Satchel Charge`, max5/default give1, no clip,
slot4/position1; SELECTONEMPTY(1), LIMITINWORLD(8), EXHAUSTIBLE(16).
Pocket ammo and owned weapon bits remain server observations.
No `.sc` firing event is precached: no synthetic event or event index added.

chargeReady is three states, projected through signed weapon_data iuser1:
0 = charge mode; 1 = radio mode (not an active-charge count); 2 = post-detonation
transition. Deploy uses charge DRAW2 or radio DRAW2. Charge holster is DROP3;
radio holster is HOLSTER4. First/additional LMB Throw switches/keeps radio and
uses RADIO_DRAW2; it does not use DROP as a throw pose. RMB in state1 uses
RADIO_FIRE3, sets state2 and a .5s idle/attack boundary. State2 blocks LMB.
Idle returns to charge DRAW2 with ammo, then .5s cooldown; zero pocket retires.
Throw primary cadence is1s, secondary .5s. R has no magazine/reload meaning.

Reference server throw uses **player origin**, velocity `forward*274 + player
velocity`, initial Y angular velocity400, BOUNCE/BBOX, gravity.5/friction.8.
SatchelSlide later changes gravity and damps velocity/angular velocity; full
water uses FLY/upward movement. These are server facts, not client physics.
There is no fuse. Server SecondaryAttack searches4096 units for owned
monster_satchel entities; AddDuplicate considers pocket plus nearby owned
deployed charges against max5. Client does neither search nor pickup counting.
Multiplayer death calls DeactivateSatchels for an owning victim and removes
their charges without requiring explosions.

## Actual approved assets

Exact resources use session-assigned model slots:
`v_satchel`, `v_satchel_radio`, `p_satchel`, `p_satchel_radio`, **w_satchel**.
The generic `grenade.mdl` is a separate control, not a world Satchel substitute.

| Model | Bones/controllers/attachments | Bodies/skins | Sequences, frames and FPS |
| --- | --- | --- | --- |
| v_satchel | 29/0/0 | 2/1 | idle1 101@20, fidget1 61@30, draw61@30, drop16@30 |
| v_satchel_radio | 30/0/0 | 2/1 | idle1 81@30, fidget1 111@30, draw19@30, fire31@30, holster16@30 |
| p_satchel | 12/0/0 | 1/1 | idle: 1 frame @30 |
| p_satchel_radio | 12/0/0 | 1/1 | idle: 1 frame @30 |
| w_satchel | 1/0/0 | 1/1 | idle: 1 frame @30, onback: 101 @30; texture companion gives2 source files |

Source-vertex min/max (not posed world AABBs): v charge
`(-7.4,-2.77,-4.61)/(11.8,11.61,5.32)`, v radio
`(-1.91,-1.81,-2.52)/(10.89,3.36,9.85)`, p charge
`(-3.96,-3.88,-3.65)/(2.81,8.54,3.23)`, p radio
`(-0.49,-6.96,-1.3)/(1.32,2.43,0.72)`, w
`(-7.71,-7.55,-7.11)/(5.48,16.64,6.29)`.
Actual labels are validated before local pose selection.
All inspected sequences have zero MDL events. No pin/throw/radio beep is
invented. Each p-model matched11 of12 child bones to the real player model;
the unmatched root retains the existing fallback. Both models contributed
distinct GPU pixels during server p_satchel -> p_satchel_radio -> p_satchel
switches; zero weaponmodel removed the attachment.

Approved `g_bounce1/2/3.wav` are mono11025Hz, 3065/5090/5341 samples, with
decoded peaks .351562/.453125/.367188. Each produced isolated finite nonzero
PCM through ApprovedSoundAssets -> ServerAudio -> existing mixer. Local
Throw/Detonate adds no firearm/throw/beep voice. common/null uses the existing
E24 quantization-bound named-channel stop handling. No second device/mixer.

## Production boundary and lifecycle

`main.cpp` resolves compiled module preparation tokens only against the existing
approved session resource table. It passes at most8 owning metadata variants
through GameClientHost; Half-Life retains only the validated two-model pair.
Metadata lookups use the existing cache, with no per-frame file I/O/import.
The API knows no Satchel filenames, chargeReady meanings or engine handles.
An alternate project-owned test module exercises the same bounded lifetime seam.

`LocalWeaponPresentationController` remains the one weapon owner; E25-specific
methods live in `src/games/halflife/satchel_presentation.cpp` and the profile in
`include/hlclient/games/halflife/satchel_profile.hpp`. It consumes exact submitted
commands through the existing64-entry dedup history and16-entry event history.
Five bounded presentation receipts reserve unconfirmed throws; HUD never
subtracts these from canonical AmmoX. Newly committed ammo decreases settle
oldest reservations, without double charging notification plus absolute count.
Coalesced consume+pickup with unchanged AmmoX and fresh mode/draw evidence
settles one occurrence, including skip-local animation; a real decrement and
same-record svc confirmation cannot settle another receipt accidentally.
Fresh server iuser1 corrects provisional state/model; inherited old values do
not masquerade as new corrections. Missing/invalid state cannot authorize
actions. Lifecycle/resource boundaries clear old poses/receipts; focus cancel
requires released input before new actions. No local world entity, owner scan,
explosion, radius damage, timer countdown or active-charge UI is created.

The neutral projection retains optional auxiliary_integer1 and exact current
field presence using E24's parsed mask. It is separate from clientdata iuser1.
Owner is an optional ordinary entity diagnostic reference. Both keep the
historical canonical hash/wire grammar unchanged; full records still publish
atomically after validation. Field presence is not usercmd execution ACK.

Selected model pairs survive the server echo without restarting a local action.
Viewmodel intents materialize through the existing ClientWorldState/RenderScene/
IRenderer chain. Remote p-model/sequence/gait are exclusively server-owned;
bone merge/rendering remain generic. Ordinary world charges persist through
holster and observation gaps until server hide/removal; owner death by itself
does not remove them client-side. Exact server removal without TE yields no
fabricated explosion/smoke/debris. Independent TE messages in one packet retain
distinct record/cursor/entry identities and all render.

## Verification and limits

Initial concurrent Debug/core builds exhausted compiler/MSBuild memory. Only
the own failed Debug compiler was terminated after matching its Tracker chain,
build interval and scoped tlog; no unrelated process touched. Retried serially
with process-local /MP1 and explicit MSBuild single-process parameters; no clean
or build-tree restoration. Final results are appended after verification.

The failed incremental build also retained an interpolation test object from
2026-10-06, older than the changed observation layout. Core tests exposed
0xC0000374. A reverse include-dependency walk advanced timestamps of181 affected
project-owned CPP files while SHA-256 verified every file's content unchanged.
This forced current-source compilation without deleting outputs. The complete
core build/tests then passed, confirming the stale object/ABI mismatch.

Production freeze: `2026-10-07T00:29:27.1599378Z` (03:29:27 local).
Focused Debug E25 with approved assets: **36 cases /15048 assertions, all passed**.
Final Debug E13–E25/B1/prediction/host: **598 passed,14 explicit optional/capability
skips,612 cases,347731 assertions**. Core-only Release: **41/41 CTests**, including
architecture guard; core/API **255 cases /125757 assertions**. Actual generated
vcxproj source/link inputs contain zero concrete Half-Life references and the
cache is OFF/VS2022/Win32. Logs are in the E25 task-record directory.
Focused Release E25 also passed **36/36 cases,15048 assertions**, with the
approved asset/GL/isolated-PCM control enabled; no skips/failures.

Scoped ASan Debug `hlclient` and `hlclient_core_api_tests` build passed. Fresh
application `--help` and core/API `[e25]` probes both exit **0xC0000135**
(-1073741515), output0 bytes. Thus `asan_build=passed`,
`asan_application_runtime=not_verified`, `asan_core_api=not_verified`,
reason=existing_loader_dependency_blocker. No ASan infrastructure repair.

One final full Release offline suite ran after the last production edit and
focused Debug pass: **3076 passed,50 explicit skips,0 failed /3126 registered
CTests**,447.37 seconds. Recorded Catch assertion executions: **1179404**,
including core aggregate plus separately registered cases (not unique assertions).
The follow-on normal application version/help/Null bootstrap, transport self-test,
native/PowerShell summaries and complete no-stock launcher fixture also passed;
`release-offline-final.log` ends `Source build/offline verification passed: Release`,
exit0. Existing short real SDL output capability passed; it is not a listening
claim. Optional assets and filesystem/SDL/WFP capabilities account for explicit
skips; no owned E25 fixture skipped. Actual assets were separately verified above.

Exact manual `-Prediction reference -RemoteAudioPeer -NoTimeLimit -CheckOnly`
passed and reported `process-launches=0`. The selector resolves the new normal
Release executable. Launcher SHA remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
Final diff check passed; index/branch/HEAD unchanged. No new live/HLDS/stock
sessions, Steam initialization, WFP activation, captures, staging/commit/push.

Owned fixtures cover atomic mixed player/Satchel/Hornet/RPG/custom-beam/sprite
wire packets, optional owner/iuser1 semantics, duplicates/lifetime and30-second
rest. Three TE_EXPLOSION messages share one record/time with distinct cursor/entry
identity: count, production GL pixels and isolated A/B contribution of each
sprite are checked, then cleanup restores baseline/render state. Removal after
owner death creates no explosion/smoke/audio. Actual v-model visible sampled
frames: charge draw/drop/idle **19/4/34**, radio draw/fire/holster **6/11/4**;
top-band checks reject the old full-width near-camera strip.

Verification commands use the existing trees, `/MP1` and MSBuild
`/m:1 /p:BuildInParallel=false /p:MultiProcessorCompilation=false /p:CL_MPCount=1`:

```powershell
cmake --build build --config Debug --target hlclient hlclient_tests --parallel 1
cmake --build build --config Release --parallel 1
cmake --build build-core-g1 --config Release --parallel 1
ctest --test-dir build-core-g1 -C Release --output-on-failure --parallel 1
pwsh -NoProfile -File scripts/build_source.ps1 -Configuration Release -Parallel 1 -TestOnly
```

The full offline run unsets local-game opt-in, disables WFP opt-in, and separately
enables the existing short SDL output capability test. Source-only and actual
launcher CheckOnly do not launch games. Asset/PCM pixels are not user listening
or live gameplay confirmation.

Local command-time cadence/visual correction is a compatible presentation
profile, not proprietary binary parity. Ownership, 4096 search, pickup limits,
physics, water, explosions, damage, kills and score remain server rules.
Server scorch rendering and ASan loader0xC0000135 remain separately documented
limits. E26/Tripmine/Snark and new gameplay systems are outside E25.

## Manual handoff

The unchanged launcher selects
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,3862016 bytes,
build timestamp `2026-10-07T00:38:20.6924051Z`. SHA-256:
`E36F3C2485AED1D5C38D1C79E88847ECE3DBE13235BEBD96B84699F758E7BB69`.
All E25 manual fields remain not_run.

```text
satchel_first_throw_manual=not_run
satchel_radio_manual=not_run
satchel_multi_throw_manual=not_run
satchel_persistence_manual=not_run
satchel_bounce_manual=not_run
satchel_detonate_manual=not_run
satchel_multi_detonate_manual=not_run
satchel_owner_scope_manual=not_run
satchel_remote_manual=not_run
satchel_death_deactivate_manual=not_run
satchel_last_charge_manual=not_run
satchel_kill_respawn_manual=not_run
satchel_water_manual=not_run
```

Changed files:20 existing ordinary files (main/CMake/architecture/E24 record;
runtime observation+replay; neutral game API/host; Half-Life controller,
presentation, module and inventory; test CMake and alternate-module test).
Eight new ordinary files: this record, `satchel_profile.hpp`,
`satchel_presentation.cpp`, `satchel_test_fixture.hpp`, and local/observations/
world/assets test CPPs. No renderer/mixer/physics implementation changed.
All1264 saved source files and snapshot hashes verified present; other source
contents unchanged. Index empty and diff check passed.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Pick up Satchel; verify pocket count/no clip and p_satchel on the other client.
LMB deploys and selects radio; additional LMB after cooldown deploys another.
Wait: there is no fuse. RMB triggers radio FIRE and server explosions/smoke;
the other player's charges must remain. With pocket ammo, return to charge;
with zero pocket, radio remains selectable until final detonation then retires.
Test switch-away/back, owner death without explosions, respawn and E13/E14.
Verify prior weapons and close both windows to allow managed cleanup.

## 2026-10-07 startup failure and offline correction

The subsequent user report is `attribution=user_report; startup=failed`.
It does not confirm Satchel gameplay. Exact retained run:
`2ed0587ed2f24d0c82c104308af74b8a`, Crossfire, reference prediction,
keyboard-mouse, remote peer and unlimited game duration; server PID31976,
client PID32180 and peer PID32212. Both clients returned2 before provider begin,
Steam initialization, connect/serverinfo and world publication. Each presented
only one frame. The client-startup and cleanup phases were2731ms and11016ms.
Owned cleanup was `exact`; managed-file restoration `scoped_exact`, with zero
cleanup/secondary errors. Fast mode did not verify full-tree restoration.
Historical E25 offline results and E24 user confirmation above remain unchanged.
The run did not record its client binary hash; the previous handoff hash is not
retroactively assigned to the run.

Production defect: `HandshakeSession` started the coordinator in its constructor,
before `run_live_visual_control` initialized SDL/window/OpenGL/audio. The default
challenge deadline is5000ms. `ChallengeExchange::update` correctly checks that
deadline before reading queued responses. A slow device/window preparation can
therefore consume the entire deadline without any RX pump: the first frame
exits, even with a response already queued. An injected8-second preparation
reproduces this deterministically and leaves the response unconsumed. Starting
after preparation accepts that same response; repeats do not restart the clock.

This explains the observed first-frame/pre-provider failure signature, but the
historical retained log omitted the coordinator's owning terminal state. An
exact per-run `timed_out` code cannot be recovered from it. The old application
summary incorrectly substituted `application_incomplete` and prediction waiting
state for early handshake errors; those are not proof of a prediction defect.

Fix: construction initializes owners without TX/deadline start. The first owning
`HandshakeSession::update(now)` starts an idle coordinator with that exact clock,
then pumps it. In the normal visual path it runs after platform/renderer/audio
preparation. Other app loops use the same method. No transport deadline, wire
grammar, retries, gameplay policy, scheduler cadence or E25 rule was relaxed.
An early terminal failure now preserves its coordinator state as `primary_error`
with `error_domain=handshake; failure_stage=handshake` before bulky counters.
Authentication/runtime owning errors retain precedence. Real failures still exit2;
healthy manual completion stays independent of scripted/prediction coverage.

Source snapshot:1272 hash-verified ordinary files under
`manual-artifacts/task-records/e25-startup-fix-20261007/source/` and its manifest.
Pre-edit coordinator/challenge/live-visual baseline:52 cases,3554 assertions.
New tests cover the delayed clock/queued response, repeated updates, real failure
reason precedence, and the normal application against a bound silent owned UDP
fixture. It uses an empty project-owned resource root and deliberately absent
Steam DLL, sends no challenge reply, and never reaches provider begin/API init.
It checks prepared/idle before TX, multiple rendered/pumped frames, the real
5000ms timeout and honest exit2. It needs no installed Half-Life. The old Release
fails this application regression; new Debug passes it. Debug focused result:
456 passed,13 optional skips,176144 assertions; process-level OpenGL test passed.

Current Release remains manual=not_run. No live/HLDS/stock/managed sessions,
Steam initialization, WFP, captures, staging, commit or push were performed.
The unchanged launcher still selects the ordinary Release executable.

Final post-production-edit verification: VS2022/Win32 Debug and Release built;
core-only Release built with `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`, all41 tests
passed, including architecture guard. Generated core projects contain zero
concrete Half-Life source/header/library references. Final full Release CTest:
**3078 passed,51 explicit optional skips,0 failed /3129 registered**,569.05s.
The new normal OpenGL application startup regression passed in both Debug and
Release (10.58s/7.71s), with no installed assets or real server. Version/help,
Null bootstrap, transport self-test, native/PowerShell summaries, complete
no-stock launcher fixtures and source-only CheckOnly subsequently passed;
`release-offline-final.log` ends `Source build/offline verification passed:
Release`, exit0. Actual launcher reference/remote-peer/unlimited CheckOnly also
passed, `process-launches=0`. Optional local asset/SDL/link/WFP capability tests
account for skips; the startup fixture did not skip. No user listening or live
startup success is claimed. ASan was not rerun for this application-only fix;
the previous loader0xC0000135 runtime limitation remains unverified, unchanged.

Final executable: `build\bin\Release\hlclient.exe`,3870720 bytes,
UTC build timestamp2026-10-07T18:52:31. SHA-256:
`A66D27F210EFD6EE64B66C56D57D1DE64393E1A625BAA77F397E66AB66551E41`.
Launcher SHA remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
All1272 saved source files are present; only six existing files changed for this
fix (main, this record, architecture, test CMake, coordinator/live-visual tests),
plus the new ordinary `scripts/test_live_visual_startup.ps1`. Index remains empty,
HEAD/branch unchanged, `git diff --check` passed. No output cleanup or other
worktree changes were made. Previous manual history is preserved.

Ready manual command (new Release `manual=not_run`):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Close both client windows to finish; allow managed cleanup/restoration to complete.

## 2026-10-07 deployed-charge contact and local death-camera follow-up

`attribution=user_report`: stepping onto another player's deployed charge jerks;
death from it also jerks. The user clarified that the death problem is on the
victim's own camera, not the observer's Studio death animation. No run ID,
recording, exact timing or executable hash was supplied; none is inferred.
Historical E13 acceptance, earlier E25 results and the startup-fix history above
are preserved. The new corrected build requires fresh manual confirmation.

Two separate production causes were found:

1. `build_reference_brush_collision` skipped non-BSP entities; the protocol
   projector did not expose their already-decoded `mins/maxs`. Local prediction
   could therefore walk through a server `SOLID_BBOX` charge while the server
   stepped/blocked the player. The pinned SDK Satchel is BOUNCE/SOLID_BBOX with
   a small(-4..4) box that can be stepped over. That dimension is a fixture/
   reference check, **not** a hardcoded production/model-vertex guess.
2. Confirmed death correctly suspended movement prediction, but the camera
   directly consumed each latest server origin/view-offset. Toss/impulse motion
   was consequently displayed in receiving-sample steps rather than render-time
   transitions. The remote E13 animation path is not changed for this report.

The neutral observation now owns optional signed float `mins/maxs` from the
ordinary negotiated entity schema. Missing fields stay unavailable; mismatched
types and malformed suffixes still reject the whole transaction. Existing wire
grammar, canonical hashes, health/damage/ammo and publication boundaries are
unchanged. Bounds are not obtained from a model or renderer.

Half-Life explicitly selects server box solidity code2 and receiving-owner
exclusion in its existing GameMovementPolicy. An absent module policy never
silently selects these rules. The current-frame collision context constructs
at most128 checked world-space boxes from committed origin+relative bounds.
The existing BrushSceneMovementCollision unions their Minkowski-expanded
standing/duck sweeps with world/brush queries, with an explicit
`server_entity_box` hit identity. Position tests, ground seeding, command
simulation, rebase/replay and derived PUSH presentation use that same query.
Boxes remain queryable without a visible/prepared model; render culling does
not remove physics. Exact SOLID_NOT/removal removes the collider; no local
charge trajectory, pickup, timer, detonation or damage is simulated.

The owner's former diagnostic projection is now also consumed for this explicit
profiled **prediction exclusion**, not as local ownership/gameplay authority.
Current-frame BBOX transforms are not extrapolated. The bounded axis-aligned
query/profile is independently implemented and local-compatible; this is not
universal GoldSrc-mod physics or a proprietary stock-binary parity claim.
Missing required box fields, invalid bounds and cap overflow report explicit
collision reasons rather than guessing a shape.

HalfLifePresentation selects a100ms authoritative camera transition only during
confirmed death. The generic LiveVisualCameraController samples that transition
using explicit absolute app presentation time. Duplicate/retained records do
not restart it; a newer target starts from the current displayed position.
It never predicts a dead player's movement, edits canonical state, affects
wire commands or smooths living-player H3/H4 translation. Generation/life/death
boundaries, respawn, backward/unavailable time and >128-unit discontinuities
reset/snap as defined. Viewmodel, remote corpse/animation and life rules remain
owned by their existing paths. No new manual flag is required.

Pre-edit snapshot:1273 hash-verified ordinary project files under
`manual-artifacts/task-records/e25-contact-death-camera-20261007/source/` and its
manifest. Pre-edit baselines:59 passed/1 optional skip,1358 assertions; movement/
prediction/E13:226 passed,277062 assertions. New Debug regressions:5 cases,
471 assertions passed (wire lifetime/atomicity/types, hull sweeps, server-box
8-unit stepping and65 neutral standing commands, actual HL policy selection,
absolute-time death camera, stale samples and session/respawn reset). Broader
Debug:288 passed/1 optional local-asset skip,278770 assertions. No old expected
values were weakened. Final Release/core-only checks and binary are appended
after completion.

New live/HLDS/stock/managed sessions, Steam initialization, WFP and captures=0;
staging/commit/push=none. Old builds/logs, other worktrees and installed game/
server files were not cleaned, modified or restored by this follow-up.

Final acceptance completed2026-10-08 (follow-up started2026-10-07):

- VS2022/v143 Win32 affected Debug and all Release targets built incrementally,
  serial `/MP1`; no clean/build-tree restoration. Both configurations passed
  the five new cases /471 assertions. Wider Debug:288 passed/1 optional skip,
  278770 assertions.
- Core-only Release `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`:41/41 tests passed,
  including architecture guard and the neutral bounds/query/camera tests.
  Generated core projects contain zero concrete Half-Life source/header/lib
  references. No installed game is required by these fixtures.
- Full final Release suite after the final production edit:3083 passed,
  51 explicit optional skips,0 failures /3134 registered,631.70s. Normal OpenGL
  application startup regression passed(8.58s); project-owned E13 death pixels,
  existing movement/H3/H4 and weapon regressions ran. Optional installed assets,
  SDL/WFP/link capabilities account for skips, not positive live evidence.
- Subsequent version/help/Null, transport self-test, native/PowerShell summaries,
  complete no-stock launcher fixtures and source-only CheckOnly passed.
  `release-offline-final.log` ends `Source build/offline verification passed:
  Release`, exit0. Actual reference/peer/unlimited `-CheckOnly` passed with
  process-launches0. `git diff --check` passed; index remains empty and
  branch/HEAD unchanged.
- ASan was not rerun for this correction; the separately recorded prior runtime
  limitation is not represented as current ASan acceptance. No device listening
  or live collision/death validation is claimed.

All1273 saved source files remain present; exactly29 existing ordinary files
changed within the described scope (neutral observations/validators, movement
policy/API/query/seed glue, Half-Life policy/camera intent, app camera, six test/
CMake files and the two existing records). No new production path is hidden in
artifacts/generated files. Supporting logs/snapshot are under the task directory
above; the normal Release and required helpers remain in the existing outputs.

Prepared `build\bin\Release\hlclient.exe`:3878400 bytes,
UTC build timestamp2026-10-07T20:58:04. SHA-256:
`377F5ABB9CFB83C0D2C8C674B72F4F8587649CC9B4195D738687DD8E5A8FB18B`.
Launcher SHA remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`
and selects this exact normal executable. New build `manual=not_run`.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Have B place a charge; A walks over it, stands on it, then steps off. Check
WASD/Shift/duck/jump and no repeated camera returns while standing. Detonate
with A nearby: the victim's own camera should move smoothly with server death
motion, without reviving prediction. Respawn and verify normal camera/viewmodel/
movement and prior weapons. Check B's remote corpse/death animation as a
regression; close both windows and let managed cleanup complete.

### Clustered-charge own-camera correction (2026-10-08)

New evidence, attribution=`user_report`: the player running over another
player's charges still sees jerks in **their own camera**. The supplied image
shows several clustered charges. The user explicitly clarified that this is
not the observer's remote Studio animation. No executable hash/run metadata
was supplied for that image; it is not attributed to a particular binary hash.
Earlier results and the previous death-camera correction remain historical;
the single-charge kernel stepping test did not prove H3 eye continuity.

The new project-owned fixture drives the existing normal LiveRuntimeStage
through in-memory signon, negotiated entity schemas, committed player state
and three overlapping server BBOX records. A real literal BSP collision
package supplies the floor. No installed model, game file or live peer is
needed; the boxes deliberately have modelindex0. The existing scripted input
drives ordinary20ms reference commands, not a replacement prediction engine.

Pre-fix evidence: physical prediction already reaches box-top origin z44,
but three render-time samples of the same ascending pair have eye z
`64.400002 -> 68 -> 64`, followed by72 in the next pair. The late straight
body chord touches a box side; LiveRuntimeStage's H3 collision fallback
returns the old endpoint after an earlier fraction had already moved forward.
This reverses the own camera despite correct physical stepping. The existing
world-ground seam recovery explicitly excludes box support and cannot repair
this path. `repro-before.log` records the deterministic failing regression.

Production correction: the neutral shared helper
`src/goldsrc/reference_brush_collision.cpp::sample_reference_box_step_eye`
checks a complete raised hull route up/across/down and the sampled free hull.
For a same-hull, supported small world/box transition, LiveRuntimeStage uses
that route while interpolating eye position at its **existing command-time
alpha**. The compensated eye offset stays inside the hull; the host separately
checks BSP/brush eye clearance. It runs for the whole eligible pair, not only
after the straight chord has failed, so late fractions cannot roll back.
The maximum rise/drop comes from the observed server movement `step_size`,
not a guessed Satchel dimension or camera timer. Failed queries use existing
H3 error handling. The exposed diagnostic reason is `box_step_eye`.

This is only presentation. Physical endpoints, source commands, authoritative
observations, server-bound boxes, ammo/health/damage, correction, rebase/replay,
prediction-off behavior and Half-Life's owner/solidity policy are unchanged.
There is no local charge physics or collision bypass. Airborne/hull changes,
zero/invalid/unavailable step limits and obstructed walls/ceilings do not take
this route. Existing death-camera/life/respawn handling is not broadened into
living-player timed smoothing. The local-compatible bounded presentation is
not claimed as stock-binary stair-smoothing parity.

Source preservation:1273 ordinary project files hash-snapshotted before edits
under `manual-artifacts/task-records/e25-cluster-step-camera-20261008/source/`
with `source-manifest.json`. This correction's production scope is the shared
reference-collision header/helper and LiveRuntimeStage; tests extend the
existing collision/core-API and real-stage fixtures. No parallel system,
asset dependency, game-rule API expansion or launcher parameter is added.
Final build/regression/binary evidence is appended below after acceptance.
New build manual validation remains `not_run`; new live/HLDS/Steam/WFP/capture
sessions=0 and staging/commit/push=none.

Final clustered-charge acceptance (after the last production edit):

- VS2022/v143 Win32 affected Debug and all Release targets built incrementally
  and serially with `/MP1`, without cleaning/restoring build trees. Focused
  Debug and Release each pass7 cases/785 assertions (old bounds/step/death
  checks plus the two new camera regressions). Broader Debug:294 passed,
  1 explicit optional installed-asset skip,280011 assertions.
- The actual-stage regression now records ascending eye samples
  `64.400002 -> 68 -> 71.599998`, reason `box_step_eye`, while physical z44
  remains unchanged. All sampled pairs check non-reversing eye/XY motion;
  render sampling leaves local_steps, TX count and the committed owning
  observation unchanged. The isolated shared test checks ascent/descent,
  free hulls, endpoint handling, unavailable/invalid step limits, airborne
  rejection, wall and ceiling obstruction (52 assertions). Existing
  collision/death-camera tests were not weakened.
- Core-only Release `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`:41/41 CTest tests
  passed, including architecture guard and new camera fixture; generated
  core projects contain zero concrete Half-Life source/header/lib references.
- One full final Release offline suite:3085 passed,51 explicit optional skips,
  0 failures /3136 registered,582.21s. The normal OpenGL application startup
  regression passed(7.92s); E13 production Studio/OpenGL death/corpse/respawn
  pixels, slopes, H3/H4, weapons and life transitions passed. Skips concern
  opt-in installed assets, SDL/WFP/link capabilities, not positive live proof.
- Follow-on version/help/Null, transport self-test, native/PowerShell summary
  roundtrip and complete no-stock launcher fixtures passed. The log ends
  `Source build/offline verification passed: Release`, exit0. The actual
  unchanged launcher `-Prediction reference -RemoteAudioPeer -NoTimeLimit
  -CheckOnly` passed, process-launches0. `git diff --check` passed; the index
  remains empty, branch/HEAD unchanged. All1273 snapshot copies remain
  hash-valid and present; this follow-up changes exactly7 ordinary files.
- ASan was not rerun; the previously recorded unconfirmed runtime limitation
  remains, without a new ASan acceptance claim. No live/device listening or
  user-observed smooth-camera confirmation is claimed for this new binary.

Prepared normal `build\bin\Release\hlclient.exe`:3879424 bytes,
UTC build timestamp2026-10-07T22:57:58. SHA-256:
`EC638A47894365C01B330D979851DE5DCB8037A6C01969B9B13B0D189652B083`.
Launcher SHA is unchanged:
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
No hidden feature flag or manual binary copy is needed; new binary
`manual=not_run`. New live/HLDS/stock/Steam/WFP/captures=0; commit/push=none.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Have B place several charges next to/partly over one another. A walks/runs
across them in both directions, stops on top and steps off; repeat with Shift
and crouch. Check the **running player's own camera**, then detonate/respawn
as a regression of the separate previous death-camera fix. Close both client
windows and allow managed cleanup to finish. Remaining bounds are current-frame
server BBOX query support, no BBOX motion extrapolation, and command-time-only
small supported-step eye presentation, not universal dynamic-object physics or
a new general living-camera smoothing profile.
