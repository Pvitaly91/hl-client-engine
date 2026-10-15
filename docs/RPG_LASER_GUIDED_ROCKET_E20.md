# E20 — RPG laser-guided rocket

## World-glow occlusion follow-up — 2026-10-14

New `attribution=user_report`: RPG laser dots are visible through walls. Both
the approved server laserdot and Egon endpoint use the neutral glow material.
The ordinary sprite renderer disabled depth testing for that material without
an independent visibility test. World glow now uses the existing world depth
test, while retaining additive blending, no depth writes and the near-camera
scale guard. This supersedes the historical additive/no-Z description below.
No local laser trace, entity ownership, laser position, beam shape or asset is
changed. Project-owned OpenGL A/B tests exercise actual normal RPG/Egon sprite
materialization, visible/occluded/foreground pixels and cleanup/state restore.
Validation and the new executable identity are recorded in the E32 follow-up.
New image `manual=not_run`; no new live session was started by the agent.

Task `HLC-E20-RPG-LASER-GUIDED-ROCKET`, worktree
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, initial HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. Prior dirty source is preserved;
no checkout, reset, cleanup, SDK update or index mutation. The existing source
snapshot mechanism saved and verified 1,231 regular source files in
`manual-artifacts/task-records/e20-rpg-20261005/source`; logs/manifest there are
evidence only, never build inputs. Pre-edit focused Release baseline: 360 passed,
9 explicit opt-in skips, 189,670 assertions, zero failures.

E19 subsequent `manual=passed; attribution=user_report` is appended to its own
record without rewriting historical `not_run`, assigning a historical hash or
promoting water/bubbles/dynamic-hit evidence. E18 manual status is unchanged.
E20 `manual=not_run`; no user listening or live rocket proof is claimed.

## Pre-edit audit and ownership

| Component | Initial status | Production work |
| --- | --- | --- |
| rpg catalogue/current | generic WeaponList/CurWeapon available; RPG rules absent | HL exact ID 8 + `weapon_rpg` + validated v-model |
| rpg event | assigned event decoding available; callback unimplemented | HL assigned `events/rpg.sc`, two independent launch cues |
| rpg viewmodel | generic imported models available; roles unsupported | HL fire/reload/loaded and unloaded idle and exact service roles |
| rpg pmodel | generic bone merge/server weaponmodel available | reused; no invented socket or forced player fire pose |
| rpg rocket entity | generic kinetic ordinary packet Studio mechanism ready | reused server origins/angles/movetype; no local projectile |
| laser spot entity | manifest sprite available; render mode rejected | generic glow mode; server entity, never mouse trace |
| glow sprite entity | live mode 3 unsupported | neutral glow enum and additive/no-Z draw |
| EF_LIGHT | observed bit, no attached live light | generic entity-owned light from bit 64 |
| beamfollow | TE22 strictly decoded but inert | generic committed outbox, bounded position history and textured ribbon |
| active rocket / spot state | vuser2 not projected | exact optional neutral vector; HL interprets y/z |

Dependency direction remains application composition -> host/API + HL module;
HL rules -> API/common mechanisms, never core -> HL. Files:
`games/halflife/rpg_profile.hpp`, `firearm_profile.hpp`,
`local_weapon_presentation.cpp`, `presentation.cpp`, `client_module.cpp`,
`weapon_audio.cpp`, `remote_effects.cpp`, `weapon_visuals.cpp` own game policy.
`runtime_replay_session.cpp` owns exact schema projection and transaction staging;
`GameClientHost` owns a generic 32-event follow outbox, independent of module
callbacks; `EntityFollowPresentation` and `RuntimeReplayLocalAssets` execute
generic entity lifetime/sample/materialization. Renderer receives only neutral
sprite blend/tint/ribbon endpoints and lights; server audio receives source
lifetime identities. No gameplay names/IDs or asset-open locators enter shaders.

## Pinned reference and authority

Read-only official SDK revision `b1b5cf5892918535619b2937bb927e46cb097ba1`:
`dlls/rpg.cpp`, `dlls/client.cpp`, `dlls/weapons.h`, `dlls/ggrenade.cpp`,
`cl_dll/ev_hldm.cpp`, event hooks, `common/const.h`, `network/delta.lst`.
No reference implementation was copied. CRpg exposes `vuser2[1]` spot active
and `[2]` active-rocket count as signed float descriptors; no ABI offset is used.
The receiving snapshot owns optional components and validates finiteness.
This extension does not change existing canonical/hash profiles or wire grammar.

RPG has clip 1/reserve `rockets` (server catalogue ammo index), fire role 3/body 1,
reload role 2/about 2 s, loaded/unloaded idle 0/8 and exact deploy 5/7.
The approved model calls role 3 **`fire`**, not `fire2`; SDK enum and asset labels
are not interchangeable. A retained derived idle from the opposite clip state
is refreshed without overriding an exact service pose or ongoing action.
Fire emits `weapons/rocketfire1.wav` gain .9 and `weapons/glauncher.wav` gain .7,
pitch 1, independent automatic voices through the existing approved loader/mixer.
Local visual punch -5 is presentation only, replaced by authoritative punch.
No RPG flash/brass/bullet trace/decal/tracer/explosion/local rocket is emitted.
Server clip/reserve, flight, guidance, collision, explosions and damage remain
authoritative. A remote event emits the two spatial launch cues, never observer
recoil. Late echoes and replay retain existing causal deduplication.

