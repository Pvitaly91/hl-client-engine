# hl-client-engine

`hl-client-engine` is an independent, clean-room implementation of a
GoldSrc-compatible game client. Its first interoperability goal is to connect
to an original Half-Life Dedicated Server (HLDS) while keeping protocol,
simulation, and rendering concerns separated enough to support a future
`hl.exe` injection bridge.

The repository has implemented M4.4.1's explicit Valve BSP-v30 geometry
compatibility profile on top of M4.4's bounded renderer-neutral spatial,
visibility, and static brush-submodel path, M4.3's first static-world rendering
path, M4.1 CPU BSP geometry, and M4.2 embedded/WAD3 RGBA textures. The
CPU continuation decodes exact GoldSrc RGB lightmap samples, retains all four
source style slots, packs deterministic padded multi-page atlases, and builds
an immutable renderer-neutral `WorldRenderPackage` with normalized base UVs,
texel-centered lightmap UVs, materials, draw batches, and exact per-surface
ranges. M4.4 adds an owning BSP spatial/PVS package, CPU PVS/frustum selection,
an immutable scene package, and optional static initial opaque brush instances.
The OpenGL 3.3 Core backend uploads scene resources transactionally while
visibility revisions change draw selection without re-upload. Both
`--stop-after world-render-package` and `--stop-after world-spatial-scene`
remain CPU-only;
`--view-world` cleans up the retained network/authentication lifetime before it
opens the local preview. The standalone `hlclient_world_viewer` performs the
same composition offline and read-only for an explicit user-owned map.

Implemented M1–M4.4.1 bounded behavior includes the Protocol 48 challenge,
captured one-shot `connect` request, strict immediate connectionless
`ACCEPT`/`REJECT`,
an explicit authentication-provider boundary, same-socket netchan bootstrap,
persistent reliable state, strict fragmentation/reassembly, the fixed `new`
request, bounded `BZ2\0` service decoding, typed server info, seven owning
delta schemas, typed movement/environment metadata, the exact opcode-13
sequence through first-batch end, one fixed nine-byte `sendres` request queued
through the retained driver, a bounded later transfer through opcode 45, and
strict parsing of the standard opcode-43 list through exact end-of-payload.
User ID, all user-info values, the fixed 16-byte opaque suffix, and the first
opcode-45 `u32` remain private. Resource names remain owning untrusted metadata:
an evidence-gated classifier must first produce a bounded virtual name, so no
server byte string becomes a native path directly.

The earlier M2.4.2 server-info second-client evidence gap remains. Signed-stock
sets separately confirm netchan/fragment behavior, the initial request, the
server-info/delta/movevars grammars, opcode-13 single/repeated profiles, request
loss/ACK/duplicate behavior, six-fragment resource-transition transfers, and
54 stable resource-list bodies and the reconstructed post-list carrier/body
grammar. The pinned public Valve SDK independently
cross-checks resource categories and fields but contains no numeric opcode-43
constant or wire serializer; the semantic gate instead also relies on exact
repeated grammar, coherent map differentials, and exact list endpoints. Live
`hlclient` to stock HLDS, slot-1/file semantics, general `svc_*` parsing, a
Steam authentication provider, custom-resource list bodies, live-stock
readiness/precache interoperability validation, resource downloads/cache,
snapshots, gameplay, and a public
raw payload/command CLI remain unavailable.
`--connect` remains challenge-only by default; later stop points are explicit.

## Reference platform

The required reference environment is:

- Windows 10 or 11;
- Visual Studio 2022 with the Desktop development with C++ workload;
- MSVC v143 and a Windows SDK;
- Visual Studio generator `Visual Studio 17 2022`;
- platform `Win32` (x86), with `Debug` as the primary configuration;
- C++20, `/W4`, and `/permissive-` for project-owned C++ targets.

x64 may become an additional target later, but it must not replace Win32. The
x86 baseline is intentional for compatibility work involving original 32-bit
GoldSrc/Steam Half-Life components and the planned `hl.exe` bridge.

## Get the source

The Valve Half-Life SDK is a pinned reference submodule, so clone recursively:

```powershell
git clone --recurse-submodules https://github.com/Pvitaly91/hl-client-engine.git
cd hl-client-engine
```

If the repository was cloned without submodules:

```powershell
git submodule update --init --recursive
```

## Generate, build, and test

The manual path is fully supported and is the acceptance-reference workflow:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DHLCLIENT_WARNINGS_AS_ERRORS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The generated solution is:

```text
<repository>\build\hl-client-engine.sln
```

For this workspace, the exact path is:

```text
D:\DEV\CPP\HL-Client-engine\build\hl-client-engine.sln
```

The equivalent preset workflow is:

```powershell
cmake --preset vs2022-win32
cmake --build --preset vs2022-win32-debug
ctest --preset vs2022-win32-debug
```

`vs2022-win32-release` and `vs2022-win32-relwithdebinfo` provide the matching
build/test presets for the other supported configurations.

Convenience preset names are also available:

```powershell
cmake --preset win32-debug
cmake --build --preset win32-debug
ctest --preset win32-debug
```

All presets retain Visual Studio's multi-configuration model; none sets
`CMAKE_BUILD_TYPE`. `Debug`, `Release`, and `RelWithDebInfo` remain normal
solution configurations.

## Run and debug

Open `build\hl-client-engine.sln`, select **Win32 / Debug**, and press **F5**.
`hlclient` is generated as the startup project and its debugger working
directory is the repository root, not `build\Debug`. CMake copies the matching
SDL3 runtime DLL beside `build\bin\Debug\hlclient.exe` whenever `hlclient` is
built, so no manual DLL copy is needed. GLAD is linked statically and OpenGL is
supplied by
Windows and the graphics driver.

The bootstrap can also run without Half-Life assets:

```powershell
.\build\bin\Debug\hlclient.exe
.\build\bin\Debug\hlclient.exe --help
.\build\bin\Debug\hlclient.exe --version
.\build\bin\Debug\hlclient.exe --renderer null
```

`--renderer opengl` is the default and retains the interactive SDL window.
`--renderer null` runs a bounded headless frame without creating SDL, a window,
an OpenGL context, or a GPU resource. `HLCLIENT_SMOKE_TEST_FRAMES` can set an
explicit bounded frame count for either backend during smoke testing.

To validate a user-owned Half-Life installation:

```powershell
.\build\bin\Debug\hlclient.exe --basedir "C:\Games\Half-Life" --game valve
```

