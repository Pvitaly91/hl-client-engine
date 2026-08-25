# World render package

M4.3 introduces `WorldRenderPackage`, the immutable CPU boundary between
validated GoldSrc assets and any renderer backend. It combines a complete
`TexturedWorldAsset`, its `WorldLightmapSet`, renderer-ready geometry,
materials, draw batches, bounds, coordinate metadata, statistics, and a stable
renderer resource identity/revision. It contains no native path, socket,
netchan/authentication state, SDL object, shader, or OpenGL handle.

```text
approved BSP bytes + WorldAsset + WorldTextureSet
    -> GoldSrcWorldLightmapImporter
    -> WorldLightmapSet
    -> WorldRenderPackageBuilder
    -> immutable WorldRenderPackage
    -> ClientWorldState -> RenderScene -> IRenderer
```

The package owns everything needed after the source and build stages are
destroyed. Backends retain only a shared immutable reference and must not
reopen files, consult `AssetManager`, or parse a BSP/WAD.

## Vertices and coordinates

`WorldRenderVertex` contains source-native GoldSrc Z-up position, flat normal,
normalized base UV, and normalized lightmap-atlas UV. The model transform is
identity; there is no hidden Y-up conversion.

M4.1 raw S/T values remain unchanged. M4.3 derives base coordinates from the
resolved texture dimensions:

```text
base_u = raw_s / texture_width
base_v = raw_t / texture_height
```

The CPU does not apply `fract`, wrapping, or a vertical flip. Texture repeat is
a sampler policy. Lightmap UVs follow the extent and texel-center atlas formula
documented in [GoldSrc world lightmaps](GOLDSRC_LIGHTMAPS.md), also with no CPU
flip. All derived coordinates must be finite.

M4.4.1 guarantees that every incoming world or brush face was produced by the
same `GoldSrcFaceGeometryBuilder`. The builder validates Valve's clockwise qbsp
wire against the side-adjusted plane normal using a centroid-rebased
double-precision area vector, applies only the bounded strictly-interior
T-junction collinear rule, and publishes counter-clockwise triangles. The
render package never guesses winding from the first three corners and never
repairs geometry. Its input invariant is:

```text
dot(cross(v1 - v0, v2 - v0), flat_face_normal) >
    triangle_winding_tolerance
```

The builder derives polygon-area and triangle-winding tolerances separately
with `clamp(64 * DBL_EPSILON * max(1, extent^2), 1e-12, 1e-4)`, using the
whole polygon extent for the former and each triangle's extent for the latter.
Its separate planarity limit is `0.02` source units. Collinear distance is bounded by
`clamp(32 * FLT_EPSILON * max(1, max_abs_coordinate),
32 * FLT_EPSILON, 0.01)`, additionally requiring same-direction dot `>= 0.99`
and a strictly interior projected segment parameter. Full derivation and
strict malformed cases are documented in
[GoldSrc BSP geometry compatibility](GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.md).

## Materials and batches

One neutral `WorldRenderMaterial` identifies:

- its deterministic render-material index and resolved base-texture asset;
- opaque or masked base alpha mode;
- atlas or `unlit_white` lightmap mode;
- an atlas page when lightmapped;
- the source special-surface flag;
- named compatibility and evidence profiles.

Materials contain no shader identifier or GPU texture name. Masked and opaque
surfaces remain different materials and batches.

`WorldDrawBatch` records a checked first-index/count range, render-material
index, alpha and lightmap modes, optional atlas page, and source-surface count.
The deterministic grouping key is:

```text
base texture asset + alpha mode + lightmap page/mode + special-surface state
```

Source surface order drives construction, and the triangle order within each
surface is unchanged. Each source triangle appears exactly once in the final
index stream.

M4.4 also retains one exact `WorldRenderSurfaceRange` per source surface. It
records the source ordinal, checked first/count index range, material, bounds,
source batch, alpha/lightmap mode, and optional atlas page. These ranges are
derived during the existing deterministic batching pass: they neither change
M4.3 geometry/resource identity nor require a second index buffer. Visibility
uses them to select individual surfaces without rebuilding or re-uploading the
package; the original full-batch path remains valid when no draw list exists.

## Builder and validation

`WorldRenderPackageBuilder` is a pure caller-driven composition step. It takes
ownership of a complete textured world and lightmap set plus explicit
`WorldRenderPackageLimits`; on success it publishes one all-or-nothing package.
Typed failures cover an invalid prerequisite, incomplete texture set,
lightmap/surface mismatch, invalid ranges or material bindings, invalid texture
dimensions or coordinates, invalid atlas bindings, batch/output limits, and
retention failure.

