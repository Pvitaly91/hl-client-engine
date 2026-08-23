# Local resource-consistency provider

M3.2.1 supplies the first production implementation behind the existing
path-free `IResourceConsistencyProvider` boundary. Filesystem configuration and
I/O remain in `hlclient_resource_consistency_local`; GoldSrc sign-on still sees
only `ResourceConsistencyRequirements`, a move-only session, a byte count, and
private bounded opaque material.

## Fixed compatibility target

The only supported profile maps internally to the fixed virtual target:

```text
stock_protocol_48_opcode5_single_resource -> tempdecal.wad
```

The target never comes from a server `ResourceName`, CLI filename, environment
variable, captured response, or arbitrary path. Resolution follows the
validated game-root then `valve` fallback order.

The wire message retains the neutral M3.1.3 name `Opcode5ResourceResponse`.
Implementing a local provider does not independently settle the protocol
opcode's semantic category or active stock interoperability.

## Prepare before networking

`PreparedLocalResourceConsistencyProvider` is prepared synchronously in the
application composition root before the UDP runtime/socket is created:

1. validate explicit `--basedir` and `--game` roots;
2. resolve the fixed target through `LocalResourceResolver`;
3. retain its one read-only handle;
4. validate nonzero bounded size and initial handle snapshot;
5. stream the bytes through the project MD5 implementation;
6. require an exact final snapshot from the same handle;
7. construct one private `ResourceConsistencyMaterial`;
8. only then allow the connection pipeline to start.

Missing, ambiguous, unsafe, non-regular, remote, escaped/reparse, empty,
oversized, changed, unreadable, or unhashable targets fail before the first
network packet. There is no empty/zero/captured fallback material.

The option is explicit:

```text
--resource-consistency-provider local --basedir <root> --game <directory>
```

The provider is prepared only for `--stop-after resource-response-boundary`.
Selecting it for an earlier stop does not perform provider filesystem work.
Omitting the option preserves the M3.1.3 typed `provider_required` result and
zero response transmission.

## Streaming MD5 compatibility material

`Md5Hasher` is a project-owned incremental C++20 implementation with explicit
little-endian operations, checked 64-bit message length, no unaligned casts,
and a fixed 16-byte digest. It has no OpenSSL, CryptoAPI, filesystem, GoldSrc,
or renderer dependency. Standard independent vectors and boundary/chunked
lifecycle cases define its behavior.

MD5 is used only because the selected GoldSrc compatibility profile requires
MD5-compatible bytes. It is not a security, authenticity, integrity, or trust
primitive. The digest has no public provider getter and is never logged.

## Nonblocking one-shot lifecycle

Preparation owns all blocking filesystem work. Later `begin()` is prompt and
returns an immediate bounded operation; `update()` promptly publishes the
prepared move-only session. The session retains its lifetime guard and private
material. Cancellation is idempotent, destruction closes the handle, and a
second `begin()` fails as already consumed. No operation closes and reopens the
path or hashes a second object.

Safe public preparation metadata is limited to validated root count, target
resolution state, byte count, and opaque material length. Errors are bounded
and path-free. Absolute roots, final paths, raw file identities, resource
names, digest bytes, authentication, and wire bytes are not logged.

## Offline read-only verification

`hlclient_local_resource_check` prepares the same production provider without
network, Steam, SDL, OpenGL, or a stock process. It reports only that roots were
validated, the fixed target resolved, its byte count, the 16-byte material
length, and that no write was requested.

`scripts/verify_local_resource_provider.ps1` accepts an explicit user-owned
root, snapshots the relevant target and fail-closed root-entry manifests
(at most 200,000 entries and 64 components deep), invokes
the checker, and verifies unchanged content hash, size, write time, and entry
set. It does not print the target digest or absolute paths and exits nonzero on
drift. This is a runtime read-only check, not stock protocol evidence.

## Dependency and scope boundary

```text
hlclient_hash_md5
        ^
        |
hlclient_resource_consistency_local
        |
        +----> hlclient_local_resources
        |
        `----> hlclient_resource_consistency_api

hlclient_goldsrc_signon ---> hlclient_resource_consistency_api
```

The sign-on target does not link the local resolver, Win32 backend, hash
implementation, or production provider. No provider cache, sidecar, registry
value, temporary manifest, file write, download, precache, asset load, or
renderer dependency is introduced.
