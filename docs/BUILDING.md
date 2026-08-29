# Building and debugging

## Supported reference environment

The acceptance platform for this milestone is Visual Studio 2022, MSVC v143,
Windows 10/11, and Win32 (x86). Debug is the primary configuration. Release and
RelWithDebInfo are also supported through Visual Studio's normal multi-config
solution model.

Install these Visual Studio Installer components:

- Visual Studio 2022, including the **Desktop development with C++** workload;
- MSVC v143 x86/x64 build tools;
- a Windows 10 or Windows 11 SDK;
- CMake 3.25 or newer;
- Git, including support for submodules.

The first configure downloads pinned SDL3 and Catch2 sources from GitHub, so it
also requires network access. GLAD2 is already generated and committed; Python
and Jinja2 are not build prerequisites.

### Shell setup

The commands below work in a normal PowerShell session when `cmake` and `git`
are on `PATH`. Visual Studio's bundled CMake is not always on a normal shell's
`PATH`; if either command is missing, open **Developer PowerShell for VS 2022**
from the Start menu.

`VsDevCmd.bat` is another supported setup path. Its location is normally:

```text
%ProgramFiles%\Microsoft Visual Studio\2022\<Edition>\Common7\Tools\VsDevCmd.bat
```

Run it from `cmd.exe` (or start `cmd /k` with it) before invoking CMake, for
example with `-arch=x86 -host_arch=x64`. Calling a `.bat` file as a child of an
existing PowerShell process does not automatically import its environment back
into that PowerShell session. The Visual Studio generator normally discovers
the installed toolchain itself; the developer shell is principally useful for
reliable tool discovery.

## Clone and initialize the SDK reference

```powershell
git clone --recurse-submodules https://github.com/Pvitaly91/hl-client-engine.git
Set-Location hl-client-engine
```

For an existing non-recursive clone:

```powershell
git submodule update --init --recursive
```

The required Half-Life SDK gitlink is
`b1b5cf5892918535619b2937bb927e46cb097ba1`. A detached HEAD inside a submodule
is normal. Verify it without changing the pin:

```powershell
git submodule status third_party/halflife-sdk
git -C third_party/halflife-sdk rev-parse HEAD
```

## Manual Visual Studio solution workflow

From the repository root, configure exactly as follows:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DHLCLIENT_WARNINGS_AS_ERRORS=ON
```

This produces the ordinary Visual Studio solution:

```text
<repository>\build\hl-client-engine.sln
```

At the workspace path used for this bootstrap, that is:

```text
D:\DEV\CPP\HL-Client-engine\build\hl-client-engine.sln
```

Build and test the reference configuration:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Build other standard configurations without regenerating the solution:

```powershell
cmake --build build --config Release
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

Do not pass `CMAKE_BUILD_TYPE` to this generator. Visual Studio selects the
configuration at build/test time with `--config` or from the IDE toolbar.

## Preset workflows

The canonical Visual Studio preset uses the same `build` tree as the manual
command:

```powershell
cmake --preset vs2022-win32
cmake --build --preset vs2022-win32-debug
ctest --preset vs2022-win32-debug
```

The matching Release commands are:

```powershell
cmake --preset vs2022-win32
cmake --build --preset vs2022-win32-release
ctest --preset vs2022-win32-release
```

For RelWithDebInfo:

```powershell
cmake --preset vs2022-win32
cmake --build --preset vs2022-win32-relwithdebinfo
ctest --preset vs2022-win32-relwithdebinfo
```

Short convenience names provide a configure/build/test triplet:

```powershell
cmake --preset win32-debug
cmake --build --preset win32-debug
ctest --preset win32-debug

cmake --preset win32-release
cmake --build --preset win32-release
ctest --preset win32-release
```

The `win32-debug` and `win32-release` configure presets intentionally configure
the same multi-config Win32 solution. Their build and test presets select the
configuration; no configure preset sets `CMAKE_BUILD_TYPE`.

## Visual Studio F5 workflow

1. Open `build\hl-client-engine.sln` in Visual Studio 2022.
2. Select **Debug** and **Win32** in the solution toolbar.
3. Confirm `hlclient` appears in the `Apps` solution folder. CMake sets it as
   the startup project, so no manual startup-project selection is expected.
4. Press **F5**.

CMake sets the debugger working directory to the repository root. The program
therefore does not accidentally depend on `build\Debug` or another
configuration-specific working directory.

Runtime output is organized per configuration:

```text
build\bin\Debug\hlclient.exe
build\bin\Debug\hlclient_bsp_compat_check.exe
build\bin\Debug\hlclient_usercmd_check.exe
build\bin\Debug\hlclient_world_viewer.exe
build\bin\Debug\SDL3.dll
build\lib\Debug\...
```

The SDL3 shared-library target is built from the pinned source and post-build
commands copy its configuration-matching DLL next to `hlclient.exe` and the
offline world viewer. This is also done for Release and RelWithDebInfo. GLAD2
is a static project target and has no runtime DLL. Windows supplies
`opengl32.dll`; the installed graphics driver must provide an OpenGL 3.3
Core-capable implementation.

