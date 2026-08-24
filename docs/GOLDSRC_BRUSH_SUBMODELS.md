# GoldSrc brush submodels and inert entities

M4.4 imports BSP models 1..N as geometry resources and separately interprets
bounded entity metadata for static initial instances. It does not implement
doors, platforms, rotation updates, think/use/touch, interpolation, server
snapshots, or any other gameplay behavior.

## Geometry and coordinate evidence

Model zero and brush submodels use one parameterized face reconstruction path:
the same validated planes, vertices, edges, surfedges, faces, texinfo,
materials, winding checks, and fan triangulation. Every model face range must
be exact and every supported submodel must build transactionally.

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
