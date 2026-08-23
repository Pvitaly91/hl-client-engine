# Resource-consistency provider boundary

M3.1.3 introduced `hlclient_resource_consistency_api` as a path-free boundary
between GoldSrc sign-on and approved resource-consistency implementations.
M3.2.1 keeps that API unchanged and adds an explicitly selected production
implementation, `PreparedLocalResourceConsistencyProvider`. The implementation
is separate from the API and is the only layer that maps the current profile to
local read-only material.

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
unless a provider supplies typed material. With no provider it preserves the
typed `provider_required` outcome and sends nothing. The local provider is
opt-in through `--resource-consistency-provider local`; it is never enabled
from `--basedir` alone.

## API and ownership

`IResourceConsistencyProvider::begin()` receives only
`ResourceConsistencyRequirements`. The supported requirement identifies a
compatibility profile, one material, and a fixed 16-byte opaque width. It does
not contain a resource name, path, `ResourceListState`, socket, or raw response.
The implementation, not the server or CLI, maps
`stock_protocol_48_opcode5_single_resource` to the fixed virtual target
`tempdecal.wad`.

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
`cancel()` are nonblocking calls on the sign-on update path. The prepared local
implementation performs root validation, sandboxed open, streaming read, and
MD5-compatible inspection synchronously in the application composition root
before network initialization. Its later `begin()` consumes the prepared
operation at most once, and its `update()` performs no filesystem work. A
different implementation must own slow work behind its polling operation. A
provider passed as a non-owning pointer must outlive its stage/coordinator.

The sign-on stage applies its own five-second manual-clock timeout (60-second
hard cap). Provider-supplied diagnostic strings do not cross into stage events,
coordinator errors, or CLI logs: only bounded project wording and typed error
codes are published. Local absolute paths and raw MD5 bytes are not logged or
exposed by the provider metadata API.

## Explicit local preparation

The local implementation requires:

```text
--resource-consistency-provider local
--basedir <explicit user-owned Half-Life root>
--game <game-directory>                         # default: valve
```

`LocalResourceSearchRoots` validates and owns an ordered set of local directory
handles. For a non-`valve` game it searches `<basedir>/<game>` before
`<basedir>/valve`; for `valve` it retains one root. Duplicate canonical roots
collapse deterministically. There is no current-directory, registry, Steam
library, environment, repository, or build-tree discovery fallback.

Preparation resolves only the fixed `tempdecal.wad` target. The command line
cannot replace it with a server name or an arbitrary file path. Missing,
empty, oversized, ambiguous, non-regular, remote-volume, reparse, containment,
I/O, or concurrent-change failures publish no material and occur before the
first network packet. Earlier stop points do not prepare the provider or touch
its roots.

Normal runtime may read an explicitly supplied, user-owned Steam Half-Life
installation. That permission is read-only and does not launch a stock
executable, mutate configuration, or auto-discover an installation. Active
stock-client/HLDS research remains a separate policy and still requires an
isolated research copy; M3.2.1 does not weaken those verifier preflights.

## Read-only sandbox and snapshot

The Win32 backend opens roots and files with `CreateFileW`. Resource files use
`GENERIC_READ` and `OPEN_EXISTING`, with no write, create, truncate, delete, or
cache path. It requires a regular `FILE_TYPE_DISK` object on a supported local
volume. Roots, every intermediate component, and the final object reject
reparse points, including an otherwise contained intermediate reparse.

Classification is performed before open, but the security decision does not
end there. The opened handle supplies the final path, volume identity, file
identity, size, write/change metadata, and bytes. Final-handle containment must
remain inside the selected approved root and on its volume. The same handle is
streamed from offset zero; no stat-close-reopen sequence is used. EOF and a
final metadata/identity snapshot are checked before material publication, so a
short read, extra bytes, or concurrent change fails closed.

The current project safety limits are:

| Limit | Default | Hard cap |
|---|---:|---:|
| consistency file size | 16 MiB | 64 MiB |
| read chunk | 64 KiB | 1 MiB |
| material count | 1 | 256 |
| opaque bytes per material | 16 | 4,096 |

The current profile additionally requires a non-empty file whose exact handle
size fits both the configured maximum and `uint32_t`. Defaults and hard caps are
project policy, not claims about stock-engine maxima.

## MD5 compatibility material

`hlclient_hash_md5` provides the project-owned incremental `Md5Hasher`. It has
64-bit checked message-length accounting, explicit little-endian word handling,
no unaligned casts, and no filesystem, OpenSSL, Windows CryptoAPI, or other
external dependency. The local inspection layer updates it incrementally from
the bounded handle reads.

MD5 is used only to reproduce the observed GoldSrc compatibility material. It
is not a security, authenticity, integrity, or trust primitive.

## Dependency direction

```text
hlclient_hash_md5 ----------------------> hlclient_core
hlclient_local_resources --------------> hlclient_core
hlclient_resource_consistency_local ---> hlclient_resource_consistency_api
                                      `-> hlclient_local_resources
                                      `-> hlclient_hash_md5

hlclient_goldsrc_signon ---------------> hlclient_resource_consistency_api
```

The sign-on target has no filesystem or local-provider implementation
dependency. The application composition root supplies the provider through the
existing path-free interface. Local resources do not depend on
`NetchanDriver`, and the hash target does not depend on GoldSrc protocol types.

## Read-only verification

`hlclient_local_resource_check` is a network-free diagnostic executable. The
PowerShell wrapper snapshots the explicitly supplied root and fixed target
before and after invoking it:

```powershell
.\scripts\verify_local_resource_provider.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_local_resource_check.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" `
  -Game valve
```

It reports bounded metadata such as validated root count, resolution status,
byte count, opaque width, and `external-file-drift`; it does not print an
absolute path or digest. This command is an optional user-owned installation
check. It is not stock interoperability evidence, does not start `hl.exe` or
HLDS, and no manual validation result is claimed by this document.

## Explicitly absent

M3.2.1 adds no resource download, cache write, downloaded-resource root,
precache handle, readiness decision, resource request generation, `dlfile`,
captured-response loader, raw-response CLI, filesystem-backed sign-on code,
asset loading, format parsing, renderer integration, or fallback material. The
general local inventory is metadata-only and is not used to generate the fixed
provider material.
