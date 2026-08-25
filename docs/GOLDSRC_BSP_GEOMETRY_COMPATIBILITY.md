# GoldSrc BSP geometry compatibility

M4.4.1 aligns the CPU geometry boundary with the face-loop convention emitted
by Valve's GoldSrc tools. The parser remains strict: this is a named format
compatibility profile, not a general option to accept reversed or damaged
polygons. World model faces and brush-submodel faces use the same rule and
publish one renderer-facing counter-clockwise representation.

## Original failure

The M4.4 starting commit,
`b7e1088e37e425033d849428ca59b1727cb98d0a`, reconstructed signed surfedges
correctly but interpreted their raw loop as if it were already in the
renderer-facing orientation. Network-free, read-only checks of three selected
user-owned Valve BSP-v30 maps therefore stopped in M4.1 geometry
materialization with `invalid_face_winding`, exit code 1. In each map ordinal,
the first bounded diagnostic was model 0, source face 0,
`negative_local_turn`.

That failure happened before texture resolution, lightmap construction,
spatial/PVS construction, brush-submodel publication, or OpenGL rendering. It
was not a renderer culling problem. A second stock compatibility issue is that
qbsp deliberately inserts T-junction points along otherwise straight polygon
edges. Such a valid point can occupy either of the first two positions after a
fan anchor, so testing only the first three corners or requiring every naive
fan triangle to have non-zero area rejects valid compiler output.

## Evidence method

The rule is based on three independent forms of evidence:

1. Aggregate-only scans of the three user-owned maps. The scans retained no
   paths, texture names, entity text, raw vertices, raw surfedge arrays, BSP
   bytes, or WAD bytes.
2. The public Valve HLSDK submodule pinned at
   `b1b5cf5892918535619b2937bb927e46cb097ba1`. It is used as format and compiler
   convention evidence only.
3. Independently authored deterministic fixtures covering both face sides,
   positive, negative and mixed surfedges, canonical conversion, collinear
   corners, scale, and malformed input.

No ReHLDS, Xash3D, decompiled executable, or reverse-engineered binary dump is
used as evidence or implementation source.

## Sanitized stock baseline

The following values are pre-fix diagnostic aggregates. They establish the
format convention; they are not post-fix pipeline acceptance results.

| Ordinal/category | Models | World faces | Brush faces | Total faces | Side 0 | Side 1 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 / `stock-bsp-v30` | 44 | 10,122 | 360 | 10,482 | 6,251 | 4,231 |
| 2 / `stock-bsp-v30` | 72 | 3,738 | 647 | 4,385 | 2,417 | 1,968 |
| 3 / `stock-bsp-v30` | 25 | 2,306 | 245 | 2,551 | 1,551 | 1,000 |

| Ordinal/category | Area dot + / - / zero | Negative / non-negative surfedges | Faces with a negative surfedge | Mixed-sign faces |
| --- | ---: | ---: | ---: | ---: |
| 1 / `stock-bsp-v30` | 0 / 10,482 / 0 | 24,578 / 24,745 | 9,370 | 8,224 |
| 2 / `stock-bsp-v30` | 0 / 4,385 / 0 | 10,077 / 10,135 | 3,873 | 3,356 |
| 3 / `stock-bsp-v30` | 0 / 2,551 / 0 | 6,028 / 6,171 | 2,263 | 1,963 |

Here, area dot is the dot product of the reconstructed raw loop's standard
area vector and the side-adjusted face normal. Every structurally valid world
and brush face was negative; none was positive or ambiguous. Surfedge sign is
not a winding selector: mixed signs are the ordinary result of globally shared
edges being traversed in face-local directions.

| Ordinal/category | Faces with exact collinear corners | Exact collinear corners | Consecutive duplicates | Broken loops | Nonplanar over 0.02 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 / `stock-bsp-v30` | 4,170 | 7,374 | 0 | 0 | 0 |
| 2 / `stock-bsp-v30` | 1,478 | 2,717 | 0 | 0 | 0 |
| 3 / `stock-bsp-v30` | 990 | 2,012 | 0 | 0 | 0 |

