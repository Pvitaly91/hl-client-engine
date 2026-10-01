# D4 ground and brush prediction

## New manual report (additive)

2026-09-27, attribution=`user_report`: crossfire's previously missing geometry,
railings, wall stations and lifts are visible. D3 visibility for these objects
is confirmed by the user. The lift reportedly does not work; walking on its
surface, uneven surfaces and uphill slopes loses smoothness. No activation
method, runtime entity identity or transform timeline accompanied this report.
Historical D3 `manual validation=not_run` records remain unchanged.

Worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`.
Pre-edit verified source snapshot: 192 files under
`manual-artifacts/task-records/d4-ground-prechange-20260927`.
Release baseline: 237 passed, one opt-in local-assets skip, 261588 assertions.

New live launches=0. Steam/WFP/capture/ETW=not_run.
Staging/commit/push=none. New movement manual validation=not_run.

## Findings and scope

| Area | Reproduced boundary | Change / remaining evidence |
| --- | --- | --- |
| A: slopes/steps | Reference walk projected wish and velocity onto the slope before the walk/step decision. On z=.5x+48 the first command produced vx=17.888544, dx=.357773, vz=8.944272 rather than vx=20, dx=.4, vz=0. | Horizontal reference acceleration, grounded vertical reset, clear-horizontal early route, reference step depth/selection restored. Synthetic profile retained. |
| B: stationary brushes | Live attachment used model-zero `WorldOnlyMovementCollision`; seed rejected any other collision profile/hit. Visible D3 brushes were not prediction contacts. | Current committed entity -> sparse model slot -> *N -> shared BSP hull library/scene now supplies seed, movement, clearance and presentation checks. |
| C: lifts | No retained per-lift transform timeline identifies activation or pusher behavior in the reported session. | Actual map/button path inspected; current-frame transforms queried, but **predictive moving-support carry remains pending**. No local lift animation or guessed velocity. |

The available latest retained run `e9870282bbf14f13bdc204dd97d71a11` is a
read-only control, not an asserted identity for the user's report. It has 1028
fresh samples, 5508 active / 471 fallback frames, two fallback transitions,
1915 local steps, 790 corrections and 280 replayed commands. Its maximum raw
correction is 15.856756 and maximum correction eye jump 19.436124; the final
ground result is ready. These aggregates cannot assign a particular spike to
A, B or C. Wrapper cleanup and restoration both say `exact`, result completed,
primary error none. Historical artifacts have not been rewritten.

## Reference and ownership

The behavior audit used pinned Valve
[`pm_shared.c`](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c):
PM_CatagorizePosition, PM_WalkMove, PM_FlyMove, PM_ClipVelocity, friction/gravity,
and [`view.cpp`](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/view.cpp).
This is a targeted correction of the existing reference profile, not a new
claim of complete PM_Move parity. Existing stop epsilon/slide bounds and the
absence of reference edge-friction probing remain limitations. No new step
camera filter is imported: H3/H4 collision-aware interpolation is retained.

Pinned compatible engine cross-checks:
[Xash CL_AddLinksToPmove/CL_SetSolidEntities](https://github.com/FWGS/xash3d-fwgs/blob/7500a6b3647e71d9b21691671957a0e06731019e/engine/client/dll_int/cl_pmove.c)
select current-frame physents; this supports a **frozen current server frame**
for a complete replay suffix, not a claim of exact historical transforms.
[ReHLDS SV_PushMove](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_phys.cpp)
displaces supported entities on the server. Transform displacement, player
velocity and basevelocity are not interchangeable. D4 does not add either
transform deltas or basevelocity to the player's position.

No GameClientAPI interface or Half-Life policy value was changed. The G1
module still selects input/buttons/environment and movement configuration.
Generic protocol projection adds owning optional `solid` and `brush_move_type`
to `RuntimePacketEntityObservation`. Schema types are checked; they do not
alter wire grammar or the historical canonical-hash field set. The existing
record transaction means a malformed suffix publishes neither field.

`hlclient_goldsrc_usercmd_session` owns `reference_brush_collision.{hpp,cpp}`
and uses the existing neutral `goldsrc_brush_models` *N parser. This dependency
also reaches shared renderer types, **not** concrete Half-Life code. There is
no second BSP parser or collision engine. `BrushSceneMovementCollision` is the
existing shared adapter generalized with `reference_brush_scene_v1`; the old
synthetic class name remains a source-compatible alias for tests/tools.

`ReferenceBrushCollisionContext` owns a const scene and generation, publication
revision and source-record identity. It accepts a complete committed entity
set (bounded to 8192), current generation and complete finite transforms.
Only explicit SOLID_BSP=4 with observed MOVETYPE_NONE=0/PUSH=7 is admitted;
SOLID_NOT=0/TRIGGER=1 does not block. Unknown fields/roles/models fail closed.
Render modes, effects, visibility and BSP classnames never establish solidity.
Existing Valve AddToFullPack copies `entvars.solid`; installed delta.lst's
solid SHORT and movetype INTEGER descriptors agree with the projection.

Runtime entity number, precache slot and *N remain distinct. Instance identity
uses the server entity number plus model index within the map generation,
never the BSP entity ordinal. Each fresh committed entity record selects one
immutable scene before seed/rebase; retained records reuse it. The scene is
not replaced midway through command replay. Removal/model change replaces
scene membership; no independent support attachment survives it. There is no
protocol spawn serial for undetectable same-number/same-model reuse, and no
invented one. A map generation mismatch fails closed. Ordinary transform
updates invalidate the presentation cache but do not reset wire history,
network generation or life epoch.

Camera interpolation's body hull and eye segment query the same scene; the
existing world-only visual-correction helper is additionally constrained by
that body/eye scene before publication. Render interpolation is never a
physics input. Solid brushes are not inferred water/contents providers.

## Precision and fail-closed limits

The second reproduced defect was binary32 rounding of a generated step landing
inside its plane, discarding the valid raised route. Only that generated
contact may move **one representable value outward per nonzero normal axis**,
and only if a second complete hull query proves it free. This is not a global
epsilon, smaller hull or modification of a canonical server seed.

Clientdata origin's observed format has a 1/128 unit grid; ordinary brush
origin has 1/8. Tests retain a free-side grid seed byte-for-value and reject
an actually penetrating point two clientdata quanta lower. No blanket seed
tolerance was added. `ground_flag_disagreement`, solid start and missing or
nonzero basevelocity remain explicit unsupported/fallback results. A future
verified seed-tolerance contract must not move canonical authority.

Moving supports currently use the most recent observed frozen transform;
there is **no extrapolation between observations and no predictive carry**.
Up/down/stop fixtures prove authoritative displacement is not added twice,
not continuous lift support. Rotation of an already stationary brush is a
rigid collision transform; rotating motion, trains, conveyors, nonzero
basevelocity and crush rules are not implemented. Large corrections still
suspend prediction; diagnostics now retain their maximum before that gate.

## Crossfire read-only inspection

Map SHA-256: `6222243E0839022F3041E6B9E97776CE54D0CF87D4C3810E627B8B2815B555F5`.
72 BSP models; 71 submodels. There are no func_plat/func_train entities here.

| Map name | Lift BSP model / entity ordinal | Button BSP model / entity ordinal | Parameters |
| --- | --- | --- | --- |
| lift1 | *21 / 28 | *24 / 40 | speed 200, wait 1, lip 16 |
| lift2 | *30 / 46 | *31 / 47 | speed 200, wait 1, lip 16 |
| lift3 | *32 / 48 | *33 / 49 | speed 200, wait 1, lip 32 |
| lift4 | *34 / 50 | *35 / 51 | speed 200, wait 1, lip 16 |

All four are named upward (`angle=-1`) func_door objects with a targeting
func_button (`spawnflags=1`, no touch-only bit). In the pinned
[`buttons.cpp`](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/buttons.cpp)
these buttons install ButtonUse, whereas named doors ignore direct player
touch in
[`doors.cpp`](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/doors.cpp).
Test **E on the associated button**, not an assumed automatic trigger or E on
the lift. BSP ordinals in the table are explicitly not runtime entity IDs.
No map parameters, server logic, targets or activation packets were changed.

## Bounded diagnostics and manual handoff

The existing `live_application_outcome` native/PowerShell roundtrip carries
collision revision/count, ground entity/model/normal, local/server grounded,
step selections, maximum raw correction and eye correction jump, fallback
count/current/last reason, basevelocity availability and frozen-support policy.
Server and render transform-change counters include last changed runtime
entity and BSP model; compare *N with the inspection table. They are bounded
aggregate/last-event diagnostics, not a complete per-object timeline. A
different object's last event, PVS absence or unsupported render mode must not
be misreported as a stationary lift. No raw packets, secrets or per-frame trace
dump is introduced. Primary typed errors remain primary even with limited
prediction coverage.

Manual checklist (new build still needs user confirmation): flat walk/Shift;
uphill/downhill/sideways and steps; stationary platform walk/duck/jump; E on
the associated lift button; observe motion beside it and while standing on
it; jump/step off and return to world; verify Glock/crowbar, reload and HUD.
Moving-support smoothness is an observation to collect, **not a passed claim**.
The normal test does not require the optional 50HP addon.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Crossfire default, explicit Map, reference/off, damage-respawn-check and optional
TestStartHealth 50 are preserved.

## Final offline verification — 2026-09-27

Result is **partial D4**, not
`ground_slopes_and_linear_brush_prediction_offline_verified`:

- Slopes/steps: targeted reference defect fixed and offline verified.
- Stationary brush support: integrated and offline verified through the shared
  provider, seed, kernel, delayed replay and normal runtime host.
- Moving support: current-frame collision integrated; predictive carry pending.
- Lift activation/render path: map/button contract inspected; D3 GL preserved;
  actual reported lift activation/motion remains unobserved.

All logs are local artifacts under
`manual-artifacts/task-records/d4-ground-prechange-20260927`; source/tests/docs
are ordinary project files. Seed: `2532982175`.

| Verification | Result |
| --- | --- |
| Pre-edit Release baseline | 237 passed, 1 opt-in skip, 261588 assertions |
| Deliberate pre-fix uphill regression | Four failing independent coordinate/velocity expectations; red log retained |
| Final D4/reference-ground | 7 cases, 3025 assertions passed |
| Single final broad offline Release suite | 2118 passed, 22 skipped; 390812 assertions passed; no failures |
| Focused Release / Debug / ASan Debug | Each 517 passed, 1 opt-in skip; 295244 assertions passed; no ASan report |
| Core-only Release / alternate module | Build passed; 95 cases, 1424 assertions; both core/API and dependency-guard CTests passed |
| Actual generated core-only projects | 175 inspected; zero concrete HL1 source/library/project references |
| Eight selected offline replay/Null process CTests | 8/8 passed |
| Read-only crossfire GL + static lift collision | 52 assertions passed, GL errors=0 |
| Native and PowerShell diagnostics/restoration fixtures | Passed; strict decimal fields retained, primary error preserved |
| Launcher and optional test-50 fixtures | Passed; no live session |
| CheckOnly from System32 | reference/manual, off/damage-respawn, reference/manual/50HP passed |
| Source snapshot / Git | All 192 snapshot hashes preserved; diff --check passed; index empty, branch/HEAD unchanged |

The unsupported combined off/damage-respawn/50HP CheckOnly correctly rejected
that combination; 50HP remains reference/manual/crossfire-only. This was not
changed to make the check pass. Broad skips are optional asset roots, Windows
symlink/reparse/second-volume capabilities and hidden-SDL focus/capture. The
crossfire asset test was then opted in separately. There was no blanket CTest
or second broad-suite run.

Analytic fixtures cover zero, .125, .5, .9 and negative slopes, rejection of
slope 2, uphill/downhill/sideways movement, 100-command stationary contacts,
12/18/18.125-unit step thresholds and stepping off to the world. Brush fixtures
use distinct point/standing/duck BSP trees, translated/yaw-rotated solid,
invisible solid, non-solid/trigger, duck, jump/landing, removal/reuse/missing
model/map/metadata, frozen up/down/stop contexts and repeated delayed replay.
Existing H3/H4 host tests run 30/60/144 and irregular render intervals, now also
with the D4 committed brush scene: physics endpoint and command count remain
unchanged; multiple commands/update and later ACK rebase retain one history.
No separate gameplay callback, input consumption or TX runs during replay.
This does not cover every multi-plane junction or full moving-platform
entry/exit/ceiling/crush campaign; those remain explicitly unclaimed.

Controlled observations: first uphill step dx=.4, dz=.2, vx=20, vz=0; the five
walkable analytic slopes have exactly zero stationary origin drift over 100
commands. Replaying the same four-command suffix on a stationary brush has
raw position error exactly 0 and an identical endpoint signature, including
four repeated replays. These are not live raw-correction measurements. Existing
H3 camera continuity/blocking expectations passed without increasing smoothing.
The read-only lift *21 trace resolves the brush's upward plane within the
independent inspected top-height range; original D3 pixel controls remain
*1=4692, *12=2529, *21=71660, *38=4692 changed pixels.

Diagnostics regression initially exposed the old inert-token parser dropping
decimal metrics. Only the five named normal/error/jump numeric fields now admit
bounded signed decimal grammar; paths, arbitrary dotted strings and nan remain
rejected. Native and PowerShell tests cover both acceptance and rejection.
Intermediate compile/fixture failures and the deliberate red regression remain
in this task's artifacts; final build/test results above are the successful
ones. No expectations were mass-rewritten.

Normal Release `build/bin/Release/hlclient.exe` SHA-256:

`070C57E1AC8B25C4002247063003692C22498270E56209C829FD0B184B360DEF`

Release orchestrator SHA-256:
`F0288A17848CBA84E85BFD82DAA172B63158FFE5B7242B939521710F648550E9`.
Optional unchanged test-health helper SHA-256:
`AC96548EFF7E4B39FA34543C0E17CEC3DC666338332E971BCE7A97A0EA232703`.

No new GameClientAPI callbacks, Half-Life gameplay rules, wire values, tickrate,
updaterate, vsync, hull dimensions or MoveVars defaults were introduced. No new
live launch, Steam initialization, WFP, capture or ETW. No staging, commit or
push. New-build manual smoothness remains **not_run**.
