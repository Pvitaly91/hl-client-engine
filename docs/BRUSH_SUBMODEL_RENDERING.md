# Static brush-submodel rendering

`BrushSubmodelRenderLibrary` is an immutable geometry/material resource table;
instances are separate. Model geometry, texture and lightmap decoding reuse the
M4.1, M4.2 and M4.3 implementations. One aggregate brush package may be
uploaded once and referenced by many instance transforms. No geometry is
uploaded per instance.

Brush texture resolution uses the same BSP miptex directory, embedded indexed
palette decoder, declared WAD3 catalogs, basename-only local-resource policy,
and source identity/evidence profile as model zero. Deduplication includes
source identity and profile and is not based only on matching RGBA bytes.
The builder verifies the retained BSP source fingerprint against the canonical
document before texture/lightmap decoding. Scene composition checks the world
and brush fingerprints again, so cross-document packages fail transactionally
instead of being associated by matching bounds.
Brush faces use the same exact lightmap extents, RGB/style ranges, padded atlas
policy, static source slot zero, and valid unlit binding as world faces.
Malformed lightmaps do not receive an invented white fallback.

`WorldSceneRenderPackage` owns or shares the M4.3 world package, owns the
spatial package, brush render library and bounded render-instance metadata,
and retains union bounds, statistics, compatibility/evidence profiles, and a
stable resource identity/revision. Unsupported instances do not enlarge scene
bounds. It contains no OpenGL handles, native paths, entity document, raw PVS,
or network state.

Per frame, the CPU resolver selects world surfaces and supported brush
instances by PVS/frustum policy. A brush is PVS-visible when at least one of its
touched non-solid leaves is in the camera PVS, and frustum-visible when its
transformed bounds intersects the view frustum. The draw-list builder expands
a visible instance into its model surface ranges.

The renderer computes `projection * view * model` for brush commands, while
world commands use the identity model. Depth, opaque/masked texture behavior,
and baseline light-style slot zero match the M4.3 path. Blending, translucent
rendermodes, dynamic brush motion, gameplay entity behavior, and network
snapshots remain outside M4.4.
