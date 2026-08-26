# Offline world viewer

`hlclient_world_viewer` is a network-free, read-only diagnostic for a
user-owned Half-Life installation. It validates and composes the complete CPU
world before creating SDL or OpenGL:

```text
safe virtual map name
    -> LocalResourceEnvironment
    -> verified BSP AssetSource
    -> BSP world geometry
    -> embedded/WAD3 RGBA textures
    -> RGB lightmaps and padded atlases
    -> immutable WorldRenderPackage
    -> WorldSpatialPackage + optional static brush scene
    -> per-frame WorldVisibilitySet + WorldVisibleDrawList
    -> WorldPreviewSceneSource
    -> OpenGL renderer
```

The viewer does not connect to a server, start Steam/HLDS/`hl.exe`, download or
cache a resource, modify a map, create a screenshot by default, or accept a
native map path. The `--map` value is a safe virtual name such as
`maps/crossfire.bsp`; all local access still passes through the existing
game-before-`valve` sandbox and same-handle verified source boundary.

## Usage

Build the `hlclient_world_viewer` target and run, for example:

```powershell
.\build\bin\Debug\hlclient_world_viewer.exe `
  --basedir "<Half-Life-root>" `
  --game valve `
  --map maps/<name>.bsp `
  --camera spawn `
  --visibility pvs-frustum `
  --brush-submodels static `
  --cull back
```

`--basedir`, `--game`, and `--map` select the explicit user-owned environment;
`--camera static` gives a deterministic still view and `--camera orbit` gives a
slow deterministic bounds orbit. `--camera spawn` selects the first valid
`info_player_start`, otherwise the first valid `info_player_deathmatch`, and
falls back to the bounds-derived camera if neither inert initial descriptor is
available. It does not create a player or apply gameplay spawn behavior.

`--visibility` accepts `all`, `frustum`, `pvs`, or `pvs-frustum`.
`--brush-submodels` accepts `off` or `static`, and `--cull` accepts `none` or
`back`. Historical behavior is preserved by the defaults `--visibility all
--brush-submodels off --cull none`; M4.4.1 verification opts into
`pvs-frustum`, `static`, and both cull modes explicitly. The PVS fallback policy
is frustum-only for the diagnostic preview when its camera has no usable PVS
row. Only `free-fly` consumes local keyboard/mouse input, and it remains a
diagnostic camera rather than gameplay control. A positive
`HLCLIENT_SMOKE_TEST_FRAMES` value bounds the frame loop for smoke/automation
runs; normal manual use runs until quit.

CPU import/package errors occur before SDL/OpenGL initialization. Upload or
render failure exits nonzero. Runtime output is bounded metadata such as
surface, texture, atlas, batch, draw-call, and triangle counts; it does not
print native paths, texture/lightmap bytes, entity text, or asset contents.

## Visual scope

The historical M4.3 preview shows static model 0 geometry with resolved
embedded and WAD3 base textures, masked index-255 surfaces, the baseline first
light-style layer,
depth testing, and a bounds-derived Z-up camera. Preview culling defaults to
none because a bounds camera is not guaranteed to be inside the closed world;
this deliberate double-sided diagnostic view does not change source winding.

M4.4.1's default BSP-v30 profile is
`valve_qbsp_clockwise_wire_to_counter_clockwise_render`. The CPU geometry
boundary reconstructs the signed-surfedge wire in Valve QBSP's clockwise
orientation relative to the side-adjusted normal, validates it strictly, and
reverses it deterministically into counter-clockwise renderer geometry. World
faces and brush-submodel faces share that builder. `--cull back` consumes this
canonical output directly; it is a proof mode, not a renderer-side winding
repair. See [GoldSrc BSP geometry compatibility](GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.md).

M4.4 opt-in modes add CPU PVS/frustum selection and supported static initial
opaque brush instances. Brush geometry reuses the world texture/lightmap
policies and is uploaded once independently of instance count. Camera or PVS
changes replace only the visibility/draw-list revision, not scene GPU
resources. Spawn mode is a diagnostic initial pose only.

The preview is not expected to show runtime doors/platforms/rotators, nonzero
or translucent rendermodes, dynamic light-style animation, dynamic lights, a
skybox, animated water, gameplay movement, entity updates, or live server
state.

## CPU compatibility gate

Run the CPU-only compatibility checker before opening an SDL/OpenGL window:

```powershell
.\build\bin\Debug\hlclient_bsp_compat_check.exe `
  --basedir "<Half-Life-root>" --game valve `
  --map maps/<name>.bsp --validate-through spatial-scene

