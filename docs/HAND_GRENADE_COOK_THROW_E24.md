# E24 — Hand Grenade prime, cook and throw

## Subsequent manual acceptance (2026-10-07)

`E24 manual=passed; attribution=user_report`. The E25 request supplies
**«Протестував все ок»** for the performed Hand Grenade scenario: prime/cook/release,
server grenade, bounce/explosion, remote/last-grenade lifecycle and normal client
stability at user-report scope. No exact timings, listening measurements or
executable hash are attributed to it. Server scorch remains visually unsupported.
The original offline preparation record and its manual fields below are historical.

## Original offline preparation record

Status: `hand_grenade_cook_throw_integrated`; final offline acceptance passed.
`manual=not_run; new_live_sessions=0; staging/commit/push=none`.

## Source preservation and baseline

Authoritative worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`, remote
`https://github.com/Pvitaly91/hl-client-engine.git`.
Existing E11–E23 and BSP-seam work is retained; no checkout, reset, cleanup,
stash, rebase, amendment or other-worktree edit. The index was empty at start.
No conflicting build or live executable was running at the initial check.

Existing source-snapshot procedure saved and SHA-256 verified 1256 ordinary
tracked/untracked source files before edits under
`manual-artifacts/task-records/e24-hand-grenade-20261007/source/`, with
`source-manifest.json`. Pre-edit Release `[e23],[e22],[e16]`: 118 cases,
113 passed, 5 optional-asset skips, 16100 assertions (seed 659576652).
Baseline Release SHA-256:
`5B09C1E540AE95E4CA7CC61D9363BA9963C9F63E8750BE000C03EED327DF5CAD`.
This hash is not attributed to an unverified historical user session.

E23's subsequent **«Протестив все ок»** acceptance is recorded separately as
`manual=passed; attribution=user_report` in its existing task record. Historical
offline `manual=not_run` statements were not rewritten into live proof.

## Audit and pinned behavior

Before E24 the generic catalogue could expose a server-owned grenade and generic
entity/audio/explosion paths existed. There was no validated grenade-specific
local prime/cook/throw controller, no no-clip grenade HUD/retirement policy, and
weapon-slot fuser2/3 were not projected to the game boundary. No grenade firing
event existed; adding an event would not fix these missing paths.

Reference is the already pinned official Valve SDK
`b1b5cf5892918535619b2937bb927e46cb097ba1`, without submodule update or copied
implementation. Behavioral references: `dlls/handgrenade.cpp`, `dlls/ggrenade.cpp`,
`dlls/weapons.h`, `dlls/client.cpp`, `cl_dll/hl/hl_weapons.cpp`,
`network/delta.lst`. No Xash/ReHLDS or proprietary engine sources used.

| Concern | Verified profile / owner |
| --- | --- |
| Identity | ID 12, `weapon_handgrenade`, ammo `Hand Grenade`, maximum 10, default give 5; server grants it |
| Inventory | slot 4, position 0; no clip, exhaustible/limited-in-world; no local ammo grant |
| Models | `models/v_grenade.mdl`, `models/p_grenade.mdl`, **`models/w_grenade.mdl`** |
| Local roles | idle 0, fidget 1, pinpull 2, throw1/2/3 3/4/5, holster 6, draw 7; labels validated before use |
| Primary | one pinpull, hold, release; earliest throw 0.5 s after prime |
| Secondary / reload | no grenade secondary mode or magazine reload; no invented effects |
| Fuse reference | `max(0, prime_time - throw_time + 3)`; diagnostic only |
| Overhold | holding beyond 3 s does not auto-spawn/explode; released server grenade may detonate immediately |
| Holster | blocked while primed/release pending; server correction/selection remains authoritative |
| Timed world entity | MOVETYPE_BOUNCE (10), server origin/angles/sequence/model; no client physics |
| Bounce audio | committed `weapons/grenade_hit1..3.wav`, server entity/channel/position/volume |
| Explosion | committed TE_EXPLOSION; existing explosion sound family and server debris audio |
| Smoke | subsequent committed TE_SMOKE; no local smoke/fuse scheduling |
| Holster stop | `common/null.wav` ordinary same-entity named-channel replacement, not a global mute |

