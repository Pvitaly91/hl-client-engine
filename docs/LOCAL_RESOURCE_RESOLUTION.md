# Sandboxed local resource resolution

M3.2.1 adds a read-only Windows local-resource foundation. It turns an
explicit user-owned Half-Life root and an already classified virtual resource
name into bounded metadata for one local regular-file candidate. It does not
turn server bytes into native paths, establish resource readiness, parse an
asset, download a missing file, or create a precache handle.

## Explicit roots and search order

`LocalResourceSearchRoots` is created only from explicit runtime inputs:

```text
--basedir X --game valve  -> X/valve
--basedir X --game mymod  -> X/mymod, then X/valve
```

Every configured root must already exist and be a local disk directory. The
root is opened read-only, its final path and volume/file identity are obtained
from the handle, and a final reparse point is rejected. Canonically duplicate
roots collapse without changing the game-before-valve priority. Public state
contains only a bounded root ID, root kind, and equality-only identity token;
absolute and final native paths remain private.

There is no current-working-directory, environment, registry, Steam-library,
repository, or build-directory fallback. A normal runtime may read an explicit
primary Steam installation because no stock process is launched and no write
is requested. Active `hl.exe`/HLDS protocol research remains subject to the
separate isolated-copy and restoration-attestation policy.

## Byte classifier and type mapping

`GoldSrcResourceNameClassifier` treats every `ResourceName` as untrusted bytes.
The supported profile accepts printable ASCII and forward slashes only. It
rejects an empty name, embedded NUL, rooted/absolute/drive-relative/UNC/device
syntax, backslashes, ADS colons, control bytes, DEL, empty or repeated
components, `.` and `..`, trailing dot/space, Windows device components,
oversized components, and an oversized path before any file-open operation.
Non-ASCII bytes return `unsupported_name_encoding`; they are not converted
through the active Windows code page or guessed to be UTF-8. Percent and URL
sequences remain literal bytes. Nothing is decoded, normalized, expanded,
replaced, or silently repaired.

The evidence-gated mapping is type-aware:

| Resource type | Local virtual-name policy |
| --- | --- |
| sound | prefix the wire name with `sound/` |
| model | preserve the approved virtual name |
| generic | preserve the approved virtual name |
| event script | preserve the approved virtual name |
| decal | metadata-only; no general file mapping |

The type is never inferred from the extension. These mappings are supported by
repeated stock-style names and user-owned installation layout; pinned public
Valve headers are a secondary category cross-check, not an implementation or
native-path contract.

## Win32 handle boundary

`LocalResourceResolver` performs a bounded component lookup within each
validated root. Exact ASCII spelling wins. If it is absent, controlled
ASCII-only case-insensitive enumeration may select one entry; multiple matches
return `ambiguous_case`. Locale-dependent case conversion is never used.

The final candidate is opened with `CreateFileW`, `GENERIC_READ`, and
`OPEN_EXISTING`, without write, create, truncate, delete, or write/delete
sharing. Directories, pipes, devices, non-disk objects, remote/UNC volumes, and
final reparse points fail closed. Every encountered intermediate reparse point
is rejected, including a link whose target would remain under the root. This
simple policy is stricter than merely following a contained link.

After open, the resolver still obtains `GetFinalPathNameByHandleW` and handle
identity metadata. The final object must be on the approved root volume and
remain below that root boundary. Security does not depend on
`std::filesystem::canonical()` or a pre-open textual prefix check.
The retained root handle's full file identity and current final path are
revalidated before and after every lookup, so a filesystem that permits a
directory rename while an attribute handle is open cannot substitute a new
directory at the configured name.

## Stable snapshot and bounded reads

`LocalReadOnlyFile` is a move-only RAII owner of the already validated handle.
Its public metadata is limited to virtual resource ID, root ID, size,
regular-file status, and an equality-only stable identity token. Native handles
and absolute/final paths are private.

Initial size, identity, write time, and change time come from that handle.
Sequential reads use the same handle. A final snapshot must match the initial
snapshot; short reads, extra data, identity/size/time changes, or concurrent
replacement fail without publishing derived material. The current project
safety limits are:

| Limit | Default | Hard cap |
| --- | ---: | ---: |
| consistency file size | 16 MiB | 64 MiB |
| streaming read chunk | 64 KiB | 1 MiB |

The supported consistency target must also be non-empty and fit `uint32_t`.
No whole-file vector is allocated.

## Metadata-only inventory

`LocalResourceInventoryBuilder` preserves exact `ResourceListState` order. For
each entry it records wire ordinal, resource type/index, original name length,
classification/resolution status, safe virtual-name correlation metadata, and
when resolved only the selected root ID, local byte count, and equality-only
file identity. It stores no path, handle, file bytes, digest, or raw native
identity.

Per-entry states distinguish `resolved`, `missing`, `unsafe_name`,
`unsupported_name_encoding`, `unsupported_mapping`, `ambiguous`, and
`io_error`. Missing and unsafe entries are ordinary metadata outcomes; a fatal
builder/configuration error publishes no partial inventory. The opaque 24-bit
wire size code is never used as a local size or allocation request.

An inventory says only whether a safe local candidate was observed. It does
not say required/optional, valid, matching, downloadable, parseable, ready, or
precached. Those policies belong to M3.2.2 and later milestones.

After a successful production-provider response boundary, the application
composition root builds this metadata-only inventory from the owning resource
list and logs only aggregate `resolved`, `missing`, `unsafe`, `unsupported`,
`ambiguous`, and `io-error` counts. It logs no resource name, native path,
identity, file bytes, or digest.

## Explicit non-goals

M3.2.1 adds no write path, persistent manifest/hash cache, auto-repair,
download root, resource request, `dlfile`, local-vs-server comparison, asset
loading, BSP/WAD/MDL/SPR/WAV parser, map loading, renderer upload, world-state
mutation, or precache state.
