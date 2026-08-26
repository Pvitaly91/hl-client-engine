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
                         +-> validated CPU assets -> WorldRenderPackage
hl.exe bridge source ----/                              |
test/offline source ------------------------------------+-> WorldSceneRenderPackage
                                                           + visibility/draw list
                                                                    |
                                                            ClientWorldState
                                                                    |
                                                               RenderScene
                                                                               |
                                                                         IRenderer
                                                                        /         \
                                                                    OpenGL       Null
```

Each arrow is a translation boundary:

1. `hlclient_network` receives bytes without interpreting GoldSrc semantics.
2. `hlclient_goldsrc` bounds-checks and decodes those bytes into protocol values.
3. Format-specific importers produce validated owning CPU assets; the pure
   builder translates complete world assets to an immutable neutral package.
4. A concrete `IClientSceneSource` applies accepted input or a shared package
   to `ClientWorldState`, an engine-owned representation of client-visible
   state.
5. `hlclient_scene_api` derives an immutable or frame-local `RenderScene`.
6. An `IRenderer` backend consumes only `RenderScene` and renderer resources.

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
M3.2.2 retains that same validated local environment after the response, strictly
correlates inventory/list metadata, selects the exact ServerInfo map entry, and
publishes an immutable metadata-only manifest with type-local sparse slots. The
M3.2.3 continuation opens only the selected world locator through the retained
environment, validates a bounded same-handle byte source, and runs pure,
deterministic importer selection before any parser or renderer boundary. M4.1
composes the filesystem-free `hlclient_goldsrc_bsp` importer after that source
boundary, validates BSP v30 transactionally, and publishes neutral owning CPU
world geometry for model 0 without texture pixels or renderer resources. M4.2
retains the same approved BSP bytes, extracts only used physical texture
records, decodes embedded indexed miptex data, parses inert first-entity WAD
metadata to safe basenames, opens required WAD3 sources through the retained
local environment, and publishes a neutral owning texture set with exact
material bindings. Missing assets may yield a typed incomplete set; malformed
bytes fail transactionally. M4.3 then decodes the retained BSP RGB lighting
lump into deterministic four-layer padded atlases and transfers the complete
textured world plus lightmaps into an immutable renderer-neutral
`WorldRenderPackage`. Its CPU-only stage finalizes the retained network and
authentication lifetime once; only afterward may a bounds-derived local scene
upload the package to OpenGL. M4.4 consumes the same canonical BSP document to
build an owning world-spatial/PVS package, optional static initial brush
resources and instances, and a renderer-neutral scene package. Per-frame CPU
PVS/frustum resolution publishes only indices, commands, bounded statistics,
and a visibility revision; it does not alter scene resource identity or return
to parser/network/filesystem layers. The inert entity document is parsed at
most once for requested brush/spawn metadata and is not retained by the scene.
M4.4.1 corrects the earlier synthetic-only geometry assumption inside the BSP
CPU boundary: one `GoldSrcFaceGeometryBuilder` now validates the evidenced
Valve qbsp clockwise wire, performs bounded collinear T-junction cleanup, and
publishes canonical counter-clockwise faces for both world and brush models.
No texture, lightmap, scene, visibility, or renderer component has an alternate
winding path. No post-manifest semantic packet or runtime entity behavior is
added. The sign-on target still sees only the path-free provider API.
Custom/player-resource
grammar, server-info second-client slot evidence, snapshots, movement
application, and commands remain future increments behind the same boundaries.
The future bridge adapts
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
| `hlclient_local_resources` | explicit validated local environment/search roots, byte-exact virtual names, path-safe locators, exact-root verified reopen, Win32 read-only handle sandbox, final-handle containment, stable equality-only identity, bounded resolution and streaming inspection | sockets, server messages, downloads, cache/assets, renderer |
| `hlclient_resource_consistency_api` | path-free bounded provider requirements, move-only asynchronous operation/session/material ownership, cancellation, and private opaque-material handoff | filesystem/path policy, local lookup, checksum calculation, sockets, GoldSrc list types, assets, renderer |
| `hlclient_resource_consistency_local` | pre-network preparation of the fixed `tempdecal.wad` compatibility target and one-shot nonblocking provider operation | server-derived paths, response codec/layout, network creation, writes, downloads, cache, assets |
| `hlclient_goldsrc_signon` | exact fixed initial/transition requests, strict `BZ2\0` decoding, owning immutable sign-on/list/response states, historical neutral opcode-43 and zero-TX resource-list stop, exact standard list and neutral 41-byte opcode-5 codecs, carrier/tail separation, provider-required response stage, same-driver semantic-once lifecycle, and next-payload opcode boundary | arbitrary commands, custom/player-resource bodies, production consistency material, resource resolution, runtime application, command execution, filesystem, renderer, SDL, assets, world state |
| `hlclient_goldsrc_delta_values` | immutable schema-aligned generic values, bounded transactional synthetic-neutral runtime-mask/scalar mechanics, and fail-before-read stock evidence boundary | schema reparsing, native/HLSDK struct writes, stock mask claims, wall clock, filesystem, assets, renderer |
| `hlclient_goldsrc_entity_snapshots` | immutable generic baseline/full/delta/history state, explicit add/remove and exact-base mechanics, strict ordering, bounded retention, and sealed synthetic-neutral builders | stock entity wire/opcode claims, clientdata prediction, model binding, filesystem, renderer |
| `hlclient_goldsrc_post_resource_signon` | exact unconsumed post-response cursor, bounded metadata transcript, typed request evidence gate, private same-driver/source-payload continuation, and a sealed four-control-fixture synthetic stage that publishes typed baseline/full/delta state for fake-HLDS tests | arbitrary commands/raw injection, stock request invention, stock entity-body decoding without evidence, opcode scanning, stufftext execution, assets, entity rendering |
| `hlclient_goldsrc_local_resources` | evidence-gated resource-type/name classification and ordered metadata-only `LocalResourceInventoryState` adapter | sign-on transport, readiness/precache decisions, downloads/cache, file contents, asset loading |
| `hlclient_goldsrc_resource_readiness` | strict list/inventory correlation, per-entry readiness and aggregate impact, exact map selection, immutable ordered manifest, and bounded type-local sparse slots | downloads/cache, file-content parsing, assets, renderer, OpenGL |
| `hlclient_local_asset_source` | incremental exact-root locator reopen, bounded same-handle read, exact EOF and final-snapshot validation, owning `AssetSource` publication | GoldSrc types, importer selection, path fallback, renderer, network |
| `hlclient_asset_dispatch` | pointer-free pure importer probes, deterministic typed/cross-category selection, and owning imported-asset result | filesystem, GoldSrc protocol, network, renderer, OpenGL |
| `hlclient_goldsrc_approved_asset_source_api` | network-free owning read-only capability shared by manifest approval and offline visual composition | source approval policy, network/sign-on stages, importer selection, renderer |
| `hlclient_model_asset_api` | format-neutral owning skeletal model metadata layered additively over the legacy model asset | GoldSrc wire names, playback state, entity binding, renderer/GPU state |
| `hlclient_goldsrc_indexed_texture` | shared strict miptex grammar, four indexed mip/palette ranges, incremental RGBA8 conversion, masked-index-255 alpha, and neutral texture assets | BSP/WAD container parsing, filesystem, network, SDL, OpenGL, renderer/GPU work |
| `hlclient_goldsrc_studio_model` | strict CPU-only IDST/IDSQ v10 parsing, skeletal geometry/textures/animations, and deterministic importer probing | filesystem/environment access, entity binding, playback, renderer/GPU work, network |
| `hlclient_goldsrc_sprite` | strict CPU-only IDSP v2 palette/frame/group parsing and owning indexed/RGBA sprite import | filesystem/environment access, billboarding/blending, renderer/GPU work, network |
| `hlclient_goldsrc_bsp` | strict BSP v30 byte grammar, structural validation, shared world/submodel `GoldSrcFaceGeometryBuilder`, evidenced qbsp-clockwise to renderer-counter-clockwise conversion, bounded collinear compatibility and diagnostics, exact texture ordinals, owning spatial-source records/entity bytes, used physical texture-source ranges, canonical inert entity parsing, and owning CPU assets | filesystem/local-resource APIs, WAD opening/catalogs, network/sign-on, `AssetManager`, SDL, OpenGL, renderer/GPU work |
| `hlclient_goldsrc_builtin_importers` | the single caller-owned production composition that registers BSP, Studio, and sprite importers exactly once | parser implementation, global registries/state, filesystem, network/sign-on, renderer/GPU work |
| `hlclient_goldsrc_wad3` | strict bounded WAD3 header/directory catalog, uncompressed type-`0x43` lookup, normalized duplicate rejection, and shared miptex adapter | filesystem/local-resource resolution, compiler-path interpretation, network, SDL, OpenGL, renderer/GPU work |
| `hlclient_goldsrc_asset_dispatch` | evidence-derived resource role/plan, approved source facade, selected-world manifest continuation, and same-session terminal dispatch state | format parsing, download/cache, renderer/GPU work, extra network messages |
| `hlclient_goldsrc_visual_asset_bundle` | canonical model-or-sprite dispatch plus transactional exact-root Studio companion composition for approved or verified local sources | network/sign-on stages, native-path input, renderer/GPU work, entity/gameplay state |
| `hlclient_entity_interpolation` | pure bounded snapshot-pair selection and renderer-neutral synthetic projection interpolation | visual-asset import/readiness, local resources, filesystem, renderer/GPU work |
| `hlclient_entity_interpolation_stage` | client-side ownership and sequencing from an immutable visual-asset stage result through pure interpolation to a coherently paired renderer-neutral entity frame | renderer/GPU calls, network mutation, filesystem I/O |
| `hlclient_goldsrc_world_texture_import` | network-free retained-BSP texture resolution, sandboxed declared-WAD source opening, incremental decode, exact material bindings, and immutable complete/incomplete texture sets | sign-on/stage state, sockets, downloads/cache, entity instantiation, lightmaps, texture effects/animation, `AssetManager`, SDL, OpenGL, renderer/GPU work |
| `hlclient_goldsrc_world_textures` | same-session terminal stage that composes asset dispatch with the CPU-only world-texture import target | texture codec duplication, SDL, OpenGL, renderer/GPU work, extra network messages after the manifest boundary |
| `hlclient_goldsrc_lightmaps` | exact face-local GoldSrc lightmap extents, RGB/style-slot decode, deterministic padded multi-page atlases, and immutable per-surface bindings | filesystem, network, SDL, OpenGL, gamma/overbright, dynamic lighting |
| `hlclient_goldsrc_world_render` | same-session texture/lightmap/package stage, bounded events and terminal publication, and exactly-once retained network/authentication cleanup | new semantic TX, SDL/OpenGL initialization, package drawing, gameplay continuation |
| `hlclient_world_render_api` | renderer-neutral world vertices, materials, batches, coordinate metadata, and immutable package contract | GoldSrc/network types, SDL, OpenGL handles, filesystem |
| `hlclient_world_render_package` | complete-texture/lightmap validation, checked UV/material/batch construction, package limits, and stable renderer resource identity/revision | parsers, native paths, SDL/OpenGL upload, network state |
| `hlclient_world_spatial` | immutable planes/nodes/leaves/PVS table, marksurface membership, and bounded point/AABB queries | BSP bytes, entity interpretation, renderer/GPU work, filesystem, network |
| `hlclient_goldsrc_spatial` | canonical BSP-record adaptation, strict bounded GoldSrc PVS RLE decode/deduplication, leaf-zero and marksurface mapping | independent BSP wire parsing, renderer calls, paths, network |
| `hlclient_world_visibility` | OpenGL-convention frustum extraction, explicit PVS/fallback resolution, exact surface/instance selection, and stable draw-list construction | BSP/PVS byte parsing, entity behavior, filesystem, network, GPU resources |
| `hlclient_world_scene_renderer` | immutable world/spatial/brush scene composition, renderer adapters, bounds/statistics, and stable resource identity/revision | paths, raw BSP/PVS/entity bytes, OpenGL handles, network state |
| `hlclient_goldsrc_brush_models` | inert bounded entity metadata, qcsg/AngleMatrix initial transforms, brush instances, shared texture/lightmap render library, spawn descriptor, and scene composition | runtime entity behavior, commands/touch/use/think, snapshots, translucent rendering, GPU calls |
| `hlclient_world_preview` | deterministic static/orbit or inert spawn diagnostic camera, per-frame visibility/draw-list composition, and neutral scene-source state | gameplay input/usercmds, runtime entity updates, network mutation, GPU calls |
| `hlclient_auth` | asynchronous provider/operation contract and move-only authentication session lifetime | file policy, Steam implementation, sockets, renderer, world state |
| `hlclient_app_support` | explicit user-file auth adapter, bounded local-file loading, and metadata-manifest CLI exit policy | discovery, caching, Steam integration, fallback search, protocol parsing |
| `hlclient_goldsrc_client` | challenge/connect coordination, same-socket bootstrap/sign-on/resource composition, selected manifest/asset/world-texture/render-package/spatial-scene continuations, and driver/auth/provider lifetime ownership through the selected terminal stop | auth or consistency-material generation, wire codec duplication, arbitrary reliable payload production, native path/handle policy, runtime application, OpenGL, SDL, render state |
| `hlclient_client` | connection-independent client world and presentation state | raw socket ownership, GL resources |
| `hlclient_asset_api` | owning asset sources, neutral CPU geometry/textures/bindings, typed importer and registry contracts | filesystem I/O, SDL, OpenGL, sockets, SDK types |
| `hlclient_asset_manager` | virtual-file reads and dispatch through typed registries | format parsing, renderer resources, caches |
| `hlclient_scene_api` | scene-source contract and world-state-to-render-scene conversion | concrete network/injection providers, graphics calls |
| `hlclient_renderer_api` | neutral render scene and renderer contract | SDL or GoldSrc headers in its public API |
| `hlclient_renderer_opengl` | OpenGL 3.3 Core world/brush scene cache/upload, built-in shaders, camera/model matrices, depth/masked visible-command drawing, historical full-world fallback, and RAII GPU ownership using GLAD | packet/format/PVS parsing, filesystem lookup, client connection state, entity/gameplay logic |
| `hlclient_renderer_null` | headless renderer lifecycle and frame statistics | SDL, OpenGL, GLAD, Winsock, SDK types |
| `hlclient_local_resource_check` | network-free, read-only diagnostic composition for an explicit user-owned root | stock process launch, path/digest output, file mutation, protocol transport |
| `hlclient_bsp_compat_check` | network-free, read-only production BSP geometry/texture/render-package/spatial-scene validation with bounded aggregate compatibility diagnostics | network, SDL/OpenGL, writes, native-path or raw-asset output, gameplay |
| `hlclient_world_texture_check` | network-free, read-only BSP/WAD texture composition for one explicit safe virtual map | stock process launch, writes, downloads/cache, renderer/GPU work, native-path or asset-byte output |
| `hlclient_goldsrc_asset_check` | network-free, read-only canonical Studio/SPR composition for one safe virtual asset with bounded aggregate diagnostics | network/stage libraries, writes, native-path or raw-asset output, rendering/entity behavior |
| `hlclient_world_viewer` | network-free, read-only BSP/WAD/lightmap/spatial/scene composition and configurable local diagnostic OpenGL preview for one safe virtual map | stock/network process launch, writes, downloads/cache, native map-path input, runtime gameplay |
| `hlclient` | composition root and frame loop | reusable subsystem implementation |
| `hlclient_tests` | deterministic unit/integration tests with local resources | public Internet or installed-game requirements |

Public dependencies should point inward toward stable project-owned contracts.
Private implementation dependencies may point outward to SDL, GLAD, OpenGL,
Winsock, or SDK headers without exposing them to unrelated consumers.

The M4.4/M4.4.1 target direction remains deliberately acyclic:

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
hlclient_goldsrc_resource_readiness
    -> hlclient_goldsrc_signon
    -> hlclient_goldsrc_local_resources
    -> hlclient_local_resources
hlclient_local_asset_source
    -> hlclient_asset_api
    -> hlclient_local_resources
hlclient_goldsrc_approved_asset_source_api
    -> hlclient_asset_api
    -> hlclient_asset_dispatch
    -> hlclient_local_asset_source
    -> hlclient_local_resources
hlclient_model_asset_api -> hlclient_asset_api -> hlclient_core
hlclient_goldsrc_studio_model
    -> hlclient_model_asset_api
    -> hlclient_asset_api
    -> hlclient_core
hlclient_goldsrc_sprite -> hlclient_asset_api -> hlclient_core
hlclient_goldsrc_indexed_texture -> hlclient_asset_api -> hlclient_core
hlclient_goldsrc_bsp
    -> hlclient_asset_api
    -> hlclient_goldsrc_indexed_texture
    -> hlclient_goldsrc_spatial
hlclient_goldsrc_builtin_importers
    -> hlclient_goldsrc_bsp
    -> hlclient_goldsrc_studio_model
    -> hlclient_goldsrc_sprite
hlclient_goldsrc_wad3
    -> hlclient_asset_api
    -> hlclient_goldsrc_indexed_texture
hlclient_asset_dispatch -> hlclient_asset_api
hlclient_goldsrc_asset_dispatch
    -> hlclient_goldsrc_approved_asset_source_api
    -> hlclient_goldsrc_resource_readiness
    -> hlclient_local_asset_source
    -> hlclient_asset_dispatch
hlclient_goldsrc_visual_asset_bundle
    -> hlclient_goldsrc_studio_model
    -> hlclient_goldsrc_sprite
    -> hlclient_goldsrc_approved_asset_source_api
    -> hlclient_local_asset_source
    -> hlclient_local_resources
    -> hlclient_asset_dispatch
hlclient_goldsrc_asset_check
    -> hlclient_goldsrc_builtin_importers
    -> hlclient_goldsrc_visual_asset_bundle
    -> hlclient_local_asset_source
    -> hlclient_local_resources
hlclient_goldsrc_signon -> hlclient_resource_consistency_api
hlclient_goldsrc_world_texture_import
    -> hlclient_goldsrc_bsp
    -> hlclient_goldsrc_wad3
    -> hlclient_local_asset_source
hlclient_goldsrc_world_textures
    -> hlclient_goldsrc_asset_dispatch
    -> hlclient_goldsrc_world_texture_import
hlclient_goldsrc_lightmaps
    -> hlclient_asset_api
hlclient_goldsrc_world_render
    -> hlclient_goldsrc_world_textures
    -> hlclient_goldsrc_lightmaps
    -> hlclient_world_render_package
hlclient_world_render_package
    -> hlclient_world_render_api
    -> hlclient_asset_api
hlclient_world_spatial -> hlclient_asset_api
hlclient_goldsrc_spatial
    -> hlclient_asset_api
    -> hlclient_world_spatial
hlclient_world_visibility
    -> hlclient_world_spatial
    -> hlclient_world_render_api
    -> hlclient_renderer_api
hlclient_world_scene_renderer
    -> hlclient_world_render_api
    -> hlclient_world_spatial
    -> hlclient_world_visibility
hlclient_goldsrc_brush_models
    -> hlclient_goldsrc_bsp
    -> hlclient_goldsrc_world_texture_import
    -> hlclient_goldsrc_lightmaps
    -> hlclient_world_render_package
    -> hlclient_world_spatial
    -> hlclient_world_scene_renderer
hlclient_world_preview
    -> hlclient_client
    -> hlclient_scene_api
    -> hlclient_world_scene_renderer
    -> hlclient_world_visibility
hlclient_renderer_opengl
    -> hlclient_renderer_api
    -> hlclient_world_render_api
    -> hlclient_world_scene_renderer
    -> hlclient_world_visibility
    -> hlclient_glad + OpenGL::GL
```

