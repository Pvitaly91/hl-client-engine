# E21 — Gauss primary, charge and beam

## Rapid secondary restart follow-up — 2026-10-15

New `attribution=user_report`: rapidly repeating secondary start/release can
leave the next actual Gauss shot without its local presentation. No executable
SHA or exact command stream was provided for that report; none is assigned
retrospectively. This is separate from the preceding door-impact correction.

The reproduced timing defect is in `GaussPresentation::fire`: all secondary
discharges previously received a one-second attack cooldown. The unchanged
pinned official SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`,
`dlls/gauss.cpp:574–580`, gives manual `WeaponIdle` release only a two-second
idle-animation deadline. `SecondaryAttack` exhaustion at lines253–258 sets
both a one-second idle and player attack gate. A new attack may therefore be
valid immediately after manual release; the former client gate skipped that
charge. Local fire echoes deliberately confirm rather than create actions,
so they could not recover its missing sound, beam, animation or saved impulse.

The existing controller now distinguishes primary, released and exhausted
discharges. Manual release has no invented attack cooldown; primary retains
0.2 seconds and exhaustion retains one second. The release idle deadline stays
two seconds; exhaustion uses the SDK one-second idle. Fresh server-advertised
player/weapon attack timers still gate new commands. Charge spin-up, MP/SP
magnitude, uranium reservations, server HUD/ammo authority and the existing
once-only post-movement impulse path are unchanged.

The prior bounded one-second completed-secondary echo suppression is now an
independent receipt, not a firing gate or a condition on the latest animation.
Within that same window, a delayed old positive phase/full/burn timer cannot
reopen a fired action, age a newer command-owned charge, or replace a newer
primary. A suppressed positive row is not evidence for cancelling that new
charge on a following old phase-zero row. New input remains immediately usable.
The receipt expires and clears at model, selection, life and session boundaries;
server-adopted charging outside the window remains supported. This retains the
existing bounded local-compatible policy, not an exact server-command timestamp
inferred from receipt time or a comparison of unrelated transport/command IDs.

Delayed local reliable stop echoes previously targeted the currently active
voice, even when it belonged to a later charge. Local command/observation
ownership now remains responsible for terminating its exact voice; these local
stop echoes do not target another lifetime. A local fire echo during a new spin
also cannot confirm that spin or reconfirm the prior action. Remote stop/fire,
event deduplication, trace/beam profiles, playback queues and lifecycle
cancellation are unchanged. No server, renderer, shared movement or wire code
is modified for this correction.

The initial three `[e21-repeat-secondary]` cases were built and run against
the unchanged Gauss implementation: all three failed (8 failed assertions,
173 total), reproducing the missing next spin/audio, immediate primary gate
and stale stop/new-voice mismatch. Further cases cover delayed phase2 then
phase0 after restart, primary replacement, receipt expiry/lifecycle, preserved
server attack readiness and local-fire confirmation during a new spin. Five
rapid short-charge cycles run in MP/SP with fresh and retained ammo, asserting
one action/voice/beam/impulse per discharge and unchanged authoritative reserve.
The existing last-ammo, water, death, holster and remote regressions remain.

Pre-edit preservation and coordinated build/test evidence are under
`manual-artifacts/task-records/e21-zoom-sliders-20261015/`, including
`tests-build-Debug-gauss-red.log`. New-image manual validation is `not_run`;
there is no new agent-launched live/stock/server session or Git write. Final
build/test results and the ordinary Release image are recorded below; the red
evidence is retained separately.

### Combined Gauss/zoom/launcher verification

VS2022/v143/Win32 targeted Debug build passed. The final focused
`[e21-repeat-secondary],[sensitivity]` run passed all11 cases /4,545 assertions.
The broader weapon/input/camera/GameClientAPI/launcher run passed296 cases,
skipped6 explicit external-asset/runtime opt-ins, with59,921 passing assertions.
Core-only Release ALL_BUILD and CTest passed44/44, including architecture,
alternate-module input and the hidden native launcher. Slider UI checks passed
3/3 Debug repeats at96/120/144/192 DPI. The two initial zoom test failures were
fixture contract errors (reused camera input revision and retained ammo source),
corrected without weakening assertions or changing production for those tests.

Normal Release ALL_BUILD passed; all11 focused cases /4,545 assertions and
3/3 native slider UI repeats passed in Release too. After the final production
edit, the complete Release offline suite registered3,708 tests:3,642 passed,
66 explicit capability/asset/stock opt-in skips, zero failures (701.71 seconds).
The subsequent version/help/null, owned transport, no-stock fixtures and
launcher `-CheckOnly -SourceOnly` checks passed; no actual game/Steam resources
or live server readiness are claimed by that source-only launcher check.
Evidence: `release-offline-suite-final.log`, `verification.json` and
`final-audit.json` in the same task-artifact directory.
Release outputs are the unchanged standard paths under `build/bin/Release`:
`hlclient.exe` SHA-256
`53540729F0261344CA0E8DCC2E54285D19EF500EB681FD79D6DBE7AED8346B33`;
`HLClientLauncher.exe` SHA-256
`BD14AD497DA65A0692DDBF074DAB975BAAFF6FF2EA3B1A993DE289B2FE8F55E6`.
The source audit verifies all1,420 pre-edit saved files, no missing/new source
files and19 task-scoped changed files; branch/HEAD/staging and launcher settings
remain unchanged. No client asset files, server configuration or input history
were rewritten. New executable manual=not_run; ASan runtime was not rerun or
claimed verified by these non-ASan builds.

## Bunker-door impact follow-up — 2026-10-15

`attribution=user_report`: bullet decals now appear on the Crossfire bunker
doors, but Gauss impacts do not. This is scoped feedback, not confirmation of
every weapon or of a particular executable SHA from the screenshot.

The earlier brush correction reached the application's decal submission path,
but `RuntimeReplayLocalAssets::trace` still queried only the world BSP. Gauss
therefore aimed its short decal re-trace and contact sprites at a farther world
wall, bypassing the nearer door. The shared effect query now uses the existing
cached committed solid-inline scene. Its nearest contact supplies the same
sky/liquid/start-solid policy, and an unavailable or unmapped brush cannot
become an invented farther-world hit. The cache is independent of movement
prediction and rebuilds only for a new committed entity publication.

Gauss primary/charged fire, reflection and the existing single-penetration
queries all use this path. Local and observer Gauss retain their existing
profiles. Existing bullets/crowbar already use the attached brush-decal owner;
the local Egon fallback also benefits from the corrected shared endpoint query.
Server-published sprites, sparks and projectile explosions continue to use
their committed event positions. No new damage, projectile hit, material sound
or arbitrary effect is inferred from shooting a door.

Stationary Gauss contact glows now retain a small owning surface attachment:
generation, entity lifetime, exact model identity and local point/unit normal.
The application resolves it through the same current render transform as the
door, validates its live target, and retains the existing oriented depth-tested
sprite and outward bias. Removal/reuse cannot attach an old glow to a new
occupant. The shot's beam and freely moving glow balls remain world-space.
Attached decals still use the existing clipped local geometry/material and
E30 bounded lifetime owner. The 16-shot/128-sprite bounds, effect deadlines,
action deduplication, ammo and prediction are unchanged.

Pre-edit preservation and build/test evidence are under
`manual-artifacts/task-records/door-all-impact-effects-20261015/`.
New-image manual status is `not_run`; no new live game/server session,
staging, commit or push is used for this correction.

### Door correction verification and ready image

The old query was compiled from its preserved pre-edit function for the new
door regression. It hit Z=0 instead of the nearer Z=16 door; all three new
cases failed, including the isolated decal pass and missing-context controls.
The corrected production query passes the three `[door-all-impacts]` cases:
local/observer primary/charged fire, nearest obstruction, clipped local decal,
independent OpenGL decal/glow pixels, translation/rotation, duplicate delivery,
removal/reuse, foreign generation, non-unit normal and finite-shear rejection.
The owned solid half-space stops charged fire at its contact glow; free exit
balls remain conditional on a successful penetration, as in the existing policy.

VS2022/v143 Win32 Release `ALL_BUILD`, Debug client/test targets and core-only
Release `ALL_BUILD` pass. The affected-effects suite passes in both Debug and
Release: 346 passed, 11 explicit installed-resource skips, 84,208 assertions.
The core-only build (`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`) passes all 44 CTest
registrations, including 407 core API cases / 404,609 assertions and the
architecture guard.

The complete serial Release CTest run after the final production edit records
3,619 passed, 66 gated/capability skips and one failed test out of 3,686
(695.16 seconds). The one failure is
`hlclient_functional_publication_roundtrip_pwsh7`: its before/after host
Steam/HL process inventory changed. The same exact Release validation command
then passes in `publication-roundtrip-isolated.log`; the original failing run
is retained. This is a successful isolated repeat, not a green first suite.
All new door cases pass in that full run. The offline CLI/transport/log controls,
G/H1 summaries, no-stock H2 fixtures and source-only launcher CheckOnly also
pass separately (`release-cli-tail.log`, runner exit0). Historical ASan runtime
remains unconfirmed and was not retested. Optional original assets and live
Crossfire play are not claimed by the owned CPU/OpenGL fixtures.

Ready normal image: `build/bin/Release/hlclient.exe`, SHA-256
`BDBB789B6E32FF207F716DC86CB3542F42A8EE0AB336C32C5BDFF69382A71BF8`.
Launcher image/settings and endpoint `192.168.0.104:27016` are preserved.
Manual check: primary and charged Gauss into both closed bunker doors, then
observe attached marks/glow as a door moves; repeat while another client
observes, and check the existing bullet/crowbar/explosion effects. New-image
`manual=not_run`, agent live/server/stock sessions=0.

## Secondary recoil and last-ammo prediction follow-up — 2026-10-14

New `attribution=user_report`: first-person auto-discharge when charging the
last uranium produced jerks. Both latest normal closed runs used6661897B...
and exit0. Retained incidents at67.858937s and71.393100s in224969... show
17.5987/16.9273-unit raw correction and about40ms prediction suspension.
Their commands release Attack2 before recoil, so they do not identify the
user's exact automatic exhaustion action. Weapon/ammo/phase/impulse were not
recorded and there was no F8 marker. This distinction is retained.

The proven code-order defect is separate from that evidence limit: E21 applied
the saved velocity delta before movement, causing premature origin advance and
ground-friction reduction. The pinned official SDKb1b5cf5892918535619b2937bb927e46cb097ba1
documents `HUD_PostRunCmd` after movement(cl_dll/hl/hl_weapons.cpp1049), then
weapon post-think1065 and velocity recoil1073–1078. Server player.cpp PostThink
calls ItemPostFrame and gauss.cpp StartFire changes velocity there. The observed
matched/auth velocity ratio about.92 is consistent with one20ms friction step;
it is not independent proof of every reported last-ammo event.

The existing optional neutral command-effect API now has a default-pre phase
selection; HL selects post only for selected Gauss. The live owner saves it
once per sequence in its existing512-command cache. An independent bounded
`post_velocity_impulse` sidecar is retained in immutable history and distinctly
hashed only when nonzero. Normal and ladder solvers add it after movement,
without advancing shot-command origin, reducing it by friction, rewriting
ground flags or touching wire grammar. Rebase reapplies that saved command
once; audio/beam/input/TX are not replayed. Legacy pre/zero behavior remains.

Two additional positive-path defects are covered with project-owned regressions:
delayed phase2 could reopen a completed discharge during its cooldown, and
receipt-relative timer reconciliation could postpone a command-owned last-ammo
burn. Completed secondary cooldown rejects charge reopening. Command-owned
charge-start/burn deadlines may reconcile earlier or accept full sentinel, but
cannot drift later; server-adopted charge initialization remains unchanged.
This timing protection is local-compatible, not invented exact server execution
time. Fresh phase0 still cancels without synthetic retrospective recoil; no
local authoritative ammo/damage or server event is created. Life/model/session
reset and future valid charging remain supported.

The canonical echo also needs the post-step boundary semantics: a grounded
upward discharge can retain the completed movement's ground flag while its
new velocity exceeds the ordinary180-unit ground-probe limit. The previous
seed categorizer rejected that combination before rebase. The neutral seed
API now accepts an optional exact immutable post-boundary command witness,
requiring reference compatibility, matching sequence and wire-derived fields,
and positive validated post-Z. Only the existing support probe is enabled;
free occupancy and real walkable support remain mandatory. Origin/velocity
stay canonical and recoil is not applied again. The live owner uses exact
history, or its existing bounded accepted-command cache for repeated anchors,
with unchanged generation/life-boundary gates. No-witness, wrong-wire,
synthetic/pre-only, unsupported-floor and strict-profile paths remain strict.
This does not loosen the separate upward limit for embedded-brush recovery.
The next real movement command categorizes the upward state as airborne.

New tests cover1/2/3-ammo auto-discharge, module phase through normal kernel,
once-only audio/beam/action, delayed fresh phase2/depletion and phase0, command
bounds, ground/air/ladder/collision timing, independent pre semantics,
subsequent integration, wire/hash isolation and retained rebase. Canonical
postZ400/1000, exact/repeated live ACK ownership and next-command airborne
behavior have positive and strict negative controls. Existing E21 assertions
are not weakened. Final Debug/Release/core/architecture evidence, the complete
Release run's unrelated E28.2 timeout and the ready ordinary image are recorded
in [E32 final follow-up verification](LAN_MOVEMENT_LIFT_STABILITY_E32.md).
New image
manual=`not_run`; no new live/stock/HLDS/LAN session or staging/commit/push.
Historical ASan runtime remains unconfirmed and is not reclassified.

## Additive user acceptance — E22 request, 2026-10-06

Latest E21 evidence is `manual=passed; attribution=user_report`, from the user's
explicit “все протестовано” in the E22 request: normal multiplayer primary,
charge/spin/audio/beam/remote/cleanup/stability. This is not a new agent-launched
session, and does not promote unreported water/overcharge specifics. The
historical `manual=not_run` reports and executable hashes below are preserved.
E22 remains a separate new implementation with `manual=not_run`; see
[E22](EGON_CONTINUOUS_BEAM_E22.md).

Implementation in the normal `hlclient.exe`, not a demo, DLL or new engine.
Worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`; branch
`codex/stock-runtime-campaign-5e48b7c1`; unchanged HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`.
Primary result: `gauss_primary_secondary_charge_beam_integrated`.
E21 manual fields remain `not_run`; offline evidence is not multiplayer or
human listening confirmation. New live/stock/HLDS/Steam/WFP/capture launches=0;
staging/commit/push=none. Previous dirty E11–E20 work is preserved.

## Prior evidence and initial audit

E20's new “Перевірено” is recorded additively as `manual=passed;
attribution=user_report`: RPG launch/server rocket, designator/guidance,
trail/light/audio, explosion, stability. Water is not promoted. E19's separate
“перевірив все працює” remains user-attributed; E18 remains factually
`manual=not_run`, not silently accepted. Historical SHA/manual reports are
unchanged and are not assigned to unidentifiable later user runs.

Pre-edit source snapshot: `manual-artifacts/task-records/e21-gauss-20261005/`,
1,233 tracked/untracked regular project source files copied and SHA-256 verified.
Scoped pre-edit Release baseline: 247 cases (239 pass, eight explicit skips),
120,985 assertions passed. These counts are historical baseline, not final E21.

Before E21: WeaponList/selection and importers already admitted Gauss resources,
but no Gauss action controller, assigned spin/fire interpretation, charge voice,
beam policy or command-history impulse existed. Audio already supported pitch,
source and stop; E20 already provided neutral textured ribbons/glow sprites.
Reuse those mechanisms, existing static BSP line queries, surface-clipped decals,
named-bone Studio merge and H4 reconciliation; no parallel physics/audio system.

## Reference and inspected resources

Read-only official SDK revision `b1b5cf5892918535619b2937bb927e46cb097ba1`:
`dlls/gauss.cpp`, `dlls/client.cpp`, `cl_dll/ev_hldm.cpp`,
`cl_dll/hl/hl_weapons.cpp`, common declarations and negotiated delta fields.
Used as behavioral reference, not copied implementation/ABI or binary parity.
No submodule update, ReHLDS/Xash/proprietary dumps or asset changes/downloads.

Exact ID9 + `weapon_gauss`, owned bit9, known ammo-type binding, positive health,
life/water/timers and validated `models/v_gauss.mdl` roles gate actions.
Uranium reserve is server-derived; there is no clip. Existing group4/wheel
selection and WeaponList inventory are reused; no game-message ID hardcoding.
`models/w_gauss.mdl` remains an ordinary server entity, not a client pickup.

Actual approved read-only v-model inspection:

| Index | Label | Frames / FPS |
| --- | --- | --- |
| 0 / 1 / 2 | idle / idle2 / fidget | 61/15, 61/15, 91/30 |
| 3 / 4 | spinup / spin | 31/30, 16/30 |
| 5 / 6 | fire / fire2 | 31/30, 41/30 |
| 7 / 8 | holster / draw | 31/45, 36/60 |

Bodies0/1/2 are selectable; EV fire uses sequence6/body2, not label `fire`
at index5. Attachment0: bone11, local `{17,-1,0}`. The existing camera-local
pose/materializer evaluates it at current pitch/yaw; normal first beam uses
that world-transformed muzzle. Recoil/light/flash use existing neutral passes.

Actual p-model has no attachments. Its twelve-bone chain ends at `Box02`,
translation `{8.02966,.950843,4.03907}`, rotation
`{1.54315,.002126,-3.04524}`; mesh bounds
`{-20.43,-4.88,-3.1}` to `{15.84,3.96,3.53}`. The HL profile validates these
roles, parent names, measured bounds and vertex bone ownership, then derives
the -X barrel-end vertex-ring centroid with .1-unit outward offset. It is a
**local-compatible socket**, not an invented stock attachment. Named-bone
merge uses the current server body pose; events do not force body animation.
An unavailable/stale socket explicitly falls back to event eye, never muzzle0.

Approved dependencies: `sprites/smoke.spr`, `sprites/hotglow.spr`,
`ambience/pulsemachine.wav`, `weapons/gauss2.wav`, `weapons/357_cock1.wav`,
`weapons/electro4.wav`, `weapons/electro6.wav`. Sprite/model slots come only
from the ready manifest; WAVs use the existing narrowly typed verified local
HL-profile dependency mechanism, without requiring a server sound slot.
Preparation happens while selected or event-bound, never in the audio callback.
Names are sound-namespace relative (no duplicate `sound/`). Missing components
gracefully skip independently with existing readiness/resource/queue reasons.

## Actions, charge and atomic events

Primary: provisional energy20, reserves two uranium, next attack .2s; fully
submerged block .15s. This predicts presentation, not server damage/ammo.
Secondary: reserves one at start; spinup→spin after .5s; MP full1.5s, SP4s,
MP periodic reserve .1s / SP .3s after spinup, then full sentinel stops burn.
Release after initial idle deadline or depletion produces one secondary action.
No local inventory/ammo/health write; server ammo decrements retire reservations.
Empty/water/cancellation do not create a successful beam. Over10s gives bounded
electro presentation/cancellation without local damage/fade authority.

Generic exact typed projection exposes optional signed weapon `iuser2` and
client `fuser2/fuser3`, without Gauss names/ABI offsets. HL interprets phase0–2,
remaining burn/full-charge timers and full sentinel. Fresh usable observations
reconcile phase, burn and magnitude timing; retained records do not slide timers.
Missing descriptors/fields inhibit actions. Already active server phase can be
adopted without a second start consumption. The post-full clamped timer cannot
recover original overcharge age; the established local start clock is retained.

Assigned `events/gaussspin.sc`: iparam1 pitch, bparam1 change/start. Starts one
continuous pulsemachine voice; updates preserve cursor and animation restart.
Exact assigned `events/gauss.sc`: fparam1 finite energy0–200, bparam1 primary,
bparam2 stop. Magnitude is not bullet spread. STOP is handled before beam/energy
validation; .01s reliable stop schedules only termination, never another shot.
The neutral record owns values and advertised presence; malformed suffix rolls
back the entire record, game state and effects. Framing/canonical runtime hash
grammar remain unchanged; new optional extension fields retain exact source and
publication ownership like the pre-existing auxiliary extension contract.
No numeric event constants, opcode scan/resync or console execution.

Event dedup keys generation + record ordinal + cursor + entry, with bounded
eviction floor; distinct spin updates remain distinct. Local echoes confirm an
already predicted action, not another voice/shot/impulse. Remote sources have
separate voice lifetimes, spatial source position and pose updates. Slot absence,
NODRAW/weapon0, incompatible pmodel, disconnect/life/model and map/session
boundaries retire charge. A conservative one-second remote update lease prevents
orphan loops when stop evidence is lost; this is bounded compatible recovery.
Focus mute stops voices; no stale missed audio is replayed on later readiness.

## Beam, impact and movement

No shell, MP5 tracer or projectile entity. Primary: smoke texture, width1,
RGB `{255,128,0}`, alpha128, lifetime .1s. Secondary: width2.5, white,
energy-related alpha, lifetime .1s. Hotglow endpoints and short hotglow balls use
the same bounded sprite materializer. Brightness/lifetime/ball motion are
reference-informed compatible rendering, not stock TempEnt physics/RNG.

Static BSP trace length8192; at most ten iterative interactions. Grazing
`n=-normal·direction < .5` reflects `direction+2*n*normal`, advances8 units,
loses energy by n; zero-n explicitly loses10%. Invalid/startsolid/allsolid,
sky/liquid/unsupported terminate safely. Primary blocks at head-on wall.
Secondary allows one penetration: forward/back static query locates exit,
thickness must be less than remaining energy, cost at least1; entry/exit reuse
existing clipped decals and independent glow/burst requests. No E7 material-hit
sound is dispatched. Optional ricochet probability is not implemented; no false
substitute material sound. Dynamic player/brush hit presentation remains
`evidence_limited`: static-only query is not server trace, target damage, blood,
shield or wall-punch authority. Server health/death/score remain E13/E14.
Aftershock stays server-audio-owned; no duplicate local delayed electro cue.

Secondary returns opposite aim * bounded magnitude *5 velocity delta (SP Z0).
The normal session host attaches it once to the accepted quantized20ms command
before movement simulation. Immutable history retains it and hashes tagged
nonzero bytes; wire codec remains identical, zero-sidecar old signatures intact.
Rollback repeats physics on the suffix once per saved command, not presentation,
audio, input or TX. Server velocity/origin rebase is final authority. Primary and
observing clients have no movement impulse. Current kernel/collision/cadence and
reference/off mode are preserved. No separate camera teleport Gauss-jump hack.

## Ownership, paths and bounds

`HalfLifeClientModule → GaussPresentation → neutral API → existing host`.
Concrete policy: new `include/hlclient/games/halflife/gauss.hpp`,
`src/games/halflife/gauss.cpp` in `hlclient_game_halflife`; existing
`client_module.cpp` and `presentation.cpp` compose it. Neutral additions:
`game_api/beam_effects.hpp`, persistent voice controls, typed scripted params,
optional command velocity. Engine owns approved imports/query/mixer/simulation;
application owns beam materialization and existing decal submission. No lower
target links concrete HL or knows ID9/timing/samples. TestGameClientModule uses
the same seam without HL fallback; `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` excludes it.

Bounds:16 pending shots,128 sprites,32 per-frame impacts,128 recent events,
32 cues/voice controls,33 possible charge owners (local plus32 remote slots),
32 active/scheduled persistent delivery entries, existing128 mixer voices,
512 command-effect sidecar cache. Effects survive weapon switch once published;
session/map reset clears them. Source/render queries are borrowed synchronously;
outboxes retain only owned values, not RX/resource buffers or engine handles.
Opt-in `net_trace` beam counters have32-line budget; submitted != visible/played.

## Verification and manual handoff

Final verification is after the last production correction in `gauss.cpp`:
charge cancellation retires its provisional visual phase, charged dry clicks
respect cooldown, and queued Gauss shots cannot borrow a different weapon's
muzzle after switching. No production edits followed the final Release suite.

| Check | Final result |
| --- | --- |
| VS2022 / Win32 Debug, client and affected test targets | Build passed |
| Focused Debug: E21, runtime replay, weapon audio, E20, entity interpolation | 143 cases: 140 passed, three explicit asset opt-in skips; 16,409 assertions passed |
| VS2022 / Win32 Release, all targets | Build passed |
| Final audited Release offline suite | 2,962 registered: 2,915 passed, 47 explicit capability/opt-in skips, zero failures; CTest 162.71s; complete script exit0 |
| Exact approved read-only Gauss asset / production GPU / isolated PCM controls | 27 cases, 1,214 assertions passed, zero skips/failures |
| Separate short opt-in SDL output capability | One case, five assertions passed; startup, queued PCM and output callback observed |
| `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`, existing `build-core-g1` | All-target Release build and 41/41 CTest passed; alternate TestGameClientModule uses the same host/API |
| Architecture | Guard passed; 159 generated projects excluding dependencies/CMake internals contain zero concrete HL source/link references |
| Scoped ASan Debug core/API | Build passed; 64 cases, 3,842 assertions passed |
| ASan normal application runtime | `not_verified`; historical exit1/no output even for `--help` is not repaired or relabelled; concrete Gauss runtime is not claimed ASan-covered |
| Null/process/offline transport and summary checks | Passed inside final audited script, including native/PowerShell roundtrips, reference/off, fake-peer/no-stock and source-only CheckOnly |
| Actual unchanged launcher, final Release | `-Prediction reference -RemoteAudioPeer -NoTimeLimit -CheckOnly` passed; process-launches=0 |
| Preservation / Git | 1,233 snapshot files: 1,200 unchanged, 33 intended E21 changes, zero missing; HEAD/branch/SDK unchanged, index empty, `git diff --check` passed |

Core-only and scoped ASan checks precede the final concrete Gauss-only edits;
their shared/core sources are unchanged, and neither build contains the HL
module. The final Debug, approved E21 controls and full Release suite include
those edits. Skips are not passes: required owned-fixture coverage runs without
game assets/device; opted-out original assets, device, link/reparse/WFP/capture
capabilities and SDL capture controls remain explicitly skipped in that suite.
No WFP activation or capture is substituted for offline proof.

GPU checks isolate beam and endpoint contributions, distinguish primary from
secondary, exercise camera-local vmodel and remote merged pmodel/socket, and
return to baseline after expiry. Isolated decoded spin/discharge PCM traverses
the production module/audio/mixer path: finite nonzero output, one continuing
spin voice, cursor-preserving pitch updates, independent discharge and stop.
The short SDL device check proves output capability, not human listening or
live multiplayer Gauss acceptance. Static fixture checks cover ten-interaction
cap, single punch across multiple walls, MP/SP recoil and immutable replay.
Atomic malformed suffix, duplicate/retained inputs, missing fields, death,
holster, source reuse, delayed stop and queue/resource bounds have regressions.
E13-E20, existing movement/camera/HUD and launcher regression paths remain green.

One existing E16 release test was updated to the new typed release contract,
not weakened: accepted capture/focus/RMB release notifications must preserve
exact zero wire buttons, transmitted command sequence/time, monotonic receipt
and one-time delivery; repeat update must not redeliver. Wire/history counts
remain unchanged. Historical first-suite failure is retained separately.

Evidence directory: `manual-artifacts/task-records/e21-gauss-20261005/`.
Final logs: `build-release-final.txt`, `full-release-offline.txt`,
`build-debug-final.txt`, `focused-debug-final.txt`, `approved-e21-final.txt`,
`device-output.txt`, `build-core-only.txt`, `core-only-tests.txt`,
`asan-build-core.txt`, `asan-core-api.txt`, `launcher-check.txt`.
These local diagnostics/snapshot are not build dependencies. Required
code/headers/tests/docs/CMake are normal project files, including new
`tests/test_gauss.cpp`; no private game assets are copied into fixtures.

Final executable: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
3,767,296 bytes; SHA-256:
`253A6472473D695BB6F3FCD1ACE71B4B6846A458312BC556A452324097321949`.
Unchanged launcher SHA-256:
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
Neither hash is assigned to a historical user run.

`gauss_primary_manual`, `gauss_charge_manual`, `gauss_spin_audio_manual`,
`gauss_remote_manual`, `gauss_reflection_manual`, `gauss_punchthrough_manual`,
`gauss_jump_manual`, `gauss_charge_cleanup_manual`, `gauss_kill_respawn_manual`,
`gauss_overcharge_manual`, `gauss_water_manual`: all `not_run` until explicit
user confirmation. New live/stock/HLDS/Steam/WFP/capture launches=0;
staging/commit/push=none. E22/Egon and other new gameplay are not implemented.

Unchanged manual command, default crossfire, two clients:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Pick up Gauss/uranium; select group4 or wheel. Test LMB narrow orange beam,
fire2/body2, sound/flash and no brass/tracer/projectile. Hold RMB: one rising
spin voice and spin animation, stable full cap after1.5s; release for wide beam,
opposite movement recoil and thin static wall entry/exit. Compare primary block.
Observe the second player's pGauss, beam and spatial charge/discharge, without
observer recoil. Switch/death/respawn while charging must stop it. Exercise
kill/death/score plus E13–E20 regressions. Close both clients and wait for cleanup.
Water/overcharge are optional and remain separately `manual=not_run`.

## E21 follow-up — stock vertical-only shot context (2026-10-05)

New manual evidence, `attribution=user_report`: Gauss fire sound not heard,
beams not visible, wall-hit effects not visible. This is a failed visual/audio
observation for the preceding handoff, not acceptance and not a rewrite of its
historical offline counts. The latest local manual report candidate is run
`9c5d7a8dd9084ac1969be600e37552e1`; it reports normal completion, an operational
audio backend and no local audio open/decode failures. Its bounded retained
window does not identify every Gauss action, so the user's exact shots are not
assigned a binary hash or inferred successful counters.

The three failures share a reproduced production cause in
`src/goldsrc/live_runtime_stage.cpp`: the pre-simulation
`command_movement_effect` path required `view_offset.complete()`. Actual
read-only Valve `clientdata_t` schema and the project-owned stock-shaped fixture
advertise only `view_ofs[2]`, leaving X/Y deliberately unavailable. Thus the
Gauss command was delivered with no shot context, `GaussPresentation::fire`
rejected it before queueing sound/beam/impacts, and the later correctly formed
application TX notification was rejected by the existing command dedup floor.
This is missing production context, not Windows audio, a missing WAV, or GPU
compositing. Required Gauss charge fields remain evidence-gated; they were not
replaced by assumed zeros to repair this defect.

The early path now accepts either a complete view offset or the exact existing
reference vertical-only eye policy (both horizontal components absent and a
finite advertised Z). Horizontal zero is applied only to the derived eye; the
owning observations retain unavailable X/Y. Partial horizontal vectors and
missing Z are not silently accepted. A coherent predicted eye still overrides
the derived server eye when available. No new Half-Life rule is in the host,
and wire buttons/history, command dedup, physics cadence, damage/ammo authority,
audio/renderer backends and launcher are unchanged.

Regression in `tests/test_resource_client_response_stage.cpp`, tag `[e21-1]`,
uses the real LiveRuntimeStage/production HalfLifeClientModule and independently
packed stock-shaped clientdata in both reference/off. Before the correction it
fails at the missing accepted action; after it, 567 assertions pass: first
primary produces one Gauss2 cue, a beam starting at the vertical-only eye and
one wall impact; TX/repeated update do not duplicate them. Charged secondary
starts spin, stops it on release and produces a full-volume discharge, wider
beam and impact in both modes. The frame timestamp follows command delivery
by 1ms, avoiding floating-point equality between differently subtracted clocks.
Existing production PCM/GPU/resource tests remain separate from this new
runtime-routing proof; none is labelled human listening or live validation.

Pre-edit verified source snapshot: 1,238 ordinary project files in
`manual-artifacts/task-records/e21-1-stock-eye-20261005/source/` with manifest.
Only the runtime source, two regression-test files and this additive record change.
The original-asset PCM control also now drives bounded nonblocking retries:
`ApprovedSoundAssets::request_local` explicitly returns pending on cache-lock
contention even for warmed PCM. A one-update assertion was therefore racy;
the sink already retains/retries correctly in production. The test forces one
busy tick for spin and discharge, checks pending is not played, then delivers
empty continuation batches within125 logical milliseconds. All existing
voice-count and nonzero isolated PCM assertions remain, with no resubmitted
action/control or relaxed playback assertion. The first failed control and
diagnostic rerun are retained, not overwritten as successful evidence.
Corrected build manual validation remains `not_run`;
new live/stock/HLDS/Steam/WFP/capture launches=0, staging/commit/push=none.

### Follow-up verification and corrected Release

| Check | Follow-up result |
| --- | --- |
| VS2022 / Win32 Release, all targets | Build passed |
| Debug, affected client/test targets and focused regressions | Build passed; 144 cases: 141 passed, three asset opt-in skips; 16,976 assertions passed |
| Exact approved read-only Gauss resources, production GPU and isolated PCM | 28 cases, 1,784 assertions passed; zero skips/failures |
| Final audited Release offline suite, sequential | 2,963 registered: 2,916 passed, 47 explicit capability/opt-in skips, zero failures; CTest 544.66s; complete script exit0 |
| Core-only, `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`, existing `build-core-g1` | All-target Release build and 41/41 CTest passed, including architecture and alternate-module API checks |
| Unchanged actual launcher | `-Prediction reference -RemoteAudioPeer -NoTimeLimit -CheckOnly` passed, process-launches=0; final script also passed Null/process/transport/summary/fake-runner/source-only checks |
| Preservation | 1,238 snapshot files: 1,234 unchanged, four intended follow-up changes, zero missing; branch/HEAD/SDK unchanged, index empty, `git diff --check` passed |

The first post-production parallel suite passed. The later parallel repeat
after the PCM-control test edit had one unrelated startup-boundary failure:
PowerShell 5.1's global stock-process inventory differed before/after its
fixture. All handshake assertions had already passed. This inventory check
can observe independently registered offline helper processes running in
parallel; the exact competing PID was not retained, so that particular cause
is an inference rather than a proved process attribution. Three isolated
repeats and the complete final sequential suite passed unchanged assertions.
No safety check was disabled and no launcher/orchestrator source was changed;
parallel inventory-test interference remains a known test limitation.

Logs and red/green evidence are retained separately in
`manual-artifacts/task-records/e21-1-stock-eye-20261005/`: `regression-before.txt`,
`regression-after.txt`, `build-release.txt`, `build-debug-final.txt`,
`focused-debug-final.txt`, `approved-e21-final.txt`, `build-core-only.txt`,
`core-only-tests.txt`, `launcher-check.txt`, `full-release-offline.txt`,
the failed `full-release-final.txt`, three `startup-isolated-*.txt` controls,
and final `full-release-serial-final.txt`. No private resource is a build/test
dependency. Core checks preceded the optional concrete asset-test retry edit;
their source and dependencies did not change afterwards. No production edit
followed the final Release suite.

No new device-output or ASan run is claimed for this correction. The preceding
E21 device capability/scoped core-ASan results remain historical; normal
application ASan runtime is still `not_verified`. Nonzero PCM is not a human
listening confirmation. Static fixture/GPU checks do not establish live stock
parity, nor do they relax missing-field/unsupported-surface evidence gates.

Corrected normal executable:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
3,767,808 bytes; SHA-256:
`775074674F860105281D04E99EC9744CB9FDD992BE6DA6948CB7FDE077D43949`.
Launcher SHA-256 remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`;
it selects that normal Release path, with no new flag or asset copying.
Neither hash is assigned to the preceding user run. Use the unchanged manual
command above: test Gauss LMB and RMB charge/release into a wall, then observe
from the second client and check switch/death/respawn cleanup. This corrected
build still needs new user confirmation of visible effects and audible sound.

