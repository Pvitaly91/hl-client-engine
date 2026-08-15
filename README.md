# hl-client-engine

`hl-client-engine` is an independent, clean-room implementation of a
GoldSrc-compatible game client. Its first interoperability goal is to connect
to an original Half-Life Dedicated Server (HLDS) while keeping protocol,
simulation, and rendering concerns separated enough to support a future
`hl.exe` injection bridge.

The repository has completed **M2.3: netchan bootstrap, sequencing, and
acknowledgements** for the bounded captured profile and deterministic local
fake-HLDS path. Completed M1–M2.3 behavior includes the Protocol 48 challenge,
captured one-shot `connect` request, strict immediate connectionless
`ACCEPT`/`REJECT`, an explicit authentication-provider boundary, same-socket
netchan bootstrap, bounded normal reassembly, and the required acknowledgement.
There is no production Steam authentication provider, authentication bypass,
`svc_*`/sign-on parsing, resource pipeline, snapshot handling, or gameplay.
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
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
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

The M2.3 target stop point is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27015 --stop-after netchan-bootstrap `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

The production path is validated end to end against a deterministic local fake
HLDS. A completed bootstrap means only that the same UDP socket processed a
valid sequenced packet, obtained one complete opaque payload, and emitted the
required acknowledgement behavior. It still stops before every `svc_*` or
sign-on interpretation.

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
under ignored `manual-artifacts/netchan-captures/`. It validates relay-reported
bounded completion but neither prints payload bytes nor proves that project
`hlclient` can authenticate to stock HLDS.

The repository does not contain or redistribute Steam, Half-Life, game, WAD,
BSP, MDL, sound, or other copyrighted game assets. Users must supply any assets
they are licensed to use.

## Architecture

The central data-flow invariants are:

```text
Virtual filesystem -> Format importer -> Neutral CPU asset -> Asset manager

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
- `hlclient_core`, `hlclient_platform`, `hlclient_filesystem`;
- `hlclient_network`, `hlclient_goldsrc`, `hlclient_goldsrc_netchan`,
  `hlclient_goldsrc_client`,
  `hlclient_auth`, `hlclient_app_support`, `hlclient_client`;
- `hlclient_asset_api`, `hlclient_asset_manager`, `hlclient_scene_api`;
- `hlclient_renderer_api`, `hlclient_renderer_opengl`,
  `hlclient_renderer_null`;
- `hlclient_tests`;
- SDL3, Catch2, GLAD2, and the Half-Life SDK reference target under
  `ThirdParty`.

See [Architecture](docs/ARCHITECTURE.md),
[Asset pipeline](docs/ASSET_PIPELINE.md), [Building](docs/BUILDING.md),
[GoldSrc connectionless protocol](docs/GOLDSRC_CONNECTIONLESS.md),
[GoldSrc connect request](docs/GOLDSRC_CONNECT_REQUEST.md),
[GoldSrc connect response](docs/GOLDSRC_CONNECT_RESPONSE.md),
[GoldSrc netchan](docs/GOLDSRC_NETCHAN.md),
[Authentication provider](docs/AUTHENTICATION_PROVIDER.md),
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
and local fake-HLDS UDP tests. M2.3 adds independent netchan fixtures and a
validated local fake-HLDS same-socket bootstrap path.
Original-HLDS checks remain opt-in and do not turn stock-server acceptance into
an automated-suite claim.

## License

Original project code and documentation are available under the [MIT License](LICENSE).
That license does not relicense third-party dependencies, generated GLAD code,
the Valve Half-Life SDK, or user-supplied game assets. In particular, the Valve
SDK has its own restrictive SDK license; review it before redistribution or
commercial use. Exact pins and license scopes are recorded in
[docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
