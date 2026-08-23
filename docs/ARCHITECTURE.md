# Architecture

## Purpose and constraints

`hl-client-engine` is a standalone, independently authored client whose first
compatibility target is the original GoldSrc Half-Life network ecosystem. The
same core should later support an in-process `hl.exe` injection bridge without
turning the renderer, simulation, or asset code into two separate engines.

The reference ABI remains Windows x86. Platform size assumptions must be made
explicit at serialization and ABI boundaries; wire data must never be decoded
by casting packet bytes to host structs.

## Non-negotiable data flow

```text
GoldSrc network source --\
                         +-> ClientWorldState -> RenderScene -> IRenderer
hl.exe bridge source ----/                         |           |
                                               OpenGL       Null
```

Each arrow is a translation boundary:

1. `hlclient_network` receives bytes without interpreting GoldSrc semantics.
2. `hlclient_goldsrc` bounds-checks and decodes those bytes into protocol values.
3. A concrete `IClientSceneSource` applies accepted input to
   `ClientWorldState`, an engine-owned representation of client-visible state.
4. `hlclient_scene_api` derives an immutable or frame-local `RenderScene`.
5. An `IRenderer` backend consumes only `RenderScene` and renderer resources.

This is an invariant, not a suggested layering style. In particular:

- renderer targets must not include GoldSrc packet or network headers;
- render code must not open sockets or drive connection state;
- network and GoldSrc code must not issue OpenGL calls;
- SDK structs must not leak across public module APIs;
- test providers, replay readers, standalone networking, and the future bridge
  must feed the same neutral world-state path;
- protocol decoding must be length-bounded and explicit about endianness.

## Providers and consumers

The target design allows several state providers without contaminating the
consumer side:

```text
GoldSrcNetworkSceneSource --\
ReplaySceneSource -----------+-> ClientWorldState -> RenderScene -> IRenderer
TestSceneSource -------------+
HlInjectionSceneSource -----/
```

The current standalone provider owns the M1 challenge, M2.1 one-shot connect,
M2.2 bounded connectionless response, and the same-socket netchan bootstrap
composition. M2.3.2 adds persistent reliable state; M2.3.3 adds a strict
fragment codec, bounded normal reassembly, deterministic outgoing
fragmentation, and a reusable transport-facing driver without interpreting
opaque bytes. M2.4.1 reuses that exact driver for the one captured client
request, strict in-memory service-envelope decoding, confirmed simple early
messages, and a typed stop before the first complex body. M2.4.2 continues that
exact owning payload through a strict typed server-info body, one confirmed
neutral control, and a category-C stop before the next complex body. The
M2.4.3 continuation decodes the exact ordered delta-schema registry, and M2.4.4
decodes the confirmed opcode-44 movement/environment metadata plus only the
confirmed simple controls before an exact opcode-13 boundary. M3.1.1 decodes
the bounded opcode-13 sequence to exact first-batch end, then optionally queues
the one fixed transition request, validates its reliable ACK lifecycle,
decodes the fixed opcode-45 control, and stops at a neutral body-unconsumed
opcode-43 boundary. M3.1.2 optionally continues from that exact retained
cursor, decodes the confirmed LSB-first standard `ResourceListMessage` into
ordered owning metadata, validates exact end-of-payload, and stops at a
metadata-only required-response boundary without TX or filesystem access. The
M3.1.3 continuation then separates descriptor/41-byte semantic/tail geometry,
uses the neutral `Opcode5ResourceResponse` codec, obtains private local-derived
fields only through `hlclient_resource_consistency_api`, queues semantics once
through the retained driver, and stops at the first opcode of the next complete
server payload. M3.2.1 adds an opt-in, pre-network
`PreparedLocalResourceConsistencyProvider`, a handle-sandboxed local resolver,
an evidence-gated GoldSrc name mapper, and an ordered metadata-only inventory.
The sign-on target still sees only the path-free API; without explicit provider
selection it ends as `provider_required` without TX. Custom/player-resource
grammar, server-info second-client slot evidence, readiness/precache, snapshots,
movement application, and commands remain future increments behind the same
boundaries. The future bridge adapts
observed or exported in-process state to the same project types. It must not
make renderer behavior depend on injected addresses or Valve private layouts.

