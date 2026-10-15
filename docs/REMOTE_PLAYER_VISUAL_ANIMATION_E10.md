# E10 — remote player visual / Studio animation

Task: HLC-E10-REMOTE-PLAYER-VISUAL-STUDIO-ANIMATION, 2026-09-30.
Worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`; starting HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`. Existing newer dirty E3–E9 work
is preserved. Before edits, 1263 source inventory files were copied and
hash-verified using the existing source-snapshot convention under
`manual-artifacts/task-records/e10-remote-player-visual-20260930/source`.

## Actual boundary and production path

Previously normal `hlclient.exe` called `RuntimeReplayLocalAssets::project_entities`
only on entity publication and evaluated a discrete Studio frame. The existing
synthetic interpolation/composer did not serve this normal path. E10 does not
relabel a live observation as synthetic evidence or add a second renderer.

| Before | E10 owner / actual path |
| --- | --- |
| animtime/framerate/gait lost in semantic projection | `src/goldsrc/runtime_replay_session.cpp` projects exact advertised neutral fields |
| snapshot-only transform and frozen pose | existing `EntitySnapshotInterpolator::interpolate_runtime`, owning presentation sample |
| whole-body pitch; no separate gait or transition | `HalfLifeRemotePlayerPresentation` inside the selected `HalfLifeClientModule` |
| one pose sample | shared `StudioPoseEvaluator::compose`, main / previous / masked local layer |
| imported static bounds | final selected skinned `posed_bounds` |
| fixed Studio lighting | optional neutral RGB from retained world mesh/lightmap; existing point light and absent-light fallback |

Normal composition is:
`main -> RuntimeReplayLocalAssets::present_entities -> EntitySnapshotInterpolator
-> GameClientHost / IGameClientModule::remote_player -> HalfLifeRemotePlayerPresentation
-> StudioPoseEvaluator -> EntityRenderFrameBuilder -> ClientWorldState
-> RenderScene -> existing OpenGL renderer`.

Targets: `hlclient_game_api`, `hlclient_game_client_host`,
`hlclient_game_halflife`, `hlclient_entity_interpolation`,
`hlclient_studio_pose`, `hlclient_entity_scene_render`,
`hlclient_application_runtime_replay` and the existing renderer. Lower targets
depend on API/common mechanisms, not the concrete module. Module/API headers
are `include/hlclient/game_api/remote_player.hpp` and
`include/hlclient/games/halflife/remote_player.hpp`; implementation is
`src/games/halflife/remote_player.cpp`. One session-owned module retains numeric
state/masks/fingerprints only; model/entity references are synchronous immutable
borrows and never outlive a call. No audio/effects or transport execution callback
is introduced by this API. Alternate test module uses the same host without HL
fallback; it is not evidence of CS support.

## Fields, identity and model binding

The checked local official SDK revision is
`b1b5cf5892918535619b2937bb927e46cb097ba1`, not floating master.
`network/delta.lst` player schema and the existing validated decoder provide:
origin, angles, modelindex, sequence, raw frame, unsigned DT_TIMEWINDOW_8 animtime,
signed DT_FLOAT framerate, four controller bytes, two blend bytes, body/skin,
gaitsequence, weaponmodel, effects and movement fields. New optional projections
require exact descriptor types. Missing descriptors stay unavailable; no guessed
default, opcode scan/resync, proprietary dump or alternative-engine code is used.
Malformed suffix and duplicate publication preserve the existing atomic boundary.

The pinned player schema does **not** advertise ordinary velocity. Exact optional
velocity descriptors can be projected when explicitly advertised, but gait here
uses the committed position trajectory, not basevelocity or receiving-client
clientdata velocity. Frame/gait fields are not inferred from renderer FPS.
Historical A–H canonical and I visual hashes remain unchanged; the explicit
`runtime_observation_visual_hash_v2` covers the added optional fields. Render
timeline identities use exact entity source record IDs, because historical hashes
can intentionally stay equal across animation-only records.
Source record IDs are opaque equality/dedup keys; committed record ordinals,
not numeric ID ordering, determine stale sources and slot continuity floors.

Player identity requires the exact player schema, server max-clients range and
receiving entity `client_slot + 1`. Local body is excluded before materialization.
Without receiving identity the HL policy is unavailable. Models follow exact
entity slot -> type-local manifest slot -> approved importer/library -> immutable
EntitySceneRenderPackage. No numeric model slot or `models/player.mdl` fallback.
Body and skin selected by the pose intent are also used by material/draw selection.
Their discrete presentation sample remains the previous appearance until alpha1;
the current animation anchor and the frozen previous-transition appearance stay
separate from that selection.

`weaponmodel` is observed but merged p_weapon attachment is not implemented.
Userinfo model remapping/top-bottom colormap parity remains unavailable; it is not
fabricated from length-only/private userinfo observations. Existing model binding
is preserved. No remote weapon effects are added.

## Timeline, gait and composition contract

The time domain is validated public `svc_time` seconds, with animtime already
decoded relative to staged server time. The host supplies explicit application
elapsed seconds anchored on the current committed source, not hidden wall time.
Two retained neutral observations are bounded presentation history, not a copy of
network delta state. HL selects delay 0.1 s, gap 0.25 s, teleport 128 units and
EF_NOINTERP mask 32. With only two observations, actual delay is
`min(selected_delay, current_server_time - previous_server_time)`; a fixed100ms
delay would snapshot-step at20ms RX. Render sample is clamped to the pair, never
extrapolated. Positions lerp; angles use shortest paths; available control/blend
bytes interpolate. Current entity set/effects/visibility remain authoritative:
removals are never resurrected. Non-player/brush transforms retain their existing
owners, including D4 prediction brush presentation. Missing/model/schema/gap/
teleport/no-interp boundaries hold current and rebaseline. Equal-time or held
pairs use current/current identity/time and alpha0.

Main frame uses metadata FPS and
`raw_frame * (numframes-1)/256 + max(0, sample-animtime) * framerate * FPS`.
Loop/clamp retain the shared evaluator's supported semantics. Signed rate0 pauses.
Sequence transition captures the prior corrected main pose once per committed
source and decays its weight over0.2s; duplicate render sampling never restarts it.
Shared composition slerps local rotations/positions, then replaces numeric masked
local gait bones, then rebuilds the same parent hierarchy. Internal1/2/4-way
sequence blending is distinct from this previous-sequence transition.

HL pitch*3 maps through actual sequence blendstart/blendend; out-of-range residual
root pitch remains explicit. Secondary sampled blending survives. Gait phase
uses distance/sequence linear movement (including backward sign), otherwise
sequence FPS*time; it persists between coherent sources. Reference gait wraps
numframes and snaps the final interval above numframes-1 to zero before shared
sampling. View vs movement yaw uses the reference-informed ±120° backward rule,
four quarter-angle torso controls from imported ranges and continuous stationary
yaw smoothing. The latter is analytic exponential rate4 for render-cadence
invariance, a documented local-compatible choice rather than stock-binary parity.

HL derives a cached numeric lower/upper mask from unique validated pelvis/spine
markers, safe hierarchy and four unique torso input0–3 rotational controllers.
Each record must declare exactly one supported XR/YR/ZR axis and be referenced
by the matching bone channel3/4/5, with validated ranges and spine ancestry.
The approved installed model control has50 bones and5 controller records:
torso records1–4 use XR/type8/channel3; the unrelated mouth record0 is not a torso
input. The initial positive control exposed and corrected an overly strict
fixture-derived Z-only assumption; topology validation was not loosened.
Unsupported or
ambiguous skeletons return typed unavailable, not a guessed general-model pose.
Renderer has no player bone names, weapon sequence meanings or gait policy.

## Lighting, culling, lifecycle and bounds

Shared materialization samples the nearest downward retained static-world triangle
within2048units, barycentric lightmap UV and bilinear selected-style atlas RGB.
Nearest unlit geometry does not borrow light from a farther floor. Probe budget is
1,048,576 triangles; reprobe after>8units motion. The 2026-10-15 Studio-lighting
corrective extends the same cache to ordinary packet models, one world-eye
viewmodel sample and 64 bounded transient slots; removed packet keys are pruned.
See `OPENGL_STUDIO_RENDERER.md`. Player lighting itself is unchanged.
No per-frame asset I/O. This is local-compatible RGB lighting, not stock-exact
LightPoint/style accumulation/directional player lighting. No sample means explicit
legacy0.85 fallback; fullbright material and existing local point light rules remain.
OpenGL receives only optional finite[0,1] RGB. Final skinned bounds include current
transition/gait; camera PVS/frustum use those bounds, not unsafe endpoint-only boxes.
An unavailable/solid camera leaf uses the existing conservative world-camera
fallback: frustum/depth remain active, no fabricated PVS row or fatal rejection.

Session/map generation remains distinct from player visual lifetime and transition
identity. Removal/reappearance/model changes reset lifetime; committed userinfo
slot boundaries drain before visual sampling, discard old interpolation continuity,
and conservatively omit a stale occupant until a fresh entity source. This does
not interpret every PVS absence as proven disconnect. Local respawn never resets
Netchan. Dead/jump poses use advertised sequence/gait, not invented remote health.
Committed queue overflow conservatively invalidates every player visual lifetime,
previous anchor and light cache before draining; it does not reset transport.
Same-generation reset clears pair/clock/light cache. Failed projection leaves the
published world unchanged; all retained-owner allocations precede publication.

Bounds:32 player states/model profiles,128 bones, two observations, existing
entity/result byte limits, three composed samples /12 internal blend evaluations,
bounded vertex work and renderer assets. Numeric status categories are
silent, ready, local_excluded, not_player, missing_fields, invalid_context,
invalid_sequence, unsupported_skeleton, stale_source. Summary retains at most32
per-frame numeric diagnostics; opt-in `--net-trace` emits at most32 remote-player
lines per session with source times, main/gait/frame, pitch/yaw, blends, body/skin,
transition weight, submitted/visible and sampled/fallback lighting. No native asset
root, paths, handles, private identity or per-frame unbounded logging. Submission
is not pixel/manual proof. No animation markers are executed as audio/effects.