A bounded exact-collinearity pass classified all 7,374, 2,717, and 2,012
corners respectively as strictly interior and never reduced a face below three
corners. An intermediate reversal-plus-exact-cleanup experiment still left
9/1/9 faces with a non-positive local turn and 4/0/4 faces with a non-positive
naive fan triangle. Those are diagnostic observations motivating the bounded
float-aware rule below; they are not permission to take an absolute winding
value or accept both orientations.

The selected maps' hash, size, and modification time remained unchanged and
the checks created no game files.

## Valve format and compiler evidence

All references below are relative to `third_party/halflife-sdk` at the pinned
commit.

### Directed surfedges

`utils/common/bspfile.h:172-192` reserves edge zero, defines the two edge
vertices, and defines the face's `firstedge` and `numedges` range.
`utils/qbsp2/writebsp.c:119-145` emits one surfedge for every consecutive pair
`pts[i] -> pts[(i + 1) % count]`. `utils/qbsp2/surfaces.c:336-370` stores a new
edge as `v[0] = p1`, `v[1] = p2`; reuse of an existing edge in the opposite
direction returns its negative index. `utils/qrad/qrad.c:258-279` and
`utils/light/ltface.c:177-183` independently reconstruct the same start vertex.

For a non-zero signed surfedge `e`, the exact rule is:

```text
e > 0: start = edges[e].v[0],  end = edges[e].v[1]
e < 0: start = edges[-e].v[1], end = edges[-e].v[0]
```

Every end must equal the next start, and the final end must equal the first
start. Sign changes neither the plane side nor the polygon convention.

### Face side and emitted normal

`utils/qcsg/brush.c:144-178` creates paired planes `(N, d)` and `(-N, -d)`.
`utils/qbsp2/writebsp.c:132-143` stores
`dface.planenum = face.planenum & ~1` and
`dface.side = face.planenum & 1`. Valve's lighting tools apply the on-disk
meaning explicitly at `utils/light/ltface.c:549-555` and
`utils/qrad/lightmap.c:1393-1399`:

```text
side == 0: emitted normal =  N, emitted distance =  d
side == 1: emitted normal = -N, emitted distance = -d
```

Only 0 and 1 are supported side values.

### Raw wire orientation

Valve's winding plane calculation at `utils/common/polylib.c:104-112` uses
`cross(p2 - p0, p1 - p0)`. The map-plane calculation at
`utils/qcsg/brush.c:187-210` has the same sign. Qcsg constructs and clips a
winding for that oriented plane at `utils/qcsg/brush.c:721-734`, writes its
points without reordering at `utils/qcsg/qcsg.c:202-214`, and qbsp reads them
without reordering at `utils/qbsp2/qbsp.c:764-804`.

The strongest paired-plane cross-check is `utils/qcsg/qcsg.c:267-283`: when
qcsg toggles the oriented plane with `planenum ^= 1`, it explicitly reverses
the winding points before emitting the mirrored face. Consequently, for every
non-degenerate compiler wire:

```text
dot(standard_area_vector(raw_wire), side_adjusted_normal) < 0
```

The relation is identical for side 0 and side 1. It is also identical for the
world model and models 1..N: `utils/qbsp2/qbsp.c:816-867` sends every model
through the same T-junction, edge and face-emission path. Texture flags and
special surfaces do not select another geometry convention.

## Canonical wire-to-render rule

The explicit profile is
`GoldSrcFaceOrientationCompatibilityProfile::valve_qbsp_clockwise_wire_to_counter_clockwise_render`.
It is the default BSP-v30 project profile, not a CLI or runtime permissiveness
toggle.