No project resources need to be copied for the current client. Half-Life
assets are optional unless `--basedir` is supplied, and are never copied into
the build tree automatically. The challenge-only network path also does not
require local game assets.

## Run from PowerShell

```powershell
.\build\bin\Debug\hlclient.exe --version
.\build\bin\Debug\hlclient.exe --help
.\build\bin\Debug\hlclient.exe --renderer null
.\build\bin\Debug\hlclient.exe
```

The null renderer is headless and completes a bounded frame by default. It does
not initialize SDL or require a display, OpenGL driver, or GPU, which makes it
the runtime smoke path used by CI.

The application can start with no game installation. To validate an installation:

```powershell
.\build\bin\Debug\hlclient.exe --basedir "C:\Games\Half-Life" --game valve
```

The M4.3 CPU-only package boundary uses the same explicit connection/resource
arguments as `world-textures` and accepts the null renderer:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after world-render-package `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve
```

This imports lightmaps and validates an immutable CPU render package but creates
no SDL/OpenGL resources. `--view-world` requires `--renderer opengl`; it builds
that package and finalizes retained network/authentication state before opening
the local diagnostic window. A positive `HLCLIENT_SMOKE_TEST_FRAMES` bounds its
frame loop.

The M4.4 spatial/scene boundary is also CPU-only and valid with the null
renderer:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after world-spatial-scene `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\Steam\steamapps\common\Half-Life" --game valve `
  --visibility pvs-frustum --brush-submodels static
```

This builds canonical spatial/PVS state and the selected renderer-neutral
static scene, but no SDL window, OpenGL context, or GPU resource. The defaults
remain `--visibility all --brush-submodels off --camera static`, preserving the
M4.3 route.

M4.4.1 also provides a network-free, SDL-free CPU acceptance target. Its
default BSP-v30 geometry profile is
`valve_qbsp_clockwise_wire_to_counter_clockwise_render`: Valve QBSP clockwise
wire loops are strictly validated against the side-adjusted normal and then
canonicalized to counter-clockwise renderer output by the shared world/brush
builder.

```powershell
cmake --build --preset vs2022-win32-debug `
  --target hlclient_bsp_compat_check

.\build\bin\Debug\hlclient_bsp_compat_check.exe `
  --basedir "<Half-Life-root>" --game valve `
  --map maps/<name>.bsp --validate-through spatial-scene

.\scripts\verify_stock_bsp_geometry_compatibility.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_bsp_compat_check.exe `
  -Basedir "<Half-Life-root>" -Game valve `
  -Maps @("maps/<name>.bsp")
```

The checker writes no files, initializes no network or graphics subsystem,
and emits deterministic bounded metadata without native paths, entity text,
texture names, or raw coordinates. The wrapper runs each map twice and fails
on a changed summary or BSP/WAD/inventory drift. Complete this CPU check before
an optional graphical proof.

For an entirely offline read-only preview, build and run the viewer target:

```powershell
cmake --build --preset vs2022-win32-debug --target hlclient_world_viewer

.\build\bin\Debug\hlclient_world_viewer.exe `
  --basedir "<Half-Life-root>" `
  --game valve `
  --map maps/<name>.bsp `
  --camera spawn `
  --visibility pvs-frustum `
  --brush-submodels static `
  --cull back
```

The viewer accepts a safe virtual map name rather than a native map path. It
validates BSP, textures, lightmaps, and the CPU package before SDL/OpenGL
initialization; it starts no network or stock process and writes no game data.
Use `--camera static` for a deterministic bounds view, `--camera orbit` for a
slow bounds-derived diagnostic orbit, or `--camera spawn` for inert initial
spawn metadata with bounds fallback. Visibility accepts `all`, `frustum`,
`pvs`, or `pvs-frustum`; brush submodels accept `off` or `static`; culling
accepts `none` or `back` and defaults to `none`. See [offline world
viewer](WORLD_VIEWER.md).

On a desktop whose actual context is OpenGL 3.3 Core or newer, the bounded
read-only wrapper validates both renderer cull modes by default:

```powershell
.\scripts\verify_local_world_render.ps1 `
  -ViewerPath .\build\bin\Debug\hlclient_world_viewer.exe `
  -Basedir "<Half-Life-root>" -Game valve -Map maps/<name>.bsp `
  -Frames 2 -Visibility pvs-frustum -BrushSubmodels static `
  -Camera spawn -CullModes @('none', 'back')
```

Each cull mode is a separate two-frame process and must report one world/scene
upload, non-clear pixels, nonzero draws and triangles, and `gl-error=none`.
The wrapper also requires unchanged BSP/WAD/inventory snapshots. Pass a single
`none` or `back` value to `-CullModes` for a bounded diagnostic rerun. A host
without the required graphics capability can omit this optional OpenGL check,
but not the preceding CPU compatibility verifier. Omitting `-CullModes`
selects the dual `none,back` acceptance run.

To perform the M1 connectionless challenge exchange without opening a window:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null --connect 127.0.0.1:27015
```

