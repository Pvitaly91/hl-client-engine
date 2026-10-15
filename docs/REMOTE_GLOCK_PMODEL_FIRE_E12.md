# E12 — remote Glock p-model pose fire presentation

Task `HLC-E12-REMOTE-GLOCK-PMODEL-POSE-FIRE-PRESENTATION`.
Authoritative worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. Existing dirty E11 is retained.
Pre-edit verified snapshot: 1,281 regular files, existing mechanism,
`manual-artifacts/task-records/e10-e12-20261001/source-manifest.json`.
Focused Release baseline: 103 passed, 6 opt-in skips, 9,554 assertions.

## Manual attribution and boundaries

New user report: “ручний тест показав що все працює”. E11
`manual=passed`, `attribution=user_report`: server-selected remote p-model,
switching, movement attachment, and tested local/remote bullet particles.
No binary hash, exact timeline or stock-exact measurements are inferred.
Historical E11 pre-handoff `manual=not_run` remains unchanged. E11 particles
remain local-compatible, not stock-exact. New E12 build `manual=not_run`.
New live/HLDS/stock/Steam/WFP/capture sessions = 0; staging/commit/push = none.

## Pre-implementation production audit

- `current_remote_muzzle_source`: `RemoteWeaponEffects::drain`, committed
  event origin + stand/duck view height (28/12) + event aim forward*20 - up*4.
  The inherited compressed player pitch is restored only for aiming.
- `current_remote_shell_source`: the same event eye + forward*20 - up*12 +
  right*4; event velocity + deterministic right(50..70), up(100..150), forward25.
- `current_remote_impact_trace_source`: **separate** committed event eye with
  aim/spread, 8192-unit existing static BSP trace. E11 decal/material/audio and
  particles converge through `RemoteEffectPresentation`/`WorldImpactPresentation`.
- `current_attack_animation_source=server_sequence`: E10 module samples exact
  committed sequence/frame/animtime/framerate and composes transition/gait.
  `RemoteWeaponEffects` never changes it. Real live attack coverage has not
  been newly acquired; no event override is justified or implemented.

Normal main evaluates `present_entities` before draining deduplicated remote
events and executing transient effects. E11 already evaluates one merged child
pose and retains approved immutable assets. Thus socket evaluation can reuse
that exact CPU pose upstream, with no GPU feedback or second importer/evaluator.
Existing source record IDs are opaque identities, not sortable ordinals.
Existing effects field is decoded and retained, but EF_MUZZLEFLASH is not a
second trigger. Scripted-event dedup remains the sole effect trigger.

Pinned official Valve SDK stays at
`b1b5cf5892918535619b2937bb927e46cb097ba1`. Glock server sets EF_MUZZLEFLASH
and PLAYER_ATTACK1; client event shell and EV_GetGunPosition are separate.
StudioCalcAttachments transforms the local origin with the calculated owning
bone. These are behavioral references, not copied code or stock-binary proof.

## Actual approved asset inspection before socket policy

Exact research root through `RuntimeReplayLocalAssets::create`, existing
approved source/importer and retained `studio_model_data`, read-only:

- `models/p_9mmhandgun.mdl`: **0 attachments**, 12 bones; geometry is on
  bone11 `sides01`, parent10 `Bip01 R Hand`; 170 emitted vertices. Bone-local
  geometry bounds (-4.12,-0.79,-2.65)..(4.12,0.79,2.65). Bone default translation
  (5.7819,0.735678,1.5869), rotation (0.027741,-0.080641,0.094173).
- `models/p_crowbar.mdl`: **0 attachments**, 12 bones; geometry on bone11
  `Box01`, parent10 `Bip01 R Hand`, not a Glock-compatible skeleton/profile.
- Both approved p-models merge 11 shared bones and evaluate one unmatched
  geometry bone through the child's hierarchy. Input modification times remain
  unchanged. No private assets copied into Git or new parser introduced.

Inspection logs: `asset-inspection.txt` and `asset-inspection-endpoints.txt`
under the E12 task-artifact directory. No SDK submodule update or game asset
modification/copy was made.

## Implemented profile and ownership