After structural validation and bounded collinear cleanup, let the surviving
source-order loop be `[q0, q1, ..., q(n-1)]`. The raw loop must have a strictly
negative orientation margin against the side-adjusted normal. An ambiguous
near-zero relation or a positive raw relation is malformed under this profile.
Canonical conversion preserves the first surviving source corner and reverses
only the tail:

```text
[q0, q1, q2, ..., q(n-1)]
    ->
[q0, q(n-1), q(n-2), ..., q1]
```

There is no lexicographic, minimum-index, or geometry-dependent cyclic
rotation. The output must satisfy, for every non-degenerate emitted triangle:

```text
dot(cross(v1 - v0, v2 - v0), emitted_face_normal) >
    triangle_winding_tolerance
```

The output representation—not a renderer flag—is canonical counter-clockwise
geometry for back-face culling. A synthetic fixture that encoded an already
counter-clockwise raw BSP loop as though it were Valve compiler output must be
corrected at the fixture source; the old raw orientation belongs in the strict
malformed corpus.

## Robust area and tolerances

Orientation is not inferred from the first three corners. For `N` finite
positions, all calculations below use double precision. First compute the
centroid and a translation-resistant twice-area vector:

```text
c = (1 / N) * sum(p[i])
A = sum(cross(p[i] - c, p[(i + 1) % N] - c))
signed_area = dot(A, emitted_face_normal)
```

Rebasing around `c` avoids cancellation from a large world-coordinate offset.
Let:

```text
polygon_extent = max(bounds.max.x - bounds.min.x,
                     bounds.max.y - bounds.min.y,
                     bounds.max.z - bounds.min.z)

polygon_area_tolerance = clamp(
    64 * DBL_EPSILON * max(1, polygon_extent * polygon_extent),
    1e-12,
    1e-4)

triangle_winding_tolerance = clamp(
    64 * DBL_EPSILON * max(1, triangle_extent * triangle_extent),
    1e-12,
    1e-4)
```

Both tolerances have area units and are bounded above and below. Whole-loop
raw, cleaned, and canonical orientation use the polygon extent. Every local
turn and fan triangle independently uses that triangle's extent, so a large
polygon does not donate its tolerance to a smaller triangle. These are
separate from planarity, exact edge adjacency, and collinearity tolerances.

Planarity retains the existing fixed `0.02` source-unit limit. Every point is
checked against the selected on-disk plane; negating the plane for side 1 does
not change point membership.

## Bounded collinear-corner compatibility

Valve qbsp normally runs T-junction repair before face emission
(`utils/qbsp2/qbsp.c:857-865`). It records all face edges at
`utils/qbsp2/tjunc.c:239-263` and inserts points strictly between an edge's
endpoints at `utils/qbsp2/tjunc.c:384-416`. Its oversized-face splitter
explicitly recognizes collinear runs at `utils/qbsp2/tjunc.c:278-373`.
Therefore an intermediate collinear corner is compiler-valid, including one
that makes the first naive fan triangle degenerate.

Qrad reconstructs the stored loop and calls `RemoveColinearPoints` at
`utils/qrad/qrad.c:258-283`; its normalized-direction implementation is at
`utils/common/polylib.c:68-96`. The shared constants are
`ON_EPSILON = 0.01` and `EQUAL_EPSILON = 0.001` at
`utils/common/mathlib.h:39-41`. Qrad's operation is lighting-local, so it does
not justify arbitrary simplification. It does establish that a strictly
interior point on a straight, flat face is polygon-redundant for this bounded
compatibility case.

For source float coordinates, define:

```text
max_abs_coordinate = max(abs(component) for every polygon position)

distance_tolerance = clamp(
    32 * FLT_EPSILON * max(1, max_abs_coordinate),
    32 * FLT_EPSILON,
    0.01)
```

For consecutive candidates `A, B, C`, removal of `B` is allowed only when all
of the following hold:

```text
line_distance(B, segment_line(A, C)) <= distance_tolerance
dot(normalize(B - A), normalize(C - B)) >= 0.99

t = dot(B - A, C - A) / length_squared(C - A)
margin = distance_tolerance / length(C - A)
margin < t && t < 1 - margin
```

