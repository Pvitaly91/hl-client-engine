# Renderer-neutral world visibility

M4.4 separates immutable scene resources from per-frame visibility. A camera
movement may replace a `WorldVisibilitySet` and `WorldVisibleDrawList`; it does
not rebuild texture/lightmap data or upload geometry again.

## Frustum convention

`WorldViewFrustum` extracts left, right, bottom, top, near, and far planes from
the column-major OpenGL view-projection matrix used by `RenderMatrix4`.
Matrices multiply column vectors and are uploaded with `transpose = false`.
Planes are finite and normalized. An AABB is outside only when all of its
positive support point lies behind a plane; intersecting boxes remain visible.

## Modes and PVS fallback

`WorldVisibilityMode` supports `all`, `frustum_only`, `pvs_only`, and
`pvs_and_frustum`. PVS modes locate the camera leaf, obtain its row, explicitly
include the camera leaf, gather exact leaf surface membership, and deduplicate
surface indices. Frustum modes test leaf bounds and then surface bounds.
Results return in deterministic source-surface order.

PVS failure is never an implicit guess. The configured policy is one of
`fail_closed`, `frustum_only`, or `all_surfaces`, and the immutable result
records a typed reason such as leaf zero, solid leaf, failed point query,
unavailable row, or absent visibility data.
The diagnostic preview emits at most one bounded warning for each fallback
reason during one scene-source lifetime, so a per-frame fallback cannot flood
logs.

Visibility statistics distinguish total brush instances from the subset that
supports static opaque rendering. PVS-culled brush counts are measured from
that supported subset to its PVS survivors; frustum-culled counts are measured
from PVS survivors to final visible instances. The same invariants hold for
`fail_closed`, `frustum_only`, and `all_surfaces` fallback results.

## Draw-list boundary

M4.3 `WorldRenderPackage` now retains one exact `WorldRenderSurfaceRange` per
source surface: first/count indices, material, bounds, alpha/lightmap mode and
atlas page. `WorldVisibleDrawListBuilder` validates those adapters and emits
renderer-neutral commands. Ordering is stable: opaque world, masked world,
opaque brush instances, then masked brush instances. Brush commands retain a
model matrix and source model/instance identity; they contain no BSP/entity
bytes.

The builder validates each surface range and brush-model range once. It retains
sorted source-surface, model, and instance lookup tables and resolves visible
ordinals with binary search, so selection does not perform a linear scan for
every visible object or revalidate one model for each of its instances.

The OpenGL backend draws the command list when present and retains the M4.3
full-batch path when it is absent. Scene-resource identity controls GPU upload;
visibility revision controls only command selection. Statistics keep bounded
counts for PVS candidates, frustum survivors, culled surfaces/instances,
commands, triangles, uploads, and visibility revisions.

Every visibility set and derived draw list also retain the neutral scene
resource identity/revision they were built against. `ClientWorldState` and the
renderer reject a cross-scene pair even when its visibility revision happens
to match. The scene revision covers spatial topology/PVS membership, brush
model mappings, transforms, bounds, and leaf links as well as render-resource
revisions. Republish of an equivalent immutable scene object with the same
identity/revision reuses the existing GPU allocation.