To perform the M1 challenge-only exchange and print bounded, escaped packet
diagnostics:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null --connect 127.0.0.1:27015 --net-trace
```

This sends `getchallenge steam`, accepts a valid response only from the exact
requested IPv4 endpoint, reports the owned challenge, and stops. It never sends
the subsequent GoldSrc `connect` request and does not create a netchan or enter
sign-on. The GoldSrc-style spelling `+connect <IPv4:port>` is also accepted.

To send the M2.1 request once, provide an explicit local 245-byte authentication
boundary. Its contents are never logged and must not be committed:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27015 --stop-after connect-request `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

A successful exit at this stop point proves transmission only, not server
acceptance. The explicit file provider does not generate auth material, and
M2.1 does not start netchan/sign-on.

`--auth-provider file` is the recommended spelling and the only supported
provider selection. The older `connect-request` and `connect-response` form
with only `--auth-material-file` remains accepted for command-line
compatibility. There is no `none`, `steam`, or `bypass` provider.

To wait for and strictly decode the immediate M2.2 connectionless `ACCEPT` or
`REJECT`, select the response stop point:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27015 --stop-after connect-response `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

The wait reuses the same UDP transport, accepts a result only from the exact
server endpoint, defaults to a five-second deadline, and exits after the typed
response. Acceptance exits successfully but does not create a netchan or enter
sign-on; rejection, timeout, malformed response, and network failure exit
nonzero. Rejection text is escaped and presentation-capped before logging.

The M2.3.1 runtime stop point remains:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27015 --stop-after netchan-bootstrap `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

The original M2.3.1 unfragmented bootstrap branch remains validated end to end
against a deterministic local fake HLDS. That fake sends the first server
sequenced datagram after `ACCEPT`; the project waits on the same UDP socket,
obtains one complete opaque payload, and emits exactly one minimal transport
acknowledgement. M2.3.3 extends the same stop to the reassembled fragment branch
described below.

M2.3.2 extends the transport-independent `NetchanSession` with bounded pending
and in-flight reliable bytes, acknowledgement-gap retransmission, and atomic
prepare/send/commit state. A deterministic fake-HLDS test reuses the same UDP
transport, source endpoint, and coordinator-owned session after the full
bootstrap; it proves one canonical outgoing send/covering-ACK clear with no
extra transmission and one owning incoming reliable marker with the correct ACK
bit plus duplicate/older delivery once. The runtime command above still
terminates at the bootstrap boundary: it does not expose arbitrary reliable
bytes or interpret sign-on content.

M2.3.3 makes `NetchanDriver` the reusable same-transport polling and timeout
owner. The netchan bootstrap stage/coordinator constructs and owns it through
the selected stop point: an unfragmented first payload completes directly,
while supported slot-0 fragments are admitted, ACKed per fragment, and
reassembled before the first owning payload completes the stop. The CLI still
terminates at `--stop-after netchan-bootstrap`; it does not interpret or expose
arbitrary payload bytes or continue into sign-on.

An embedding owner can construct the persistent driver with the already-bound
`IDatagramTransport`, exact endpoints, monotonic time, and an optional opaque
connection-lifetime guard. Terminal paths clear reliable, fragment, and
one-shot unreliable state and release the guard exactly once. A lower-level
caller that uses `NetchanSession` without the driver must still call
`clear_reliable_state()` on terminal failure. This project path is covered by
deterministic/fake-HLDS tests; live project-to-stock fragmentation remains
pending.

Stock capture established a client-first post-`ACCEPT` order. The stock
client's first reliable semantic bytes are exactly `03 6E 65 77 00`; the
transport appends three `01` minimum-padding bytes. M2.4.1 reproduces only this
typed fixed request, never an arbitrary string command. Earlier M2.3.1/M2.3.2
compatibility fixtures remain unchanged at their own stop points.

The new explicit runtime stop is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after signon-boundary `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

After `ACCEPT`, the same socket and authentication lifetime pass directly to
`InitialSignonStage`. It queues the fixed request once, relies on persistent
netchan retransmission/ACK state, reassembles the first normal stream, strictly
decodes its `BZ2\0` envelope, parses captured opcode 8 as one bounded owning
NUL string, and exits successfully at opcode 11/offset 42 without consuming its
body. No resource/spawn continuation is sent, and server text is not executed
or printed raw.

The explicit typed continuation is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after pre-resource `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

This route retains the same driver and authentication lifetime, parses the
variable opcode-11 server-info body transactionally, consumes only the exact
empty-string/zero opcode-54 control, and stops successfully at the confirmed
opcode-14 category-C boundary with its body untouched. It sends no `sendres`
or other resource command. Server game/label/map strings remain untrusted
owning metadata and never reach filesystem, assets, world state, or a renderer.
The bounded success log sanitizes only confirmed game/map metadata and omits
the server label and opaque fields. The stock second-client evidence gap leaves the offset-29 candidate
private, so M2.4.2 is not described as fully evidence-complete.

The explicit delta-schema continuation is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after delta-schemas `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

It retains that same driver/payload/authentication lifetime, decodes all seven
opcode-14 messages transactionally into an immutable ordered registry, and
stops at numeric opcode 44 without consuming its body or sending a resource
response. Opcode 44 deliberately remains `PostDeltaBoundary`: available
evidence at the M2.4.3 layer did not yet establish its semantic. See
[GoldSrc delta descriptions](docs/GOLDSRC_DELTA_DESCRIPTIONS.md).

The explicit M2.4.4 movement-environment continuation is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after movevars `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

It reuses that exact retained socket, driver, payload, endpoint, and
authentication lifetime. Opcode 44 is decoded field-by-field as the confirmed
movement/environment metadata profile; the stage then consumes only the
confirmed opcodes 32, 5, 39, and 9 at the returned cursor and stops before the
stock-observed opcode-13 body. It neither applies the values to runtime
movement/rendering nor sends `sendres` or any resource response. See
[GoldSrc movement-environment state](docs/GOLDSRC_MOVEVARS.md).

The M3.1.1 user-info stop continues over that same retained first payload:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after user-info `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

It parses one or more exact opcode-13 messages into owning private-value
metadata, requires exact end of the first service batch, and sends nothing.
The public surface includes zero-based client index plus safe key presence/
length metadata; user ID, info values, unknown/protected keys, and the opaque
16-byte suffix have no raw getter. See
[GoldSrc opcode-13 user info](docs/GOLDSRC_USERINFO.md).

The optional transition stop is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after resource-list-boundary `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