## Verification and manual handoff

Verification logs live in the E10 task-record directory. Mandatory project-owned
cases cover exact field publication/absence/transaction; interpolation endpoints,
missingness/discontinuities/current set; normal20ms RX/5ms render composition;
main FPS/rate/loop/clamp, pitch/controller/internal blend, transition identities;
forward/back/strafe/diagonal, cadence-invariant gait, zero-linear-motion gait;
masked skeleton composition, skin/body, posed bounds; source/generation/slot reuse;
4/8/16 independent players; actual RGB floor probe/fallback; production OpenGL
endpoint/intermediate/masked/depth/light pixels and asset reuse. Core alternate
module returns independent neutral output and reset/teardown through the same API.
Core-only also executes the shared snapshot interpolation, Studio pose/composition
and entity-frame composer fixtures without the concrete Half-Life module.

Optional installed player control uses only approved exact-root pipeline with
`HLCLIENT_LOCAL_GAME_ROOT`, no game session or private build dependency.
It is asset/profile proof, not live remote animation acceptance. Current
Debug/Release/core-only/GL/launcher counts, SHA and skips are recorded below after
final verification. ASan runtime was previously unconfirmed (loader0xC0000135);
successful compilation does not convert it to passed.

Default remains crossfire/reference; launcher unchanged:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Two-client visual test (existing E9 mode):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer
```

User checklist: A observes B idle/forward/run/back/strafe/diagonal/standing turn,
look up/down/crouch/jump/landing; smooth transitions, no frozen/flickering pose,
stable camera/distance/culling and no duplicate local body. Reverse A/B if practical.
Regress remote footsteps, local Glock/crowbar/HUD/movement and both connections.
No new peer flag, model copy, remote weapon effect or hidden demo path is required.

New E10 build: manual=not_run. New live/Steam/HLDS/WFP/capture launches=0;
staging/commit/push=none. No stock-exact or universal mod/skeleton parity claim.

## Final verification — after the last production edit

Primary result: `remote_player_visual_animation_integrated` (offline verified;
manual visual acceptance remains `not_run`). Completed on2026-10-01 local time.
All builds use the existing VS2022/v143/Win32 configurations, with warnings as
errors. No source changes followed the final Release suite; subsequent edits
only record these results.

| Gate | Actual result |
| --- | --- |
| Final normal Release/all targets | passed; `build-release-final-3.txt` |
| Final normal Debug/client+tests | passed; `build-debug-final.txt` |
| Focused Release E10 |34 passed /2 optional skips /36 cases;1606 assertions |
| Final Debug E10/E3–E9/prediction/render regression selection |407 passed /5 optional skips /412 cases;262271 assertions |
| Full final Release CTest |2593 passed /37 explicit skips /2630 registered;0 failures;198.27s |
| Separate actual-context OpenGL regressions |29 passed;15631 assertions;0 skips |
| Approved installed-player normal-provider CPU/GPU control |1 passed;6581 assertions;0 skips |
| Core-only Release engine aggregate and API executable |passed; concrete HL module OFF |
| Core-only CTest |41 passed;0 failures, including actual graph/include/alias guard |
| Core/API Catch cases |151 passed;3774 assertions, including alternate module and shared Studio/interpolation/composer |
| Default and two-client launcher CheckOnly |passed; reference/default, peer/reference, peer/off; native dry-run launches0 |
| Actual no-stock launcher fixture |passed, including damage-respawn-check, health profile, mute/peer, foreign/incomplete reports and exit forwarding |
| ASan affected Debug compile |passed, including current client/module/materializer/renderer |
| Fresh ASan runtime `hlclient.exe --help` |unavailable, native exit-1073741515 /0xC0000135; NOT sanitizer pass |
| Final Git/source preservation |snapshot1263 files,0 missing/0 snapshot hash failures; index empty, branch/HEAD unchanged; diff check passed |

The full Release `LastTest.log` contains2475 passing Catch summaries totalling
829608 executed assertions (sum includes repeated child-fixture summaries, not
a claim of unique assertions). Ordinary CTest pass/skip counts above are exact.
The37 skips are opt-in installed assets/device playback, WFP disabled,
interactive SDL capture/focus, filesystem-link/UNC capabilities and unavailable
provider fixtures. They are listed verbatim in `release-offline-suite-final.txt`.
No OpenGL capability skip occurred in the29-case GL selection. The Debug
selection's emitted injected protocol/game failures are expected negative
fixtures, not unexpected runtime failures.

Command inventory was audited before execution:2630 registrations,0 live
commands,13 explicit offline capture-script self-test invocations and2 parser
fixtures which reject `--connect`. `HLCLIENT_RUN_WFP_CAPABILITY_TEST=0` and no
installed-game opt-in were used for the full suite. Thus its fake-peer/process
checks are offline evidence, not new HLDS/Steam/stock-client sessions.

Optional approved-model evidence: virtual resource `models/player.mdl`,50 bones,
77 sequences, validated XR torso profile. Same normal host/materializer produced
finite palettes for imported `look_idle`, `walk2handed`, `run2`, `crouch_idle` and
`jump` labels using project-owned observations. A dynamic-only actual-asset GPU
control (static-world drawing deliberately omitted, retained production lighting
unchanged) produced14316 non-clear pixels for poseA and8726 for poseB, different
framebuffer hashes, identical repeated poseB pixels/transition identity and one
Studio asset upload. This control is not stock gameplay or BSP-occlusion proof;
the mandatory project-owned GL control separately verifies world depth testing,
transition intermediates, lower/upper mask, light response and resource reuse.
Original BSP/model timestamps remained unchanged; no asset was copied or edited.

All evidence logs are under
`manual-artifacts/task-records/e10-remote-player-visual-20260930/`:
`focused-release-final.txt`, `debug-regressions-final.txt`,
`release-offline-suite-final.txt`, `release-offline-summary-final.json`,
`opengl-regressions-final.txt`, `installed-player-production-final.txt`,
`core-only-ctest.txt`, `core-api-tests.txt`, `checkonly-*-final.txt`,
`launcher-no-stock-final.txt`, `build-asan-affected.txt`,
`asan-runtime-status.txt`, `source-preservation-final.json` and
`git-source-verification.txt`. Earlier failing logs are preserved and explicitly
superseded by these final results, not rewritten as passing.

### Changed source inventory and clean-source reproducibility

Five new ordinary project files:
`include/hlclient/game_api/remote_player.hpp`,
`include/hlclient/games/halflife/remote_player.hpp`,
`src/games/halflife/remote_player.cpp`,
`tests/test_remote_player_presentation.cpp` and this document. The29 changed
existing files are listed exactly in `source-preservation-final.json`:

- neutral observations/projection: `include/hlclient/client/runtime_observation.hpp`,
  `src/client/runtime_observation.cpp`, `src/goldsrc/runtime_replay_session.cpp`;
- shared interpolation/Studio/frame mechanisms: their existing headers/sources,
  `src/entity_render/entity_scene_render.cpp` and
  `src/renderer/opengl/opengl_entity_renderer.cpp`;
- API/host/concrete composition: `game_client_module.hpp`, `game_client_host.hpp`,
  `src/game_api/game_client_host.cpp`, `src/games/halflife/client_module.cpp`;
- normal application: `include/hlclient/app/runtime_replay_local_assets.hpp`,
  `src/app/runtime_replay_local_assets.cpp`, `apps/hlclient/main.cpp`;
- `CMakeLists.txt`, both test CMake files, seven existing focused test files,
  `docs/ARCHITECTURE.md` and appended E9 user confirmation.

No required implementation lives in generated/manual-artifact files. The
unchanged launcher and old E3–E9 source/logs remain preserved. New files are
unstaged and ready for a future user-owned Git checkpoint; none was staged.

```powershell
$e10Cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$e10Ctest = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
# Normal existing configuration
& $e10Cmake --build build --config Release --parallel 2
& $e10Cmake --build build --config Debug --target hlclient hlclient_tests --parallel 2
# Existing core-only configuration: cache confirms HLCLIENT_BUILD_GAME_HALFLIFE=OFF
& $e10Cmake -S . -B build-core-g1 -DHLCLIENT_BUILD_GAME_HALFLIFE=OFF
& $e10Cmake --build build-core-g1 --config Release --target hlclient_engine_core hlclient_core_api_tests --parallel 2
& $e10Ctest --test-dir build-core-g1 -C Release --output-on-failure
```

The core-only generated manifest/source lists contain no `games/halflife` files
or `hlclient_game_halflife` dependency; the configure-time guard walks actual
direct/transitive links, aliases and project includes rather than relying on a
Glock text search. No installed-game environment gate is required for core tests.

### Final ready binary

`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`
(3457024 bytes). SHA-256:

```text
6E7F1706E3743CB3CCF8293E91105CF97278890CC09F96CB9494FDEC6211823E
```

The unchanged `Get-H2ManualConfiguration` resolves `ClientPath` to this exact
Release location; final CheckOnly checks passed against the prepared binaries.
The SHA belongs only to this E10 binary, not the earlier user-reported E9 run.
Default crossfire/reference and the exact `-RemoteAudioPeer` command above are
ready. E10 user manual validation remains `not_run`.

## Close-range user report and diagnostic follow-up — 2026-10-01

New evidence, `attribution=user_report`: on **crossfire**, an approaching remote
player disappears on the observer's screen and reappears when retreating.
The exact disappearance distance, run ID, executable hash and source/cull stage
were not supplied. This does not overwrite the historical E10 offline report or
its `manual=not_run` line; the new manual observation is a reported defect, not
an acceptance of every E10 requirement.

Work remains in the same branch/HEAD. Before this follow-up, 1268 current source
files were copied and hash-verified under
`manual-artifacts/task-records/e10-near-player-visibility-20261001/source`.
No existing source, snapshot or build tree was removed.

### What was established, and what was not

The newest retained crossfire/reference two-client run is
`8be7cc46e4e0407db1f5720f6c168ed6` (client34684, peer38444, server36500).
Both clients completed with exit0, runtime/parser errors absent, queue drops0,
GL errors0 and exact scoped cleanup/restoration. Correlation to the user's
particular approach instant is **not established**. Its retained16KB diagnostic
tails contain no approach-time player/cull observations and no raw journal.
The independent wrapper verdict `functional-diagnostic-publication-incomplete`
was caused by oversized diagnostic-line truncation; it is not evidence for the
model-disappearance cause.

The current production near plane is0.1; CPU/GPU transforms agree. No
distance-based hide branch was found. A same-leaf PVS discrepancy exists between
world/entity selection, but all1366 usable crossfire rows include their self
bit, so that discrepancy was **not** labelled or patched as this report's cause.
Possible boundaries still include committed server omission/model/effects,
game/pose readiness, PVS/frustum rejection, light sampling or the final composed
GPU image. A zero sampled RGB can make a queued model black, but this is a
diagnostic hypothesis, not an established explanation of this manual run.

### New offline controls and bounded normal-path evidence

`tests/test_runtime_replay_local_assets.cpp` adds `[e10-near]` controls through
the same host, approved importer, pose evaluator, normal materializer and
production GL renderer. Project-owned upright geometry checks center distances
128,64,48,32,24,16,8 and return128, a frustum-intersecting edge, local receiving
body exclusion, behind-camera rejection, fixed palette/light and one retained
asset upload. The approved `models/player.mdl` control separately uses the
retained crossfire package, center-origin standing placement, eye+28, identical
distances and production PVS. It is read-only and not a live gameplay replay.

Initial controls passed **without a visibility/lighting behavior correction**:
project-owned201 assertions; imported player6738 assertions. Imported player
GPU pixels at32/24/16 were55052/71422/108662; at8 also nonzero. Return128 matched
the initial non-clear pixel count (not a framebuffer equality assertion). Thus
the isolated near-plane/frustum/posed-bounds path did
not reproduce the reported defect. Dynamic-only pixel controls do not establish
the result at the user's exact map position with world/viewmodel composition.

`include/hlclient/app/remote_player_visibility_trace.hpp` supplies an owned,
allocation-free diagnostic sample and a bounded journal, not another scene or
game implementation. Normal `apps/hlclient/main.cpp` collects it only with the
existing `--net-trace` (already selected by the unchanged managed launcher),
then emits its **last16 transitions** at session end so the first32 startup pose
lines cannot consume all useful evidence. It reports source/model/effects,
game/pose readiness, PVS/frustum/frame status, actual posed bounds, camera/near,
distance and numeric static RGB. Distance48/96 bands and RGB0.01/0.12 categories
are diagnostic thresholds only; they change no culling or lighting behavior.
Stable observations within a category emit no per-frame spam. Never-seen absent
slots are silent; source absence after observation and reappearance are distinct;
generation reset clears old evidence. Records retain no buffer pointers,
native paths, SDK handles or mutable engine state.

`queued_visible`, `queued_dim` and `queued_unlit` mean CPU draw eligibility, not
GPU visibility. The follow-up remains **root_cause_unestablished** and must not
be described as a fixed close-range bug. The new diagnostic binary needs a new
manual reproduction; no live session was started for this work.

### Final verification of the diagnostic follow-up

After the last production edit, the normal VS2022/v143/Win32 Release build and
affected Debug client/test targets passed. The first Release attempt rejected a
shadowed local under `/WX`; its failing log is retained and the task-prefixed
variable correction is covered by the final builds, not suppressed warnings.

| Verification | Current follow-up result |
| --- | --- |
| Release full audited offline suite |2635 registered:2598 passed,37 skipped,0 failed;207.01s |
| Focused Release E10/near |41 cases:39 passed,2 opt-in asset skips;2077 assertions |
| Debug affected regressions |417 cases:412 passed,5 opt-in skips;262742 assertions |
| Actual production OpenGL regressions |29 cases,15631 assertions passed |
| Read-only approved crossfire/player control |1 case,6738 assertions passed after final edit; no gameplay replay |
| Core-only Release build and CTest |41/41 passed; `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` |
| Neutral core/API executable |155 cases,4044 assertions passed, including4 new visibility tests |
| Actual target/include architecture guard |passed; no concrete HL1 source/link in the core-only manifest |
| Launcher CheckOnly |default/reference, peer/reference and peer/off passed; native dry-run launches0 |

Offline test skips remain explicit capability/installed-asset/device gates;
`HLCLIENT_RUN_WFP_CAPABILITY_TEST=0` throughout. The separately opted-in approved
player test performs read-only asset operations only. The normal launcher
no-stock fixtures are included in the audited final suite. ASan was not rebuilt
or executed for this diagnostic-only follow-up; the earlier E10 runtime loader
failure0xC0000135 remains unconfirmed sanitizer coverage, not a current pass.
No blanket live-script run, fresh game/Steam/HLDS/managed session, WFP activation
or capture was performed.

The follow-up adds two ordinary source files (the owned diagnostic header and
its neutral tests) and changes five existing files: application integration,
near-player controls, normal/core-only test source lists and this record. It
does not change gameplay, wire grammar, rendering, culling or sampled light.
Evidence logs are under
`manual-artifacts/task-records/e10-near-player-visibility-20261001/`; final source
preservation verification checks every original snapshot hash, missing file,
index state and `git diff --check`. Existing unrelated modifications remain.

Current diagnostic Release executable is the unchanged launcher location:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,3464192 bytes.
Its SHA-256 (not attributed to the earlier user run) is:

```text
895A232A73DA9290327D20191FD4F3B3537B540F8B8B52CADF83CB33B7626A12
```

Next user-only reproduction uses the same two-client workflow, default crossfire:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer
```