## E21.2 user-requested surface-glow and rounded 3D beam follow-up (2026-10-05)

New attribution=user_report: the screenshot shows a sharply clipped orange
wall-contact glow; requested Gauss beams use the current MP5 rounded 3D shape,
three times its length, significantly brighter, yellow primary and white
secondary. No executable hash was supplied for that screenshot. Historical
E21/E21.1 sound/manual evidence above is retained, not assigned to this build.

The clipped image is the stationary `sprites/hotglow.spr` effect, not evidence
that `{SHOT1` WAD pixels or decal alpha were damaged. The exact approved
read-only original hotglow is additive, view-parallel, 64x64, with black-neutral
corners. Gauss previously placed that billboard exactly on the BSP hit; at an
oblique camera angle its plane intersected the opaque wall, so the unchanged
depth test clipped one side. WAD/importer palettes and installed assets are
untouched. Stationary entry/reflection/exit glows now carry a normalized owning
contact normal through `BeamSpriteFrame`. `RuntimeReplayLocalAssets` derives
the oriented surface plane, transformed bounds and 0.125-unit outward bias.
The generic frame has a tightly validated, signature-tagged surface-aligned
override; ordinary server sprite orientation still matches source metadata.
Additive depth testing remains enabled, unlike global depth-disabled Glow.
Moving balls stay billboards and actual bullet decals retain their clipped
surface owner, modulation, depth and expiry rules.