Both spellings are accepted:

```text
--connect <IPv4:port>
+connect <IPv4:port>
```

`--connect` remains challenge-only by default. It sends the bounded
connectionless `getchallenge steam` request, waits with bounded retries and an
overall timeout, accepts a response only from the exact requested endpoint,
reports the challenge, and exits. This exact behavior is also selected by
`--stop-after challenge`.

The explicit M2.1 development path validates a local 245-byte authentication
input, sends one connect request on the same socket, and exits:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27015 --stop-after connect-request `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

The file is not copied or logged. Success means only that one datagram was sent;
this `connect-request` stop point does not wait for a response or create a
netchan. Authentication generation remains unimplemented.

The M2.3.3 transport-only path waits for connectionless `ACCEPT`, preserves the
same socket, and runs a bounded `NetchanDriver` through the selected stop. An
unfragmented first payload completes directly; supported slot-0 fragments are
ACKed per fragment and reassembled before the first complete owning opaque
payload is returned. The CLI then exits before `svc_*` or sign-on parsing:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after netchan-bootstrap `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

The bootstrap stage/coordinator owns the driver and optional authentication
lifetime through success or a typed timeout, cancellation, network, protocol,
or backpressure terminal. Cleanup releases reliable, fragment, and one-shot
unreliable state exactly once. The driver borrows the already-bound transport;
it does not replace or close that socket. No raw reliable/fragment payload CLI
is provided.

The six-run M2.3.1 stock capture set established that the stock client sends
first, while that milestone deliberately kept its initial reliable body opaque.
At the `netchan-bootstrap` stop, the deterministic fake HLDS therefore sends the
first server packet and verifies the project's exact single ACK plus absence of
an extra datagram. Later explicit sign-on stops use the independently captured
fixed request described below. The project-to-stock live path remains pending
because the project has no production Steam authentication provider. Persistent
reliable state is M2.3.2; bounded normal fragmentation/reassembly and the driver
are M2.3.3. See
[GoldSrc netchan](GOLDSRC_NETCHAN.md) and
[GoldSrc fragmentation](GOLDSRC_FRAGMENTATION.md) for evidence labels, limits,
and unsupported behavior.

The M2.4.1 stop continues on that same socket with only the typed initial
request and stops before the first complex service-message body:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after signon-boundary `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

Success means the fixed `03 6E 65 77 00` semantic request was acknowledged,
the first normal payload was reassembled and strictly decompressed from its
`BZ2\0` envelope, opcode 8 was decoded as one bounded owning NUL string, and
opcode 11 was reached without parsing its body. No resource/spawn continuation
is sent. Live project-to-stock sign-on remains pending a production Steam
authentication provider; the completed project path is proved against local
fake-HLDS fixtures.

The M2.4.2 continuation retains that exact socket/driver/auth lifetime and
stops before the confirmed opcode-14 category-C boundary body:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after pre-resource `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

Success publishes owning typed server-info metadata, consumes only the exact
empty-string/zero opcode-54 control, and sends no resource command. Game/map
values are untrusted metadata; they are never used as filesystem paths. The
second-client slot candidate remains evidence-gated and private. See
[GoldSrc server info](GOLDSRC_SERVERINFO.md).

The M2.4.3 and M2.4.4 continuations use the same retained transport and
authentication lifetime:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after delta-schemas `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace

.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after movevars `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

`delta-schemas` publishes the seven-schema registry and stops before opcode
44. `movevars` additionally decodes the confirmed movement/environment state
and simple controls, then stops before the neutral opcode-13 body. Neither mode
sends `sendres`, parses resource entries, applies movement values, or opens a
server-provided path. See [GoldSrc delta descriptions](GOLDSRC_DELTA_DESCRIPTIONS.md)
and [GoldSrc movement-environment state](GOLDSRC_MOVEVARS.md).

Add `--net-trace` when diagnosing the exchange:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null --connect 127.0.0.1:27015 --net-trace
```

Challenge trace previews are size-capped and escaped. Connect traces are
metadata-only and redact authentication. Diagnostics include direction, endpoint,
classification, attempt, elapsed time, and datagram size without printing raw
untrusted control bytes.

### Post-resource entity evidence boundary

The M4.5.1 diagnostic spellings are CPU-only and accept the null renderer:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27015 --stop-after server-baselines `
  --auth-provider file --auth-material-file C:\private\hl-auth-material.bin `
  --resource-consistency-provider local `
  --basedir "D:\isolated\Half-Life" --game valve --net-trace
```

Replace `server-baselines` with `entity-snapshot` for the later reserved stop.
The current stock profile intentionally reaches only the proven opcode-5
continuation boundary, sends no unconfirmed request, reports
evidence-pending, and returns code 2. This is expected until accepted
restoration-attested captures close the stock grammar gate.

Research-root preflight can be run without launching stock processes:

```powershell
.\scripts\verify_stock_entity_snapshots.ps1 `
  -ValidateResearchRoot `
  -ResearchHalfLifeRoot "D:\isolated\Half-Life" `
  -ClientPath "D:\isolated\Half-Life\hl.exe" `
  -HldsPath "D:\isolated\Half-Life\hlds.exe"
