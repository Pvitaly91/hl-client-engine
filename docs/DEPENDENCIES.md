# Dependencies and licenses

## Pinning policy

Network-fetched source dependencies and the SDK reference are pinned to full,
immutable Git commit IDs. Release tags are recorded for human review, but CMake
uses commits so a moved tag cannot silently change a build. Updating a pin
requires a dedicated review of release notes, Win32/MSVC behavior, exported
CMake targets, and license changes.

The following pins were verified against official upstream repositories on
2026-08-13:

| Component | Release | Immutable commit | License | Integration |
| --- | --- | --- | --- | --- |
| SDL3 | `release-3.4.14` | `147a8ee32dbf9ac02f3794964490687b6bbda1bc` | `Zlib` | CMake `FetchContent`; shared `SDL3::SDL3-shared` target |
| Catch2 | `v3.15.3` | `8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb` | BSL-1.0 | CMake `FetchContent`; `Catch2::Catch2WithMain` and `catch_discover_tests` |
| GLAD2 | `v2.0.8` | `73db193f853e2ee079bf3ca8a64aa2eaf6459043` | generated-file expression described below | generated C source committed and built statically |
| Valve Half-Life SDK | reference snapshot | `b1b5cf5892918535619b2937bb927e46cb097ba1` | custom Valve Half-Life 1 SDK License | Git submodule; `SYSTEM` headers/reference only |

The project also links the Windows SDK/OpenGL system import library through
CMake's `OpenGL::GL` target and links Winsock2 where required. Those operating
system components are not vendored or redistributed by this repository.

The bootstrap intentionally does not add Boost, Asio, Qt, ImGui, GLM, OpenAL,
FMOD, Vulkan, DirectX renderer code, protobuf, or a JSON framework. UDP remains
a small project-owned abstraction over Winsock2 on Windows and BSD sockets on a
future portable backend so GoldSrc wire behavior stays explicit.

## SDL3

- Official repository: <https://github.com/libsdl-org/SDL>
- Official release: <https://github.com/libsdl-org/SDL/releases/tag/release-3.4.14>
- Pinned commit: <https://github.com/libsdl-org/SDL/commit/147a8ee32dbf9ac02f3794964490687b6bbda1bc>
- License text: <https://github.com/libsdl-org/SDL/blob/release-3.4.14/LICENSE.txt>

SDL3 is configured as a shared library and linked privately by the platform
layer through `SDL3::SDL3-shared`. Upstream's concrete build target is
`SDL3-shared`. SDL tests, examples, and install rules are disabled for this
project. Its targets are placed below `ThirdParty/SDL3` in Solution Explorer.
It is needed for the window, event/input abstraction, OpenGL context and buffer
presentation, timing, and the planned portable audio backend.

The executable has a post-build deployment rule equivalent to copying
`$<TARGET_FILE:SDL3::SDL3-shared>` into `$<TARGET_FILE_DIR:hlclient>`. Generator
expressions select the correct Debug, Release, or RelWithDebInfo Win32 DLL, so
Visual Studio F5 does not depend on `PATH` or a manually installed SDL runtime.

SDL3 uses the permissive zlib license (SPDX identifier `Zlib`). Preserve its
license notice in source or
binary redistributions as required by that text; the project MIT license does
not replace it.

## Catch2

- Official repository: <https://github.com/catchorg/Catch2>
- Official release: <https://github.com/catchorg/Catch2/releases/tag/v3.15.3>
- Pinned commit: <https://github.com/catchorg/Catch2/commit/8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb>
- License text: <https://github.com/catchorg/Catch2/blob/v3.15.3/LICENSE.txt>

`hlclient_tests` links `Catch2::Catch2WithMain`. CMake appends Catch2's `extras`
directory, includes its official `Catch.cmake` integration, and registers cases
with `catch_discover_tests`. Catch2 is fetched only when `BUILD_TESTING` is on;
its own tests, development mode, docs, extras installation, and project install
rules are not built.

Catch2 is needed only for the deterministic unit/integration test executable.
The annotated `v3.15.3` tag object is
`95d8a61b089317bec800c7cc4c64064cbcb3802d`; it peels to the commit pinned by
CMake, `8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb`.

Catch2 is licensed under the Boost Software License 1.0 (`BSL-1.0`). Its license
scope remains independent of the project's MIT license.

## GLAD2 and OpenGL

