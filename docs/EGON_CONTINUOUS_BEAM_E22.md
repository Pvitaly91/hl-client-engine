# E22 — Egon continuous beam

## Observer beam illumination follow-up — 2026-10-16

The user requests visible scene illumination from other players' Egon beams.
The observer path already selected the approved main `xbeam1.spr` strand, but
its light multiplied the server ribbon's RGB and opacity. The pinned SDK
`b1b5cf5892918535619b2937bb927e46cb097ba1`, `dlls/egon.cpp`
`CEgon::UpdateEffect`, pulses brightness from 255 down to 75, with wide-beam
RGB around 30..55 / 30..60 / 64..144. At its dim phase this made scene light
roughly five to ten times weaker than the local beam. The previous observer
pixel fixture used local-like RGB/opacity and did not exercise that phase.

The game now supplies separate observer emission through the existing numeric
scene-light profile: radius 192, intensity 0.455 and RGB `{0.5,0.5,1}`. This
matches the current local beam's `0.65 * 0.7` emission. A neutral optional color
override and opacity-inheritance switch allow this without changing server
ribbon tint, transparency, shape or halo. Defaults retain the previous behavior
for alternate profiles. The override is finite and bounded per RGB component.
The exact approved `xbeam1.spr` main sine strand alone selects this policy;
noise/halo strands do not duplicate light. Zero-opacity, rejected/unavailable
resources, removal and reset still disable illumination on the existing frame
boundary. Existing light budgets, prioritization and unshadowed falloff apply.

The Release regression uses actual `entity_point + FSINE` beams attached to
player 2's validated `p_egon` socket, with receiving player 1. SDK dim
`{55,60,64},75` and bright `{30,30,144},255` presentations produce identical
scene lights and identical isolated world/Studio readbacks, while their ribbon
RGB, opacity, width and attachment stay server-derived. Each phase brightens
56,644 wall pixels and 19,602 Studio pixels with the ribbons removed before
readback. Zero-opacity and noise-only controls emit no light; removal and
out-of-radius controls match the dark baseline. The prior local fixture PPMs
remain byte-identical to the previous task's Release images.

VS2022 Win32 Debug normal-client build and Release `hlclient_tests`/normal
client build pass. The focused Release E22/last-weapon/game-API/module/custom-
beam run passes 199 cases and 66,622 assertions, with two explicit approved-
asset opt-in skips. Core-only Release `ALL_BUILD` plus all 44 CTests pass;
normal architecture guard, launcher `-CheckOnly` and null-renderer bootstrap
also pass. The full 3,733-test run recorded below belongs to the prior build
and was not repeated for this focused follow-up. ASan runtime remains
unverified and was not rerun. No staging, commit or push was performed.

Ready normal Release client SHA-256:
`F2D7A32A0B1E40C73019EDEF9E8C80A1E9E27A7A07A38BD6EB7761BA2F4F9CF7`.
The existing launcher executable is unchanged and observes the current client
identity on each connection attempt. Manual check: watch another player fire
Egon beside a dark wall/item, then stop; nearby objects should brighten during
the beam and return to their previous lighting after it disappears.

Source preservation and new verification belong to
`manual-artifacts/task-records/egon-observer-light-20261016/`.
New manual verification remains `not_run`; no live session is started by these
offline checks. Historical counts and executable identities below remain
evidence for their earlier builds.

## Beam scene illumination — 2026-10-16

The user requests illumination of surrounding objects from the accepted beam.
The spiral and its 2026-10-15 halo remain unchanged. Half-Life now selects a
separate neutral `BeamSceneLightProfile` for the main sine strand only; the
second textured strand and halo do not duplicate its light. Local firing
intents and approved committed observer `sprites/xbeam1.spr` beams select the
same policy. Alternate modules and other sprites default to no scene light.

The existing application materializer stages at most four point lights for
the entire frame. Radius is 192 units; intensity is 0.65 multiplied by current
beam opacity, with the current blue-violet beam RGB. Sampling uses nominal
192-unit spacing capped at four samples. Both ends are inset by at most a
quarter radius (48 units), so long beams still illuminate the muzzle/contact
neighborhoods. Sampling is symmetric because local and observed beam endpoint
order differs. Long middle sections are deliberately sparsely sampled; this
is bounded local-compatible tuning, not stock-exact dynamic-light parity.
With simultaneous emitters the four nearest camera samples are retained.

Only a successfully materialized, approved sprite frame can publish its staged
lights. Invalid, absent, expired or rejected beams, reset and empty subsequent
frames cannot retain them. A weak owning-frame identity gates the final
consuming append. That append runs after existing muzzle, explosion, tracer
and impact producers, never overwrites them, and fills only the remaining
space in the existing eight-light batch. At capacity some or all beam lights
are intentionally omitted. There is no extra timer, shadow/rendering system,
resource open, shader or texture upload, network entity, audio or prediction
change. World geometry, translated inline brushes and ordinary Studio models
use the already-shared effect-light shader uniforms. Camera-local viewmodels
retain their existing separate lighting coordinates.

These existing point lights are **unshadowed**. Their finite radius and falloff
limit illumination, but intervening walls do not cast dynamic shadows. The
accepted beam/halo/impact sprites still retain ordinary depth testing; this
does not constitute shadowing of nearby objects by the new point lights.

Project-owned `[e22-scene-light]` cases cover profile/NaN/bounds/defaults,
single-strand ownership, endpoint reversal, six simultaneous emitters/global
four-sample cap, duplicate geometry, pre-existing eight-slot priority, consumed
frame identity, missing/expired/rejected/reset cleanup. Actual OpenGL local
and observer A/B tests remove all beam sprites before readback and measure
nearby world and ordinary Studio pixels, with out-of-radius and stopped-frame
controls. A separate translated runtime-brush GPU test checks nearby increase,
distant unchanged pixels and light removal. Existing shape/halo tests remain
unchanged. Coordinated verification belongs to
`manual-artifacts/task-records/q-last-weapon-egon-light-20261016/`.
New manual status is `not_run` until user verification; no live session was
started for these offline checks.