Reload/holster provisional policy uses ONLY valid optional server state:
blocked when spot on and active count >0; allowed when spot off or count zero.
Absent/invalid fields are not zero and fail closed. Exact server poses can still
arrive. Inventory commands go through existing validated host selection. RMB
uses the existing pre-history physical edge mask, not local FOV or dot creation;
the server decides toggle cooldown and laser lifetime. No FOV20/40 policy added.

## Generic visual/audio lifetime

Server `sprites/laserdot.spr` is an ordinary model-slot sprite with mode 3,
renderfx 14 (NoDissipation), renderamt 255. The glow pass is additive and disables
depth checks/writes, restores state, and keeps ordinary world coordinates and
camera near/far/frustum bounds. A local-compatible near-camera guard caps the
glow quad at 10% of view height, retaining the actual entity/asset scale below
that cap; this is not distance dissipation or a fullscreen icon.
No pulse/local reload hide timer, proximity dot,
forced local-only dot or temporary-sprite expiry. EF_NODRAW hides and clearing it
shows the same server entity; server removal deletes it. Distance/PVS treatment
is a bounded local-compatible subset, not stock-binary glow parity.

EF_LIGHT is interpreted generically at the presented server entity origin,
not from an ignition timer. Local-compatible radius 160/intensity 1/warm RGB;
bit clearing/removal retires it next frame. It shares eight additional neutral
lights without displacing the pre-existing muzzle light. Trail/audio/explosion
are independent components. Player death does not reset ordinary entity effects.

TE_BEAMFOLLOW retains exact payload entity/attachment, sprite, life/width in .1
units, RGB and brightness. RPG reference payload is 40/5/224,224,255/255;
production consumes fields, not those constants. Attachment 0 is supported;
nonzero attachment refs are explicitly rejected rather than guessed.
32 active trails, 64 points each, 512 rendered segments, 64 recent event identities
plus monotonic provenance floor; committed entity directory <=4,096. Samples
are already-rendered/interpolated origins at >=20 ms with >=.1-unit movement;
no physics, velocity integration or extrapolated guidance. 512-unit jumps and
NOINTERP break the path. Each segment fades within the payload lifetime; after
removal history decays without following a new slot occupant. Texture is the
approved manifest sprite's first frame, reused from the immutable GPU package;
no per-frame resource opens or re-import. This ribbon/fade/tessellation is
local-compatible, not an opaque stock engine beam implementation.
At most 64 recent positions are retained per trail, so sustained high-frequency
flight can truncate older history before the payload's maximum decay interval.
Source lifetime proof is removal/reappearance/model change in the committed
stream, not an unavailable stock edict serial; unobserved same-model slot reuse
cannot be independently proven. Head-on ribbon projections can be degenerate.
The normal world and standalone entity passes both render the same neutral
ribbon. Pair interpolation now agrees with ordinary history about MOVETYPE
changes as discontinuities; continuous BOUNCE/FLY turns still use shortest angles.

`svc_sound` alone starts `weapons/rocket1.wav` motor/loop. No local motor launch
rule was added. Generic positive source lifetime identities stop loops when an
entity is removed/reused, including pending loop loads; explicit stops remain
supported. One-shots can finish independently. Legacy sources with no lifetime
proof retain their existing fallback. Existing TE_EXPLOSION/SMOKE and sound
flags are reused; no second explosion or damage is inferred.

Opt-in `--net-trace` diagnostics have a 16-line event-change budget, not per-frame
spam: follow accepted/duplicate/invalid/capacity/detached/discontinuity,
resource absent/unsupported/frame-rejected, segment/light submitted. Submitted
is not a GPU-pixel or audible-output verdict; the independent tests provide
framebuffer and isolated PCM evidence.

## Actual approved read-only controls

`v_rpg`: 32 bones, 3 bodyparts, 10 sequences, 24 textures, 615 triangles.
Exact `(label, fps, frames)` roles:
`idle/15/91`, `fidget/15/91`, `reload/30/61`, `fire/30/25`,
`holster1/30/16`, `draw1/30/16`, `holster2/30/16`, `draw2/30/16`,
`idle2/15/91`, `fidget2/15/91`. Validated selectable bodies 0/1.
`p_rpg`: 12 bones, one sequence, no attachments; ordinary named bone merge is
reused, never a fabricated muzzle socket. `rpgrocket`: one bone/sequence, no
attachments; `w_rpg`: one bone/two sequences; `w_rpgammo`: two bones/two sequences.
Laserdot and smoke resolve through manifest slots and pass actual GPU A/B.
All three WAVs are mono 22,050 Hz: rocketfire1 9,406 samples, glauncher 28,093,
rocket1 54,997 with valid imported loop markers. Each independently produces
finite nonzero production-mixer PCM; this is not human listening confirmation.

## Verification status

Primary result: `rpg_laser_guided_rocket_integrated`, offline verified with the
explicit local-compatible and unsupported-attachment/water limits above.
All final runs below follow the last production edit; no earlier E19 result or
failed preliminary run is substituted for final acceptance. Evidence files are
under `manual-artifacts/task-records/e20-rpg-20261005/` and are not source inputs.

