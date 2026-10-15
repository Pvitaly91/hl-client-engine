# E17 — Half-Life Shotgun single, double and shell-by-shell reload

Task: `HLC-E17-SHOTGUN-SINGLE-DOUBLE-RELOAD`. Authoritative checkout:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, inspected starting HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. Existing E11–E16 work is newer
than the published checkpoint and is preserved. The pre-edit source snapshot
and execution evidence are retained in
`manual-artifacts/task-records/e17-shotgun-20261004`; required implementation
and tests remain ordinary project files, not generated task artifacts.

Current E17 result: **`shotgun_single_double_reload_integrated`**;
offline verification completed, `manual=not_run`. Normal application
integration is checked with owned fixtures and separate approved read-only
asset controls, not inferred from request counters or an earlier E16 suite.
Final acceptance and the Release digest are recorded below.

## New E16 manual evidence

The E17 request explicitly reports a successful preceding E16 manual scenario.
Record `E16 manual=passed; attribution=user_report`, scoped to MP5/M203
secondary launch, server projectile/explosion presentation, clients remaining
operational and preceding multiplayer presentation remaining usable. This is
new user evidence, not a rewrite of the historical E16 `manual=not_run` logs.
Underwater, teamplay, exact RNG parity and unexercised stress cases are not
upgraded to passed. E17 single/double, shells, reload interruption, remote
Shotgun and Shotgun kill/respawn manual observations are initially `not_run`.

## Pre-edit dependency audit

| Boundary | State in the saved pre-E17 source | E17 scope |
| --- | --- | --- |
| `shotgun_catalogue_current` | Generic server WeaponList, ownership, inventory group/wheel selection and server clip/reserve HUD already work; no Shotgun-specific list codec is needed | Verify existing selection and cross-check catalogue ID/command with selected model semantics |
| `shotgun_viewmodel_current` | Generic approved Studio importer, server-selected resource and camera-local viewmodel pass exist; no validated Shotgun action roles | Add the Half-Life model role/profile and actual controller behavior |
| `shotgun_pmodel_current` | E11 generic server weapon-model slot, Studio bone merge and remote-player pose already exist; E12/E15 socket selection only covers handgun/MP5 | Reuse merge and add a validated Shotgun socket profile, not a renderer |
| `shotgun_event_binding_current` | Exact manifest-derived Glock/crowbar/MP5/M203 names are supported; Shotgun event names have no effect binding | Add separate single/double semantics; never session-assigned numeric IDs |
| `shotgun_reload_state_current` | Neutral `in_reload`, `next_reload`, attack timers and idle fields exist; `m_fInSpecialReload` and `m_flPumpTime` are not projected | Add schema-checked optional numeric observations and staged Half-Life presentation |
| `shotgun_pellet_support_current` | One-shot static BSP impact, approved decal/material/audio, particles and MP5 tracer mechanisms exist | Add bounded distinct deterministic pellet inputs and buckshot impact policy, with no tracer |
| `shotgun_shell_support_current` | Existing bounded shell pool, point sweep, unique shell life and contact audio work with brass `shell.mdl` | Reuse simulator/pool with `shotgunshell.mdl`, one/two distinct shell sub-identities |

The retained E13 life/corpse/respawn and E14 DeathMsg/ScoreInfo paths remain
server-authoritative. A Shotgun event is not a hitgroup, damage, health, death,
score or HUD-notice authority. No local player hitbox damage or blood source
is introduced.

## Pinned behavioral reference

Read-only official SDK: `third_party/halflife-sdk`, clean inspected revision
`b1b5cf5892918535619b2937bb927e46cb097ba1`. Inspected `dlls/shotgun.cpp`,
`dlls/weapons.h`, `dlls/weapons.cpp`, `dlls/client.cpp`,
`cl_dll/ev_hldm.cpp`, `cl_dll/hl/hl_events.cpp`,
`cl_dll/hl/hl_weapons.cpp`, `common/weaponinfo.h`, `network/delta.lst`
and the existing shell callback in `cl_dll/entity.cpp`.
These files supply behavioral facts, not copied implementation bodies or a
claim of proprietary stock-engine/binary/RNG parity. SDK revision is not
changed.

Verified catalogue identity: `WEAPON_SHOTGUN=7`, `weapon_shotgun`, primary
ammo `buckshot`, clip maximum 8, slot 2/position 1. Models are
`models/v_shotgun.mdl`, `models/p_shotgun.mdl`, `models/w_shotgun.mdl`;
shell is `models/shotgunshell.mdl`. The actual selected server viewmodel is
still required; ID 7 alone cannot replace a selected model.

| Verified reference fact | Single | Double |
| --- | --- | --- |
| Event resource | `events/shotgun1.sc` | `events/shotgun2.sc` |
| Multiplayer visual pellet count | 4 | 8 |
| Multiplayer spread axes | X 0.08716, Y 0.04362 | X 0.17365, Y 0.04362 |
| Trace distance | 2048 units | 2048 units |
| Required/consumed clip shells | at least 1 / 1 | at least 2 / 2 |
| Both attack cooldowns | 0.75 s | 1.5 s |
| Post-fire server pump sound delay | 0.5 s | 0.95 s |
| Local event animation/body | `SHOTGUN_FIRE` / 2 | `SHOTGUN_FIRE2` / 2 |
| Local pitch recoil | -5 degrees | -10 degrees |
| Fire sample | `weapons/sbarrel1.wav` | `weapons/dbarrel1.wav` |
| Ejected shotshells | 1 | 2 |