Isolated Debug and Release `[e22-scene-light]` runs each passed four cases and
206 assertions. With the beam sprites removed from readback, both local and
observer paths brighten 56,644 owned-fixture world pixels and 19,602 Studio
pixels; far and stopped-frame controls remain unchanged. Corresponding saved
PPM files are byte-identical between configurations. The translated-brush GPU
case passes as well. These are controlled GPU fixtures, not live HL screenshots
or a claim of original-engine pixel parity. The final coordinated suite and
executable identity are recorded in the current Architecture follow-up.

Manual check: fire Egon near a dark wall and nearby item/player, then release;
the surroundings should brighten only while the beam is active. Repeat as an
observer. Preserve the previously accepted spiral and halo appearance.

## Additive beam glow — 2026-10-15

User requests a visible halo and explicitly confirms keeping the accepted
spiral shape. The existing core endpoints, sampled centerline, four turns,
phase, texture axes, widths and audio/lifecycle remain unchanged. The normal
sprite renderer draws one extra low-opacity additive layer from the exact same
segment points and approved `sprites/xbeam1.spr`, then the unchanged core.
Half-Life selects width multiplier4 and opacity0.35; these are local-compatible
visual tuning, not a claim of original engine bloom or stock-pixel parity.

Both receiving-player beams and committed observer beams use this profile.
The optional neutral module/host callback is selected only for the exact
approved sprite dependency. Other sprite/beam profiles and alternate modules
default to no halo. No new resource loader, texture, world-light, retained
effect, firing timer or network entity is created. Profile floats are finite
and bounded (width multiplier1..8, opacity0..1); unsupported profiles are
rejected. Visibility bounds include halo width. Map/session reset discards
cached numeric profiles, and beam removal/release removes glow in the same frame.
World depth testing remains enabled; no behind-wall glow is authorized.

Project-owned `[e22-glow]` tests cover local/observer spatial pixel A/B,
unchanged sampled spine, preserved core pixels, halo pixels outside the core,
unchanged upload count, deterministic repeat, opaque-world occlusion and
release/removal cleanup. The previous twist coverage assertions still execute
with the optional halo explicitly disabled to isolate their original core-shape
invariant. Invalid/NaN profile and non-Egon selection checks remain independent.
Manual visual verification of the new glow is pending; the historical accepted
beam shape and earlier manual records below are preserved.

## Server screen-effects follow-up — 2026-10-14

Crossfire user report: nuclear flash and shake absent. Approved BSP inspection
confirms `env_fade strike_flash` and `env_shake strike_shake`; these are server
messages, not client-inferred explosion timers. Previously ScreenShake decoded
but was never presented, and ScreenFade was ignored. Normal atomic module commit
now retains one bounded shake and one fade on the owning receipt clock. Duplicate
or stale events do not restart them; clockless events are not replayed later.
Map/session teardown clears them; death alone does not cancel a server flash.

Pinned SDK `engine/shake.h` / `dlls/util.cpp` define 4.12 timing/amplitude,
8.8 shake frequency, 10-byte RGBA fade and IN/OUT/MODULATE/STAYOUT semantics.
The HL profile supports flags 0..7, not the SDK's separately annotated CZero
LONGFADE extension. The independently implemented smooth multi-axis rumble is
local-compatible, not claimed stock-binary noise: amplitude falls quadratically,
translation stays within 16 units/axis and roll within 3.2 degrees; overlaps
retain stronger amplitude/later end without additive growth. Viewmodel stays
camera-local. The renderer receives only neutral translation/roll/RGBA/modulation,
uses a private camera, and composites after scene color/MSAA but before HUD.
Command aim, prediction, movement, audio listener, damage and server state do not
change. No new sprite/explosion asset or fabricated nuclear fireball is added.

`[screen-effects]` covers strict transactional messages, clocks, fades/hold,
replacement/dedup/reset, finite bounds, camera isolation and actual OpenGL
whiteout/modulation/HUD preservation/expiry. Historical deferred status below is
retained as history; this implementation still needs user visual confirmation.

## Endpoint world-depth follow-up — 2026-10-14

New `attribution=user_report`: Egon's wall-contact sprite is visible through
walls. The shared glow-sprite draw temporarily disabled world depth testing;
it now retains that test with unchanged additive blending/no depth writes.
Normal approved server endpoint materialization is covered by a project-owned
OpenGL visible/occluded/foreground A/B test. The accepted beam geometry,
continuous audio, firing eligibility and endpoint ownership are unchanged.
See E20's shared world-glow explanation and the E32 verification follow-up.
The newly built image remains `manual=not_run` until tested by the user.

## Additive manual acceptance — E23 request, 2026-10-06

The user explicitly reports “Протестовано” in the E23 request. Record
`manual=passed; attribution=user_report` for the E22 multiplayer manual scenario:
startup/run/off audio, continuous local and remote beams, endpoint/attachment
presentation, shared uranium, cleanup and normal client stability. No executable
hash was supplied for that observation. Historical `manual=not_run` reports and
their hashes below remain unchanged. The accepted current beam shape is retained.
ScreenShake visual presentation remains deferred; this acceptance does not claim
that deferred feature was implemented or tested. E23 has its own `manual=not_run`.

## Seam follow-up — 2026-10-06

New `attribution=user_report` screenshot locates the bright pinpixels at a
crate/wall surface join. The accepted beam shape stays unchanged. The separate
CPU BSP boundary-subdivision correction and its regression evidence are recorded
in [GoldSrc BSP geometry compatibility](GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.md).
The earlier diagnostic record below remains historical, not a claim that the
subsequent fix was already present or manually validated.

## Manual follow-up: accepted beam shape, separate image noise — 2026-10-06