| Check | Final result / evidence |
| --- | --- |
| VS2022/Win32 Release, all targets | passed, `build-release-acceptance.txt` |
| Affected Debug client/tests | passed, `build-debug-final.txt` |
| Audited full Release offline suite | 2,889 passed, 46 explicit skips / 2,935 registered, zero failures; `full-release-verified.txt` |
| Focused E13–E20/B1/API/interpolation/prediction/lifecycle/sprite regressions | Debug AND Release: 434 passed + 10 opt-in skips / 444 cases, 287,823 assertions each; `regressions-debug.txt`, `regressions-release.txt` |
| E20 with approved actual read-only assets | 18 cases / 596 assertions, zero failures/skips; `e20-actual-final.txt` |
| Core-only all targets, HL option OFF | passed, `build-core-only.txt`; 41/41 CTest passed, `core-only-tests.txt` |
| Direct core/API without HL | 229 cases / 88,219 assertions passed, `core-only-api-final.txt` |
| Actual dependency/source/link graph | architecture guard passed; core-only generated projects/graph have zero concrete HL target, source or link references, `core-only-config-graph.txt` and `core-only-dependencies.dot` |
| Scoped ASan | `asan_build=passed` for core/API target; `asan_core_api=passed`, 59 cases / 3,805 assertions, `build-asan-core-api.txt`, `asan-core-api-final.txt` |
| Full ASan application | `asan_application_runtime=not_verified`; not rerun in E20; historical startup exit 1/no output is not repaired or promoted |
| Null/process-level / fake-peer / native–PowerShell summaries / no-stock launcher | passed in the audited Release script, no stock processes or capture files |
| Launcher read-only checks | reference + RemoteAudioPeer + NoTimeLimit and off + damage-respawn-check passed, `launcher-reference.txt`, `launcher-off.txt` |
| Source preservation | 1,231 snapshot hashes valid, zero original files missing; 34 existing files changed, prior dirty work retained; index empty, initial branch/HEAD/SDK unchanged |
| Whitespace | `git diff --check` passed |

The full suite's 46 skips are explicit installed-asset/device/WFP/reparse/native
window or other capability opt-ins, not failed tests counted as successes.
Mandatory owned E20 framebuffer and mixer tests passed without installed assets
or an audio device. The separate exact-root E20 control also passed: actual
laserdot/smoke pixels, trail expiry back to baseline, inspected model roles and
isolated nonzero PCM for each launch/motor sample. Human listening, real-device
output and actual server guidance remain untested here.

The first full pass attempt correctly found an old E16 assertion that TE22 was
inert. Its replacement verifies the newly implemented exact one-event payload,
the generic host outbox, malformed-suffix atomicity and duplicate suppression;
it does not relax decoding or manufacture a game callback. A separate fake-HLDS
test inventories a global exact executable identity and raced another valid
parallel fake-client test. It passed unchanged in isolation and now runs serially
after Catch discovery (`tests/cmake/process_identity_test_serialization.cmake`);
the no-client assertion is preserved. The complete final suite then passed.

Owned fixtures need no game assets/device. Reuse the existing configurations;
do not clean, update the SDK or use the non-TestOnly source script:

```powershell
cmake --build build --config Debug --target hlclient hlclient_tests --parallel 2
cmake --build build --config Release --parallel 2
pwsh -NoProfile -File scripts/build_source.ps1 -TestOnly -Configuration Release -Parallel 4
cmake -S . -B build-core-g1 -G "Visual Studio 17 2022" -A Win32 -DHLCLIENT_BUILD_GAME_HALFLIFE=OFF
cmake --build build-core-g1 --config Release --parallel 2
ctest --test-dir build-core-g1 -C Release --output-on-failure --parallel 2
cmake --build build-asan --config Debug --target hlclient_core_api_tests --parallel 2
```

Scoped ASan uses the existing x86 ASan runtime on PATH and the bounded filter
`[game-api],[audio-api],[camera-policy],[particles],[pose],[entity-interpolation],[e20]`.
This does not establish ASan coverage of the application/render/audio device.