Both attacks reject successful fire at water level 3. Double with clip <=1
attempts reference reload/empty behavior, not a false successful double.
Single-player reference counts are 6/12 with both spread axes 0.08716;
normal Crossfire E17 acceptance is multiplayer and must not silently use
those single-player counts.

Reference body nuance: primary `CShotgun::PrimaryAttack` does **not** call
`SetAnimation(PLAYER_ATTACK1)`; secondary does. Remote Shotgun events must not
invent a body attack sequence to make the two paths look identical. Current
committed player sequence remains authoritative.

### Buckshot distribution and impact policy

Reference buckshot samples each axis as a sum of two uniform values in
[-0.5,0.5], rejecting samples whose squared radius exceeds one. This is a
triangular, disk-truncated distribution, not a square and not one reused
direction. Project presentation uses independently authored, bounded,
shot-identity/pellet-ordinal-stable sampling:
`buckshot_spread_rng=local-compatible deterministic`. It does not claim the
stock mutable RNG stream or an authoritative server pellet hit arrangement.
Shot context is frozen at accepted input/event time, not recomputed from the
listener's later mouse/camera or render-frame number.
The seed includes the causal action timestamp and kind, not a sampled wall
clock. Committed-only local events can legitimately share command 0 and one
canonical revision; separate accepted occurrences must still vary. Equivalent
positive/negative zero timestamps have identical seeds. Shotgun volume/pitch
use this occurrence-specific variation; the older Glock/MP5 variant function
and both MP5 tracer branches remain unchanged.

Pinned `EV_HLDM_FireBullets` routes buckshot to gunshot decal/particles but
**does not** call the ordinary `EV_HLDM_PlayTextureSound` material-contact
branch. Thus buckshot material-contact audio is disabled; Glock/MP5/.357
sound policy is not blindly reused per pellet. The gunshot decal helper
independently allows ricochet: integer choice 0..32767 below 16383 and sample
choice modulo five (`weapons/ric1.wav` through `ric5.wav`). An independently
deterministic equivalent is local-compatible, not exact stock RNG parity.
Ricochet is optional, not eight mandatory voices. Tracer count is zero.

## Read-only installed model evidence

Only the explicitly approved research resource root is inspected. No assets
are downloaded, copied, edited or committed and no fallback installation is
searched. Initial structural/geometry inspection below reads bounded v10
records; importer/normal production rendering verification is reported
separately in final checks, not inferred from raw metadata.

| Virtual resource | Bytes | Bones | Attachments | Sequences | Bodyparts | Skin families / texture references / textures |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `models/v_shotgun.mdl` | 92316 | 32 | 1 | 10 | 2 | 1 / 10 / 10 |
| `models/p_shotgun.mdl` | 13468 | 12 | 0 | 1 | 1 | 1 / 4 / 4 |
| `models/shotgunshell.mdl` | 6328 | 1 | 0 | 1 | 1 | 1 / 1 / 1 |

All have IDST v10 and exact declared file lengths. Header movement/clipping
bounds are zero; useful geometry/per-sequence bounds are retained instead.

Actual `v_shotgun.mdl` roles:

| Index | Label | Frames | FPS | Loop / marker |
| ---: | --- | ---: | ---: | --- |
| 0 | `sm_idle` | 21 | 9 | loop |
| 1 | `shoot` | 21 | 20 | frame 0 event 5001, options `31` |
| 2 | `shoot_big` | 21 | 13 | frame 0 event 5001, options `41` |
| 3 | `reload` | 10 | 16 | non-loop |
| 4 | `pump` | 11 | 13 | non-loop |
| 5 | `start_reload` | 9 | 12 | non-loop |
| 6 | `draw` | 13 | 24 | non-loop |
| 7 | `reholster` | 11 | 30 | non-loop |
| 8 | `idle4` | 21 | 9 | loop |
| 9 | `deepidle` | 61 | 12 | loop |

Viewmodel attachment 0 has an empty name, owning bone 29
`Bip01 R Hand`, local origin (30,1,6) and three zero basis vectors. Its
position is valid; the existing neutral attachment evaluator uses the owning
bone basis for this verified zero-basis case. There is no explicit shell
ejection attachment. Weapon geometry on bone 29 has source bounds
(3.41,-0.62,1.95)..(26.43,1.53,6.26); bone 30 `Bone01` owns the movable
fore-end; bone 31 `shell` is a separate root for the reload shell mesh, not
proof of an ejection socket. Both viewmodel bodyparts have one submodel;
reference body 2 resolves safely to that existing selection, not a new mesh.

For local shotshell ejection the inspected asset has no shell attachment, so
the existing explicit eye-space fallback is retained and diagnosed rather
than calling a guessed bone/attachment a validated socket. Both pinned
Shotgun callbacks request **32 units forward, -12 units up, +6 units right**:
`eye + forward*32 - up*12 + right*6`. These are the Shotgun reference offsets,
not the handgun's 20/-12/+4 fallback and not the separate remote model-local
receiver profile below. Spawn lifetime remains 2.5 s. The shell pool and
point-sweep response are shared mechanisms, not a second Shotgun physics
system.