```

Raw research output is ignored. The verifier never creates the tracked stock
evidence JSON for a zero-run or incomplete corpus. Capture restoration rejects
reparse points before and during rollback and preserves its bounded temporary
backup for manual recovery if exact before/after drift verification cannot
complete. M4.7.1 now provides a bounded byte-preserving capture harness and an
independent structural checker, but neither promotes transport metadata to a
typed stock message observation. Tracked validation and projection therefore
remain fail-closed instead of accepting retry/drop/duplicate/replay claims from
labels and counts.

### Stock runtime authority evidence boundary

Build the M4.7.1 capture, checker, and synthetic boundary tests with:

```powershell
cmake --build build --config Debug --target `
  hlclient_stock_runtime_capture `
  hlclient_stock_runtime_check `
  hlclient_tests
.\build\bin\Debug\hlclient_tests.exe "[stock-runtime]"
```

The default verifier is intentionally zero-stock-process and zero-write. With no
accepted stock corpus it succeeds only by reporting `accepted-runs=0`, stock
versions as `not-observed`, restoration as `not-run`, and
`result=evidence_pending`:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  .\scripts\verify_stock_runtime_state.ps1 -ValidateEvidencePending
```

Active stock orchestration is currently disabled. The commands below document
the intended future campaign, but every active scenario fails before launch,
network access, output creation, or backup creation until OS-level outbound
isolation and exact app-build/engine/Protocol/server-build observation exist.

Prepare a separate user-owned research copy without overwriting or deleting any
existing destination. Inspect both paths before running this example and never
substitute a regular play installation as `$research`:

```powershell
$source = "D:\Steam\steamapps\common\Half-Life"
$research = "D:\DEV\HLCLIENT-RESEARCH\Half-Life"

if (-not (Test-Path -LiteralPath $source -PathType Container)) {
  throw "Source Half-Life directory is absent."
}
if (Test-Path -LiteralPath $research) {
  throw "Research destination already exists; choose a new empty path."
}

$researchParent = Split-Path -Parent $research
New-Item -ItemType Directory -Path $researchParent -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $research -Recurse
Set-Content -LiteralPath (Join-Path $research ".hlclient-research-isolated") `
  -Value "HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1" -Encoding ascii
```

The read-only preflight is the only stock-installation validation currently
enabled:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  .\scripts\capture_stock_runtime_state.ps1 -ValidateResearchRoot `
  -ResearchHalfLifeRoot $research `
  -ClientPath (Join-Path $research "hl.exe") `
  -HldsPath (Join-Path $research "hlds.exe")
```

Preflight accepts only a ready fixed local drive-letter path: UNC/network,
volume-alias, substituted-drive, and reparse-root forms are rejected. It uses a
read-only `subst.exe` listing and expands existing DOS/8.3 aliases before
comparing the research root with the repository and every configured Steam
library. It then requires the exact marker, canonical unlinked
`hl.exe`/`hlds.exe` files, no reparse points or alternate data streams anywhere
in the bounded tree, valid Valve signatures, and launcher `VERSIONINFO`
1.1.1.1/4.1.1.1. It
does not observe Steam App build 15961492, engine 1.1.2.2, Protocol 48, or
server build 10210 and therefore is not a version-complete stock attestation.

Exercise the synthetic hostile-tree restoration guard independently with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  .\scripts\capture_stock_runtime_state.ps1 -ValidateRestorationGuard
```

This creates and removes only bounded temporary self-test files. It proves the
implemented hardlink/junction rollback cases, not restoration of a stock run.
The retained future orchestration takes a complete transactional copy and keeps
the backup for manual recovery if exact restoration cannot be established.

These baseline, idle, manual movement/view, and loss command templates all
currently exit 1 with `active-capture=evidence_pending` and zero processes or
files written:

```powershell
$active = @{
  ResearchHalfLifeRoot = $research
  ClientPath = Join-Path $research "hl.exe"
  HldsPath = Join-Path $research "hlds.exe"
  CaptureToolPath = ".\build\bin\Debug\hlclient_stock_runtime_capture.exe"
  Game = "valve"
}

& .\scripts\capture_stock_runtime_state.ps1 @active `
  -Map boot_camp -Scenario baseline
& .\scripts\capture_stock_runtime_state.ps1 @active `
  -Map crossfire -Scenario idle-runtime
& .\scripts\capture_stock_runtime_state.ps1 @active `
  -Map stalkyard -Scenario forward
& .\scripts\capture_stock_runtime_state.ps1 @active `
  -Map boot_camp -Scenario yaw-positive
& .\scripts\capture_stock_runtime_state.ps1 @active `
  -Map boot_camp -Scenario drop-server-runtime
```