Actual Glock muzzle source **and** shell source: `validated_pmodel_socket`.
The Half-Life profile identifies the approved virtual token, then validates the
12 bone names/parent chain, geometry-owning bone11, its default transform and
bone-local geometry bounds once per immutable resource ID/content revision.
Neither session-assigned model slot nor player hand position identifies it.
The 32-entry bounded profile cache is reset with the module. Rendering still
uses the server-selected slot and the unchanged E11 merge/material path.

Local-compatible socket coordinates on `sides01`:

| Socket | Bone-local origin | Basis |
| --- | --- | --- |
| Muzzle | (4.20, 0, 1.865) | +X forward, -Y right, +Z up |
| Shell port | (1.00, -0.85, 1.80) | Same weapon-local axes |

The muzzle is 0.08 beyond the observed +X front face, between the measured
front upper/lower edge heights (2.65/1.08). The shell port is a separately
chosen upper side-of-receiver point. This is a geometry-validated **local
compatible presentation policy**, not stock attachment metadata or stock-exact
ejection constants. A compatible validated revision may supply unambiguous
named `muzzle`/`shell` attachments, which take priority; no arbitrary attachment
ordinal is guessed. The actual inspected revision has none.

| Layer / actual files | Responsibility |
| --- | --- |
| `assets/model_asset_types.hpp`, `entity_render/studio_socket.hpp/.cpp` | Neutral numeric socket definition/result; O(1) selected-bone math on an already evaluated immutable pose |
| `game_api/remote_effects.hpp`, `game_client_host.*` | Synchronous borrowed model view, owning numeric outputs, bounded module handoff, distinct event provenance |
| `games/halflife/remote_effects.*`, `client_module.cpp` | Virtual-name/profile validation, committed Glock event eligibility/correlation, frozen effect selection and deterministic ejection |
| `app/runtime_replay_local_assets.*` | Reuse exact final merged child pose and same entity transform, publish at the existing successful scene transaction |
| `goldsrc/runtime_replay_session.cpp` | Add record ordinal/committed weapon-model evidence at the existing post-commit event bridge, without changing wire grammar/hash |
| `apps/hlclient/main.cpp` | Existing order plus bounded opt-in pose diagnostic; existing effects/audio/transient owners execute outputs |

Dependency direction remains composition → host/API + Half-Life module; the
host/materializer use API only. `hlclient_entity_render_api` now links the
neutral `hlclient_studio_pose` target, which has no concrete game dependency.
No sockets, native handles, mutable world access or renderer feedback are in
the new API. The model view is borrowed only for the synchronous planning call;
no model/RX/bone pointer is retained in the 32 numeric samples or effect requests.

Normal production order:

`committed observations → E10 composed body → E11 merged p-model → generic
socket transform → successful scene publication → module event drain →
existing transient/light/shell/audio execution → normal renderer`.

Generic socket math uses the selected row-major bone 3x4 and exactly
`entity_render_model_matrix`, the existing column-major renderer transform.
It validates model identity/revision, bone/attachment bounds, finite numbers,
uniform transform and orthonormal directions. It does not evaluate a second
pose, search bone names per frame, import again or read GPU data. Explicit
attachment zero basis vectors use the owning bone axes; nonfinite/degenerate
nonzero directions fail with typed errors.

## Event/pose correlation, lifetime and degradation

One module lives for the session. Each successful frame publishes at most 32
owning samples with network/map generation, player lifetime, exact entity,
sampled and current weapon slot, content revision, source identity **and**
ordering ordinal, frame identity and application-relative clocks. Frames and
records are identities, not clocks. The event bridge's ordinal does not alter
the serialized event grammar. Pending events remain bounded at 32 and retain
the known emitter lifetime at commit; existing event dedup/local-echo checks
remain authoritative. The existing 250 ms event/pose freshness limit is used.

- Resolve only the exact unique emitter with matching generation/map/lifetime,
  current and event weapon evidence, recognized Glock semantic, non-future
  source ordinal, valid clock and validated socket data.