Approach until disappearance, retreat until reappearance, finish normally and
wait for managed cleanup/restoration; retain the exact `run_id` and identify the
observer. The owned last16 transition records can then distinguish source
omission/flags, game/pose rejection, PVS/frustum rejection and queued light state.
They cannot independently prove final GPU visibility; if all close-range rows
remain queued/lit, the remaining boundary is actual composed output or state not
captured by these CPU categories. Do not call a queued row a visible model.

Result: `root_cause_unestablished_diagnostic_build_prepared`, not a completed
visibility fix. New binary `manual=not_run`; new live launches0; staging,
commit/push=none. The existing user report and previous historical E10 result
remain distinct evidence.

## Reciprocal contact screenshots and retention correction — 2026-10-01

New `attribution=user_report` on crossfire: photo1 shows the other player;
photo2 (`HLC_E9_B`) shows no other player while the user reports movement is
blocked like a wall. This is an asymmetric visual defect, not a report that
both clients lost the session. Collision is user-observed; the images alone
do not identify whether a committed entity was omitted, culled or overdrawn.

Run `2cc74118aac349b79873cf58e3e72e5a` strongly correlates: screenshot2 was saved
at02:09:55 Europe/Kyiv, run finalized02:09:59, and main/peer reserve ammo168/68
matches the photos. The copied photo1 file timestamp is not its scene time.
Terminal centers are main(201.984375,1212.109375,-1819.968750) and
peer(202.515625,1244.140625,-1819.968750), approximately32.036 units apart;
yaw89.9/-94.3 faces one another. Peer last eye is directly observed; a main
standing eye constructed from presented origin+28 is only an offline control,
not a retained historical camera sample. Terminal samples are not an exact
screenshot-time record.

Both clients completed with exit0, primary_error=none and gl_errors=0. Owned
process cleanup was exact, scoped restoration was scoped_exact, cleanup errors0.
The wrapper's diagnostic-completeness failure is separate from model visibility.
Earlier run `c340b8011ed74b08adea0486a8dd285e` ended02:09:04, before photo2, with
opcode23 runtime errors; it is a distinct failure, not established as this
newer screenshot's cause.

### Confirmed defect in the preceding diagnostic handoff

The prior application emitted an owned last16 visibility journal, but
`functional_diagnostic_excerpt` in the actual orchestrator did not retain either
new marker. Its16KiB excerpt was also filled by earlier verbose summaries,
including duplicated outcome text. Both newer client/peer logs contain zero
visibility rows; no alternate raw journal survives. The previous follow-up
verified the producer and journal but missed this downstream retention boundary.
Those tests were insufficient to establish retained normal-launch evidence.
This correction must not be labelled the cause or fix of the missing player.

Source was preserved again (1270 files, verified copies) before edits in
`manual-artifacts/task-records/e10-reciprocal-contact-20261001/source`.
The application now builds each journal line before using the existing mutex-
serialized core logger; concurrent progress logging cannot split that row.
The orchestrator correction separately prioritizes the bounded owned numeric
journal within its existing private-log limits. Neither change alters entity
selection, model transforms, lighting, prediction, collision or gameplay.

### Corrected production retention and reciprocal controls

The helper now selects the last16 distinct valid numeric journal rows and last
valid summary **before** verbose terminal lines. Its unchanged limit is64 lines,
16KiB total. Row grammar has fixed ordered keys, known stage/status vocabulary,
finite numeric/vector fields, ordered bounds and bounded RGB; individual rows
over768 bytes are rejected instead of truncated. Unknown/duplicate fields,
paths/tokens, invalid or oversized marker lookalikes cannot enter either the
numeric selection or generic error-redaction route. Optional `[info]` and CRLF
are normalized. Primary application outcome is prioritized next, preserving a
bounded prefix; outcome JSON parsing remains independent. The already retained
identical outcome is not added twice by the general priority loop.

Existing `--validate-functional-log-observation` now checks both client-role
grammars, latest16 of24 changes, duplicate suppression, all diagnostic stages,
missing-source sentinels, malformed/oversized/private-field rejection and long
competing weapon/prediction/outcome rows. A primary error2700 characters into
the outcome still survives journal reservation. Both Debug and Release native
self-tests passed with stock-processes-started0 and capture-files-written0.

