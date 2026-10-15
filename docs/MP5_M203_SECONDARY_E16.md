# E16 — MP5 / M203 secondary grenade presentation

## Later manual evidence supplied with E17 (2026-10-04)

`manual=passed; attribution=user_report`, scoped to the reported successful
MP5/M203 launch, server grenade/explosion and continued client operation in the
preceding manual scenario. The previous multiplayer path remained usable.
This is new user evidence, not a rewrite of historical `manual=not_run` records.
It does not establish unreported water, exact explosion audio, stress or ASan
acceptance, and does not validate the new E17 Shotgun build.

Normal `hlclient.exe` integration; result:
`mp5_m203_secondary_grenade_integrated` (offline verified, manual pending).

The initial E16 offline report below is retained as historical evidence.
The subsequent user reported missing MP5 primary/secondary animations and
inactive secondary fire. See [the normal-path corrective follow-up](#normal-path-corrective-follow-up)
for the defects, new regressions and latest Release handoff; the original
offline pass was not manual acceptance.

The [2026-10-04 corrective follow-up](#2026-10-04-follow-up-grenade-motion-main-boom-and-glock-rmb)
adds stable grenade presentation and main explosion audio, and extends local
secondary presentation to Glock at the user's explicit request. Scope and
verification statements in the earlier sections remain historical.

The latest [late-birth first-pose follow-up](#2026-10-04-late-birth-first-pose-follow-up)
addresses the subsequent user report of a grenade end cap held at the center
of the screen before flight; previous verification remains historical.

E16 `manual=not_run`; `water_manual=not_run`. New live clients, HLDS, Steam
initialization, WFP, managed sessions and captures: **0**. Staging, commit and
push: **none**. No game assets or SDK implementation bodies are added to the
repository. E17/other weapons are outside this change.

## Starting evidence and reference

Authoritative worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`; branch
`codex/stock-runtime-campaign-5e48b7c1`; inspected HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. Existing staged/unstaged/untracked
work is preserved. The E16 source snapshot contains 1,204 verified files.
Pre-edit focused baseline: 142 passed, one optional skip, 181,182 assertions.
This is a baseline, not final E16 acceptance.

E15 exists locally: MP5/9mmAR ownership, validated v_9mmAR roles, p_9mmAR bone
merge, exact `events/mp5.sc`, automatic primary, hks audio, shells, tracers and
primary impacts. Its historical document and `manual=not_run` are preserved.
Pre-E16, exact `events/mp52.sc` was safely classified as secondary-deferred;
it did not produce M203 launch presentation. No E15 manual acceptance is
inferred from implementation, screenshots or earlier offline results.

Official Half-Life SDK reference: `third_party/halflife-sdk`, pinned revision
`b1b5cf5892918535619b2937bb927e46cb097ba1`. Inspected `dlls/mp5.cpp`,
`dlls/ggrenade.cpp`, `dlls/weapons.cpp`, `dlls/weapons.h`,
`cl_dll/ev_hldm.cpp`, `cl_dll/hl/hl_events.cpp` and `common/const.h`.
The SDK is behavioral/wire evidence only, not copied production code or a
claim of proprietary stock-binary parity.

Pinned `EV_FireMP52` proves local launch role/body 2, pitch punch -10, and
`weapons/glauncher.wav` / `weapons/glauncher2.wav` at pitch 94..109%. It does
not invoke the primary bullet, shell or muzzle-flash path. Server secondary
rejects full waterlevel and empty ARgrenades; it sets both attack deadlines
to +1 second and idle to +5 seconds. Primary sets both attack deadlines to
+0.1 second. The server reference creates a contact grenade at player origin
+ view offset + forward*16, velocity forward*800 without adding player
velocity, with gravity .5, bounce movement, solid bbox, velocity-derived
angles and server damage. These values are documentation/fixture reference:
**none are used to integrate a local projectile trajectory**.

## Production paths and direction

| Responsibility | Actual project paths / owner |
| --- | --- |
| Physical RMB and submitted secondary bit | Existing input adapter; `reference_client_move` and Half-Life `movement_policy`; explicit secondary-enabled button policy |
| MP5 eligibility, ammo meanings, timers and launch reconciliation | `games/halflife/local_weapon_presentation.hpp/.cpp`, `firearm_profile.hpp` |
| Local launch audio and camera presentation | `games/halflife/weapon_audio.cpp`, `presentation.cpp`; neutral GameClientAPI action/audio outputs |
| Assigned mp52 binding and remote launch cue | `games/halflife/remote_effects.cpp`, `client_module.cpp`; generic committed scripted-event input |
| Server projectile | Existing packet-entity observation, visual projection and generic interpolation; `app/runtime_replay_local_assets.cpp`, ordinary Studio renderer |
| Exact temporary-entity framing | `goldsrc/runtime_control_decoder.hpp/.cpp`; no renderer calls |
| Owning committed temporary observations | `client/temporary_effect.hpp`, `goldsrc/runtime_replay_session.cpp` |
| Host/API transaction and time conversion | `game_api/game_client_host.hpp/.cpp`, `game_client_module.hpp`, `temporary_effects.hpp` |
| Half-Life explosion/smoke component policy | `games/halflife/temporary_effects.hpp/.cpp`, `client_module.cpp` |
| Bounded neutral world-effect lifetime | `app/temporary_effect_presentation.hpp/.cpp` |
| Approved sprite resolution / animation | `app/runtime_replay_local_assets.hpp/.cpp`; existing imported resource package |
| Numeric sprite/light/particle execution | Existing entity/OpenGL/Null render paths and `ImpactParticlePool`; neutral blend modes, no MP5/M203 names |
| Application composition | `apps/hlclient/main.cpp`: drain committed intents, append existing RenderScene components, materialize transient sprites |

Data flows `transport/observations -> GameClientAPI -> selected
module -> neutral intents -> application/renderer/audio`. Dependencies are
module -> API/shared mechanisms, host -> API and application -> host/module;
the core/host does
not link a concrete Half-Life module. Normal application composition creates
the module; no extra game loop, physics engine, audio device or renderer is
introduced. Core-only remains `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`.

## Local input, launch and confirmation

RMB uses neutral `kReferenceGoldSrcButtonAttack2 = 2048`. Half-Life selects
the explicit `jump_duck_primary_secondary_reload_use` policy. Existing
physical capture/focus/life-release behavior applies: the capture click is
not a shot, and stale held input cannot launch on a new life. Immutable wire
history/backups retain the submitted bit. H4 v4 carries it as an inert
physics bit; command quantization, 20 ms cadence and movement/replay behavior
are unchanged. The older secondary-disabled helper remains explicit.
Attack2 transmission is generic, not filtered by weapon name. Successful local
secondary presentation in E16 is MP5-only; other weapons' secondary presentation
remains unsupported rather than silently mapped to M203.

Only selected, owned, compatible MP5 presentation can start
`LocalWeaponAction::secondary_launch`. WeaponList's secondary ammo type
selects authoritative reserve observations; missing/zero ARgrenades does
not create a successful launch. Primary clip zero is not secondary ammo
zero. Confirmed waterlevel 3 suppresses successful local launch; absent water
evidence is not fabricated from camera height. Current reload, server attack
timers and action state gate eligibility.

Both attack bits choose secondary, not both presentations. A provisional
secondary starts no faster than one second after the previous secondary;
that interval also gates primary. Secondary additionally honors the local
100 ms primary-to-secondary deadline even while a server timer zero is
retained. No sleeps or render-frame cadence are used. Presentation-only
grenade reservations prevent repeated held launches against stale reserve
ammo; canonical ammo/HUD/server state are never decremented locally.

The existing validated MP5 model profile resolves the `grenade` role to
numeric sequence 2/body 2. The previously inspected actual v_9mmAR role has
34 frames at 30 fps; synthetic fixtures independently exercise the role,
not an installed-asset dependency. Mismatched labels/body/metadata are
unsupported, not a Glock fallback. The renderer only executes numeric pose.
Visual pitch punch is -10, bounded through the existing local camera path
and fixed 20 ms decay; authoritative/usercmd angles are untouched. Server
nonzero punch retains precedence. The launch idle clock is delayed to +5
seconds without delaying RX/input or inventing authoritative idle state.

One action produces one launch cue through the existing approved local
sound preload and mixer: deterministic glauncher1/2 choice, volume 1,
pitch .94..1.09, automatic independent voice. A local cue is local mix;
remote cues use committed world origin and attenuation .8. There is no hks
sample, primary flash/light, shell, tracer, static bullet trace, bullet
impact/decal/particles or local grenade/explosion on secondary.
`m203_launch_muzzle_flash_source=none` for the mp52 presentation path.
Existing independently observed entity effects retain their own ownership;
mp52 does not invent or duplicate them.

Fresh secondary reserve decrease, a fresh next-secondary timer increase or
an exact committed local mp52 can confirm the pending launch. A local echo
does not restart pose, recoil or audio. Completed launch animation retains
a bounded pending confirmation until the 1.5-second action deadline; an
outstanding MP5 primary is retained in the existing 16-action ACK window.
The local launch event ring holds 32 exact provenance identities. Unknown
names, conflicting assigned indices, missing arguments, stale generation,
old model/life boundary and invalid time are explicit non-launch paths.
Bindings survive the initial 0-to-first-generation controller setup only;
later generation changes require fresh advertisement.

## Remote launch and server projectile

Runtime event index is bound by the session-advertised virtual name
`events/mp52.sc`, never a fixed numeric index. A non-receiving emitter gets
one world-spatial launch cue, with no observer recoil and no primary weapon
effect package. Remote body sequence and weapon_model_index/p_9mmAR remain
server-owned through E10/E11/E13; no p-model replacement, detach or local
attack-pose restart is introduced. Duplicate/replayed events create no new
cue. Two independent events/emitters remain independent.

The actual grenade is an ordinary server packet entity. Model index resolves
the session manifest's approved Studio asset; `models/grenade.mdl` can be
used for diagnostic correlation, not filename-triggered physics. Rendering
does not require owner, a matching mp52, or a locally predicted launch.
Pre-E16 audit found ordinary Studio entities could render a valid static
pose, but their transforms were discrete: generic kinetic interpolation was
not opted in. E16 adds the narrowly bounded ordinary-Studio kinetic opt-in
for confirmed public movetype 5/6/9/10/11; missing/unknown/static movement
types remain discrete. Ordinary wire movetype currently resides in the
legacy-named `RuntimePacketEntityObservation::brush_move_type`; it is not
the player-only `move_type`. The ready-Studio selector reads that confirmed
ordinary field. It is not a grenade filename or game-event switch.
Generic snapshots provide origin/angles/sequence/frame; generic entity
interpolation honors current discontinuity/EF_NOINTERP, model/sequence/schema
changes, teleport and generation boundaries, with no extrapolation. No
guessed angular spin, velocity integration,
gravity, contact trace, radius damage, kill or explosion is implemented.

Network/map generation, entity lifetime and resource identity own the
projectile. Server removal removes it; slot reuse must not interpolate old
grenade state into another object. Viewmodel switch, reload or player death
does not own this world entity. Visibility is best-effort from actual server
snapshots: a fast contact grenade can be too brief/unobserved before its
explosion. Missing packet evidence must not be labeled render failure or
replaced with a fake projectile. Diagnostics separate entity observed,
resource resolved, renderable pose/interpolation, visual observed and
too-brief/unavailable/render failure. Exact requested audit fields:

| Field | Pre-E16 code audit | E16 evidence/status |
| --- | --- | --- |
| `grenade_entity_received` | Ordinary packet-entity observation path exists | Project-owned committed entity fixture; live_pending |
| `grenade_model_resolved` | Approved model-slot/Studio binding exists | Sparse approved fixture slot and model changes; live_pending |
| `grenade_pose_renderable` | Static ordinary Studio pose supported | Normal asset materialization/GL fixture, no event/owner dependency; live_pending |
| `grenade_interpolation_current` | Ordinary transform remained discrete | New generic confirmed-kinetic opt-in fixture; live_pending |
| `grenade_effects_current` | Server fields/current-list removal remain authority | Effects/discontinuity/removal fixtures; live_pending |

These are passed implementation/fixture coverage statuses, not received live
grenade observations; final offline results belong to the run table below.

## Exact temporary-entity grammar and atomic publication

All coordinates below are signed little-endian 16-bit eighth-unit values.
Sizes exclude the svc opcode and subtype byte. Sprite references are
signed 16-bit wire values; invalid/nonpositive references cannot resolve an
approved sprite. No opcode scan/resynchronization or guessed skip is added.

| Subtype | Exact body | Bytes | E16 presentation |
| --- | --- | ---: | --- |
| 3 TE_EXPLOSION | xyz coords; sprite short; scale byte; fps byte; flags byte | 11 | Committed explosion components |
| 5 TE_SMOKE | xyz coords; sprite short; scale byte; fps byte | 10 | Committed smoke sprite |
| 9 TE_SPARKS | xyz coords | 6 | Exact safe decode; visual deferred |
| 113 TE_BUBBLES | min/max coordinate triples; height short; sprite short; count byte; speed short | 19 | Exact safe decode; visual deferred |

Existing TE_BLOODSPRITE and decal grammar are retained. Unknown subtypes
keep typed unsupported failure under the current parser policy, not byte
guessing. Truncation rejects the containing record atomically. Temporary
observations are staged with the record, own their data, and publish through
GameClientHost only after the shared semantic commit succeeds. No pointer
into a released RX buffer or renderer state exists before commit. Replay
fingerprints and exact `(generation, record, message_bit_offset,
entry_ordinal)` effect identity suppress duplicates; two valid explosions
at identical coordinates remain distinct.

GameClientHost performs the existing single clock-origin conversion and
forwards the owning immutable observation through the same API used by the
independent test module. The module selects components; the application
owns bounded lifetime; the renderer executes neutral requests. Drain/consume
accepts at most 250 ms old events and never replays expired pending effects
later after resource readiness.
Origin subtraction tolerates at most one microsecond of floating-point future
clock noise and clamps that presentation start to now; genuinely future events
remain rejected. This does not schedule new effects from a local timer.

Verified explosion flags are independent low bits; all 16 combinations have
an explicit component interpretation:

| Bit | Public flag meaning | Implemented component behavior |
| --- | --- | --- |
| 1 | NOADDITIVE | Sprite opaque instead of additive |
| 2 | NODLIGHTS | No new explosion light |
| 4 | NOSOUND | No main-audio evidence_pending counter; no invented voice |
| 8 | NOPARTICLES | No new explosion particle burst |

Unknown extension bits are safely consumed by framing but typed
`unsupported_flags` presentation rejection, not silently reinterpreted.

## Explosion/smoke resources, profiles and bounds

Sprite slot comes from the committed server temp entity, resolves existing
approved session bindings and imported SpriteRenderAsset, and shares the
normal package/GPU cache. No hardcoded resource slot, arbitrary filesystem
open, per-event/per-frame import or copied SPR/WAV/MDL is used. Missing or
unsupported sprite is a typed component skip, independent of light,
particles and server audio. Materialization records invalid, absent,
unsupported, expired, frame_rejected and submitted separately; submitted
does not itself prove pixels. Scale is server byte*.1; animation is
floor(age*server_fps), nonlooping and bounded by imported frame count.

Normal explosion is additive, opacity 1; flag 1 selects opaque. Smoke uses
source image coverage with alpha blending, opacity .65 and linear animation
fade. Sprite blend override is neutral; depth testing/state restoration
remain shared. OpenGL tests compare actual pixels, animation and occlusion,
not only submissions. Explosion rises at 8 units/sec; smoke at 30; active
sprite ownership is capped at 4 seconds and actual frame exhaustion can
stop drawing earlier. These speeds, fades and bounds are **local-compatible**,
not stock-engine constants.

Explosion light is local-compatible: radius 192, initial intensity 1.1,
RGB (1,.55,.2), .35-second linear decay. Distinct explosion particle profile
uses the existing E11 pool: deterministic radial/flickering emissive burst,
16 particles, .65-second lifetime, speed 96..240, gravity 120, radius 4,
RGBA (1,.56,.15,.8). It is not the bullet-impact profile or authoritative
physics. Particle timing starts at bounded application consumption, at most
250 ms after event time; exact proprietary engine timing is not claimed.

| Owner | Bound / overflow policy |
| --- | --- |
| Module pending batch | 32 intents; component batch drop with capacity reason |
| Module/application recent identities | 256 entries each; exact-provenance bounded rings |
| Application sprite/light pools | 32 sprites / 16 lights; component-only cyclic eviction |
| Existing particle pool | 32 bursts, at most 16 particles/burst, 128 recent identities |
| RenderScene shared limits | 8 world-effect lights / 256 world particles; existing remote priority retained |
| Sprite lifetime | At most 4 seconds, nonlooping imported animation |

Capacity never closes a connection, rejects otherwise valid packet state,
disables HUD/audio or creates unbounded emitters. Weapon/life changes do not
erase already committed world explosion/smoke. Network/map/resource reset
and teardown clear pending/live effects and old generation identities.
The main loop uses an opt-in bounded 16-report diagnostic budget, not
per-frame spam or native asset paths.

Lifecycle limitation: normal live runtime currently uses one fixed-map
RuntimeReplaySession/network generation. Same-connection changelevel is not
implemented or proven by E16. Module reset clears pending/seen events; the
application clears world-effect owners on network generation or BSP resource
identity change. Committed temporary-effect provenance contains network
generation, not an independent map epoch: a hypothetical retained API event
manually reinjected after a same-generation map reset cannot distinguish that
old map. There is no such asynchronous stale-event source in the normal path.
Do not infer full changelevel support from fixed-map/reset regressions.

## Audio, water, scorch and authority limitations

`main_explosion_audio=evidence_pending`. Public TE_EXPLOSION semantics prove
a sound role, but no exact main sample/profile is established by current
pinned evidence. E16 does not invent a sample and does not call the launcher
sound an explosion. The separate existing server svc_sound path preserves
debris1/2/3 as server-selected precache cues. TE_EXPLOSION never duplicates
those sounds. NOSOUND suppresses the pending main-audio component only,
not an independently committed server svc_sound.

Dry-world crossfire is the intended manual acceptance path. A server-selected
water explosion sprite can use the generic sprite path; valid TE_BUBBLES and
TE_SPARKS decode exactly without session termination, but bubble/spark-shower
visual systems are explicitly deferred. Full underwater visual/audio parity
is not claimed. `water_manual=not_run` even if dry fixtures pass.

Read-only installed asset control found grenade IDST; zerogxplode/WXplo1
additive orientation 2, 64x64, 15 frames; steam1 orientation 4, index-alpha,
32x48, 16 frames. These findings guide the existing generic coverage/blend
path. The separate approved importer/PCM/GL control passed 71 assertions:
grenade model, zerogxplode/WXplo1/steam1 frames and GPU coverage, glauncher
and glauncher2 finite nonzero decoded/mixed PCM, no repeat sprite upload,
and unchanged modification times for all seven read-only assets. This is
not a build dependency, live projectile proof or a listening claim.
The verified target sprites use single-frame sequences; general grouped-SPR
interval/stock timing parity is not established by those controls.

Server scorch decal messages retain the existing exact RuntimeControlDecal /
BspDecal decoder, but are not forwarded/rendered by the new temporary-effect
path. Server scorch visualization and spark_shower visuals remain deferred;
TE_EXPLOSION creates no extra local scorch. Damage,
health/ammo, blast occlusion, DeathMsg and ScoreInfo are never authored by
the E16 effect path. Server death/corpse/respawn uses E13; server notice/score
uses E14. TAB and primary presentation have no E16 authority mutation.

## Preserved E15 tracer shape and behavior

No E16 retuning: normal 3D default and retained flat variant, 48-unit streak,
12,000 units/sec, .078-second lifetime, local/remote widths .65/1.30, stable
per-action muzzle offset 0..16, rounded leading end, rear taper beginning
10% behind the front (taper .90), rear equator 20% body diameter with round
cap, glow radius1.25/light radius48/intensity.35, alternating first/every-
other primary shot. Frozen debug inspection remains opt-in and unchanged.
These are primary visual profiles only; secondary creates none of them.

## Regression and acceptance checklist

Results below were obtained after the last production edit and rebuilt
binaries. Offline fixtures are project-owned; installed resources/device
checks are separate opt-in evidence, never core/build dependencies or live proof.

| Acceptance topic | Implementation / regression evidence | Final result |
| --- | --- | --- |
| Exact assigned mp52, unknown/reset/duplicate handling | `test_mp5_secondary.cpp`; existing mp5 event tests | passed offline |
| Local grenade role/body, -10 visual recoil, launch sound, no primary package | Normal GameClientHost/module composition; isolated synthetic PCM | passed offline |
| ARgrenades/missing/zero/full-water/reload/timer/cross-attack gates | Secondary tests including stale timer0 primary100ms guard | passed offline |
| Held cadence, both buttons, late ACK, no render/replay side effects | Secondary fixed-command/FPS tests; existing action/window tests | passed offline |
| Remote spatial launch, independent emitters, zero observer recoil/effects | Remote module/mixer fixtures; no p-model mutation | passed offline |
| Immutable secondary history/backups and inert H4 movement | Wire/input/adapter/prediction regressions | passed offline |
| Ordinary grenade resource, interpolation/removal/slot identity, actual GL | `test_entity_snapshot_interpolation.cpp`, `test_runtime_replay_local_assets.cpp`; generic approved kinetic Studio path | passed offline |
| TE_EXPLOSION/SMOKE exact boundary and malformed suffix atomicity | `test_runtime_control_decoder.cpp`, `test_runtime_replay_session.cpp` | passed offline |
| Mixed bloodsprite/explosion/smoke/decal cursor boundaries | Literal independently sized control stream fixtures | passed offline |
| Same host/API alternate test module, no Half-Life fallback | `test_game_client_module.cpp`; core-only/guard | passed offline |
| Flags, duplicates/same-position distinct events, map/life/reset/capacity | `test_temporary_effect_presentation.cpp`; fixed-map scope only | passed offline |
| Approved sprite lookup, frame progression/material/absent components | Local-assets fixtures; `test_opengl_sprite_entity_renderer.cpp` | passed offline |
| GL pixels, additive/opaque/coverage, depth/state/expiry/no repeated upload | Actual production GL A/B; Null neutral validation | passed offline |
| Light/particle bounds and independent server debris cue | Temp-effect/session/audio coexistence fixtures | passed offline |
| E13 death/corpse/respawn and mixed entity baselines | Existing and mixed-stream regressions | passed offline |
| E14 DeathMsg/ScoreInfo/TAB and E15 primary regressions | Existing server-derived game/message/presentation fixtures | passed offline |
| Installed grenade/explosion/smoke/launcher resources | Optional `test_runtime_replay_local_assets.cpp` approved read-only imports/PCM/GL, not listening | passed separately, 71 assertions |
| Main explosion audio | Evidence pending; server debris not substituted | evidence_pending |
| Water bubbles / generic spark-shower / full scorch parity | Exact safe supported framing, visual scope above | limited/deferred |
| User listening, projectile visibility, dry/water multiplayer | User-controlled manual scenario only | not_run |

Final verification (VS2022 / Win32; incremental existing build trees):

| Check | Final result / evidence |
| --- | --- |
| VS2022/Win32 normal Release application/helpers/tests | passed; full normal Release target build, focused Debug targets rebuilt |
| Focused Debug E16 and affected regressions | 189 passed / 4 optional skips, 109,638 assertions |
| Focused Release E16 | 42 passed / 1 optional installed-asset skip, 1,865 assertions; asset skip separately exercised |
| Complete final Release offline suite after last production edit | single full serial run: 2,800 registered tests, 2,760 passed / 40 capability or optional skips / 0 failures; 416.98 seconds |
| Serialized fake-process/time-sensitive checks | 9/9 passed, including unchanged campaign gate isolated after fixture race |
| Actual OpenGL / Null / prediction hashes | production GL pixel/depth/blend/cleanup tests passed; Debug/Release version/help/Null passed; 14 scenarios x 1,000 commands gave 28 matching state/history hash pairs |
| Core-only Half-Life OFF and direct/transitive architecture guard | 41/41 passed; core/API executable 211 cases / 87,004 assertions; no Half-Life objects/headers/link dependency |
| `asan_build` | passed for bounded core/API target using existing configuration |
| `asan_core_api` | 39 cases / 2,858 assertions passed with matching x86 runtime on PATH |
| `asan_application_runtime` | not_run/not_verified; not inferred from core/build |
| Launcher reference/off CheckOnly | passed for reference peer/no-limit, off, damage-respawn-check and SourceOnly; process-launches=0 |
| No-stock/acceptance supplemental fixtures | passed: transport self-test, functional-log validator, G jump/duck and H1 speed summaries, H2 no-stock launcher, Fast five-file/legacy recovery and SourceOnly CheckOnly |
| Snapshot preservation / staged diff / git diff --check | 1,137 unchanged + 67 intended changed + 0 missing from 1,204; nine new source/doc/test files; staged empty; branch/HEAD/SDK and assets unchanged; diff check passed |

The parallel Release segment ran 2,792 tests in 149.76 seconds: 2,751 passed,
40 skips, one failed campaign metadata-invariance gate. The unchanged
research-manifest fixture concurrently creates/removes entries below the
same watched `manual-artifacts/stock-runtime` directory, changing its mtime.
An isolated serial segment passed that same campaign test and all eight
previously separated time-sensitive tests (9/9, 30.10 seconds). Thus final
verification was repeated as a full serial run: all 2,800 registrations
completed with zero failures. Its 2,642 captured Catch pass summaries contain
1,025,360 assertions (PowerShell assertions/capability skips are separate,
not invented into that total). The initial race and log are retained, not
relabeled as a clean first attempt. No assertions or production code were
changed to obtain the isolated or final full pass.

Forty standard-suite skips are optional installed resources, opt-in WFP/device
or interactive SDL tests, and filesystem/link/capability cases. WFP opt-in
remained 0; the local game root was unset in the mandatory suite. Actual E16
approved-asset control was run separately. Real-device playback/listening was
not run; nonzero decoded/mixed PCM is not human listening confirmation.
The local acceptance checks use the existing `.github/workflows/windows.yml`
VS2022/Win32 configuration and supplemental commands; no remote CI job, clean
build or submodule update is claimed.
The earlier full-application ASan runtime problem is not claimed repaired:
E16 executed only the bounded core/API target, separately from normal Debug/
Release application checks.

Reproducible normal/core-only build and final offline commands (existing trees,
VS2022 developer shell with CMake on PATH; no live runner):

```powershell
$env:HLCLIENT_RUN_WFP_CAPABILITY_TEST = '0'
$env:HLCLIENT_LOCAL_GAME_ROOT = $null
$env:HLCLIENT_LOCAL_RESOURCE_TEST_ACCESS = $null
cmake --build build --config Release --parallel 1
ctest --test-dir build -C Release --output-on-failure -j 1
cmake -S . -B build-core-g1 -G "Visual Studio 17 2022" -A Win32 -DHLCLIENT_BUILD_GAME_HALFLIFE=OFF -DBUILD_TESTING=ON
cmake --build build-core-g1 --config Release --parallel 1
ctest --test-dir build-core-g1 -C Release --output-on-failure -j 1
```

Run evidence is retained under
`manual-artifacts/task-records/e16-m203-20261003`: pre-edit source manifest,
`focused-debug-final.txt`, `focused-release-final.txt`,
`release-offline-final.txt`, `release-parallel-initial.txt`,
`release-parallel-LastTest.log`, `release-final-LastTest.log`,
`release-serialized-final.txt`, `core-tests-final.txt`,
`asan-core-final.txt`, `assets-e16-final.txt` and launcher/supplemental reports.

## Release and user manual handoff

This section records the **initial E16** binary; the latest corrected binary
and its checks are in the follow-up section below.

### E16-only source inventory

This inventory compares the preserved dirty pre-E16 snapshot, not HEAD: a
whole-worktree Git diff also contains earlier user/E11–E15 work and must not
be attributed to E16. All 1,204 snapshot files still exist. Nine new ordinary
project files (no generated or private production-code dependency):

```text
docs/MP5_M203_SECONDARY_E16.md
include/hlclient/client/temporary_effect.hpp
include/hlclient/game_api/temporary_effects.hpp
include/hlclient/app/temporary_effect_presentation.hpp
include/hlclient/games/halflife/temporary_effects.hpp
src/app/temporary_effect_presentation.cpp
src/games/halflife/temporary_effects.cpp
tests/test_mp5_secondary.cpp
tests/test_temporary_effect_presentation.cpp
```

The 67 changed snapshot files are:

```text
docs/MP5_PRIMARY_FIRE_E15.md
include/hlclient/games/halflife/firearm_profile.hpp
tests/test_mp5_primary.cpp
CMakeLists.txt
Start-HLClient-H2-Manual.ps1
apps/hlclient/main.cpp
docs/ARCHITECTURE.md
include/hlclient/app/runtime_replay_local_assets.hpp
include/hlclient/entity_render/entity_scene_render.hpp
include/hlclient/entity_render/sprite_billboard_basis.hpp
include/hlclient/entity_render/sprite_render_asset.hpp
include/hlclient/game_api/audio.hpp
include/hlclient/game_api/game_client_host.hpp
include/hlclient/game_api/game_client_module.hpp
include/hlclient/game_api/local_visuals.hpp
include/hlclient/game_api/presentation.hpp
include/hlclient/game_api/remote_effects.hpp
include/hlclient/games/halflife/local_weapon_presentation.hpp
include/hlclient/games/halflife/presentation.hpp
include/hlclient/goldsrc/entity_snapshot_interpolation.hpp
include/hlclient/goldsrc/reference_client_move.hpp
include/hlclient/goldsrc/reference_prediction_command.hpp
include/hlclient/goldsrc/runtime_control_decoder.hpp
include/hlclient/goldsrc/runtime_replay_session.hpp
include/hlclient/goldsrc/usercmd_movement_policy.hpp
include/hlclient/renderer/render_scene.hpp
src/app/runtime_replay_local_assets.cpp
src/entity_render/entity_render_frame_composer.cpp
src/entity_render/entity_scene_render.cpp
src/entity_render/sprite_billboard_basis.cpp
src/entity_render/sprite_render_asset.cpp
src/game_api/game_client_host.cpp
src/game_api/movement_policy.cpp
src/games/halflife/client_module.cpp
src/games/halflife/local_weapon_presentation.cpp
src/games/halflife/movement_policy.cpp
src/games/halflife/presentation.cpp
src/games/halflife/remote_effects.cpp
src/games/halflife/weapon_audio.cpp
src/goldsrc/entity_snapshot_interpolation.cpp
src/goldsrc/live_runtime_stage.cpp
src/goldsrc/movement/goldsrc_local_movement.cpp
src/goldsrc/reference_brush_collision.cpp
src/goldsrc/reference_prediction_command.cpp
src/goldsrc/reference_prediction_seed.cpp
src/goldsrc/runtime_control_decoder.cpp
src/goldsrc/runtime_replay_session.cpp
src/goldsrc/usercmd_input_adapter.cpp
src/goldsrc/usercmd_state.cpp
src/renderer/null/null_renderer.cpp
src/renderer/opengl/opengl_entity_renderer.cpp
src/renderer/opengl/opengl_renderer.cpp
src/renderer/transient_visuals.cpp
tests/CMakeLists.txt
tests/test_entity_render_frame_composer.cpp
tests/test_entity_render_sprite_asset.cpp
tests/test_entity_scene_render_package.cpp
tests/test_entity_snapshot_interpolation.cpp
tests/test_game_client_module.cpp
tests/test_goldsrc_usercmd_input_adapter.cpp
tests/test_half_life_client_module.cpp
tests/test_local_player_movement_integration.cpp
tests/test_opengl_sprite_entity_renderer.cpp
tests/test_reference_move_transmission.cpp
tests/test_runtime_control_decoder.cpp
tests/test_runtime_replay_local_assets.cpp
tests/test_runtime_replay_session.cpp
```

Normal Release target:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
New Release size: **3,636,736 bytes**. SHA-256:
`861180090E2174C5B903EFFDA978809728E4AA66FDB4A57746782C822DEFA91A`.
Launcher `ClientPath` selects exactly this rebuilt Release file. Its only
source change is the RMB instruction; parameters/selection are preserved.
No hidden E16
flag, demo executable, WAV copy or required new manual parameter is added.
Default crossfire, reference/off, damage-respawn-check, duration/no-limit and
peer selection remain the existing launcher contract.

Single client:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Two clients, user-controlled close (Codex does not run this):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

## 2026-10-06 launch-audio cache correction

`attribution=user_report`: MP5 secondary launch audio is absent in both clients.
The shared loader's 64-name quota rejected later client-effect samples after
the current preparation set exceeded it. The bounded quota is now 128; original
glauncher/glauncher2 semantics, grenade authority, movement and explosion
presentation remain unchanged. Shooter and observer launch PCM pass with a
previously filled cache. Full evidence, Release SHA and pending manual check:
[E2 shared audio-cache correction](LOCAL_WEAPON_AUDIO_E2.md).

## 2026-10-04 late-birth first-pose follow-up

New attribution=user_report: the first grenade pose/end cap remains at screen
center for approximately one second after firing, before flight. This report
does not separately confirm the other corrected visuals/audio or associate
the user's run with a retained executable SHA. Historical manual statuses and
verification above are not rewritten as acceptance of this correction.

Pre-change snapshot: 1,213 hash-verified files in
`manual-artifacts/task-records/e16-grenade-first-frame-20261004/source`.
Fresh selected Release baseline: 70 passed, 3 optional asset skips,
6,438 assertions. Branch/HEAD, existing dirty code, E15 tuning, SDK and
launcher are preserved. No new live/stock/HLDS/Steam/WFP/managed/capture session
is authorized or launched here; no staging/commit/push.

### Reproduced cause and repair

`RuntimeOrdinarySnapshotPresentation::sample` initialized a newly visible
ordinary lifetime at the newest server pose, but advanced it only when the
older session-wide clock reached that birth. A positive server/application
phase smaller than the recovery threshold was valid and retained indefinitely.
The newborn therefore waited for that phase plus the cadence despite receiving
moving server poses. Earlier tests spawned at zero phase and missed this case.

The new regression was compiled and run against unchanged production first:
core-only Release `[grenade-first-frame]` returned42, with six failing position
assertions out of498. Actual Half-Life policy (250ms gap guard,240ms phase)
heldx0 instead ofx8/16/32 at30/40/60ms. The generic1s guard with960ms phase also
failed. The latter is a core configuration control, **not** evidence that normal
Half-Life reproduced a literal one-second stall. Retained manual logs lack
per-projectile/render timing, so the exact reported duration remains unmeasured.

Each ordinary visual lifetime now retains its immutable birth server and
application anchors. A per-life presentation floor advances from that birth
after the existing one-cadence buffer, capped at the latest committed pose.
The sampled target is the maximum of this floor, the unchanged global desired
target and the previous rendered target. No global clock re-anchor, invented
trajectory, velocity/gravity integration or extrapolation is introduced.
Current removal, absent/intermediate reuse, untimed reset, discontinuities,
backwards/repeated clocks and bounded history remain unchanged. Player/brush
sampling, movement prediction, launch animation/audio and E15 tuning are intact.

The normal asset path prepares all precached models asynchronously before
presentation; grenade spawn does not open/import files or upload a new package.
The accepted-TX viewmodel clock advances from the existing immutable action start;
confirmations do not reset it. The actual launch animation is1.1s, whereas the
world grenade's sequence is only33ms: neither duration alone proves which graphic
the user observed. A separate future-idle/retained-canonical limitation after
launch **completion** was identified read-only; it is not a first-launch latch
and is not expanded into this narrow world-birth correction.

### Verification and new handoff

All following builds and tests ran after the last production/test edit. No old
suite counts are reused as acceptance of this first-pose correction.

| New verification | Result |
| --- | --- |
| Normal VS2022/Win32 Release / affected Debug | Incremental application, helpers and test builds passed; no clean or competing output build |
| Genuine red / green first-frame regression | Unchanged old production: 6/498 assertions failed; corrected CPU + normal HalfLife/approved Studio/production GL: 2 cases, 768 assertions passed |
| Actual GPU first-pose evidence | At30ms the instance moved0.8units and framebuffer signature changed; repeat/backwards sample signature remained identical; one Studio upload; removal returned zero non-clear pixels; 270 explicit successful assertions |
| Focused Release | 101 passed, 3 optional resource skips; 8,116 assertions |
| Focused Debug | 274 passed, 5 optional resource skips; 249,069 assertions |
| Core-only Half-Life OFF / architecture | 41/41; core/API 220 cases, 88,014 assertions; dependency/include guard passed |
| Scoped ASan core/API | 51 cases, 3,882 assertions with matching x86 runtime; full application ASan remains not_run/not_verified |
| Complete final Release offline suite | **Not green**: 2,831 registered, 2,789 passed, 41 optional/capability skips, 1 failure; 649.17s; CTest exit8 |
| Passing Catch output captured in that suite | 2,672 summaries, 1,031,711 assertions; scripts and skips counted separately |
| Isolated unchanged legacy test retry | Also failed: 1/1, 21.77s; a different fake lifecycle readiness/limit boundary; no assertions/timeouts edited |
| Normal Release / unchanged launcher | version/help/Null and reference-peer/no-limit, off, damage-respawn-check, SourceOnly CheckOnly all passed; actual launcher Release path verified |
| Preservation | 1,207 identical + exactly6 intended changes + 0 missing/new ordinary source files; all1,213 saved copies hash-verified; launcher/all74scripts/E15/SDK/branch/HEAD unchanged; staged empty; git diff --check passed |

The full-suite failure is
`hlclient_functional_publication_roundtrip_powershell51`, in the unchanged
project-owned fake lifecycle, **before normal hlclient.exe runs**. The first
failure returned native exit8/map-entry-not-ready: it could not establish either
fake-client ready or relay bidirectional traffic within the existing3s waits.
Its PowerShell7 counterpart passed. The isolated PowerShell5.1 retry advanced
beyond that first stage, then missed the client-packet-limit marker in a second
independent fake lifecycle. That requests max-client-packets4, relay exit7,
owned cleanup and incomplete journal publication. The wrapper's second failure
omits native exit/output and deletes its temporary fixtures in finally, so
the failed invariant and exact cleanup result are **unavailable**. Slow shell
execution was observed, but load/timeout is not established as root cause.
Relevant source and helper binaries predate this correction. This known
verification gap is not hidden or replaced by a successful focused result;
the complete suite and isolated retry are both retained as failures.

Logs/source snapshot are in
`manual-artifacts/task-records/e16-grenade-first-frame-20261004`, including
`baseline-release.txt`, `reproduction-red.txt`,
`first-frame-green-release.txt`, `first-frame-gpu-proof-release.txt`,
focused/build/core/ASan `*-final.txt`, `release-offline-final.txt`,
`release-final-LastTest.log`, and `legacy-publication-isolated-retry.txt` plus
its copied LastTest log. No further production changes followed these runs.

Normal executable: `build/bin/Release/hlclient.exe`, 3,656,192 bytes.
SHA-256: `71E0B6E2A340EA4190372325048F32414DC36397D7C77F233576B156F7A5AF10`.
No feature flag, alternate binary, game asset copy or launcher change is needed.
This binary's manual status is **not_run**, including the user's approximate
one-second observation. Full-suite verification remains incomplete as stated
above; the birth-clock defect itself has a genuine red/green normal-path proof.
New live/stock/HLDS/Steam/WFP/managed/capture sessions=0;
staging/commit/push=none. The existing manual command remains:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Check the first and subsequent M203 launches after moving around for a while:
the newborn should not remain at the initial center pose waiting for session
phase. Normal no-extrapolation endpoint holds when server updates are missing
remain explicit, not fabricated motion. Preserve MP5 primary/Glock RMB,
explosion/debris audio, switching/reload, removal and respawn checks.

## 2026-10-04 follow-up: grenade motion, main boom and Glock RMB

New attribution=user_report: the M203 grenade flies in jerks; the audible
explosion does not sound like a main explosion; Glock secondary works on the
server but lacks local animation/sound/effects. This does not assign an
executable SHA to the user's run. The earlier MP5 corrective report above is
retained, not rewritten as validation of this follow-up.

Pre-change source snapshot: 1,213 hash-verified project files in
`manual-artifacts/task-records/e16-grenade-glock-secondary-20261004/source`.
Baseline selected Release regressions: 78 passed, 2 optional asset skips,
3,530 assertions. Existing branch, HEAD, SDK revision, E15 tracer tuning,
launcher and unrelated dirty work are preserved.

### Independent causes and normal-path repairs

Grenade presentation was anchored again at each received snapshot publication
in `RuntimeReplayLocalAssets::present_entities`. The existing pair interpolator
did not extrapolate, but irregular receipt timing could move its presentation
target forward abruptly or hold it. This is a presentation-clock defect, not
proof of a player prediction or server grenade physics defect. Per-projectile
timestamp/transform telemetry is not retained in the inspected manual metadata.
The correction keeps a stable server/application clock and bounded committed
history for ready ordinary kinetic Studio entities; players and brushes retain
their existing paths. Current server removal/flags/visibility remain immediate.

Main explosion sound was not implemented: TE_EXPLOSION produced only visual
intents and `main_audio_evidence_pending`. The separate svc_sound debris already
worked. The HL profile now selects actual approved `weapons/explode3.wav`,
`explode4.wav` or `explode5.wav`; normal startup preloads all three, and the
normal application submits a spatial one-shot at committed explosion origin
through TemporaryAudio and the existing mixer. NOSOUND4, absence/readiness,
duplicate/stale events and output rejection remain explicit. The independent
server debris sound is preserved without a second local debris request.

Pinned official SDK confirms TE_EXPLOSION client sound and NOSOUND, but does not
publish the proprietary engine sample RNG/gain/attenuation. Deterministic
sample selection, gain1, pitch1 and attenuation0.8 are documented as
local-compatible. Read-only installed explosion family controls found mono
PCM8/22050 Hz, about1.3 s, nonzero samples; these are not device/listening proof.

Glock's controller accepted only Attack for its local bullet-fire action despite
Attack2 reaching the server. It now selects the pinned secondary sequence3/body2
(including the last round), secondary .2 s versus primary .3 s shared cycle and
server timer/reload/clip eligibility. The existing neutral primary_fire action
executes gunshot, recoil, animation, flash/light, brass and impacts. An assigned
glock2.sc echo confirms without restarting. Remote glock2 support already existed
and receives an independent regression; no separately proven remote-layer
defect is claimed. Canonical ammo, damage, wire grammar and prediction stay
unchanged. This user's explicit Glock request extends the earlier MP5-only
secondary presentation scope without starting another weapon milestone.

The world audio namespace is separate from local/remote fire and server entity
voices; map/session stop_group cannot silence those channels. The exact256
recent provenance window rolls rather than becoming silent after256 explosions.
Weapon switches/death do not cancel an explosion; map reset cancels pending
and active world voices. Approved virtual references stay owning and bounded;
there are no arbitrary paths, new audio devices or new game assets.

### Verification and handoff

The existing VS2022/Win32 normal Release application/helpers and affected
Debug targets were rebuilt. The following results are new runs after the
last production/test edit (including the newer untimed-publication reset
guard), not the previous MP5 corrective counts reused as acceptance.

| Corrective verification | Result |
| --- | --- |
| Final focused Release | 99 passed, 3 optional installed-resource skips; 7,348 assertions |
| Final focused Debug | 272 passed, 5 optional installed-resource skips; 248,301 assertions; actual first-person/weapon-presentation pose tests ran, not the unmatched `[viewmodel]` tag |
| Complete final Release offline suite | 2,829 registered; 2,788 passed, 41 optional/capability skips, zero failures; 392.65 seconds |
| Passing Catch summaries captured in that suite | 2,670 summaries, 1,030,445 assertions; script assertions and skips are separate |
| Core-only Half-Life OFF / architecture | 41/41; core/API 219 cases, 87,516 assertions; direct/alias/transitive/object/include guard passed |
| Scoped ASan core/API build and runtime | 50 cases, 3,384 assertions; matching x86 runtime; full application ASan remains not_run/not_verified |
| Debug/Release prediction | 14 scenarios x 1,000 commands per configuration; 28 identical state/history hash pairs; network operations=0 |
| Production GL and read-only installed controls | Actual project-owned ordinary Studio short-flight/removal and existing weapon/remote GL tests passed; exact approved MP5/M203/explosion controls: 3 cases, 322 assertions |
| Isolated audio | Project-owned main-boom spatial PCM and gunshot/debris coexistence, pending/reset/dedup/rolling-window controls passed; actual approved explode3/4/5 each reached finite nonzero isolated production PCM |
| Normal process / unchanged launcher | Debug/Release version, help and Null startup; reference peer/no-limit, off, damage-respawn-check and SourceOnly CheckOnly passed |
| Supplemental no-stock fixtures | G jump/duck, H1 speed, H2 launcher, isolated Fast restoration/recovery, client-move transport self-test and functional-log observation passed; Fast passed on its first isolated attempt |
| Preservation | 1,184 byte-identical + 29 intended changed + 0 missing/new ordinary source files from the 1,213-file snapshot; staged empty; branch/HEAD/SDK, E15 tracer implementation/tuning and all 74 scripts plus launcher unchanged |

Mandatory tests use project-owned fixtures without an installed game or sound
device. The complete offline suite disables WFP and unsets game/device opt-ins;
the separate positive installed controls use only the exact approved research
root read-only. Device output and human listening are **not_run**. Nonzero PCM
is not a claim of heard sound. The scoped core ASan runtime pass does not
retroactively repair or certify the earlier application ASan startup failure.

The first build attempt encountered Windows' `far` macro in a newly added
test variable. Renaming only that test variable to `distant_sink` preserved
the assertions; the failed build log and successful final rebuild are retained.
An earlier malformed version-check argument invocation was corrected to literal
arguments, not treated as a production defect. Existing interpolation fixture
timestamps were aligned with its server cadence without changing its expected
positions; no old gameplay or wire assertions were weakened.

Logs and the hash-verified pre-change source are in
`manual-artifacts/task-records/e16-grenade-glock-secondary-20261004`:
`build-release-post-guard.txt`, `build-debug-final.txt`,
`release-offline-final.txt`, `release-final-LastTest.log`, focused/core/ASan,
installed controls, prediction comparisons and supplemental `*-final.txt` logs.
Final `git diff --check` passed. No installation assets, launcher defaults,
server files or isolation policies were modified.

Normal executable: `build/bin/Release/hlclient.exe`, 3,655,680 bytes.
SHA-256: `0E589F7D869BD4EB06314DCCD95536947DE3E084AEFD7746BF2385F28FDB7C91`.
The unchanged launcher resolves that exact Release executable; no hidden flag,
alternate executable or manual asset copy is required.

The ordinary buffer holds at most 16 committed snapshot references and one
observed cadence capped at 100 ms; it does not predict a grenade trajectory.
Current removal is immediate, and newer untimed publication breaks history
rather than bridging an unobserved removal/reuse. Arrival gaps beyond the
buffer can still cause a brief endpoint hold. Explosion variant/gain/attenuation
are local-compatible, not stock-engine RNG or binary parity. Existing deferred
water/spark/scorch and beam-follow presentation limitations remain; this repair
does not add their implementation.

New build manual=not_run; water_manual=not_run. New live, stock-client, HLDS,
Steam, WFP, managed and capture sessions=0; staging/commit/push=none. Offline
synthetic loopback transport checks are not stock/live proof.

Use the unchanged normal manual command:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

For this follow-up, first check Glock RMB animation, gunshot, flash/light,
brass and impact in both views, then acquire server ARgrenades and check M203
flight and the main boom separately from later debris. Repeat primary/secondary
switching, reload, weapon switch, death/respawn and two independent emitters.
M203 launch intentionally has no primary brass/tracer/flash. The earlier manual
checklists below describe historical implementation scope; they do not negate
the now-implemented main explosion sound. New listening/live confirmation must
be recorded as attribution=user_report, not inferred from these offline tests.

Manual checklist: obtain MP5/ARgrenades; verify E15 primary first; RMB should
show grenade launch, bounded recoil and glauncher sound with no primary
shell/tracer/flash. If actual snapshots expose the grenade, observe independent
world motion, server angles/removal, weapon-switch/death independence and
separate A/B entities. At server contact check short explosion sprite/light/
bounded particles, later server smoke and debris cue without invented main
audio or uncontrolled duplication. Check no permanent effects, both clients
connected, primary/secondary switching, reload, TAB, server kill feed/score,
smooth E13 death/corpse/respawn. Classify an unseen brief projectile by packet/
resource/render evidence rather than immediate failure. Dry acceptance and
any later water listening/visual confirmation remain `attribution=user_report`;
until provided, this new build's manual status is **not_run**.

## Normal-path corrective follow-up

User report, `attribution=user_report`: MP5 primary animation absent,
secondary animation absent, secondary fire inactive. This does not overwrite
the initial E16 offline counts or its historical `manual=not_run` entry.
The corrected build requires a new manual test; `manual=not_run` and
`water_manual=not_run` remain its current statuses.

### Proven production defects and repairs

1. `HalfLifePresentation::viewmodel` in `src/games/halflife/presentation.cpp`
   accepted local animation intents only for Glock/crowbar. The MP5 controller
   could create a valid action while the normal viewmodel retained server idle.
   The gate now additionally accepts active MP5 with the same validated
   `mp5_sequences` profile. Numeric roles/body validation, asset readiness,
   authority, camera-local transforms and existing Glock/crowbar rules remain.
2. `make_live_visual_intent` in `src/goldsrc/live_runtime_stage.cpp` reconstructed
   held primary/reload/Use but omitted secondary. Weapon buttons intentionally
   are not in the movement/Use press latch, so there was no press fallback:
   pure RMB became wire buttons 0. It now reconstructs held secondary only
   when the selected explicit secondary policy permits it and capture/focus
   are valid; the latch and legacy secondary-disabled policies are unchanged.
3. `update_usercmd_after_driver` notified presentation of accepted primary/
   reload TX only, omitting Attack2. Its existing accepted-new-command
   notification gate now includes secondary. Queue/history/TX ownership stays
   in the host; render/update/replay do not invent a weapon notification.

Previous tests exercised controller/adapter results, not these final normal
composition boundaries. New tests cross the missing boundaries rather than
relaxing assertions: actual LiveRuntimeStage/fake-peer Netchan payload2048,
20 ms commands, exact accepted-TX notification and owning RX in the same update;
normal GameClientHost.viewmodel role/body/frame progression over retained server
idle; approved Studio materialization and production GL pixel/camera/upload
checks. Focus/capture/release recovery, duplicate/replay suppression, late ACK,
invalid model/body and unchanged server ammo remain explicit negative controls.

### Existing manual run and separate protocol failure

The latest retained unlimited crossfire peer run is
`42fbac766fb74051be3990339bcd854f` (directory timestamp 2026-10-03 23:15:42 +03:00):
server PID18716, client PID22224, peer PID22824. The peer recorded90 primary
presentation starts, consistent with controller work not reaching the gated
MP5 viewmodel. No per-run client executable SHA was retained; the subsequently
inspected file SHA is not retroactively assigned to that run.

Separately, client exit2 was a real protocol error, not a coverage verdict:
`unsupported_temporary_entity_type=22`, opcode23, record1165, source sequence1179,
message byte306, failure byte308 (bit cursor2448). Peer exit was0.
Publication was complete; owned cleanup=exact; scoped restoration=scoped_exact;
cleanup errors=0. No cleanup repair or new live launch was performed.

Pinned SDK `common/const.h` defines TE_BEAMFOLLOW22 exactly: packed entity/
attachment short, sprite short, life/width bytes, RGB bytes, brightness byte
(10 body bytes, excluding opcode/subtype). The decoder now retains that typed
inert observation with every-byte truncation and mixed-record atomicity tests.
The following service byte stays exact; malformed suffix still publishes
nothing; duplicate records do not replay audio. Other unknown subtypes still
fail explicitly without scanning or guessed skips. Beam-follow visuals remain
deferred; no RPG/hornet/other weapon gameplay is added. `ShootContact` does not
prove this subtype is an M203 launch effect, so it is not presented as the
cause of the three independently proven MP5 defects above.

### Preservation, verification and latest binary

Pre-repair snapshot: 1,213 hash-verified project files under
`manual-artifacts/task-records/e16-mp5-manual-fix-20261003/source` and its manifest.
Earlier E16/tracer/source work and historical logs are retained. Branch/HEAD,
SDK, resources, launcher parameters/defaults and live isolation remain unchanged.
New live/stock/HLDS/Steam/WFP/managed/capture sessions=0; staging/commit/push=none.

The corrected normal Release application/helpers and affected Debug targets
were rebuilt using the existing VS2022/Win32 configurations. The complete
Release suite below ran after the last production/test edit, serially, with
game-resource and WFP opt-ins unset/disabled. These are new results, not the
earlier E16 pass reused as evidence for the repairs.

| Corrective verification | Result |
| --- | --- |
| Focused Release E16 | 49 passed, 2 optional asset skips; 2,620 assertions |
| Optional exact approved MP5/M203 controls | 2 cases, 199 assertions; actual imported normal viewmodel/Studio/GL animation and camera checks, read-only assets, launch PCM |
| Focused Debug E16/E15, first-person, life and viewmodel regressions | 102 cases, 87,079 assertions; no skips/failures |
| Complete final Release offline suite | 2,807 registered; 2,767 passed, 40 optional/capability skips, 0 failures; 535.58 seconds |
| Captured passing Catch summaries in that suite | 2,649 summaries, 1,026,115 assertions; script assertions/skips are separate |
| Core-only Half-Life OFF / architecture guard | 41/41; core/API 211 cases, 87,004 assertions; direct/alias/transitive/object/include guard passed |
| Existing bounded ASan core/API build and runtime | 39 cases, 2,858 assertions passed with matching x86 runtime; full application ASan remains not_run/not_verified |
| Prediction Debug/Release | 14 scenarios x 1,000 commands; 28 identical state/history hash pairs |
| Normal Debug/Release process-level checks | version/help/Null startup passed; actual production OpenGL tests passed |
| Unchanged launcher CheckOnly | reference peer/no-limit, off, damage-respawn-check, SourceOnly passed; live process launches=0 |
| Supplemental no-stock fixtures | G jump/duck, H1 speed, H2 launcher, Fast restoration/recovery, transport and functional-log observation passed |
| Preservation | 1,203 unchanged + 10 intended changed + 0 missing from 1,213; staged empty; branch/HEAD/SDK, E15 tracer profile and launcher unchanged; git diff --check passed |

The first newly written live-input test incorrectly expected a stale held RMB
to bypass the initial life-epoch release barrier. It failed with wire0 and no
notification. The test-only setup now explicitly asserts that suppression,
supplies a real release, then requires held-only wire2048 and all accepted-TX,
focus/capture/RX/replay expectations. No lifecycle gate or pre-existing assertion
was weakened; the failed run and detailed assertion output are retained.

The unchanged supplemental Fast fixture initially failed its fresh PowerShell
child's one-second first-marker/readiness test while other verification was
running. Its generic assertion does not retain the underlying caught exception;
slow startup/read delivery is consistent but not proven. After builds and the
suite completed, the unchanged isolated retry passed, including runtime and
cleanup markers. This is not evidence of an unlimited gameplay timeout and no
production launcher deadline was changed. A separate initial version-check
invocation had incorrectly splatted a scalar string; the corrected typed argument
array passed version/help/Null, with the invocation error log retained.

All new verification logs are under
`manual-artifacts/task-records/e16-mp5-manual-fix-20261003`, including
`release-offline-final.txt`, `release-final-LastTest.log`, focused/core/ASan/asset
results, prediction outputs and `test_manual_fast-isolated-retry.txt`.

Corrected executable: `build/bin/Release/hlclient.exe`, 3,637,760 bytes.
SHA-256: `5EF86A039984ABD9D20619184FDA644D1B754ABE4ECE6E25111EE3C918FBFEB3`.
The unchanged launcher resolves that normal Release path; no feature flag,
alternate executable or manual asset copying is required.

Remaining E16 limitations are unchanged: main explosion audio is evidence-pending,
water/spark/scorch visuals are limited/deferred, and this new exact beam-follow
observation has no visual trail implementation. Offline wire/animation/PCM/GL
proof is not a live server-grenade or human listening confirmation. Corrected
build manual=not_run; water_manual=not_run; new live/stock/HLDS/Steam/WFP/managed/
capture sessions=0; staging/commit/push=none.

The same manual command applies, with MP5 primary first, then acquired server
ARgrenades and RMB:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```
