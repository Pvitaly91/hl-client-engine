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
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve `
  --map maps/crossfire.bsp `
  --camera spawn `
  --visibility pvs-frustum `
  --brush-submodels static
```

`--basedir`, `--game`, and `--map` select the explicit user-owned environment;
`--camera static` gives a deterministic still view and `--camera orbit` gives a
slow deterministic bounds orbit. `--camera spawn` selects the first valid
`info_player_start`, otherwise the first valid `info_player_deathmatch`, and
falls back to the bounds-derived camera if neither inert initial descriptor is
available. It does not create a player or apply gameplay spawn behavior.

`--visibility` accepts `all`, `frustum`, `pvs`, or `pvs-frustum`.
`--brush-submodels` accepts `off` or `static`. Historical behavior is preserved
by the defaults `--visibility all --brush-submodels off`; M4.4 verification
must opt into `pvs-frustum` and `static` explicitly. The PVS fallback policy is
explicitly frustum-only for the diagnostic preview when its camera has no
usable PVS row. There are no keyboard/mouse gameplay controls. A positive
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

M4.4 opt-in modes add CPU PVS/frustum selection and supported static initial
opaque brush instances. Brush geometry reuses the world texture/lightmap
policies and is uploaded once independently of instance count. Camera or PVS
changes replace only the visibility/draw-list revision, not scene GPU
resources. Spawn mode is a diagnostic initial pose only.

The preview is not expected to show runtime doors/platforms/rotators, nonzero
or translucent rendermodes, dynamic light-style animation, dynamic lights, a
skybox, animated water, gameplay movement, entity updates, or live server
state.

## Read-only verification wrapper

For a bounded drift-detecting check, use:

```powershell
.\scripts\verify_local_world_render.ps1 `
  -ViewerPath .\build\bin\Debug\hlclient_world_viewer.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" `
  -Game valve `
  -Map maps/boot_camp.bsp `
  -Frames 2 `
  -Visibility pvs-frustum `
  -BrushSubmodels static `
  -Camera spawn
```

The wrapper snapshots relevant BSP/WAD size, hash, write time, and file
inventory before and after a bounded run. It requires exit code zero, at least
one draw call and a nonzero triangle count, validates the reported camera leaf,
PVS fallback, visible/total surfaces, and brush counts, rejects
created/deleted/modified files, and prints metadata only. The three new
parameters default to the historical `all`/`off`/`static` route.
`maps/boot_camp.bsp`, `maps/crossfire.bsp`,
and `maps/stalkyard.bsp` are suggested user-owned checks; no game asset or
screenshot belongs in the repository.

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