`GaussPresentation` selects the existing solid 3D rounded streak mesh, 144 units
(3 x current MP5 48), 12000 units/second, 90% trailing taper, 20% rounded terminal
diameter, neutral RGB emission gain3, yellow `(1,1,0)` primary and white
`(1,1,1)` secondary. Existing Gauss widths1/2.5 and causal beam expiry0.1s stay.
Head glow radius2.5, light radius64/intensity1 are bounded local-compatible
presentation settings, not stock constants. The first-frame motion clock is
captured once when the trace materializes, like the existing MP5 muzzle streak,
so a nearby segment is visible before its tail advances. Subsequent frames
sample absolute age from immutable endpoints; close hits clip length and the
tail vanishes once it reaches them. Instant trace/reflection/punch/impacts,
command history/replay, charge/recoil, audio and ammo/damage authority are not
delayed or changed. No later muzzle following, retracing, or sprite-slot
dependency is introduced for solid geometry. MP5 source/profile remains
byte-identical to the pre-edit source snapshot; neutral gain defaults to1.

The pre-edit snapshot contains 1238 verified ordinary project files under
`manual-artifacts/task-records/e21-2-gauss-3d-20261005/source/`. Production files
changed for this follow-up: game-api beam contract, Half-Life Gauss header/source,
app materializer, neutral entity frame header/source, render-scene contract,
solid mesh validator and OpenGL RGB gain. Tests use owning project fixtures;
the approved original sprite/model/audio control is optional read-only, never
a build dependency. New manual validation remains `not_run`; live/stock/HLDS/
Steam/WFP/capture launches=0, staging/commit/push=none.