Accepted-input eye/direction/right/up are retained once in the existing
bounded owning shot-context ring. Ejection and pellet spread use that frozen
basis even if the render camera subsequently turns or moves. A committed-only
local `shotgun1/2` event can also supply an immutable context: its resolved
origin, duck/view-height state, full pitch/yaw/roll and explicit
entity-angle provenance determine the basis. That context is tied to the
matching generation and exact event/action time; an unused older echo cannot
become a later action's input. Existing predicted/confirmed action dedup
continues to decide whether an event is a new occurrence. This path does not
rewrite the event, receiving client, wire command, canonical state, ammo,
view angles or prediction history. It is not another local damage trace.

The p-model has the existing compatible 12-bone player chain, with final
bone 11 `Box01`, parent 10 `Bip01 R Hand`. Box01 default translation is
(12.295609,1.386845,1.667124), rotation radians
(1.598449,-0.010846,0.096108). All 104 source vertices belong to Box01,
bounds (-12.26,-4.01,-1.57)..(12.26,4.01,1.57). Its sole `idle` sequence
has one frame at 30 FPS; animation comes from generic bone merge, not a
Shotgun fire sequence on the p-model. Sequence bounds are
(0.607818,-12.254080,15.116165)..(14.604750,-9.114079,38.236977).

The `shothole.bmp` muzzle-face mesh contains exactly four vertices at
X=12.26, Y=2.22..3.32, Z=-0.64..0.64. This verifies a +X barrel axis
and the muzzle center (12.26,2.77,0), independently of an MP5/Glock offset.
The normal remote profile uses bone 11 muzzle (12.35,2.77,0), including
0.09-unit front clearance, and shell origin (2,2.77,-1.65) beside the
receiver. Its local basis is forward +X, right -Z, up +Y, not the MP5's -X
barrel profile. These numeric points are a validated local-compatible model
profile, not explicit asset attachments or stock constants. Validation
checks the model semantic/revision, chain, Box01 defaults and source-vertex
bounds once per immutable revision. Explicit compatible named attachments
retain priority; unsupported models retain an explicit degraded reference
fallback or unavailable result, not a falsely validated socket.

Shotshell `shell` bone is a root. Geometry bounds in its bone-local space:
(-0.8,-0.8,0)..(0.8,0.8,3.51); sequence bounds
(-2.324276,-0.794042,0.025172)..(1.185724,0.805958,1.625173).
Its static `idle` has one frame and FPS 0; this is not a failed animation.

Read-only SHA-256 controls:

| Virtual asset | SHA-256 |
| --- | --- |
| `models/v_shotgun.mdl` | `2247AD40F9209743F3C1611D0B1FBD2650505F2C7710F5093E1F1D77BBD50D08` |
| `models/p_shotgun.mdl` | `0462D6B8A8E96EDBD8150A1FC89A7929A8F4E51C95076757ABCADBFC81243FC1` |
| `models/shotgunshell.mdl` | `24A299315CCFF74B2EA9C83CE6DB74AD9089C26C5464A83277A7728C72F79BBE` |

## Audio and special reload authority

`shotgun_reload_audio_source=server_sound`. Pinned insert calls emit
`weapons/reload1.wav` or `weapons/reload3.wav` on the item channel. Finish
reload emits `weapons/scock1.wav`; post-fire pump timer independently emits
the same sample. Existing server `svc_sound` execution must remain the
single source, without a timer-based local duplicate. Fire sounds instead
use the established immediate local/remote event presentation and echo
deduplication, with one sound per semantic shot.

Reference special state values: 0 not reloading; 1 start/wait after a prior
transfer; 2 current insert. Start animation gates the player for 0.6 s and
attack timers for 1.0 s. Insert animation/sound sets a 0.5 s wait. The later
state-2 branch transfers exactly one server shell from reserve to clip and
returns to state 1. At clip 8 or exhausted reserve, the reference sends the
`pump` animation, emits scock1 and retires special state to 0. The base
ItemPostFrame prioritizes eligible secondary/primary attack over explicit
reload, so firing can validly interrupt; presentation must not fight fresh
server state. Death, switch, map/network generation and teardown retire
pending old-life reload state without resetting transport sequence/history.

Neutral decoding uses negotiated descriptor names/types, never SDK ABI
memcpy or fixed offsets. Pinned `network/delta.lst` advertises
`m_fInSpecialReload` as 2-bit integer and `m_flPumpTime` as signed 22-bit
float with multiplier 1000. Missing descriptors remain optional/unavailable;
malformed projection fails atomically. Descriptor availability does **not**
prove live timer availability: the pinned `dlls/client.cpp` export of
`m_flPumpTime` is commented out, as are its restore/publication/decrement in
client prediction. `m_fInSpecialReload` is actively exported/restored.
`m_flNextReload` is advertised but its meaningful population is not assumed
merely because the field exists; fresh special-state/clip/idle/attack
observations determine staged presentation. No fabricated clip increment,
local full-magazine timer or unconditional post-fire pump animation.