- Official repository: <https://github.com/Dav1dde/glad>
- Official release: <https://github.com/Dav1dde/glad/releases/tag/v2.0.8>
- Pinned generator commit: <https://github.com/Dav1dde/glad/commit/73db193f853e2ee079bf3ca8a64aa2eaf6459043>
- Upstream license file: <https://github.com/Dav1dde/glad/blob/v2.0.8/LICENSE>

The repository vendors only deterministic generated loader output in
`third_party/glad`, not a FetchContent build of the generator. The generation
contract is:

```text
language: C
API: gl:core=3.3
extensions: none
built-in platform loader: enabled
reproducible mode: enabled
generator: GLAD2 v2.0.8 at 73db193f853e2ee079bf3ca8a64aa2eaf6459043
```

Equivalent generator arguments are recorded beside the output:

```text
--api gl:core=3.3 --extensions <empty-file> --reproducible c --loader
```

Committing generated output avoids making Python and Jinja2 part of the normal
Visual Studio configure path. If the loader is regenerated, use the exact pin
and arguments, review the diff and SPDX headers, compile it as C, and update the
record only in the same reviewed change.

The generated loader is a static `hlclient_glad` target exposed internally as
`hlclient::glad`; it has no runtime DLL. The OpenGL renderer also links
`OpenGL::GL`. On Windows this resolves to the platform OpenGL import library,
while actual OpenGL 3.3 support comes from the graphics driver.
GLAD is needed because the Windows system OpenGL import library does not expose
modern Core-profile entry points directly.

GLAD licensing needs more precision than a single `MIT` label:

- the GLAD generator source is MIT licensed;
- Khronos specifications carried by the generator are Apache-2.0 licensed;
- EGL specification/header material has a separate permissive MIT-style grant;
- the generated `gl.h` and `gl.c` in this repository declare
  `SPDX-License-Identifier: (WTFPL OR CC0-1.0) AND Apache-2.0`.

Keep `third_party/glad/LICENSE` and the SPDX headers with redistributed generated
files. The repository's top-level MIT license does not replace these terms.

## Valve Half-Life SDK

- Official repository: <https://github.com/ValveSoftware/halflife>
- Pinned commit: <https://github.com/ValveSoftware/halflife/commit/b1b5cf5892918535619b2937bb927e46cb097ba1>
- License at the pin: <https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/LICENSE>

The SDK is a Git submodule at `third_party/halflife-sdk`. CMake exposes selected
SDK include directories through `hlclient_hlsdk_headers` /
`hlclient::hlsdk_headers` as `SYSTEM` includes. It does not build the SDK's game
DLLs, tools, legacy Visual Studio projects, prebuilt libraries, or bundled SDL2.
Those files exist inside the upstream submodule but are outside the
`hl-client-engine` dependency graph.

The SDK snapshot is needed as the official API/ABI and compatibility reference
for selected declarations under `common`, `engine`, `pm_shared`, `cl_dll`, and
`public`. It is not the source of this client's runtime architecture.

The Valve Half-Life 1 SDK license has no SPDX identifier and is not MIT, Zlib,
BSL-1.0, or a generally permissive open-source license. Among other terms, it
limits the licensed use to
developing a modified Valve game running on the Half-Life 1 engine, permits SDK
and modified-game distribution only for free, requires preservation of its
license and notices for SDK distributions, and directs commercial-use requests
to Valve. Consult the complete upstream `LICENSE`; this summary is not legal
advice.

Consequences for this repository:

- never describe the SDK submodule as covered by the top-level MIT license;
- do not copy SDK implementation sources into project-owned modules;
- do not ship an SDK-derived binary or substantial SDK content without a
  specific license review and all required notices;
- keep the submodule pin and its `LICENSE` file intact;
- keep the SDK on an isolated reference/header boundary;
- do not use the SDK's bundled SDL2 instead of the project's pinned SDL3.

## Game and Steam assets

No Steam or Half-Life installation content is a project dependency fetched by
CMake, and none may be committed merely to make a test pass. BSP, WAD, MDL,
sprites, sounds, textures, `client.dll`, and other game files remain supplied by
the user under the terms governing their copy. Automated tests must use small,
original test fixtures or synthetic byte sequences.

## Updating a dependency

For each update:

1. choose an official upstream release and resolve it to its full commit ID;
2. read the upstream release notes and compare license files;
3. verify CMake target names and Visual Studio 2022 Win32 support;
4. configure, build, run CTest, and verify F5/runtime deployment in Debug;
5. repeat build/test in Release and, when relevant, RelWithDebInfo;
6. update the pin, this document, and any retained third-party notices together;
7. never replace an immutable pin with `master`, `main`, or another floating ref.