The v-model's approved actual metadata is 49 bones, 0 controllers, 0 attachments,
2 bodyparts, 1 skin family, 8 sequences. Frame counts are
91/76/25/19/19/19/16/16; FPS 30/30/45/13/20/30/30/30. Original resources are
read-only additional controls, never repository fixtures or build dependencies.
The p-model has 12 bones, one bodypart/skin and one single-frame idle sequence;
the w-model has one bone/bodypart/skin, idle plus roll1/roll2 (61 frames, 15 FPS)
and four toss sequences (61 frames, 10 FPS). All three have no controllers or
attachments. Their mesh-local bounds (not posed world bounds) were printed by
the approved importer control, not guessed from a fallback model.
Source-vertex minima/maxima are v: `(-1.01,-2.81,-3.31)` / `(12.35,3.49,3.11)`,
p: `(-2.20,-2.68,-0.92)` / `(0.69,1.76,1.73)`, and w:
`(-7.09,-8.61,-2.96)` / `(2.22,5.67,5.56)`. Remote p-model merge matched 11 of
12 child bones by name; the unmatched root uses the existing merge fallback.

`ShootTimed` overrides the generic grenade entity's initial `grenade.mdl` with
`w_grenade.mdl`; resource slots are session-assigned, not constants. Its server
tumble sequence 3–6, gravity/friction, bounce/rest, explosion hide/remove and
short remaining-fuse behavior are observations, not reimplemented simulation.

## Production path and ownership

`main.cpp` captures optional owning view/punch/velocity data once for each newly
submitted immutable wire command. Existing angle encoding, eye/direction vectors,
20-ms scheduling, history and replay remain unchanged. The normal live owner
now forwards every newly TX-submitted command through its existing 128-entry
FIFO: its previous attack-bit filter dropped LMB release and later neutral ticks,
which would prevent quick-release completion in the real application. Backup
commands, repeated rendering and movement replay do not re-notify submissions.
Fake-peer tests check exact wire bytes, timestamps, RX and reference/off delivery.
`GameClientHost` forwards
through the same GameClientAPI to `HalfLifeClientModule` / `HalfLifePresentation`
/ `LocalWeaponPresentationController`.

`hand_grenade_profile.hpp` validates the exact v-model roles and computes only
presentation classification: combine view+punch; map pitch to
`-10 + pitch * (80/90 if pitch<0 else 100/90)`; speed
`min(1000, (90-mapped_pitch)*6.5)`; throw1 below 500, throw2 below 1000,
otherwise throw3. Frozen context retains command identity, angles and optional
reference velocity (`forward*speed + observed player velocity`). None of these
values move a world entity or assert the server's actual hit/fuse outcome.

The one session-owned controller tracks ready, primed, release_pending, throwing,
post_throw and retired. State progression uses submitted command timestamps,
not render FPS. Render/update/replay does not repeat prime or release effects.
Missing/invalid context does not invent a throw. Reconciliation consumes
server observations and genuine svc_weaponanim, never a fabricated `.sc` event.
Fresh clientdata throw poses remain authoritative when an explicit fuser clear
ends a predicted prime, including a server throw before the local minimum and
without svc_weaponanim. Rejecting a new prime cannot release the reservation
of an earlier outstanding throw; both fuser and service-idle paths are covered.
Provisional reservations bound the number of unconfirmed throws; HUD always
uses the assigned server AmmoX slot. Last-grenade presentation cannot fall back
to an old idle grenade; inventory/group/wheel exclude confirmed zero-ammo ID 12.
Fresh later pickup restores usability without changing other no-clip weapons.