The approved read-only player control now exercises receiving1/remote2 and
receiving2/remote1 at the retained centers, moving the remote origin through
separation offsets96,48,24,8,0 and back out, with ascending coherent record/time
identities. It compares per-step no-player baselines with dynamic-only and
world+dynamic images. Sequence0, gait0, controllers127, effects0 and render-mode0
are explicit project-owned controls, **not recovered historical wire fields**.
The main eye is constructed as noted above; peer eye is retained. These are not
full historical frames and do not include the normal first-person/HUD passes.

At contact, both CPU instances remain visible in camera/model leaf165, with
available PVS and a non-solid camera. World+dynamic A/B changed76164 pixels for
receiving1 and80303 for receiving2; respective sampled RGB approximately
(0.133649,0.129727,0.117963) and(0.154163,0.150242,0.138058), not zero. Dynamic-only
controls and all approach/retreat steps also produced model pixels. Final
approved-player run passed7397 assertions; original pose/interpolation/upload
assertions remain unchanged.

The first reciprocal test failed upload-count assertions because alternating
its no-dynamic baseline on the original renderer legitimately evicted that
renderer's entity cache. This was fixture interference, not a visibility
defect. Separate baseline/player production renderer instances now isolate the
control and preserve original one-upload assertions. That initial failing log
is retained; its result is not relabelled as passing.

Core-only Release engine/API build and41/41 CTest checks passed again with
`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`; no concrete HL1 source/dependency was added.
Debug neutral visibility tests passed270 assertions in4 cases. The unchanged
launcher CheckOnly passed for peer/reference and peer/off, process-launches0.
ASan was not rebuilt/run here; previous unavailable runtime coverage remains
unavailable. No new game, Steam, HLDS, WFP or capture session was launched.

### Final verification and user handoff for the retention correction

After the last production edit, the VS2022/v143/Win32 normal Release build and
affected Debug targets passed. The final audited Release offline suite ran
2635 registered checks:2598 passed,37 explicit skips,0 failed (222.40s).
Debug affected regressions passed262742 assertions:417 cases,412 passed and5
explicit skips. Actual production OpenGL regressions passed15631 assertions
in29 cases. These results are from this correction, not the earlier diagnostic
build. Logs and source-preservation verification are in
`manual-artifacts/task-records/e10-reciprocal-contact-20261001/`.

The four existing source files changed by this correction are
`apps/hlclient/main.cpp`, `apps/hlclient_stock_runtime_orchestrator/main.cpp`,
`tests/test_runtime_replay_local_assets.cpp` and this record. No second scene,
gameplay module, renderer or visibility policy was introduced. The1270-file
pre-edit source snapshot remains preserved; final verification checks its
hashes, missing originals, empty index and `git diff --check`.

The unchanged launcher selects the prepared normal Release executable at
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe` (3464704 bytes).
Its current SHA-256 is:

```text
6F8CA711C5B8356C13079B20DEE6CBA169280D2AE1790788088C559B342E0ABF
```

The Release orchestrator SHA-256 is
`AB163799A94353FFBD007144C753CA08141B2B2EC5B0504C696CA35B16672729`.
Neither hash is attributed to the screenshot run. Both binaries are required
for the corrected retained diagnostics and are ready in the usual outputs.

User-only reproduction, with no extra diagnostic flag:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer
```

Repeat the close approach and retreat while observing client B, then finish
normally and wait for cleanup/restoration. Retain its run_id/report. A new
reproduction is necessary because the preceding retained logs cannot recover
the missing visibility rows. The corrected CPU journal can locate the
source/game/pose/culling/light boundary; queued CPU output alone still cannot
establish GPU visibility or exclude a later composed-pass defect.

Result: `diagnostic_handoff_corrected_visibility_root_cause_unestablished`.
This is a partial diagnostic result, **not a completed player-visibility fix**.
Current build `manual=not_run`; new live launches0; staging, commit/push=none.

## Equal-height player origin inheritance — 2026-10-01

New `attribution=user_report`: in A the other player is visible at a distance,
disappears after approaching, and returns after retreating; B continues to see A.
The three screenshot timestamps (15:09:05, 15:09:17, 15:09:28 Europe/Kyiv) fall
inside run `dc13b19a3ef646f6b7c910a6f940a010`, requested crossfire/reference/peer,
300 seconds. The retained A journal now contains the evidence that earlier
follow-ups lacked: entity2/model153 repeatedly changes from `queued_visible`
at posed Z bounds approximately[-1696,-1624] to `frustum_culled` at[-36,36].
X/Y bounds, model, effects0, render-mode0 and positive static lighting remain
stable. Camera Z stays near-1632. The model is translated to world Z0; changing
near-plane, PVS or lighting cannot repair that input.

The run used the prior timing helper hash `EEB54327...57ABD70`; the wrapper
records no client executable hash, so no new client hash is attributed to the
photos. A completed with exit0 and primary_error=none; B was ended by owned
cleanup (exit120), with no terminal visibility journal. The wrapper reported
client-connection-rejected despite the observed gameplay; this separate session
outcome is not the cause of the visual Z jump. Owned cleanup was exact, scoped
restoration scoped_exact, cleanup errors0. No full-tree attestation is claimed.

