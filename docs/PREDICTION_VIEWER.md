# Offline prediction checker and viewer

M4.6.3.3 provides separate offline composition roots for the synthetic
prediction profile:

- `hlclient_prediction_check` is a CPU-only deterministic route checker;
- `hlclient_prediction_viewer` is an OpenGL player-walk visualization.

Both read one explicit user-owned BSP through the existing sandbox, build
collision state locally, and use an in-memory synthetic authority. They open no
network socket, add no protocol message, write no game file, and do not claim
stock server reconciliation.

The ordinary `hlclient_movement_check` and
`hlclient_world_viewer --camera player-walk` remain non-prediction regression
tools. Prediction is not added as another world-viewer camera mode.

## Build

```powershell
cmake --build build --config Debug --target `
  hlclient_prediction_check hlclient_prediction_viewer `
  hlclient_movement_check hlclient_world_viewer
```

The required reference configuration remains Visual Studio 2022, MSVC v143,
Win32/x86, C++20, `/W4`, `/permissive-`, and warnings as errors.

## CPU checker

Example:

```powershell
.\build\bin\Debug\hlclient_prediction_check.exe `
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve `
  --map maps/crossfire.bsp `
  --scenario delayed-authority `
  --authority-delay-commands 8 `
  --commands 1000
```

Supported checker scenarios are:

- `exact-authority`;
- `delayed-authority`;
- `small-correction`;
- `velocity-correction`;
- `large-correction`;
- `teleport`;
- `stale-duplicate`;
- `wall-replay`;
- `jump-replay`;
- `duck-replay`;
- `mixed`;
- `deterministic-route`.

`--commands` defaults to 1000 and accepts 1..100000.
`--authority-delay-commands` defaults to 8 and accepts 0..64; when omitted for
`exact-authority`, it is normalized to 0. Scenario values
select bounded project-owned corrections; the CLI accepts no raw position,
velocity, authority payload, acknowledgement or command sequence.

Success output is metadata only:

```text
[prediction] profile=synthetic_authoritative_reconciliation_v1
[prediction] commands=<N>
[prediction] authority-updates=<N>
[prediction] acknowledgements=<N>
[prediction] reconciliations=<N>
[prediction] exact=<N>
[prediction] replays=<N>
[prediction] replayed-commands=<N>
[prediction] maximum-replay-depth=<N>
[prediction] small-corrections=<N>
[prediction] snaps=<N>
[prediction] stale=<N>
[prediction] duplicates=<N>
[prediction] history-high-water=<N>
[prediction] final-state-hash=<SHA-256>
[prediction] history-replay-hash=<SHA-256>
[prediction] history-overflow=0
[prediction] startsolid=0
[prediction] allsolid=0
[prediction] network-operations=0
[prediction] writes-performed=0
[prediction] result=success
```

Determinism verification may additionally retain a bounded history/replay hash.
No raw position, velocity, input recording, collision trace, BSP data or native
path is printed.

## OpenGL viewer

Example:

```powershell
.\build\bin\Debug\hlclient_prediction_viewer.exe `
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve `
  --map maps/crossfire.bsp `
  --scenario delayed `
  --authority-delay-commands 8 `
  --prediction-diagnostics summary `
  --visibility pvs-frustum `
  --brush-submodels static `
  --cull back
```

Viewer scenarios are `exact`, `small-correction`, `large-correction`,
`delayed`, `wall-replay`, `jump-replay`, `duck-replay`, and `mixed`. The
controls match player-walk: click captures relative mouse, Escape releases it,
W/A/S/D walk, Space jumps, Ctrl ducks, and mouse motion looks.

The viewer parses the BSP once, builds immutable render and collision packages,
selects a collision-valid local spawn, and releases the local file environment
before the render loop. Local commands are predicted, submitted to a separate
in-memory synthetic authority state, reconciled after the configured delay,
and displayed through the corrected camera. Visible static brush submodels do
not become collision evidence; movement remains world-only.

Simulation correction is immediate. A small correction may decay only as a
collision-constrained camera offset; large corrections and teleports snap. No
key or mouse button injects an arbitrary correction.

`--prediction-diagnostics off|summary` defaults to `off`. Summary mode may
report only bounded values such as history size, latest and acknowledged
command, replay depth, correction class, smoothing/constrained flags and
failure count. It does not dump coordinates, input or traces.

## Verification

The read-only wrapper is:

```powershell
.\scripts\verify_local_prediction_reconciliation.ps1 `
  -CheckerPath .\build\bin\Debug\hlclient_prediction_check.exe `
  -ViewerPath .\build\bin\Debug\hlclient_prediction_viewer.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" `
  -Game valve `
  -Maps @('maps/boot_camp.bsp','maps/crossfire.bsp','maps/stalkyard.bsp') `
  -Scenarios @('exact-authority','delayed-authority','small-correction',`
    'wall-replay','jump-replay','duck-replay','mixed') `
  -Iterations 20 `
  -Frames 1000
```

The CPU phase is mandatory and runs each route at least twice (the wrapper
default is 2), comparing final-state and history/replay hashes while requiring
no prediction error, history overflow,
start-solid or all-solid state. Before/after BSP metadata and root inventory
must report `created-files=0`, `deleted-files=0`,
`external-file-drift=none`, and `network-operations=0`.

On an actual OpenGL 3.3 Core host, the bounded viewer phase covers delayed
small correction, wall replay and the explicit teleport inside `mixed`. It requires a non-clear
framebuffer, `GL_NO_ERROR`, finite nonblocking player state, multi-frame small
decay, one-sample teleport snap, and exactly one world, scene and brush upload.
A typed unavailable/legacy OpenGL startup is a capability skip; shader, upload,
draw, swap, allocation and runtime failures are not skips.

## Failure and forbidden interfaces

On a CPU failure, the checker prints a bounded typed classification and returns
nonzero. On an interactive prediction failure, the viewer preserves the last
valid predicted state and camera, stops prediction, clears input, releases
capture, and keeps the renderer responsive where safe; it returns nonzero when
closed.

The tools do not expose `--authoritative-position`,
`--authoritative-velocity`, `--raw-authority`, `--authority-packet`,
`--force-ack`, `--force-command-sequence`, history/session bypasses,
`--stock-prediction`, correction transmission, state files or replay files.
Synthetic authority is strictly in-memory and never becomes a production
opcode.

Wall-contact stability remains a release gate for both tools. See
[local prediction](LOCAL_PLAYER_PREDICTION.md) and
[player wall-contact stability](PLAYER_WALL_CONTACT_STABILITY.md).
