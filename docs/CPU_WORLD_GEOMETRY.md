# CPU world geometry

M4.1 converts the validated world model (`models[0]`) of a GoldSrc BSP v30 file
into a neutral, owning `WorldAsset`. It does not traverse the BSP tree to choose
draw surfaces and does not emit geometry belonging only to brush submodels.
All model ranges are still validated; omitted face counts are retained in
statistics. Door, platform, and rotating-brush entity association belongs to a
later milestone. M4.4 applies the same parameterized reconstruction path
separately to `models[1..N]`. M4.4.1 makes that path a named shared builder and
aligns both world and brush fixtures with Valve's qbsp face convention.

## Face reconstruction

For each world-model face, surfedges are consumed in source order. A positive
surfedge uses `edge.v[0] -> edge.v[1]`; a negative surfedge uses
`edge.v[1] -> edge.v[0]`. Zero is rejected under the supported stock edge-0
sentinel policy, and `INT32_MIN` is rejected before negation. Each edge must end
where the next begins and the final edge must close on the first.

The face normal is the normalized source plane normal for side 0 and its
negation for side 1. Pinned Valve compiler evidence establishes that the raw
qbsp loop is clockwise relative to that side-adjusted normal: its standard
area-vector dot is strictly negative. The explicit
`valve_qbsp_clockwise_wire_to_counter_clockwise_render` profile is therefore a
single wire-format conversion, not a permissive retry. After bounded cleanup,
`[q0, q1, ..., q(n-1)]` becomes `[q0, q(n-1), ..., q1]`; the first retained
source corner remains the fan anchor.

Orientation uses a centroid-rebased double-precision area vector:

```text
c = sum(p[i]) / N
A = sum(cross(p[i] - c, p[(i + 1) % N] - c))
polygon_extent = max(axis-aligned polygon spans)
polygon_area_tolerance = clamp(
    64 * DBL_EPSILON * max(1, polygon_extent * polygon_extent),
    1e-12, 1e-4)
triangle_winding_tolerance = clamp(
    64 * DBL_EPSILON * max(1, triangle_extent * triangle_extent),
    1e-12, 1e-4)
```

Raw `dot(A, face_normal)` must be below `-polygon_area_tolerance`; canonical
whole-loop output must be above `polygon_area_tolerance`. Each local turn and
fan triangle is checked independently against the tolerance derived from that
triangle's own extent. Planarity remains a separate fixed `0.02` source-unit
limit.

Valve qbsp can insert valid T-junction points inside straight boundary edges.
The shared builder may remove only such a strictly interior middle corner. Its
source-float distance tolerance is:

```text
clamp(32 * FLT_EPSILON * max(1, maximum_absolute_coordinate),
      32 * FLT_EPSILON, 0.01)
```

The point-to-line distance must be within that tolerance, the normalized
incoming/outgoing direction dot must be at least `0.99`, and the projected
segment parameter must remain inside both endpoint margins
`distance_tolerance / segment_length`. Passes are bounded by the original
corner count, at least three corners must remain, and cleaned raw orientation
must remain negative. Retained positions and texture coordinates are not
altered.

The resulting polygon must have at least three distinct vertices, lie on its
plane, have no intersecting non-adjacent edges, and be consistently convex.
Invalid sides/references, edge zero, `INT32_MIN`, duplicate vertices, broken or
open loops, non-finite/nonplanar input, concavity, self-intersection,
zero/ambiguous area, the opposite raw convention, and failed cleanup remain
typed transactional errors. Pairwise intersection work has a named aggregate
default limit of 16,777,216 comparisons so hostile inputs cannot multiply the
bounded face-corner limit into unbounded CPU work.

Supported polygons use a deterministic fan:

```text
(v0, v1, v2), (v0, v2, v3), ...
```

One `WorldVertex` is emitted per retained canonical face corner. Vertices are
intentionally not deduplicated across faces so flat normals, texture mappings,
and future lightmap seams retain face ownership. Faces remain in source order;
corners retain the evidenced first source anchor followed by the deterministic
reversed tail.

## Neutral asset data

`WorldVertex` owns source-native X/Y/Z position, a flat face normal, and raw
texture S/T coordinates. Coordinate metadata states GoldSrc Z-up with no axis,
handedness, or unit conversion. Texture coordinates are raw texel units:

```text
s = dot(position, texinfo.s.xyz) + texinfo.s.w
t = dot(position, texinfo.t.xyz) + texinfo.t.w
```

They are not normalized by texture dimensions.

`WorldSurface` retains both its index range and its exact contiguous face-local
vertex range (`first_vertex`, `vertex_count`), plus material index, computed
face bounds, source face ordinal, optional source light offset metadata, four
light-style bytes, and the neutral special-surface flag derived from
`TEX_SPECIAL`. The explicit vertex range preserves face ownership for M4.3
lightmap extent calculation; it does not change M4.1 vertex/index output. The
surface contains no native BSP struct or lightmap samples.