Only this route retains the same driver after first-batch completion, queues
the fixed `03 73 65 6E 64 72 65 73 00` request once, delegates retry and
covering-ACK recognition to the driver, decodes the bounded later `BZ2\0`
transfer, consumes opcode 45 plus its eight-byte body, and stops with the exact
next opcode 43 unconsumed. The stop-point spelling does not assert resource-list
semantics; no opcode-43 body byte is parsed and no response/filesystem action
occurs. See [GoldSrc resource transition](docs/GOLDSRC_RESOURCE_TRANSITION.md).

The M3.1.2 standard resource-list stop continues from that historical
boundary:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after resource-list `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

It decodes opcode 43 at the exact retained cursor into an ordered owning list,
requires the exact terminal zero fill and end-of-payload, and publishes a
metadata-only required-response boundary. It sends no post-list response and
performs no path normalization, filesystem access, download, cache, precache,
asset, or renderer action. Custom/player-resource flag profiles fail closed as
typed unsupported. See
[GoldSrc opcode-43 resource list](docs/GOLDSRC_RESOURCE_LIST.md).

The response continuation without a consistency provider is selected with:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after resource-response-boundary `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

`ResourceListStage` and `--stop-after resource-list` retain their historical
M3.1.2 semantics: `response_queue_count() == 0`, no response is queued or sent,
and no provider is consulted. `ResourceClientResponseStage` is a distinct
continuation. It requires path-free typed consistency material, builds and
queues the 41-byte semantic unit exactly once, lets the retained driver own
retransmission and covering-ACK handling, and publishes only the first opcode
of the next complete server payload with its complex body unconsumed. The
command above intentionally terminates as `provider_required` before TX.

M3.2.1 opts into the production local implementation explicitly:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after resource-response-boundary `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve --net-trace
```

`--basedir` is required for the local provider and `--game` defaults to
`valve`. For a mod, the validated game root is searched before the `valve`
fallback. There is no current-directory, registry, Steam-library, environment,
repository, or build-tree discovery. The provider resolves only the
profile-fixed `tempdecal.wad`; no server name or CLI option can substitute a
different target. Root validation and one-handle streaming preparation complete
before network initialization, so provider preparation failures send zero
packets. Earlier stop points perform no provider filesystem work.

M3.2.2 continues that exact session into the metadata-only manifest stop:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after precache-manifest `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve --net-trace
```

It reuses the same validated roots, strictly correlates list/inventory metadata,
selects the exact ServerInfo model resource, and builds sparse type-local slot
tables. It sends no packet after the existing response boundary and neither
opens asset contents nor downloads, caches, parses, or renders a resource.

M3.2.3 adds the explicit approved-source and importer boundary:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after asset-dispatch `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve --net-trace
```

This continues the same retained socket, driver, endpoint, authentication
lifetime, manifest, and validated root environment. It opens only the selected
world source through `reopen_verified()`, validates exact EOF and a final
same-handle metadata snapshot, and runs deterministic importer probing. A valid
version-30 BSP is now imported into an owning CPU `WorldAsset`; explicit empty
test registries retain the historical no-importer boundary. Missing/stale
sources and ambiguous or malformed imports exit nonzero. Unsupported/no-match
input remains a successful diagnostic boundary for `asset-dispatch`, while the
stricter `world-geometry` stop requires an imported CPU world and exits nonzero.
No asset path is reopened, no dependent resource is loaded, and no renderer or
GPU work occurs.

M4.1 adds a stricter CPU-geometry stop on that same route:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after world-geometry `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve --net-trace
```

The BSP parser accepts version 30 only, validates the exact 124-byte header and
all 15 lumps with explicit little-endian reads, cross-checks every retained
reference, reconstructs closed convex world-model face loops, and emits flat
normals, deterministic fan indices, raw texel-unit S/T, material-reference
metadata, surfaces, and finite bounds. Only model 0 geometry is emitted by
this historical M4.1 result. Entity interpretation, PVS decoding, collision
runtime, brush-submodel instances, embedded/WAD
texture pixels, palettes, lightmaps, and renderer resources remain absent.

M4.2 adds a later texture-resolution stop on the same retained composition:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after world-textures `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve --net-trace
```

Only BSP physical texture records used by model 0 materials enter M4.2 miptex
parsing. Duplicate directory offsets alias one physical record; missing BSP
entries are never guessed by name. The first entity is parsed as inert quoted
metadata only, and
compiler WAD prefixes are discarded permanently. Required `.wad` basenames are
searched in declaration order through the existing safe local environment.
WAD3 accepts the exact 12-byte header, bounded 32-byte directory entries,
uncompressed type `0x43` miptex records, unique ASCII-insensitive miptex names,
and exact BSP name/dimension matches. The shared decoder validates four indexed
mips and a 256-color palette, then incrementally emits four owning RGBA8 levels;
names beginning with `{` make palette index 255 transparent without replacing
its RGB.

Missing BSP references or WAD lists/archives/textures and exact-dimension
mismatches publish typed incomplete material bindings, and the CLI exits
nonzero. Unsafe resolution, malformed WAD3/miptex bytes, unsupported
compression, cancellation, or timeout publish no partial texture set.
Embedded-only worlds open no WAD. No texture animation, water/sky effects,
lightmaps, renderer material, OpenGL upload, or other GPU work occurs.

Normal runtime may read an explicitly supplied user-owned Steam installation
with zero writes and no stock process launch. Active stock `hl.exe`/HLDS
research remains restricted to an isolated marked copy. The local provider and
deterministic fake-HLDS tests are not a claim of completed stock
interoperability or manual installation validation.

For a user-owned read-only map check with the optional offline checker, use
`scripts/verify_local_bsp_import.ps1 -ToolPath <checker.exe> -Basedir <root>
-Game valve -Map maps/<name>.bsp`. The wrapper runs the checker twice, compares
deterministic summaries, and fails on target-content, size, write-time, or
created/deleted-file drift while printing metadata only. At the historical
M4.1 boundary this was not a stock-run claim; the later M4.4.1 compatibility
acceptance is documented below.

The optional M4.2 network-free checker can verify a complete local texture set:

```powershell
.\scripts\verify_local_world_textures.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_world_texture_check.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" `
  -Game valve -Map maps/<name>.bsp