The point must also satisfy the face planarity check. Removal must preserve
loop closure and raw orientation, must not alter any retained position or UV,
and must leave at least three corners. Candidates are visited in deterministic
source-loop order; passes are bounded by the original corner count. `q0` is the
first surviving source corner—source corner 0 remains the canonical anchor
unless it independently passes this strict removal gate.

This rule does not remove endpoints, consecutive duplicates, off-segment
points, arbitrary near points, or non-collinear corners. It does not merge
vertices between faces. The statistics separately count canonicalized faces
and removed collinear corners so this compatibility behavior remains visible.

## Strict malformed policy

The profile continues to reject:

- surfedge zero, `INT32_MIN`, and edge or vertex references outside their
  validated lumps;
- face sides other than 0 or 1 and invalid plane references;
- broken adjacency, open loops, repeated/consecutive duplicate vertices, and
  fewer than three usable corners;
- non-finite coordinates, vertices more than `0.02` units off-plane, zero-area
  and ambiguous-area polygons;
- raw loops whose orientation is not the evidenced Valve negative relation;
- concave or self-intersecting polygons;
- cleanup candidates outside the strict interior-collinear gate or cleanup
  exceeding its pass bound;
- canonical output with a non-positive or ambiguous triangle-normal margin.

There is no `accept_reversed_faces` option, absolute-value winding test,
unconditional acceptance of both signs, or retry-until-it-renders path.

## Shared CPU and renderer boundary

The same face geometry builder and compatibility profile apply to model 0 and
models 1..N. Positions, raw texture S/T, lightmap ownership, face ordinals,
bounds, and material association follow the retained canonical corner order.
Normals and triangle indices agree before a `WorldAsset` or brush render model
is published.

The renderer receives only canonical counter-clockwise triangles. This
milestone does not invert `glFrontFace`, change global culling, flip normals in
a shader, reorder draw commands, or add a map-specific renderer workaround.
`CullMode::back` remains usable with the normal OpenGL front-face convention.

## Real-map CPU pipeline acceptance

The final network-free verifier selected three user-owned BSPs locally and
projected only ordinal plus the fixed `stock-bsp-v30` category. The tables map
those ordinals to the prompt-approved BSP basenames without publishing a native
path. The checker ran twice per ordinal with `--validate-through spatial-scene`;
both runs had byte-identical bounded summaries and terminal success. The
SHA-256 values below identify those summaries, not BSP or WAD contents. Content
hashes used for before/after drift checks remain local and are not projected.

These are post-fix acceptance values, distinct from the pre-fix diagnostic
counts above. The production scale-aware cleanup can classify additional
strictly interior corners beyond the earlier exact-collinearity scan, which is
why its removed-corner counts are 7,392, 2,721, and 2,039.

| Map / ordinal/category | Models | World / brush faces | Canonicalized / removed corners | Vertices / triangles | Minimum winding dot | Maximum planarity error |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `boot_camp` / 1 / `stock-bsp-v30` | 44 | 10,122 / 360 | 10,482 / 7,392 | 40,427 / 20,183 | 3.44000244140625 | 0.0049299772518907048 |
| `crossfire` / 2 / `stock-bsp-v30` | 72 | 3,738 / 647 | 4,385 / 2,721 | 14,897 / 7,421 | 8 | 0.0033869684980345482 |
| `stalkyard` / 3 / `stock-bsp-v30` | 25 | 2,306 / 245 | 2,551 / 2,039 | 9,193 / 4,581 | 0.76082342824591809 | 0.0064975884840237086 |

