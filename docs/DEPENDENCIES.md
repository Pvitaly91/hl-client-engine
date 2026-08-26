# Dependencies and licenses

## Pinning policy

Network-fetched source dependencies and the SDK reference are pinned either to
full immutable Git commit IDs or to an official release archive plus its
published cryptographic digest. Release tags are recorded for human review,
but CMake never trusts a movable tag alone. Updating a pin requires a dedicated
review of release notes, Win32/MSVC behavior, exported CMake targets, and
license changes.

The following pins were verified against official upstream repositories on
2026-08-13:

| Component | Release | Immutable commit | License | Integration |
| --- | --- | --- | --- | --- |
| SDL3 | `release-3.4.14` | `147a8ee32dbf9ac02f3794964490687b6bbda1bc` | `Zlib` | CMake `FetchContent`; shared `SDL3::SDL3-shared` target |
| bzip2 | `1.0.8` | official Sourceware archive, published SHA-512 `083f…b9f3` | `bzip2-1.0.6` | CMake `FetchContent`; static project target with stdio API disabled |
| Catch2 | `v3.15.3` | `8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb` | BSL-1.0 | CMake `FetchContent`; `Catch2::Catch2WithMain` and `catch_discover_tests` |
| GLAD2 | `v2.0.8` | `73db193f853e2ee079bf3ca8a64aa2eaf6459043` | generated-file expression described below | generated C source committed and built statically |
| Valve Half-Life SDK | reference snapshot | `b1b5cf5892918535619b2937bb927e46cb097ba1` | custom Valve Half-Life 1 SDK License | Git submodule; `SYSTEM` headers/reference only |

The project also links the Windows SDK/OpenGL system import library through
CMake's `OpenGL::GL` target and links Winsock2 where required. Those operating
system components are not vendored or redistributed by this repository.

M3.2.1 through M4.3 add no third-party dependency. `hlclient_hash_md5` is an
independently authored C++20 incremental compatibility module and does not use
OpenSSL, Windows CryptoAPI, or another crypto package. MD5 is present only to
reproduce GoldSrc compatibility material; it is not approved for security,
authenticity, integrity, or trust decisions. `hlclient_local_resources` uses
private Windows SDK file-handle APIs (`CreateFileW` and handle
metadata/final-path queries) for
the Win32 read-only sandbox. Those APIs do not add a redistributed runtime.

The local-resource target direction remains project-owned and acyclic:

```text
hlclient_hash_md5 -> hlclient_core
hlclient_local_resources -> hlclient_core + private Win32 system APIs
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
hlclient_world_render_api -> hlclient_asset_api
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
    -> hlclient_glad + OpenGL::GL
```

`hlclient_goldsrc_indexed_texture`, `hlclient_goldsrc_bsp`,
`hlclient_goldsrc_wad3`, `hlclient_goldsrc_world_texture_import`,
`hlclient_goldsrc_world_textures`,
`hlclient_goldsrc_lightmaps`, and the world-render, spatial, visibility,
brush-model, scene-package, and preview modules use only project-owned C++20
code and existing project contracts. M4.4 adds no external dependency: no
image, packing, math, shader-file, BSP/PVS, entity, or scene-graph library is
introduced. The
OpenGL renderer reuses the already pinned SDL3, GLAD2, and system OpenGL
boundaries. The pinned Valve SDK's public BSP/WAD tool declarations were
reviewed as reference evidence, but no SDK source is compiled into these
targets and no SDK struct is used as a wire ABI. In particular, the sign-on
target does not link the filesystem or concrete local-provider implementation,
the resolver does not link the netchan driver, and the MD5 target does not
depend on GoldSrc protocol types.

The offline world-texture checker and world viewer link the CPU-only
`hlclient_goldsrc_world_texture_import` target. They do not link the
same-session stage target, so their generated executable closure contains no
`hlclient_network`, netchan, sign-on, resource-transition, asset-dispatch
stage, or Winsock dependency. `hlclient_goldsrc_lightmaps` links only
`hlclient_asset_api`; its implementation consumes inline BSP v30 wire
constants without linking the BSP parser library.