The retained labels are `baseline`, `idle-runtime`, `forward`,
`backward`, `left`, `right`, `forward-right`, `jump`, `duck`, `duck-stand`,
`yaw-positive`, `yaw-negative`, `pitch-positive`, and `pitch-negative`.
They do not inject or prove a manual action. Whole-datagram labels are
`drop-server-runtime`,
`drop-two-server-runtime`, `duplicate-server-runtime`,
`reorder-server-runtime`, `drop-client-move`, and `delay-client-move`.
The relay implementation selects the Nth datagram in a direction (default 20),
not a decoded runtime or move packet, so none of these names earns semantic
movement/view/loss credit. Lifecycle/rate labels remain pending too.

The standalone relay code can store bounded raw UDP datagrams and flat
transport metadata below ignored
`manual-artifacts/stock-runtime/<32-hex-run-id>`. It does not yet record parsed
netchan, transformed, fragment, reassembled, decompressed, cursor, or runtime
message layers; configured limits for those future layers are not evidence that
they were consumed. If a future valid transport-only run exists, inspect it
without decoding unconfirmed grammar with:

```powershell
.\build\bin\Debug\hlclient_stock_runtime_check.exe `
  --capture-root ".\manual-artifacts\stock-runtime\<32-hex-run-id>" `
  --scenario transcript
```

The other accepted CLI labels are `baselines`, `entities`, `clientdata`,
`authority`, and `ack`; all six currently return the same zero-observation
`result=evidence_pending` report. The checker binds raw filenames, sizes, and
file-content hashes, but is not a runtime grammar walker. Corpus verification
is read-only and always returns nonzero for transport-only records because no
promotion path or accepted runtime grammar exists:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  .\scripts\verify_stock_runtime_state.ps1 `
  -CaptureRoot ".\manual-artifacts\stock-runtime" `
  -CheckerPath ".\build\bin\Debug\hlclient_stock_runtime_check.exe"
```

| Command | Current exit | Stock processes / persistent writes | Meaning |
|---|---:|---|---|
| `verify_stock_runtime_state.ps1 -ValidateEvidencePending` | 0 | 0 / 0 | Honest zero-corpus pending state. |
| `capture_stock_runtime_state.ps1 -ValidateResearchRoot ...` | 0 for a policy-screened root, otherwise 1 | 0 / 0 | Structural/signature/launcher `VERSIONINFO` preflight; invokes one read-only `subst.exe` helper and retains physical identity as pending. |
| `capture_stock_runtime_state.ps1 -ValidateRestorationGuard` | 0 on success | 0 / temporary self-test files removed | Synthetic rollback test, not stock attestation. |
| Any active scenario | 1 | 0 / 0 | Deliberately blocked before launch/output. |
| Checker on a future structurally valid transport run | 0 | 0 / 0 | Transport/raw integrity with `result=evidence_pending`. |
| Corpus verifier | 1 | 0 / 0 | Accepted runs and decoded runtime updates remain zero. |

Do not create `docs/evidence/GOLDSRC_STOCK_RUNTIME_STATE.json` while the accepted
run count is zero. The current decoder retains the exact first unsupported
runtime cursor and stops; opcode/body grammar, baseline/update/removal semantics,
local-player identity, clientdata, server time, authoritative movement, and
command acknowledgement all remain evidence-gated. The catalog decoder is a
standalone pending API and is not yet composed into production
`PostResourceSignon`.

### Manual original-HLDS verification

With a user-run original HLDS already listening on loopback, run exactly:

```powershell
.\scripts\verify_original_hlds_challenge.ps1 `
  -ClientPath .\build\bin\Debug\hlclient.exe `
  -Endpoint 127.0.0.1:27015
```

The script requires an explicit client path and endpoint; it does not discover
or download Steam or game binaries. To let it start a user-supplied server for
the duration of the check, pass matching endpoint/port values and an explicit
path:

```powershell
.\scripts\verify_original_hlds_challenge.ps1 `
  -ClientPath .\build\bin\Debug\hlclient.exe `
  -Endpoint 127.0.0.1:27015 `
  -HldsPath "D:\Steam\steamapps\common\Half-Life\hlds.exe" `
  -Game valve `
  -Map boot_camp `
  -Port 27015
