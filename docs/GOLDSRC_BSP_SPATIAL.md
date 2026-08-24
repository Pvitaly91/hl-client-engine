# GoldSrc BSP spatial package

M4.4 extends the canonical BSP v30 parser with an owning, renderer-neutral
spatial handoff. The geometry importer and the spatial builder consume the
same validated plane, node, leaf, marksurface, face, and model records; there
is no second wire-layout implementation.

## Package boundary

`WorldSpatialPackage` owns normalized planes, node children, leaves,
leaf-to-world-surface membership, the model-0 root, a deduplicated PVS table,
bounded statistics, and compatibility/evidence profiles. It owns no BSP byte
span, path, network object, renderer handle, or OpenGL name.

GoldSrc node children retain their source order. Child zero is the front
half-space and child one is the back half-space. A non-negative child is a node
index. A negative child uses the GoldSrc conversion `leaf = -1 - child`; the
conversion is checked before publication. The model-0 `headnode[0]` is the
world traversal root. Cycles, invalid children, invalid roots, and traversal
work beyond configured limits are failures.

Leaf zero is retained for exact BSP traversal and bounds checks, but it is not
PVS-addressable. PVS bit zero identifies source leaf one. Solid/special leaves
are retained with an explicit flag so camera and instance policies can make a
typed fallback decision.

## Marksurfaces

Each leaf's `firstmarksurface`/`nummarksurfaces` range is validated against the
canonical marksurface table. A marksurface names a source face ordinal. The
builder maps that ordinal to the model-0 `WorldSurface` which retains the same
`source_surface_ordinal`. Duplicate references within a leaf are deduplicated;
one world surface may legitimately occur in several leaves. Faces owned only
by models 1..N are not inserted into model-0 membership.

## Queries

`WorldSpatialQuery::locate_leaf` classifies a finite point against normalized
planes and follows the exact front/back child order from the model-0 root.
`collect_intersecting_leaves` classifies a finite ordered AABB, traverses both
children when it crosses a plane, preserves deterministic front-before-back
order, and deduplicates leaves. Both operations are bounded and cycle-safe.

The spatial layer does not decide rendering, parse entities, decode textures,
or issue network or GPU operations. Visibility consumes this package through
format-neutral query and membership APIs.
