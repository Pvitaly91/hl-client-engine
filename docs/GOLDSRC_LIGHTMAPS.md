# GoldSrc world lightmaps

M4.3 decodes the BSP v30 lighting lump into an immutable, renderer-neutral
`WorldLightmapSet`. The importer receives the already approved BSP bytes and
the owning M4.1/M4.2 world data; it does not reopen a map, resolve a native
path, call `AssetManager`, or read a server resource name from the filesystem.
Earlier `asset-dispatch`, `world-geometry`, and `world-textures` stops still
decode no lightmaps.

## Source profile

The supported profile stores three unsigned bytes per sample in RGB order,
with no alpha and no compression. A face can identify at most four source
light-style slots. Style IDs are retained in source order and the first `255`
byte terminates the active list.

For an active style, the source layout is one complete face image followed by
the next style image:

```text
bytes_per_sample = 3
sample_count = sample_width * sample_height
bytes_per_style = sample_count * bytes_per_sample
style_0 RGB samples
style_1 RGB samples
style_2 RGB samples
style_3 RGB samples
```

The exact active-style count participates in checked range validation against
the lighting lump. A lightmapped surface must have an offset and at least one
active style. A surface without an offset is valid only as
`unlit_no_lightmap`; it never causes a lighting-lump read. Inconsistent
metadata and out-of-range samples are fatal, rather than silently becoming
white.

Every source RGB triplet becomes RGBA8 by copying R, G, and B and setting
alpha to 255. The CPU importer performs no gamma conversion, exposure,
premultiplication, or overbright scaling.

## Exact face extents

Extents are calculated from the exact contiguous face-local `WorldVertex`
range recorded by `WorldSurface::first_vertex` and `vertex_count`. For every
corner, the importer reads the raw texel-space S/T values retained by M4.1 and
requires them to be finite. With a GoldSrc lightmap block size of 16:

```text
minimum_block_s = floor(min_s / 16)
minimum_block_t = floor(min_t / 16)

maximum_block_s = ceil(max_s / 16)
maximum_block_t = ceil(max_t / 16)

texture_min_s = minimum_block_s * 16
texture_min_t = minimum_block_t * 16

extent_s = (maximum_block_s - minimum_block_s) * 16
extent_t = (maximum_block_t - minimum_block_t) * 16

sample_width  = extent_s / 16 + 1
sample_height = extent_t / 16 + 1
```

All arithmetic and conversions are checked. The importer does not round S/T
before `floor`/`ceil`, normalize by a base texture, clamp a bad coordinate, or
infer the range from triangle indices alone. Each face corner must map within
`[0, sample_width - 1] x [0, sample_height - 1]` under the documented small
floating-point tolerance.

The local coordinate for one face vertex is:

```text
local_s = (raw_s - texture_min_s) / 16
local_t = (raw_t - texture_min_t) / 16
```

## Style retention and baseline rendering

`WorldSurfaceLightStyles` preserves the active count, exact style IDs, and
source slot order. Each atlas page contains exactly four identically sized
RGBA8 style-slot images. The importer decodes every active source layer and
places all layers at the same rectangle. Pixels belonging to an unused style
slot or unused rectangle start at black RGB with alpha 255.
The immutable factory revalidates those values, exact opaque alpha, page
coverage, non-overlap, and duplicated borders before it can publish a set;
orphan atlas pages are rejected.

The first renderer deliberately samples source style slot 0 only. The other
decoded layers remain in the CPU atlas and GPU texture array for later dynamic
style work; M4.3 neither sums them nor invents animation values. This baseline
policy is not a claim of stock dynamic-light-style compatibility. Unlit faces
use a separate white fallback in the renderer, never zero-filled atlas data.

## Deterministic atlases and borders

Packing is deterministic, consumes surfaces in source order, never rotates a
rectangle, and supports multiple pages. The default packing width is 1024,
the default maximum dimension is 2048, the default page count is 16, and the
padding is exactly one texel. Supported hard ceilings are a 4096 dimension and
64 pages. A face rectangle that cannot fit produces a typed limit failure.

Every rectangle owns an inner region and a one-pixel padded region. On every
style-slot layer the importer:

- copies the source samples into the inner region;
- duplicates the first and last rows into the top and bottom padding;
- duplicates the first and last columns into the left and right padding;
- duplicates all four corner texels into the padded corners.

The duplicated border prevents linear filtering from bleeding between adjacent
face images. Valid samples are not surrounded by arbitrary black pixels, and
all four style layers use the same placement.

After packing, a vertex receives normalized atlas coordinates at texel centers:

```text
atlas_u = (inner_x + local_s + 0.5) / atlas_width
atlas_v = (inner_y + local_t + 0.5) / atlas_height
```

The CPU package does not vertically flip the source or atlas coordinates. The
`+0.5` offset is intentional: it addresses the center of a lightmap texel.
Asymmetric fixtures fix this orientation contract.

## Publication and limits

`WorldLightmapSet` owns its pages, one ordered
`WorldSurfaceLightmapBinding` per world surface, statistics, and named
compatibility/evidence profiles. A binding records its status, optional page,
inner and padded rectangles, local texture minima, sample dimensions, source
styles, and selected baseline slot. It contains no BSP bytes, native path,
file handle, SDL object, or OpenGL resource.

Renderable statuses are `resolved` and `unlit_no_lightmap`. The remaining
typed statuses identify invalid metadata, out-of-range source data, atlas
limits, or an unsupported style profile. Fatal input produces no final set.
Defaults bound 65,535 surfaces, 65,536 samples per surface, 16 atlas pages, a
2048 maximum atlas dimension, and 256 MiB of atlas RGBA data. The importer also
bounds aggregate source samples and performs checked arithmetic before every
allocation and byte-range operation.

The per-surface default is the bounded stock-compatibility profile for one
256 x 256 sample rectangle. Read-only evidence included a 177 x 204 face,
which requires 36,108 source samples and therefore exceeds the historical
16,384-sample default. The 65,536 limit admits that independently reproduced
structural case without changing the separate 1,048,576 hard ceiling, atlas
dimension/page limits, aggregate-source-sample bound, or exact checked range
validation.

M4.3 does not implement gamma/overbright correction, dynamic light styles,
dynamic lights, PVS, brush entities, animated textures, water, sky, fog, or
gameplay lighting.
