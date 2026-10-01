# E5 — local Glock impacts on static BSP world

Status: integrated in the normal `hlclient.exe` path; deterministic offline
checks and optional read-only asset controls are separate from manual play.
This ID was not occupied by an existing local E5 task document. The user's
E4.1 «Тепер все працює» is recorded in `LOCAL_WEAPON_VISUALS_E4.md` with
`attribution=user_report`; **this E5 build is manual=not_run**.

## Ownership and production chain

`LiveWeaponCommandSubmission` retains the quantized angles of the actual
submitted wire command. The normal host pairs those with the latest committed
presentation camera eye at submission-drain time, and passes an owning
`LocalWeaponSubmittedCommand::ShotContext` to `GameClientHost`. Only the
Half-Life module associates a fresh accepted B1 Glock primary-fire identity
with that generation/command sequence. Empty fire, reload, crowbar, a capture
click, rejected/stale action and missing context do not schedule an impact.
The action journal prevents a confirmation or replay from re-arming it.

The module emits one neutral `LocalWorldImpactRequest`. Shared
`WorldImpactPresentation` traces a finite 8192-unit line against the BSP
collision world and, when present, the committed brush collision scene. A
nearer dynamic brush hit is an unsupported blocker, never a reason to put a
mark on the farther wall. Start-in-solid, miss, invalid/expired request and
trace failure are typed skips. For a world hit the presenter requires a unique
matching render-surface triangle within tight plane/bounds tolerances; a
second candidate is ambiguous and rejected. Special, masked, sky, liquid,
backfacing, degenerate and unmapped surfaces receive no decal or impact audio.
The supported material profile is one opaque static world surface; there is
no inferred materials table, penetration, ricochet or water effect.

The initial E5 profile chose `{SHOT1` in `decals.wad` and
`weapons/ric1.wav`; E5.1 supersedes the sound and material mode below. The WAD is opened once through the same exact-root,
identity-checked local resource environment as the current world, with a
4 MiB file and 256×256 texture bound. Its WAD3 masked alpha is retained.
There is no arbitrary path open or game asset checked into Git. WAD status is
`not_requested` (or `pending_local_assets` before the local projection),
`ready`, `absent`, or `rejected`. No old shot is retried if it was not ready.
The sound uses the existing approved E3 loader/cache/mixer, with world-space
hit origin, initial attenuation 1, bounded pending voices and 250 ms readiness
deadline. Missing decal does not suppress an otherwise valid sound request;
missing/failed sound does not remove a ready decal. Diagnostics distinguish
impact requests, submitted/missing/late/muted/rejected audio and decal asset
status. An output-device failure is not claimed to be audible success.

`RenderScene::world_decals` contains only neutral position/UV triangles and
the retained masked texture. Initial E5 OpenGL used alpha blending;
E5.1 selects modulation for `{SHOT1`. Both draw after the world with
depth test, backface culling and a small polygon offset; no screen-space quad
or viewmodel coordinates are used. The exact surface triangles clip each
decal before presentation. The immutable texture is uploaded once per GL
resource lifetime, and revisioned vertices upload only on spawn/expiry.
The pool holds at most 64 decals × 96 vertices = 6144 vertices, 20 seconds
each, with deterministic oldest-slot replacement. A request remains eligible
only 250 ms after the accepted action; published marks may survive death or
respawn on the same map. Network generation or world resource identity change
clears the map-scoped pool. Module reset, weapon switch, focus cancellation,
death and life-epoch change clear pending impacts without resetting network
history or the map's already published marks.

## Exactness boundary

This is **deterministic local-compatible presentation**, not a stock-exact
Glock hit prediction. The aim direction is from quantized submitted angles;
the eye is the latest presented camera when that notification is drained.
That eye is not an independently recorded historical command eye. The
profile does not reproduce stock random seed, spread, recoil-dependent shot
vector, trajectory distance or server-authoritative hit/damage. The muzzle
attachment remains separate from this shot origin and from the world hit.
Studio/player targets and moving-brush decals are unsupported; a known nearer
brush blocker is honored, but unrepresented dynamic entities cannot be
guaranteed blockers. Remote weapon events are not synthesized.

## Verification boundary