The BSP parser target remains independent of Studio and sprite. The separate
`hlclient_goldsrc_builtin_importers` composition target owns the one canonical
production registration helper, so world-only BSP consumers do not inherit
the visual importer libraries.

The offline `hlclient_world_texture_check` and `hlclient_world_viewer`
composition roots link `hlclient_goldsrc_world_texture_import`, never the
same-session `hlclient_goldsrc_world_textures` stage. The offline
`hlclient_goldsrc_asset_check` similarly links the canonical builtin registrar,
network-free visual bundle, and approved-source API seam, never
`hlclient_goldsrc_asset_dispatch`. Their
generated link closures therefore exclude `hlclient_network`, Winsock,
netchan, sign-on, resource-transition, readiness, and asset-dispatch stage
libraries. The lightmap target
likewise links only `hlclient_asset_api`; its private implementation reads the
clean-room inline BSP v30 header constants and does not acquire the BSP parser
library or any network-bearing target.

The corresponding Visual Studio folders include `Engine/Core/Hash`,
`Engine/Resources/Local`, `Engine/Resource Consistency`,
`Engine/Assets/Source`, `Engine/Assets/Dispatch`, `Engine/Assets/Models`,
`Engine/Assets/Formats/GoldSrc`, `Engine/Assets/World Textures`,
`Engine/Assets/World Render`, `Engine/Renderer/World`,
`Engine/Renderer/Visibility`, and
`Engine/Resources/GoldSrc`; offline asset diagnostics belong under
`Tools/Assets`, and the graphical diagnostic belongs under `Tools/World`.
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

