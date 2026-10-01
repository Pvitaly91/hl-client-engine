# E8 — Half-Life local material footsteps

Current build: `manual=not_run`. Offline verification is recorded below after
the final production edit. No new live/managed/HLDS/stock/Steam/WFP session is
part of this task; staging, commit and push are not performed.

Historical attribution is preserved: E6 crowbar miss/hit/contact/decal/anchor
and Glock regressions are `manual=passed, attribution=user_report`. That does
not promote E7's partial sound report or an external map's preflight to full
manual success. E8 requires a new user listening/gameplay test. Historical
E3–E7 records and the previous external-map failed-suite record are unchanged.

## Baseline gate, before any E8 production edit

Repository/worktree: `Pvitaly91/hl-client-engine`,
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`. This is an identity check, not a
checkout/reset request. Staged changes were empty; extensive existing
unstaged/untracked implementation was preserved. A SHA-verified 1153-file
source snapshot uses the existing ignored task-record/source convention at
`manual-artifacts/task-records/e8-material-footsteps-20260930/source`.

The two prior failures were reproduced and corrected in **fixtures only**:

1. `scripts/test_research_copy_smoke.ps1` required the obsolete native literal
   `(project_mode ? "true" : "false")`. Current native eligibility correctly
   excludes optional 50HP and Fast manual runs:
   `project_mode && !options.test_start_health && !options.fast_manual`.
   The fixture now checks that stricter native predicate **and** the matching
   wrapper predicate. Production eligibility/isolation was not relaxed.
2. `scripts/test_stock_research_copy_topology.ps1` did not copy
   `stock_manual_fast.ps1` into its synthetic repository. The current capture
   wrapper always dot-sources it, including Strict mode. The missing helper
   threw before the intended version gate. The fixture copies the exact
   current helper; its exact `stock client version is not accepted.` assertion
   and topology/security checks are unchanged.

The corrected pair passed 2/2. Then the complete serial Release baseline
passed **2522 passed, 33 explicit skips, 0 failed / 2555 registrations**,
424.52 seconds, before E8 production edits. WFP capability opt-in was disabled.
Logs: `baseline-contract-fixes.txt`, `baseline-release-full.txt` in the ignored
task record. Neither failure was caused by BSP compatibility or material audio.

## Actual ownership and normal application chain

```text
completed H3/H4 quantized prediction command + actual collision ground
  -> SurfaceTextureQuery (support model/plane -> exact imported face)
  -> MovementAudioObservation through GameClientHost / GameClientAPI
  -> HalfLifeClientModule's ONE owning E7 HalfLifeMaterials table
  -> existing HalfLifeMovementAudio cadence/footstep profile
  -> owning LocalAudioBatch / LocalSoundCue
  -> ApprovedSoundAssets exact-root worker -> WAV PCM
  -> existing LocalAudio -> bounded Mixer -> existing SDL output