Project-owned fixtures cover accepted B1 identity and immutable context,
duplicate/missing/empty/crowbar exclusion, exact-root WAD ready/absent/rejected,
finite trace/miss/start-solid/unsupported blocker, unique surface match,
clipped edge geometry/UV, pool eviction/expiry, E3 world-origin mixer start
and OpenGL framebuffer A/B with stable off-patch controls, camera movement,
backface and expiry. Optional installed Valve `decals.wad` and E5-era `ric1.wav`
controls are read-only and not build dependencies. Runtime manual sound and
visual quality remain for the user to verify; no new live session was run for
this implementation. Build/suite results and executable SHA belong to the
final task handoff, not to a fabricated live record.

## E5.1 correction — decal material and contact audio

`attribution=user_report` for the subsequent E5 manual run:
`decal_background=white_rectangles_observed`,
`bullet_impact_sound=not_heard`, `shell_contact_sound=not_heard`.
The reported screenshot establishes the rectangles, not which import/render
stage produced them. No executable hash or per-effect audio trace was supplied
for that run. The E4.1 light/shell-visibility user confirmation remains scoped
to E4.1; it did not establish E5 audio. The new E5.1 build is `manual=not_run`.

The installed `decals.wad` `{SHOT1` is 16×16: 211 of 256 base texels use
palette index 0, RGB (255,255,255), and none use masked index 255. The shared
WAD decoder correctly preserves those opaque white texels. E5 incorrectly
submitted them to straight-alpha OpenGL blending, turning the decal footprint
into a white square. HalfLifeClientModule now selects a narrow
`white_neutral_modulate` material for this profile. The neutral renderer mixes
the texture RGB toward white by its coverage and multiplies it with the
already-lighted framebuffer wall; white leaves the wall unchanged and the
dark shot mark remains. Other straight-alpha decals keep their separate blend
mode. Filtering is linear, wrap is clamp-to-edge, and no mipmap is sampled;
the renderer restores blend/depth/cull/offset state after the world-decal pass.
No WAD asset is edited or copied into the repository.

Pinned SDK `EV_HLDM_PlayTextureSound` chooses `player/pl_step1.wav` for a
concrete material strike. E5 had selected `weapons/ric1.wav`, which that SDK
uses for an optional, independent ricochet; it was not a material-hit sample.
Moreover, E5 started asynchronous approved loading only when an impact cue
arrived, while the cue had a 250 ms deadline. This could silently drop the
first otherwise valid hit if the resource stayed pending. E5.1 chooses the
concrete material-strike sample for its existing single static-surface profile
and begins exact-root, bounded approved loading at local projection readiness.
The world hit, decal and sound remain independent: absent audio does not
prevent a mark and absent WAD does not prevent sound. The old manual report
cannot prove whether its specific silence was asset readiness, playback device,
or attenuation; E5.1 does not relabel a queued voice as heard.

The existing 120 Hz `TransientVisuals` BSP point sweep had collision/bounce
statistics but **no contact-to-audio path**. It now emits at most 32 neutral,
owning contacts per update from real non-startsolid plane hits, with world
position, pre-response inward normal speed, action/lifetime identity and a
monotone per-shell contact ordinal. A 45 ms same-contact exclusion avoids
substep chatter; reset/expiry produce no contact. Culling is downstream of
simulation. The HL module applies a local-compatible profile: first contact
requires at least 25 units/s, later bounces 40 units/s, at most three audible
ordinals with gains 0.7/0.45/0.3, attenuation 0.8 and the approved
`player/pl_shell1.wav` sample. The first contact is not scheduled at muzzle
time. Pool-slot reuse begins a fresh ordinal/identity. Contact audio is not
cancelled merely by a later weapon-model/reload change; map/session reset
rejects stale generation. Existing mixer voices own PCM independently of the
visual shell's pool slot and may finish after that shell expires.

The E3 sink preserves distinct automatic voice IDs for fire, impact and shell
contact. Pending loading of one sample no longer blocks a later ready sample;
world-effect cues survive a same-session weapon scope correction until their
existing 250 ms deadline, while network-session reset still discards them.
Diagnostics separate local resource pending/absent/not_authorized/open_failed/
decode_failed, cue rejection/mute/late, queue submission, mixer rejection and
device availability. `submitted` is not a listening confirmation.

Project-owned tests exercise production OpenGL A/B with an opaque-white decal
on dark/light walls at direct/oblique camera angles: center changes, neutral
points **inside** the footprint remain baseline, and expiry returns to
baseline. A read-only installed-WAD control checks the actual decoded profile.
Host/module + real BSP sweep tests cover contact timing, duplicate suppression,
offscreen simulation, expiry, slot reuse, stale generation and reload
independence. Production local-audio/mixer tests isolate impact and shell PCM,
world-space panning and concurrent fire/impact/contact voices. The installed
WAVs are optional read-only controls, never test build dependencies.

