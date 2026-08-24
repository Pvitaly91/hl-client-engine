# GoldSrc WAD3 catalog and texture profile

M4.2 implements a clean-room, read-only parser for the subset of WAD3 needed
to resolve GoldSrc world textures. The parser consumes an already approved,
bounded byte span. It has no filesystem, local-resource, network, renderer, or
GPU dependency and never interprets a compiler-recorded WAD path.

The pinned public Valve tools are format evidence only. `wadlib.h` defines
`TYP_LUMPY` as 64, `qlumpy.c` places `miptex` at command index 3, and the same
tool writes `TYP_LUMPY + command_index`; therefore the supported WAD3 miptex
directory type is exactly `67` (`0x43`). `wadlib.h` also identifies compression
value zero as `CMP_NONE`. Project code does not include, cast to, copy, or use
`sizeof()` on the Valve wire structures.

## Header and directory wire grammar

The WAD3 header is exactly 12 bytes:

| Offset | Field | Encoding |
| ---: | --- | --- |
| 0 | identification | four bytes, exactly `WAD3` |
| 4 | lump count | signed 32-bit little-endian |
| 8 | directory offset | signed 32-bit little-endian |

The world-texture profile requires a positive lump count. The default catalog
limit is 4,096 entries and the hard supported ceiling is 65,536. The directory
offset must be non-negative, begin at or after byte 12, and identify a complete
`lump_count * 32` byte table within the retained source. Multiplication and end
offsets are checked before any subspan is formed.

Every directory entry is exactly 32 bytes:

| Offset | Field | Encoding |
| ---: | --- | --- |
| 0 | file position | signed 32-bit little-endian |
| 4 | disk size | signed 32-bit little-endian |
| 8 | uncompressed size | signed 32-bit little-endian |
| 12 | type | unsigned byte |
| 13 | compression | unsigned byte |
| 14 | padding 0 | unsigned byte, exactly zero |
| 15 | padding 1 | unsigned byte, exactly zero |
| 16 | name | bounded 16-byte field |

File position, disk size, and uncompressed size must be non-negative. Every
non-empty payload range must fit the source, remain disjoint from the 12-byte
header and directory table, and not overlap another non-empty payload range.
Physical payload order need not match directory order; gaps are permitted.
Zero-sized metadata entries have an empty range, but a zero-sized `0x43`
entry cannot decode as miptex and therefore fails at the texture boundary.
Unreferenced archive gaps or suffix bytes receive no semantics and are never
exposed by the catalog. Inside a referenced miptex record, the shared decoder
permits only its evidence-backed maximum of three zero alignment bytes after
the 256-color palette and rejects nonzero or larger unexplained trailing data.

## Compression and entry types

Only `compression == 0` is supported, for every catalog entry. Disk size must
equal uncompressed size exactly. LZSS and unknown compression values fail with
a typed `unsupported_compression` catalog error; M4.2 contains no decompressor
and never attempts a compressed fallback.

Type `0x43` is the only miptex type eligible for world-texture lookup.
Uncompressed entries with another type remain bounded, owning catalog metadata
but are classified as non-miptex and are never passed to the shared texture
decoder. Calling the WAD texture adapter with such an entry fails with
`invalid_entry_type`.

## Name and lookup policy

An entry name ends at its first NUL. A full 16-byte non-NUL name is supported.
The retained bytes must form a non-empty printable ASCII name; control bytes,
DEL, and non-ASCII bytes are rejected. Source spelling is preserved for output,
and a separate locale-independent ASCII-uppercase key is retained for lookup.
Characters such as `/`, `\`, and `:` are texture-name bytes here, never path
separators.

The catalog preserves exact directory order. It provides exact-spelling and
ASCII-case-insensitive miptex lookup. Two `0x43` entries in one archive with
the same normalized name make the catalog ambiguous and fail transactionally;
directory order is never used as a silent tie-breaker. Non-miptex entries do
not participate in this duplicate rule. Choosing between separate WAD files is
the world-texture resolver's declared-archive-order policy, not catalog state.

## Catalog ownership and source lifetime

`GoldSrcWad3Catalog` is immutable after construction and owns only bounded
metadata: directory ordinal, source and normalized texture names, payload
offset and sizes, type, compression, profiles, and total source byte count. It
does not retain a byte span, source bytes, palette, pixel data, file handle,
archive basename, compiler path, native path, file identity, or resource
locator. Catalog lookup remains valid after the source buffer is destroyed.

The caller retains the approved WAD source while decoding. A catalog entry is
paired with that original span through `GoldSrcWad3TextureParser::parse`. The
adapter revalidates type, compression, equal sizes, and checked source range,
then invokes the one shared `GoldSrcMiptexParser` with the `wad3_lump` profile.
It additionally requires the miptex record name to match the directory name
under ASCII normalization. When BSP expectations are supplied, texture name,
width, and height must match exactly; dimensions are never rescaled, cropped,
or taken from only one source.

Successful preparation owns parsed miptex metadata and record offsets but no
source span. This lets the caller drive the shared incremental indexed-texture
operation while keeping the approved source alive. The synchronous test/tool
adapter uses the same shared decoder and produces an owning
`WorldTextureAsset` with source kind `external_wad3`, four RGBA8 mip levels,
optional BSP texture ordinal, and optional archive declaration ordinal. No WAD
source bytes or palette survive RGBA conversion.

## Limits, failures, and diagnostics

The default and hard WAD source limit is 64 MiB. Catalog count, directory size,
payload ends, and all offset arithmetic are checked. Catalog construction is
transactional: an invalid header, range, compression profile, padding byte,
name, or duplicate publishes no partial catalog. Texture preparation and
decoding likewise publish no partial texture after malformed input.

Errors carry a typed code, bounded byte offset, optional directory ordinal,
and bounded context. A nested shared-miptex error is retained when applicable.
Contexts contain no texture name, WAD basename, compiler path, native path,
palette, indexed pixel, RGBA byte, or source byte dump.

Automated coverage uses original synthetic WAD3 and miptex bytes only. It
covers every header truncation, signed/count/range limits, header/directory and
payload overlap, compression and size policy, padding, full 16-byte names,
normalized ambiguity, case-insensitive lookup, source destruction, miptex
type, four RGBA levels, opaque and masked alpha, malformed palette/dimensions,
directory-record name mismatch, and BSP dimension mismatch. No Half-Life WAD,
BSP, game, capture, authentication, palette, indexed-pixel, or RGBA fixture is
committed.

## M4.2 boundary

This layer does not resolve compiler paths, scan directories, open files, or
choose game-versus-`valve` roots; those actions belong to the sandboxed
world-texture stage. It also does not implement downloads, cache writes, LZSS,
lightmaps, texture animation, water warp, sky rendering, decals, shaders,
OpenGL upload, renderer materials, or other GPU work.