## Target responsibilities

| Target | Responsibility | Must not own |
| --- | --- | --- |
| `hlclient_core` | logging, command-line parsing, fundamental utilities, version | SDL, sockets, protocol parsing |
| `hlclient_platform` | SDL lifetime, windows, events/input, GL context, clocks | game protocol or world state |
| `hlclient_filesystem` | safe base/game path discovery and asset-facing I/O foundations | Steam discovery policy embedded in render code |
| `hlclient_network` | address values, Winsock lifetime, nonblocking datagram transport | GoldSrc message meaning |
| `hlclient_goldsrc` | byte readers/writers, connectionless codecs, strict info strings, and opaque auth value | sockets, retries, files, logging, OpenGL, UI |
| `hlclient_goldsrc_netchan` | netchan classifier/base/fragment codec, payload transform, wrap-safe persistent session, bounded pending plus one reliable unit in flight, transactional unfragmented/fragment sends, filesystem-free slot-0 normal reassembly, bounded same-transport driver, owning events, metadata-only traces, and first-ACK compatibility primitive | transport creation/closure, authentication semantics or bytes, slot-1/file interpretation, decompression, files, `svc_*`, world/render state |
| `hlclient_hash_md5` | project-owned incremental MD5 required only for GoldSrc compatibility material | filesystem, external crypto libraries, trust/security policy, GoldSrc wire types |
| `hlclient_local_resources` | explicit validated local search roots, byte-exact virtual names, Win32 read-only handle sandbox, final-handle containment, stable equality-only identity, bounded resolution and streaming inspection | sockets, server messages, downloads, cache/precache, assets, renderer |
| `hlclient_resource_consistency_api` | path-free bounded provider requirements, move-only asynchronous operation/session/material ownership, cancellation, and private opaque-material handoff | filesystem/path policy, local lookup, checksum calculation, sockets, GoldSrc list types, assets, renderer |
| `hlclient_resource_consistency_local` | pre-network preparation of the fixed `tempdecal.wad` compatibility target and one-shot nonblocking provider operation | server-derived paths, response codec/layout, network creation, writes, downloads, cache, assets |
| `hlclient_goldsrc_signon` | exact fixed initial/transition requests, strict `BZ2\0` decoding, owning immutable sign-on/list/response states, historical neutral opcode-43 and zero-TX resource-list stop, exact standard list and neutral 41-byte opcode-5 codecs, carrier/tail separation, provider-required response stage, same-driver semantic-once lifecycle, and next-payload opcode boundary | arbitrary commands, custom/player-resource bodies, production consistency material, resource resolution, runtime application, command execution, filesystem, renderer, SDL, assets, world state |
| `hlclient_goldsrc_local_resources` | evidence-gated resource-type/name classification and ordered metadata-only `LocalResourceInventoryState` adapter | sign-on transport, readiness/precache decisions, downloads/cache, file contents, asset loading |
| `hlclient_auth` | asynchronous provider/operation contract and move-only authentication session lifetime | file policy, Steam implementation, sockets, renderer, world state |
| `hlclient_app_support` | explicit user-file auth adapter and bounded local-file loading | discovery, caching, Steam integration, fallback search, protocol parsing |
| `hlclient_goldsrc_client` | challenge/connect coordination, same-socket bootstrap/initial/pre-resource/delta/movevars/user-info/transition/list/response composition, and driver/auth/provider lifetime ownership through the selected terminal stop | auth or consistency-material generation, wire codec duplication, arbitrary reliable payload production, runtime application, OpenGL, SDL, filesystem, world/render state |
| `hlclient_client` | connection-independent client world and presentation state | raw socket ownership, GL resources |
| `hlclient_asset_api` | owning asset sources, neutral CPU assets, typed importer and registry contracts | filesystem I/O, SDL, OpenGL, sockets, SDK types |
| `hlclient_asset_manager` | virtual-file reads and dispatch through typed registries | format parsing, renderer resources, caches |
| `hlclient_scene_api` | scene-source contract and world-state-to-render-scene conversion | concrete network/injection providers, graphics calls |
| `hlclient_renderer_api` | neutral render scene and renderer contract | SDL or GoldSrc headers in its public API |
| `hlclient_renderer_opengl` | OpenGL 3.3 Core implementation using GLAD | packet parsing or client connection state |
| `hlclient_renderer_null` | headless renderer lifecycle and frame statistics | SDL, OpenGL, GLAD, Winsock, SDK types |
| `hlclient_local_resource_check` | network-free, read-only diagnostic composition for an explicit user-owned root | stock process launch, path/digest output, file mutation, protocol transport |
| `hlclient` | composition root and frame loop | reusable subsystem implementation |
| `hlclient_tests` | deterministic unit/integration tests with local resources | public Internet or installed-game requirements |