This remains a concrete/static-surface local-compatible effect profile, not
stock-exact material classification, ricochet probability, moving-brush hit
decals, rigid-body brass or server damage authority. The short opt-in SDL
device-output test passes, but actual impact/shell audibility and the updated
visual appearance require a new user manual test.

E5.1 final offline handoff after the last production edit: Win32 VS2022
Debug `hlclient`/`hlclient_tests` built; focused world-impact/audio/OpenGL
run passed 819 assertions across 17 tests with one opt-in installed-resource
skip. The installed read-only `{SHOT1`/WAV control was also run separately
and passed. The separate 250 ms SDL playback-device startup/output test
passed 5 assertions without a game session; it is not user listening
confirmation. Win32 Release `hlclient`/`hlclient_tests` built; the single final
full offline suite passed 2,379 of 2,410 tests (31 capability/opt-in skips),
819,539 assertions. Core-only Release with
`HLCLIENT_BUILD_GAME_HALFLIFE=OFF` built and both API/architecture guard tests
passed. Normal and optional-50-HP launcher `-CheckOnly`, no-stock Fast fixture
and `git diff --check` passed. ASan runtime was not revalidated; its earlier
failure/hang remains unverified, not a passing sanitizer run. New live sessions,
stage/commit/push: zero. The unchanged launcher selects
`build/bin/Release/hlclient.exe`, SHA-256
`DD8A20EB46C2BA33D095B3A4848B379984E4CC76E8DB15EE26266AC31EEC1082`.
This E5.1 binary still has `manual=not_run`.

## E5.1 manual audio-isolation follow-up

New `attribution=user_report`: the user did not hear the selected E5 impact
sound while firing a Glock at a wall. This report does not identify the exact
binary or prove that the later E5.1 correction was heard or failed. To test
possible masking without changing gameplay or master volume, the normal
launcher now accepts optional `-MuteGlockFireSound` for the manual
keyboard-mouse scenario. It forwards the bounded switch through the managed
runner and native orchestrator to `hlclient.exe --mute-glock-fire-sound`.
HalfLifeClientModule suppresses only the local `LocalSoundKind::fire` cue for
`weapons/pl_gun3.wav`; it still accepts the shot and independently emits the
world-hit `player/pl_step1.wav` and later casing-contact
`player/pl_shell1.wav` cues. Other weapon, movement and server audio, HUD,
ammo, visuals, wire history and mixer volume are unchanged. The default is
off; the switch is rejected outside the HL manual live-visual profile, and
cannot enter the scripted damage-respawn scenario.

Offline evidence for this diagnostic: a production Host/HalfLife module test
generates one accepted shot, impact and contact, then confirms two independent
ready-resource mixer voices and nonzero mixed PCM with the fire cue absent.
CLI, no-stock PowerShell forwarding and native parser tests cover the opt-in
argument. This is an isolation aid, not proof of user-perceived impact audio;
the new Release manual outcome remains `not_run` until the user listens.

Follow-up offline handoff: Debug and Release client/test targets built; the
final Release suite passed 2,381/2,412 tests (31 capability/opt-in skips),
819,559 assertions. Focused Debug diagnostic passed 19 assertions in two
cases. Core-only Release API and architecture guard tests passed 2/2.
No-stock launcher fixture, native parser self-test, real `-CheckOnly` with
the flag and `git diff --check` passed. The Release executable SHA-256 is
`43DFE7AE509C6666353EA2F09CF4ADD09CD52738B5717C7635095CF11BEA4630`.
New game/HLDS/Steam/WFP sessions, stage/commit/push: zero.

## E6 input: E5.1 audio handoff reported by the user

New `attribution=user_report`: the user reports that `-MuteGlockFireSound`
actually mutes the local Glock shot, and that the Glock impact and shell
collision sounds are audible ("вимкнутий звук працює, всі звуки працюють").
This confirms those three audible outcomes only. It does not establish exact
attenuation, stock volume/pitch, all surface materials, decal transparency or
stress/replay behavior. Historical E5.1 `manual=not_run` entries above refer
to the state before that report and are not rewritten. The subsequent E6
binary is a new build with its own `manual=not_run` status.