The first focused GPU run retained in `focused-initial.txt` failed at the
explicit source-orientation identity check when the app selected an oriented
instance from a view-parallel asset. The narrow surface-aligned contract fixes
this without relaxing ordinary sprite validation. CPU tests cover accepted
override, immutable metadata, deterministic signature and invalid combinations;
GPU A/B checks use opaque textured walls, both sides of the glow, neutral
rectangle corners, oblique angles, foreground occlusion and cleanup. Isolated
low-tint GPU checks distinguish gain3 from gain1 without framebuffer clipping,
preserve coverage/alpha, test yellow/white, invalid gains and later HUD/pass
state. Camera-facing moving-head flashes are suppressed when the head is
clipped to its immutable trace endpoint; the surface-aligned contact glow owns
that final point, while the bounded world light remains. This prevents the new
short flash from repeating the same plane-intersection artifact. Module-to-app
tests cover motion144/60-unit advance, sprite-independent
geometry, near-hit first frame, repeats, expiry, stale/invalid inputs and pool
bounds. Final verification and Release hash are recorded below after completion.

The owned PCM fixture also encountered the already documented nonblocking
cache-lock `pending` on a warmed sample in one combined control run
(`approved-gpu-final.txt`). `request_local` uses `try_to_lock`, so readiness
does not promise that every subsequent request returns ready in the same tick.
The owned test now advances the existing application sink with empty bounded
continuation batches, without repeating the shot/control. Voice-count, stop,
persistent cursor, update and finite nonzero PCM assertions are unchanged;
audio production was not edited. Intermediate test-only Win32 `near` macro and
namespace compilation failures are retained in build logs; both are corrected.

