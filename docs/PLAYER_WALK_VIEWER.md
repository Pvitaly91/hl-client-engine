# Player-walk viewer and movement checker

`hlclient_world_viewer --camera player-walk` is the visual M4.6.3.2 path with
the M4.6.3.2.1 wall-contact stability boundary. It
parses the selected user-owned BSP once, builds the immutable world render and
collision packages, selects a collision-valid dry spawn, and runs local fixed
synthetic commands against world-only collision. It opens no network socket
and performs no prediction or reconciliation.

M4.6.3.3 adds a separate `hlclient_prediction_viewer` composition root. It
reuses this viewer's world-only movement and rendering invariants but owns
bounded prediction history, an independent in-memory synthetic authority,
reconciliation, deterministic command replay, and camera-only visual
correction. The ordinary world viewer remains unchanged. See
[the prediction viewer](PREDICTION_VIEWER.md).

## Build and run

```powershell
cmake --build build --config Debug --target `
  hlclient_movement_check hlclient_world_viewer

.\build\bin\Debug\hlclient_world_viewer.exe `
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve `
  --map maps/crossfire.bsp `
  --camera player-walk `
  --visibility pvs-frustum `
  --brush-submodels static `
  --cull back `
  --movement-diagnostics summary
```

Controls are click to capture relative mouse, Escape to release, WASD to walk,
Space to jump, Ctrl to duck, and mouse motion to look. Movement uses yaw only;
look pitch affects the camera but not wish direction. The synthetic speed bit
does not scale movement in this kernel.

`static`, `orbit`, `spawn` and `free-fly` remain separate modes. In particular,
free-fly remains a noclip diagnostic with vertical Space/Ctrl motion and no
player collision or gravity.

## Spawn and environment

`LocalPlayerSpawnSelector` scans every `info_player_start` in source order,
then every `info_player_deathmatch` in source order. Ambiguous/invalid records,
non-empty contents and blocked standing hulls are skipped. No classname-based
brush solidity or stuck nudge is used. This is
`project_dry_walk_ordered_candidates_v1`, not stock spawn-selection semantics.

The offline viewer uses the explicit project-owned MoveVars baseline. It does
not invent a captured server state. Production movement collision is world
model zero only; visible stock brush submodels do not become solid merely
because `--brush-submodels static` renders them.

## Controller and camera boundary

`LocalPlayerMovementController` combines the existing fixed usercmd scheduler,
synthetic input adapter, movement kernel and player camera. One-shot input
edges remain pending until consumed by the first successfully simulated
command; later commands generated in the same update do not duplicate them.
It does not insert commands into network history or submit them to a transport.

The camera position is `movement origin + view_offset`, with normalized yaw,
bounded pitch and `GameplayCameraMode::player_walk`. Camera publication updates
CPU visibility through `ClientWorldState`; renderer input remains only camera
and scene state. Camera movement does not rebuild the immutable scene or
re-upload world/brush/entity GPU resources. A bounded OpenGL smoke requires one
world upload, one scene upload, a visible non-clear framebuffer and no GL
error on an actual OpenGL 3.3 Core host. Its selected-wall proof counts only
touches from commands committed by the controller; traces from discarded step
or direct candidates do not qualify.

`--movement-diagnostics off|summary` defaults to `off`. Summary mode retains a
bounded metadata-only overwrite history and never prints raw coordinates,
velocities, traces, BSP data or input recordings. If a typed scheduler,
movement or collision failure occurs, the viewer keeps the last valid state
and camera, disables further simulation, clears input, releases relative mouse
capture, logs once and continues rendering until close. The eventual process
status remains nonzero so a failure is not hidden.

## Read-only checker

```powershell
.\build\bin\Debug\hlclient_movement_check.exe `
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve `
  --map maps/crossfire.bsp `
  --scenario deterministic-route
```

Supported scenarios are `summary`, `spawn-settle`, `walk-forward`,
`strafe-wall`, `jump`, `step`, `duck`, `deterministic-route`,
`wall-contact-stress`, `wall-glance-stress`, `corner-contact-stress`,
`jump-wall-stress` and `duck-wall-stress`. Every checker
invocation executes its script twice and requires identical final state and
aggregate counters. Output contains aggregate counts and a SHA-256 digest of
the deterministic state signature, never raw positions or velocities.

For the required three-map read-only campaign:

```powershell
.\scripts\verify_local_player_movement.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_movement_check.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" `
  -Game valve `
  -Maps @('maps/boot_camp.bsp','maps/crossfire.bsp','maps/stalkyard.bsp') `
  -Scenarios @('spawn-settle','walk-forward','jump','deterministic-route')
```

The wrapper snapshots selected BSP hashes/sizes/timestamps and the complete
root file inventory, runs each map/scenario twice, requires zero network
operations and zero solid-start summaries, then reports
`created-files=0`, `deleted-files=0` and `external-file-drift=none`.

The focused wall campaign is wrapped by
`scripts/verify_player_wall_contact_stability.ps1`; it runs every wall scenario
twice per map/campaign, checks deterministic selection/route/final hashes and
executes the capability-gated scripted OpenGL viewer phase. See
[player wall-contact stability](PLAYER_WALL_CONTACT_STABILITY.md).

Known limitations are water/slime/lava/current movement, ladders, dynamic or
stock brush entity collision, conveyors/base velocity, moving platforms,
moving doors, Studio hitboxes, stuck recovery, fall damage, and stock/live
multiplayer prediction authority. The separate M4.6.3.3 synthetic prediction
tool does not change those stock limitations.