Before publication the builder verifies, among other invariants:

- the texture set is complete, every material texture is resolved, and every
  retained world material is referenced by at least one surface;
- world-material storage, source index, dimensions, and closed profiles match
  the resolved texture binding exactly;
- the lightmap binding count exactly equals the surface count;
- surface vertex/index ranges and every material/page index are valid;
- render coordinates and bounds are finite;
- the index count is divisible by three;
- batch ranges cover valid indices and every source triangle exactly once;
- base texture, lightmap, geometry, and aggregate CPU byte limits hold.

No partial package is exposed after failure. Default limits allow 4,194,304
vertices, 12,582,912 indices, 8,192 materials, 65,535 batches, 256 MiB each of
base-texture and lightmap bytes, and 768 MiB of aggregate CPU render data.

## Scene and camera model

`ClientWorldState` may hold a shared immutable package, a neutral camera,
preview options, and a world revision. M4.4 optionally adds an immutable
`WorldSceneRenderPackage`, a `WorldVisibilitySet`, and a
`WorldVisibleDrawList`, with separate scene-resource and per-frame visibility
revisions. Scene conversion maps these values into `RenderScene`, whose
`RenderStaticWorld` retains the package reference, cull/baseline-style policy,
and optional scene/draw-list references. The scene still owns its clear color
and exposes no GoldSrc protocol object.

`RenderCamera` is a Z-up look-at camera with position, target, up vector,
vertical field of view in radians, and positive near/far planes. Validation
requires finite values, distinct position and target, a nonzero up vector,
nonparallel forward/up directions, a safe field of view, and `far > near > 0`.
A zero render extent or invalid camera skips/fails world drawing safely.

`WorldPreviewSceneSource` is diagnostic, not a gameplay camera. Its historical
static/orbit modes derive a finite center and radius from package bounds, use a
deterministic isometric direction based on `(1, -1, 0.75)`, target the center, keep
`(0, 0, 1)` as up, and chooses bounded near/far distances from the radius. Its
optional orbit is elapsed-time deterministic and bounded; static mode never
moves. M4.4 spawn mode may consume one already validated inert initial camera
descriptor and falls back to the bounds camera if none is available. It creates
no player, consumes no keyboard/mouse gameplay input, and generates no user
commands.

The preview defaults to `RenderCullMode::none`. Because bounds do not identify
a valid in-world player spawn, the diagnostic camera may view the outside of a
closed world shell. Double-sided rendering keeps that shell visible without
a renderer-side winding workaround. M4.4.1 canonical geometry also supports
the implemented `RenderCullMode::back` path with the normal front-face
convention.

## Stage and lifetime boundary

`--stop-after world-render-package` continues the existing pipeline through
complete textures, imports lightmaps, builds the package, and exits without
initializing SDL or uploading OpenGL resources. It works with the null renderer
because it is a CPU-only boundary.

`WorldRenderPackageStage` sends no new semantic network message. Once the
package is published it finalizes the retained driver and authentication
lifetime exactly once. `--view-world` starts its local OpenGL diagnostic only
after that cleanup. Rendering may then outlive all network/resource stages,
but it is not a gameplay connection: the renderer owns only the immutable CPU
package reference.

M4.4's `world-spatial-scene` continuation builds the spatial package and
renderer-neutral scene entirely on the CPU and works with the null renderer.
Optional static brush geometry/instances and diagnostic spawn-camera
extraction reuse one bounded inert entity document parse. The returned scene
retains no entity document, BSP/PVS source bytes, path, network state, or GPU
handle. Per-frame PVS/frustum results change only the visibility revision;
scene identity continues to control resource upload.

The M4.3 package and historical all-surfaces/full-batch defaults remain
available. M4.4 still excludes runtime entity behavior, dynamic brush motion,
translucent rendermodes, dynamic lights/styles, animated/water/sky effects,
gameplay input, server snapshots, and M3.3 downloads/cache. See
[world visibility](WORLD_VISIBILITY.md) and
[brush-submodel rendering](BRUSH_SUBMODEL_RENDERING.md). M4.4.1 stock
acceptance uses the same package path through a network-free, read-only checker
or viewer wrapper; those tools accept safe virtual names, report bounded
metadata, detect external-file drift, and never commit user-owned assets.
