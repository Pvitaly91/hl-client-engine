# GoldSrc brush submodels and inert entities

M4.4 imports BSP models 1..N as geometry resources and separately interprets
bounded entity metadata for static initial instances. It does not implement
doors, platforms, rotation updates, think/use/touch, interpolation, server
snapshots, or any other gameplay behavior.

## Geometry and coordinate evidence

Model zero and brush submodels use one parameterized face reconstruction path:
the same validated planes, vertices, edges, surfedges, faces, texinfo,
materials, winding checks, and fan triangulation. M4.4.1 names that path
`GoldSrcFaceGeometryBuilder`; there is no brush-only orientation branch. Every
model face range must be exact and every supported submodel must build
transactionally.

The pinned Valve qbsp convention is explicit. A positive surfedge traverses
`edge.v[0] -> edge.v[1]`, a negative one traverses
`edge.v[1] -> edge.v[0]`, and `face.side == 1` negates the selected plane
normal. The closed raw wire is clockwise relative to that side-adjusted normal.
The shared `valve_qbsp_clockwise_wire_to_counter_clockwise_render` profile
requires that negative area-normal relation, preserves the first retained
source corner, and reverses the remaining corners to publish canonical
counter-clockwise triangles. Surfedge sign and model index never select a
different rule.

The builder computes a centroid-rebased double area vector and uses
`clamp(64 * DBL_EPSILON * max(1, extent^2), 1e-12, 1e-4)` for its area margin.
Planarity remains fixed at `0.02` source units. Compiler-inserted T-junction
corners may be removed only by the bounded strictly-interior collinear gate:
distance uses
`clamp(32 * FLT_EPSILON * max(1, max_abs_coordinate),
32 * FLT_EPSILON, 0.01)`, adjacent normalized directions must have dot at least
`0.99`, and the projected point must be inside both tolerance-scaled endpoint
margins. No duplicate, off-segment, nonplanar, concave, self-intersecting,
ambiguous, or oppositely encoded face is repaired.

Thus brush normals, raw S/T values, bounds, lightmap extents, and triangle
indices all derive from the same canonical candidate as model zero. OpenGL
receives the canonical result and does not invert front-face state, culling, or
shader normals. See
[GoldSrc BSP geometry compatibility](GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.md).

The supported `BrushSubmodelCoordinateProfile` follows the pinned Valve qcsg
origin-brush path: qcsg records the entity origin and subtracts it from brush
geometry, so retained submodel geometry is entity-origin-relative and the
entity origin supplies the initial translation. A nonzero BSP `dmodel_t`
origin is rejected by the supported profile instead of being silently folded
into a guessed transform.

Entity angles are `[pitch, yaw, roll]`. The supported column-vector matrix uses
Valve's AngleMatrix order `(YAW * PITCH) * ROLL`. The `angle` shorthand maps a
normal value to yaw; the evidence-confirmed `-1` and `-2` values mean straight
up and straight down. There is no scale.

Evidence is pinned to public Half-Life SDK/compiler sources, including qcsg
origin-brush processing and `cl_dll/studio_util.cpp` AngleMatrix construction;
synthetic nonzero-translation and rotation fixtures cross-check the profile.

## Entity document

`GoldSrcEntityDocumentParser` accepts only ordered quoted key/value pairs in
braced records. It performs no execution and no escape, path, locale, or shell
interpretation. Limits bound source bytes, entities, total/per-entity pairs,
keys, and values. The older worldspawn WAD reader is now a consumer of this
canonical parser and preserves the M4.2 basename-only sandbox policy.

M4.4 interprets only `classname`, `model`, `origin`, `angles`, `angle`,
`rendermode`, and `renderamt`. A brush reference is exactly `*` followed by a
positive bounded decimal model index; `*0`, signs, suffix whitespace, and
overflow are invalid. Exact duplicate or ASCII-case-colliding interpreted keys
make the candidate ambiguous; there is no last-value-wins policy.

Only absent or decimal `rendermode = 0` is renderable. Nonzero modes are kept
with a typed unsupported status and never rendered as opaque. Instance bounds
are transformed from all eight local AABB corners, queried against the world
spatial tree, and retain deduplicated non-solid PVS-addressable touched leaves.

## M4.6.3.1 collision boundary

The renderer-facing transform now delegates its rigid math to the neutral
`BrushRigidTransform` API. Collision can transform world segments to
model-local space, trace an exact compiled hull, preserve its fraction, and
rotate the hit normal/plane back to world space without depending on renderer
or OpenGL types. The broad phase rejects only against a conservative bound
proven from exact axial constraints in the selected BSP hull tree; unproven
source bounds always fall through to the exact trace. It never substitutes an
AABB for a rotated BSP hull.

Collision models exist for BSP models 1..N, but model presence is not evidence
of runtime solidity. The stock role provider returns `evidence_pending`; only
an explicit synthetic provider may opt an instance into solid composition.
Doors, platforms, water, illusionaries, and triggers are not guessed from
classnames. See [static brush collision](STATIC_BRUSH_COLLISION_BOUNDARY.md).