```

Its wrapper snapshots the selected map, root-level WAD files, and both approved
search-root inventories, runs the checker twice, requires identical summaries,
and rejects content/metadata or created/deleted-file drift. It prints a summary
digest and bounded counts, not paths or asset bytes. This historical M4.2
section predates the M4.4.1 user-owned read-only acceptance documented below.

M4.3 adds the CPU render-package stop without changing any earlier stop:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after world-render-package `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve --net-trace
```

It validates complete base textures, decodes three-byte RGB lightmap samples,
retains up to four ordered source style layers, builds deterministic
one-pixel-padded atlases, and publishes one immutable package. Baseline
rendering selects style slot 0; there is no gamma/overbright conversion or
dynamic style blending. This stop creates no SDL window, OpenGL context, or GPU
resource and sends no new semantic network message.

The graphical continuation uses the same CPU package, but finalizes the
retained network/driver/authentication lifetime before starting a local
diagnostic preview:

```powershell
$env:HLCLIENT_SMOKE_TEST_FRAMES = "2"
.\build\bin\Debug\hlclient.exe --renderer opengl --view-world `
  --connect 127.0.0.1:27128 `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve
Remove-Item Env:HLCLIENT_SMOKE_TEST_FRAMES
```

`--view-world` is intentionally not a gameplay connection and rejects the null
renderer. Its historical defaults show static model 0 with base textures,
baseline lightmaps, masked alpha, depth testing, and a double-sided bounds
preview. M4.4 options can explicitly enable PVS/frustum culling, supported
static initial opaque brush instances, and an inert diagnostic spawn pose.
Runtime entities, dynamic brush motion, nonzero/translucent rendermodes,
dynamic lights/styles, animated water, and sky remain unavailable.

For a completely offline, read-only user-owned map preview:

```powershell
.\build\bin\Debug\hlclient_world_viewer.exe `
  --basedir "<Half-Life-root>" `
  --game valve --map maps/<name>.bsp `
  --camera spawn --visibility pvs-frustum --brush-submodels static `
  --cull back
```

The viewer accepts a safe virtual map name, not a native map path; it starts no
network or stock process and writes no game data. See the
[viewer contract](docs/WORLD_VIEWER.md) for bounded verification usage.

For the same M4.4 scene composition without SDL, OpenGL, or GPU resources:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after world-spatial-scene `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve `
  --visibility pvs-frustum --brush-submodels static
```

Visibility accepts `all`, `frustum`, `pvs`, or `pvs-frustum`; brush submodels
accept `off` or `static`; cameras accept `static`, `orbit`, or `spawn`.
Viewer culling accepts `none` or `back`; historical viewer defaults remain
`all`, `off`, `static`, and `none`.

M4.4.1 names the default BSP-v30 face rule
`valve_qbsp_clockwise_wire_to_counter_clockwise_render`. Signed surfedges first
reconstruct the Valve QBSP clockwise wire relative to the side-adjusted face
normal. The shared world/brush geometry builder then publishes deterministic
counter-clockwise renderer geometry; it does not accept both orientations or
invert winding in OpenGL.

The network-free CPU checker can validate one safe virtual map through any
bounded pipeline boundary, including the complete spatial scene:

```powershell
.\build\bin\Debug\hlclient_bsp_compat_check.exe `
  --basedir "<Half-Life-root>" --game valve `
  --map maps/<name>.bsp --validate-through spatial-scene
```

For multiple user-selected maps, the read-only wrapper snapshots the BSPs,
relevant WADs, and file inventories, runs every CPU check twice, and requires
identical metadata plus `external-file-drift=none`:

```powershell
.\scripts\verify_stock_bsp_geometry_compatibility.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_bsp_compat_check.exe `
  -Basedir "<Half-Life-root>" -Game valve `
  -Maps @("maps/<name>.bsp")
```

Run the CPU verifier before the optional OpenGL proof. On an OpenGL 3.3
Core-capable desktop, invoke `verify_local_world_render.ps1` with `-Frames 2`,
`-Visibility pvs-frustum`, `-BrushSubmodels static`, `-Camera spawn`, and
`-CullModes @('none','back')`; the wrapper now selects that dual set by default.
It requires one world/scene upload per run, two rendered frames, non-clear
pixels, nonzero draw/triangle counts, `gl-error=none`, and no file drift. A
host without the required context may skip only this graphical proof; the CPU
compatibility verifier remains required.

See [GoldSrc post-resource client response](docs/GOLDSRC_RESOURCE_CLIENT_RESPONSE.md)
and [resource-consistency provider boundary](docs/RESOURCE_CONSISTENCY_PROVIDER.md),
[local resource resolution](docs/LOCAL_RESOURCE_RESOLUTION.md), and the
[local consistency provider](docs/LOCAL_RESOURCE_CONSISTENCY_PROVIDER.md),
[local readiness](docs/LOCAL_RESOURCE_READINESS.md), the
[metadata-only precache manifest](docs/PRECACHE_MANIFEST.md),
[approved asset sources](docs/APPROVED_ASSET_SOURCE.md), and
[asset importer dispatch](docs/ASSET_IMPORTER_DISPATCH.md), the
[GoldSrc BSP v30 profile](docs/GOLDSRC_BSP_V30.md), and
[CPU world geometry](docs/CPU_WORLD_GEOMETRY.md), the
[GoldSrc indexed-miptex profile](docs/GOLDSRC_INDEXED_TEXTURE.md),
[GoldSrc WAD3 profile](docs/GOLDSRC_WAD3.md), and
[world texture resolution](docs/WORLD_TEXTURE_RESOLUTION.md),
[GoldSrc world lightmaps](docs/GOLDSRC_LIGHTMAPS.md), the
[world render package](docs/WORLD_RENDER_PACKAGE.md), the
[GoldSrc BSP spatial package](docs/GOLDSRC_BSP_SPATIAL.md),
[GoldSrc PVS profile](docs/GOLDSRC_PVS.md),
[world visibility](docs/WORLD_VISIBILITY.md),
[GoldSrc brush submodels](docs/GOLDSRC_BRUSH_SUBMODELS.md),
[brush-submodel rendering](docs/BRUSH_SUBMODEL_RENDERING.md), the
[stock BSP geometry compatibility profile](docs/GOLDSRC_BSP_GEOMETRY_COMPATIBILITY.md),
[OpenGL world renderer](docs/OPENGL_WORLD_RENDERER.md), and the
[offline world viewer](docs/WORLD_VIEWER.md).

The captured stock request and response layouts were discovered with
unmodified stock components and bounded, sanitized relay observations. The
project client exercises request plus accept/reject behavior against
deterministic local fake HLDS tests. A separate `hlclient` -> stock HLDS
acceptance proof has not been performed and is not claimed.

For an explicit manual check against a user-run original HLDS:

```powershell
.\scripts\verify_original_hlds_challenge.ps1 `
  -ClientPath .\build\bin\Debug\hlclient.exe `
  -Endpoint 127.0.0.1:27015
```

The verifier can optionally start an explicitly supplied `hlds.exe`; see
[Building](docs/BUILDING.md) for the exact form and cleanup behavior.

To reproduce the bounded stock netchan observation with user-owned stock
components and a user-supplied bounded relay:

```powershell
.\scripts\verify_stock_netchan_capture.ps1 `
  -ClientPath C:\Games\Half-Life\hl.exe `
  -HldsPath C:\Servers\Half-Life\hlds.exe `
  -RelayPath C:\Tools\bounded-netchan-relay.ps1 `
  -Game valve -Map boot_camp -Port 27128 `
  -CapturePacketCount 64 -CaptureByteLimit 1048576 -TimeoutSeconds 30
```

This opt-in script is loopback-only, bounded, and writes process/capture output
under ignored `manual-artifacts/netchan-captures/`. The local research set used
for M2.3.1 comprised six controlled stock sessions: two passive, one drop, one
duplicate, and two reorder runs. The wrapper validates relay-reported bounded
completion but neither prints payload bytes nor proves that project `hlclient`
can authenticate to stock HLDS.

The M2.3.2 reliable-state evidence has its own stricter metadata-only verifier:

```powershell
.\scripts\verify_stock_reliable_netchan_capture.ps1 `
  -RelayPath C:\Tools\bounded-reliable-netchan-relay.ps1 `
  -HalfLifePath C:\Games\Half-Life\hl.exe `
  -HldsPath C:\Servers\Half-Life\hlds.exe `
  -Game valve -Map boot_camp -Port 27320 `
  -Scenario drop-first-client-reliable -TimeoutSeconds 30
```

Its fixed scenarios are `baseline`, `drop-first-client-reliable`,
`drop-first-server-ack`, `duplicate-client-reliable`, and `delay-stale-ack`.
The M2.3.2 primary evidence set contains exactly 16 `bounded_complete` runs:
two each for baseline/two generations, drop-first reliable, the no-server-ACK
timer control, drop-first server ACK, duplicate reliable, stale-ACK replay,
drop-second distinct reliable, and drop-first-two transmissions. Three further
baseline runs exercised the verifier end to end and their summaries satisfy its
strengthened baseline action/accounting rules. The verifier accepts only a
fixed metadata schema with `raw_packet_bytes_stored=false`; no raw
capture, authentication material, identity bytes, or opaque payload bytes are
tracked. See [GoldSrc netchan](docs/GOLDSRC_NETCHAN.md) for the exact
stock-confirmed semantics and the separately labeled project policies.

The M2.3.3 fragment research has a separate metadata-only verifier:

```powershell
.\scripts\verify_stock_netchan_fragments.ps1 `
  -RelayPath C:\Tools\bounded-fragment-relay.ps1 `
  -HalfLifePath C:\Games\Half-Life\hl.exe `
  -HldsPath C:\Servers\Half-Life\hlds.exe `
  -Game valve -Map boot_camp -Port 27420 `
  -Scenario drop-middle-fragment -TimeoutSeconds 45
```

The primary evidence is 12 `bounded_complete` research runs: two each for
baseline, drop-middle, exact duplicate, old-index replay after a newer packet,
drop-first, and drop-final. The reorder pair does not establish true unseen
out-of-order stock delivery. Rejected and incomplete attempts are listed
separately in [GoldSrc fragmentation](docs/GOLDSRC_FRAGMENTATION.md) and do not
count. The strengthened wrapper permits only one metadata file with
`raw_packet_bytes_stored=false`, validates scenario-specific action/accounting
and cleanup, and never turns a failed cleanup into accepted evidence.

The M2.4.1 sign-on research uses its own metadata-only wrapper:

```powershell
.\scripts\verify_stock_initial_signon.ps1 `
  -RelayPath C:\Tools\bounded-signon-relay.ps1 `
  -HalfLifePath C:\Games\Half-Life\hl.exe `
  -HldsPath C:\Servers\Half-Life\hlds.exe `
  -Game valve -Map boot_camp -Port 27520 `
  -Scenario baseline -TimeoutSeconds 40
```

Its scenario allowlist is `baseline`, `drop-initial-request`,
`drop-request-ack`, and `duplicate-server-batch`. The primary evidence contains
six accepted baselines and two accepted runs for each perturbation. The wrapper
allows exactly one whitelisted metadata document and forbids raw packets,
authentication/identity values, server text, decompressed bytes, and game data.
See [GoldSrc initial sign-on](docs/GOLDSRC_INITIAL_SIGNON.md) for the stable
request/envelope/opcode facts and the separate fake-HLDS proof.

The M2.4.2 differential research uses a separate metadata verifier:

```powershell
.\scripts\verify_stock_serverinfo.ps1 `
  -RelayPath C:\Tools\bounded-serverinfo-relay.ps1 `
  -HalfLifePath C:\Games\Half-Life\hl.exe `
  -HldsPath C:\Servers\Half-Life\hlds.exe `
  -Game valve -Map boot_camp -MaxPlayers 2 -Port 27620 `
  -Scenario baseline -TimeoutSeconds 40
```

Its allowlist covers baseline, map, maxplayers, first/second-client,
server-restart, same-process map-change, and hostname differentials. The
accepted primary set contains 16 bounded single-client runs; two second-client
attempts failed before canonical `getchallenge` and are explicitly excluded.
The wrapper accepts exactly one strict metadata document, while raw research
projections stay ignored. See [GoldSrc server info](docs/GOLDSRC_SERVERINFO.md)
for the exact field evidence table and pending semantics.

The M2.4.3 verifier projects the exact delta stream from one accepted ignored
M2.4.2 canonical payload and can revalidate the metadata under either
PowerShell host:

```powershell
.\scripts\verify_stock_delta_descriptions.ps1 `
  -SourceRunDirectory .\manual-artifacts\signon-captures\<accepted-run>

.\scripts\verify_stock_delta_descriptions.ps1 `
  -ValidateMetadataPath .\manual-artifacts\delta-description-captures\<accepted-run>\metadata.json