```

No second scheduler, materials parser/loader, movement loop, mixer or render
path is introduced. `HalfLifeMovementAudio::observe` borrows the module's
immutable table for that call, rather than retaining the old duplicate table.
Only `configure_movement_materials` changes the table at approved asset
readiness. The borrowed texture name is normalized/copied before returning.

Changed production paths:

- `include/hlclient/game_api/{movement_audio,audio,game_client_module,game_client_host}.hpp`:
  neutral support metadata/origin, owning bounded diagnostic, independent
  movement queue epoch and immutable resource-preparation view.
- `src/game_api/game_client_host.cpp`: forwards that view, still owns the
  retained host audio clock and one selected module.
- `src/games/halflife/{client_module,movement_audio}.cpp` and
  `include/hlclient/games/halflife/movement_audio.hpp`: shared E7 classifier,
  seven step categories, deterministic side/tile choice, reversible timer but
  monotonic presentation identity, explicit mute reasons and life cancellation.
- `include/hlclient/goldsrc/surface_texture_query.hpp`,
  `src/goldsrc/surface_texture_query.cpp`: exact neutral surface/material/miptex
  source IDs, real contact position, support-plane agreement and typed ambiguity.
- `src/goldsrc/live_runtime_stage.cpp`: publishes those fields and jump edge
  only after a successful command/history append. Replay uses the same seam
  with `replay=true`; it never invokes audio inside the movement kernel.
- `include/hlclient/goldsrc/local_audio.hpp`, `src/goldsrc/local_audio.cpp`:
  movement pending cues survive a weapon/model scope change, but not a life
  epoch; existing body/voice channels stop on that epoch, not on render rebuild.
- `apps/hlclient/main.cpp`: requests the bounded preparation view when the
  approved root arrives, before collision/prediction attachment, and executes
  cues through the same application audio path. `--net-trace` prints at most
  32 movement decision records per application; it does not turn on footsteps.

Tests are regular project files:
`tests/test_halflife_movement_audio.cpp`, `tests/test_server_audio.cpp`,
`tests/test_audio_api.cpp`, `tests/test_game_client_module.cpp`. No required
source or fixture is hidden in manual artifacts. Target graph is unchanged:
`hlclient_game_halflife -> hlclient_game_api`, host -> API, normal executable
composes both; `hlclient_goldsrc_audio`, collision, renderer and lower core do
not include/link the Half-Life module.

## Supporting surface, not crosshair or nearest-texture guessing

The completed movement state's ground hit establishes the model. Static
world lookup uses that actual contact's XY and a bounded feet+2 to feet−64
presentation point-hull trace in the same immutable collision package. Its
unexpanded plane must agree with both the support normal and the imported
render face. A compiled player clip plane is not analytically converted into a
source face plane: original compiled BSP hulls do not guarantee that relation.
Plane-distance tolerance remains 0.125; incompatible/ambiguous faces are rejected.
This does not perform a second physics step or alter the player's position.
Collision-plane IDs are **never** used as render-face ordinals.

The selected face retains its original BSP surface ordinal, material index
and miptex index. The sound origin is the actual ray/face contact, not eye or
viewmodel. Coplanar interior seams choose the current supporting face; a truly
shared exact edge with two different faces is `ambiguous_support` and silent.
Invalid/absent support and typed generated missing-texture bindings are not
promoted to confirmed concrete. Unknown names on a **real resolved surface**
use the explicit E7 concrete fallback.

E8's new verified scope is local dry **static world BSP** movement. The
pre-existing E3 identity-constrained brush query remains compatible; no new
dynamic-BSP footstep policy, moving-platform sound inference, remote-player
footsteps, entity/flesh hits or general entity material system is added.

## Pinned reference, material versus step category

Official SDK gitlink pin:
`b1b5cf5892918535619b2937bb927e46cb097ba1`, `pm_shared/pm_shared.c`.
The local reference directory has no nested `.git`; its parent repository
HEAD is not the SDK revision. Local content was checked against the official
exact-pin file (CRLF-normalized text equal), without updating the submodule or
copying an SDK implementation into production. Behavioral references:
`PM_CatagorizeTextureType`, `PM_MapTextureTypeStepType`, `PM_UpdateStepSound`,
`PM_PlayStepSound`. This independently authored slice is not stock-binary or
full PM_Move/RNG parity.

Primary reference: [pm_shared.c at the verified SDK pin](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c).

| E7 material | Footstep category | Approved virtual family | walk/run gain |
| --- | --- | --- | --- |
| concrete C | concrete | `player/pl_step1..4.wav` | .2/.5 |
| metal M | metal | `player/pl_metal1..4.wav` | .2/.5 |
| dirt D | dirt | `player/pl_dirt1..4.wav` | .25/.55 |
| vent V | vent | `player/pl_duct1..4.wav` | .4/.7 |
| grate G | grate | `player/pl_grate1..4.wav` | .2/.5 |
| tile T | tile | `player/pl_tile1..4.wav`, special 5 | .2/.5 |
| slosh S, on supported dry solid | slosh | `player/pl_slosh1..4.wav` | .2/.5 |
| wood W, computer P, glass Y, flesh F, snow N | concrete fallback | `player/pl_step1..4.wav` | .2/.5 |
| unknown real texture, missing/malformed optional table | explicit E7 concrete fallback | `player/pl_step1..4.wav` | .2/.5 |

There are no invented wood/glass/snow footsteps. Slosh **material** does not
enable water physics or STEP_WADE. E7 impact profiles remain separate and
unchanged, including their material gain and optional ricochet rules.

## Cadence, identity, replay and multiplayer mute

- Time advances by each completed quantized command's milliseconds, normally
  20 ms. Rendering/drain frequency and camera direction do not advance it.
  A zero timer permits a positive low-speed decision as in the pinned SDK;
  stopped motion, blocked velocity zero, unsupported/wet context, jump edge
  and airborne state cannot create a regular ground step.
- Standing walk/run cadence 400/300 ms, run threshold 210 units/s. Duck uses
  reference run threshold 80, adds 100 ms and multiplies gain by .35. Reference
  walking thresholds are 120 standing / 60 duck; the zero-timer alternative
  still permits quieter positive motion. No input speed was changed: HL1
  forward/side wish 400, Shift multiplier .3, server caps and current duck
  movement rules are untouched. Audio reads completed velocity, not keys.
- Normal crossfire is the current Half-Life **multiplayer** profile. The
  already decoded MoveVars byte is a verified Boolean protocol observation;
  true allows this profile, false disables step playback, absent stays silent.
  The SDK multiplayer non-ladder horizontal **≤220** gate is preserved:
  ordinary Shift and crouch may be deliberately inaudible, even while cadence
  and reduced gain decisions are recorded. A positive counter is not hearing
  proof. Do not diagnose these quiet modes as a missing WAV by assumption.
- Logical left/right alternates on new decisions, including multiplayer-muted
  decisions. Left samples 2/4, right 1/3. Tile's confirmed one-in-five special
  uses an event-stable hash to choose sample 5. There is no `random_device` or
  fake left/right stereo displacement. Choice is local-compatible, not stock
  shared RNG parity.
- 128 checkpoints retain **only** command boundary and remaining timer.
  Rebase restores/recalculates that timer; heard ordinal/side and command
  high-water remain monotonic. Replay never toggles them or emits a voice.
  Duplicate commands and old life/network observations are rejected. History
  exhaustion retains the timer; known initial boundary zero resets only it.
  Network/map host reset and life change clear phase/outbox, not Netchan.
- Existing E3 airborne→ground landing/fall-pain contract is retained. A landing
  command cannot also emit a regular step; the next regular interval starts
  after contact. A >580 fall may retain its pre-existing separate pain voice,
  not a second regular step. No new landing effect/physics/damage is added.
- R1 already implements validated dry ladder contact/commands and replay
  (SOLID_NOT/MOVETYPE_PUSH/CONTENTS_LADDER binding); existing E3 ladder audio
  stays 350 ms, .35 gain, same MoveVars/replay rules. E8 adds no ladder physics.
  Water/STEP_WADE remains `unsupported_due_to_movement_scope`. Prediction off
  or suspended has no completed local command at this seam and stays silent.

## Resource, output and diagnostics bounds

34 exact preparation tokens (seven four-sample families + tile5 + four ladder
+ fall-pain) are requested at approved asset readiness. The same async worker
adds `sound/` exactly once and resolves the selected world's verified root.
No invented server slot, fallback installation, callback/per-step I/O or disk
search is used. Existing six-token drain warm-up remains compatible; all 34
are already requested before normal reference prediction attaches. Pending
loads are not declared ready; existing 250 ms cue deadline forbids a later
seconds-old burst. Focus mute consumes without backlog.

Bounds remain 32 cue outbox/pending slots, 32 per-drain preparation tokens,
64 local cached references inside the 512-entry/32 MiB shared PCM cache,
512 output commands, 128 mixer voices (64 static maximum). Spatial attenuation
.8 is the existing local-effects convention in GoldSrc units; a foot source
64 units below the listener retains gain factor .9488. BODY/VOICE are separate
from weapon and automatic impact/shell/server voice identities. Mixer drop-new
and output-queue rejection remain distinct from successful queue submission.

Diagnostics separate material, classification source and footstep category;
owning texture key, support ID, command/generation/life/ordinal/side, grounded
state, movement mode/speed band, cadence,
gain, selected sample and typed decision/mute reason. Opt-in application output
adds actual resource state and cumulative submitted count, not an assertion
of audible playback. Existing counters distinguish pending, missing,
not-authorized, open/decode failure, queue rejection, lateness, mute and mixer
rejection. No native resource paths or per-frame spam are exported.
Stationary, airborne, jump and duplicate decisions are recorded only when the
reason changes; repeated ticks retain the diagnostic serial. Multiplayer quiet
is the verified non-ladder <=220 threshold, not an unavailable-resource claim.

## Verification and handoff

Primary result: `halflife_local_material_footsteps_integrated`.
Verification logs use the existing ignored task record named above. The final
complete serial Release run followed the **last production C++ edit** and the
last test additions: **2540 passed, 34 explicit skips, 0 failed / 2574
registrations**, 365.90 seconds. `release-offline-current.txt` is the final log;
earlier runs remain preserved and are not substituted for it.

| Check on final source | Result | Evidence |
| --- | --- | --- |
| VS2022/v143 Win32 normal Release and Debug | built successfully | `release-build-final.txt`, `debug-build-final.txt` |
| Final full Release offline suite | 2540 passed, 34 skipped, 0 failed / 2574 | `release-offline-current.txt` |
| E8 plus preserved E3 focused Release | 26 passed, 1 optional asset skip; 2480 assertions | `e8-focused-current.txt` |
| Debug audio/movement/prediction/camera/input/game/weapon/impact/HUD/lifecycle/GL/Null matrix | 432 passed, 9 explicit optional skips; 348194 assertions | `debug-regressions-current.txt` |
| Production OpenGL and Null Release fixtures | 52 passed, 2 optional installed-asset skips; 62424 assertions | `release-gl-null-current.txt` |
| Core-only OFF libraries/API/current test run | 41/41 registrations passed | `core-build-final.txt`, `core-tests-current.txt` |
| Independent TestGameClientModule and neutral API suite | 125 cases, 2579 assertions; no HL fallback | core API binary, also included in the 41-test run |
| Actual graph/include architecture guard | passed in core-only and full build; 179 generated core projects with zero concrete target/source/link matches | `game-boundary-manifest.txt`, `core-tests-current.txt` |
| Approved synthetic WAV readiness -> module first slosh step -> application LocalAudio -> production mixer | passed, no server sound slot required | final E8 focused suite |
| Isolated concrete/metal/tile/dirt/vent/slosh spatial PCM | finite nonzero signal, exact gain/origin/attenuation and direction; duplicate silent | final E8 focused suite |
| Original local 34-sample profile, read-only | passed, 136 assertions | `original-step-assets-current.txt` |
| Existing short moderate SDL device output check, no game | passed, 5 assertions; not a user hearing confirmation | `audio-device-current.txt` |
| Unchanged default crossfire launcher | `-CheckOnly` passed; resolved exact Release path below | `launcher-current.txt` |
| Optional materials/gentest BSP preflight | both passed on final Release SHA; not gameplay acceptance | `launcher-materials-current.txt`, `launcher-gentest-current.txt` |
| No-stock launcher fixture | passed reference/off, damage-respawn-check, mute, maps, paths, exact report/error roundtrip | `launcher-no-stock.txt` |
| Working-copy preservation and whitespace | 1153 snapshot source files retained, none missing; `git diff --check` exit 0; index empty | final read-only audit |

The full suite's capability skips are explicit, not blanket test suppression:
link/reparse/UNC features unavailable to the fixture, opt-in original assets,
captured SDL focus/cursor capability, and disabled active WFP/guard canaries.
Installed-assets and audio-device controls above were separately enabled and
run read-only. Mandatory tests use only project-owned fixtures. Expected injected
protocol/module errors in compact regression output are negative-path proofs,
not a real live session or a hidden failed test.

An initial unfiltered core-only CTest invocation ran before eight registered CLI
tools had been built: 39 CLI registrations were missing/not runnable, while API
and architecture passed. `core-tests.txt` is retained. Building those exact
generic CLI targets (without HL module) fixed the invocation inputs; final
`core-tests-current.txt` is 41/41 green. No test expectation/guard was weakened.

ASan final Debug compilation succeeded (`asan-build-final.txt`), but runtime
is **not confirmed**: the final test binary `--help`, focused E8 test invocation
and normal ASan client `--help` all exit `-1073741515` (`0xC0000135`) with empty
output, before any test/Steam initialization. Exact missing loader dependency
was not established in this task. This is not an ASan pass or a new proven
memory defect; the historical silent exit-1 report remains historical, not
rewritten. Current logs: `asan-help-current.txt`, `asan-focused-current.txt`,
`asan-client-help-current.txt`. No ASan infrastructure redesign was undertaken.

Final normal executable:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
SHA-256: `B4B2B07B95CE88BA4421975120AF11F955B0F3D5BCE90BE65A95BD0065517A89`.
`Get-H2ManualConfiguration.ClientPath` resolves exactly that file, map=crossfire,
prediction=reference. The launcher is byte-for-byte identical to the preserved
source snapshot. Earlier task/user-session SHAs are not assigned to this build.

Use existing VS2022/v143 **Win32** incremental trees; never clean/recreate them:

```powershell
cmake --build build --config Release --target hlclient hlclient_tests hlclient_core_api_tests --parallel 1
cmake --build build --config Debug --target hlclient hlclient_tests hlclient_core_api_tests --parallel 1
ctest --test-dir build -C Release --output-on-failure --parallel 1
cmake -S . -B build-core-g1 -DHLCLIENT_BUILD_GAME_HALFLIFE=OFF
cmake --build build-core-g1 --config Release --target hlclient_engine_core hlclient_core_api_tests hlclient_goldsrc_sound_assets --parallel 1
cmake --build build-core-g1 --config Release --target hlclient_runtime_control_check hlclient_entity_baseline_check hlclient_entity_snapshot_check hlclient_clientdata_check hlclient_runtime_replay_check hlclient_usercmd_check hlclient_goldsrc_asset_check hlclient_entity_viewer --parallel 1
ctest --test-dir build-core-g1 -C Release --output-on-failure --parallel 1
```

Default user-run command, unchanged **crossfire** (one command, one session):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Optional material/general QA only; not mandatory game assets or CI dependencies:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\_materials_valve.bsp"
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\gentest.bsp"
```