Prepared normal executable:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`, 3,723,776 bytes.
SHA-256: `08D73C96C44C270B74F03D64A399A662ACE13992C5D731F3A6D823652D1C3D46`.
The unchanged launcher resolves exactly that path without hidden flags or asset
copies. Its preserved SHA-256 is
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
CheckOnly verifies files/contracts, not a live installation or audible gameplay.

Unchanged manual handoff (user runs, not executed by the agent):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Check loaded/last/empty fire, two launch cues, ordinary server rocket then motor/
light/curved trail/explosion, laser spot and RMB on/off without zoom, reload and
holster gating while a guided rocket is active, remote pmodel and effects, then
death/respawn and E13–E19 regressions. Water/bubble visualizations remain deferred.

```text
rpg_launch_manual=not_run
rpg_projectile_manual=not_run
rpg_designator_manual=not_run
rpg_guidance_manual=not_run
rpg_beamfollow_manual=not_run
rpg_light_manual=not_run
rpg_motor_audio_manual=not_run
rpg_reload_restriction_manual=not_run
rpg_holster_restriction_manual=not_run
rpg_remote_manual=not_run
rpg_kill_respawn_manual=not_run
new_live_sessions=0
stock_client/HLDS/Steam/WFP/capture=not_run
staging/commit/push=none
```

## Subsequent laser-motion correction (2026-10-05)

`attribution=user_report; laser_designator_motion=jerky`: user reported
“лазер ривками перміщається”. The report supplies no executable hash or exact
transaction ID; no historical SHA is assigned to this observation. Other E20
manual fields are not promoted to passed. Corrected-build `manual=not_run`.

Root cause: `RuntimeReplayLocalAssets::present_entities` opted only ready
kinetic Studio entities into `RuntimeOrdinarySnapshotPresentation`. The server
laser dot is a glow sprite with `MOVETYPE_NONE`, even though the server updates
its origin. It was therefore rendered at each newest snapshot position with
no between-update pose sampling. The new normal-path regression failed on
the pre-fix code at all four independent intermediate checks: x was 8 instead
of 2/4/6, and a later turn's y was 8 instead of 4 (`reproduction.txt`).

The same generic selection now also admits ready sprites with supported glow
mode 3 and renderfx 0/14, without relying on any HL filename, entity number or
weapon ID. It reuses the existing sixteen-observation render-only buffer with
one observed cadence of delay capped at 100 ms, no extrapolation and the
existing 512-unit continuity guard. No local trace, fake dot, physics, RX/game
state mutation or additional event/audio delivery is introduced. Static
non-glow sprites, brush carry, player clock/policy and kinetic Studio selection
remain unchanged. Server NODRAW and removal are applied immediately; source
absence/reuse, NOINTERP, model/sequence/movetype discontinuities and invalid
clock/gap boundaries retain their existing safety rules.

Scoped pre-change source/hash snapshot and correction evidence:
`manual-artifacts/task-records/e20-laser-jitter-20261005/`. Source changes are
`src/app/runtime_replay_local_assets.cpp`, an API ownership comment and
`tests/test_rpg_entities.cpp`, plus this additive record. The original acceptance
table and hash above remain historical; the current corrected handoff is below.

| Correction check | Result |
| --- | --- |
| VS2022/Win32 Release all targets / affected Debug client and tests | passed (`build-release.txt`, `build-debug.txt`) |
| New normal-path regression | pre-fix four expected failures; corrected Debug 87/87 assertions passed, including non-glow exclusion and immediate lifecycle/discontinuity handling |
| Focused E20 + entity interpolation | Debug AND Release: 33 passed + 1 explicit installed-resource opt-in skip / 34 cases, 1,871 assertions each |
| Full Release offline suite after the last production edit | 2,890 passed + 46 explicit capability/opt-in skips / 2,936 registered; zero failures; entire audited script exit 0 (`full-release-offline.txt`) |
| Actual approved read-only E20 assets / GPU / isolated PCM | 19 cases / 683 assertions, no failures or skips (`e20-actual.txt`) |
| Core-only HL OFF, all targets / CTest | passed, 41/41; architecture guard passed, zero concrete HL source/link references in generated projects |
| Null, fake peers, native/PowerShell summaries and no-stock harness | passed in the final audited script, no stock/live launches |
| Unchanged launcher | exact current Release path; reference/RemoteAudioPeer/NoTimeLimit CheckOnly passed (`launcher-check.txt`) |
| Preservation / whitespace | four scoped snapshot hashes verified, initial branch/HEAD/SDK and empty index retained; `git diff --check` passed |

ASan application runtime remains `not_verified`. ASan was not rebuilt or rerun
for this application-selector-only correction; the earlier scoped core/API
result is historical, not new application coverage. No renderer shader, mixer,
movement kernel, protocol parser or HL weapon rule was changed here.

Current normal Release: 3,724,800 bytes,
SHA-256 `CC563835FC39E6A02B7E7E2F34700A47A0C174B44D82FF7F96DF774399E5AD58`.
The executable is already at the same launcher path; launcher hash remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
`laser_motion_fix=integrated_offline_verified; manual=not_run` until a new user
test of this build. New live/stock client/HLDS/Steam/WFP/capture sessions=0;
staging/commit/push=none.

## Subsequent user acceptance (E21 request, 2026-10-05)

`manual=passed; attribution=user_report`: user explicitly reported “Перевірено”.
Scope is the E20 manual RPG scenario: launch, server rocket, laser designator,
guidance, rocket trail/light/audio, explosion and normal client stability.
Optional water behavior is not promoted to passed. The earlier handoff and
laser-correction `manual=not_run` entries remain historical. This report
supplies no executable hash/transaction ID; none is inferred. E19's separate
user acceptance and E18's factual historical `manual=not_run` are unchanged.

## RPG flight smoke and MP5/Gauss visual tuning — 2026-10-06

`attribution=user_report; rpg_flight_appearance=line_only`: user requested an
original-HL-like flight effect alongside MP5 lifetime +30% and observer Gauss
diameter +70%. Historical acceptance remains above; it did not establish
stock-exact trail appearance. New-build manual remains `not_run`.

The old path stretched frame zero of the approved smoke sprite across each
0.5-unit-wide TE_BEAMFOLLOW ribbon. The pinned official SDK's `dlls/rpg.cpp`
ignites with EF_LIGHT and TE_BEAMFOLLOW using `sprites/smoke.spr`; it exposes no
stock engine particle implementation. The actual approved sprite is additive,
view-parallel oriented, five centered 32x64 frames. Assets were read only.

`HalfLifeClientModule::entity_trail_profile` selects an owning neutral profile
only for the existing manifest pair `models/rpgrocket.mdl` / `sprites/smoke.spr`.
The normal application's existing event drain passes its GameClientHost to
`RuntimeReplayLocalAssets::consume_follow`, which resolves those virtual names
from committed entities and the approved manifest. No server slot is guessed,
no new resource authorization or file search is added. Other follow effects
keep the existing ribbon path; alternate/core-only modules default to no profile.

The existing `EntityFollowPresentation` samples already-presented server poses
at 8-unit intervals. Each smoke element expands from 4 to 18 units, rises at
6 units/s and fades over min(wire life, 1.2 s), with bounded deterministic
position jitter of +/-0.6 units. All five original animation frames are used.
These tessellation/drift/opacity constants are local-compatible presentation,
not stock constants. The approved additive sprite pass renders camera-facing
puffs without stretched ribbon geometry, including axial views. An attached
3-unit warm glow follows the server EF_LIGHT bit; the existing light and motor
audio mechanisms remain independent. No rocket flight, aim, collision, damage,
laser guidance or explosion is simulated by this effect.

Bounds: 32 trails, 128 smoke samples per trail (legacy ribbons retain64),
512 output elements, unchanged bounded event/lifetime dedup. Repeated/stationary
updates do not emit new smoke. A >512-unit jump, NOINTERP or moving sample gap
>250 ms clears continuity; visibility absence does not draw a guessed bridge.
Removal stops emission and head glow, while published smoke expires independently
of local death/weapon selection. Slot reuse cannot attach the previous trail;
map/session reset clears all. Saturation discards oldest per-trail samples or
limits output; it never grows memory. Renderer/core retain no RPG names/rules.

Pre-edit snapshot: `manual-artifacts/task-records/weapon-flight-tuning-20261006`,
1238 hash-verified ordinary source files. Baseline Release E15/E20/E21:106 passed,
3 optional asset skips,124856 assertions. Ordinary source and existing task
documents contain the implementation; task artifacts contain only evidence.
Final verification and executable identity are appended after validation.

Final verification for the combined MP5/Gauss/RPG request (VS2022 / Win32):

- Normal Release all targets and affected Debug application/tests built.
- Debug focused E15/E20/E21/remote-effects/API: 183 passed,4 explicit optional
  asset skips,143730 assertions. Release with the approved read-only root:
  187 passed,348110 assertions,zero skips/failures. Original smoke frames,
  side/axial GPU visibility, expiry/state restoration and independent audio
  controls passed; this is offline evidence, not a user listening/visual report.
- The five new smoke tests cover exact profile selection, spatial emission,
  expansion/fade, no repeated stationary emission, server-bit head glow,
  discontinuities, removal/reuse/reset, invalid profiles and pool/output bounds.
  The separate actual-resource Debug control passed6 cases/214 assertions;
  it also verifies that the final original smoke animation frame is selected.
- Core-only `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`: all targets built and41/41 tests
  passed, including the architecture guard and alternate-module/API coverage.
- Complete final Release CTest:2977 registered,2930 passed,47 explicit
  capability/opt-in skips,zero failures,284.08 seconds. Required owned fixtures
  do not depend on game assets or an audio device. Skips are not successful
  device/live validations. Evidence: `full-release-offline.txt` in the task record.
- The complete `build_source.ps1 -TestOnly -Configuration Release -Parallel 2`
  script also exited0 after Null startup, transport self-test, native/PowerShell
  summary roundtrips and the no-stock manual-launch fixture (reference/off,
  duration/no-limit, external-map and exact-report failure contracts).
- Unchanged reference/RemoteAudioPeer/NoTimeLimit launcher `-CheckOnly` passed,
  process-launches=0. Launcher SHA remains
  `AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