The BSP parser target does not link Studio or sprite; the separate
`hlclient_goldsrc_builtin_importers` composition target owns the one canonical
caller-owned production registrar for all three categories. Consequently,
world-only BSP consumers do not inherit the visual importer libraries.

The offline `hlclient_goldsrc_asset_check` links that registrar and
`hlclient_goldsrc_visual_asset_bundle` through the interface-only
`hlclient_goldsrc_approved_asset_source_api` seam. It does not link
`hlclient_goldsrc_asset_dispatch`, readiness, sign-on, netchan,
`hlclient_network`, or Winsock. The same visual operation accepts either the
manifest-approved capability or a verified exact-root `LocalAssetSource`; both
routes use the canonical cross-category dispatcher and invoke only its selected
registered importer.

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

The client and offline world viewer have post-build deployment rules equivalent
to copying `$<TARGET_FILE:SDL3::SDL3-shared>` into their target-file
directories. Generator expressions select the correct Debug, Release, or
RelWithDebInfo Win32 DLL, so Visual Studio F5 and the viewer do not depend on
`PATH` or a manually installed SDL runtime.

SDL3 uses the permissive zlib license (SPDX identifier `Zlib`). Preserve its
license notice in source or
binary redistributions as required by that text; the project MIT license does
not replace it.

## bzip2

- Official release directory: <https://sourceware.org/pub/bzip2/>
- Official archive: <https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz>
- Official digest list: <https://sourceware.org/pub/bzip2/sha512.sum>
- Pinned SHA-512:
  `083f5e675d73f3233c7930ebe20425a533feedeaaa9d8cc86831312a6581cefbe6ed0d08d2fa89be81082f2a5abdabca8b3c080bf97218a1bd59dc118a30b9f3`

The stock Protocol 48 initial service batch is carried in a captured `BZ2\0`
envelope followed by one standard bzip2 stream. `hlclient_bzip2`, exposed as
`hlclient::bzip2`, compiles the official 1.0.8 decoder sources into a static
library. `BZ_NO_STDIO` is defined for the library and its consumers, so the
file-oriented convenience API is absent. Production sign-on code uses only the
bounded streaming memory API, rejects bytes after `BZ_STREAM_END`, and never
derives or opens a path from server data.
The small project-owned `bzip2_no_stdio.c` hook implements the invariant
callback required by upstream's no-stdio configuration as a no-output
fail-fast abort; normal malformed-input failures still return typed decoder
errors through the public bzip2 API.

The bzip2 sources use the permissive license represented by SPDX identifier
`bzip2-1.0.6`. Preserve the upstream license notice with source or binary
redistributions as required by that text; the repository's MIT license does
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

M4.5.1 adds no third-party dependency. `hlclient_goldsrc_delta_values`,
`hlclient_goldsrc_entity_snapshots`, and
`hlclient_goldsrc_post_resource_signon` are project-owned C++20 static targets.
They reuse the existing delta schema and netchan APIs and do not link SDL,
OpenGL, filesystem, `AssetManager`, game DLLs, Steam libraries, HLSDK object
code, or a secondary engine. The pinned Valve SDK declarations remain a
semantic cross-check only and are not a runtime wire proof.

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

For normal M3.2.1–M4.4 runtime, the local consistency provider, readiness
environment, and approved selected-world source opener may read an explicit
user-owned installation supplied through `--basedir` and `--game`. That is an
opt-in, read-only runtime input, not a build dependency: there is no Steam
library/registry/environment auto-discovery, stock executable launch,
configuration mutation, download, cache write, or renderer use. M3.2.3 may read
one selected world file and dispatch its owning bytes to explicitly registered
importers; M4.1 production registers the project-owned `goldsrc-bsp-v30`
importer and retains neutral CPU geometry plus texture-reference metadata.