`RenderScene` carries clear color, a validated Z-up camera, and an optional
static package reference plus cull/baseline-style policy. M4.4 may additionally
carry an immutable scene package and frame-local visible draw list. The
packages contain format-neutral CPU vertices, indices, materials, exact
surface ranges, base mip levels, four-layer lightmap pages, spatial metadata,
and supported static brush resources. Stable scene resource identity/revision
lets one renderer reuse the same world/brush uploads and replace them
transactionally when resources change. Visibility has a separate revision and
changes commands only; there is no global cache or per-frame upload.

The renderer boundary receives only M4.4.1 canonical counter-clockwise
triangles whose standard cross product has a strictly positive margin against
the flat face normal. The BSP layer computes that margin from a
centroid-rebased double area vector with an extent-scaled bounded tolerance;
the renderer does not invert `glFrontFace`, globally disable back-face culling,
flip normals in a shader, or retry a face with another order.

Resource handles crossing this boundary must be project-owned opaque values,
not raw OpenGL names. Asset decoding produces CPU-side formats before a
renderer-specific upload step. The OpenGL backend owns all shader, VAO, buffer,
base-texture, lightmap-array, and white-fallback names through RAII; a failed
partial upload cannot become active. Destruction order preserves the GL context
until all renderer resources are released. See
[world render package](WORLD_RENDER_PACKAGE.md) and
[world visibility](WORLD_VISIBILITY.md),
[brush-submodel rendering](BRUSH_SUBMODEL_RENDERING.md), and
[OpenGL world renderer](OPENGL_WORLD_RENDERER.md).

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
evidence, and M3.2.2 builds only local metadata readiness/manifest state on top
of it. M3.1.4 remains conditional on sufficient evidence for the next complex
server message; M4.1 through M4.3 add only the local
BSP/CPU-world/texture/lightmap/package continuation and do not consume that
later network message or transmit after manifest publication. M4.3 explicitly
releases the retained network/authentication lifetime before any unbounded
diagnostic rendering.

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
file parsing, or GPU handle in a neutral asset. M3.2.3 adds a separate approved
path for one manifest locator:

```text
LocalResourceLocator -> reopen_verified -> same-handle bytes
    -> ApprovedAssetSource -> AssetDispatchPlan -> typed registry
```

That path never reopens `AssetSource::virtual_path()` and never calls
`AssetManager`. Pure probes preserve confidence-before-priority selection.
M4.1 production composition registers one `goldsrc-bsp-v30` world importer;
explicit empty registries retain the typed no-importer test boundary. Its
filesystem-free target publishes CPU geometry only and does not feed a
renderer.

M4.2 adds a later path that deliberately keeps container parsing separate from
filesystem authority:

```text
retained approved BSP bytes + WorldAsset
    -> used BSP miptex source ranges
    -> embedded RGBA textures
    -> inert worldspawn safe WAD basenames
    -> LocalResourceEnvironment resolver
    -> exact-root verified WAD AssetSource
    -> WAD3 catalog + shared miptex decoder
    -> immutable WorldTextureSet
```

Compiler-recorded prefixes are discarded before local resolution. The existing
resolver alone chooses the ordered game-before-`valve` root, and every found
WAD is rebound to an exact-root locator before the approved source opener reads
it. At most one WAD source is live. The texture catalog and decoder never gain
a native path, and `AssetManager` is not used. Earlier asset/geometry routes do
not enter this path. See
[world texture resolution](WORLD_TEXTURE_RESOLUTION.md).

