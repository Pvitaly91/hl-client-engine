# World texture resolution

M4.2 continues the approved M4.1 BSP import and resolves only the texture
references used by model 0 world materials. It publishes an immutable, owning
CPU `WorldTextureSet`; it does not change geometry, apply entity state, build
lightmaps, create renderer materials, or upload GPU resources.

```text
retained approved BSP bytes + owning WorldAsset
    -> used BSP texture-source ranges
    -> embedded miptex decode
    -> inert first-entity worldspawn metadata
    -> safe WAD basenames in declaration order
    -> sandboxed local resolution and verified WAD source opening
    -> WAD3 catalog + shared miptex decode
    -> one binding per WorldAsset material
    -> immutable WorldTextureSet
```

## Retained BSP source and physical texture records

The M4.1 `WorldMaterialReference` now retains the exact optional BSP texture
directory ordinal. M4.2 cross-checks that ordinal, name, dimensions, and
missing/external/embedded storage classification against the same approved BSP
bytes. The approved dispatch state keeps those bytes alive; the BSP is never
reopened and `AssetSource::virtual_path()` is never used as a filesystem path.

The texture-source parser revalidates the BSP v30 header plus entity and texture
lump ranges. A non-missing directory offset must begin after the complete
directory table and leave room for a 40-byte miptex header. Its physical record
ends at the next greater distinct directory offset, regardless of directory
order, or at the texture-lump end for the last physical record. Duplicate
offsets are aliases: they share one parsed/decoded source, retain every source
ordinal, and use the lowest ordinal as canonical provenance. A missing `-1`
entry remains an independent missing source.

Only physical records referenced by emitted world materials enter the shared
miptex parser. Unused payload bytes are not decoded, but the directory and all
its offsets are still range-validated. An embedded record is decoded exactly
once and may satisfy several material bindings. An external-reference record
must be resolved by name and exact BSP dimensions. A missing BSP directory
entry never causes a WAD name guess.

## Inert worldspawn and basename policy

The entity-lump parser recognizes only the first brace-delimited entity and
only quoted key/value pairs. Backslash is an ordinary byte; there is no escape
language, command execution, entity instantiation, variable expansion, or
interpretation of later entities. NUL, malformed quoting/braces, excess limits,
and duplicate ASCII-case-insensitive keys fail. The first entity must contain
the exact value `worldspawn` for `classname`.

For WAD declarations, a non-empty `_wad` value takes precedence over `wad`;
otherwise a non-empty `wad` value is used. If neither is present, the approved
list is empty. The chosen value is split on semicolons. Outer ASCII whitespace
is removed, internal empty entries are rejected, and one trailing empty segment
is accepted for compiler output compatibility.