Public dependencies should point inward toward stable project-owned contracts.
Private implementation dependencies may point outward to SDL, GLAD, OpenGL,
Winsock, or SDK headers without exposing them to unrelated consumers.

The M3.2.1 target direction is deliberately acyclic:

```text
hlclient_hash_md5 -> hlclient_core
hlclient_local_resources -> hlclient_core + private Win32 APIs
hlclient_resource_consistency_local
    -> hlclient_resource_consistency_api
    -> hlclient_local_resources
    -> hlclient_hash_md5
hlclient_goldsrc_local_resources
    -> hlclient_goldsrc_signon
    -> hlclient_local_resources
hlclient_goldsrc_signon -> hlclient_resource_consistency_api
```

The corresponding Visual Studio folders are `Engine/Core/Hash`,
`Engine/Resources/Local`, `Engine/Resource Consistency`, and
`Engine/Resources/GoldSrc`; the diagnostic belongs under `Tools/Resources`.
In particular, `hlclient_goldsrc_signon` does not acquire a filesystem or
concrete local-provider dependency.

## Platform boundary

SDL3 is the owner of:

- process-level SDL initialization and shutdown;
- window creation and destruction;
- event polling and input normalization;
- OpenGL context creation, swap interval, and buffer presentation;
- portable timing needed by the client loop.

Native APIs are allowed only where SDL does not provide the required contract
or interoperability demands it. Current examples are Winsock2 behind
`hlclient_network` and, later, module loading/injection primitives behind a
dedicated Windows bridge target. Include Windows headers in implementation
files or narrowly scoped private headers, with `WIN32_LEAN_AND_MEAN` and
`NOMINMAX` where appropriate.

## Renderer boundary

`hlclient_renderer_api` owns the backend-neutral `RenderScene` and `Renderer`
contract. `hlclient_renderer_opengl` implements that contract with OpenGL 3.3
Core and the committed GLAD2 loader. `hlclient_renderer_null` implements the
same contract without SDL, a window, a graphics context, or a GPU. Another
renderer backend must be able to consume the same scene without changing
GoldSrc decoding.

Resource handles crossing this boundary must be project-owned opaque values,
not raw OpenGL names. Asset decoding should produce CPU-side formats before a
renderer-specific upload step. Destruction order must preserve the GL context
until all GL resources and the renderer are released.

## Protocol and safety boundary

Network input is untrusted. Parsers must:

- consume a bounded byte span through checked readers;
- reject truncated, oversized, inconsistent, or unsupported messages cleanly;
- use fixed-width integer types for wire fields;
- decode byte order explicitly;
- cap strings, resource counts, fragment sizes, decompression output, and
  allocation sizes before allocating;
- keep connectionless and sequenced-channel parsing separate;
- make malformed-packet tests local and deterministic.

Compatibility constants may be checked against official SDK declarations, but
the runtime implementation remains project-owned.

The current application path exposes explicit bounded stops through the
provider-gated post-resource response boundary:

```text
--connect IPv4:port
    -> UdpDatagramTransport
    -> ChallengeExchange
    -> getchallenge request / strict challenge response
       |-> default terminal M1 success
       `-> optional ConnectRequestStage on the same transport/socket
           -> one connect send
              |-> terminal M2.1 success/error
              `-> optional ConnectResponseWaitStage on that transport/socket
                  -> strict connectionless ACCEPT/REJECT
                     |-> terminal M2.2 outcome
                     |-> optional NetchanBootstrapStage on the same socket
                     |   -> coordinator/stage-owned NetchanDriver
                     |   -> one unfragmented or reassembled slot-0 payload
                     |   -> required transport acknowledgement(s)
                     |   -> terminal CLI netchan-bootstrap outcome
                     `-> optional InitialSignonStage on the same socket
                         -> one typed reliable client request
                         -> request ACK and normal-stream reassembly
                         -> strict BZ2-NUL in-memory envelope
                         -> bounded opcode-8 text control
                         |-> terminal opcode-11 boundary; body untouched
                         `-> optional PreResourceSignonStage facade
                             -> exact-cursor typed opcode-11 server-info
                             -> bounded empty-string/zero opcode-54 control
                             -> opcode-14 delta-description boundary
                              `-> optional DeltaDescriptionStage facade
                                  -> seven exact-cursor LSB-first schemas
                                  -> immutable ordered metadata registry
                                  |-> terminal numeric opcode-44 boundary;
                                  |   body untouched for `delta-schemas`
                                  `-> optional MovementEnvironmentStage facade
                                      -> strict typed opcode-44 MoveVars state
                                      -> exact opcodes 32, 5, 39, and 9
                                      -> exact opcode-13 boundary
                                      |-> terminal `movevars`; body untouched
                                      `-> optional UserInfoSignonStage
                                          -> one or more exact opcode-13 messages
                                          -> immutable private-value metadata
                                          -> exact end of first service payload
                                          |-> terminal `user-info`; no TX
                                          `-> optional ResourceTransitionStage
                                              -> one exact typed 9-byte request
                                              -> driver-owned TX/retry/covering ACK
                                              -> bounded later BZ2-NUL transfer
                                              -> opcode 45 + exact 8-byte body
                                              |-> terminal neutral opcode 43;
                                              |   validated but unconsumed;
                                              |   body unread, no reply
                                               `-> optional ResourceListStage
                                                   -> exact opcode-43 cursor
                                                   -> strict LSB-first count/entries
                                                   -> immutable ordered metadata
                                                   -> terminal zero fill + exact EOP
                                                   -> response-required boundary
                                                   |-> terminal `resource-list`;
                                                   |   historical metadata only,
                                                   |   zero queued response/TX/files
                                                   `-> optional ResourceClientResponseStage
                                                       -> descriptor/41-byte/tail split
                                                       -> typed provider requirements
                                                       |-> no selected provider:
                                                       |   `provider_required`, no TX
                                                       `-> prepared local material:
                                                           -> queue semantic bytes once
                                                           -> driver retry/covering ACK
                                                           -> first complete next payload
                                                           -> opcode at offset 0;
                                                              complex body unconsumed