Run over actually available concrete/metal/dirt/vent/grate/tile/slosh floors;
compare ordinary W movement, stop/start, turns, diagonal, Shift and duck policy,
jump/air/landing, texture seams and slopes. Quiet Shift/duck is intentional in
this multiplayer profile. Mark absent zones N/A. Check Glock/crowbar, decals,
flash/light/shell contact, HUD, camera and movement as regressions. Original
audio is still subject to the user's final listening confirmation.

Final task status: E8 `manual=not_run`; new live/managed/HLDS/stock/Steam/WFP
launches=0; staging=none, commit=none, push=none. No installed game assets,
research configuration, server precache, Steam files or other worktrees were
changed. No existing builds were cleaned and no foreign process was stopped.

## 2026-09-30 corrective continuation: Crossfire ramps and strafe onset

The preceding completion report/counts/SHA remain historical. The new manual
report is `attribution=user_report`: `ramp_footsteps=disappear_up_and_down`,
`strafe_footsteps=delayed_on_A_D`. This is not blanket E8 acceptance. The screenshot
has no executable SHA; the new build below is not attributed to that session.

The closest read-only run by time is `cb0302ac1aad4484ae1123a5637136a4`,
2026-09-30 14:17:52 UTC, server PID 25216/client PID 36476, Crossfire/reference/
keyboard-mouse/direct-loopback. It retains grounded world support normal
approximately (0.447214, 0, 0.894427), playback-ready audio, application completed,
client/server exit 0, exact owned-process cleanup and scoped-exact restoration.
The wrapper separately reports diagnostic-publication incomplete; that is not
proved to cause the footstep defects. No step/surface diagnostic or client SHA
is retained there; the screenshot is not definitively assigned to this run.

