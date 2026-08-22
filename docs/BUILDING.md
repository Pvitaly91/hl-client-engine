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
build\bin\Debug\SDL3.dll
build\lib\Debug\...
```

The SDL3 shared-library target is built from the pinned source and a post-build
command copies its configuration-matching DLL next to `hlclient.exe`. This is
also done for Release and RelWithDebInfo. GLAD2 is a static project target and
has no runtime DLL. Windows supplies `opengl32.dll`; the installed graphics
driver must provide an OpenGL 3.3 Core-capable implementation.

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

### F5 reports a missing SDL3 DLL

Build `hlclient` for the currently selected solution configuration. The
post-build deployment runs when that target is built. Confirm that both
`hlclient.exe` and `SDL3.dll` are in `build\bin\<Configuration>`. Do not copy a
DLL from another architecture or configuration.

### OpenGL context creation fails

Update the native GPU driver and avoid Remote Desktop/software-display setups
that expose only a legacy OpenGL implementation. The project requests an
OpenGL 3.3 Core context. GLAD loads function pointers after the SDL context
exists; it does not provide the driver itself.