```

Logs are written below ignored
`manual-artifacts\original-hlds-challenge\<timestamp>`. The `finally` block
stops only the HLDS process that this script started; an already running server
is never stopped by the script.

The M1 compatibility profile has live stock-Valve evidence on both sides: the
exact request transmission was captured from original signed `hl.exe`, and the
exact response was observed from original signed HLDS (Protocol 48, executable
version 1.1.2.2, build 10210). Only the first response decimal is proven to be
the dynamic challenge; the following three decimal fields remain intentionally
opaque. This manual evidence complements deterministic synthetic and loopback
tests and is not required by CTest.

For a bounded window/render-loop smoke run, set a positive frame count:

```powershell
$env:HLCLIENT_SMOKE_TEST_FRAMES = "3"
.\build\bin\Debug\hlclient.exe
Remove-Item Env:HLCLIENT_SMOKE_TEST_FRAMES
```

This OpenGL smoke still requires a usable graphical desktop and OpenGL driver.
CI uses `--version`, `--help`, and `--renderer null` instead of assuming that a
hosted runner has a suitable interactive graphics session.

Renderer frame tests use the actual current `GL_VERSION`, parsed through a
bounded test helper. They run on OpenGL 3.3-or-newer contexts and may report a
capability skip on unavailable or genuinely legacy contexts. The SDL requested
version alone does not enable the tests, and the gate is not an unconditional
skip. Production also checks the actual context and requires OpenGL 3.3 Core.

## Build options

`HLCLIENT_WARNINGS_AS_ERRORS` is off by default. Enable it during configure to
promote warnings to errors on project-owned targets:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DHLCLIENT_WARNINGS_AS_ERRORS=ON
cmake --build build --config Debug
```

The setting is target-scoped. Third-party SDL3, Catch2, generated GLAD2 code,
and Half-Life SDK headers are not globally compiled with `/WX`.

`BUILD_TESTING` defaults on through CTest. It can be disabled for a minimal
consumer build with `-DBUILD_TESTING=OFF`, but the acceptance build and presets
leave it enabled.

## Troubleshooting

### CMake selects the wrong architecture or generator

A CMake build directory is tied to its original generator and platform. If an
existing `build` tree was configured for x64, Ninja, or another Visual Studio
version, remove or rename that build tree and configure it again with:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
```

The generated solution toolbar and CMake configure output should both identify
Win32. Do not substitute x64 for the reference acceptance run.

### CMake cannot find Visual Studio or v143

Open Visual Studio Installer, modify the installation, and install the Desktop
C++ workload plus the MSVC v143 x86/x64 tools and a Windows SDK. Then rerun the
configure step from Developer PowerShell for VS 2022.

### FetchContent cannot obtain SDL3 or Catch2

Confirm Git is on `PATH`, the submodule is initialized, and GitHub access is
available. Dependencies are pinned by immutable commit ID, so changing to an
unreviewed branch or floating tag is not a supported workaround. A populated
build cache may be used offline only after the same pinned sources were fetched.

### F5 or the viewer reports a missing SDL3 DLL

Build `hlclient` or `hlclient_world_viewer` for the currently selected solution
configuration. The post-build deployment runs when either executable target is
built. Confirm that the executable and `SDL3.dll` are in
`build\bin\<Configuration>`. Do not copy a DLL from another architecture or
configuration.

### OpenGL context creation fails

Update the native GPU driver and avoid Remote Desktop/software-display setups
that expose only a legacy OpenGL implementation. The project requests an
OpenGL 3.3 Core context. GLAD loads function pointers after the SDL context
exists; it does not provide the driver itself.

If graphical CTest cases skip, inspect the reported actual context rather than
assuming that a successfully requested 3.3 context was supplied. A legacy
driver is an accepted capability skip for those tests; it is not accepted by
the production OpenGL renderer.

## GoldSrc visual-asset checker

The Win32 build also produces the CPU-only
`build\bin\<Configuration>\hlclient_goldsrc_asset_check.exe`. It accepts an
explicit Half-Life base, one game directory, a safe virtual asset name, and an
optional category constraint:

```powershell
.\build\bin\Debug\hlclient_goldsrc_asset_check.exe `
  --basedir "D:\Games\Half-Life" --game valve `
  --asset models/barney.mdl --kind model
```

Use `/` rather than `\` in the virtual asset argument. The tool resolves only
the configured game and `valve` roots, follows Studio companions only in the
main model's exact root, prints aggregate metadata, and performs no rendering,
networking, or writes. User-owned repeated read-only checks are available via
`scripts\verify_local_goldsrc_visual_assets.ps1`; missing optional names are
reported as pending and are never created. Before reading selected assets, the
verifier rejects reparse points in both roots, binds every selected-file digest
to its root-relative identity, and rejects a substituted checker PE containing
known Windows network dependencies, network API evidence, or process-launch API
evidence. Each checker run has a 30-second wall deadline and a 64 KiB combined
standard-output/error capture limit. While it runs, the verifier observes the
full process tree (including the trusted Windows console host) for TCP/UDP
endpoints and fails if the checker launches another process. These checks report
the evidence actually tested instead of making an unconditional network claim.
Game files are not required by the automated build or CTest suite.

The M4.5.3 offline `hlclient_entity_viewer` accepts only safe virtual map/model/
sprite names below explicit roots and constructs project-owned synthetic
snapshots. `scripts\verify_local_entity_rendering.ps1` performs bounded read-only
hash/size/write-time checks around that viewer. This validates imported asset
rendering under synthetic playback; it does not validate stock network mapping.

M4.6.1 extends the viewers with local interactive cameras. The world viewer
accepts `--camera free-fly`; the entity viewer additionally accepts
`--camera entity-first-person --controlled-entity <synthetic-number>`. Set
`HLCLIENT_SMOKE_TEST_FRAMES` for bounded hidden runs: they require no physical
input and remain stationary. Unbounded viewers use click-to-capture, Escape to
release, WASD, Space/Control, Shift, and relative mouse look. These controls
are offline preview semantics and emit no network command.

## GoldSrc usercmd checker

M4.6.2 adds three project-owned Win32 library targets and one offline checker:

```powershell
cmake --build build --config Debug --target `
  hlclient_goldsrc_usercmd_api `
  hlclient_goldsrc_usercmd_codec `
  hlclient_goldsrc_usercmd_session `
  hlclient_usercmd_check
