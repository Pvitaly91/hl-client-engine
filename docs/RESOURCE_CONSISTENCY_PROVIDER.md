# Resource-consistency provider boundary

M3.1.3 introduces `hlclient_resource_consistency_api` as a path-free boundary
between GoldSrc sign-on and future approved local-resource work. It is an API,
not a filesystem implementation. No production provider is registered in this
milestone.

## Why the boundary exists

The selected reliable range in every reconstructed stock response is 41 bytes.
Its final fixed-width 16-byte field equals the MD5 of the local
`tempdecal.wad` used by the observed client, and the preceding little-endian
32-bit field equals that file's byte count. The same resource is absent from
all three captured server resource-list profiles. These observations establish
that the builder needs locally derived material and must not replay captured
bytes. They do not, by themselves, prove the semantic name of opcode 5, so the
wire API remains neutral.

The production sign-on target therefore cannot construct a complete response
unless a provider supplies typed material. With no provider it publishes the
typed `provider_required` outcome and sends nothing.

## API and ownership

`IResourceConsistencyProvider::begin()` receives only
`ResourceConsistencyRequirements`. The supported requirement identifies a
compatibility profile, one material, and a fixed 16-byte opaque width. It does
not contain a resource name, path, `ResourceListState`, socket, or raw response.

The provider returns a move-only `ResourceConsistencyOperation`. Calls to
`update()` yield one of:

- `pending`;
- `succeeded` with a move-only `ResourceConsistencySession`;
- `failed` with a bounded typed `ResourceConsistencyError`.

The session owns a single `ResourceConsistencyMaterial` and an optional
provider lifetime guard. Public metadata exposes only the local byte count and
opaque width. Opaque bytes are private and accessible only to the exact
opcode-5 builder. The session guard remains alive after material is moved into
the builder and is released once by stage cleanup.

Cancellation is cooperative and explicit. `begin()`, `update()`, and
`cancel()` are nonblocking calls on the sign-on update path; slow work must be
owned behind the polling operation. A provider passed as a non-owning pointer
must outlive its stage/coordinator, while the returned operation owns its own
pending state. The sign-on stage applies its own five-second manual-clock
timeout (60-second hard cap); the provider API contains no production sleep or
thread. Provider-supplied diagnostic strings do not cross into stage events,
coordinator errors, or CLI logs: only bounded project wording and typed error
codes are published.

## Bounds

These values are project safety policy, not stock-engine maxima:

| Limit | Default | Hard cap |
|---|---:|---:|
| material count | 1 | 256 |
| opaque bytes per material | 16 | 4,096 |

The supported stock-evidence profile additionally requires exactly one
material and exactly 16 opaque bytes. Invalid limits, empty material, and
limit+1 material fail before publication.

## Dependency direction

```text
hlclient_resource_consistency_api
             ^
             |
hlclient_goldsrc_signon
```

The API target has no dependency on filesystem, assets, renderer, network, or
GoldSrc list types. The sign-on target consumes the API. A future M3.2 provider
may depend on an approved local-resource abstraction, but the protocol layer
must continue to receive only typed bounded material.

## Explicitly absent

M3.1.3 adds no path normalization, file open/stat/read, VFS mount, checksum
calculation, resource resolution, download, cache, precache, captured-response
loader, raw-response CLI, or fallback value. Synthetic providers and material
are test-only.