Schema-based weapon fuser2/3 are signed 22-bit floats with multiplier 128 in the
pinned descriptor. They are optional neutral `auxiliary_float2/3` values plus
exact current-record presence flags. Existing parsed delta masks are retained
as bounded owning metadata; inherited fields do not masquerade as explicit
clear events. Absent fields stay absent. Clientdata fuser2/3 (Gauss timers) are
separate. Absolute server throw times use committed server-time mapping, not
local epoch or countdown assumptions. Canonical hashes and wire grammar do not
change; malformed suffix/duplicate/reset preserve atomic publication.

World path remains server packet entity -> generic kinetic interpolation ->
Studio render frame -> renderer. Bounces are bounded interpolation between
authoritative points, not extrapolated gravity. Multiple grenades survive the
shooter death when the server retains them; map/session reset and entity reuse
retain existing lifetime rules. Remote p_grenade uses server weapon_model_index
and E11 bone merge/server pose; it does not infer the remote cook timer.
E13/E14 remain the only death/corpse/respawn and score/death-notice presentation.

No new local pinpull/throw/fire sound, muzzle flash, casing, tracer or local
projectile. Server sounds go through approved async decode and the existing
bounded spatial mixer. There is no extra audio device or per-frame file I/O.
Approved bounce samples are mono 22050 Hz, with 14322/20841/10953 samples;
debris samples are mono 11025 Hz, with 13971/9999/29922 samples. Each of the six
produced finite nonzero isolated PCM through `ApprovedSoundAssets -> ServerAudio
-> mixer`; this does not depend on an unrelated gunshot masking a missing voice.
The approved `common/null.wav` is 11025-Hz mono, 1103 samples at the two adjacent
8-bit silence quantization levels -1/128 and zero (absolute peak 1/128, measured
AC RMS 0.000904845842), not mathematical PCM zero or a constant DC level. Its
quantization-bound control and named-channel replacement are tested;
15 samples use -1/128 and 1088 use zero. The mixed right-channel bound includes
the actual 140/255 volume, 51/64 attenuation, 50-unit distance and pan gain;
the left channel is zero. Replacing the prior audible named-channel voice is
checked separately, so low null energy cannot mask failed stop semantics.
the decoder is not changed to special-case a Half-Life filename. Owned fixture
silence remains exact zero. This is signal evidence, not a listening claim.

## Explicit limits

- `grenade_scorch_status=server_message_unsupported` at presentation: existing
  exact temporary-decal framing stores an owning wire observation, but it is not
  materialized as a server scorch draw. No locally guessed scorch added.
- Wire masks prove explicit updates, not acknowledgement of a particular
  usercmd. Netchan carrier ACK is not compared blindly to command sequence.
  Retained unchanged zero alone cannot prove rejection of a new prime.
- Local throw strength/timing is presentation/reference behavior, not a claim
  of proprietary binary parity or independent grenade physics.
- No E25, satchel, tripmine, snark, spectator or new gameplay authority.

## Changed source paths

- Normal host: `apps/hlclient/main.cpp`, `src/goldsrc/live_runtime_stage.cpp` and
  `include/hlclient/game_api/presentation.hpp` capture owning command context and
  deliver release/neutral commands without changing their wire bytes.
- HL policy: new `include/hlclient/games/halflife/hand_grenade_profile.hpp`;
  existing `local_weapon_presentation.hpp/.cpp`, `presentation.hpp/.cpp`,
  `client_module.cpp` and `inventory.cpp` implement grenade state/HUD/selection.
- Neutral observations: `runtime_observation.hpp/.cpp`,
  `delta_value_decoder.hpp/.cpp`, `client_data_decoder.hpp/.cpp` and
  `runtime_replay_session.cpp` project existing negotiated fields and freshness.
- Tests: `test_hand_grenade_local.cpp`, `test_hand_grenade_observations.cpp`,
  `test_hand_grenade_world.cpp`, `test_hand_grenade_assets.cpp`, two owned grenade
  fixture headers, the existing `test_resource_client_response_stage.cpp`,
  `tests/CMakeLists.txt` and `tests/core_api/CMakeLists.txt`.