M4.3 consumes that owning state without introducing new filesystem authority:

```text
retained approved BSP bytes + complete TexturedWorldAsset
    -> GoldSrcWorldLightmapImporter -> WorldLightmapSet
    -> WorldRenderPackageBuilder -> immutable WorldRenderPackage
    -> ClientWorldState -> RenderScene -> IRenderer
```

The lighting importer receives a byte span, never a path. Package publication
transfers local ownership once and then the stage finalizes the retained
driver/authentication lifetime. The offline viewer begins at a caller-supplied
safe virtual map name and follows the same resolver/source/importer chain with
no network and no writes. See [GoldSrc world lightmaps](GOLDSRC_LIGHTMAPS.md),
[world render package](WORLD_RENDER_PACKAGE.md), and
[offline world viewer](WORLD_VIEWER.md).

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

M4.4.1 uses the pinned public compiler sources only to establish the signed
surfedge endpoint rule, the `face.side` plane-normal rule, Valve's reversed
cross-product winding convention, mirrored-face reversal, and T-junction point
emission. The project-owned builder and its centroid-rebased arithmetic remain
independently authored. Exact references, the fixed `0.02` planarity limit,
the bounded area formula, and the float-scale strictly-interior collinear gate
are documented in
[GoldSrc BSP geometry compatibility](GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.md).

