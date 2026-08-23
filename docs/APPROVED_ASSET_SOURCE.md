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

The production continuation opens one resource on demand: the exact model
entry selected as the world by `WorldResourceSelection`. It does not scan or
preload the remaining manifest, maintain a source cache, or follow dependent
asset names.

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

The operation publishes the owning source atomically only after validation.
Cancellation, timeout, missing/replaced locators, size drift, short reads,
unexpected growth, metadata changes, allocation failure, and `AssetSource`
creation failure discard partial bytes and close the handle. No importer sees a
partial source.

## Scope

This milestone does not add a BSP, MDL, SPR, WAV, or WAD parser; download or
cache behavior; background prefetch; renderer/GPU integration; or an
`AssetManager` path-based bypass. A same-identity, same-size content rewrite
completed before the verified reopen is outside the locator's identity/size
evidence; M3.2.3 does not invent hashing as a trust mechanism.