Exact approved local WAV controls are present and decode structurally to
nonzero PCM: sbarrel1 (1.039 s), dbarrel1 (1.374 s), reload1 (0.215 s),
reload3 (0.607 s), scock1 (0.531 s), all mono 8-bit PCM at 22050 Hz.
Ric1..5 are mono 8-bit PCM at 11025 Hz, durations 0.474..0.628 s.
`weapons/sshell1.wav` and `weapons/sshell3.wav` also exist and are nonzero
(0.707/0.947 s). SDK `TE_BOUNCE_SHOTSHELL` delegates contact playback to
the opaque engine callback; its exact sample selection is not established
by visible shell geometry or the commented client precache lines. The
production Shotgun contact selection uses the approved original
`weapons/sshell1.wav` through the existing qualifying-contact/pool/mixer path
and prewarms it, separate from handgun/MP5 `player/pl_shell1.wav`. This
selection and the retained bounded collision gains/contact limit are
local-compatible, not proven stock shotshell-contact parity. No
device/listening result is inferred from PCM.

## Production integration contract

All Shotgun meanings, event names, sample choices, model compatibility,
pellet count/spread/ricochet policy and special-reload rules belong to
`games/halflife`. Generic host/API provide immutable observations, accepted
command notification and bounded neutral outputs. Existing engine retains
wire framing/delta transactions, approved I/O, collision queries, history,
prediction replay, Studio evaluation/bone merge, shell simulation,
decal/particle pools, OpenGL passes and bounded mixer. Renderer does not
know Shotgun, shotgun1/2, buckshot, filenames or pellet counts.

One semantic shot owns one action identity, one flash/light, one sound and
one/two distinct shell sub-identities. Each pellet owns a stable sub-identity
for spread/impact/ricochet; no render/update/replay duplicate may create a
second set. Missing decal, audio or shell resources skip only that component
and never reinterpret a ready component as unavailable or another model.
Fresh/retained/missing fields, late confirmations and generation/life
boundaries remain explicit. The server selected resource and actual event
manifest win over arbitrary IDs. World-space effects already published do
not disappear merely on later weapon switching.

The neutral per-action output has one primary shell/impact plus bounded
extra arrays for one more shell and seven more impacts: at most two casings
and eight traces per action, not a growable per-shot allocation. Remote
effect pending/batch/pose/profile bounds remain 32, exact event dedup remains
128 and accepted action history remains 64. Generic pooled execution retains
existing global decal, particle, shell and voice limits.

### Actual source and dependency mapping

| Responsibility | Production source / contract | Ownership |
| --- | --- | --- |
| Exact negotiated numeric weapon fields and atomic publication | `src/goldsrc/runtime_replay_session.cpp`; `include/hlclient/client/runtime_observation.hpp` | Generic GoldSrc/client observations; missing is unavailable, not a fabricated reload/pump value |
| Validated Shotgun catalogue/model roles, 0.75/1.5 s cadence and bounded pellet distribution | `include/hlclient/games/halflife/firearm_profile.hpp` | `hlclient_game_halflife` |
| Single/double eligibility, sequence/body/recoil, staged server-observed shell-by-shell reload and interruption | `src/games/halflife/local_weapon_presentation.cpp`; `include/hlclient/games/halflife/local_weapon_presentation.hpp` | `hlclient_game_halflife`; no canonical ammo writes |
| Selected viewmodel intent and exact local event interpretation | `src/games/halflife/presentation.cpp`; `src/games/halflife/client_module.cpp` | `hlclient_game_halflife`; generic host receives typed outputs |
| Marker flash/light, frozen input/event shot basis, one/two casing requests and four/eight distinct pellet requests | `src/games/halflife/weapon_visuals.cpp`; `include/hlclient/games/halflife/weapon_visuals.hpp` | `hlclient_game_halflife`; neutral bounded `game_api/local_visuals.hpp` output |
| Immediate fire samples, volume/pitch, readiness set and echo dedup | `src/games/halflife/weapon_audio.cpp`; `src/games/halflife/client_module.cpp` | `hlclient_game_halflife`; reload/pump remain server audio |
| Exact remote Shotgun event binding, p-model socket/profile, semantic effect identity and remote audio | `src/games/halflife/remote_effects.cpp`; `include/hlclient/games/halflife/remote_effects.hpp` | `hlclient_game_halflife`; no remote body sequence invention |
| Buckshot static-surface decal/particles, optional per-pellet ricochet and shotshell contact sample selection | `src/games/halflife/client_module.cpp`; existing Half-Life material/audio profile | Game policy; no material-contact sound copied from the ordinary bullet branch |
| Approved model import, camera-local Studio pose, neutral bone merge/socket execution and exact per-shell virtual model binding | `src/app/runtime_replay_local_assets.cpp`; `include/hlclient/app/runtime_replay_local_assets.hpp` | Generic application mechanism; named missing shotshell cannot silently use ready brass |
| BSP impact execution, bounded decals/particles and remote effect execution | `src/app/world_impact_presentation.cpp`; `src/app/remote_effect_presentation.cpp` | Generic application/renderer mechanism |
| Fixed-step shell pool, per-life contact observation and named owning shell token | `src/renderer/transient_visuals.cpp`; `include/hlclient/renderer/transient_visuals.hpp` | Generic bounded simulation; existing lifetime/point-sweep rules |
| Approved WAV opening/decoding, spatial voices and PCM mix | `src/goldsrc/sound_assets.cpp`; `src/goldsrc/local_audio.cpp`; existing E3 audio backend/mixer | Generic execution; validated game-selected tokens only |

