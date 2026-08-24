# GoldSrc indexed miptex profile

M4.2 implements one clean-room miptex parser and indexed-pixel decoder shared
by embedded BSP records and WAD3 lumps. The library consumes a caller-owned,
bounded byte span and produces neutral owning `WorldTextureAsset` data. It has
no filesystem, local-resource, network, SDL, OpenGL, renderer, or GPU
dependency.

The pinned public Valve tools are format evidence only. Project code reads
every integer explicitly as little-endian bytes and does not include, cast to,
copy, or use `sizeof()` on a Valve wire structure.

## Exact record grammar

The miptex header is exactly 40 bytes:

| Offset | Field | Encoding |
| ---: | --- | --- |
| 0 | name | bounded 16-byte field |
| 16 | width | unsigned 32-bit little-endian |
| 20 | height | unsigned 32-bit little-endian |
| 24 | mip level 0 offset | unsigned 32-bit little-endian |
| 28 | mip level 1 offset | unsigned 32-bit little-endian |
| 32 | mip level 2 offset | unsigned 32-bit little-endian |
| 36 | mip level 3 offset | unsigned 32-bit little-endian |

The name ends at its first NUL. A full 16-byte non-NUL name is supported. The
retained name must be non-empty printable ASCII; source spelling is preserved
and a separate locale-independent ASCII-uppercase key is produced for lookup.
Name characters are texture metadata, never a native path.

Width and height must be positive multiples of 16. They need not be powers of
two. The four exact source mip dimensions are `width >> level` and
`height >> level` for levels zero through three. Each indexed level therefore
contains exactly `mip_width * mip_height` bytes.

The four offsets must be either all zero or all nonzero:

- all zero means a BSP external-reference header and is accepted only with the
  `bsp_embedded` source profile;
- mixed zero and nonzero offsets are malformed;
- a WAD3 miptex record must contain all four indexed levels.

For a pixel-backed record, every level begins at or after byte 40, fits wholly
inside its physical record, and appears in increasing, non-overlapping order.
Gaps are permitted but receive no meaning. Immediately after mip level 3 is a
little-endian 16-bit palette count, which must be exactly 256, followed by
exactly 768 RGB bytes. After the palette, only zero alignment fill is accepted,
with a configurable maximum no greater than three bytes. Nonzero or longer
unexplained suffix data is rejected.

For an all-zero BSP external reference, the 40-byte header may likewise have
only the bounded zero alignment suffix. No palette or indexed pixels are
invented for that record, and it cannot enter RGBA conversion.

## RGBA conversion

Each source index selects one three-byte RGB palette entry. The output keeps
the four source mip levels and stores four bytes per texel in `R, G, B, A`
order. Conversion does not apply gamma correction, premultiplication,
resampling, cropping, axis flips, or color-key RGB replacement.

The supported alpha rule is deterministic:

- a texture whose source name begins with `{` has
  `masked_index_255`; palette index 255 receives alpha zero and its palette RGB
  bytes are preserved;
- every other texel, including index 255 in a non-masked texture, receives
  alpha 255.

`WorldTextureAsset` records whether the source was `embedded_bsp` or
`external_wad3`, the alpha mode, all four owning RGBA8 mip buffers, and optional
BSP texture/archive provenance ordinals. No palette, indexed source byte span,
WAD record, or native path survives in the neutral asset.

## Incremental and transactional behavior

Parsing is allocation-bounded and publishes either complete parsed metadata or
a typed error. Production conversion uses
`GoldSrcIndexedTextureDecodeOperation`: the caller retains the source span and
each `update(maximum_rgba_bytes)` converts at most
`floor(maximum_rgba_bytes / 4)` texels. A budget below four bytes fails rather
than creating a zero-progress loop. The completed owning texture is visible
only after all four mip levels convert; a failure publishes no partial
texture. A synchronous convenience decoder exists for bounded tests and
offline tools and uses the same parser and operation.

Default limits are a 4,096-texel maximum dimension, 16,777,216 level-zero
texels, 64 MiB decoded RGBA per texture, and at most three zero suffix bytes.
The supported hard ceilings are 16,384 texels per dimension, 268,435,456
level-zero texels, and 256 MiB decoded RGBA. The world-texture composition uses
the stricter defaults and additionally caps aggregate RGBA ownership.

Errors contain a typed code, bounded record-relative byte offset, optional mip
level, and bounded context. Context never contains texture names, source bytes,
palette values, indexed pixels, RGBA values, WAD basenames, compiler paths, or
native paths.

Automated tests use original synthetic bytes only. Coverage includes every
header truncation, full 16-byte names, invalid dimensions and limits, external
and mixed offsets, mip range overlap/out-of-bounds, palette count/range,
alignment fill, four-level RGBA output, masked alpha, update budgets,
provenance, and source-lifetime requirements. No Half-Life texture or WAD is
committed.

## M4.2 boundary

The shared decoder does not implement WAD directory parsing, local WAD
resolution, texture animation families, water warp, sky rendering, lightmaps,
decals, shaders, renderer materials, OpenGL upload, or other GPU work. See
[GoldSrc WAD3](GOLDSRC_WAD3.md) for the archive profile and
[world texture resolution](WORLD_TEXTURE_RESOLUTION.md) for composition.