```

The application has only an explicit user-file authentication provider; it
does not generate tickets or integrate with Steam. The local consistency
provider is likewise explicit and is fully prepared before network creation.
Each later terminal stage composes the driver on the already-bound transport
and validates unchanged
local/exact remote endpoints. The sign-on branch queues only the fixed
five-byte initial request. The transition branch queues only the fixed
nine-byte request and owns at most one second payload received before its
covering ACK. The resource-list continuation queues nothing. Each public
earlier stop closes after publishing its own
boundary; friend-only retention carries that same driver/lifetime only through
the selected continuation. The separate response continuation can queue one
typed semantic unit only after a provider succeeds; without explicit local
selection it publishes `provider_required` and sends nothing. No
route exposes received raw reliable/fragment/
delta/movevars/user-info/resource bytes or updates filesystem, world, asset,
or renderer state; the transition request object exposes only its fixed typed
nine-byte wire message. The delta, MoveVars, user-info, transition, and
resource-list states are metadata for future milestones and are not applied to
packets, simulation, player entities, resource lookup, or world memory.

The active stock verifier has no completed restoration-attested M3.1.3 runs,
and therefore no tracked response projection exists. Historical reconstructed
carrier evidence and deterministic fake-HLDS tests justify the bounded API and
test behavior, but are not an active project-client-to-stock response success
claim. M3.2.1 implements the local provider independently of that pending stock
evidence. M3.2.2 is next for readiness/precache state; M3.1.4 remains
conditional on sufficient evidence for the next complex server message.

M2.3.3 splits netchan into pure base/fragment wire codecs and transform,
transport-independent persistent reliable state, a transactional normal
reassembler, a reusable transport-facing `NetchanDriver`, and the bootstrap
composition stage. The driver borrows—never creates or closes—the already-bound
`IDatagramTransport`, requires exact endpoints, uses injected monotonic time,
and exposes only const session/reassembler inspection. The bootstrap stage's
post-success mutable session accessor is a narrow compatibility seam; it does
not expose mutable driver/reassembler state or transfer socket ownership.

Project defaults/hard maxima include 4,096/16,384-byte datagrams,
4,088/16,376-byte unfragmented bodies, a 16,376-byte pending/outgoing transfer,
1,024-byte normal fragments, 65,536/1,048,576-byte incoming normal transfers,
64/1,024 fragments and ranges, one active normal transfer, 30/300-second channel
inactivity, 5/30-second fixed fragment deadlines, 8/64 receives and 1/8 sends
per update, and 16/256 queued events with a minimum capacity of five. These are
project safety limits, not stock-engine maxima.

M3.1.1 adds project-only defaults/hard caps of 2,048/8,192 bytes per user-info
message, 1,024/4,096-byte info strings, 64/256-byte keys, 256/4,096-byte values,
64/256 entries, 32/256 messages, 32,768/262,144 total message bytes, and
64/256 user-info stage events. The transition request and opcode-45 message
are fixed at a 9/9-byte default/hard cap; the private opcode-45 `u32` has a
1,000,000/`UINT32_MAX` project upper bound, the second decompressed payload is
65,536/1,048,576 bytes, and transition events are 64/256. None is a claim
about a stock engine buffer maximum.

Session and reassembly mutations are transactional. Preparation is read-only;
only successful send or receive commit advances sequences/generations, promotes
pending A, records a fragment, or publishes completion. Failures and abandoned,
foreign, stale, or consumed plans preserve prior state. Canonical bytes are
remunged for each committed sequence. Count-greater-than-one outgoing normal
fragments use deterministic per-fragment stop-and-wait; this is project-tested
policy pending stock-client multi-fragment verification.

Fresh signed-stock capture confirms two descriptor slots, slot-0 normal shape,
1,024-byte chunks, no stable wire transfer ID, per-fragment alternating reliable
generation, ACK-gap retry, duplicate/old filtering, and next-transfer gating.
True unseen reorder, slot-1/file semantics, compression universality, explicit
old-after-completion behavior, and live project-to-stock fragmentation remain
pending. See [GoldSrc fragmentation](GOLDSRC_FRAGMENTATION.md).

The deterministic M2.3.2 fake-HLDS UDP integration reuses the exact bootstrap
socket/source/session for one outgoing reliable clear and one incoming owning
reliable delivery with duplicate/old filtering. M2.3.3 codec, reassembly,
outgoing, and driver behaviors are covered deterministically; such tests are not
promoted to stock-server interoperability claims.

`NetchanDriver` is the reusable same-transport polling/timeout owner. The
bootstrap stage/coordinator constructs and owns one through `--stop-after
netchan-bootstrap`; `--stop-after signon-boundary` instead creates
`InitialSignonStage` directly after `ACCEPT`, so no second driver competes for
the socket. `--stop-after pre-resource` creates `PreResourceSignonStage`, whose
nested private-retention mode reuses that exact initial stage, driver, socket,
and lifetime until the synchronous server-info continuation terminates.
`DeltaDescriptionStage` and `MovementEnvironmentStage` repeat that private,
friend-only retention pattern for the exact delta and movevars cursors.
`UserInfoSignonStage` carries the same owning payload to exact first-batch end;
`ResourceTransitionStage` alone may retain the same driver afterward for the
fixed request/ACK/second-transfer path. The public earlier stops retain their
historical close-on-success behavior. An
embedding composition can also own the driver persistently. In every case the
existing transport and optional opaque `INetchanDriverLifetime` stay valid
through the driver terminal. Timeout, cancellation, network/protocol
failure, event backpressure, and close clear reliable/reassembly/unreliable
state and release that guard exactly once. A lower-level session caller remains
responsible for `NetchanSession::clear_reliable_state()` on terminal failure.

Challenge traces use bounded previews. Connect-request, connect-response,
netchan, fragment, driver, initial-sign-on, pre-resource, delta, and
movement-environment, user-info, and transition traces never contain raw
packets, authentication bytes, private user-info/control values,
compressed/decompressed payloads, or boundary remainders. Typed callbacks may
carry confirmed owning-state string views (such as schema or sky names) only
for the callback duration; terminal output always passes untrusted text through
the bounded presentation sanitizer. See
[Connect response](GOLDSRC_CONNECT_RESPONSE.md),
[Netchan](GOLDSRC_NETCHAN.md), [Fragmentation](GOLDSRC_FRAGMENTATION.md),
[Initial sign-on](GOLDSRC_INITIAL_SIGNON.md),
[Server info](GOLDSRC_SERVERINFO.md),
[Delta descriptions](GOLDSRC_DELTA_DESCRIPTIONS.md),
[Movement environment](GOLDSRC_MOVEVARS.md),
[User info](GOLDSRC_USERINFO.md),
[Resource transition](GOLDSRC_RESOURCE_TRANSITION.md),
[Resource list](GOLDSRC_RESOURCE_LIST.md),
[Resource response](GOLDSRC_RESOURCE_CLIENT_RESPONSE.md),
[Local resource resolution](LOCAL_RESOURCE_RESOLUTION.md),
[Local consistency provider](LOCAL_RESOURCE_CONSISTENCY_PROVIDER.md), and
[Authentication provider](AUTHENTICATION_PROVIDER.md).

## Filesystem and asset boundary

The repository contains no game data. A user explicitly supplies `--basedir`
and optionally `--game` (default `valve`). The M3.2.1 local provider additionally
requires explicit `--resource-consistency-provider local`; basedir alone does
not enable it. A non-Valve game produces an ordered game root followed by a
`valve` fallback, while `valve` produces one deduplicated root. There is no CWD,
registry, Steam-library, environment, repository, or build-directory discovery.

Normal project runtime may read an explicitly supplied, user-owned Steam
Half-Life installation. It does so with zero writes, launches no stock process,
and does not mutate configuration. Active stock `hl.exe`/HLDS research remains
subject to the stricter isolated-copy marker and root preflight. Runtime access
does not relax that research policy, and neither path proves stock
interoperability by itself.

Server resource-name bytes never become native paths directly. The pure
GoldSrc mapper accepts printable ASCII and `/` only, rejects traversal,
absolute/drive/UNC/device/ADS/backslash/reserved-device and bounded-name
violations, and does no decoding or repair. The backend then converts the safe
virtual name component-by-component. It opens with Win32 read-only
`CreateFileW`/`GENERIC_READ`/`OPEN_EXISTING`, rejects every intermediate and
final reparse point, requires a regular local disk file, and validates the
opened handle's final target, volume, and root containment. Metadata and bounded
streaming bytes come from that same handle; initial/final identity, size, and
write/change metadata detect concurrent modification.

The production provider resolves only profile-fixed `tempdecal.wad`, never a
server- or CLI-selected filename. The general ordered resource inventory stores
correlation metadata and typed resolution status only. It is not a readiness,
precache, download, cache, asset, or renderer state. Downloaded content, if a
later milestone implements it, must be validated before becoming visible to
asset loaders.

Asset formats (BSP, WAD, MDL, sprites, sounds) should have parser libraries that
do not depend on SDL, the renderer, or a live network session. This makes them
fuzzable and reusable by tests and tools.

The M0.1 asset path is:

```text
IFileSystem -> AssetSource -> typed importer -> neutral CPU asset -> AssetManager
```

`AssetSource` owns its path and input bytes. Typed registries own importers and
select them by signature/version/structure confidence followed by explicit
priority; an unresolved tie is an error rather than a registration-order
choice. Format modules are statically linked and registered explicitly by the
composition root. See [Asset pipeline](ASSET_PIPELINE.md) for the complete
selection and extension contract.

There is no global registry, global constructor registration, renderer-side
file parsing, or GPU handle in a neutral asset. Real GoldSrc format targets are
deferred until their parsers are implemented. The M3.2.1 resolver and provider
do not feed `AssetManager` or either renderer.

## Clean-room and SDK boundary

No runtime target may depend on `Pvitaly91/hl-engine`, its server code, or any
other private server implementation. Compatibility work is based on observable
behavior, public protocol information, and an explicitly isolated official
Valve SDK reference.

Implementation code must not be copied from ReHLDS, Xash3D,
reverse-engineered/proprietary GoldSrc source dumps, or original Valve binaries
such as `hl.exe`, `hw.dll`, or `sw.dll`. External implementations may be treated
only as separately recorded behavioral/reference material where legally and
technically permissible. Repository code must remain independently authored.

M1's wire profile was established by black-box observation of stock signed
Valve programs: the exact request transmission was captured from original
`hl.exe`, and the exact response was observed live from original HLDS. That
evidence establishes the framing, terminators, response shape, and dynamic
challenge. It does not establish the semantics of the three decimal values
after the challenge; they remain opaque profile parameters. No third-party
engine implementation was used as production source code.

The Half-Life SDK submodule is pinned. Its include directories are exposed by a
`SYSTEM` interface target so SDK warnings do not weaken or pollute warning
policy for project code. Do not compile SDK game/server programs, link its
prebuilt libraries, use its bundled SDL2, or copy SDK implementation files into
project modules. Any future use beyond reference headers requires an explicit
architecture and license review.

## Composition and lifetime

`hlclient` is the composition root. A normal standalone frame follows this
order:

1. parse command-line inputs and validate paths/endpoints;
2. independently create the asset-facing filesystem,
   `AssetImporterRegistries`, and `AssetManager` only when that existing asset
   path is requested; the consistency provider does not feed them;
3. explicitly register compiled-in format implementations (none exist in
   M0.1);
4. when a later stop point is explicit, acquire bounded authentication material
   through the configured provider and retain its optional session lifetime;
5. only for the response-boundary route with explicit local selection, validate
   `--basedir`/`--game`, prepare fixed-target consistency material through one
   sandboxed handle, close the file, and retain the one-shot provider;
6. create one nonblocking UDP transport and the M1/M2 coordinator for the
   validated endpoint;
7. select the built-in OpenGL or null renderer;
8. for OpenGL only, initialize SDL and create the window/context before the
   renderer;
9. poll events where applicable, advance the handshake coordinator, and let
   the selected stage-owned driver process the same socket until the requested
   opaque, initial-sign-on, pre-resource, delta-schema, movement-environment,
   user-info, neutral opcode-43, or standard resource-list/response boundary is
   acknowledged and published;
10. derive `RenderScene` from its `ClientWorldState`, render, and present;
11. stop after the configured terminal challenge/connect-request/
    connect-response/netchan-bootstrap/signon-boundary/pre-resource/
    delta-schemas/movevars/user-info/resource-list-boundary/resource-list or
    resource-response-boundary outcome,
    let driver terminal cleanup release its optional lifetime exactly once,
    then shut down renderer resources before their platform dependencies.

Partially initialized states must unwind safely through RAII. Logging and error
messages should identify the failed boundary without exposing secrets or
turning malformed remote data into a process crash.

## Module and plugin policy

The current product is a modular monolith: one executable, separate static
CMake targets, narrow public contracts, and explicit construction and
registration in `apps/hlclient`. This avoids committing to a C++ DLL ABI while
the APIs are still changing.

After stabilization, optional runtime plugins may use a versioned C ABI. That
future ABI must not pass STL containers, `std::string`, iterators, C++
exceptions, C++ virtual objects across different toolsets, or memory without an
explicit allocator and ownership contract. M0.1 does not implement a loader or
that ABI.