No new concrete module, engine loop, renderer, mixer, dynamic library or
gameplay CMake target is introduced. The G1 direction remains
`hlclient.exe composition -> generic host/API + hlclient_game_halflife`,
`hlclient_game_halflife -> hlclient_game_api + permitted shared mechanisms`;
lower engine targets do not link back to Half-Life. The host uses the same
typed bounded outputs for this profile; `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`
still excludes concrete game objects. The per-shell model token is neutral,
owning and checked against already approved bindings, not a native path or
game identifier exposed to the renderer.

Focused project-owned regressions are ordinary files:
`tests/test_shotgun_local.cpp`, `tests/test_shotgun_remote.cpp`,
`tests/test_shotgun_composition.cpp`, negotiated-field cases in
`tests/test_runtime_replay_session.cpp`, and normal host/importer/Studio/GL
cases in `tests/test_runtime_replay_local_assets.cpp`. The latter builds its
own geometry, animation streams, ten public role labels and frame-zero
markers; it does not embed installed model bytes. Separate opt-in approved
installed-asset controls are read-only and not required by core-only CI.
Completed execution results and explicit skips are recorded below; test names
alone are not used as evidence of passing verification.

### Verification-driven corrections

The affected Debug run exposed a genuine Win32 stack-headroom defect. The
expanded fixed remote batch is 89,088 bytes. A conditional-expression return in
`GameClientHost::drain_remote_effects` created two aggregate temporaries in
unoptimized MSVC, contributing 178,440 bytes. Direct returns now forward the
caller-owned result with guaranteed value elision (12 local bytes in the same
Debug object), without changing inactive/active behavior or the public API.
The two existing `WorldImpactPresentation` pools are each 58,896 bytes; normal
`run_live_visual_control` now owns them once per session through RAII heap
storage and stable references. No per-shot/frame allocation, new pool, changed
bound, clock, tracer setting or gameplay rule is introduced.

Measured Debug normal visual-loop frame: 735,256 -> 617,480 bytes; major
drain-chain frames approximately 1,029 KB -> 733 KB, excluding ordinary
prologue/CRT overhead. Both Win32 PE stack reservations stay exactly 1 MiB.
The long lifecycle test also used a separate full-batch temporary for each
count/statistics-only assertion; a small owning summary helper reduces its
frame from 774,756 to 240,948 bytes while keeping every assertion and both
effect-bearing drain results. These are object/disassembly and offline-test
observations, not a live stack capture.

The new multi-model shell materializer initially overwrote
`binding_unavailable` with `no_valid_instances` when every unavailable binding
was skipped. It now preserves the former, preserves pose/material rejections,
and uses the latter only when a ready binding yielded invalid instances. The
unchanged E4.1 typed-failure assertion is the independent regression control.

Approved audio loading is deliberately nonblocking: `try_to_lock` may return
`pending` even for a ready cache entry while incremental movement preparation
uses the same worker/cache. The new composition fixture initially assumed one
update must start a voice. It now delivers the event once and pumps empty
matching session/scope/epoch batches at fixed test simulation time, bounded by
2 seconds of wall time and 2,001 updates. Exact voice counts, deduplication and
finite nonzero isolated PCM remain required; missing, late, rejected and
output-failure counters must remain zero. No production event is resubmitted,
no 250 ms production deadline is changed, and reload/pump remain single
committed server sounds.

Actual installed insert-animation GPU control samples a meaningful visible
phase (frame 6), not an arbitrary early subframe: early hand/root-shell palette
changes are below the 60-degree view frustum and the weapon-owning bone remains
static through frame 4. At frame 6 the shell and owning weapon bone move in the
visible region. The test retains separate CPU bone-change and GPU pixel-change
checks, camera-local invariance, no asset reupload and canonical/duplicate
checks. Mandatory owned-fixture early timing and old assertions are unchanged.

The final task audit added focused normal p-Shotgun merge/socket/GPU controls,
Shotgun -> Glock/MP5 -> Shotgun switchback, retained/missing-slot reload inputs,
server-only Shotgun DeathMsg/ScoreInfo and repeated four/eight bloodsprite
bursts. Both owned and separately approved installed p-model controls exercise
the real module, approved importer, named-bone merge and renderer: four attached
walk/crouch/fire/re-arm phases require body-only GPU A/B differences; server
sequence/palette, exact socket transforms and upload reuse remain checked.
Death/slot-zero removes the child and publishes a typed unavailable per-entity
pose sample. A late Shotgun event must not reuse the old flash/light/shell
socket. The initial new fixture expected an empty all-player pose view; it was
corrected to this existing explicit invalidation contract and strengthened
with stale-event checks. Existing assertions and production invalidation are
unchanged. A new compound Catch assertion also required parentheses to compile;
its logical condition was preserved.

The new rejection test reproduced a real provisional-ammo bookkeeping defect:
with server clip=1, a pending single rejected by a fresh special-reload stage
left one reserved shell, so the next permitted primary action had no identity.
`finish_pending(server_rejected)` now releases only the selected Shotgun
action's own reservation before retiring it. Canonical clip/reserve is never
written. Existing clip-consumption and timeout release paths are unchanged;
the rejection guard excludes `timed_out` and other weapon profiles, preventing
double-release or changing MP5/Glock behavior. Single/double rejection and a
separate overlapping-pending timeout regression pass without weakening their
eligibility assertions. The failing pre-fix run is retained as
`rejection-reservation-red.txt`.

## Shotgun top-band correction — 2026-10-04