The public protocol reconstruction omitted the local origin transfer. In pinned
official SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`, `dlls/client.cpp`
`Player_Encode` omits the receiving player's origin; `cl_dll/entity.cpp`
`HUD_TxferLocalOverrides` restores it from clientdata. A later full-packet player
may select that earlier entity as an intra-message base. Equal Z is then omitted
from its delta, so the former decoder inherited zero. Different Z is transmitted
and appears correctly. The ascending entity order also explains why receiving
entity1 can lose entity2 while receiving entity2 still sees entity1.

No raw wire capture survives this manual run; the retained journal proves the
Z jump, and a project-owned packet reproduction tests this concrete reconstruction
defect. It is not labelled a byte-for-byte replay of the user's packets.
Source was preserved before edits:1271 verified files under
`manual-artifacts/task-records/e10-player-origin-inheritance-20261001/source`.
The fix is in the existing `GoldSrcPacketEntityDecoder`: the runtime session
passes its serverinfo-derived receiving entity, and the decoder restores all
three signed-float origin components before the local record becomes an
intra-message base. Only an ordinary receiver with a valid1..max-clients identity
and complete clientdata from the same generation/transport-sequence/payload
ordinal can transfer origin. Missing, older or later-in-payload clientdata is
not guessed. Explicit remote fields retain their own values. The schema-typed
builder checks the restored values under existing bounds; malformed suffixes
publish neither the joined entity state nor staged clientdata.

Targets remain `hlclient_goldsrc_packet_entity_decoder` and
`hlclient_goldsrc_runtime_replay`; their new input is an optional numeric player
identity, with no concrete game dependency. This is protocol state reconstruction;
there is no proximity rule, pose/renderer workaround, modified wire grammar,
camera-derived coordinate, altered server trace or disabled culling. Existing
raw payloads/cursors and exact intra-message base identity remain unchanged.
Reconstructed state hashes naturally reflect the corrected coordinates.

The old decoder failed11 of211 assertions, including A's remote Z0 instead of
-1660; B's remote coordinates remained correct. After correction, the three
new cases passed483 assertions. The production session -> observation -> same
game host -> approved project-owned Studio asset -> materializer -> OpenGL
test traverses both receiving identities, unequal-to-equal height, approach
256/192/128/96/48/32 and return192. Every step produces model pixels and an
empty no-model baseline; equal-height samples range from5443 pixels at192 to
26296 at32. These are measured GPU pixels, not submission counts. Additional
decoder cases cover exact old delta-base retention, fresh high-precision local
origin, explicit remote Z, missing receiver/clientdata/component, late or stale
clientdata, invalid receiver, proxy rejection and atomic malformed suffix.
The neutral packet tests also run in the existing core-only API executable.

The first GPU-test compile failed because its Catch INFO ternary lacked
parentheses; this test-only syntax error was corrected, with the initial log
retained. Assertions were not relaxed.

### Final verification and handoff for the origin fix

VS2022/v143/Win32 Release all-target build and affected Debug build passed.
After the last production edit, the full Release offline suite covered all2640
registered tests:2603 passed,37 explicit capability/asset/device skips,0 failed.
The2639-test parallel partition took127.24s; the existing global-absence
fake-client fixture ran separately (1 passed,2.06s) to avoid its documented
cross-test process race. No test was omitted from the combined result.

Debug decoder/replay/E10/interpolation regressions passed5373 assertions in85
cases, with2 installed-asset skips (87 cases total). Its initial filter included
the nonexistent tag `studio-pose`; the actual `goldsrc-studio`+`pose` filter was
then run separately and passed156 assertions in9 cases. Both Debug and Release
ran the new production OpenGL pixel controls successfully.

The existing `build-core-g1` tree retains `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`:
all targets built and41/41 CTest checks passed, including the architecture
guard. Its module-free API executable independently passed the new decoder
cases (211 assertions,2 cases). The fix adds no HL module source or linkage to
the decoder or replay target. Launcher CheckOnly passed reference/peer/unlimited
and off/peer/300 seconds, with process-launches0. The no-stock launcher fixtures
are included in the full Release suite. ASan runtime was not run here; previous
unconfirmed runtime coverage is unchanged.

Logs are in `manual-artifacts/task-records/e10-player-origin-inheritance-20261001/`.
The pre-edit1271-file snapshot is retained; `source-preservation-final.json`
and `git-source-verification-final.txt` verify original copies, missing-file
absence, the empty index and `git diff --check`. Required fixture code is an
ordinary project file (`tests/player_origin_test_fixture.hpp`), not an artifact
dependency. Existing unrelated changes and other worktrees remain intact.

Normal Release `build/bin/Release/hlclient.exe`,3467776 bytes, SHA-256:

```text
9A612D03C30A3940C5A04DCE0A38AED9A7999AB91443B8EE5BCF6B5D8BD9BE74
```

The unchanged timing helper remains
`EEB543271B8EBA3F33CE6193EFCBC85A3A039E16197C9DC6D092AA53257ABD70`.
The current launcher selects these outputs. User reproduction without a game
timer remains:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Repeat A's approach onto the same level as B and retreat on the ramp, then
compare the reciprocal view. Result: origin-inheritance defect reproduced and
fixed, offline decoder/session/OpenGL verification passed. The new binary still
has `manual=not_run`; no new live game/stock/HLDS/Steam/WFP/capture session was
started. Staging, commit and push: none.

## Follow-up: two-client weapon effects (2026-10-01)

`attribution=user_report`: the two-client reference/unlimited test has footsteps
but lacks flashes, decals, gunshot sounds, shells and related effects. This report
does not erase the earlier remote-model/origin fix or the prior local E4/E5
confirmations. It is recorded separately; the new corrective build still needs
manual validation.

The source audit found an actual missing path: the E9.1 generic decoder retained
owning svc_event/svc_event_reliable observations, while the Half-Life module had
no consumer that interpreted the advertised Glock/crowbar event bindings into
remote presentation. E10 player Studio pose/animation did not supply that work.
Server svc_sound footsteps and local weapon presentation therefore could work
without producing another player's scripted weapon effects. The existing-run
table in REMOTE_PLAYER_MOVEMENT_AUDIO_E9.md also records independent permanent
peer mute and stale local-action timing evidence; none is inferred from a
renderer counter alone.

The current correction uses the same GameClientHost/module boundary and normal
application scene/audio paths. The protocol bridge publishes owned numeric
scripted-event arguments only after the complete record commits, with exact
record/cursor/entry identity. A packet reference resolves against its preceding
same-payload packet snapshot, not a culled rendering list. Reliable event origin
and angles come from their argument object, including zero vectors: pinned SDK
common/event_flags.h explicitly says reliable events do not inherit the invoking
edict transform. Event bindings are advertised type-local slots, not hardcoded
session IDs, and no .sc file is executed.

Bindings/resources must be installed after the runtime generation reset and
asset readiness; the host's application time origin spans that reset. Otherwise
startup can erase a valid binding or discard its one-time sound preparation
before an asset provider exists. Reset/replay cannot replay already consumed
effects. The bounded new diagnostic row separates unresolved, unsupported, late
and invalid events from accepted effects and downstream audio/visual counts.

Pinned official SDK revision remains
`b1b5cf5892918535619b2937bb927e46cb097ba1`: cl_dll/ev_hldm.cpp Glock callbacks
select the Glock sample, shell and static hit behavior; ev_common.cpp supplies
the contact-independent shell launch and player gun height. Remote muzzle
attachment, deterministic variation and missing-velocity inheritance remain
documented local-compatible presentation, not stock-binary parity. Nonzero
event delays or unresolved packet arguments remain explicit unsupported/skipped
inputs; missing identities are not guessed from camera/model proximity. This
slice adds no remote damage authority or all-weapon/mod support. Existing user
logs contain no failing wire payload for exact incident replay; final offline
results and new binary identity belong to the current corrective task record.

Final verification for this follow-up is recorded in
`REMOTE_PLAYER_MOVEMENT_AUDIO_E9.md`, under "Final offline verification and
manual handoff": Release 2614 passed / 37 skipped / 0 failed, focused Debug
256 passed / 6 skipped, core-only 41/41, actual OpenGL and isolated mixer checks
passed. The earlier origin-fix binary/report above is preserved. The new
two-client effect build is still manual=not_run; no new live sessions were used.

## Follow-up: remote crouch pose jerks (2026-10-01)

`attribution=user_report`: while one client watches the other crouch-walk,
there are jerks or changes of pose, not complete model disappearance. This is
a separate report from the repaired origin inheritance and weapon effects.
Earlier manual confirmations and binary identities above remain historical;
the new crouch corrective build requires its own manual validation.

The actual source defects and corrections are:

- `HalfLifeRemotePlayerPresentation::sample` reset the gait phase on each
  gait-sequence change. It now retains the bounded numeric phase across walk,
  crouch-walk and idle selection. Explicit gait zero pauses it without wrapping
  against unrelated sequence-zero metadata. Real network/map/lifetime/model,
  teleport, excessive-gap and no-interpolation boundaries still reset it.
- The same policy discarded continuity when multiple committed RX records
  arrived between render samples, or a new record had the same server time.
  Zero-time updates now preserve phase/active transition without integrating
  displacement. Skipped-render records catch up through the actual previous
  anchor under existing 0.25-second/128-unit bounds. Only owned numeric state
  and exact source identities are retained; no RX pointer or second history is
  introduced. Catch-up is net displacement, not reconstruction of unseen
  intermediate direction/gait changes or stock-binary parity.
- `RuntimeReplayLocalAssets` now passes exact committed previous server seconds
  through `GameClientAPI`, including when renderer interpolation holds current
  across a gap. Held render metadata cannot disguise a real gap as zero time.
- `StudioPoseEvaluator::evaluate_blend_pose` previously interpolated Euler
  channel scalars before converting to a quaternion, sending +179/-179 through
  zero and misinterpolating coupled axes. It now applies controller adjustments
  to both endpoint orientations and shortest-path slerps their quaternions.
  Translation sampling, channel budgets, layer masks, hierarchy, palette upload
  and posed bounds retain their existing mechanisms.

Pinned official SDK revision remains
`b1b5cf5892918535619b2937bb927e46cb097ba1`:
`cl_dll/StudioModelRenderer.cpp` keeps its gait-sequence reset commented out;
`StudioCalcBoneQuaterion` interpolates endpoint quaternions. SDK code was not
copied. An additional read-only approved `models/player.mdl` control scanned
65,536 crouch rotation intervals (bounded, not exhaustive), finding eight
endpoint spans above 180 degrees, with a maximum 218.111 degrees. This confirms
the real model exercises the problematic angle domain, not the exact packet
or pose from the user's incident. No failing wire trace was available.

Project-owned regressions cover gait switches with different frame counts,
gait-zero resume, equal-time transition retention, skipped render records,
repeated sampling, and genuine reset boundaries. An independent test module
checks committed clocks through the normal host/materializer. Studio tests
check all three rotation axes, controller adjustments and a coupled-axis arc.
A production OpenGL control compares the wrapped lower-body animation against
independent fixed-180 and incorrect-zero poses at five fractional coordinates;
it also checks immutable upload reuse, stable repeated pixels and GL errors.
Required fixtures remain ordinary project sources, not task-artifact inputs.

Logs/source snapshots for this follow-up are under
`manual-artifacts/task-records/e10-crouch-animation-20261001/`. The pre-edit
snapshot contains 1,279 source files. Initial build diagnostics retain a missing
test-only pose-header include; initial focused diagnostics retain a new-test
exact floating-point equality failure. Both were corrected without relaxing
historical assertions: fractional dense/sparse phase comparisons use tolerance,
while discrete fields and repeated-identical updates remain exact.

### Final offline verification and manual handoff for crouch correction

VS2022/v143/Win32 all-target Release and affected Debug builds passed. Final
focused Release: 51 passed, 2 explicit installed-asset skips, 2,693 assertions.
Focused Debug including weapon/viewmodel preservation: 112 passed, 6 asset
skips, 6,482 assertions. Actual OpenGL rotation-wrap pixels passed in both.
The optional approved actual-player control passed 204,005 assertions; its
bounded track scan is supplemental and not a required build/test dependency.

After the last production edit, the full Release offline catalog contained
2,660 tests. The 2,659-test parallel partition took 132.15 seconds: 2,621 passed,
37 capability/asset/device skips, and one startup process-inventory assertion
failed. The fixture compares system-wide named process IDs before/after;
its mismatch does not establish a crouch-code failure or the exact external
process change. Its isolated unmodified repeat passed, as did the deliberately
serialized global-absence fake-profile fixture (2/2, 6.87 seconds). Combined
catalog coverage is 2,623 passing checks and 37 explicit skips, with the initial
inventory mismatch retained in the log, not hidden or called a clean first run.
No assertions or process-inventory protection were disabled.

Existing `build-core-g1` still has `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`; all targets
built and 41/41 core/API/architecture checks passed. Launcher reference/peer/
unlimited `-CheckOnly` passed with process-launches=0. No-stock launcher fixtures
are included in the full offline catalog. ASan runtime was not run for this
correction; the previously unconfirmed ASan runtime status is unchanged.

Normal Release `build/bin/Release/hlclient.exe`: 3,493,376 bytes, SHA-256:

```text
8A5B14A0C47FD81A2A257C40E2F61DF6C0A91E6D931DE326821E490A8BA8372D
```

The unchanged launcher selects this binary. User reproduction:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Watch the other client crouch-walk forward/backward and strafe, stop/start,
then alternate crouching/standing; check both receiving views. Also check normal
fire/reload/viewmodel as preservation controls. Close the clients to finish.
New build `manual=not_run` until actual user confirmation. New live/stock/HLDS/
Steam/WFP/capture sessions=0; staging/commit/push=none. Source preservation and
empty-index verification are recorded separately in the task directory.

## User-authorized current-source checkpoint and post-E10 summary — 2026-10-01

The user now requests commit and push of the current project state. This
supersedes the no-commit instruction for this checkpoint only; historical
task records stating staging/commit/push=none describe those earlier tasks and
remain unchanged. The destination is the existing
`codex/stock-runtime-campaign-5e48b7c1` branch on
`Pvitaly91/hl-client-engine`, not `main`. Parent source checkpoint:
`ea738a80e5a66a500c245f95b96eed32198e2d66`.

Work completed after the initial HLC-E10 visual/Studio-animation milestone:

1. Added bounded normal-path remote-player visibility diagnostics and actual
   close-range OpenGL controls. The initial isolated controls did not reproduce
   the user's disappearance; no speculative near-plane/PVS/lighting fix was
   labelled its cause.
2. Fixed diagnostic retention end to end: mutex-serialized visibility rows,
   validated latest numeric journal entries prioritized within the existing
   native excerpt bounds, both receiving roles, and reciprocal contact controls.
   This repaired lost evidence, not player visibility by itself.
3. Added manual `-DurationSeconds 1..86400` and genuinely user-ended
   `-NoTimeLimit` for single/two-client reference/off launches. Startup, cleanup,
   Jobs, isolation and restoration retain bounds. Scripted scenarios retain
   their prior limits; closing the primary client ends the paired session.
4. Fixed equal-height remote-player coordinate reconstruction. Typed
   packet-entity decoding inherits omitted coordinates from the exact
   same-message high-precision clientdata where the schema requires it, instead
   of incorrectly assuming a zero base. Missing/stale data is not guessed;
   malformed suffixes remain atomic. Reciprocal approach/retreat session-to-GPU
   regressions cover the normal path without disabling culling.
5. Fixed two-client effects: removed the peer's forced permanent mute; corrected
   local action timestamps after scheduler stalls; consumed committed advertised
   Glock/crowbar events through the Half-Life module and normal approved asset,
   shell/decal/light/flash and mixer paths. Local/remote voices have independent
   identities. Focus gates playback, not event receipt or effect simulation.
6. Fixed remote crouch pose jerks: gait-phase preservation across gait switches,
   paused gait zero, equal-time records and bounded skipped-render catch-up;
   exact committed anchor clocks at the API; shortest endpoint-quaternion Studio
   interpolation rather than scalar Euler interpolation. Details and independent
   numeric/pixel controls are in the preceding section.

Latest tested Release remains SHA-256
`8A5B14A0C47FD81A2A257C40E2F61DF6C0A91E6D931DE326821E490A8BA8372D`.
Latest results: 2,623 passing Release catalog checks and 37 explicit skips after
the documented isolated process-inventory retry; 6,482 focused Debug assertions;
41/41 core-only checks and the architecture guard; actual OpenGL and launcher
CheckOnly passed. No new live session or listening/manual confirmation is claimed.
ASan runtime remains unconfirmed. Catch-up is bounded local-compatible behavior,
not reconstruction of unseen motion; remote effects remain the documented
Glock/crowbar slice, not all weapons, damage authority or stock-binary parity.

The source checkpoint also contains the earlier accumulated G1 and B/C/D/E1-E9
project changes required to reproduce the current application. Those are not
misrepresented as new post-E10 work. Required source, headers, fixtures, build
definitions, scripts and task contracts are ordinary Git files. Build outputs,
private captures, ignored manual artifacts and loose local diagnostic logs are
excluded from publication and preserved locally; no game assets are added.

## Subsequent manual confirmation before E11 (2026-10-01)

New user report: “Зробив ручний тест 2-х клієнтів все працює”. Current E10
manual outcome is `passed`, `attribution=user_report`, for reported two-client
body/animation and post-E10 behavior. No numeric gait/interpolation measurement,
new stock trace, audio recording, run ID or launched-process SHA is inferred.
Historical offline reports with `manual=not_run` above are not rewritten.
E11 remote attachments/impact particles are a separate build and require their
own manual check; see `REMOTE_PMODELS_BULLET_PARTICLES_E11.md`.

## E32 remote-player packet-phase smoothing (2026-10-13)

User authorizes the next correction after the read-only audit of runs
3ab506514bc6f0fa72f1598d32d1738b and1765739d73a569cb62b30b85b476dad6,
both actually using93358D21... on192.168.0.104:27016. The own-prediction path
was active about99.6% of controlled frames, with remaining lift67 and large
correction events; those separate movement defects are not claimed fixed here.
Retained contiguous remote render samples show short holds and packet-arrival
phase advances even with local prediction active. These are clock witnesses,
not per-player position/velocity recordings or evidence of packet loss.

Cause: normal `RuntimeReplayLocalAssets::present_entities` retained only the
latest pair, reduced the requested100ms delay to that pair's interval and
reset its application anchor on each publication. Irregular receipts therefore
changed playback phase rather than merely adding authoritative observations.

Task record: `manual-artifacts/task-records/e32-remote-player-buffer-20261013`.
1390 source files,36 ready Release files,3 configuration/cache files and22
latest user-run files were copied and SHA256-verified before edits. Existing
dirty/untracked code, branch/HEAD, SDK, launcher settings and other projects
are preserved; no staging/commit/push or new agent live game session is used.

Production mechanism: `RuntimePlayerSnapshotPresentation` extends the existing
GoldSrc interpolation layer with16 owning immutable observation references,
at most32 numeric player-continuity entries and bounded returned anchor data.
It reuses `EntitySnapshotInterpolator::interpolate_runtime` for actual pose
mixing, not a new renderer/physics implementation. Its clock is anchored once
per continuous episode, never per packet. Startup retains the short actual
initial pair interval and fills the game-selected <=100ms reserve with at
most25% render-clock slowdown. Once filled, ordinary arrivals do not shift
phase. This is explicit local-compatible presentation, not stock constants.
Samples never extrapolate past an observed endpoint or rewind during ordinary
continuous sampling. Gap/clock recovery follows the selected bounded policy.

Only selected remote player presentation fields are merged into an owning
copy of the latest current list. Receiving player, latest visibility/effects,
nonplayers/brushes, network state, command history, collision and gameplay
authority are not rewritten. Missing current fields are not fabricated.
Exact immutable buffered brackets are borrowed by the existing game-owned
Studio/gait/transition composer; proxies and raw excessive-gap semantics keep
their existing rules. Map/session reset, absent/reused slots, model change,
teleport, EF_NOINTERP and game-selected historical respawn boundaries prevent
bridging old lives. Equal-time corrections retain exact new source identity.

Ordinary projectiles/gibs/glows keep their separate existing cadence/birth
profile. Both buffers share the same immutable observations; they do not copy
game assets, open new paths or run object simulation. Socket receipt freshness
is separate from delayed player presentation. An incompatible delayed old
p-model omits only its numeric socket so the existing server-event fallback
can present a new weapon's effects; mismatch guards are not relaxed.

Required project-owned regression cases are in
`tests/test_remote_snapshot_buffer_e32.cpp`, normal full acceptance and core-only.
A fast test-only target runs those same production mechanisms, not a replacement
application. Normal materializer tests verify jittered receipts, exact committed
Studio clocks, unchanged nonplayer/brush ownership and p-model/socket readiness.
An independent old-clock negative control exhibits packet-phase jumps. Existing
p-model pixel/signature assertions caught a cancellation boundary infinitesimally
before an endpoint; the sampler now snaps only within1e-9 server-time seconds
to exact observed endpoint identity. No collision epsilon or old assertion was
weakened. Final Debug/Release/core-only results and ready-image identity follow
after the last production edit. Fresh manual remains not_run.

Final targeted validation: VS2022/v143 Win32 Debug, Release and core-only
ALL_BUILD pass /WX. Debug and Release each pass147 remote/animation/attachment/
interpolation cases /85384 assertions, with3 explicit installed-resource
opt-in skips. The previously failing unchanged E11 p-model socket, signature
and OpenGL pixel assertions pass. Core-only CTest passes44/44 including the
architecture guard;364 shared API cases pass319935 assertions without HL1.
The full Release offline suite is repeated after the last production edit;
its completed result is recorded below, not inferred from these subsets.

Ready normal client SHA256:
`2C6EAA63B261B20E1D6539132ED3460D0F62590153CF31D4F970CACD393BAA34`.
Normal SourceOnly/CheckOnly selects that exact Release image. Launcher SHA256
remains`AF58350DA40A7B1C0885A33A59EB96CEE8D0E33696CD51D3DA87617203C8E62A`;
saved user settings and endpoint192.168.0.104:27016 are preserved. No new live
game/stock client/HLDS session, server change, asset copy, stage/commit/push or
ASan runtime claim is introduced. Fresh manual=not_run.

Full acceptance history is preserved rather than hidden: the first complete
run has only2 timeouts, the unchanged resource-manifest fixture at60.16s and
normal-close/verdict fixture at180.08s; no actor/interpolation assertions fail.
A direct unchanged manifest retry passes in8.86s. The entire suite is then
repeated, without changing assertions, test limits, code or binary. Final
repeat:3597 registrations,3532 passed,65 explicit optional skips,0 failures,
718.94s; normal-close/verdict passes in124.20s. Subsequent version/help/Null,
synthetic UDP, functional-log, no-stock wrapper and SourceOnly/CheckOnly checks
also pass; `FULL_RELEASE_EXIT=0`. Initial and final logs remain separate in the
task record. The timing cause is not attributed to the server or game code.

Final source audit preserves1390 original files;9 existing source/doc/build/
test files changed plus one ordinary project-owned regression file. No originals
are missing. Branch/HEAD, clean pinned SDK, saved launcher configuration and
the unchanged launcher remain preserved; no staging/commit/push. Normal client
remains2C6EAA63... and fresh manual remains not_run. Own movement/prediction,
collision, ammo/health/damage and server files are unchanged by this correction.

Manual handoff: use the same normal launcher and server192.168.0.104:27016,
watch other players running/strafe/crouch, including close range and stopping/
turning. Check their p-model switches/fire effects and death/respawn boundaries.
Keep movement diagnostics enabled and use F8 if a jerk remains. The existing
remote clock/bracket fields now describe the selected buffered presentation
anchors, not necessarily the latest pair. No claim of universal stock engine
parity or resolution of the separate lift67 prediction failure is made.

## E32 railing presentation follow-up (2026-10-13)

New manual report, attribution=user_report: other players running on railings
appear jerky. Run8d56959e9b9ab0fb8c392003d1d305cc is identity-bound to the
preceding2C6EAA63... normal image, crossfire, endpoint192.168.0.104:27016;
exit0, window_close, transport_acknowledged. The bounded movement recording
contains1719 retained detailed frames, not every actor's trajectory. Its
controlled-alive cohort has16495 frames,16389 prediction-active,106 inactive,
8 fallback entries,5 frames over33.3ms and none over50ms. The global buffered
clock is not proof of individual player/pose smoothness. No per-player evidence
identifies the exact railing episode, so this report does not claim to have
replayed or proven the user's particular live incident.

Three production defects were independently reproduced before editing code:

- `RuntimePlayerSnapshotPresentation::sample` treated any changed player row
  at the same server timestamp as a position correction. A gait/frame-only
  publication could hold a delayed actor at its future pose: the owned fixture
  jumps from expected x3.2/z12 to x12.8/z48. The floor now changes only for an
  actual origin correction (other life/model/teleport/EF boundaries remain).
  Newer same-position anchors at an already held boundary preserve exact
  metadata/source identity; the unchanged E10 assertions caught this case.
- `HalfLifeRemotePlayerPresentation::sample` used the upper anchor's main/gait
  descriptor even when the sampled entity retained the previous run/jump pose.
  Valid continuous pairs now retain the selected discrete main/gait until the
  endpoint; same-sequence raw wire clocks remain authoritative. Future main
  transition blending does not replace that older pose. Death/life/nointerp/
  teleport/gap paths remain bounded and discrete.
- Moving gait yaw jumped to the upper interval direction on its first render
  sample. It now interpolates committed compensated facing directions on the
  same selected clock. Backwards walk compensation is applied before angle
  interpolation, within the supported torso-facing arc; a90 -> -90 turn does
  not take the180-degree tie and flip midway. Aim and the server transform are
  not rewritten.

This is local-compatible presentation, not stock-binary parity or predicted
movement. Official pinned SDK b1b5cf5892918535619b2937bb927e46cb097ba1 was
inspected read-only for gait semantics; no implementation was copied and no
ReHLDS/Xash code was used. Four project-owned regressions cover same-time
animation updates, delayed run/jump descriptors, rail turns and the actual
Half-Life host + production Studio composition over180 buffered samples.
The three initial negative controls have70 assertions/28 failures across3
cases on the previous implementation. Old assertions are retained unchanged.

Source/Release/configuration/current-run evidence was preserved using the
existing snapshot mechanism in
`manual-artifacts/task-records/e32-remote-railing-20261013`:
1391 source files,37 Release files,3 configuration files and22 run files,
all SHA-verified. Previous dirty/untracked work is retained. No server/Steam
files, physics, wire grammar, gameplay authority or weapon effects were changed.
Final verification and normal executable identity are appended after acceptance.
New agent live game/stock client/HLDS sessions=0; staging/commit/push=0;
new image manual=not_run. ASan runtime remains historically unconfirmed.

Final validation after the last production edit: VS2022/v143 Win32 /WX Debug
and Release targeted suites each pass151 cases /104367 assertions, with3
explicit read-only installed-resource opt-in skips. Core-only ALL_BUILD and
all44 CTest registrations pass, including the architecture guard;365 shared
API cases pass319959 assertions without the Half-Life module. Full Release
offline CTest:3601 registrations,3536 passed,65 explicit optional skips,
0 failures,768.81s. The unchanged normal-close/verdict fixture passes142.05s.
No old assertions or test deadlines were weakened. Additional normal launcher/
CLI/no-stock checks and source/image audits are recorded in the task directory.

Normal Release client SHA256:
`45B5B5DF2E16CC96B3FCA57394F23F0D14A7D1A497B316D499D44F5416AFC6BC`.
Unchanged launcher SHA256:
`AF58350DA40A7B1C0885A33A59EB96CEE8D0E33696CD51D3DA87617203C8E62A`.
SourceOnly/CheckOnly selects that exact normal executable. Saved launcher
settings, branch/HEAD and pinned SDK are preserved. This check is not live
resource attestation or fresh manual confirmation. Use the same launcher and
server, watch a player running/turning/jumping on the railings, and retain
diagnostics/F8 if a jerk remains. The exact reported live railing scene has
not yet been retested with the new image: manual=not_run. Remaining local
movement/lift corrections or genuine server trajectories are not claimed fixed.

## E32 delayed looping animation clock follow-up (2026-10-13)

New reports, attribution=user_report: remote Barney running/firing Glock and
then Gman running, crouching, jumping and switching crowbar/Glock appear jerky;
the latter was in crossfire's Egon/wall-charger room. This is not classified as
a model- or weapon-specific defect. User runs a78c0fde84ce109fe1b67b7da325bde9
and c402633f66b142894d479510affc01b7 used the preceding CC4E64AF... normal image.
Both closed normally. The latter has40 controlled-play frames over33.3ms,
maximum47.7184ms; its retained851 paired render/remote clocks progress together
within0.001ms. These bounded global samples do not record each actor's position,
pose or actions, and do not establish the exact cause of the user's live moment.
Local prediction activity is not proof of remote animation smoothness.

An independent production-path regression proves a remaining animation defect:
`HalfLifeRemotePlayerPresentation::sample` passed the upper looping raw anchor
to `main_sample` even before that anchor's animtime. The raw evaluator correctly
clamps negative elapsed time, so the whole delayed interval displayed its future
frame. Consecutive owned11-frame/20FPS anchors0,51.2,102.4 at10.0,10.1,10.2
therefore held frame2 then frame4 rather than advancing0 through4. Production
Studio composition independently shows the same plateau in an upper-body bone.
The initial unchanged-production negative control has4 cases/361 assertions,
3 failing cases/50 failing assertions; the boundary-only case already passes.

The correction selects the preceding raw anchor only for a valid continuous
delayed pair, matching sampled/previous/current looping main sequence, and
strictly increasing animtimes whose older clock applies at the sample time.
The sample must be within the observed server interval; the new animtime must
not be later than its current observation. At/after new animtime the current
raw anchor is used exactly. Equal/regressing animtimes and unsupported pairs
retain prior behavior. No blend of wrapped byte frames, guessed extra loops,
new smoothing history, allocation or per-frame I/O is added.

Signed frame rates, pauses, forward/reverse wrap and discrete same-sequence
restarts stay in the existing evaluator. The separate `delayed_main` flag still
means a different-sequence transition, so choosing an older same-sequence clock
does not suppress an active transition blend. Existing nonloop/death endpoint
interpolation, gait, pitch/controllers, sampled appearance and weapon attachment
selection are unchanged. Model/life/map/session/teleport/gap/no-interpolation
boundaries also remain effective on repeated sampling.

Only the selected HL module changes production behavior; ordinary application
materialization already supplies its exact buffered anchors. No wire, server,
physics, own movement prediction, asset, renderer, audio or launcher change is
needed. Pinned official SDK b1b5cf5892918535619b2937bb927e46cb097ba1 was read for
raw frame/animtime semantics; no implementation was copied or submodule updated.
This is local-compatible delayed presentation, not a stock-binary parity claim.
Quantized authoritative corrections may still be discrete at their true anchor;
CPU/FPS spikes and remaining local collision fallbacks are separate observations.

Four additive `[e32-loop-clock]` regressions exercise two committed brackets
through GameClientHost and Studio composition, dense/sparse/duplicate sampling,
gait/weapon endpoint changes, forward/reverse wrap, pause/rate changes, restart
timing, unchanged raw-clock corrections and13 repeated continuity guard variants.
Existing assertions were not weakened. Evidence and SHA-verified pre-edit
preservation (1391 sources,37 Release files,3 configurations,22 latest-run files)
are in `manual-artifacts/task-records/e32-remote-loop-clock-20261013`.
Final verification after the last production edit:

- VS2022/v143 Win32 /WX Debug and Release ALL_BUILD pass. The four focused
  regressions now pass all361 assertions; the negative control above remains
  preserved. Debug and Release targeted suites each pass81 cases /33998
  assertions, with2 explicit installed-resource opt-in skips (83 cases total).
- Core-only ALL_BUILD, all44 CTest registrations (including architecture guard)
  and369 shared API cases /320106 assertions pass with the HL module disabled.
- Full Release offline suite:3609 registrations,3544 passed,65 explicit optional
  skips,0 failures,698.58s. The unchanged no-stock normal-close/verdict fixture
  passes in121.32s. Existing assertions and test deadlines were not weakened.
- Normal CLI/version/help/Null and synthetic UDP checks, functional-log
  validation, G/H1 summaries, H2 launcher/no-stock fixtures and SourceOnly
  CheckOnly pass. These do not constitute game-resource or LAN live validation.

Normal ready Release client (`build/bin/Release/hlclient.exe`) SHA256:
`8BC236DB35A2A79C9AF1A5C95206405CB4D1BDC6CC8FCDE8D411DA65796B0433`.
Normal launcher (`build/bin/Release/HLClientLauncher.exe`) SHA256:
`AC5F628B93A1DB84A0226F92DD2EE36EB933631459E0472FB9B7B3007557083A`.
ALL_BUILD relinked the launcher; its source and saved settings are unchanged.
SourceOnly CheckOnly selects this exact normal client. The final source audit
is limited to remote_player.cpp, its additive tests, this task record and the
existing architecture document. Prior dirty/untracked work, branch/HEAD, pinned
SDK and rollback/source snapshots remain preserved.

Use the same launcher and192.168.0.104:27016, with existing movement/FPS
diagnostics retained. Recheck remote running, crouching, jumping and weapon
switches, including the Egon room. This fixes the reproduced looping-clock
plateau; it does not establish that every live positional jerk or CPU/FPS spike
has the same cause. New agent live game/stock client/HLDS/LAN sessions=0;
staging/commit/push=0; fresh manual=not_run. Historical ASan runtime remains
unconfirmed.

## E32 per-player position diagnostics follow-up (2026-10-13)

Attribution=user_report: after the looping-clock correction, a remote player
crouching and switching Glock/crowbar near the Crossfire outer pillar appeared
to jump from one location to another. The older screenshot identifies the
location, not the motion or its cause. The next explicit request is to extend
diagnostics, not change interpolation, prediction, physics or animation policy.

The last run `f61a2b519f1dad78278a48137a947f01` used the preceding 8BC236DB...
normal client, closed normally, and retained no F8 marker or per-actor position
trajectory. Its healthy common remote clock does not rule out a single actor's
continuity reset. No exact cause for that live positional jump is claimed.

The existing launcher movement/FPS checkbox now also records bounded numeric
per-player observations in `external-movement.json` (`remote_players`, schema
`hlclient.remote-players.v1`). Every successfully presented frame compares up
to32 players. It distinguishes actual position-bracket raw endpoints, latest
RX position, sampled position and the submitted parent Studio transform.
Submitted/culled/missing remain distinct; a submitted transform is not GPU
pixel proof. Snapshot sequence/gait/weapon/effects, source ordinals and times,
slot lifetime, network/map generation, per-actor interpolation/hold decision,
logical/resolved continuity floors and typed reset/hold/clock reasons are
recorded alongside the existing local movement and frame-time diagnostics.

This uses the actual interpolation result, not an independent guess from the
common bracket. Same-server-time changed-origin corrections, removal/reentry,
model/schema changes, game boundaries, no-interpolation flags, teleport/gap
guards and clock resets are distinguishable. A missing capture is unavailable,
not an empty actor list. Missing model materialization does not fabricate a
rendered origin. Records contain no player names, native asset paths, handles,
auth material or new server queries.

The recorder is opt-in, heap-backed and bounded, with no per-frame file I/O.
The normal application transports numeric observations only after successful
scene publication. All authority, collision, movement, interpolation results,
Studio blending, weapons, resource permissions and launcher settings remain
unchanged. Offline fixtures compare diagnostics on/off through the production
asset importer, host and scene publication, including a per-actor reset while
the global bracket remains healthy. Further tests cover bounded long sessions,
invalid input, generation/lifetime boundaries, F8 and legacy report analysis.

Source/Release/config/latest-run preservation and current-task acceptance are
in `manual-artifacts/task-records/e32-remote-position-diagnostics-20261013`.
Use the existing launcher and192.168.0.104:27016 with diagnostics enabled;
press F8 promptly after a positional jump, then close normally. Automatic
observations also work without F8. These logs support the next diagnosis; they
are not a claim that the remaining live stutter has been fixed.

Retention is640 samples (all32 actors at5Hz for about4s),128 exact before/after
events and two independent F8 captures, each up to160 samples including the
latest exact actor frame plus64 retained events from the preceding second.
Every-frame aggregate comparisons are not downsampled. Displacement events
use an8-unit diagnostic threshold and250ms cooldown; these are observation
heuristics, not teleport verdicts or movement constants. Benign interpolated/
exact-endpoint alternation does not flood the event ring. All evictions,
invalid data, manual truncation and event suppression are explicit counters.
Memory is separately capped at1MiB and the remote JSON section at2MiB, within
the existing16MiB overall receipt limit. Near that overall limit, omitted local
detailed frames are explicitly counted while session/FPS summaries and incident
headers survive. Disabled, awaiting presentation, allocation/encoding failure
and byte-budget exhaustion are not reported as zero motion. Skipped captures
can miss transient clock details; persistent floor reasons remain observations,
not proof of complete event coverage. F8 after more than a second cannot recover
an already evicted exact incident, so press it promptly.

Final VS2022/v143 Win32 acceptance after the last production edit:

- Debug and Release ALL_BUILD pass. Each targeted suite has103 cases,
  101 passed,2 explicit installed-resource opt-in skips,103038 assertions.
- Core-only ALL_BUILD,384 API cases/340759 assertions and all44 CTest
  registrations pass, including the architecture guard.
- Full Release offline suite:3625 registrations,3560 passed,65 explicit
  optional skips,0 failures,668.74s. The no-stock normal-close/receipt verdict
  regression passes in122.40s. The initial new-fixture failures were corrected
  without changing existing assertions or deadlines; final logs are separate.
- The analysis script's125 fixtures pass; the preceding real session remains
  readable with `remote_players=not_recorded`, not a false zero-event result.
- SourceOnly CheckOnly selects the exact ordinary Release image. The saved
  launcher configuration,1391 source backups,37 Release rollback files, branch,
  HEAD and pinned SDK remain preserved. Staged entries and deleted sources:0.

Ready normal client SHA256:
`B8D9A708307D2F85BF9C60CC536831F30E41BB7646F017CCD2E3CEEBA050165E`.
Launcher SHA256:
`B25036E55DD276A7ED71D44A2CD03574B67350889ADFDA1C42B1E157C8EFC282`.
The linker updated the launcher binary; its source and settings did not change.
New live game/stock/HLDS/LAN sessions=0; staging/commit/push=0; manual=not_run.
Historical ASan runtime remains unconfirmed and was not retested here.

## E32 firing-player motion continuity corrective (2026-10-13)

Attribution=user_report: a perceived jump of another player's model near a
lift, marked with F8. Run `1ab2299dbe7bbebe1d3b1ce70b294fba` used the preceding
B8D9A708... normal client and closed normally. The marker is106.150140s.
Visible entity11/model155/lifetime167 moves10.146 units in12.527ms at106.192824s,
42.684ms after the marker. Earlier visible resets at103.830710/104.124144s
move39.459/21.431 units. The flagged latest-server displacement near F8 is
6.266 units over42.969ms, while presentation advances its selected clock by
140.179ms and snaps to the newest position. Clearing the flag then moves the
position floor again and holds the model until106.370963s. The F8 local window
has no frame longer than19.9836ms. These are position-presentation observations;
the retained fields do not establish that this actor is attached to a lift.

Raw effects alternate0 ->34 ->0, with the same model/lifetime/weapon/main.
The pinned SDK `common/const.h` defines muzzleflash2 and nointerp32. Its Glock
attack sets muzzleflash, and `CBasePlayer::SetAnimation(PLAYER_ATTACK1)` may
set nointerp when restarting a nonlooping attack. The same nointerp bit is
also used by death and other position-discontinuity contexts. Treating every
such attack pulse as a new position lifetime floor discards the render delay;
OR-ing both endpoints repeats that reset on the falling edge.

The concrete HL module now selects a narrow animation-only interpretation for
an exact flagged observation. Eligibility requires exact effects34, an existing
matching player/model and stable nonzero weapon, valid positive gait, complete
finite transforms, bounded increasing clocks/displacement, and an already
approved compatible model with a nonlooping `ref_shoot_*`/`crouch_shoot_*`
sequence. Unknown/missing/unsupported metadata, effect32 alone, spectator,
death/rearm, zero gait, changed model/weapon, large displacement and excessive
gap retain strict behavior. This is a documented local-compatible profile:
observations cannot distinguish a custom small teleport deliberately combined
with an otherwise qualifying muzzle/animation pulse.

The ordinary application transports sorted bounded numeric qualifications by
entity and exact committed ordinal. The engine subtracts only that qualified
effect mask from motion-discontinuity checks, including the previous flagged
endpoint after its flag clears. It retains every immutable raw effect, source
identity and animation anchor. Generic/core defaults, explicit no-interpolation,
actual teleport/model/schema/life/map/session/removal/gap and same-time position
correction guards remain independent. No input, local prediction, physics,
server configuration, damage, asset files or wire grammar changes.

The HL pose owner independently preserves gait/yaw for the qualified position
pair while the raw effect still clears main-animation transitions and preserves
the existing exact restart/frame rules. The renderer/materializer continues
to use the sampled world transform and existing Studio path. No per-contact
or render-time asset I/O, second smoothing owner or guessed player motion is
introduced. Qualification holds only synchronous approved metadata borrows;
the reusable numeric table is bounded by16 observations x32 players.

Regression fixtures exercise moving horizontal trajectories and rising/falling
platform trajectories, both pulse edges, exact-source/actor isolation, default
strict behavior and lifecycle/teleport/schema/gap guards. The normal approved
fixture importer/HL host/scene materializer verifies the final published
transform; separate HL tests verify gait retention and main-animation restart.
Existing assertions and architecture guards are preserved. The latest run and
pre-edit source/image/configuration hashes are retained in
`manual-artifacts/task-records/e32-remote-nointerp-pulse-20261013`.

The final VS2022/v143 Win32 /WX build and focused acceptance pass:

- Debug and Release mechanism suites:19 cases/17816 assertions each.
- Normal Release targeted suite:120 cases,117 passed,3 explicit installed-
  resource skips,122355 assertions. All prior assertions remain unchanged.
- Core API binary (without the concrete HL module):389 cases/355349 assertions;
  the CMake architecture guard passes without conditional HL dependencies.
- Separate exact-root approved asset control: models/player.mdl and the zombie
  appearance both expose sequence34 `ref_shoot_onehanded`, loop0, and qualify
  mask32 for the observed numeric pulse;10 assertions pass. No game, Steam API,
  stock client, HLDS or LAN session is started by this read-only asset control.
- The initial configuration rejection of an HL include in a shared test source
  was repaired by separating neutral and HL test files; the architecture rule
  was not relaxed. One new-test INFO macro precedence error was also repaired.

Ready normal client SHA256:
`6661897B72FCF7983B093A2A17EA806034FE706012AA8280C8924EC2D4BEC14A`.
Launcher SHA256:
`9726588AAC10A63B075C4DBAEDF308271D2B10799B9F0C22BB5B8171E0C4319B`.
SourceOnly CheckOnly selects this exact client. The linker regenerated the
launcher binary; its source and saved configuration are unchanged. Current
source scope is11 modified files and2 new test files;1395 pre-edit sources,
37 prior Release files,3 configurations and22 latest-run files were preserved
and SHA-verified before changes. Branch/HEAD, pinned SDK and prior dirty work
remain unchanged. Staging/commit/push and new agent live sessions:0.
Fresh manual=not_run; the user's old F8 run is evidence for the preceding image.
ASan runtime remains historically unconfirmed, not retested here.

After the last production edit, the full Release offline suite passes all3634
registrations:3568 passed,66 explicit optional skips,0 failures,752.52s. The
unchanged normal-close/receipt-verdict fixture and subsequent normal CLI/Null,
synthetic UDP, summaries, no-stock wrappers and SourceOnly CheckOnly pass.
The existing cleanup policy is applied again after verification: temporary
compiled objects/Debug outputs and raw test logs are removable, while the new
ordinary Release, source/Git/dependency caches, compact final verification and
MDL-Viewer are preserved. Detailed raw logs are not archived by this cleanup;
the final numeric verification records remain in the task record.
