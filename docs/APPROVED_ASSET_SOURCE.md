# Approved asset sources

M3.2.3 introduces the only asset-content opening path used by the GoldSrc
precache continuation. A manifest virtual name is metadata, not a filesystem
capability. The opening chain is therefore fixed:

```text
PrecacheManifestEntry
    -> LocalResourceLocator
    -> LocalResourceEnvironment::reopen_verified()
    -> LocalReadOnlyFile
    -> AssetSource
    -> ApprovedAssetSource
```

The production continuation first opens one resource on demand: the exact
model entry selected as the world by `WorldResourceSelection`. It does not scan
or preload the remaining manifest or maintain a source cache. M4.1 passes the
resulting approved owning bytes directly to the production GoldSrc BSP v30
importer; the importer never reopens the virtual name or asks for a native
path. M4.2 retains those same bytes through texture-source extraction and
embedded decoding, so the BSP is still opened only once.

The M4.2 dependent WAD chain reuses the same verified local source opener but
does not pretend a compiler reference was part of the manifest:

```text
inert worldspawn value -> safe WAD basename
    -> LocalResourceEnvironment resolver
    -> exact-root LocalResourceLocator
    -> LocalAssetSourceOpener
    -> owning LocalAssetSource -> WAD3 parser
```

Only the safe basename becomes a virtual name. The resolved handle is closed
after identity capture and the locator is verified again while reading the WAD.

## Security boundary

The plan builder accepts an exact entry owned by the supplied manifest and
retains a private copy of that entry as the opening capability. The opener
accepts that plan directly; it has no second entry parameter that could swap in
metadata from another manifest. Only a `ready_local_file` capability with a
locator and a supported dispatch role can proceed. `reopen_verified()` uses
the locator's exact root; it never searches a fallback root. Before allocating
the byte vector, the opener revalidates root ID, virtual-resource ID, stable
identity, and exact size. It then reads sequentially from that same retained
handle, performs an explicit one-byte EOF probe, obtains a final same-handle
metadata snapshot, and compares identity, size, last-write time, and change
time. Importers cannot run until all validation succeeds.

The approved virtual name is copied into `AssetSource` once as probe metadata.
It is never passed back to the filesystem. Native paths, root paths, native
handles, identity fields, source bytes, digests, and signatures are absent from
events and logs.

## Bounded operation

Source opening is move-only, incremental, and driven by `update(now)`. It uses
no worker thread, sleep, or busy loop. Each update performs at most the
configured number of bounded reads, including the final EOF probe. The project
defaults are a 16 MiB source limit, 64 KiB read chunks, one chunk per update,
and one simultaneously open source. Hard limits are 64 MiB, 1 MiB per chunk,
and one open source for this stage. Zero limits and unchecked integer
conversions are rejected.

The M4.1 production `asset-dispatch`/`world-geometry` composition explicitly
raises only its resolver and source-open byte limit to the BSP parser's 32 MiB
default and uses 1,024 stage-event slots, which covers 512 default-size progress
chunks plus lifecycle events. M4.2's `world-textures` route uses that same BSP
source profile and separately permits one declared WAD source of at most
64 MiB, still opened incrementally with one active source. Earlier
response/manifest stop points retain the generic 16 MiB profile.

The operation publishes the owning source atomically only after validation.
Cancellation, timeout, missing/replaced locators, size drift, short reads,
unexpected growth, metadata changes, allocation failure, and `AssetSource`
creation failure discard partial bytes and close the handle. No importer sees a
partial source.

## Scope

M4.1 adds BSP v30 parsing only after this boundary has published a fully
validated owning source. M4.2 adds a separate, explicit dependent-texture route
after successful CPU geometry. Compiler-recorded WAD prefixes are reduced by
the inert parser to safe basenames only. Each required basename is resolved in
the retained game-before-`valve` environment and opened through a newly bound,
exact-root verified locator; no compiler or native path crosses into the
texture API. Simply missing archives may produce a typed incomplete texture
set. Unsafe resolution, verified-open failure, malformed WAD3, or malformed
miptex fails transactionally.

The earlier `asset-dispatch` and `world-geometry` routes still do not follow
dependent names or open WADs. M4.2 does not add MDL, SPR, or WAV parsing;
download/cache behavior; background prefetch; renderer/GPU integration; or an
`AssetManager` path-based bypass. A same-identity, same-size content rewrite
completed before a verified reopen remains outside the locator's
identity/size evidence; the source boundary does not invent hashing as a trust
mechanism. See [world texture resolution](WORLD_TEXTURE_RESOLUTION.md).