### E21.2 final verification and normal Release

Result: `gauss_surface_glow_rounded_3d_beams_integrated_offline_verified`.

| Check | Final result |
| --- | --- |
| VS2022 / Win32 Release, all targets | Passed after final endpoint guard |
| Debug affected client/test build and focused regressions | 173 cases: 169 passed, four explicit asset opt-in skips; 117375 assertions passed |
| Release owned/original approved asset and production GL controls | 41 cases, 101231 assertions passed; no failures/skips, includes original hotglow surface A/B, gain and neutral-state restoration |
| Final audited Release offline suite, parallel4 | 2969 registered: 2922 passed, 47 capability/opt-in skips, zero failures; CTest295.18s; complete source-verification script exit0 |
| Core-only OFF, existing build-core-g1, after final production edit | All-target build and 41/41 CTest passed, including architecture/alternate-module API; no Half-Life linkage |
| Actual unchanged manual launcher | reference/RemoteAudioPeer/NoTimeLimit CheckOnly passed; process-launches=0, normal Release selected |
| Null/process/transport/summary/no-stock fixture | Passed in final audited script; new live sessions0 |
| Preservation | 1238 snapshot files: 1224 unchanged, 14 scoped source/test/docs changes, zero missing; MP5 policy/world-impact files unchanged, branch/HEAD/SDK unchanged, index empty; diff check passed |