New manual evidence, `attribution=user_report`: after picking up Shotgun, a
red/brown horizontal band appears at the top of the viewport; it disappears
and returns. This reports a defect, not acceptance of all E17 features.
The new corrected executable's manual validation remains `not_run`.

The approved unchanged `models/v_shotgun.mdl` has a second bodypart named
`shell`, with one mesh using `shell.bmp` (48 x 79), attached to root bone 31.
Outside insert sequence 3 its root-forward coordinate is 0.002979 units;
the mesh spans forward -0.397021..0.402979. The previous camera-local pass
inherited the world near plane of 0.1 units, admitting six of its 18 vertices
while clipping twelve. This camera-intersecting reload detail expands into
the band. It is not the HUD, an ejected world shell, an edited texture or a
new pickup effect.

The new independent owned-fixture GPU regression fails before the fix in
both world-present/world-absent branches and 4:3/16:9 extents. The read-only
actual Shotgun GPU control reproduces 28,800 changed pixels in the top
60-row region at idle frames 0 and 1. Evidence is retained as
`clipping-owned-red.txt` and `clipping-installed-red.txt` under
`manual-artifacts/task-records/e17-shotgun-stripe-fix-20261004`. An initial
new-test compile error used equality on `AssetVector3`, which deliberately
has no such operator; the new test now checks each component explicitly.
This fixture correction does not weaken any old assertion.

Production fix: `src/renderer/render_camera_math.cpp` gives only the
camera-local first-person pass its own 1-unit near plane. FOV/far plane stay
unchanged; the world camera/depth, viewmodel transform, complete bone palette,
body selection, installed assets, effects, wire data and game rules stay
unchanged. The renderer remains weapon-neutral. This is a local-compatible
projection policy, not a claim about an opaque stock-engine clipping constant.
Invalid local depth ranges still fail validation rather than being guessed.

The actual-asset control checks every frame of the nine non-insert sequences
(189 frames), comparing the complete model against a test-only pose exclusion
of root bone 31. This component contributes zero pixels anywhere, including
while a legitimate fire animation raises the weapon into the top region.
All ten insert-sequence frames keep every shell vertex beyond the new near
plane. A separate shell-only GPU control has seven visible frames; the full
model has two frames with an unoccluded shell contribution. The remaining
frames may legitimately be outside the view frustum or hidden by hand/receiver.
No installed bytes are modified. The initial new expectations incorrectly
required a blank top region during every fire frame and an unoccluded shell at
the final insert frame. Component-isolated A/B replaces these assumptions;
existing animation, occlusion and old assertions remain unchanged.

The first Debug link attempt also reported LNK1163 (invalid COMDAT selection)
in the existing large runtime-asset test translation unit after new checks
were appended. Object inspection found an associative section with parent
index zero; the compiler's exact internal cause is not established. Only the
new checks were moved into ordinary `tests/test_viewmodel_clipping.cpp` and
registered in `tests/CMakeLists.txt`. The original runtime-asset test source
is byte-identical to the pre-correction snapshot; no compiler/linker flags,
existing tests or production behavior were changed to bypass it. The split
fixture now supplies the required generation, publication and canonical-hash
metadata rather than expecting an invalid observation to be accepted.

The existing local/remote Shotgun action, reload and other weapon controls
remain enabled. Corrected-build verification and digest are recorded in a
separate section after the historical initial E17 handoff below.

## Initial E17 verification and handoff (before the top-band correction)

Evidence lives under `manual-artifacts/task-records/e17-shotgun-20261004`.
Earlier successful runs and red/build/fixture failures are retained, not
rewritten as final acceptance. No old assertion is weakened to obtain green.

| Check | Result / retained evidence |
| --- | --- |
| Pre-edit affected Release baseline | 136 cases: 132 passed, 4 optional skips; 94,576 assertions (`baseline-release.txt`) |
| Final affected Debug after the last production fix | 370 cases: 361 passed, 9 optional skips; 270,437 assertions; zero failures (`focused-debug-handoff-final.txt`) |
| Final focused E17 Release | 38 cases: 37 passed, one installed-asset opt-in skip; 12,731 assertions (`focused-release-handoff-final.txt`) |
| Final complete Release offline suite after the last production edit | 2,869 CTest entries: 2,827 executed/passed, 42 explicit capability/opt-in skips, zero failures; 428.84 s (`release-offline-handoff-final.txt`, `release-handoff-final-LastTest.log`); earlier 2,862-test success remains historical |
| Half-Life-OFF core Release | Existing `build-core-g1` rebuilt; 41/41 CTest checks pass in 24.63 s, including alternate-module/core API and architecture guard (`core-offline-handoff-final.txt`) |
| Mandatory production OpenGL | Owned viewmodel single/double/start/insert/finish A/B, camera-local invariance, remote p-Shotgun merge/socket/body-only A/B and localized eight-impact/no-tracer controls passed; no mandatory GL capability skip |
| Separate approved installed Shotgun / device control | Two cases / 715 assertions pass, including actual v/p-model importer/Studio, p-model bone merge/socket/GPU, original ready WAV PCM and short SDL device output (`approved-assets-device-handoff-final.txt`); not human listening |
| Prediction preservation | 14 scenarios x 1,000 commands/config; 28 Debug/Release state/history pairs match retained E16 baselines and one another exactly; network operations=0 (`prediction-hash-comparison.txt`) |
| No-stock launcher/summaries | Existing owned-fixture jump/duck, H1 speed, H2 launcher and Fast-launch checks passed; no real game helper is substituted |
| Final launcher `-CheckOnly` | reference/two-peer/NoTimeLimit, prediction off, damage-respawn-check and source-only all pass against the final executable; zero runner/game/Steam/WFP/socket launches (`checkonly-handoff-*.txt`) |
| ASan build | Existing `build-asan` Win32 core/API target build passed (`build-asan-core-acceptance.txt`); no tooling repair or claim that all application objects were rebuilt under ASan |
| ASan core/API runtime | 22 cases / 523 assertions pass with the matching MSVC x86 runtime (`asan-core-handoff-final.txt`) |
| ASan application runtime | `not_run` / `not_verified`; earlier application `--help` exit 1/no-output remains an unconfirmed historical limitation, not a full-app pass |