- A known generation/lifetime/weapon mismatch suppresses Glock flash/light/
  shell, **not** the independently valid event trace/audio. In particular a
  sampled old Glock/current crowbar race never flashes on the crowbar.
- Missing, stale/future or invalid socket data use the existing documented
  `legacy_player_space_fallback` immediately, with typed diagnostic status;
  there is no indefinite wait, hidden retry or later replay of a missed flash.
- A resolved effect owns a frozen muzzle position and distinct shell origin/
  basis. Player movement, frame publication or weapon change afterward cannot
  drag it along. Existing 75 ms flash/light and 2.5 s free shell lifetime remain
  unchanged. Shell velocity keeps server velocity and the same deterministic
  variation; only its directional basis follows the resolved weapon.
- Reset/teardown clear pose/profile/pending state; slot reuse changes the
  existing host lifetime. No Netchan reset or game animation override is added.

Limitations: the bridge uses the latest bounded numeric presented pose, not
arbitrary historical reevaluation. If its source ordinal is newer than an
event it degrades explicitly; it does not borrow a future pose. An initial
event with no previously known lifetime uses exact emitter/source ordering
rather than inventing an unobserved occupant identity. The normal asset host
currently uses its existing generation for both network/map stamps; module/API
checks keep these distinct and separately test map mismatch. This does not add
new reconnect/map-generation machinery.

## Attack animation and impact invariance

`remote_attack_animation_source=server_sequence`. E10 retains sequence, frame,
animtime/framerate, gait/controller/transition rules. The new socket reads the
result, never writes a body sequence. Existing decoded `effects` is diagnostic
only; EF_MUZZLEFLASH does not fire a second event or bypass dedup. A deterministic
test feeds a server attack sequence + gait + transition, commits a Glock event,
then checks unchanged sampled fields and all evaluated body bone matrices.
No newly captured real-server attack timeline is available here; standing/run/
crouch attack visibility remains a separate user observation at handoff.

The impact request still originates at committed event eye/aim/spread, **not**
the socket. Its range, trace order, nearest blocker policy, decal clipping,
material/audio and E11 particle profile/identity are unchanged. An independent
normal-module A/B changes muzzle/shell sockets but compares exact hit/decal
vertices/UVs/material mode, spatial audio sample/gain and isolated PCM, particle
positions/colors and causal action identity. No gameplay damage/ammo or server
trace authority changes.

## Diagnostics

Existing `--net-trace` emits at most 64 `[remote-fire-pose]` rows per application:
emitter, semantic, model/resource/frame/lifetime, socket source/resolution,
bone indices, frozen world coordinates, pose/event ages, server sequence/effects,
`server_sequence` authority and event-dedup trigger. No raw RX bytes, native
paths, private handles or entire bone matrices are logged. Resolved, missing,
stale, generation/lifetime/weapon mismatch and invalid numeric socket are
different statuses. Submission coordinates are not claimed as pixel proof.

## Verification and manual handoff