- Documentation: this record, `ARCHITECTURE.md`, and the appended E23 acceptance.
  Renderer, mixer, grenade world physics, SDK sources and launcher were not edited.

## Verification

The first concurrent Debug/core-only build attempt exhausted compiler/commit
memory (C1060 and MSBuild OutOfMemory, not a C++ semantic diagnostic). Existing
caches specify `/MP4`; two parallel projects in each tree could spawn sixteen
compilers. The failed attempt's verified orphan compiler children were stopped;
no unrelated process was touched. Retries use `--parallel 1` with process-local
`_CL_=/MP1`, without cache reset or source/build cleanup. After verifying more
than 5 GB free commit, separate trees were limited to two compilers total.

Core-only Release: build succeeded; 41/41 offline CTests passed (36.02 s),
including architecture guard. `hlclient_core_api_tests`: 249 cases / 125556
assertions; isolated `[e24]`: 5 cases / 223 assertions. Cache verified
`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`, VS2022 Win32 and this source worktree.
Actual generated source/link lists contain neither `games/halflife` objects nor
`hlclient_game_halflife` linkage. Logs: `core-build-serial.log`, `core-tests.log`,
`core-e24-observations.log` in the E24 task-record directory.

Focused Debug regression: 541 cases registered, 529 passed, 12 optional-asset
skips, 346152 assertions. It covers E13–E23 tags, weapon presentation, prediction,
player-walk, game API, live fake-peer host and clientdata. First E24 Debug pass:
41/42 cases passed; the sole optional actual-asset case exposed test assumptions
(literal-zero null PCM and visibility of every sampled early animation frame),
not an owned-fixture/gameplay failure. A follow-up asset check disproved constant
DC too: the real sample uses two neighboring 8-bit silence levels. The controls
therefore check the actual quantization range, not zero AC. Every pinpull/throw
sequence has verified visible GPU frames. Failed diagnostic logs are retained;
final acceptance requires the corrected asset control to pass.

Scoped ASan Debug application/core API build passed with the final production
controller object newer than its source. Both `hlclient --help` and core API
`[e24]` probes exit **0xC0000135** before any output (0 bytes). Therefore
`asan_application_runtime=not_verified; asan_core_api=not_verified` due to the
existing loader dependency blocker. Successful compilation is not sanitizer
runtime proof; no unrelated ASan infrastructure repair was attempted.

Final production edit: `2026-10-07T01:26:29+03:00`. Normal Debug/Release and the
scoped ASan rebuild include this edit. Subsequent changes only corrected the
new optional asset fixture's actual PCM expectations and C++ type checking;
no old assertion was weakened and no production audio behavior was changed.

Final Debug E24 + E13–E23/prediction/host regression (without installed assets):
576 cases, **563 passed, 13 explicit optional-asset skips**, **347020 assertions**.
Final focused E24 in **each Debug and Release** with exact approved read-only
resources: **44/44 cases, 3256 assertions, no skips/failures**. Production GL
reports visible sampled frames for pinpull/throw1/throw2/throw3: **8/1/1/3**;
also checked actual remote p-model contribution, w-model pose/tumble, server
explosion/smoke and return to baseline. Six actual bounce/debris WAVs produced
isolated nonzero finite PCM. Logs: `debug-offline-final.log`,
`debug-e24-final.log`, `release-e24-final.log`, `asan-*-final.*`.

The unchanged launcher `-Prediction reference -RemoteAudioPeer -NoTimeLimit
-CheckOnly` passed and printed `process-launches=0`: no runner, game, Steam API,
WFP or sockets. Its selector resolves this worktree's normal Release executable.
Launcher SHA-256 remains
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.

Incremental verification commands (existing VS2022 Win32 trees; no clean):

