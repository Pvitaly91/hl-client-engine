# D4 continuation: vertical lift prediction (2026-09-28)

## Additive manual evidence

Attribution: `user_report`. The user reports that the lift is visible,
activates and ascends, but player ascent jerks. Smooth ascent is **not**
confirmed. Slopes/rough terrain were not re-evaluated by this report.
No run identity or numerical measurements are attributed to this report.
Historical D4 reports and retained run artifacts are unchanged.

## Baseline and reproduction

Worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66` (existing dirty source preserved).
Verified pre-edit copy/hash snapshot: 284 files in
`manual-artifacts/task-records/d4-lift-prechange-20260928/source` and inventory.
Existing D4 baseline: 6 cases, 3006 assertions passed.

The retained local run `96bb5551e2a74ae8b4721292e77364c4` has
`support_policy=current_server_frame_no_pusher_extrapolation`, five fallbacks,
last fallback `player_allsolid`, maximum raw error 19.577885, maximum camera
correction jump 8.664062, and zero recorded long-stall frames. These are
aggregate run diagnostics, **not** a proven association with the user's lift
report or a time-localized diagnosis. They cannot exclude shorter frame gaps.

The pre-fix production kernel regression independently specifies a platform
moving at 100 units/s, a server-seeded player at z=86, and a 20 ms command.
Expected z=88, relative height 36; actual z=86, relative height 34. Both
assertions fail (`red-test.log`). Old D4 tests only seeded each frozen scene
separately; they did not cover between-observation carry.

## Contract being verified

`ReferenceVerticalSupportMotion` is an owning, bounded **derived** model, not
server-observed history or complete stock PM_Move/pusher parity. Matching
generation/library/entity/model, unchanged XY/angles, explicit SOLID_BSP and
MOVETYPE_PUSH, two coherent entity/svc_time sources 1–250 ms apart, speed at
most 256 units/s. Extrapolation clamps after 250 ms. Receipt/render clocks do
not estimate speed. Missing or unsupported motion retains the frozen-scene
boundary; unknown/nonzero basevelocity still fails the existing seed contract.

The Half-Life module explicitly selects this policy through GameClientAPI.
Shared append/rebase use `simulate_reference_movement`, with the existing
movement kernel, commands, collision hulls, and history. Server seeds already
include displacement through their source time. Only the command interval is
carried. Player velocity and wire commands are unchanged; replay has no input,
effects, console or TX access. Failed carry publishes no candidate state.

Ground checks use a scene sampled at the command start. Movement uses the
existing kernel; retained ground contact is swept through the support delta
against other collision objects, then tested in the end scene. Jump/edge/world
contact does not retain a sticky camera relation. Ceiling/crush decisions are
not locally authoritative and fail closed.

Presentation samples support and player at the same interpolated simulation
time. Its collision sweep start is expressed at that support time, preventing
a falsely embedded older endpoint. D3 still determines presence/materials;
only existing matching instances receive a render-only vertical transform and
updated bounds. No canonical observation, ClientWorldState brush publication,
renderer-specific gameplay rule, texture, speed or network cadence is changed.

A repeated identical source time/scene retains the trajectory (it does not
infer zero velocity). Latest-command corrections retain a bounded owning
pre-anchor presentation state, including repeated corrections, without adding
it to command replay history. Cache keys still include time, history, epoch and
world publication. Life transitions clear the trajectory and predecessor.

Generated contact/interpolation positions may receive one outward binary32
ULP **only** after a blocking query and a complete-scene free-hull proof.
This does not relax server-seed ground agreement or permit ceiling penetration.

Limits: two observations cannot predict an unseen stop, acceleration, reversal,
or server block. Their next correction remains real raw error, not hidden by
long smoothing. Quantized server positions/timestamps limit velocity accuracy.
Gaps beyond 250 ms, speeds above 256 units/s, non-vertical/rotating motion,
nonzero/unknown basevelocity, crushed or unsupported contact remain outside
this slice. The extrapolation horizon clamps; it is not fabricated exact
history. Reference-off keeps its existing non-predicted path.

Reference: pinned ReHLDS `SV_PushMove` physically pushes standing entities with
the pusher temporarily non-solid and rolls back blocked pushes; this is distinct
from Valve `pm_shared.c` basevelocity handling. The client implementation is
explicitly a bounded derived subset, not a copy of server think/touch/blocked.
See https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_phys.cpp
and repository-pinned `third_party/halflife-sdk/pm_shared/pm_shared.c`.

## Completed offline validation

Result: `vertical_support_prediction_integrated_offline_verified` within the
bounded profile above, not a claim of complete stock pusher parity or a proven
diagnosis of the unassociated manual run.

The original 20 ms/100 units/s fixture now gives player z=88 and relative height
36 (previously 86/34). Independent rising/falling/idle trajectories run 50
commands each at 30/60/144 FPS and irregular render intervals. Endpoint and
command counts are invariant. Delayed uneven source updates replay two queued
commands; exact/repeated correction raw error and relative support height are
asserted within 0.001 units. A separately injected 0.125-unit correction remains
measurable raw error and does not add vertical carry. Camera/source/model times
are sampled together, not measured by counting position changes.

Production fake-peer LiveRuntimeStage separately covers four commands in one
update, active support, camera/model relative height within 0.002 units, newest
and repeated zero-correction continuity, unchanged local-step count at rebase,
and no TX during render sampling. It uses the actual GameClientHost Half-Life
policy. The finite authored BSP covers jump/landing, walking off onto world,
stepping onto a stationary ledge, duck/stand and blocked ceiling carry. Evidence
tests cover stop estimation, horizon clamp, removal and generation rejection;
existing D4 tests cover unavailable/replaced model binding and source ownership.
Unobserved start/stop timing is still an explicit limitation, not a zero-error
claim. Existing C lifecycle and B1/D1/D2/H3/H4 tests remain in the focused suite.

The D3 render test independently checks model matrix/bounds, immutable committed
frame, removal/no resurrection, generation rejection and actual changed OpenGL
pixels. Camera motion is verified separately through production stage snapshots.
No glReadPixels/glFinish was added to the normal application loop.

| Verification | Result |
| --- | --- |
| Release, Debug, existing ASan Debug builds | Passed, incremental, no clean |
| Same focused set, each configuration | 519 passed, 1 opt-in skip; 308182 assertions |
| D4 + D3 focused set | 13 passed, 1 opt-in skip; 16135 assertions |
| One final Release offline suite | 2120 passed, 22 skips; 403750 assertions |
| Existing `build-core-g1`, Half-Life OFF | Built; 95 core/API/alternate-module cases, 1424 assertions |
| Core CTest + dependency guard | 2/2; direct/alias/transitive/generator/object/source/include cases |
| Generated core project audit | 175 vcxproj files, no concrete Half-Life target/source/header refs |
| Null/process-level replay CTest | 8/8 |
| Additional read-only local Crossfire/GL control | 52 assertions; four inspected submodels; GL errors=0 |
| Native/PowerShell diagnostic roundtrip self-tests | Passed, stock processes=0 |
| Launcher fake-runner + test-health fixtures | Passed |
| CheckOnly from system32 | reference/default crossfire; off/damage-respawn-check; reference/TestStartHealth50 passed |
| `git diff --check`, staging audit | Passed; no staged files |

The 22 broad-suite skips are unavailable filesystem symlink/reparse/second-volume
capabilities, hidden-SDL focus/relative capture and opt-in local asset controls.
The relevant local brush control was then explicitly run read-only and passed.
These are not manual input validation. ASan reported no errors.

Logs are in `manual-artifacts/task-records/d4-lift-prechange-20260928/`:
`red-test.log`, `final-lift-test.log`, `focused-{release,debug,asan}.log`,
`final-offline-release.log`, `coreonly-ctest.log`, `null-process-ctest.log`,
`local-readonly-brush-gl.log`, `checkonly-*.log` and build/launcher/parser logs.
Required implementation, fixtures and this contract are ordinary project files;
none depend on those artifacts or installed Half-Life for the core-only build.

Actual targets remain `hlclient_goldsrc_usercmd_session` (shared support/replay),
`hlclient_goldsrc_live_runtime` (host), `hlclient_application_runtime_replay`
(render-only composition), `hlclient_game_halflife` (policy selection), and
`hlclient`. No concrete module linkage was added to lower targets.

Build examples (existing trees; use installed CMake on PATH):

```powershell
cmake --build build --config Release --target hlclient_tests hlclient hlclient_core_api_tests --parallel 1 -- /p:UseMultiToolTask=true /p:CL_MPCount=1 /nodeReuse:false
cmake --build build --config Debug --target hlclient_tests hlclient hlclient_core_api_tests --parallel 1 -- /p:UseMultiToolTask=true /p:CL_MPCount=1 /nodeReuse:false
cmake --build build-asan --config Debug --target hlclient_tests --parallel 1 -- /p:UseMultiToolTask=true /p:CL_MPCount=1 /nodeReuse:false
cmake --build build-core-g1 --config Release --target hlclient hlclient_core_api_tests hlclient_application_runtime_replay --parallel 1 -- /p:UseMultiToolTask=true /p:CL_MPCount=1 /nodeReuse:false
ctest --test-dir build-core-g1 -C Release -R '^hlclient_(core_api_offline|game_boundary_guard)$' --output-on-failure
```

The core-only cache remains `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`. Normal trees
retain the enabled Half-Life module. ASan uses the existing MSVC x86 runtime
directory in the test process PATH only.

Release `build/bin/Release/hlclient.exe` SHA-256:
`9A9BC4B2CE9864E512E009060A42E311C7BA9A5037E04A970CCB03EFA50F88EB`.
Unchanged orchestrator SHA-256:
`F0288A17848CBA84E85BFD82DAA172B63158FFE5B7242B939521710F648550E9`.

Manual handoff (not executed by the agent):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

New manual smoothness remains **pending user validation**. Optional 50 HP,
reference/off and damage-respawn-check remain unchanged. New live/HLDS/Steam/WFP
launches=0; historical artifacts unchanged; staging/commit/push=none. Final
process inventory contained no active compiler or targeted game executables.
