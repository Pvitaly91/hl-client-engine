# GoldSrc Studio MDL v10 import profile

M4.5.2 implements a clean-room, CPU-only importer for the little-endian Valve
GoldSrc Studio `IDST` version 10 profile. The importer decodes untrusted bytes
field by field and publishes an owning, renderer-neutral skeletal model. It
does not include or cast to Half-Life SDK wire structs, skin vertices, choose a
body or skin, play an animation, execute an event, bind a server entity, or
create a GPU resource.

## Wire records

The main `IDST` header is exactly 244 bytes. Its leading fields are the
four-byte ID, signed version 10, 64-byte fixed name, and signed declared length
at offsets 0, 4, 8, and 72. An external sequence source begins with an exact
76-byte `IDSQ` version 10 header. `IDSQ` is never a top-level model.

Every main-header field is decoded at the following fixed wire offset; no
native `studiohdr_t` layout or `sizeof` value participates in decoding:

| Offset | Bytes | Wire field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 4 | `id` | exact bytes `IDST` |
| 4 | 4 | `version` | `i32le`, exactly 10 |
| 8 | 64 | `name` | fixed printable-ASCII-compatible bytes |
| 72 | 4 | `length` | `i32le` declared model length |
| 76 | 12 | `eyeposition` | three `f32le` values |
| 88 | 12 | `min` | three `f32le` movement-hull minimum values |
| 100 | 12 | `max` | three `f32le` movement-hull maximum values |
| 112 | 12 | `bbmin` | three `f32le` clipping-box minimum values |
| 124 | 12 | `bbmax` | three `f32le` clipping-box maximum values |
| 136 | 4 | `flags` | raw `i32le` flags |
| 140 | 4 | `numbones` | `i32le` bone count |
| 144 | 4 | `boneindex` | `i32le` bone-table offset |
| 148 | 4 | `numbonecontrollers` | `i32le` bone-controller count |
| 152 | 4 | `bonecontrollerindex` | `i32le` bone-controller-table offset |
| 156 | 4 | `numhitboxes` | `i32le` hitbox count |
| 160 | 4 | `hitboxindex` | `i32le` hitbox-table offset |
| 164 | 4 | `numseq` | `i32le` sequence count |
| 168 | 4 | `seqindex` | `i32le` sequence-table offset |
| 172 | 4 | `numseqgroups` | `i32le` sequence-group count |
| 176 | 4 | `seqgroupindex` | `i32le` sequence-group-table offset |
| 180 | 4 | `numtextures` | `i32le` texture count |
| 184 | 4 | `textureindex` | `i32le` texture-descriptor-table offset |
| 188 | 4 | `texturedataindex` | `i32le` embedded texture-data offset |
| 192 | 4 | `numskinref` | `i32le` skin-reference count |
| 196 | 4 | `numskinfamilies` | `i32le` skin-family count |
| 200 | 4 | `skinindex` | `i32le` skin-reference-table offset |
| 204 | 4 | `numbodyparts` | `i32le` bodypart count |
| 208 | 4 | `bodypartindex` | `i32le` bodypart-table offset |
| 212 | 4 | `numattachments` | `i32le` attachment count |
| 216 | 4 | `attachmentindex` | `i32le` attachment-table offset |
| 220 | 4 | `soundtable` | unsupported sound metadata; must be exactly zero |
| 224 | 4 | `soundindex` | unsupported sound metadata; must be exactly zero |
| 228 | 4 | `soundgroups` | unsupported sound metadata; must be exactly zero |
| 232 | 4 | `soundgroupindex` | unsupported sound metadata; must be exactly zero |
| 236 | 4 | `numtransitions` | `i32le` transition count |
| 240 | 4 | `transitionindex` | `i32le` transition-table offset |

| Record | Bytes |
| --- | ---: |
| `IDST` header | 244 |
| `IDSQ` header | 76 |
| bone | 112 |
| bone controller | 24 |
| hitbox | 32 |
| sequence-group descriptor | 104 |
| sequence descriptor | 176 |
| sequence event | 76 |
| pivot | 20 |
| attachment | 88 |
| animation-offset record | 12 |
| bodypart | 76 |
| texture descriptor | 80 |
| submodel | 112 |
| mesh | 20 |
| triangle-command vertex | 8 |
| skin reference | 2 |
| source vertex | 12 |
| source normal | 12 |

Every integer is decoded explicitly in little-endian order. A float is formed
by decoding its `uint32_t` bits and using `std::bit_cast<float>`; non-finite
geometry, bounds, controller, bone, attachment, or sequence values are
rejected. Fixed strings stop at their first NUL and otherwise follow the
bounded printable-ASCII profile. Names remain inert metadata and never become
native paths or commands.

The declared length must include the header and cannot exceed the supplied
source. All non-empty tables use nonnegative signed counts and offsets, checked
`count * record_size`, checked range ends, profile limits, and containment in
that declared length. The supported strict profile rejects unexplained bytes
after the declared length and rejects undocumented overlap between physical
records. Ordered physical-range and variable-stream-start indices preserve
exact-alias rejection and bound each stream at the next known record or stream
without quadratic all-pairs scans. The sound-table and sound-group record
grammars are outside this visual importer profile, so `soundtable`,
`soundindex`, `soundgroups`, and `soundgroupindex` must all be zero. Other
zero-count tables are never dereferenced under their documented marker policy.
Validation is transactional: no partial `ModelAsset` is returned.