Both defects were independently reproduced before production changes:

- `SurfaceTextureQuery::at_support` subtracted the ideal +/-16 XY and standing/
  duck Z hull expansion from a compiled player plane. Original Crossfire slopes
  differ from that assumption by approximately 3.10–6.71 plane units. With the
  strict 0.125 face check, the read-only control found **0/24** inspected ramp
  materials. Independent point-hull lookup now finds **24/24**, without widening
  tolerances, guessing a nearest face, changing movement, or editing BSP assets.
- `HalfLifeMovementAudio::observe` armed a complete quiet 400 ms interval on the
  first positive acceleration tick. Crossing the confirmed multiplayer audible
  threshold on the next command inherited that delay. The new local-compatible
  `quiet_onset_pending` timer phase releases only a previously *due, quiet*
  decision when supported speed exceeds 220 and MoveVars permits footsteps.
  It is checkpointed/replayed, cannot create replay voices, and does not bypass
  an already heard step's cadence on direction changes/speed fluctuations.
  Physical acceleration delay and intentionally quiet Shift/duck remain.
  This onset refinement is explicitly **not stock-exact timing parity**.

Production ownership and integration:

- `include/hlclient/goldsrc/surface_texture_query.hpp` and
  `src/goldsrc/surface_texture_query.cpp`: the neutral lookup retains the existing
  immutable collision package plus bounded scratch, used only on the session
  thread (no concurrent calls). Missing point-hull source fails closed; brush
  stable-identity/transform lookup is unchanged.