```

It stores only selected schema names, counts, masks, message geometry, and
definition hashes. Raw payloads, full field lists, authentication values, and
the opcode-44 body remain ignored/untracked. See
[GoldSrc delta descriptions](docs/GOLDSRC_DELTA_DESCRIPTIONS.md).

The M2.4.4 verifier projects one ignored signed-stock run or validates the
complete metadata-only 28-run evidence set under PowerShell 7 or Windows
PowerShell 5.1:

```powershell
.\scripts\verify_stock_movevars.ps1 `
  -SourceRunDirectory .\manual-artifacts\movevars-captures\<accepted-run>

.\scripts\verify_stock_movevars.ps1 `
  -ValidateMetadataSetRoot .\manual-artifacts\movevars-captures\projections
```

It strict-walks the first service stream from byte zero to opcode 13, leaves
that body untouched, and records only bounded field/cursor evidence. That
M2.4.4 layer sends no resource-transition request; the separate M3.1.1 stage
owns the exact request and stops at the neutral opcode-43 boundary. See
[GoldSrc movement-environment state](docs/GOLDSRC_MOVEVARS.md).

M3.1.1 uses a separate ignored stock corpus for opcode-13 and transition
evidence. Six clean baselines, maxplayers first-batch variants, accepted
same-HLDS overlap sessions, and paired request-loss, covering-ACK-loss, and
duplicate-datagram transitions establish the exact user-info grammar, exact
first-batch end, fixed nine-byte request, six-fragment later transfer, fixed
opcode-45 control, and opcode-43 cursor. Raw identity and payload bytes remain
ignored. The pinned SDK still has no numeric opcode-43 mapping, so the tracked
M3.1.1 pre-body API remains neutral. See
[GoldSrc user info](docs/GOLDSRC_USERINFO.md) and
[GoldSrc resource transition](docs/GOLDSRC_RESOURCE_TRANSITION.md).

Project and validate the sanitized ignored metadata with:

```powershell
pwsh -File .\scripts\verify_stock_resource_transition.ps1 -ProjectEvidenceSet
pwsh -File .\scripts\verify_stock_resource_transition.ps1 `
  -ValidateMetadataSetRoot `
  .\manual-artifacts\resource-transition-captures\projections
```

Both modes report 22 accepted sources, 18 complete transitions, 11 rejected
controlled attempts whose requested values were not observed, two incomplete
runs, and `opcode43=neutral-unconsumed`.

M3.1.2 uses an offline, bounded projector over the existing ignored canonical
second-service payloads. It starts no stock or project process, rejects a
primary or managed Steam research root with no override, and projects no raw
names, payload bytes, or digests:

```powershell
.\scripts\verify_stock_resource_list.ps1 -ProjectEvidenceSet
.\scripts\verify_stock_resource_list.ps1 `
  -ValidateMetadataPath `
  .\docs\evidence\GOLDSRC_RESOURCE_LIST_STOCK.json
```

The exact result is 54 parseable standard payloads: 50 `boot_camp` lists with
540 entries, two `crossfire` lists with 607 entries, and two `stalkyard` lists
with 532 entries. Historical `m244-sky-night-*` run IDs describe captures whose
configuration metadata identifies `stalkyard`; no stock map named `night` is
claimed. Two additional isolated `maxplayers 1` runs ended boundedly before
`sendres` with no resource payload/list; their grammar/count result is N/A and
they are not added to the 54-list denominator. At least two `maxplayers 8`
runs contain the full standard list. The standard gate is completed, custom
entries remain pending, and the offline run reports
`external-file-drift=none`. See
[GoldSrc opcode-43 resource list](docs/GOLDSRC_RESOURCE_LIST.md) and the
[tracked sanitized stock projection](docs/evidence/GOLDSRC_RESOURCE_LIST_STOCK.json).

M3.1.3 has a separate PowerShell 5.1-compatible verifier. Its research-root
mode is preflight only: it starts no process and makes no post-run drift claim.
Projection accepts only a complete set of bounded ignored relay metadata with
a successful per-run restoration attestation:

```powershell
.\scripts\verify_stock_resource_response.ps1 -ValidateResearchRoot `
  -ResearchHalfLifeRoot C:\research\Half-Life-isolated
.\scripts\verify_stock_resource_response.ps1 -ProjectEvidenceSet
```

At present there are no completed restoration-attested active M3.1.3 runs, so
projection intentionally returns nonzero and
`docs/evidence/GOLDSRC_RESOURCE_CLIENT_RESPONSE_STOCK.json` does not exist.
The reconstructed 54-session corpus supports the bounded codec design, but it
does not substitute for the required active baseline/map/restart/reconnect,
loss/duplicate, local-resource differential, and next-payload scenarios. No
active-stock scenario success or project-to-stock response TX is claimed.

An independent M3.2.1 wrapper can check the fixed local provider against an
explicit user-owned root without starting a network or stock process:

```powershell
.\scripts\verify_local_resource_provider.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_local_resource_check.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" -Game valve
```

The wrapper snapshots the fixed target and fail-closed root metadata (at most
200,000 entries and 64 components deep) before and after the network-free
checker and reports `external-file-drift`. It prints no
absolute path or raw MD5. This is optional local verification; this README does
not assert that it has been run successfully on an installed game.

The repository does not contain or redistribute Steam, Half-Life, game, WAD,
BSP, MDL, sound, or other copyrighted game assets. Users must supply any assets
they are licensed to use.

## Architecture

The central data-flow invariants are:

```text
Approved source -> Format importers -> Neutral CPU assets
    -> WorldRenderPackage -> WorldSceneRenderPackage
    -> WorldVisibilitySet/WorldVisibleDrawList
    -> ClientWorldState -> RenderScene -> IRenderer

GoldSrc network source --\
                         +-> ClientWorldState -> RenderScene -> IRenderer
hl.exe bridge source ----/                         |           |
                                               OpenGL       Null
```

The renderer must never parse packets or include GoldSrc networking structures.
Future network, `hl.exe` bridge, replay, and test providers all converge on the
same engine-owned world/scene representation. SDL3 owns the window, events,
input, OpenGL context, and timing; native APIs such as Winsock remain behind
small platform or network boundaries.

CMake groups the Visual Studio projects into `Apps`, `Engine`, `Tests`,
`ThirdParty`, and `CMake` folders. The principal targets are:

- `hlclient`;
- `hlclient_core`, `hlclient_platform`, `hlclient_filesystem`,
  `hlclient_hash_md5`, `hlclient_local_resources`;
- `hlclient_network`, `hlclient_goldsrc`, `hlclient_goldsrc_netchan`,
  `hlclient_goldsrc_signon`, `hlclient_goldsrc_client`,
  `hlclient_goldsrc_local_resources`, `hlclient_resource_consistency_api`,
  `hlclient_resource_consistency_local`, `hlclient_auth`,
  `hlclient_app_support`, `hlclient_client`;