## Composition and lifetime

`hlclient` is the composition root. A normal standalone frame follows this
order:

1. parse command-line inputs and validate paths/endpoints;
2. create the caller-owned `AssetImporterRegistries`;
3. explicitly register the compiled-in GoldSrc BSP v30 world importer before
   any dispatcher, stage, or `AssetManager` can borrow that registry;
4. independently create the asset-facing filesystem and `AssetManager` only
   when that existing asset path is requested; the approved manifest route
   borrows the registries but never feeds its path through `AssetManager`;
5. when a later stop point is explicit, acquire bounded authentication material
   through the configured provider and retain its optional session lifetime;
6. only for response/server-baselines/entity-snapshot/manifest/asset-dispatch/
   world-geometry/world-textures/world-render-package/world-spatial-scene/
   view-world routes with explicit local selection,
   validate `--basedir`/`--game`, prepare fixed-target consistency material
   through one sandboxed handle, close the file, and retain the one-shot
   provider plus its validated environment;
7. create one nonblocking UDP transport and the M1/M2 coordinator for the
   validated endpoint;
8. for an explicit asset-dispatch, world-geometry, world-textures,
   world-render-package, or world-spatial-scene stop,
   advance that retained coordinator headlessly until the selected-world source
   is dispatched; the texture route additionally retains the BSP bytes/world,
   opens required declared WADs one at a time, and publishes the owning texture
   set; the package route also imports RGB lightmaps and builds one immutable
   package; the spatial-scene route additionally builds canonical spatial/PVS
   state and any explicitly selected static initial brush scene; print the
   requested bounded result, release the optional lifetime
   exactly once, and return before selecting or initializing any renderer;
   the separate `server-baselines` and `entity-snapshot` diagnostics likewise
   advance the retained network coordinator headlessly and return before any
   renderer, asset importer, BSP, or WAD work (the stock evidence-pending
   profile stops at the exact unconsumed body and queues no request);