Only the explicit M4.2 `world-textures` route follows dependent texture
metadata. It retains the already approved BSP bytes, reduces compiler WAD
references to safe basenames, resolves those names through the existing
game-before-`valve` sandbox, and opens at most one verified WAD source at a
time. The output owns neutral RGBA8 mip levels/bindings and retains no source
bytes, palette, file handle, resource locator, compiler path, or native path.
Earlier stop points still open no WAD. The consistency provider's independent
fixed target remains `tempdecal.wad`, selected by its compatibility profile
rather than by server bytes or an arbitrary CLI path.

The M4.3 continuation reads lightmap bytes only from the already approved BSP
source, then transfers owning CPU geometry, textures, atlases, and materials
into a path-free `WorldRenderPackage`. `--view-world` cleans up the retained
network/authentication lifetime before OpenGL upload. The standalone viewer
starts from an explicit safe virtual map under the same read-only sandbox,
performs no network or writes, and does not turn user assets into build
dependencies. No game asset or screenshot is committed.

M4.4 reuses those already approved BSP bytes and already opened declared WAD
sources to build canonical spatial/PVS state and, only when explicitly
selected, an aggregate static brush render library. Its owning scene retains no
raw BSP/PVS/entity bytes, locator, file handle, compiler path, or native path.
Changing camera visibility does not reopen an asset and does not re-upload the
scene resources. No additional game file category or write/cache policy is
introduced.

M4.5.2 adds no third-party library and does not build or link an SDK tool. The
project-owned Studio and sprite parsers use the pinned public Half-Life SDK
only as reviewed format/compiler evidence for record sizes, strip/fan winding,
texture layout, and animation RLE semantics. Production code does not include
or ABI-cast SDK `mstudio*`/`dsprite*` structs. Automated MDL/SPR sources are
small project-created literal fixtures; installed game models and sprites
remain optional, read-only, user-owned verification inputs and are never
committed or fetched.

The M4.5.2 target set is `hlclient_model_asset_api`,
`hlclient_goldsrc_studio_model`, `hlclient_goldsrc_sprite`,
`hlclient_goldsrc_builtin_importers`,
`hlclient_goldsrc_approved_asset_source_api`,
`hlclient_goldsrc_visual_asset_bundle`, and the offline
`hlclient_goldsrc_asset_check`. All are project-owned C++20/API composition;
none adds a redistributed runtime or an SDK linkage.

The entity renderer reuses the existing pinned SDL3, GLAD/OpenGL 3.3, Catch2,
and public Half-Life SDK reference material. It adds no runtime dependency and
does not link SDK structs or implementation. Valve sources are consulted only
as public rendering/math evidence; project-owned neutral records remain the ABI.

M4.6.1 adds no dependency. It reuses the pinned SDL3 event, scancode, mouse,
focus, and relative-mode APIs already required by the platform/window target.
SDL types remain private to `hlclient_platform_sdl_input`/`hlclient_platform`;
gameplay input, camera, client state, and renderer targets do not expose them.

This runtime policy is distinct from active stock-client/HLDS research. Research
verifiers continue to require their isolated marked copy and reject primary or
registered Steam roots where their existing contracts specify that behavior.
Allowing explicit read-only runtime access does not relax research isolation,
and a local provider check does not by itself establish stock interoperability.

## Updating a dependency

For each update:

1. choose an official upstream release and resolve it to its full commit ID;
2. read the upstream release notes and compare license files;
3. verify CMake target names and Visual Studio 2022 Win32 support;
4. configure, build, run CTest, and verify F5/runtime deployment in Debug;
5. repeat build/test in Release and, when relevant, RelWithDebInfo;
6. update the pin, this document, and any retained third-party notices together;
7. never replace an immutable pin with `master`, `main`, or another floating ref.