- `hlclient_asset_api`, `hlclient_local_asset_source`,
  `hlclient_asset_dispatch`, `hlclient_asset_manager`, `hlclient_scene_api`;
- `hlclient_goldsrc_indexed_texture`, `hlclient_goldsrc_bsp`,
  `hlclient_goldsrc_wad3`, `hlclient_goldsrc_asset_dispatch`,
  `hlclient_goldsrc_world_texture_import`,
  `hlclient_goldsrc_world_textures`, `hlclient_goldsrc_lightmaps`,
  `hlclient_goldsrc_world_render`, `hlclient_goldsrc_spatial`,
  `hlclient_goldsrc_brush_models`;
- `hlclient_world_render_api`, `hlclient_world_render_package`,
  `hlclient_world_spatial`, `hlclient_world_visibility`,
  `hlclient_world_scene_renderer`, `hlclient_world_preview`;
- `hlclient_renderer_api`, `hlclient_renderer_opengl`,
  `hlclient_renderer_null`;
- `hlclient_local_resource_check` and `hlclient_world_texture_check`
  (network-free read-only diagnostics), plus `hlclient_world_viewer`
  (network-free read-only OpenGL preview);
- `hlclient_tests`;
- SDL3, bzip2, Catch2, GLAD2, and the Half-Life SDK reference target under
  `ThirdParty`.

See [Architecture](docs/ARCHITECTURE.md),
[Asset pipeline](docs/ASSET_PIPELINE.md), [Building](docs/BUILDING.md),
[GoldSrc connectionless protocol](docs/GOLDSRC_CONNECTIONLESS.md),
[GoldSrc connect request](docs/GOLDSRC_CONNECT_REQUEST.md),
[GoldSrc connect response](docs/GOLDSRC_CONNECT_RESPONSE.md),
[GoldSrc netchan](docs/GOLDSRC_NETCHAN.md),
[GoldSrc fragmentation](docs/GOLDSRC_FRAGMENTATION.md),
[GoldSrc initial sign-on](docs/GOLDSRC_INITIAL_SIGNON.md),
[GoldSrc server info](docs/GOLDSRC_SERVERINFO.md),
[GoldSrc delta descriptions](docs/GOLDSRC_DELTA_DESCRIPTIONS.md),
[GoldSrc movement environment](docs/GOLDSRC_MOVEVARS.md),
[GoldSrc user info](docs/GOLDSRC_USERINFO.md),
[GoldSrc resource transition](docs/GOLDSRC_RESOURCE_TRANSITION.md),
[GoldSrc resource list](docs/GOLDSRC_RESOURCE_LIST.md),
[GoldSrc resource response](docs/GOLDSRC_RESOURCE_CLIENT_RESPONSE.md),
[Local resource resolution](docs/LOCAL_RESOURCE_RESOLUTION.md),
[Local consistency provider](docs/LOCAL_RESOURCE_CONSISTENCY_PROVIDER.md),
[Resource-consistency provider API](docs/RESOURCE_CONSISTENCY_PROVIDER.md),
[Authentication provider](docs/AUTHENTICATION_PROVIDER.md),
[GoldSrc indexed miptex](docs/GOLDSRC_INDEXED_TEXTURE.md),
[GoldSrc WAD3](docs/GOLDSRC_WAD3.md),
[world texture resolution](docs/WORLD_TEXTURE_RESOLUTION.md),
[GoldSrc world lightmaps](docs/GOLDSRC_LIGHTMAPS.md),
[world render package](docs/WORLD_RENDER_PACKAGE.md),
[GoldSrc BSP spatial](docs/GOLDSRC_BSP_SPATIAL.md),
[GoldSrc PVS](docs/GOLDSRC_PVS.md),
[world visibility](docs/WORLD_VISIBILITY.md),
[GoldSrc brush submodels](docs/GOLDSRC_BRUSH_SUBMODELS.md),
[brush-submodel rendering](docs/BRUSH_SUBMODEL_RENDERING.md),
[OpenGL world renderer](docs/OPENGL_WORLD_RENDERER.md),
[offline world viewer](docs/WORLD_VIEWER.md),
[Dependencies](docs/DEPENDENCIES.md), and [Roadmap](docs/ROADMAP.md) for the
detailed contracts.

## Project boundaries

Runtime code is authored independently in this repository. It does not depend
on, link to, or copy implementation code from `Pvitaly91/hl-engine` or any
server implementation. Network behavior should be derived from observable
protocol behavior and publicly available documentation. The official Valve
Half-Life SDK submodule is pinned and exposed only as a `SYSTEM` header/reference
boundary; its programs, bundled libraries, and bundled SDL2 are not part of the
client build.

For the M1 profile, the exact request transmission was captured from an
original signed Valve `hl.exe`, and the exact response was observed live from
an original signed Valve HLDS. This black-box interoperability evidence proves
the request bytes, response layout, and the first decimal field's role as the
dynamic challenge. It does not prove the meaning of the response's other three
decimal fields, which remain deliberately opaque in project APIs and
documentation.

Do not copy implementation code from ReHLDS, Xash3D, reverse-engineered
proprietary GoldSrc code dumps, or original `hl.exe`, `hw.dll`, `sw.dll`, and
similar binaries. External projects may inform separately documented behavior
only where that use is lawful; code committed here must remain an independent
implementation.

Keep protocol-specific types inside `hlclient_goldsrc`. When a protocol value
must cross a boundary, translate it into a project-owned neutral type first.
This separation is a functional requirement for both standalone operation and
the planned injection bridge.

## Warnings and tests