9. for `--view-world`, build the same scene headlessly, resolve the requested
   initial visibility/draw list, finalize retained driver/authentication state
   exactly once, and only then initialize SDL, an
   OpenGL context, and the local bounds-derived preview; this preview is not a
   connected gameplay session;
10. for ordinary rendering routes, select the built-in OpenGL or null renderer,
    initializing SDL/window/context before OpenGL only;
11. poll events where applicable, advance the handshake coordinator, and let
    the selected stage-owned driver process the same socket until the requested
    opaque, initial-sign-on, pre-resource, delta-schema, movement-environment,
    user-info, neutral opcode-43, standard resource-list/response boundary, or
    metadata-only precache-manifest boundary is acknowledged and published;
12. derive `RenderScene` from its `ClientWorldState`, render, and present until
    the configured terminal challenge/connect-request/connect-response/
    netchan-bootstrap/signon-boundary/pre-resource/delta-schemas/movevars/
    user-info/resource-list-boundary/resource-list/resource-response-boundary/
    precache-manifest outcome, let driver terminal cleanup release its optional
    lifetime exactly once, then shut down renderer resources before their
    platform dependencies.

The standalone viewer is a separate composition root: it begins with one safe
virtual map under an explicit user-owned local environment, validates all CPU
assets and builds the scene before initializing SDL/OpenGL, performs no
network operation or write, updates only CPU camera/visibility state per frame,
and destroys renderer resources before the context. The M4.4.1 checker/wrapper
uses the same safe virtual-name and verified-open boundary without initializing
a renderer; before/after selected-file metadata and approved-root inventories
make external drift a terminal failure. Only bounded aggregate diagnostics may
be retained—never native paths, raw face arrays, map/WAD bytes, texture names,
or entity text.