- `src/goldsrc/live_runtime_stage.cpp`: normal session surface attachment receives
  the already approved collision package used by prediction/camera. Main already
  attaches collision before surfaces; no new asset I/O, loop, key sounds or flags.
- `include/hlclient/games/halflife/movement_audio.hpp` and
  `src/games/halflife/movement_audio.cpp`: only HL presentation timing owns the
  quiet-onset rule. Kernel, wire, inputs, command history, material families,
  gains, regular 400/300 ms cadence, ladder and landing policies are unchanged.
- `tests/test_halflife_movement_audio.cpp`: independent point-hull fixtures replace
  the invalid analytic-hull assumption; unchanged face/material/position/cadence
  assertions remain. New tests cover deliberately offset compiled player planes,
  up/down motion, audible onset, recent-step cadence, missing/false MoveVars,
  checkpoint replay/duplicate suppression, and optional original Crossfire.

Before editing, seven touched source/doc files were copied and hash-verified via
the existing ignored `manual-artifacts/task-records/e8-slope-strafe-fix-20260930/source`
snapshot convention. Required code/tests/docs remain ordinary project files.
No Git index/branch/history operations or changes to other worktrees occurred.

Verification on this corrective source (details in the same task-record folder):

| Check | Result |
| --- | --- |
| VS2022/v143 Win32 Release and Debug | built, `release-build-final.txt` / `debug-build-final.txt` |
| Final full Release offline suite, after last production edit | 2543 passed, 35 explicit skips, 0 failed / 2578, 425.66 s; `release-offline-final.txt` |
| New regressions plus optional original Crossfire | 4/4, 992 assertions; 24/24 ramp supports; `fix-final.txt` |
| Project-owned E8/E3 focused Release | 29 passed, 1 explicit optional asset skip, 3463 assertions |
| Debug audio/movement/prediction/B1/G1/HUD/GL/lifecycle matrix | 435 passed, 9 explicit skips, 349177 assertions |
| Production OpenGL/Null Release | 49 passed, 2 optional asset skips, 62246 assertions |
| Core-only OFF/API/architecture/generic CLI | 41/41 registrations; no concrete HL source/link dependency |
| Default launcher `-CheckOnly` and no-stock fixture | passed; reference/off/scenario/optional parameters preserved |