Definitive logs: `build-debug-final-v2.txt`, `focused-debug-final-v2.txt`,
`build-release-verification-final-v2.txt`, `approved-gpu-verification-final-v2.txt`,
`build-core-only-final.txt`, `core-only-final.txt`, `launcher-check-final.txt`
and `full-release-offline-final.txt` in this follow-up's task record directory.
No production edit followed the full Release suite. Ordinary project sources
remain clean-source/Git-checkpoint eligible; no code lives only in artifacts.
No new device-output/ASan run is claimed for this visual follow-up. Earlier
scoped core-ASan results are historical; normal application ASan runtime
remains `not_verified`. GPU pixels/isolated PCM are not new human confirmation.

Normal Release `build/bin/Release/hlclient.exe`: 3772416 bytes, SHA-256
`00862C99923C1829792FB8454C627D98282B19ED0403913C8C6E293DF944DE05`.
Launcher unchanged: SHA-256
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
Neither hash is assigned to the preceding screenshot/run. No new flag or
resource copying is required. Optional original asset control was read-only;
game/server/Steam files and the pinned SDK were not changed.

Manual handoff (new build manual=`not_run`):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Test one primary into a dark wall at an oblique angle, then charged secondary:
surface glow must have no straight clipped half; primary volume is yellow,
secondary white, both longer/brighter than unchanged MP5. Check from the peer,
near/far walls, movement and weapon/life transitions. Geometry is the current
local-compatible rounded moving streak, not a claim of stock-exact beam
rendering or dynamic-hit/damage parity. New live/stock/HLDS/Steam/WFP/capture
launches=0; staging/commit/push=none.

### E21.2 width tuning — 2026-10-05

User-requested follow-up to the above 3D profile: primary diameter1 →1.75
units (+75%), secondary diameter2.5 →5.5 units (+120%, multiplier2.2).
`GaussPresentation::sprite` selects these widths for local and remote
solid ribbons only; non-ribbon glow metadata keeps its previous widths.
Length144, speed12000, gain3, yellow/white colors, taper, terminal caps,
head/contact glow, lifetime, audio and gameplay are unchanged. MP5 and
world-impact policy files are byte-identical to this follow-up's snapshot.