.\scripts\verify_stock_bsp_geometry_compatibility.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_bsp_compat_check.exe `
  -Basedir "<Half-Life-root>" -Game valve `
  -Maps @("maps/<name>.bsp")
```

The checker is network-free, SDL/OpenGL-free, and write-free. The wrapper runs
each safe virtual map twice, requires identical deterministic metadata through
`spatial-scene`, and rejects BSP, WAD, or file-inventory drift. Neither command
prints native paths, asset bytes, texture names, entity text, or raw geometry.

## Read-only OpenGL verification wrapper

For a bounded drift-detecting check, use:

```powershell
.\scripts\verify_local_world_render.ps1 `
  -ViewerPath .\build\bin\Debug\hlclient_world_viewer.exe `
  -Basedir "<Half-Life-root>" `
  -Game valve `
  -Map maps/<name>.bsp `
  -Frames 2 `
  -Visibility pvs-frustum `
  -BrushSubmodels static `
  -Camera spawn `
  -CullModes @('none', 'back')
```

The wrapper snapshots relevant BSP/WAD size, hash, write time, and file
inventory before and after all requested runs. Each cull mode executes in its
own process for exactly two frames in the M4.4.1 proof. It requires exit code
zero, world and scene upload counts of one, non-clear pixels, nonzero draw-call
and triangle counts, exact reported cull mode, and `gl-error=none`. It also
validates the camera leaf, PVS fallback, visible/total surfaces, brush counts,
and unchanged external snapshots, then prints metadata only. `-CullModes`
defaults to the M4.4.1 acceptance set `none,back`. A single mode remains
available for a bounded focused diagnostic. The historical scene choices still
default to `all`, `off`, and `static`.

This OpenGL wrapper is optional and must run only on a host whose actual
context supports OpenGL 3.3 Core or newer. An unavailable or older context is a
graphical capability skip, not permission to skip the preceding CPU verifier.
No game asset, extracted data, or screenshot belongs in the repository.

## Network-built preview

The main client offers a separate `--view-world` mode. It completes the
network/resource pipeline, builds the same immutable scene, finalizes the
retained netchan and authentication lifetime exactly once, and only then opens
the local OpenGL preview. It accepts the same `--visibility`,
`--brush-submodels`, and `--camera` choices, requires `--renderer opengl`, and
honors the same frame-limit environment variable. The resulting view is a
diagnostic snapshot of resources received before cleanup, not a
gameplay-connected session.

For a CPU-only proof, `--stop-after world-render-package` builds and validates
the M4.3 package. `--stop-after world-spatial-scene` additionally builds the
M4.4 spatial/scene package and applies the selected brush/spawn policy, still
with no SDL window or GPU upload and valid with `--renderer null`.

## Local free-flight camera

The offline viewer also accepts `--camera free-fly`. It starts with the same
bounds-derived diagnostic camera but leaves the cursor released. Click captures
relative mouse input; Escape releases it; WASD moves horizontally, Space and
Control move vertically, Shift selects the project speed multiplier, and the
mouse changes yaw/pitch. This is a local noclip-style preview with no collision,
gravity, player movement, prediction, stock sensitivity, or network commands.

With `HLCLIENT_SMOKE_TEST_FRAMES` set, the hidden viewer does not request
capture and needs no physical input. Camera-only changes rebuild CPU visibility
when necessary while retaining exact world/brush/entity GPU resource identities.
See [interactive preview](INTERACTIVE_PREVIEW.md).