Partially initialized states must unwind safely through RAII. Logging and error
messages should identify the failed boundary without exposing secrets or
turning malformed remote data into a process crash.

## CPU visual-asset import boundary

M4.5.2 adds separate targets for neutral model types, the network-free approved
source API seam, Studio v10, SPR v2, and GoldSrc visual dependency composition.
Parsers depend inward on the neutral asset API only; they cannot see a renderer,
`LocalResourceEnvironment`, network driver, or native filesystem. The outer
visual operation owns the
approved-or-verified source/environment boundary, uses the canonical
cross-category dispatcher, invokes only the selected registered importer, and
follows only a typed Studio dependency result.

```text
approved model resource
    -> verified owning source
    -> global model-or-sprite probe
    -> self-contained IDST / exact-root Studio bundle / IDSP
    -> owning ModelAsset or SpriteAsset
```

Models retain source-native bone-local geometry and compressed animation;
sprites retain indexed frames, palette, groups, intervals, and format metadata.
No output is a render instance. Entity-number/model-index association,
fractional interpolation, body/skin/sequence application, billboarding, blend
state, and OpenGL upload remain outside the importer boundary. M4.5.3 implements
them in separate entity-visual, pose, renderer-neutral scene, and OpenGL modules.
`RenderScene` sees only shared entity package/frame contracts; it never sees a
GoldSrc snapshot, local-resource locator, or native path. Stock visual projection
remains a fail-closed evidence boundary.

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
