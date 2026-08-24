# GoldSrc BSP v30 import profile

M4.1 implements a clean-room, read-only importer for Valve GoldSrc BSP version
30. The implementation uses the pinned public Half-Life SDK only to
cross-check published constants and field meanings. Project code decodes every
field from bounded bytes; it does not include, cast to, or copy SDK wire
structures.

## Wire grammar

The file begins with one little-endian signed version (`30`) followed by 15
little-endian `(offset, length)` lump descriptors. Both descriptor fields are
signed 32-bit values. The header is therefore exactly 124 bytes.

| ID | Lump | Record size |
| ---: | --- | ---: |
| 0 | entities | byte stream |
| 1 | planes | 20 |
| 2 | textures | variable directory |
| 3 | vertices | 12 |
| 4 | visibility | byte stream |
| 5 | nodes | 24 |
| 6 | texinfo | 40 |
| 7 | faces | 20 |
| 8 | lighting | byte stream |
| 9 | clipnodes | 8 |
| 10 | leaves | 28 |
| 11 | marksurfaces | 2 |
| 12 | edges | 4 |
| 13 | surfedges | 4 |
| 14 | models | 64 |

The variable texture lump begins with a signed texture count and that many
signed offsets. `-1` denotes missing metadata. Every other offset must contain
a complete 40-byte miptex header: a bounded 16-byte name, width, height, and
four mip offsets. A name ends at its first NUL; all 16 non-NUL bytes are valid
metadata and are never interpreted as a path. Non-missing width and height are
positive, bounded multiples of the stock 16-texel miptex granularity; they need
not be powers of two, and their area is checked without allocating pixels.

Two directory entries may contain the same valid miptex offset. They remain
distinct texture ordinals with independently owned metadata; the parser does
not deduplicate, merge, or reinterpret the directory.

Texture storage is classified without reading pixels:

- `missing`: directory offset `-1`;
- `external_reference`: a valid header with all four mip offsets zero;
- `embedded`: a valid header with four increasing, in-lump non-zero offsets;
- mixed zero/non-zero offsets: malformed input.

Embedded pixels, palettes, animated texture families, WAD lookup, and
dependent-resource loading are deliberately deferred to M4.2.

## Validation and limits

All input is untrusted. The parser validates signed ranges, checked
`offset + length`, source containment, header exclusion for non-empty lumps,
and pairwise non-overlap before taking a subspan. Empty lumps may use offset
zero, and valid lumps need not appear in physical ID order. Fixed lumps must
be exact record-size multiples and counts are checked before allocation.

Default limits include a 32 MiB source, 4,096 corners per face, 512,000 output
vertices, 1,536,000 output indices, and 16,777,216 aggregate non-adjacent edge
pair tests. Hard ceilings are 64 MiB, 32,767 face corners, 1,048,576 output
vertices, 3,145,728 output indices, and 536,788,994 edge-pair tests. The supported
record ceilings are 400 models, 32,767 planes/nodes/clipnodes, 8,192 leaves and
texinfo records, 65,535 vertices/faces/marksurfaces, 256,000 edges, 512,000
surfedges, and 512 texture-directory entries. All additions and
multiplications used for ranges or output sizing are checked.

Integers are decoded explicitly as little-endian fixed-width values. Floats
are produced with `std::bit_cast<float>` from decoded `uint32_t` values. NaN
and infinity are rejected for vertices, planes, texinfo vectors, and model
bounds/origins. Plane normals must be non-zero and within `0.01` of unit
length; plane type is restricted to 0 through 5.

Every edge vertex, surfedge edge, face plane/texinfo/range, node child/face,
leaf marksurface/visibility, marksurface face, clipnode child/plane, model
headnode, and model face range is cross-validated. Lighting and visibility
offsets may be `-1` or a valid offset in the corresponding retained lump.
Entity text is not parsed or exposed, PVS is not decompressed, lighting samples
are not decoded, and collision hull runtime state is not created.

Failures are transactional and typed with an error code, optional lump ID,
bounded byte offset, optional element index, and bounded context. Errors never
contain source bytes, entity text, texture names, or native paths. No partial
`WorldAsset` is published.

## Importer and composition

`GoldSrcBspWorldImporter` implements the neutral `IWorldImporter` contract. Its
stable ID is `goldsrc-bsp-v30`; the named production priority is `300`. Probe is
bounded and side-effect-free: version 30 is required, structural header and
lump evidence raise confidence, and `.bsp` is only a hint. A `.bsp` extension
alone never matches. A malformed version-30 candidate is selected so import can
return `MalformedData`; other versions do not match.

The `hlclient_goldsrc_bsp` / `hlclient::goldsrc_bsp` target lives under
`Engine/Assets/Formats/GoldSrc` and depends only on `hlclient_asset_api` (and
therefore core). It has no filesystem, local-resource, network, sign-on, SDL,
OpenGL, renderer, or asset-manager dependency. Production composition calls
`register_builtin_asset_importers` once on a caller-owned registry; there is no
global registry or cache.

Automated coverage uses only original synthetic bytes, including an independent
literal 482-byte BSP v30 quad and a separate mutation builder. No Valve or game
BSP data is committed.