Core-only inspection found no concrete Half-Life source, include or direct/
transitive link in the actual OFF project/dependency graph: 145 projects,
319 compile entries / 318 unique source paths. The concrete project/library
is absent. The guard's negative direct, alias, transitive, generator-expression,
object, source and include controls remain enabled. The same `GameClientHost`
and project-owned alternate module run without a Half-Life fallback.

The historical PS5.1 publication failure did not retain enough diagnostics to
establish its precise invariant/cause. Its unchanged isolated validation passed
later; no deadline, launcher or assertion was changed. The final complete suite
passes both PowerShell 7 and Windows PowerShell 5.1 publication roundtrips;
the latter took 6.72 s. These are final-suite results, not an inferred cause or
an upgrade based only on the isolated retry.

Release binary:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
Release SHA-256:
`E24BA02E1AF8B7B6CF1BDE142BB8836C89C03001DB0E9B2732997B513DA01B41`.
Size: 3,680,768 bytes; final build written 2026-10-04 14:56:46 UTC.
The unchanged launcher resolves `build/bin/Release/hlclient.exe` beneath its
own worktree root, not a demonstration/test executable. Its SHA-256 remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
Both digests were rechecked after the final suite. The pre-edit snapshot still
has 1,213 valid saved source files: zero hash mismatches or missing current
files, 1,181 unchanged and 32 intentionally changed existing files. E17 adds
four ordinary files (this contract and three Shotgun test sources). All 74
script files and 158 source paths matching movement/prediction/collision/input
remain byte-identical to the E17 baseline. These counts compare to the saved
local E16 state, not to the older Git HEAD. Existing unrelated dirty files are
preserved. See `source-preservation-handoff-final.txt`.

Actual branch/HEAD remain as recorded above; the SDK pin is clean and staging
is empty. `git diff --check` passes. No build tree was cleaned or recreated.
Normal and core-only verification use the existing VS2022/Win32 trees:

```powershell
cmake --build build --config Release
cmake --build build --config Debug --target hlclient_tests
cmake --build build-core-g1 --config Release
ctest --test-dir build-core-g1 -C Release --output-on-failure
```

The existing core-only cache has `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`; normal
`build` has it ON. The complete Release suite uses its discovered offline
inventory with opt-in live/capability gates disabled, not a new managed session.

Unchanged user-run manual command (Codex does not run it):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Crossfire/two-client checks: select Shotgun; primary one shell/four pellets,
secondary two shells/eight wider pellets, differing animation/audio/recoil,
one flash/light per shot and no MP5 tracer; double fails safely with one
shell; server HUD increments reload one shell at a time; reload and pump
sound do not duplicate; fire/switch/death interrupt cleanly; remote p-model
stays attached during crouch/move/jump; player hits retain E13 smooth
death/respawn and E14 notices/scoreboard; MP5/M203/Glock/crowbar remain usable.
No exact pellet RNG parity is inferred visually.

Keep the new build's manual evidence separate from the preceding E16 report:

```text
single_fire_manual=not_run
double_fire_manual=not_run
shell_count_manual=not_run
pellet_spread_manual=not_run
special_reload_manual=not_run
reload_interrupt_manual=not_run
remote_shotgun_manual=not_run
shotgun_kill_respawn_manual=not_run
```

## Corrected top-band build verification and handoff

Correction evidence is separate from the initial E17 report:
`manual-artifacts/task-records/e17-shotgun-stripe-fix-20261004`.
Production changes were frozen before these final builds/runs. No historical
manual or acceptance log is rewritten, and no old assertion is weakened.

