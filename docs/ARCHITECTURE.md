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

The current standalone provider owns the M1 challenge and M2.1 one-shot connect
request stages. Future increments may add response semantics, channel
sequencing, sign-on, resource state, snapshots, and commands behind the same
provider boundary. The future bridge adapts observed or exported in-process
state to the same project types. It must not make renderer behavior depend on
injected addresses or Valve private layouts.

## Target responsibilities

| Target | Responsibility | Must not own |
| --- | --- | --- |
| `hlclient_core` | logging, command-line parsing, fundamental utilities, version | SDL, sockets, protocol parsing |
| `hlclient_platform` | SDL lifetime, windows, events/input, GL context, clocks | game protocol or world state |
| `hlclient_filesystem` | safe base/game path discovery and asset-facing I/O foundations | Steam discovery policy embedded in render code |
| `hlclient_network` | address values, Winsock lifetime, nonblocking datagram transport | GoldSrc message meaning |
| `hlclient_goldsrc` | byte readers/writers, connectionless envelope, strict info strings, stateless challenge/connect codecs and opaque auth value | sockets, retries, files, logging, OpenGL, UI |
| `hlclient_goldsrc_client` | challenge exchange, one-shot connect stage/coordinator, retry/timeout and endpoint policy | auth generation, response semantics, OpenGL, SDL, world/render state |
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

The current path remains intentionally smaller than a connection:

```text
--connect IPv4:port
    -> UdpDatagramTransport
    -> ChallengeExchange
    -> getchallenge request / strict challenge response
    -> default terminal M1 success
    -> optional ConnectRequestStage on the same transport/socket
    -> one connect send -> terminal M2.1 success/error
```

The explicit M2.1 path constructs the captured request but does not generate
authentication, interpret acceptance/rejection, allocate a netchan, sequence
packets, or update sign-on/world state. Challenge traces use bounded previews;
connect traces are metadata-only and never contain the raw packet or auth data.

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
4. validate optional M2.1 settings/auth locally, then create one nonblocking UDP
   transport and the M1/M2.1 coordinator for the validated endpoint;
5. select the built-in OpenGL or null renderer;
6. for OpenGL only, initialize SDL and create the window/context before the
   renderer;
7. poll events where applicable, advance the handshake coordinator, and update a
   scene source;
8. derive `RenderScene` from its `ClientWorldState`, render, and present;
9. stop after the configured terminal challenge/connect-request outcome, then shut down
   renderer resources before their platform dependencies.

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