- Snapshot audit:1217 unchanged,21 intended ordinary source/test/doc edits,
  zero missing/new ordinary paths. Initial branch/HEAD/pinned SDK and empty
  index preserved; `git diff --check` passed. No ASan rerun or new application
  ASan runtime claim; its historical unverified status remains.

Prepared normal Release: `build/bin/Release/hlclient.exe`,3776512 bytes,
SHA-256 `B11EA8C4BBE8C04900B1395AB52ADC487AE34ACAC4D52F791DDB0D8817C8E193`.
The standard launcher resolves this executable. No new flags or asset copies.
New-build `manual=not_run`; new game/stock-client/HLDS/Steam/WFP/capture
launches=0; staging/commit/push=none.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

## Sky gap, RPG launch mix and nozzle exhaust correction — 2026-10-06

`attribution=user_report`: dark polygon amid the blue sky in the supplied
screenshot; RPG launch sounds grenade-like; request for denser smoke and a
sprite at the rocket nozzle. No run/binary identity was supplied, so the prior
SHA is not assigned to this report. Prior acceptance entries remain historical.
Pre-edit snapshot: `manual-artifacts/task-records/sky-rpg-exhaust-20261006`,
1238 hash-verified ordinary source files; existing index/dirty work preserved.
Baseline existing Release E20:23 passed,1 opt-in asset skip,803 assertions.

### Sky evidence and correction

The screenshot's dark RGB(9,14,36) equals the normal connected scene's clear
color; the surrounding RGB(181,222,255) equals the approved Crossfire SKY WAD
texture, whose decoded pixels are uniform. This is an unpainted framebuffer
region, not a corrupt blue texel. The actual BSP also has sky marksurfaces
extending beyond their referring leaf bounds (faces38,1088). Leaf-only frustum
gating is therefore unsafe: PVS now selects candidates and each surface's own
bounds determine frustum rejection. The exact screenshot camera was not saved,
so that shortcut is not claimed as the unique trigger for that particular gap.

`WorldRenderPackageBuilder` additionally publishes a neutral immutable flat-sky
RGB only when a special, unlit, opaque, non-placeholder sky texture has uniform
matching pixels in every mip. `OpenGlRenderer` uses that verified backdrop for
finite sky gaps/far clipping; finite geometry/depth/PVS and later passes remain.
Ordinary packages retain their requested clear color. No installed texture is
modified and no asset/path access occurs during drawing. This fixes the current
flat sky profile; nonuniform sky/cubemap rendering is not newly implemented.