```

The checker accepts only the sealed synthetic profile and one named scenario:

```powershell
.\build\bin\Debug\hlclient_usercmd_check.exe `
  --profile synthetic --scenario idle
.\build\bin\Debug\hlclient_usercmd_check.exe `
  --profile synthetic --scenario batch
.\build\bin\Debug\hlclient_usercmd_check.exe `
  --profile synthetic --scenario loss-recovery
```

The complete scenario set is `idle`, `move`, `look`, `buttons`, `batch`, and
`loss-recovery`; CTest registers all six. The tool constructs typed fixtures,
binds the exact 15-field schema, performs a bounded encode/decode round trip,
and prints aggregate geometry only. It opens no socket, contacts no server, and
is not stock interoperability evidence. The session target's scheduler,
history, planner, same-driver sequence context, unreliable carrier, and
fake-peer lifecycle remain covered by `hlclient_tests`.

The production `hlclient --stop-after usercmd-boundary` route remains an
intentional non-success diagnostic: it reaches the fail-closed stock evidence
boundary, reports zero sampled/history/transmitted usercmds, sends no usercmd
packet, and exits nonzero. The stock corpus currently has zero accepted runs
and zero verified move packets. See [GoldSrc usercmd](GOLDSRC_USERCMD.md),
[client-move message](GOLDSRC_CLIENT_MOVE_MESSAGE.md), and
[usercmd transmission](GOLDSRC_USERCMD_TRANSMISSION.md).

The stock research verifier defaults to a zero-process, zero-write pending
check:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  .\scripts\verify_stock_usercmd.ps1 -ValidateEvidencePending
```

An operational run requires an explicit isolated research copy and one named
bounded relay scenario:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  .\scripts\verify_stock_usercmd.ps1 `
  -ResearchHalfLifeRoot "C:\research\Half-Life-isolated" `
  -Game valve -Map boot_camp -Scenario Baseline
```

The other scenario names are `DropOneClientSequenced`,
`DropTwoConsecutiveClientSequenced`, `DropOneServerSequenced`,
`DuplicateOldClientSequenced`, and `ReorderTwoClientSequenced`. The harness
validates the exact process, window, and loopback endpoint owners; forwards
bytes through one learned client endpoint and one connected upstream socket;
writes only bounded structural metadata below ignored `manual-artifacts`; and
accepts a transport run only after complete restoration proves
`external-file-drift=none`. It injects no input and does not promote a capture
to accepted stock usercmd evidence without independent review.

## GoldSrc BSP collision checker

Build the CPU-only package/query targets and offline checker with the required
Win32 warnings-as-errors configuration:

```powershell
cmake --build build --config Debug --target `
  hlclient_collision_api hlclient_goldsrc_bsp_collision `
  hlclient_goldsrc_collision_scene hlclient_collision_check

.\build\bin\Debug\hlclient_collision_check.exe `
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve --map maps/crossfire.bsp `
  --scenario deterministic-probes
```

The checker accepts only a safe virtual map under an explicit user-owned root,
opens it through `LocalResourceEnvironment`, parses it once, and prints only
counts and structural/query SHA-256 values. It opens no socket and performs no
write, SDL, OpenGL, texture, or movement work. Verify multiple maps and all
three scenarios twice with:

```powershell
.\scripts\verify_local_bsp_collision.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_collision_check.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" `
  -Game valve `
  -Maps @('maps/boot_camp.bsp','maps/crossfire.bsp','maps/stalkyard.bsp')
```

Success requires deterministic summaries, nonzero structural/query work, and
`external-file-drift=none`. The production CPU stop spelling is
`--stop-after collision-world`; it requires the existing local consistency
provider and remains valid with `--renderer null`.

## Deterministic local movement tools

Build the movement libraries, read-only checker and viewer in the required
Win32 configuration:

```powershell
cmake --build build --config Debug --target `
  hlclient_movement_api hlclient_goldsrc_local_movement `
  hlclient_local_player_controller hlclient_movement_check `
  hlclient_world_viewer

.\build\bin\Debug\hlclient_movement_check.exe `
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve --map maps/crossfire.bsp `
  --scenario deterministic-route