New `attribution=user_report`: the beam looks better and its shape is now right;
leave that shape unchanged. This confirms the visual shape only, not stock-exact
parity or every E22 regression. Do not rewrite the historical `manual=not_run`
records below. The user did not supply a hash for this manual observation.

The same follow-up reports intermittent white pixels, described as white noise.
Clarification: present all the time, not tied to firing or another action.
No screenshot/video of this issue has yet been supplied. Root cause remains
unconfirmed; the renderer and accepted beam parameters are unchanged in this
diagnostic follow-up.

Read-only state audit: each sprite resets the beam UV flag; world rendering
binds its own shader, textures and VAO; the entity pass restores blending and
depth-write state. No demonstrated beam-to-world state leak was found.
An independent candidate is per-face removal of collinear/near-collinear BSP
boundary vertices (`goldsrc/bsp/goldsrc_face_geometry_builder.cpp`), potentially
undoing shared edge/T-junction stitching and exposing a bright sky background.
This behavior exists independently of the UV edit; it is not proof of the user's
artifact. Screenshot/video and its relation to polygon edges/camera movement
are required to distinguish cracks from texture shimmer or other image noise.

Evidence: `manual-artifacts/task-records/e22-white-pixels-investigation-20261006/`.
Existing Release offline `[renderer][opengl],[e22-beam-uv]` checks passed36 cases
with20,671 assertions. This did not reproduce the reported noise and does not
establish that a user's live frame is clean.
The previous Release is retained; no production edits, build, live session,
staging, commit or push in this diagnostic follow-up.

## Corrective: transverse sprite profile — 2026-10-06

New `attribution=user_report`: the strand forming the Egon spiral is too wide
and the result differs from original HL. The preceding visual follow-up is not
manually accepted. The user explicitly permitted looking at Xash for this
comparison; no implementation was copied into the project.

The pinned official SDK defines the two layers and their parameters, not the
private GoldSrc beam renderer. Read-only reference comparison with FWGS revision
`9137964147d8f749dbeddf1cb5482c3d16f86e4a`, `ref/gl/gl_beams.c:282–331`, confirms
texture U across ribbon width and V advancing along its length. Its width and
camera-relative sine contracts differ from ours; those were not blindly ported.
The existing four-turn, beam-axis helix remains local-compatible, not a claim of
stock-pixel parity.

Root cause: the production sprite fragment shader assigned ribbon longitudinal
X to texture U. This rotated the original texture's narrow transverse glow
profile into the length direction, rendering a broad short ribbon instead of a
thin strand. The beam-only branch now samples `(across, repeated/scrolled along)`.
Ordinary sprite UVs, filtering, blend state, geometric width, endpoints, helix
parameters, weapon/audio lifecycle and authority are unchanged. Both local and
committed observer ribbons use this same production correction.

Proof: an independent owned 8x8 profile is constant along V, with a two-column
bright U center. The new GPU test failed on the old shader: longitudinal test
columns80/160/240 had bright widths0/18/0 pixels. It passes after correction with
3–8-pixel bounds at all three positions, including nonzero longitudinal scroll
that must not move the transverse profile. Cleanup restores the blank scene.
This invariant-V fixture proves profile preservation, not visible texture motion.

The separate approved read-only `sprites/xbeam1.spr` control uses the normal
approved importer/materializer and OpenGL renderer. Its decoded size is16x64;
mean blue intensities at U columns0/4/8/12/15 are0/2/241/2/0. On the same
8-world-unit-wide GPU control, bright widths are5/5/6 pixels across three
longitudinal positions. The rendered spiral now has thin, continuous luminous
strands. The old optional wrap-area budget (>50 outside pixels) depended on the
incorrect broad fill; it is replaced by visibility on both sides of the core
plus strict transverse-width checks at three longitudinal positions, not a lowered
opaque-area target. Existing owned helix visibility assertions are unchanged.

Evidence/snapshot: `manual-artifacts/task-records/e22-beam-texture-axes-20261006/`;
1249 ordinary source files copied/hash-verified before edits. Baseline Release
E22:18 passed,1 approved-root skip,6758 assertions. Generated before/after owned
profile and approved original-resource GPU images remain diagnostic evidence,
not a new asset dependency. Installed files are read-only; no game asset added.

Final verification after the last production edit:
- VS2022 Win32 Debug and Release built successfully. Focused Egon/helix/UV
  checks passed23/23 with13,107 assertions in each, including actual OpenGL and
  optional approved original-resource controls. Debug core/API passed244 cases
  with125,333 assertions.
- Final complete audited Release CTest:3015 registered checks,2968 passed,
  47 explicit capability/private-resource skips,0 failures (491.57s).
  Following Null/process-level, transport, native/PowerShell summary, no-stock
  launcher fixtures and source-only CheckOnly all passed; acceptance script exit0.
- Existing `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` Debug build/tests passed41/41,
  including the architecture guard (32.64s).
- Actual unchanged reference/peer/NoTimeLimit launcher `-CheckOnly` passed:
  process launches0. `git diff --check` passed; all1249 snapshot source files
  remain present; only these four scoped source/test/doc files changed.
- ASan runtime was not rerun; its earlier unverified loader status remains
  unchanged. Offline GPU output does not replace user validation in a session.

Normal Release `build/bin/Release/hlclient.exe`:3,816,448 bytes;SHA-256:
`3999EF656FB054467EC2B8F3962F1BAD15F17834207056EC85DC7DFCAF947E9C`.
Launcher SHA-256 remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
Next manual check: compare the strand's thickness over its full length from
both clients, hold/release/fire again, and check that viewmodel and sound stay
present. The existing reference/peer/NoTimeLimit command below is unchanged.

New build manual=`not_run`; new live/Steam/HLDS/WFP/capture launches=0;
staging/commit/push=none. Historical records below remain unchanged.

## Visual follow-up: rotating sine beam — 2026-10-06