```powershell
$env:_CL_ = '/MP1' # process-local compiler cap; restore after build
cmake --build build --config Debug --target hlclient hlclient_tests --parallel 1
cmake --build build --config Release --parallel 1
cmake --build build-core-g1 --config Release --parallel 1
ctest --test-dir build-core-g1 -C Release --output-on-failure --parallel 1
pwsh -NoProfile -File scripts/build_source.ps1 -Configuration Release -Parallel 1 -TestOnly
```

The core-only existing cache has `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`. For the full
offline run, `HLCLIENT_LOCAL_GAME_ROOT` is unset and WFP capability opt-in is 0;
the independent short SDL output capability test is explicitly enabled. Local
asset tests above use only the exact approved research root read-only.

Final full Release offline suite was run once after the last production edit
and after the final focused Debug pass. Result: **3041 passed, 49 explicit skips,
0 failed, 3090 registered CTests**, 477.77 s. Recorded Catch assertion executions
total **1178492**, including the core aggregate and separately registered cases
(not a count of unique assertions). The script also passed normal application
version/help/Null bootstrap, deterministic transport self-test, functional-log
and summary roundtrips, complete no-stock launcher fixture, and source-only
`-CheckOnly`. `release-offline-final.log` ends with
`Source build/offline verification passed: Release` and exit 0.

Explicit skip categories: 26 approved-asset opt-ins absent; 14 symlink/reparse
capabilities unavailable; one custom reparse-tag fixture unavailable; one second
writable-volume fixture unavailable; four hidden-SDL focus/relative-mouse cases;
three dynamic-WFP opt-ins deliberately disabled. No owned E24 fixture skipped.
Three passing tests additionally disclose partial omissions (elevated preflight
cases, the existing campaign-root corpus, and one symlink assertion); these are
not counted as extra CTest skips. Actual short SDL output capability passed.
This is backend callback/output evidence, not a user listening confirmation.

Final source preservation: all 1256 original snapshot files remain present;
snapshot hashes match. E24 changed 21 existing ordinary files and added eight;
all other preserved source files are unchanged. `git diff --check` passed,
index empty, branch/HEAD unchanged. No Git staging/commit/push, source cleanup,
game-installation edit, new live/HLDS/stock/Steam/WFP session or capture.

Owned fixtures cover local command states,
correction/replay/input/lifecycle, negotiated fields and transactions, BOUNCE
interpolation, remote p-model, spatial isolated PCM and production OpenGL.

All E24 manual fields remain `not_run`: prime, release, quick throw, cook,
overcook, throw strength, bounce, remote, explosion, last weapon, kill/respawn.
Offline pixels/PCM are not user visual/listening confirmation.

```text
handgrenade_prime_manual=not_run
handgrenade_release_manual=not_run
handgrenade_quick_throw_manual=not_run
handgrenade_cook_manual=not_run
handgrenade_overcook_manual=not_run
handgrenade_throw_strength_manual=not_run
handgrenade_bounce_manual=not_run
handgrenade_remote_manual=not_run
handgrenade_explosion_manual=not_run
handgrenade_last_weapon_manual=not_run
handgrenade_kill_respawn_manual=not_run
```

## Manual handoff

The unchanged normal application launcher selects
`build\bin\Release\hlclient.exe`; no debug/demo flag or resource copying needed.
Actual file: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
3847168 bytes, built `2026-10-06T22:28:42.1072024Z` (01:28:42 local).
SHA-256:
`491745E1770109013F67E6AB7B3ED9AD79E8506B68D37B2739F725404B78B8FD`.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Pick up/select Hand Grenade; check server grenade count and no clip. Test quick
release, 1 s and 2–2.5 s cook, then >3 s hold (no automatic explosion until
release). Compare level/up/down and moving throws, bounces and separate grenade
sounds, server explosion then smoke. Check the other client's p-model/projectile,
death/respawn, last-grenade retirement and later pickup. Verify Hornet/Egon and
other previous weapons still work. Close both windows and allow managed cleanup.