The neutral app materializer and `solid_line_mesh` now accept a bounded
diameter through6 units so secondary5.5 reaches normal OpenGL. Existing
flat-line limits and geometry allocation bounds are unchanged. Tests accept
exactly6, reject6.01, and validate full/short watertight rounded meshes at
1.75/5.5/6. Production local/remote module, live-stage reference/off,
materialization and GPU tests assert the requested widths; no effects are
enabled by a new flag and no wire/history/collision policy is changed.

Verification after the final source/test edit:

- VS2022/Win32 Debug client/tests built; 72 focused cases:69 passed,
  three explicit original-asset opt-in skips;118790 assertions passed.
- Release all-target build and focused read-only approved asset/OpenGL
  controls passed:72 cases,119025 assertions, zero skips/failures.
- Core-only `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` all-target Release build and
  41/41 CTest passed, including architecture and alternate-module checks.
- Final audited Release CTest:2969 registered,2922 passed,47 capability/
  opt-in skips, zero failures;205.01s. Normal launcher
  reference/RemoteAudioPeer/NoTimeLimit CheckOnly passed;process-launches=0.
  Complete audited `build_source.ps1 -TestOnly` passed, including Null,
  transport/summary checks and no-stock launcher fixtures;script exit0.

Logs and the 1238-file pre-edit verified snapshot are retained in
`manual-artifacts/task-records/e21-2-width-20261005/`. The initial focused
Release run retained one old secondary2.5 expectation in the real host test;
the final expectation is5.5. Debug initially exhausted compiler/MSBuild
memory; a serial incremental retry passed without source/config changes.
No assertions were removed; only the requested width/bound expectations
changed and additional diameters entered the existing geometry checks.

Normal Release `build/bin/Release/hlclient.exe`:3772416 bytes, SHA-256
`3160B2A5B9563FF65752B76E384F7172B08480C11E041CA5133ACB44A8351FDF`.
The unchanged launcher/manual command above selects this executable.
New manual validation=`not_run`;no new ASan/device-output claim. Historical
reports and user confirmations remain unchanged. Live/Steam/HLDS/WFP/
capture launches=0;staging/commit/push=none;branch/HEAD/SDK unchanged.

### E21.2 secondary diameter and trace lifetime follow-up — 2026-10-05

Latest user tuning: secondary solid-ribbon diameter5.5 →3.5 units;
primary remains1.75. Both Gauss ribbon lifetimes0.10 →0.15 seconds (+50%).
The two production changes are in `GaussPresentation::sprite` and its ribbon
creation call. Speed12000, length144, rounded shape/taper, yellow/white colors,
gain3, contact/head glow, ball lifetimes, audio, trace/penetration authority
and MP5 are unchanged. The existing neutral solid-width limit6 stays bounded.

Lifetime remains the causal maximum expiry/fade window, not a promise to
linger at a wall. The existing moving tail retires earlier when it reaches
the immutable traced endpoint; no flight-speed change or stationary tail
was introduced. Materialization still advances60 units per5ms, never follows
a later muzzle, retraces, or repeats effects through render/prediction replay.

New temporal checks cover local/remote primary/secondary ribbon retention
at action+0.125s and retirement by+0.151s. The normal app path with an owned
miss fixture remains visible after the previous0.12s deadline, has alpha0.30
at0.125s and positive fading alpha below0.01 immediately before its0.17s
expiry. Existing near-hit early retirement, immutable endpoints, repeated
updates, resets, bounds and normal GPU controls remain checked.

Verification after the final test edit:

- VS2022/Win32 Debug client/test build passed;72 focused cases:69 passed,
  three explicit original-asset opt-in skips;118828 assertions passed.
- Release all-target build and focused approved read-only asset/OpenGL
  controls passed:72 cases,119063 assertions, zero skips/failures.
- Existing core-only OFF configuration:41/41 CTest including architecture
  passed. This change touches no shared production/test source or linkage.
- Unchanged reference/RemoteAudioPeer/NoTimeLimit manual launcher CheckOnly
  passed with process-launches0; normal Release selected.
- Final audited full Release retry completed2026-10-06 with parallel2:
  2969 registered,2922 passed,47 capability/opt-in skips, zero failures;
  CTest240.82s. Complete `build_source.ps1 -TestOnly` exited0, including
  Null, transport/summary and no-stock launcher fixtures.

Logs and the verified1238-file pre-edit snapshot are in
`manual-artifacts/task-records/e21-2-width-lifetime-20261005/`. Initial Debug
compilation rejected a signed/unsigned comparison in the new count assertion;
the explicit nonnegative size conversion fixed it without removing or
weakening the assertion. Source/profile review found exactly the requested
two production constant changes; other snapshot sources remain unchanged.

The retained initial parallel4 full suite had one unrelated unchanged owned
fake-peer test failure (CTest207.26s): the malformed-response variant returned
`endpoint_not_owned` before reaching its parser, not an accepted malformed
response. Its isolated repeat passed once, then a map-mismatch variant
returned `query_timeout`. Exact endpoint/PID/response evidence was not retained,
so the cause beyond these observed readiness gates remains undetermined;
scheduling/port reuse is only a plausible inference. The header, host, fake
server and test match the pre-edit snapshot. No readiness code/assertions were
changed. The same assertions passed in the final bounded full parallel2 retry;
this is not a claim that the intermittent readiness issue was fixed.
Both full logs and `fake-peer-readiness-repeat.txt` remain in this task record.

Normal Release `build/bin/Release/hlclient.exe`:3772416 bytes, SHA-256
`F2577722F0668ABDF4D1C866717A627AA6E59A37630F3B9E7359010CB2844229`.
Launcher and manual command above are unchanged. Manual validation of this
new build=`not_run`; no new ASan/device-output claim. Earlier report hashes
and user confirmations remain historical, not assigned to this build.
Live/Steam/HLDS/WFP/capture launches=0;staging/commit/push=none.

### E21 remote Gauss aiming fix — 2026-10-06

New `attribution=user_report`: observer A sees B's Gauss model aim downward,
but B's beam travels upward on A. The model animation was reported correct.
No run identity or executable hash was provided; none is assigned retrospectively.

Root cause: `GaussPresentation::event` used inherited packet-entity pitch
directly as view pitch. The neutral committed-event API already owns
`angles_from_entity`; the generic publisher distinguishes inherited entity
orientation from explicit event arguments. Half-Life player model pitch is
inverted/compressed. The Gauss callback now restores only that inherited
source using `-3 * remainder(model_pitch,360)` before its existing forward
vector calculation. Explicit event aiming angles are unchanged, as are local
command directions. This matches the existing Half-Life remote-effects and
weapon-visual profiles; no generic decoder, canonical hash/wire grammar,
player pose, renderer, prediction or server damage/trace authority changed.
The pinned official SDK remains `b1b5cf5892918535619b2937bb927e46cb097ba1`;
its Gauss event uses `AngleVectors` and the existing8192-unit trace, without
requiring a copied engine/event implementation or new assets.

Independent literal regressions cover inherited signed/wrapped pitch,
up/down/level shots and different yaw, versus explicit uncompressed angles;
both primary/secondary and standing/ducking. The observer's own muzzle does
not redirect the remote beam; repeat/reset create no duplicate trace/audio or
stale effects and remote firing does not punch the observer's camera. An owned
BSP-floor regression also uses the actual static query and application line
materializer: remote downward fire now hits the floor and submits descending
neutral render lines, not an upward miss. Geometry widths1.75/3.5, TTL0.15,
length144, gain3, yellow/white colors, speed12000, glow, poses and MP5 remain.