User requested the original Half-Life-style twisted Egon beam. This is a visual
follow-up; it does not mark the previous corrective build manually accepted.
The old `sine_noise` branch only changed frequency between two unrelated sine
waves. Its small displacement was largely covered by the wider inner ribbon.

Pinned SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`,
`cl_dll/ev_hldm.cpp:1483–1488` and `dlls/egon.cpp:433–471`, confirms two
`sprites/xbeam1.spr` layers with FBEAM_SINENOISE/BEAM_FSINE on the main layer.
That public reference does not contain the private engine's tessellation.
The existing neutral sine profile now uses a coherent four-turn spatial helix
with quadrature sine/cosine, rotating at the existing absolute phase rate of
9 radians/second. Radius is `min(3 * amplitude, beam_length / 8)` with a
`sin(pi*t)` envelope and exact fixed endpoints. These radius/turn choices are
local-compatible visual tuning, not stock-pixel-exact constants.

The main layer uses 64 bounded segments; the inner noisy layer preserves its
old 32 sampled segments, color, width and scroll. Both the receiving-player
module path and committed observer beam entities pass through this same neutral
renderer mechanism. Culling bounds use the shared conservative radius helper.
The near-wall cap keeps a short trace from becoming a compressed wide coil.
Original sprite resources, server endpoints, muzzle attachments, firing/audio
lifecycle, viewmodel fix, ammo and movement authority stay on their existing paths.

Changed production paths: `entity_render/beam_ribbon.hpp`, existing OpenGL sprite
ribbon draw and `RuntimeReplayLocalAssets::present_entity_effects` bounds. There
is no new game dependency in lower targets and no new launch option.
Independent CPU regressions measure winding, radius, finite values, short traces,
vertical/diagonal axes, deterministic rotation and old nonsine coordinates.
Production OpenGL A/B for local and server composition verifies visible pixels
outside the unchanged core, time-varying shape, repeated-frame identity, bounded
materialization and removal. Optional read-only original `xbeam1.spr` has a
separate GPU A/B control; generated fixture images are diagnostic artifacts.

Pre-edit source snapshot:1249 verified ordinary files. Baseline E22:18 cases,
17 passed,1 explicit resource skip;1999 assertions. Evidence is retained in
`manual-artifacts/task-records/e22-twisted-beam-20261006/`.
Final verification after the last production edit:
- VS2022 Win32 Debug and Release built successfully. Focused Egon/helix tests
  passed22/22 with13,069 assertions in both, including the optional approved
  original-resource GPU control. Debug core/API passed244 cases/125,333 assertions.
- Complete final audited Release suite passed3014 checks:2967 passed,47 explicit
  capability/private-resource skips,0 failures (533.40s). Null/process-level,
  summary/transport/no-stock launcher fixtures and source-only CheckOnly passed.
- Existing core-only build with `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` passed41/41,
  including the architecture guard and new pure geometry cases.
- Actual unchanged reference/peer/NoTimeLimit launcher `-CheckOnly` passed with
  process launches=0. `git diff --check` passed; all1249 pre-edit source files
  remain present,only seven scoped source/test/doc files changed,index untouched.
- ASan was not re-run for this visual follow-up; its previously unverified
  runtime status (loader0xC0000135) remains unchanged.

Normal Release `build/bin/Release/hlclient.exe`:3,816,448 bytes;SHA-256:
`12AE6BE1280C3D45AA030B440ADA7401BDF12B052E0AB3636D6FD37DBD0E7C14`.
The unchanged launcher selects this executable. Next manual test should hold
Egon fire,repeat release/fire,look up/down and check from the observing client;
verify a rotating wrap around the core and stable muzzle/contact endpoints.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

New build manual=`not_run`; new live/Steam/HLDS/WFP/capture launches=0;
staging/commit/push=none. Historical E22 records below are preserved.

## Corrective: one-frame looping fire pose — 2026-10-06

New `attribution=user_report`: first Egon shot shows a beam, later firing loses
both beam and viewmodel. This is a failed manual follow-up, not full E22 acceptance.
Run `8c132b0fa7ae48378a56ceac368321ee` used the normal Release/crossfire/unlimited
two-client path: server28384, client31032, peer24472; both clients exited0,
owned cleanup=`exact`, scoped restoration=`scoped_exact`, cleanup errors=0.
Its shooter log identifies Egon ID10/model121 with remaining charges7→6→5 while
`viewmodel_ready=0;viewmodel_status=unsupported_pose`. It has no decoder failure
like the older peer run; those are separate observations.

Actual imported `fire1/fire2` are looping sequences with exactly one frame.
The shared Studio normalizer used `fmod(source_frame, max(last_frame,1))`, which
produced positive fractional coordinates despite the sole valid pose being0.
The subsequent range check rejected those poses. Switching deterministic firing
variants from multi-frame FIRE3/4 to FIRE1/2 therefore removed the actual
first-person frame and its muzzle attachment; the predicted beam correctly
refused to invent a muzzle without that attachment.

The generic evaluator now maps every finite time of a one-frame sequence to
frame0/frame0 with fraction0. Zero-frame and nonfinite input rejection, and
multi-frame wrapping/interpolation, remain unchanged. There is no Egon special
case in the engine, no bypass of material validation and no replacement of the
missing muzzle with a camera/feet fallback.

Both regressions fail on old code: the pure looping pose at frame0.001, and the
repeated-shot normal materializer with `unsupported_pose`. Required independent
fixtures now contain actual one-frame FIRE1/2 roles; six repeated firing episodes
exercise all four variants, viewmodel materialization, muzzle and beam continuity.
Production OpenGL draws each variant; approved original v_egon read-only controls
materialize all four at four nonintegral ages. No installed asset was changed.

Pre-edit1249-file verified snapshot and E22-only baseline17 cases (16 passed,one
explicit resource skip),1848 assertions are retained in
`manual-artifacts/task-records/e22-single-frame-fire-fix-20261006/`.
The requested additional `[studio-pose]` filter matched no cases; it is not
claimed as pre-edit Studio coverage.

Final verification after the last production edit:
- Debug focused E22/E21/E20/pose/viewmodel:102 cases,99 passed,3 explicit private
  resource skips;29,451 assertions passed. Debug core/API:241 cases and119,112
  assertions passed.
- Release approved original Egon controls:18 cases and2075 assertions passed,
  including all FIRE1–4 materializations and the normal GPU path.
- Complete audited Release offline suite:3010 checks,2963 passed,47 explicit
  capability/private-resource skips,0 failures; plus Null/process-level,
  transport/summary/no-stock launcher fixtures and source-only CheckOnly passed.
- Core-only `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`:41/41, including the same-host
  alternate module and architecture guard. Actual reference/peer/NoTimeLimit
  launcher `-CheckOnly` passed with process launches=0.
- Supported ASan Debug compilation passed, but both core/game executables fail
  before runtime with loader `0xC0000135`; ASan runtime is not verified. No
  sanitizer-infrastructure repair or DLL copying was performed.
- `git diff --check` passed;all1249 pre-edit source files remain present. Only
  the evaluator,its two regression files and this document changed in this
  corrective. The index,branch/HEAD and launcher hash remain unchanged.

Prepared normal `build/bin/Release/hlclient.exe` (3,815,424 bytes),SHA-256:
`48EEB9A1079BCAA7BA90C05B1D0A3B7859EBFCA808C68FBD2C28958A0D844839`.
The unchanged launcher selects this executable; no debug/demo flag is required.
Next user test: pick up Egon,repeat press/hold/release firing enough times to
exercise FIRE1–4; verify the viewmodel and beam stay present,then switch weapons.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

New-build manual=`not_run`;new live/Steam/HLDS/WFP/capture launches=0;
staging/commit/push=none. Historical builds/evidence below remain unchanged.

## Corrective: queued command/presentation clocks — 2026-10-06

New manual evidence is `manual=failed; attribution=user_report`: server charges
decrease while Egon sound/beam/effects are not observed. This does not erase the
earlier offline gate or promote the corrective build to manual acceptance.

The latest matching crossfire/two-client/unlimited run is
`3edd5f6421e34d88995d693514b175ee` (server PID2204, client42720, peer25208).
Primary completed with0; peer ended with2, `runtime_record_failed`,
`decoder_failed/schema_mismatch`, opcode40, record1856, sequence1870, bit cursor1428.
The peer's entity/schema-boundary fields were beyond the retained16384-byte
diagnostic window; the exact decoder cause is **not established** from this
report. No schema guard is relaxed on that guess. Typed failure fields now
precede optional counters in the normal outcome line, so the next failure can
retain that boundary. Cleanup=`exact`, scoped restoration=`scoped_exact`,
cleanup errors=0. The report did not record a client executable SHA; do not
assign the current executable hash retroactively to that run.

The missing local effects have a concrete reproduced cause: the normal app
observes at render/update wall time before draining due20ms commands. Egon
incorrectly rejected `command.sample_end_seconds < now_`, even though `now_`
had advanced via observe/sample. Thus valid attack bytes reached the server,
while the module never started its local episode. The old code fails the new
same-host regression at `observe(.037) -> command(.020)` with no action identity.

The corrective separates monotonic command sequence/time ordering from the
non-rewinding presentation clock. Zero/elapsed *present* server timers mean
ready, not a new render-frame deadline; positive and missing timers keep their
existing gates. Start/stop/action/audio times retain the immutable command's
time. Duplicate notification, actually regressing command time, water/ammo/
ownership, server authority and wire/history/prediction remain unchanged.

The regression covers reference movement-effect notification and the off/submit
path, held/duplicate/release ordering, positive cooldown/missing fields, approved
owned startup/run/off PCM and the normal GPU beam path with a render clock ahead
of command time. Pre-edit1249-file snapshot and baseline15 cases (14 pass, one
explicit resource skip),1813 assertions are retained in
`manual-artifacts/task-records/e22-effects-clock-fix-20261006/`.
Corrective verification: old-code regression reproduced failure; updated Debug
scope135 cases (127 pass,8 explicit skips),33,308 assertions passed;core/API240
cases/118,980 assertions passed. Release E22 plus approved original-resource
controls:17 cases/1876 assertions passed, including the clock-ahead PCM/GPU path.
The complete final Release audited suite passed3008 checks (2961 pass,47 explicit
skips),0 failures, plus Null/process-level/summary/no-stock runner checks.
Core-only OFF and its architecture guard passed41/41. Actual reference/peer/
NoTimeLimit `-CheckOnly` passed withprocess launches=0. `git diff --check` passed;
all1249 pre-edit source files remain present,only the four scoped code/test files
and this document changed,index untouched. No historical logs were rewritten.

Focused supported ASan Debug compilation passed. The executable still cannot
start its runtime (loader `0xC0000135`); sanitizer runtime is not verified and
its infrastructure was not changed.

Corrective Release `build/bin/Release/hlclient.exe`:3,815,424 bytes; SHA-256:
`1017103BC5647654A170AA7729BD6E8CA914A375091276FFE66F53826F1F4402`.
The launcher remains byte-identical with SHA
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
Use the unchanged manual command below. These corrective values supersede the
older E22 build's bytes/hash for the next launch,not its historical evidence.
New live/Steam/HLDS/WFP/capture launches=0;staging/commit/push=none.
Corrective build `manual=not_run`.

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`; branch
`codex/stock-runtime-campaign-5e48b7c1`; HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213` remains unchanged.
Primary result: `egon_continuous_beam_integrated`, offline verified. This is the
normal static-library `hlclient.exe`, not a demo or plugin. All E22 manual
fields remain `not_run`. New live/stock/HLDS/Steam/WFP/capture launches=0;
staging/commit/push=none. No installation/configuration files were changed.

## Prior evidence and preservation

E21 is newly accepted as `manual=passed; attribution=user_report`, from the
user's explicit “все протестовано” in the E22 request. This is the reported
normal multiplayer primary/charge/spin/audio/beam/remote/cleanup/stability
scope, not a new automated live observation. Unreported water/overcharge
details are not promoted. Historical E21 `manual=not_run` reports and hashes
are preserved; this acceptance does not identify an earlier executable SHA.

Before E22 edits, 1,244 regular project source files were copied and SHA-256
verified by the existing mechanism into
`manual-artifacts/task-records/e22-egon-20261006/source/` with its manifest.
Release baseline: 86 cases, 84 passed, two explicit private-resource skips;
27,647 assertions passed. This is a baseline, not the final E22 suite.

Before E22, Egon had no concrete game presentation controller, custom entities
had wire schema dispatch but no beam visual projection, persistent audio had
no finite-startup/replace operation, and ScreenShake had no strict HL decoder.

## Pinned reference and actual resources

Reference: official Half-Life SDK revision
`b1b5cf5892918535619b2937bb927e46cb097ba1`, read locally without updating or
copying implementations. `dlls/egon.cpp`, `dlls/effects.cpp/.h`,
`dlls/client.cpp`, `network/delta.lst`, `engine/customentity.h`,
`engine/shake.h`, and `cl_dll/ev_hldm.cpp` establish the roles below.
No ReHLDS/Xash/proprietary dump source is used.

Identity is ID10, exact `weapon_egon`, `models/v_egon.mdl`,
`models/p_egon.mdl`, `models/w_egon.mdl`; shared server uranium reserve,
reference maximum100/default20, no clip and no reload. The runtime reads the
advertised WeaponList ammo index, not a guessed numeric uranium slot, and
never reserves/debits ammo locally. HUD omits CLIP/-1 for Egon/Gauss.
Primary selects WIDE; stock CEgon has no accessible SecondaryAttack override.
RMB is not a new narrow/zoom/charge mode. Wire buttons/history are unchanged.
Internal server NARROW event parameters can be decoded safely; this is not
claimed as a stock player input mode.

Approved read-only original-model control established these v-model labels:

| Role/index | Imported label | Frames/FPS | Loop |
| --- | --- | --- | --- |
| 0 | idle1 | 61/30 | yes |
| 1 | fidget1 | 81/30 | no |
| 2 | altfireon | 46/30 | no |
| 3 | altfirecycle | 16/30 | yes |
| 4 | altfireoff | 21/30 | no |
| 5/6 | fire1/fire2 | 1/30 | yes |
| 7/8 | fire3/fire4 | 16/30 | yes |
| 9/10 | draw/holster | 16/30 | no |

v_egon:32 bones, one attachment (bone11 `Gauss`, local(-10,2.5,0)),
one skin family; source-vertex bounds(-13.73,-2.8,-6.19)→(11.68,5.24,9.78).
p_egon:10 named Biped bones ending at `Bip01 R Arm2`, no attachments,
one skin family; source-vertex bounds(-12.03,-14.99,-12.22)→(29.07,5.89,10.08).
The verified p-model fallback derives a reference-front centroid once, then
the engine evaluates it in the current merged bone9 pose. It is explicitly
local-compatible, not a fabricated stock attachment or a Glock socket.

## Game/API/engine ownership and normal path

`hlclient_game_halflife`: `games/halflife/egon.hpp/.cpp`, integration in
`client_module.cpp` and `presentation.cpp`, no-clip HUD in `presentation.cpp`,
strict ScreenShake in `server_messages.cpp`. It owns ID/name/sequence/sample
policy, firing lifecycle, eligibility, confirmation and cleanup. Pure shared
`studio_pose` is a private dependency used only for immutable socket planning.

`GameClientAPI`: existing committed-event/command/pose seams and owning
`BeamEffectsFrame` outputs; persistent/noise/scroll numeric fields are neutral.
`LocalVoiceControl` adds finite `repeat_sample=false` and `replace` on an
existing voice owner. No engine/session pointer, SDK layout, OS handle, asset
root, raw send or second application loop crosses the seam.

Engine: generic schema projection in `runtime_replay_session.cpp`, checked
mixed intra bases in `packet_entity_decoder.cpp`, ordinary sprite scale/frame,
approved assets/current-pose resolution in `runtime_replay_local_assets.cpp`,
existing LocalAudio/mixer/output in `local_audio.cpp`, bounded neutral ribbon
sampling in `entity_render/beam_ribbon.hpp`, execution in the existing
`opengl_entity_renderer.cpp`. `apps/hlclient/main.cpp` continues composing the
same scene and shared audio output. Lower targets have no concrete HL linkage.

Normal flow is accepted command → Egon → presentation/viewmodel/beam/audio
intents → existing approved application composition → existing renderer/mixer.
The actual viewmodel guard accepts Egon, including legitimate one-frame fire
variants; snapshot-only success is not used as application integration proof.
The module appends into the existing effect frame to avoid another large
Win32 stack copy. Gauss finite world effects survive viewmodel switching.

## Firing and audio

One accepted primary episode chooses a deterministic FIRE1..4 variant and
body1, with stable restart identity during rendering. Imported looping metadata
is used; no system RNG/per-frame random restart. Draw, idle, and fresh service
holster/animation inputs are separate. Stop returns idle; no ALTFIRE input mode
or fake reload is introduced.

Exact assigned names `events/egon_fire.sc` and `events/egon_stop.sc` bind the
session's numeric slots. Conflicting assignments fail closed. Events are owned
and delivered only after the entire RX record commits. Dedup uses record
ordinal/cursor/entry, with a bounded128 ring and stale floor, not render calls.
The optional negotiated signed integer `weapon_data_t.iuser3` is projected
generically; HL interprets OFF/CHARGE only. Missing fields are not guessed.

Local startup is predicted after validated history insertion. A fresh server
state/event or received beam pair confirms it; an unconfirmed episode times
out at0.75s and requires release before retry (local-compatible rejection
policy). Local run's logical transition is start+2s; remote run is established
by the committed startup=false event, not a remote timer. Short tap cancels the
pending transition. EndAttack uses a0.5s compatible cooldown plus observed
server attack timers. No damage/ammo simulation accompanies these timers.

Original virtual samples are `weapons/egon_windup2.wav`,
`weapons/egon_run3.wav`, `weapons/egon_off1.wav`, through the pinned HL client
effect approved loader, with exactly one `sound/` namespace expansion. They
are warmed through the existing startup path without requiring a server sound
slot. The older movement preload ordering remains first; its exact warm-up
count becomes6+3, with explicit Egon membership assertions.

WIDE startup/run use gain0.98/pitch1.25; off gain0.98/pitch1.0. Startup is finite;
run replaces the same owner/cursor and loops. Independent emitters have distinct
IDs; spatial sources follow the current merged pose. Stop controls execute even
when muted or a sample is missing. A local stop plus reliable echo does not play
two off cues. Focus mute stops output but retains only current continuous intent;
unmute can resume run without replaying startup. Stop while muted prevents
resurrection. Session reset clears that exact audio domain, not other voices.
Resource pending/absent/authorization/decode/output failures retain existing
typed diagnostics and250ms finite-request grace; a request is not claimed played.

## Custom entities, baselines and current-state beams

The wire custom bit selects the exact negotiated `custom_entity_state_t`.
Player/ordinary/custom fields remain separate. Descriptor-checked projection
decodes POINTS0, ENTPOINT1, ENTS2 and HOSE3. Custom `angles[]` are signed float
world coordinates, not Euler angles. `sequence`/`skin` split into low12 entity
and high4 attachment bits. Model index, width `scale`, noise `body`, RGB,
renderamt, frame and animtime/scroll are projected with their own wire meanings.
Postmultiply0.1 is already applied by the delta codec for width/scroll.
No SDK offsets, ABI memcpy or opcode scan is introduced.

Null, exact entity/instanced baselines and the existing history are retained.
Mixed intra-message public beam/normal bases remap only compatible named,
same-offset/storage scalar members into the target schema; unadvertised fields
keep target protocol-null values. Invalid descriptors still reject atomically.
The captured storage-size marker remains one; it is not an SDK `sizeof`.
Signed SHORT skin converts to the custom packed16 domain. If a custom inherited
integer is outside a narrower ordinary wire range, a bounded target-delta mask
must prove that the member is replaced before its temporary target default can
be used. Omitted unrepresentable members reject; old validation is not weakened.
The old malformed custom-category fixture remains rejected. Existing delta
category-change safeguards remain; there is no silent resync. Stable historical
A–H canonical hashes are not rewritten for the later beam/scale/iuser3 fields.

Server main and noise are independent current snapshot entities, not timed
client effects. ENTPOINT uses decoded world contact and owner attachment1.
Resolution priority: current evaluated owner attachment → matching live merged
p-model socket → explicit unavailable; no feet/camera/unrelated-weapon fallback.
Receiving-player override is used only for the active selected module's server
handoff and validated current viewmodel muzzle. Lifetime/current model/generation
checks reject stale remote sockets. POINTS/ENTS execute; HOSE and flags
SOLID/SHADEIN/SHADEOUT are explicitly unsupported in this minimal renderer.
SINE is decoded and executed; unknown profiles are not reinterpreted as Studio.

Server sprite comes from modelindex, not a hardcoded slot. Expected SDK asset is
`sprites/xbeam1.spr`. Width/color/opacity/frame follow decoded state. Noise uses
body×0.1 world units and scroll uses decoded rate×0.01 cycles/s: documented
local-compatible visual conversion, not stock engine raster/units parity.
Absolute-time sine/noise samples and repeat UV scrolling use the same texture
ribbon mechanism, depth testing and normal state restoration. There are at most
32 segments per noisy ribbon,64 server beams and128 module effects per frame;
resource/voice/history/cache bounds stay finite. Local persistent renderer IDs
are presentation-only logical slots; no fake network entity is inserted.

Local prediction supplies two current persistent xbeam ribbons from the current
validated muzzle, not camera origin, with a2048-unit static BSP eye trace refreshed
from current aim each frame. Widths3.5/5, opacity0.7, compatible blue/noise/scroll
tuning are game-owned. Miss can draw to range; invalid/startsolid/unsupported
queries skip. No bullet decals, shells, firearm flash or repeated hit sounds.
Receiving a supported same-owner pair suppresses the predicted pair and hands
the endpoint to server state, avoiding duplicate beams. Server-filtered profile
keeps prediction. SDK FL_SKIPLOCALHOST/FEV_NOTHOST behavior is reference evidence;
actual live profile is **not observed by this task**, which launches no session.

## Endpoint, shake, lifecycle and authority

`sprites/XSpark1.spr` is the ordinary authoritative endpoint sprite, not a third
predicted beam. Existing glow/nodissipation execution now supports ordinary
multi-frame sprites with server frame/scale/opacity/origin. NODRAW/removal and
server expansion remain authoritative; no local timeout respawns it. The SDK
WIDE multiplayer path may hide it on non-damageable surfaces. No local duplicate
flare or speculative owner association is added when owner data is unavailable.

ScreenShake requires exactly6 bytes: amplitude/duration u16÷4096, frequency
u16÷256. Own bounded output retains provenance; malformed suffix rejects the
whole staged record. Visual camera shake is explicitly deferred. UserCmd aim,
prediction and gameplay angles are never altered to fake shake.

Release, ammo0, water3, holster, death, disconnected/hidden/reused remote slot,
map/session reset and teardown cancel the appropriate continuous owner. Late
fire after a known missing/reused slot is rejected. Retained records do not
replay events. Network generation, life epoch, action/restart/resource identities
remain distinct. E13 corpse/respawn and E14 death/score/HUD authority are unchanged;
remote body sequences remain server/E10 owned. No health/armor/ammo/damage/score
write or local player trace authority was introduced.

## Verification and handoff

Final gates below were run after the last production decoder change. Required
own fixtures cover mixed
schemas and malformed suffix, normal viewmodel, primary-only/no clip,30s hold at
30/60/144/240 FPS, startup/run/off isolated PCM, independent emitters/stop dedup,
focus/cleanup, server merged socket movement/removal/handoff, noise determinism,
normal OpenGL beam pixels/cleanup and endpoint frame/scale/NODRAW/removal.
Original resources are optional read-only controls, never build dependencies.
Synthetic PCM and GPU pixels are not human listening or stock live proof.

The unchanged manual command for the prepared Release is:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Pick up Egon normally, check shared uranium/no clip, short tap and long hold,
aim while moving/pitching, observe the other client, release/switch/ammo depletion,
kill/respawn and TAB. Close both clients yourself and await managed cleanup.
`egon_primary/startup_audio/run_audio/stop_audio/local_beam/remote_beam/attachment/
endpoint_flare/ammo/shared_uranium/cleanup/kill_respawn/water_manual=not_run`;
`screen_shake_manual=not_run`; overall E22 `manual=not_run`.

Known limits: static-only predicted trace, compatible p-socket/noise/scroll,
unsupported HOSE/extra beam flags, visual ScreenShake deferred, actual skip-local
profile unobserved, manual output/long-run acceptance pending. No universal mod,
stock-binary parity, Hornet Gun or E23 support is claimed.

### Final gates — 2026-10-06

| Gate | Actual result |
| --- | --- |
| VS2022/Win32 Debug build and affected scope | passed;229 cases,221 passed,8 explicit skips;39,687 assertions passed |
| Debug core/API executable | passed;240 cases,118,980 assertions |
| VS2022/Win32 Release build, normal client/helpers | passed |
| Final Release audited offline suite | passed;3006 registered checks,2959 passed,47 explicit skips,0 failures;436.96s |
| Cumulative reported Catch assertions in final CTest log | 1,120,424 passed, including assertions preceding explicit skips; not unique source assertions |
| Release E22 approved original-resource control | passed;15 cases,1841 assertions, no skips;actual models/xbeam/WAV plus independent required fixtures |
| Core-only `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` | passed;41 CTest checks, including same host/API alternate module and architecture guard |
| Actual core-only source/link lists | no `hlclient_game_halflife` or concrete game source in generated vcxproj/props; no installed game used |
| Architecture negative graph guard | passed, including alias/interface/monolithic dependency detection |
| OpenGL/Null | required production beam/endpoint GPU pixels, pose updates and cleanup passed;audited Null/process-level checks passed |
| Audio | isolated owned startup/run/off PCM passed;approved original WAV decode passed;existing short SDL playback output test passed (5 assertions), not human listening |
| Native/PowerShell/no-stock | transport self-test, functional-log observation, G/H roundtrips and full no-stock manual runner fixture passed |
| Actual launcher | `-Prediction reference -RemoteAudioPeer -NoTimeLimit -CheckOnly` passed, process launches=0;reference/off/source-only/no-stock branches also passed |
| Preservation |1244 initial source files retained,missing=0;26 pre-existing files changed only in E22 scope,5 new ordinary source/header/test/doc files |
| Git hygiene | branch/HEAD/SDK unchanged,index untouched,`git diff --check` passed |

The47 skips are explicit platform/privilege/device-interaction/private-resource
capability opt-ins, not successful live evidence. The ordinary OpenGL beam,
endpoint and isolated PCM tests were not skipped. The SDL output test opened
the existing playback backend without starting a game or recording/capture.
The first pre-final full-suite attempt was interrupted when the scalar bridge
was refined; its partial output is not the final gate. Only
`release-offline-last.log` is the complete post-final-production result.

Supported ASan Debug build passed for core/API, game tests and the normal app.
Actual attempts of core E22, game E22 and app `--help` all failed before output
with `0xC0000135` (loader dependency unavailable). The required
`clang_rt.asan_dbg_dynamic-i386.dll` is absent from the binary directory/PATH.
ASan runtime is **not verified**, not a passed sanitizer test. No runtime-copy,
PATH/infrastructure repair or historical-code1 reinterpretation was performed.

Normal Release: `build/bin/Release/hlclient.exe`,3,815,936 bytes; SHA-256:

`00038C8859D7462BE94D9485B2C0EAD914FCEE5080FBE38ABF23B2F51A9EAE60`

Unchanged launcher SHA-256:
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
Its existing selection resolves this worktree's normal Release executable.

Reproducible build/gates (no submodule update or live sessions):

```powershell
cmake --build build --config Debug --target hlclient_tests hlclient_core_api_tests --parallel 2
build\bin\Debug\hlclient_tests.exe "[e22],[e21],[e20],[audio],[weapon-presentation],[lifecycle],[packet-entities]" --reporter compact
cmake --build build --config Release --parallel 2
pwsh -NoProfile -File scripts\build_source.ps1 -Configuration Release -Parallel 1 -TestOnly
cmake -S . -B build-core-g1 -G "Visual Studio 17 2022" -A Win32 -DHLCLIENT_BUILD_GAME_HALFLIFE=OFF
cmake --build build-core-g1 --config Release --parallel 1
ctest --test-dir build-core-g1 -C Release --output-on-failure --parallel 1
```

Per-request evidence is in `manual-artifacts/task-records/e22-egon-20261006/`;
none of the required implementation is stored there. Source/header/test/CMake/doc
files are ordinary project files, suitable for a future Git checkpoint.
E22 overall and every granular manual field remain `not_run`. New live sessions,
stock client/HLDS/Steam/WFP/campaign/capture launches=0;staging/commit/push=none.
