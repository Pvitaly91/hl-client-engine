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
opcode-43 boundary. The opcode-43 resource-list semantic/body, server-info
second-client slot field, resource entries, snapshots, movement application,
and commands remain future evidence or implementation increments behind the
same provider boundary. The future bridge adapts
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
| `hlclient_goldsrc_signon` | exact fixed initial and transition client requests, strict `BZ2\0` in-memory envelope decoder, bounded confirmed service-message continuation, owning immutable server-info/pre-resource/delta/movement/user-info/transition state, dedicated private-value user-info grammar, fixed opcode-45 control, same-driver retained stages, neutral opcode-43 boundary, and exact cursor accounting | arbitrary commands, opcode-43/resource-list body parsing, resource responses, runtime delta or movement application, command execution, filesystem, renderer, SDL, assets, world state |
| `hlclient_auth` | asynchronous provider/operation contract and move-only authentication session lifetime | file policy, Steam implementation, sockets, renderer, world state |
| `hlclient_app_support` | explicit user-file auth adapter and bounded local-file loading | discovery, caching, Steam integration, fallback search, protocol parsing |
| `hlclient_goldsrc_client` | challenge/connect/response coordination, same-socket bootstrap/initial/pre-resource/delta/movevars/user-info/resource-transition composition, and driver/auth-lifetime ownership through the selected terminal stop | auth generation, wire codec duplication, arbitrary reliable payload production, opcode-43 body negotiation, runtime delta or movement application, OpenGL, SDL, filesystem, world/render state |
| `hlclient_client` | connection-independent client world and presentation state | raw socket ownership, GL resources |
| `hlclient_asset_api` | owning asset sources, neutral CPU assets, typed importer and registry contracts | filesystem I/O, SDL, OpenGL, sockets, SDK types |
| `hlclient_asset_manager` | virtual-file reads and dispatch through typed registries | format parsing, renderer resources, caches |
| `hlclient_scene_api` | scene-source contract and world-state-to-render-scene conversion | concrete network/injection providers, graphics calls |
| `hlclient_renderer_api` | neutral render scene and renderer contract | SDL or GoldSrc headers in its public API |
| `hlclient_renderer_opengl` | OpenGL 3.3 Core implementation using GLAD | packet parsing or client connection state |
| `hlclient_renderer_null` | headless renderer lifecycle and frame statistics | SDL, OpenGL, GLAD, Winsock, SDK types |
| `hlclient` | composition root and frame loop | reusable subsystem implementation |
| `hlclient_tests` | deterministic unit/integration tests with local resources | public Internet or installed-game requirements |

Public dependencies should point inward toward stable project-owned contracts.
Private implementation dependencies may point outward to SDL, GLAD, OpenGL,
Winsock, or SDK headers without exposing them to unrelated consumers.

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

The current application path reaches the bounded post-movevars boundary:

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
                                              -> terminal neutral opcode 43;
                                                 validated but unconsumed;
                                                 body unread, no reply
```

The application has only an explicit user-file authentication provider; it
does not generate tickets or integrate with Steam. Each later terminal stage
composes the driver on the already-bound transport and validates unchanged
local/exact remote endpoints. The sign-on branch queues only the fixed
five-byte initial request. The transition branch may later queue only the fixed
nine-byte request and owns at most one second payload received before its
covering ACK. Each public earlier stop closes after publishing its own
boundary; friend-only retention carries that same driver/lifetime only through
the selected continuation. No route exposes received raw reliable/fragment/
delta/movevars/user-info/resource bytes or updates filesystem, world, asset,
or renderer state; the transition request object exposes only its fixed typed
nine-byte wire message. The delta, MoveVars, user-info, and transition states are
metadata for future milestones and are not applied to packets, simulation,
player entities, resource lookup, or world memory.

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
[Resource transition](GOLDSRC_RESOURCE_TRANSITION.md), and
[Authentication provider](AUTHENTICATION_PROVIDER.md).

## Filesystem and asset boundary

The repository contains no game data. A user explicitly supplies `--basedir`
and optionally `--game` (default `valve`). Path resolution must prevent a game
or server-supplied relative path from escaping its approved roots. Downloaded
content, when implemented, will be validated before becoming visible to asset
loaders.

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
deferred until their parsers are implemented.

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
2. create the filesystem, `AssetImporterRegistries`, and `AssetManager` when a
   game root is available;
3. explicitly register compiled-in format implementations (none exist in
   M0.1);
4. when a later stop point is explicit, acquire bounded authentication material
   through the configured provider and retain its optional session lifetime;
5. create one nonblocking UDP transport and the M1/M2 coordinator for the
   validated endpoint;
6. select the built-in OpenGL or null renderer;
7. for OpenGL only, initialize SDL and create the window/context before the
   renderer;
8. poll events where applicable, advance the handshake coordinator, and let
   the selected stage-owned driver process the same socket until the requested
   opaque, initial-sign-on, pre-resource, delta-schema, movement-environment,
   user-info, or neutral opcode-43 boundary is
   acknowledged and published;
9. derive `RenderScene` from its `ClientWorldState`, render, and present;
10. stop after the configured terminal challenge/connect-request/
    connect-response/netchan-bootstrap/signon-boundary/pre-resource/
    delta-schemas/movevars/user-info/resource-list-boundary outcome,
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