Core-only generated graph and all 179 recursive VS project source/link lists
contain no concrete Half-Life source/target dependency. `git diff --check`
passes; index remains empty. Launcher SHA is identical to the pre-E8 snapshot;
its configuration resolves exactly the new normal Release executable below.
Full-suite skips are explicit optional local assets, WFP/active canaries, and
link/SDL-capture capabilities, not hidden failed tests. The optional original
Crossfire ramp test was separately enabled/read-only and passed as recorded.

ASan Debug compiled on this source, but runtime remains unavailable: tests
`--help`, E8/E3 filter, and client `--help` all exit `0xC0000135` with empty
output before testing. This is not an ASan pass. No infrastructure repair,
device listening claim or new game/Steam/server session was used for this fix.

New normal Release path is unchanged:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
SHA-256: `00F8036B894BC71027451C3C4818C42424490437A37D1C75552B92095BF6EFE2`.
New build `manual=not_run`. Use the unchanged command above; check this ramp
up/down, A/D stop/start, then recent-step direction changes, Shift/duck/jump,
ladder/landing and Glock/crowbar regressions. New live sessions=0;
staging/commit/push=none.

## Subsequent user acceptance recorded during E9 (2026-09-30)

`attribution=user_report`: "перевірив зі звуками тепер все ок" accepts the
reported slope and A/D sound corrections in the user's tested scope. It does
not establish every material/map, prediction-off, ASan or two-client playback.
The historical `manual=not_run` reports above remain unchanged. No executable
SHA was supplied by the user; the E9 pre-edit baseline was independently
hashed as `00F8036B894BC71027451C3C4818C42424490437A37D1C75552B92095BF6EFE2`.
E7 acceptance is not upgraded by this statement. New E9 Release requires its
own manual verification.