| Check | Corrected-build result / evidence |
| --- | --- |
| Normal VS2022/Win32 Debug | `hlclient` and tests built; affected suite: 395 cases, 385 passed, 10 opt-in skips, 271,093 assertions (`focused-debug-final.txt`) |
| Normal VS2022/Win32 Release | All existing targets built; same affected suite and counts pass (`build-release-final.txt`, `focused-release-final.txt`) |
| Final complete Release offline suite | 2,872 entries: 2,829 executed/passed, 43 explicit capability/opt-in skips, zero failures; 388.18 s (`release-offline-final.txt`, `release-final-LastTest.log`) |
| Inventory preservation | All 2,869 previous commands unchanged; only three new clipping cases, no removals (`release-test-inventory-final.json`) |
| Mandatory production OpenGL | Independent near-origin/ordinary Studio geometry, both world branches, 4:3/16:9, camera-local invariance, upload reuse and cleanup pass; math and GPU clipping cases also pass in the final full suite |
| Separate approved actual Shotgun GPU controls | Debug and Release: two cases / 3,369 assertions each; 189 non-insert frames have zero root-shell contribution, insert vertices survive clipping, seven isolated-visible / two full-model unoccluded shell frames (`clipping-installed-debug-isolated-final.txt`, `clipping-installed-release-final.txt`) |
| Half-Life-OFF Release | Existing `build-core-g1` rebuilt; 41/41 CTest checks pass, including alternate module/API, new camera math and architecture guard; 21.46 s (`core-offline-release-final.txt`) |
| Focused ASan Debug core/API/camera | Existing target rebuilt and 28 cases / 611 assertions pass with the MSVC x86 runtime on PATH (`build-asan-core-final.txt`, `asan-core-final.txt`) |
| Full-application ASan | Not rerun / not verified; the historical application exit-1/no-output limitation is not upgraded by a scoped core pass |
| Unchanged launcher | reference/two-peer/NoTimeLimit, off, damage-respawn-check and source-only `-CheckOnly` pass against the new normal Release; zero runner/game/Steam/WFP/socket launches (`checkonly-final-*.txt`) |
| New manual acceptance | `not_run`; the user's intermittent stripe report is defect evidence, not acceptance of this corrected executable |

The reachable core-only `ALL_BUILD` graph, including third-party dependencies,
has 151 projects and 695 compile entries / 694 unique source paths. No concrete
Half-Life source/include/link match, concrete module project or module library
is present (`core-dependency-audit-final.txt`). The same guard still rejects
direct, alias, transitive, generator-expression, source, object and include
violations. No Half-Life installation is needed for the core-only suite.

Corrected normal Release:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
SHA-256: `2FE860AA107D7D789E707AD7B724FC4CBD9CF3FEC19DF1BA5359133EA8D2410C`.
Size: 3,680,768 bytes; written 2026-10-04 16:11:09 UTC. This supersedes the
initial E17 digest above; it is not attributed to the earlier user screenshot.
The launcher still resolves that executable beneath its own worktree root;
its unchanged SHA-256 is
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
The read-only installed viewmodel retains SHA-256
`2247AD40F9209743F3C1611D0B1FBD2650505F2C7710F5093E1F1D77BBD50D08`.

The verified pre-correction snapshot contains 1,217 ordinary source files:
zero saved hash mismatches and zero missing current files. This correction
changes six existing files (camera helper/header, camera math tests, test
registration, architecture and this record) and adds only ordinary
`tests/test_viewmodel_clipping.cpp`. The original runtime-asset test source,
launcher/scripts, gameplay, prediction, audio, effects and installed assets
are preserved. Branch/HEAD and clean SDK pin remain as recorded above;
staging is empty and `git diff --check` passes. No build tree was cleaned or
recreated. No new game/live/capture session, Steam initialization, WFP
activation, audio-device control, staging, commit or push was performed.

Use the unchanged two-client manual command above. Pick up/select Shotgun;
check idle/draw, primary/double fire, start/insert/finish reload, pitch/yaw and
switchback to the other weapons. The top stripe should be absent while the
real reload-shell detail remains visible in its appropriate phases. This is
an offline-verified local-compatible projection correction, not new gameplay
or a stock-engine clipping-constant parity claim.

## Explicit compatibility and verification limits

- E17's four/eight-pellet profile is HLDM, not the single-player six/twelve
  profile. Deterministic triangular/disk-rejected sampling is presentation,
  not the server's mutable RNG stream or damage authority.
- The inspected p-model muzzle/receiver points and original `sshell1.wav`
  contact choice are validated local-compatible profiles, not proven opaque
  stock-engine socket/contact parity. Invalid/missing models remain typed
  unavailable/degraded results, without substituting brass for shotshells.
- Special reload follows fresh negotiated server observations. Missing fields
  do not invent inserts; advertised `m_flPumpTime` does not prove meaningful
  live values. `server_sound` is established from the pinned reference and
  owned committed-audio fixtures, not a new live Shotgun traffic capture.
- Pellet effects remain static-BSP presentation with the existing explicit
  unsupported-surface/committed-brush policy. No player hitbox damage, blood
  authority, dynamic-body simulator, new material system or tracer is added.
  Existing global effect/voice limits can saturate and retain typed skips.
- Nonzero PCM, production GPU pixels and short SDL device output are separate
  evidence. They do not establish human listening or new build manual gameplay
  acceptance. Full-application ASan is not inferred from a scoped core run.

New live clients/stock clients/HLDS/managed captures, Steam initialization and
WFP activation: **0**. Separate opt-in offline audio-device controls were run;
human listening confirmation is **not_run**. Staging/commit/push: **none**.
E18/.357 and other weapon/gameplay milestones are outside E17.

## Later user confirmation recorded during E19

The E19 task supplies the user's later manual acceptance after the E17
implementation/fixes: `E17 manual=passed; attribution=user_report`.
This is additive evidence for Shotgun, not a rewrite of the initial
`manual=not_run` acceptance record. The reported intermittent near-camera
cartridge clipping defect and its subsequent fix remain historical evidence.
This confirmation does not certify every stress/water case, stock-binary
parity, E18/.357, or the new E19 executable; E18 and E19 manual remain `not_run`.