| Map / ordinal/category | Textures | Lightmap pages | PVS rows | Brush models / supported instances | Terminal state | Summary SHA-256 | External drift |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| `boot_camp` / 1 / `stock-bsp-v30` | 88 | 1 | 2,921 | 43 / 21 | `spatial-scene-success` | `c91fb62285e3c4a0c6a8a383a5e60443304c9198ad13e3056773b73a709a2467` | `none` |
| `crossfire` / 2 / `stock-bsp-v30` | 72 | 1 | 1,239 | 71 / 52 | `spatial-scene-success` | `cc8a5b8ec01f36f34962a63c2709a625ae8fb834dee533af2db234c4e1c20849` | `none` |
| `stalkyard` / 3 / `stock-bsp-v30` | 54 | 1 | 691 | 24 / 17 | `spatial-scene-success` | `b785a2edf160bf30a34f85d6614628aa98e28a18b6bb25928cdc82451e364827` | `none` |

### Sanitized verifier aggregate

The count totals below are exact arithmetic sums of the three accepted summary
rows. The verifier's aggregate SHA-256 is independently calculated over the
ordered `ordinal|summary-sha256` rows, so it remains sensitive to ordering or
any per-map summary change without disclosing asset identity.

| Metric | Accepted value |
| --- | ---: |
| selected ordinal/category rows | 3 |
| deterministic checker executions | 6 |
| models | 141 |
| world / brush faces | 16,166 / 1,252 |
| side 0 / side 1 faces | 10,219 / 7,199 |
| positive / negative surfedges | 41,051 / 40,683 |
| faces with a negative / mixed-sign surfedge | 15,506 / 13,543 |
| negative / positive / near-zero raw wire dots | 17,418 / 0 / 0 |
| canonicalized faces / removed collinear corners | 17,418 / 12,152 |
| vertices / triangles | 64,517 / 32,185 |
| textures / lightmap pages | 214 / 3 |
| PVS rows | 4,851 |
| brush models / supported instances | 138 / 90 |
| global minimum winding dot | 0.76082342824591809 |
| global maximum planarity error | 0.0064975884840237086 |

The aggregate summary SHA-256 is
`b9e12d857f00422c8a2b3f625ee47e2ab4f6ffb604ce0de344ebd482d90bba48`.
The verifier reported `network-operations=0`, `writes-performed=0`,
`created-files=0`, `deleted-files=0`, and `external-file-drift=none` after the
two runs per ordinal. The complete tracked sanitized projection is
[`docs/evidence/GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.json`](evidence/GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.json).

## Bounded OpenGL acceptance

After CPU success, all three ordinals completed the default dual `none,back`
cull proof on a capable OpenGL 3.3 Core host. Each cull mode ran in its own
process for two frames with `pvs-frustum` visibility, static initial brush
instances, and the spawn camera. The recorded counters are per cull-mode run;
both modes produced the same bounded counters shown for their map.

| Map / ordinal | Cull modes | Frames per mode | World uploads per mode | Draw calls per mode | Brush draws per mode | Triangles per mode | Non-clear pixels per mode | GL error | External drift |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| `boot_camp` / 1 | `none`, `back` | 2 | 1 | 1,204 | 80 | 2,512 | 921,597 | `none` | `none` |
| `crossfire` / 2 | `none`, `back` | 2 | 1 | 1,524 | 340 | 3,020 | 921,600 | `none` | `none` |
| `stalkyard` / 3 | `none`, `back` | 2 | 1 | 1,370 | 24 | 2,738 | 921,589 | `none` | `none` |

Every run exited successfully with nonzero draws, triangles, and framebuffer
proof. Back-face culling consumed the same canonical geometry as cull-none;
there was no global renderer inversion or malformed-face skip. The dual runs
left the selected BSP, relevant WAD inventory, and external file inventory
unchanged.

## Data and scope boundary

Only sanitized aggregate metadata and public source references belong in the
repository. No BSP, WAD, texture, lightmap, entity, raw face array, native path,
authentication material, manual capture, or game-owned derived file is added.
Local verification is network-free and read-only, with before/after inventory
and selected-file metadata checks. M4.4.1 does not begin M4.5, M4.6, or M3.3.