Before production edits, the existing Release Gauss baseline passed33/34 cases
with one original-asset opt-in skip and23025 assertions. The two new Debug
regressions then failed on the original code:1112 assertions,49 failures;
e.g. model pitch330 produced vertical direction+0.5 instead of-1 and no floor
impact. Only the provenance-aware angle restoration was applied in production.

Current verification:

- VS2022/Win32 Debug client/tests:104 focused cases,101 passed, three explicit
  original-asset opt-in skips;39997 assertions passed.
- Release all-target build and focused approved read-only asset/OpenGL paths:
  104 cases,40232 assertions passed; zero skips/failures.
- Unchanged core-only OFF/API/architecture configuration:41/41 CTest passed.
- Actual unchanged manual reference/RemoteAudioPeer/NoTimeLimit launcher
  CheckOnly passed;process-launches0 and normal Release selected.
- Final audited Release offline suite, parallel2:2971 registered,2924 passed,
  47 explicit capability/opt-in skips, zero failures;CTest247.22s. Complete
  `build_source.ps1 -TestOnly` exited0, including Null, transport/summary and
  no-stock launcher fixtures. No production/test edit followed this suite.

Logs and the1238-file verified pre-edit snapshot are in
`manual-artifacts/task-records/e21-remote-aim-20261006/`. Ordinary project source
changes are limited to the Half-Life Gauss callback, its regressions and this
record; lower core/host/renderer and other game policies remain unchanged.

Normal Release `build/bin/Release/hlclient.exe`:3772928 bytes, SHA-256
`90EF1E14BB99A1E54747361AFFA31F0123BAA9A3428298467251D3480C4F0C9D`.
Manual validation of this build=`not_run`;no new ASan/device-output claim.
Manual handoff is the unchanged two-client command above: observer A watches
B aim/fire up, down and level, standing/crouched, primary/secondary. The beam
must follow B's aim rather than A's view, with no second trace on repeat render.
No live/Steam/HLDS/WFP/capture launches;staging/commit/push=none.

### E21 remote Gauss explicit-angle correction — 2026-10-06

New `attribution=user_report`: the downward-fire/upward-observer-beam problem
persisted after the previous correction. That correction's inherited-only
tests passed, but its assumption that explicit Gauss angles were uncompressed
view angles was wrong. The earlier report is preserved as history, not treated
as new manual success.

Read-only inspection found candidate manual run
`ddddf8a952ec4f898f7fcff539c8ac08`: Release normal client path, crossfire,
reference, remote peer and unlimited duration; server37548, client24444,
peer40432. Its wrapper reports owned cleanup=`exact`, restoration=`scoped_exact`,
publication=`complete`, cleanup errors0. Redacted client/peer logs contain
camera samples and beam counters, not raw Gauss event angles/provenance; no
transport capture or client executable SHA was retained. The pre-edit current
file hash was90EF..., but it is not assigned retrospectively to this run.
These logs do not establish exact captured input values or a stock angle replay.

Producer evidence from the unchanged pinned SDK:

- `dlls/gauss.cpp:394` explicitly supplies `m_pPlayer->pev->angles` for fire.
- `dlls/client.cpp:1181` publishes the same `ent->v.angles` in packet entities.
- `cl_dll/StudioModelRenderer.cpp` uses the player pitch*3 convention and
  inverted pitch transform. The existing Half-Life player profile follows it.

`angles_from_entity` describes storage/wire provenance, not a universal angle
coordinate system. Remote Gauss now restores `-3*remainder(model_pitch,360)`
for both inherited and explicit values. This rule is scoped to the stock
Half-Life Gauss event callback, after local-event handling has returned.
Local command aiming, generic decoder/publisher/API, other weapons, poses,
renderer, wire/canonical contracts and server damage authority are unchanged.

The previous synthetic explicit view-angle literals were replaced with
producer-correct model-angle literals, keeping the independent forward-vector
assertions and expanding signed/wrapped/up/down/level/yaw checks to both source
flags, both attacks and both duck states. The owned BSP/application-line floor
test also covers both flags. A new independently encoded real replay test
passes packet actor2/weaponmodel71 and explicit angle fields through normal
`svc_event` and reliable framing, decoder, atomic publisher and concrete module.
It checks malformed-suffix rollback, RX-clear ownership, duplicate trace/audio
suppression and teardown. Reliable coverage exercises that publisher branch;
it does not claim the captured main fire was reliable (SDK main uses FEV_NOTHOST).

Before the corrective production edit, all three regressions failed:
2060 assertions,81 failures. The actual publisher path with independently
quantized model pitch330 degrees produced `z=+0.499972` instead of approximately
`-1`, for both wire branches. The production change removes only the incorrect
provenance-dependent pitch branch; no assertions were loosened to obtain green.

Current verification after the final source/test edit:

- VS2022/Win32 Debug client/tests:105 focused cases,102 passed, three explicit
  original-asset opt-in skips;40981 assertions passed.
- Release all-target build and approved read-only asset/OpenGL controls:
  105 cases,41216 assertions passed; zero failures/skips.
- Unchanged core-only OFF/API/architecture configuration:41/41 CTest passed.
- Unchanged reference/RemoteAudioPeer/NoTimeLimit launcher CheckOnly passed;
  process-launches0, actual normal Release selected.
- Final audited Release offline suite, parallel2:2972 registered,2925 passed,
  47 explicit capability/opt-in skips, zero failures;CTest271.66s. Complete
  `build_source.ps1 -TestOnly` exited0, including Null, transport/summary and
  no-stock launcher fixtures. No production/test edit followed this suite.

Logs and the verified1238-file pre-edit snapshot are in
`manual-artifacts/task-records/e21-remote-explicit-aim-20261006/`. Independent
read-only source review found only Gauss production and its two regression
files changed before this record update; no missing source files. SDK, branch,
HEAD and index are unchanged. Widths1.75/3.5, TTL0.15, speed12000, length144,
shape, gain/colors, glows, audio, lifecycle/dedup and MP5 remain unchanged.

Normal Release `build/bin/Release/hlclient.exe`:3772928 bytes, SHA-256
`0D0C1549E7D649FF3465D23227EB130042B93D31FD93DBC3C94FF65B60789578`.
The same manual command above remains ready. New-build manual=`not_run`;
no new ASan/device-output claim, no live/Steam/HLDS/WFP/capture launches,
staging/commit/push=none. User listening/visual confirmation remains separate
from SDK-backed deterministic and production-path offline proof.

### Observer-only diameter increase — 2026-10-06

Both remote Gauss ribbon profiles multiply their diameter by 1.7: primary
2.975 units and secondary 5.95 units. Owning causal emitter identity selects
this branch; local first-person values stay 1.75 and 3.5. Non-ribbon contact
glows, yellow/white colors, 0.15 s lifetime, motion and the corrected explicit/
inherited remote aiming convention remain unchanged. All values fit the
existing neutral solid-line maximum of 6; no renderer limit was broadened.
Existing local, remote, real event publisher and owned BSP/RenderScene checks
retain their direction, timing and identity assertions with the requested
remote widths. This request supplies no new manual aim confirmation.
Shared final build verification is appended to E20's flight follow-up.