### Launch audio

Pinned official SDK `ev_hldm.cpp:1359–1360` layers `rocketfire1.wav` at .9 with
`glauncher.wav` at .7. The previous implementation reproduced both. To satisfy
the requested rocket-only sound, local and observer RPG paths now prepare/emit
only original `weapons/rocketfire1.wav`, gain .9, pitch 1. This is a deliberately
adjusted mix of the original sample, not a claim of stock-exact mixing.
MP5 secondary samples, server `rocket1.wav` motor, explosion audio, spatial
source positions and voice/action dedup are unchanged.

### Denser smoke and real nozzle sprite

The Half-Life profile now emits smoke every 4 units (formerly 8), expands 6→22
units (formerly 4→18), starts at opacity .85 (formerly .7), keeps the existing
1.2 s maximum lifetime, 6 units/s rise and deterministic jitter. Per-trail storage
is 256 points (formerly 128) so denser spacing retains the same 1024-unit spatial
budget; legacy ribbons still 64, live trails 32, smoke draw output 512. Separately
bounded 32 head sprites cannot be starved by smoke-output saturation.

The old procedural glow at the entity origin is replaced by original approved
`sprites/hotglow.spr` through the existing manifest sprite materializer. The
module owns the exact virtual name, 10-unit diameter, white tint and local-X −1
offset. The application resolves its existing manifest slot once on follow
consumption; no invented precache slot, file search, new loader or permissions.
Missing/unsupported glow skips only the head, retaining smoke and independent
light/audio. Original `smoke.spr` remains the same approved five-frame asset.

Pinned SDK calls EF_LIGHT a rocket flare glow sprite, while the engine's exact
internal sprite selection is not exposed. `hotglow.spr` is already precached by
the official Gauss profile. Its actual 64×64 additive, centered view-parallel
image has orange center RGB(255,140,0) and black corners. The actual one-bone
rocket has no attachment: after its idle bone transform, nose is local X +14.18
and nozzle near X 0. A −1 offset puts the glow behind the nozzle, not in front.
The host uses the same presented Studio transform including source-pitch sign,
not camera or estimated velocity. Size/density/asset choice are local-compatible
original-HL-style presentation, not proprietary-engine parity.

Only server EF_LIGHT+current entity lifetime authorizes the attached sprite;
NODRAW, removal/reuse, incomplete/nonfinite angles and reset suppress it. Smoke
already published continues its bounded decay independently. No network, ammo,
rocket physics, guidance, prediction, damage or weapon rules changed.
New-build manual remains `not_run`; final build/test identity follows below.

Validation for this correction (VS2022/Win32):

- Affected Debug application/tests and all normal Release targets built.
  One Debug test-object/linker COMDAT failure was recovered by regenerating
  that single object and nonincrementally linking the tests; no clean build,
  gameplay change or weakened assertion was used.
- Final Debug focused E20/E16/sky/visibility/API: 142 passed, 5 explicit optional
  asset skips, 14396 assertions. Final Release with exact approved read-only
  assets: 147 passed, 246433 assertions, no failures/skips. This includes isolated
  launch PCM and real smoke/nozzle GPU pixels, not a listening confirmation.
- Original Crossfire package validates SKY RGB(181,222,255); real OpenGL pixels
  retain that color outside finite geometry. Owned GL controls also retain
  foreground/depth/teardown. Original hotglow produces nonzero pixels alone
  after smoke expiry, from side and/or axial controls. Missing glow retains
  smoke; head pose, server visibility, reuse/reset and bounds are covered.
- Core-only all targets built with `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`; 41/41
  CTest checks passed, including core/API and architecture guard.
- Unchanged reference/RemoteAudioPeer/NoTimeLimit launcher `-CheckOnly` passed
  with process-launches=0. `git diff --check` passed and index remains empty.
- The first parallel full-suite attempt was interrupted at 2294/2982 amid
  system allocation failures: Windows 0xC000012D (`STATUS_COMMITMENT_LIMIT`,
  locally verified SDK definition), BZip −3 (`BZ_MEM_ERROR`), processes not
  started and a loopback query timeout. It is NOT recorded as a passing suite.
  Related orchestration/fixture sources match the pre-edit snapshot. The final
  serial full-suite rerun, without production edits or altered assertions,
  completed: 2935 passed, 47 explicit capability/opt-in skips, 0 failures out
  of 2982 registered tests (619.21 s). The audited Release verification script
  then completed with exit 0: Null startup, loopback move transport, native/PS
  summary roundtrips, no-stock manual runner fixtures and source-only launcher
  CheckOnly all passed. Log: `manual-artifacts/task-records/sky-rpg-exhaust-20261006/full-release-offline-serial.log`.
- ASan application runtime remains historically `not_verified`; not rerun here.
  No new live game/stock-client/HLDS/Steam/WFP/capture launches; no commit/push.

Prepared normal Release: `build/bin/Release/hlclient.exe`, 3777536 bytes,
SHA-256 `2A46468C60E0161250E8D43C953E1DEBDE38AB0364512AF03D77C709B1075714`.
Launcher SHA remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
No asset copying or new flags are needed. Snapshot integrity: 1238 valid saved
files, 1219 unchanged current files, 19 scoped source/test/doc edits, none missing.

## Startup stack-overflow correction — 2026-10-06