## Neutral data and coordinates

`SkeletalModelAssetData` retains the source-native GoldSrc Z-up coordinates and
units. Source positions and normals remain in their owning bone-local spaces;
the importer applies neither entity transforms nor skinning. A skinned source
vertex preserves position, normal, signed raw texture-space S/T, position-bone
index, and the independently evidenced normal-bone index. Normals must be
finite and their bone references valid, but the explicit source-normal policy
retains an exact zero vector without normalization or repair. Valve-compatible
v10 assets reference such vectors, and the pinned renderer forwards source
normals without imposing a positive-length precondition.

The owning result retains ordered bones, controllers, hitboxes, attachments,
bodyparts, submodels, meshes, textures, skin families, sequences, events,
pivots, compressed animation tracks, sequence-group metadata, transitions,
statistics, and compatibility/evidence labels. Bone parents are `-1` or a
valid index, self-parenting and cycles are rejected, and multiple roots are
allowed. Controller references, hitbox/attachment bones, mesh skin-reference
slots, skin texture indices, and vertex/normal bones are cross-validated.

## Geometry and textures

A mesh command stream repeats signed 16-bit command counts until an exact zero
terminator. Positive counts are strips; negative counts are fans; `INT16_MIN`
and absolute counts below three are invalid. Each command vertex is four
signed 16-bit values: vertex index, normal index, raw S, and raw T. Strips emit
`(0,1,2), (2,1,3), (2,3,4), ...`; fans emit `(0,1,2), (0,2,3), ...`. There is
no global winding inversion. The explicit output count must equal the source
mesh triangle count, and indices, bones, command termination, finite vectors,
and aggregate limits are checked before publication. Valve v10 command streams
can include declared degenerate strip and fan triangles, both through repeated
source indices and through distinct collinear positions. The pinned renderer
submits every command vertex and the mesh `numtris` includes those triangles,
so the importer deterministically retains their explicit indices and exposes
per-mesh and aggregate retained-degenerate counts instead of silently dropping
or rejecting them. The evidence count covers a repeated source index or an
exact zero cross-product; an arbitrarily small nonzero triangle is retained but
not mislabeled through a tolerance guess. Output byte/count limits still
include this geometry.

Studio textures use positive bounded dimensions, `width * height` indexed
bytes, then exactly 768 RGB palette bytes. There is no palette-count field and
no mip generation, gamma conversion, premultiplication, or GPU upload. RGBA is
palette RGB with alpha 255, except the evidenced `STUDIO_NF_MASKED` rule gives
index 255 alpha 0 while preserving its RGB. Other source flags are retained as
metadata only. The ordered `numskinref * numskinfamilies` signed-16 table is
preserved; the importer does not select family zero.

Textures and skins may live in the main source or in the exact derived
`<stem>T.mdl` source. Animation data for sequence group zero lives in the main
source; groups 1 through 15 use exact derived `IDSQ` companions. See
[Studio dependencies](GOLDSRC_STUDIO_DEPENDENCIES.md) and
[Studio animation](GOLDSRC_STUDIO_ANIMATION.md).

The default configuration is also the project-supported hard ceiling for every
allocation-bearing field:

| Limit | Supported maximum |
| --- | ---: |
| main source bytes | 16 MiB |
| each companion source bytes | 16 MiB |
| total source bundle bytes | 32 MiB |
| bones / controllers | 128 / 8 |
| hitboxes / attachments | 512 / 512 |
| sequences / sequence groups | 2,048 / 16 |
| events / pivots | 1,024 / 256 |
| aggregate animation blends / bone tracks | 2,048 / 262,144 |
| animation runs / quantized value bytes | 1,048,576 / 16 MiB |
| nonzero animation stream starts | derived `min(6 * tracks, runs)` (1,048,576 default) |
| bodyparts / models per bodypart / total submodels | 32 / 32 / 1,024 |
| meshes | 256 |
| vertices / normals per source submodel | 2,048 / 2,048 |
| triangles per submodel / total triangles | 20,000 / 262,144 |
| triangle commands | 1,048,576 |
| output vertices / indices | 1,048,576 / 3,145,728 |
| textures / skin references / skin families | 100 / 100 / 256 |
| texture dimension / total RGBA bytes | 4,096 / 64 MiB |
| fixed-string retained bytes | 64 |

Caller-provided lower ceilings are supported, but a value above this table is
an invalid configuration. Exact-hard and hard-plus-one tests cover the
configuration boundary; source counts and output totals are checked again
before allocation and publication.

M4.5.3 converts this immutable source data into renderer-neutral Studio render
assets without changing the importer. Runtime body and skin selections are
validated exactly, geometry retains raw texture S/T and bone indices, and
unsupported chrome/additive/alpha material profiles remain typed. See
[Studio pose evaluation](STUDIO_POSE_EVALUATION.md) and
[OpenGL Studio rendering](OPENGL_STUDIO_RENDERER.md).