```

The checker accepts `summary`, `spawn-settle`, `walk-forward`, `strafe-wall`,
`jump`, `step`, `duck`, `deterministic-route`, `wall-contact-stress`,
`wall-glance-stress`, `corner-contact-stress`, `jump-wall-stress` and
`duck-wall-stress`. It executes a scenario twice,
requires identical state/counter summaries and prints no raw position,
velocity or asset bytes.

Run the read-only three-map campaign with:

```powershell
.\scripts\verify_local_player_movement.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_movement_check.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" `
  -Game valve `
  -Maps @('maps/boot_camp.bsp','maps/crossfire.bsp','maps/stalkyard.bsp') `
  -Scenarios @('spawn-settle','walk-forward','jump','deterministic-route')
```

Success requires two equal summaries per route, zero network operations, zero
solid-start summaries, unchanged selected BSP metadata/hash, unchanged root
inventory and `external-file-drift=none`.

For visual testing use `hlclient_world_viewer` with
`--camera player-walk`; the complete command and controls are in
[PLAYER_WALK_VIEWER.md](PLAYER_WALK_VIEWER.md). An actual OpenGL 3.3 Core host
is required for graphical verification. Unavailable/legacy GL is a capability
skip, not a reason to skip CPU tests. Generic SDL startup failures and
allocation/resource exhaustion are not treated as capability skips.

Run the focused wall-contact CPU and scripted-OpenGL campaign with:

```powershell
.\scripts\verify_player_wall_contact_stability.ps1 `
  -ToolPath .\build\bin\Debug\hlclient_movement_check.exe `
  -ViewerPath .\build\bin\Debug\hlclient_world_viewer.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" `
  -Game valve `
  -Maps @('maps/boot_camp.bsp','maps/crossfire.bsp','maps/stalkyard.bsp') `
  -Iterations 20 `
  -Frames 1000
```

MSVC AddressSanitizer is opt-in and capability-checked:

```powershell
cmake -S . -B build-asan -G "Visual Studio 17 2022" -A Win32 `
  -DHLCLIENT_WARNINGS_AS_ERRORS=ON `
  -DHLCLIENT_ENABLE_ADDRESS_SANITIZER=ON

cmake --build build-asan --config Debug --target `
  hlclient_world_viewer hlclient_movement_check `
  hlclient_prediction_check hlclient_prediction_viewer `
  hlclient_collision_fixture_writer hlclient_tests

.\build-asan\bin\Debug\hlclient_tests.exe `
  '[prediction]~[.stress]'
.\build-asan\bin\Debug\hlclient_tests.exe `
  '[prediction][reconciliation][.stress]'
```

The option is off by default. It removes incompatible Debug runtime checks
only inside the sanitizer build tree and has no effect on ordinary Release
behavior. Run sanitizer executables from a Visual Studio 2022 Developer
PowerShell, or prepend the selected toolset's `bin\Hostx64\x86` directory (the
directory containing `clang_rt.asan_dynamic-i386.dll`) to that process's
`PATH`. A missing runtime can otherwise open a Windows loader dialog instead
of producing sanitizer output. See
[player wall-contact stability](PLAYER_WALL_CONTACT_STABILITY.md).

## Local prediction and reconciliation tools

M4.6.3.3 adds separate offline synthetic-authority tools. The ordinary
movement checker and world viewer remain non-prediction regression paths:

```powershell
cmake --build build --config Debug --target `
  hlclient_prediction_check hlclient_prediction_viewer hlclient_tests

.\build\bin\Debug\hlclient_prediction_check.exe `
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve --map maps/crossfire.bsp `
  --scenario delayed-authority --authority-delay-commands 8 `
  --commands 1000

.\build\bin\Debug\hlclient_prediction_viewer.exe `
  --basedir "D:\Steam\steamapps\common\Half-Life" `
  --game valve --map maps/crossfire.bsp `
  --scenario small-correction --authority-delay-commands 8 `
  --prediction-diagnostics summary --visibility pvs-frustum `
  --brush-submodels static --cull back
```

The checker and viewer read one safe virtual BSP through the existing local
resource sandbox, then use only an in-memory synthetic authority. They open no
socket, add no protocol message, and write no game file. Stock Protocol 48
authoritative acknowledgement/state extraction remains fail-closed as
`stock_protocol_48_authoritative_reconciliation_evidence_pending`.

Run the deterministic CPU and capability-gated OpenGL campaign with:

```powershell
.\scripts\verify_local_prediction_reconciliation.ps1 `
  -CheckerPath .\build\bin\Debug\hlclient_prediction_check.exe `
  -ViewerPath .\build\bin\Debug\hlclient_prediction_viewer.exe `
  -Basedir "D:\Steam\steamapps\common\Half-Life" -Game valve `
  -Maps @('maps/boot_camp.bsp','maps/crossfire.bsp','maps/stalkyard.bsp') `
  -Scenarios @('exact-authority','delayed-authority','small-correction',`
    'wall-replay','jump-replay','duck-replay','mixed') `
  -Iterations 20 -Frames 1000
```

The wrapper requires equal final-state and history/replay hashes, zero solid
starts, history overflow, network operations, created/deleted files, and
external-file drift. See [the prediction viewer](PREDICTION_VIEWER.md).