Each segment treats both `/` and `\` only as source separators and discards
everything except the final basename. The compiler-recorded prefix is never
retained, logged, resolved, or exposed by public state. A basename must be
printable ASCII, end in `.wad` under ASCII-insensitive comparison, fit the
configured bound, and contain no slash, backslash, colon, control/DEL byte,
non-ASCII byte, trailing dot/space, `.`/`..`, or reserved Windows device name.
ASCII-case-insensitive duplicates collapse to the first declaration while its
original declaration ordinal and spelling are preserved.

The default bounds are a 128 KiB entity lump/value, 256 first-entity pairs,
128 WAD declarations, and a 128-byte basename. The supported basename hard
ceiling is 255 bytes; the composition retains the stricter default.

## Approved local WAD lookup

A sanitized basename is converted to a `LocalVirtualResourceName` and resolved
only through the retained `LocalResourceEnvironment`. For a non-`valve` game,
the existing ordered roots search the selected game before the `valve`
fallback; `valve` uses one deduplicated root. There is no CWD, registry,
environment-variable, Steam-library, repository, build-directory, compiler
path, or recursive-directory search.

Resolution first obtains a path-safe read-only file identity, closes that
temporary handle, creates a locator bound to its exact root/name/identity/size,
and then uses the approved source opener. The opener performs the same
read-only, reparse-free, local-fixed-disk, final-handle containment, exact EOF,
and final metadata checks used by the BSP source boundary. At most one WAD
source is open at a time; completed sources and catalogs are released before
the next declaration. No WAD is opened when every used material is embedded or
missing, and later declarations stay `not_required` once all external bindings
are resolved.

Published archive metadata uses only declaration ordinal, safe-basename byte
count, optional source-root ordinal, status, bounded catalog/supplied counts,
and source byte count. It does not retain even the safe basename, a locator,
identity, handle, source bytes, or compiler/native path. Final archive statuses
are `not_required`, `resolved`, or `missing`; `malformed` and
`unsupported_profile` remain neutral taxonomy for failed work, but production
fatal paths publish no texture set containing them.

A declaration that is simply not found is recorded as `missing` and lookup
continues. Unsafe/ambiguous resolution, verified-open failure, malformed WAD3,
unsupported compression/profile, or malformed referenced miptex is a fatal
transactional error. The resolver never falls back from such malformed data to
a later archive.

## WAD3 selection and material bindings

Declared archive order is authoritative. Within an archive, type `0x43`
uncompressed miptex entries are found by locale-independent ASCII-insensitive
name; duplicate normalized miptex names make that archive malformed rather
than letting directory order decide. The directory name, miptex record name,
BSP expected name, width, and height are cross-checked. Textures are never
rescaled or cropped.

The first declared archive containing the requested normalized name determines
that binding. A valid exact-dimension record resolves it and is decoded once;
materials referencing the same WAD entry reuse the owning texture asset. A
dimension mismatch is retained as a typed unresolved binding and does not
search later archives. If no resolved archive contains the name, the result is
`external_texture_not_found`; if no declared archive could be resolved, it is
`external_wad_archive_missing`. An external reference with no WAD list is
`external_wad_list_missing`.

Every world material receives exactly one ordered
`WorldMaterialTextureBinding`. Successful statuses are `resolved_embedded` and
`resolved_wad3`. Expected-absence statuses are
`missing_bsp_texture_reference`, `external_wad_list_missing`,
`external_wad_archive_missing`, `external_texture_not_found`, and
`external_texture_dimension_mismatch`. The neutral vocabulary also reserves
`malformed_embedded_texture`, `malformed_wad_texture`, and
`unsupported_texture_profile`; production malformed/unsupported paths are
fatal and therefore do not publish those provisional statuses in a partial
texture set.

`WorldTextureSet` transactionally validates all four RGBA8 mip buffers,
the exact material binding count/order, resolved-status/texture-index/source
agreement, archive count, and aggregate ownership. It also computes bounded
statistics. The set is complete only when every material binding is resolved.
Expected absence can therefore publish an owning `textures_incomplete` set for
diagnosis, while malformed bytes, invalid policy, cancellation, timeout, or
retention failure publish no set.

## Bounded operation and runtime stop

`WorldTextureImportOperation` is caller-driven and move-only. BSP
texture-source, worldspawn, and WAD catalog parsing are synchronous but bounded
phases. The material-oriented binding and external-lookup passes consider at
most one material per update by default; an RGBA conversion update processes at
most 64 KiB by default, and a verified-source update reads at most one bounded
chunk by default. The operation supports cancellation, an optional caller
deadline, monotonic-time checks, and typed progress. Production stage
composition retains
the same socket, driver, endpoint, authentication lifetime, manifest, world,
approved BSP source, and local environment; it emits no packet after manifest
publication.

The operation lives in the CPU-only
`hlclient_goldsrc_world_texture_import` target. Offline tools link that target
directly and therefore do not inherit protocol transport or same-session stage
libraries. `hlclient_goldsrc_world_textures` contains only the production
same-session wrapper and depends on both the CPU operation and asset-dispatch
stage.

The composition defaults and accepted ceilings are:

| Resource | Default | Supported ceiling |
| --- | ---: | ---: |
| retained BSP source | 32 MiB | 32 MiB in this stage |
| world materials | 8,192 | 8,192 |
| decoded texture assets / BSP texture directory | 512 | 512 |
| WAD declarations | 128 | 128 |
| one WAD source | 64 MiB | 64 MiB |
| one WAD catalog | 4,096 entries | 65,536 entries |
| texture dimension | 4,096 | 16,384 |
| level-zero texels | 16,777,216 | 268,435,456 |
| decoded RGBA per texture | 64 MiB | 64 MiB |
| aggregate decoded RGBA | 256 MiB | 256 MiB |
| first-entity pairs | 256 | 256 |
| entity lump / worldspawn value | 128 KiB | 128 KiB |
| WAD read chunk | 64 KiB | 1 MiB |
| WAD read chunks per update | 1 | 64 |
| simultaneously open WAD sources | 1 | 1 |
| materials per binding/lookup update | 1 | 8,192 |
| RGBA conversion per update | 64 KiB | 64 MiB |
| texture-stage event slots | 2,048 | 8,192 |

An optional operation or source-open timeout may not exceed 60 seconds. Limit
arithmetic and conversions are checked before allocation or subspans. The
shared miptex parser exposes broader library-level hard limits where documented,
but this production composition deliberately retains the stricter decoded and
aggregate memory ceilings above.

The explicit runtime boundary is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after world-textures `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve --net-trace
```

The CLI prints bounded counts and completeness only. It returns zero only for a
texture set complete for all world materials; a typed incomplete set returns
nonzero. Earlier `asset-dispatch` and `world-geometry` stops keep their M4.1
behavior and never invoke worldspawn parsing, WAD resolution, or texture
decoding. None of these stops initializes a renderer or creates GPU resources.
Stage events and traces likewise expose counts, typed classifications, and
bounded progress only; operation errors may additionally identify material,
BSP-texture, or archive ordinals. They never expose texture/archive names,
paths, source bytes, palettes, indexed pixels, RGBA bytes, native handles, or
network payloads.

For an optional network-free check of a user-owned map and its declared WADs,
build `hlclient_world_texture_check.exe` and run:

```powershell
.\scripts\verify_local_world_textures.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_world_texture_check.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" `
  -Game valve -Map maps/<name>.bsp
```

The wrapper accepts only a safe virtual map name, snapshots the selected map,
root-level WAD files, and game/`valve` inventories, runs the network-free
read-only checker twice, requires identical summaries, and fails on file or
metadata drift. It prints only a summary digest and bounded counts. No
user-owned local verification run is claimed by this repository change.

## M4.2 boundary

M4.2 stops at owning CPU RGBA textures and typed material bindings. It does not
implement downloads or cache writes, compressed WAD decoding, animated texture
selection, water/sky/decal effects, lightmap decode or composition, BSP PVS or
collision runtime, brush-submodel instances, renderer-neutral draw materials,
OpenGL upload, renderer integration, or other GPU work. See
[indexed miptex](GOLDSRC_INDEXED_TEXTURE.md),
[GoldSrc WAD3](GOLDSRC_WAD3.md), and
[CPU world geometry](CPU_WORLD_GEOMETRY.md).

M4.3 is a separate continuation and does not weaken this historical stop. It
requires a complete `WorldTextureSet`, transfers the owning textured world
once, imports RGB lightmaps from the already retained approved BSP bytes, and
builds the immutable package described in
[GoldSrc world lightmaps](GOLDSRC_LIGHTMAPS.md) and
[world render package](WORLD_RENDER_PACKAGE.md). The earlier
`--stop-after world-textures` path still performs zero lightmap decode and zero
renderer upload.