`attribution=user_report`: both clients fail to enter the map after the sky/RPG
correction. Exact run `1c9319d1ffb04b4dbc8ba3edeebbf5da`: Crossfire, reference
prediction, RemoteAudioPeer, unlimited duration; server PID 8500, client PID
37860, peer PID 3396. Both clients exited with 3221225725 / `0xC00000FD`
(`STATUS_STACK_OVERFLOW`, verified in the local Windows SDK). The report records
successful connection acceptance, resource/spawn/signon exchange and live
payloads before failure. The aggregate `connection_status=rejected` does not
override the separately observed successful accept. Cleanup is `exact`,
managed-file restoration `scoped_exact`, cleanup errors 0; full-tree restoration
remains unverified under Fast mode. Historical manual records remain intact.

The denser trail pool exposed a Win32 stack regression. The main live loop
already retains a large frame; `project_entities` copied all follow history
onto its stack, and `EntityFollowPresentation::reset` materialized another full
pool through aggregate assignment. Measured Release object-code frame requests:

| Function | Before | After |
| --- | ---: | ---: |
| `run_live_visual_control` | 715956 bytes | unchanged |
| `RuntimeReplayLocalAssets::project_entities` | 281072 bytes | 42848 bytes |
| `EntityFollowPresentation::reset` | 206084 bytes | 6444 bytes |

The nested pre-fix requests alone exceed the executable's verified 1048576-byte
stack reservation. Transactional follow staging now uses an owned heap copy;
reset clears one trail at a time. All allocation still precedes publication and
exceptions use the existing failure result. Trail bounds, density, nozzle,
generation/lifetime semantics and all sky/audio changes remain unchanged.
The executable stack reservation was not enlarged.

Added a project-owned offline regression exercising the production ready asset
projection/reset 20 times on an explicitly reserved 256 KiB Win32 worker stack.
Before the fix it failed with `SIGSEGV - Stack overflow`; after the fix it passes
in Release and Debug. The small worker budget reproduces the limited space below
the normal live caller without Steam, a stock process, game assets or a live
connection. The fixture's asset owner and world state remain outside the worker
stack, with lifetime retained through thread completion.

Final scoped verification after the production fix:

- All normal Release targets and affected Debug application/tests built.
- Release E20/runtime-replay/sky/visibility: 115 passed, 36148 assertions,
  no failures/skips, including approved read-only original RPG audio and actual
  OpenGL sky/smoke/nozzle controls. Debug: 114 passed, 1 explicit installed-asset
  opt-in skip, 8438 assertions, no failures.
- Core-only all Release targets rebuilt with the Half-Life module disabled;
  41/41 checks passed, including architecture guard.
- Unchanged reference/RemoteAudioPeer/NoTimeLimit launcher CheckOnly passed
  with process launches 0. Normal Release Null startup and `basic-mixed` offline
  application replay passed (4 input records, 4 applied, 0 failures).
- `git diff --check` passed, index empty. Four pre-edit source snapshot hashes
  verified in `manual-artifacts/task-records/sky-rpg-stack-startup-20261006`.
  The earlier full-suite count belongs to the earlier binary; this correction
  uses the scoped checks above. ASan runtime remains historically unverified.

New normal Release SHA-256:
`201D5DDD2CEA3DF8F4F038EC7661F15A51719B1D514F96DA86C2DC795C36D43C`.
Launcher SHA remains unchanged. New-build manual validation `not_run`;
new live game/HLDS/Steam/WFP/capture sessions 0; staging/commit/push none.

## Slow Steam startup and retained authentication error — 2026-10-06

`attribution=user_report`: the same reference/RemoteAudioPeer/NoTimeLimit command
still fails. The attached report contains two distinct runs:
`8fc6362c5c2742c8841197707fdf8a26` (server 8504, client 28980, peer 12172) and
`0b26753fd1034c9abeebb7dbd63487f2` (server 33024, client 21864, peer 30836).
All four clients return 2, not the previous stack exception. Their last confirmed
stage is successful Steam API initialization; no fresh material, connect send,
serverinfo, asset job or world publication is observed. Each retains only two
presentations, with longest updates about 5.4–6.8 seconds. Both reports confirm
exact owned cleanup, scoped-exact restoration and zero cleanup errors.

The prior provider started a single 5-second deadline before its synchronous
Steam initialization. A successful initialization consuming over 5 seconds then
returned pending, and the next poll expired before obtaining material. The
retained old logs redact the typed authentication failure, so they do not prove
which provider error occurred in each run; their timing/stage pattern matches
this independently verified defect. The pre-edit installed Release SHA was
`201D5DDD2CEA3DF8F4F038EC7661F15A51719B1D514F96DA86C2DC795C36D43C`;
no separately retained client SHA is assigned to either historical run.

`SteamAuthenticationProvider` now gives initialization/readiness a separate
30-second acceptance budget and starts the unchanged 5-second material budget
only after successful readiness. Both phases check expiry before work and after
returning synchronous work; an expired positive material result releases its
session exactly once. Reused APIs get new operation clocks. Existing app-ID,
logged-on, challenge/profile, size, cancellation and lifetime checks remain.
Synchronous Steam calls cannot be preempted internally: expiry is checked on
return and the existing managed 60-second startup bound remains external.

The application previously evaluated only runtime errors, masking an earlier
handshake authentication failure as `application_incomplete`. It now carries the
actual typed handshake code through `evaluate_live_visual_session`, preserves
runtime-first error priority and keeps failure exit 2. The outcome begins with
bounded `primary_error=authentication_*`, plus authentication domain/stage when
appropriate, ahead of long audio counters. No ticket, credential, arbitrary
context or path enters these fields. The native parser/excerpt retains these
existing summary fields; neither report schema nor manual parameters changed.