Final build/test results and SHA-256 are recorded below. Normal Release remains
`build/bin/Release/hlclient.exe`;
the unchanged launcher selects that file, default map crossfire, with no new
feature flag or manual asset copy.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer
```

A observes B: fire while standing, crouching, moving/strafe, looking up/down
and jumping where allowed. Confirm flash/light at the gun, separate shell port
then free flight, unchanged hit decal/sound/particles, Glock/crowbar switching,
no duplicate local shot, smooth gait/p-model and footsteps. No permanent effects
or disconnected clients. Record **separately** body attack visibility while
standing, running and crouching (yes/no); do not infer it from the flash.
E12 `manual=not_run` until this new build is actually tested by the user.

### Final offline verification after the last source/test edit

Normal existing VS2022/v143 **Win32** Debug and Release all-target builds
passed. Compiler host was the existing x64 host with two workers; output target
remains Win32. No clean build, changed compiler safety flags or concurrent
builds into the same output tree were used.

| Check | Exact result |
| --- | --- |
| Final affected Debug E10/E11/E12, pose, module/lifecycle, effects, audio and prediction | **279 passed, 7 opt-in skips; 169,983 assertions passed** |
| Final Release E12 focused owned fixtures | **10 passed, 1 opt-in asset skip; 5,057 assertions passed** |
| Final full Release offline catalog | **2,642 passed, 38 skipped, 0 failed / 2,680 entries** |
| Core-only Debug, concrete game OFF | All engine/tool targets built; **41/41 CTest passed**, including architecture guard; core executable **192 cases / 7,554 assertions passed** |
| Core-only E12 helper and independent alternate-module seam | **3 cases / 83 assertions passed**; **168 generated project files, 0 HL-module source/link references**, excluding intentional negative guard fixtures |
| Optional exact-root approved Glock/crowbar control, final Release | **1 case / 67 assertions passed**; same normal host/model/merge/socket/event path plus actual GL p-model pixels, unchanged source modification times |
| `asan_build` | Passed: existing bounded `hlclient_core_api_tests` target |
| `asan_core_tests` | **8 E11/E12 cases / 895 assertions passed** with the already-installed matching runtime on this process's PATH; core `--help` exited 0 |
| `asan_application_runtime` | **not_run / not_verified**; no full application-runtime or full ASan-suite claim, no infrastructure repair |
| Unchanged two-client launcher `-CheckOnly` | Passed, `process-launches=0`; no runner/game/Steam/WFP/sockets started |
| Existing no-stock launcher fixture | Passed: reference/off, timing, maps/scenarios, same-run reports, exit/error contracts |
| Snapshot/index/diff check | 1,281 original snapshot files verified, no bad hashes/missing originals; prior loose diagnostics and launcher untouched, index empty, `git diff --check` passed |

The full catalog was run after the last generic basis-validation and test edit.
Its 2,677 entries ran with four workers (2,639 passed, 38 skipped, 108.16 s).
The three fake/process-inventory-sensitive profile/startup fixtures then ran
serially (3/3 passed, 9.00 s); these are not permanent exclusions. WFP execution
was explicitly disabled. Test names containing “Live” use existing offline/fake
peers, not new real game sessions. Skips include optional original assets,
audio-device playback, interactive capture and Windows reparse/capability tests;
they are not passes. The skipped approved p-model control was separately passed
read-only above. Actual owned OpenGL socket-driven flash, light-only nearby wall
pixel increase/distant wall invariance, expiry/baseline restoration and existing
depth tests passed. Isolated synthetic impact PCM A/B remained equal/nonzero;
this is not a listening confirmation or a new real-device output measurement.

Evidence directory: `manual-artifacts/task-records/e10-e12-20261001/`, especially
`build-*-after-last-edit.txt`, `focused-debug-regressions-final.txt`,
`focused-release-e12-final.txt`, `release-offline-main-final.txt`,
`release-offline-serial-final.txt`, `core-only-ctest-final.txt`,
`actual-pmodel-sockets-release.txt`, `asan-core-e11-e12-final.txt`,
`launcher-checkonly-final.txt`, `launcher-no-stock-final.txt` and
`source-preservation-final.json`. These are evidence, not required hidden code.

Prepared normal Release executable:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
3,527,680 bytes. SHA-256:

```text
08C19558242857AEC46C634D22814957D195343BC0BAFCF02C630234A663977E
```

The launcher is byte-identical to the pre-E12 snapshot
(`7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`).
E12 is integrated and offline verified; new build manual validation remains
**not run**. New real live/stock-client/HLDS/managed captures/Steam initialization/
WFP activation = **0**. Staging/commit/push = **none**. HEAD/branch unchanged.

## Subsequent user acceptance before E13 (2026-10-02)

New report: “перевірив все ок”. E12 `manual=passed`,
`attribution=user_report`, for the requested checklist: visible remote Glock
socket flash while standing/crouching/moving, separate free shell, spatial wall
light, unchanged impacts/audio/decals/E11 particles, switching without obvious
stale effects/local duplication, and stable player/weapon animation. No binary
hash or stock-exact measurements are inferred. Historical pre-handoff
`manual=not_run` entries above remain unchanged. The inspected p-model still has
zero native attachments; `validated_pmodel_socket` is local-compatible, not
stock-exact. This acceptance is not E13 death/corpse/respawn acceptance.