Project code builds with strict warnings. To make warnings fatal for
project-owned targets:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DHLCLIENT_WARNINGS_AS_ERRORS=ON
```

This option is deliberately not applied globally to third-party code. Tests use
Catch2, avoid Internet and external game/server dependencies, and run through
CTest. M1/M2.1/M2.2 protocol and state-machine coverage uses synthetic fixtures
and local fake-HLDS UDP tests. M2.3.1 adds independent netchan wire/sequence
fixtures and a validated local fake-HLDS same-socket first-ACK path. M2.3.2 adds
deterministic persistent reliable-state, transaction, loss/ACK, duplicate,
pending-A/B, payload-bound, fragment-boundary, and 30-bit wrap coverage. Its
dedicated fake-HLDS UDP test confirms the exact same-socket success/incoming
scope described above; loss, lost-ACK, and A/B remain deterministic driver
coverage rather than extra real-UDP claims. M2.3.3 adds strict fragment-body
fixtures, malformed/range matrices, transactional normal reassembly, fixed
deadlines, duplicate/conflict/replacement handling, per-fragment reliable
generation, deterministic outgoing fragmentation, and bounded driver
receive/send/backpressure/cleanup coverage. A real-loopback fake-HLDS suite runs
the production handshake through `ACCEPT` and then the same-socket driver: its
incoming out-of-order/duplicate scenario passes 20/20 runs, and its independently
decoded outgoing drop/retry/clear scenario passes 20/20 runs. Separate cases
prove fixed missing-fragment timeout without partial delivery or extra traffic,
and the typed secondary-stream boundary without payload delivery or persistence.
These are bounded project tests, not stock interoperability claims. Stock-client
multi-fragment C2S and live project-to-stock checks remain opt-in/pending.
M2.4.1 adds strict initial-request, BZ2-envelope, service-stream, text-safety,
stage, coordinator, and same-socket fake-HLDS coverage. The sign-on fake runs
20/20 baseline, 20/20 dropped-request under an explicit deterministic ACK-gap
stimulus, and 20/20 fragmented/out-of-order batch scenarios, stops at the owning
opcode-11 boundary, and proves no extra/resource datagram after terminal
success. This is not an autonomous time-retry claim: a quiet peer reaches the
bounded channel timeout. Signed-stock observations and project-to-fake tests
are reported separately; live project-to-stock sign-on remains pending.
M2.4.2 adds typed server-info and exact pre-resource continuation coverage;
M2.4.3 adds seven-schema/219-field delta registry, bit-order, padding, and exact
post-delta cursor coverage. M2.4.4 adds exact 24-float/boolean/string parsing,
all truncation prefixes, finite-value and bounds checks, signed opcode-39
sizes, no-scan post-message continuation, transactional stage publication, and
same-socket fake-HLDS integration. Its deterministic suite covers 20/20
baseline, 20/20 fragmented/reordered, and 20/20 differential fake variants,
plus wrong-endpoint, malformed-BZ2, missing-fragment, duplicate-terminal-batch,
timeout, cancellation, and backpressure cases with zero resource continuation.
M3.1.1 adds exact opcode-13/private-info parsing, repeated-message and
first-batch completion, exact fixed request and opcode-45 codecs, neutral
opcode-43 cursor accounting, and same-driver transition composition. Its
deterministic stage suite covers 20/20 baseline, 20/20 dropped-request retry,
20/20 fragmented second-transfer, and 20/20 repeated-user-info runs. Negative
coverage includes every parser truncation, malformed pairs and duplicate info
keys, limits and limit plus one, wrong endian/reserved control values,
missing/wrong opcode-43 boundary, wrong endpoint, and transactional malformed
opcode 45.
M3.1.2 adds the exact independent resource-list fixture, LSB bit boundaries,
all byte and bit truncations, count/name/size/flag limits, duplicate identity,
terminal-fill/EOP, byte-preserving malicious-name isolation, immutable owning
state, post-list response metadata with zero TX, transactional stage events,
and same-socket fake-HLDS continuation. The historical pre-body stop retains
its M3.1.1 behavior.
M3.1.3 adds an independent 41-byte neutral opcode-5 fixture, strict response
parser/builder, exact descriptor/semantic/tail separation, a path-free
move-only provider API, semantic-once reliable lifecycle, covering-ACK and
next-payload boundaries, and deterministic provider/loss/duplicate tests.
These project tests are not promoted to active-stock evidence; the stock
projection remains blocked as described above.
M3.2.1 adds independent standard MD5 vectors and chunk/lifecycle cases;
virtual-name and GoldSrc type-mapping matrices; game-before-`valve`, exact-case,
case-fold ambiguity, traversal/absolute/ADS/device/non-ASCII/reparse/remote-file
resolver failures; one-handle size/read/change checks; ordered transactional
metadata-only inventory; fixed-target provider success/failure; and full
production-provider fake-HLDS response, retransmission, lost-ACK, and next-boundary
coverage. Test roots and file bytes are synthetic and temporary. No test writes
to an installed game, creates a download/cache/precache entry, loads an asset,
or touches a renderer. M3.2.2 adds strict correlation, separate per-entry
readiness/aggregate impact, exact world selection, path-safe locators, and the
bounded metadata-only manifest. M3.2.3 adds one-source incremental verified
opening, exact EOF/final-snapshot checks, pure importer probes, deterministic
cross-category dispatch, and a same-session selected-world stop before any
parser or renderer work. M4.1 adds the production `goldsrc-bsp-v30` importer,
strict synthetic BSP grammar/mutation coverage, world-model-only face-loop
reconstruction, raw texel UVs, metadata-only texture states, owning CPU world
geometry, and same-session fake-HLDS import coverage without post-manifest TX.
M4.2 adds shared miptex grammar and incremental four-level RGBA conversion;
BSP physical-record/alias extraction; inert worldspawn basename isolation;
strict WAD3 catalog, compression, type, duplicate, and dimension policies;
deterministic declared-archive/game-root lookup; immutable complete/incomplete
material bindings; bounded same-session stage coverage; and the network-free
read-only checker. All automated asset bytes and local files are original
synthetic fixtures; no installed game asset, renderer, or GPU is required.
M4.3 adds exact face-local lightmap extents and RGB/style-range coverage,
deterministic padding and multi-page atlas fixtures, texel-center/base-UV and
package-validation tests, neutral camera/matrix/scene tests, package-stage
lifetime/backpressure cases, null-renderer regressions, and OpenGL upload,
state, cache, masked, culling, resize, and transactional-failure checks where a
3.3 Core context is available. A synthetic graphical integration validates
non-clear pixels without network or installed game data; the offline viewer is
an optional user-owned verification path rather than a CI asset dependency.
M4.4 adds synthetic BSP-node/leaf/marksurface and strict PVS RLE fixtures,
point/AABB traversal, frustum and fallback coverage, exact per-surface draw
ranges, inert entity/transform/brush-instance tests, scene/visibility revision
separation, and static-brush rendering checks. OpenGL frame tests run when the
actual current context reports 3.3 or newer and skip only for unavailable or
legacy contexts; a requested version alone is not the capability gate.

## License

Original project code and documentation are available under the [MIT License](LICENSE).
That license does not relicense third-party dependencies, generated GLAD code,
the Valve Half-Life SDK, or user-supplied game assets. In particular, the Valve
SDK has its own restrictive SDK license; review it before redistribution or
commercial use. Exact pins and license scopes are recorded in
[docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