Production changes are in the Steam provider header/source, live visual outcome
header/source, normal application composition and native summary validator.
Tests add fake-clock initialization/material boundaries (including a 6-second
cold initialization followed by success), late callback/ticket cleanup, reuse,
cancellation and typed early-failure precedence. Independent source review found
no blocking defect. Nine pre-edit source snapshots are hash-verified under
`manual-artifacts/task-records/steam-startup-phase-timeout-20261006`.

After the final production edit:

- All normal Release targets and affected Debug targets built.
- Scoped Release auth/startup/live-visual/E20/replay/sky/visibility: 163 passed,
  37622 assertions, no failures/skips; approved original asset/GPU controls
  included. Debug: 162 passed, 1 explicit original-asset opt-in skip,
  9912 assertions, no failures. The prior stack-budget regression still passes.
- Native log-observation validators passed in Release and Debug. PowerShell 7
  and Windows PowerShell 5.1 functional publication roundtrips: 2/2 passed.
- Core-only all Release targets rebuilt with Half-Life disabled; 41/41 checks
  passed including architecture guard. Launcher CheckOnly, normal Release Null
  startup and built-in basic-mixed replay passed; launcher SHA unchanged.
- `git diff --check` passed; index empty. Earlier full-suite counts remain tied
  to their earlier builds. ASan runtime remains historically unverified.

Prepared normal Release `build/bin/Release/hlclient.exe`, 3781632 bytes,
SHA-256 `36BC5D4E1D11BCA03F5DA99C5A4C520D0BD3168BEE7FCAB97A7DDB3AFAA977A6`.
New-build manual validation is `not_run`; new live/stock/HLDS/Steam/WFP/capture
sessions 0; staging/commit/push none.

## Repeated/automatic RPG reload animation — 2026-10-06

`attribution=user_report`: the RPG reload animation does not play. No new
historical executable identity or detailed input/flight scenario is inferred.
Existing manual records remain intact. Before editing, five current source/test/
record files were copied and hash-verified under
`manual-artifacts/task-records/rpg-reload-observation-20261006`.

The controller could start a provisional reload from a new R/empty-attack edge
and could present a changed `weaponanim`. It did not present a new authoritative
RPG `m_fInReload` occurrence independently. Official pinned `CRpg::PrimaryAttack`
uses the firing event rather than changing the server's retained `weaponanim`;
`DefaultReload` can therefore set role 2 again without a changed numeric value.
Stock auto-reload and a held-R retry after the rocket/laser restriction also
need not supply another local press edge. The before-fix regression reproduces
a subsequent fresh reload with unchanged role 2: snapshot/viewmodel remains
unloaded idle 8 instead of reload 2 (10 failed assertions in the positive case;
the two existing-behavior/guard controls pass).

`LocalWeaponPresentationController::observe_rpg_reload` now interprets a fresh
current-client transition into `in_reload=true`, with current empty RPG clip and
valid role/body, as an authoritative reload pose. Repeated true/retained snapshots
do not restart it. A provisional reload keeps its command identity, start time,
restart token and single confirmation. A first matching exact service confirmation
upgrades the observed pose's source without replaying its timeline; other exact
service poses retain precedence. Death/model/session transitions keep their
existing reset semantics. No ammo is filled locally, no command is synthesized,
and no new audio/effect is generated. Missing/invalid auxiliary rocket state
still fails closed for provisional reload/holster; an observed server reload
does not need that provisional permission. Unseen transitions are not fabricated.

All rules remain in the Half-Life controller and its private header; transport,
wire grammar, prediction, scene/materializer and renderer mechanisms are unchanged.
Normal GameClientHost sample -> viewmodel intent -> approved materializer uses
the pose. The current original approved v_rpg role 2 has 61 frames at 30 FPS;
an isolated production OpenGL control renders the actual model at frame 0 and
frame 30 with stable camera/body, verifies non-clear pixels and differing pixels.
No original asset bytes were copied or changed.

Final validation after the production edit:

- Focused reload plus exact-root actual asset/GPU control: 4 cases,
  27780 assertions, all passed. It includes two reload occurrences with the same
  clientdata role, repeated records, frame/intent progress, unchanged canonical
  clip, late same service confirmation, exact different service precedence,
  provisional confirmation and death, plus missing/retained/inconsistent controls.
- Scoped Release RPG/weapon presentation/audio/game-module/startup/stack:
  189 passed, 66579 assertions, no failures/skips. Debug: 187 passed,
  2 explicit original-asset opt-in skips, 38799 assertions, no failures.
- Normal Release and affected Debug targets built. Core-only Release all targets
  built with Half-Life disabled; 41/41 tests including architecture guard passed.
  Launcher reference/RemoteAudioPeer/NoTimeLimit CheckOnly and Null startup passed.
- `git diff --check` passed; index empty; five source snapshot hashes verified.
  ASan runtime remains historically unverified. New live/HLDS/Steam/WFP/capture
  launches 0; staging/commit/push none. New-build manual validation `not_run`.

Prepared normal Release `build/bin/Release/hlclient.exe`, 3781632 bytes,
SHA-256 `06FF573972F2621484E3450066F5CBA0E3FEC9411388A36F4C07F5E2F43ADE02`.
Launcher SHA is unchanged. For the manual check, repeat two rocket-fire/flight-
completion/reload cycles, including normal server auto-reload and R. Existing
laser-plus-active-rocket restrictions remain server-owned.