One `WorldMaterialReference` is created on first use of each texinfo by a world
face. It owns the exact optional BSP texture-directory ordinal, optional
texture name/dimensions, missing/external/embedded storage state, raw source
texture flags, source texinfo ordinal, and named compatibility/evidence
profiles. M4.2 uses that ordinal to cross-check the retained BSP source instead
of matching material names heuristically. The reference contains no pixels,
palette, WAD path, shader, OpenGL handle, or lightmap texture.

`WorldAsset` owns identity, coordinate semantics, computed geometry bounds,
optional declared model bounds, vertices, indices, surfaces, materials, source
profile, and statistics. Main bounds are computed from every emitted position;
empty geometry or non-finite/inverted bounds fail. Statistics include source
version/model/face counts, world and skipped face counts, emitted surfaces,
vertices and triangles, material count, and each texture-storage count. No
native path or `AssetSource` bytes survive import.

## Runtime boundary

`--stop-after asset-dispatch` now uses the production BSP importer for a valid
selected version-30 world. `--stop-after world-geometry` follows the same
retained socket, resource response, manifest, verified-locator source opening,
and importer dispatch. It additionally requires non-empty CPU geometry and
prints only bounded counts, texture-storage totals, and finite-bounds status.
Both stops send no packet after manifest publication and perform no texture,
WAD, lightmap, PVS, collision, renderer, or GPU work. M4.2 adds a later explicit
`world-textures` stop; the two M4.1 stops do not invoke it. Texture names,
entity text, raw BSP bytes, and native paths are not logged.

For these two production stops, local resolution and same-handle source opening
share the parser's 32 MiB default source ceiling. A 1,024-event dispatch queue
accommodates the corresponding 512 default 64 KiB progress chunks plus bounded
lifecycle events. The 64 MiB hard ceiling and earlier stop-point defaults are
unchanged.

## Manual read-only verification

`scripts/verify_local_bsp_import.ps1` is the bounded wrapper for a user-owned
map check when the optional `hlclient_bsp_check.exe` tool is available. It
accepts an explicit Half-Life root, game directory, and safe `maps/*.bsp`
virtual name; native map paths are not accepted by the checker interface. The
wrapper captures checker output instead of echoing it, runs the checker twice,
requires byte-for-byte deterministic summaries, and reports metadata only.

M4.4.1 adds `hlclient_bsp_compat_check` and
`scripts/verify_stock_bsp_geometry_compatibility.ps1`. The tool can validate a
safe virtual map through geometry, textures, render package, or spatial scene;
the wrapper selects the full spatial-scene path, runs each selected map twice,
and requires identical normalized summaries.

The M4.4.1 wrapper snapshots each selected map's content, size, and write time,
the relevant WAD set, and the approved game-root inventory before execution
and after both runs. Any content, metadata, created-file, or deleted-file drift
fails closed. No user-owned BSP, entity text, texture data, native path, or raw
checker output is committed or printed. It records only bounded aggregate
diagnostics. Exact evidence, formulas, and the separately verified acceptance
summary belong in
[GoldSrc BSP geometry compatibility](GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.md),
never in tracked game data.

The separate M4.2 checker and resolution policy are documented in
[world texture resolution](WORLD_TEXTURE_RESOLUTION.md). They reuse this
geometry unchanged and stop at owning CPU RGBA textures.

M4.3 likewise consumes, but does not mutate, this geometry. It calculates
lightmap extents from every raw S/T value in the exact face-local vertex range,
normalizes base UVs only in the renderer-neutral package, and preserves source
Z-up coordinates and triangle winding. See
[GoldSrc world lightmaps](GOLDSRC_LIGHTMAPS.md) and the
[world render package](WORLD_RENDER_PACKAGE.md).

## M4.4 geometry reuse and spatial membership

M4.4 publishes each brush submodel as a separate owning local-space geometry
asset. M4.4.1 routes model zero and every brush model through the same
`GoldSrcFaceGeometryBuilder`, including the Valve wire-orientation profile,
centroid-rebased area test, scale-aware collinear gate, canonical order, raw
S/T evaluation, bounds, and deterministic fan triangulation. No alternate
face decoder or renderer-side BSP parsing is introduced.

The renderer-neutral spatial package maps leaf marksurfaces only to exact
model-0 source-surface ordinals. Faces owned exclusively by brush submodels do
not enter that membership table; instances instead retain transformed bounds
and their bounded set of intersecting, non-solid PVS-addressable world leaves.
Point and AABB queries follow the validated BSP node graph and remain CPU-only.
Geometry construction itself still performs no visibility selection, entity
behavior, collision runtime, filesystem access, or GPU work. See
[GoldSrc BSP spatial](GOLDSRC_BSP_SPATIAL.md) and
[GoldSrc brush submodels](GOLDSRC_BRUSH_SUBMODELS.md).
