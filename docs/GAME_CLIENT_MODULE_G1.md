# G1: engine and Half-Life client module

Result: `engine_game_boundary_integrated_offline_verified` (2026-09-27).
Executed in `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`; HEAD remained
`ea738a80e5a66a500c245f95b96eed32198e2d66`. Existing work was preserved;
no staging, commit or push was performed.

The normal `hlclient.exe` composes one `GameClientHost` with
`make_half_life_client_module()`. Existing Half-Life message, inventory,
presentation, lifecycle and input/movement policy work executes in
`hlclient_game_halflife`. The executable still uses
`IClientSceneSource -> ClientWorldState -> RenderScene -> IRenderer`.
There is one platform pump, clock source, connection driver and application
loop. This boundary is static C++ code in the same executable.

## Ownership and dependency direction

```text
apps/hlclient/main.cpp                 composition root
    +--> hlclient_game_halflife ------> hlclient_game_api
    |        +--> shared pure client/input/movement/command mechanisms
    +--> hlclient_game_client_host --> hlclient_game_api
    +--> existing live/replay host --> hlclient_game_client_host
    +--> existing scene/assets/renderers

lower engine targets -X-> hlclient_game_halflife
```

`hlclient_game_api` is the contract target; `hlclient_game_client_host` owns
the module and forwards typed calls. `hlclient_game_policy` validates an
explicit movement policy without constructing Half-Life defaults. The
concrete target has no dependency on the live/session/asset host. The small
`hlclient_goldsrc_client_message` target retains bounded client-message wire
encoding, separate from sign-on/session ownership.

| Previous owner | Current owner | Shared mechanism retained |
| --- | --- | --- |
| `src/goldsrc/runtime_replay_session.cpp` HUD/game body interpretation | `src/games/halflife/server_messages.cpp` | Service framing, registration, delta/clientdata/weapondata/entity reconstruction, provenance, atomic commit |
| `src/client/local_player_lifecycle.cpp` | `src/games/halflife/local_player_lifecycle.cpp` | Owning observation publication and bounded history |
| Weapon/inventory selection in GoldSrc helpers and application | `src/games/halflife/inventory.cpp` | Typed request validation and existing reliable transport |
| `src/app/local_weapon_presentation.cpp` | `src/games/halflife/local_weapon_presentation.cpp` | Imported sequence/body metadata and Studio evaluation |
| Asset provider's weapon binding/animation selection | `src/games/halflife/presentation.cpp` | `RuntimeReplayLocalAssets::presentation_model` and `materialize_viewmodel` |
| Main HUD meaning plus renderer labels/layout/colors | `HalfLifePresentation::hud` | Bounded glyph/rectangle rasterization |
| Live camera health/life/punch interpretation | `HalfLifePresentation::camera` | Input orientation, exact angle-correction identity, geometric camera and publication |
| Live fixed input/speed/button/profile selection | `src/games/halflife/movement_policy.cpp` | Input tracking/builders, scheduler, quantization, collision, movement kernel and H3/H4 replay/interpolation |
| Existing B/B1/C scripted decisions and action evidence | `scenario_policy.cpp`, `action_evidence.cpp`, game-owned script headers | Host clocks, command queueing, RX/TX, receipts and process summaries |

Public contracts live under `include/hlclient/game_api/`; concrete headers
under `include/hlclient/games/halflife/`. Only application/test composition
includes the concrete factory. No downcast selects behavior in the host.
The generic asset provider and renderer include only the neutral presentation
value header, which contains STL types and no protocol/host/native handles.

`cmake/GameModuleBoundary.cmake` inspects actual target sources and direct and
transitive dependencies, including aliases, conditional generator-expression
links, object libraries and intermediate targets. It follows project includes
to reject a concrete Half-Life header in a lower target. Configure emits
`game-boundary-manifest.txt` in the chosen build tree. Architecture fixtures
exercise the allowed API route and rejected direct, alias, transitive,
conditional, source, object and include routes.

## API, transaction and lifetime contract

`IGameClientModule` accepts a `GameSessionIdentity`, committed observation read
views, framed `GameMessageView` values, submitted command notifications and
model metadata. It returns owning game state, presentation intents and typed
requests. It receives no mutable `ClientWorldState`, socket, SDL/OpenGL/Win32
handle, Steam interface or SDK ABI structure.

Game command requests have two existing variants: inventory selection and
the explicit C self-kill check. The shared encoder accepts only a bounded
`weapon_...` token or the exact `kill` token for its matching variant, rejects
separators/control characters and unknown variants, and submits through the
existing host queue. The module has no raw-send or arbitrary-console service.

The service decoder still owns user-message numeric ID/name/size registration
and bounds the exact body. The module dispatches by the registered name;
Health, Battery, CurWeapon, AmmoX, WeaponList, Damage, DeathMsg, HideWeapon,
ResetHUD, InitHUD and SetFOV retain their existing semantics. Standard
`svc_weaponanim` framing remains in the protocol layer and its sequence/body
presentation interpretation crosses the same game-input boundary. Unhandled
registered game messages remain explicitly ignored by Half-Life semantics;
the strict service decoder retains its existing unknown-service failure.
There is no opcode scanning, resynchronization or stufftext execution.

`GameMessageView::name/body` are immutable borrows valid only during the
synchronous `stage_record()` call. Retained strings, catalogues and life events
own their storage. Protocol record/body limits still bound input; game results
pass the existing observation validator, including the bounded life-event and
source-provenance checks. The module stages only the game projection, not a
second decoder/network state.

The concrete module owns its committed HUD/lifecycle projection. Subsequent
staging reads that owner; the previous protocol observation supplies only
protocol values and provenance. An external previous HUD cannot replace the
module's committed state. The world holds the validated immutable publication.

The publication sequence is:

1. Decode the complete candidate record and reconstruct neutral observations.
2. Call `stage_record()` without mutating committed module state.
3. Validate game output, canonical projection and staged world publication.
4. Call non-allocating `commit_record()` at the existing decoder/world/history
   move-commit boundary.

A malformed suffix, invalid game body, failed allocation or rejected world
candidate commits no game state or effects. Errors propagate as the existing
typed replay failure with context. Duplicate records retain the existing
rejection/identity contract. Fresh, retained and unavailable metadata remain
distinct; movement replay and repeated rendering do not resubmit actions.
Wire grammar, command bytes, provenance cursors and canonical hash profiles
are unchanged by relocation.

Initial creation and generation reset stage with `previous == nullptr` and an
empty game projection. Only after successful candidate validation does the
host reset the module and commit that new state. A failed generation reset
therefore retains the old session. `GameClientHost` owns one module instance;
explicit teardown is idempotent and destruction tears down once. Reset permits
reuse for a new identity. There is no mutable process-global module.

Network generation, map generation, life epoch, action identity and
presentation revision are distinct contracts. Current runtime initialization
uses the same numeric value for the separate network/map generation fields;
independent map transitions are not newly implemented here. Death/respawn
retains netchan sequence, socket, schemas, delta history and imported assets;
the existing game life transition resets only its eligible prediction and
presentation state.

## Presentation and movement

The Half-Life controller retains Glock/crowbar eligibility, sequence/body
choices, confirmation precedence, restart identities, cooldowns and visual
recoil. HUD ammo/health remain server-derived; provisional animation never
establishes hit or damage authority. First click still captures, focus/release
and pending presses retain their order, and committed attack/reload wire bits
are not rewritten by presentation.

`ViewmodelIntent` owns generation/revision, model slot, sequence, body and frame
coordinate. The provider resolves the imported slot, evaluates Studio pose and
material support, and builds the existing camera-local identity transform.
Body selection and material support remain separate checks. It knows no
Glock/crowbar sequence meaning. Renderer support for Studio chrome stays shared.

`HudState` exposes read-only diagnostic values and neutral draw commands.
The Half-Life module chooses labels, visibility, layout and colors. The
renderer rasterizes at most eight rectangles and eight owning text commands
of at most 160 bytes each; it rejects oversized payloads before caching.
`CameraIntent` selects view eligibility, whether predicted translation may be
used, and bounded canonical/local punch offsets. Camera math, local look and
one-shot typed angle corrections remain shared mechanisms.

The session obtains its immutable `GameMovementPolicy` from the module. The
Half-Life factory explicitly selects the existing bindings, 0.10-degree mouse
scales, 400-unit direction speeds, 0.3 Shift multiplier, jump/duck/primary/reload
button policy and the current dry-walk movement/environment profile. Missing
or zero MoveVars remain server observations and are not replaced with guessed
values. A default-constructed game policy is unavailable; live configuration
validates the supplied module policy. The 20 ms cadence, retained quantized
commands, collision queries and H3/H4 kernel/replay behavior remain unchanged.

This is selection of the existing bounded kernel, not a universal simulation
extension API. Different future physics may require wider simulation hooks;
CS is not implemented. Existing observation DTOs also retain compatibility
names such as `RuntimeWeaponHudObservation`; reusing them avoids parallel
copies of every snapshot but does not claim universal mod semantics.

The common input library still provides its existing project default key
table; Half-Life explicitly selects it, while the live host receives bindings
only from the selected module. Existing C diagnostic DTOs retain Glock/crowbar
names for summary compatibility, and old motion-proof diagnostics retain their
positive-health eligibility check. These are residual compatibility coupling,
not weapon/life simulation authority in the host.

The shipped composition selects Half-Life explicitly, without a new manual
flag. `--game` still chooses an asset search root for generic viewers; it is
not a gameplay-module selector. A core-only executable reports that its
required gameplay module is absent instead of silently constructing Half-Life.
There is no dynamic loader, DLL ABI, IPC or scripting interface.
Server-advertised game negotiation and mod autodetection are not implemented.

## Reproducible build and offline checks

Run from the repository with CMake and the VS2022 Win32 toolchain available.
Normal builds use the current `build` tree; these commands do not clean it:

```powershell
cmake --preset vs2022-win32 -DHLCLIENT_BUILD_GAME_HALFLIFE=ON
cmake --build build --config Debug --target hlclient hlclient_tests hlclient_core_api_tests hlclient_stock_runtime_orchestrator hlclient_stock_runtime_check --parallel 4
cmake --build build --config Release --target hlclient hlclient_tests hlclient_core_api_tests hlclient_stock_runtime_orchestrator hlclient_stock_runtime_check --parallel 4
```

Core-only verification uses the separate `build-core-g1` tree, with the
already-present public dependency sources. It neither replaces the normal
Release output nor needs installed Half-Life or private captures:

```powershell
$repo = 'D:\DEV\CPP\HLC-steamcfg-5e48b7c1'
cmake -S $repo -B "$repo\build-core-g1" -G 'Visual Studio 17 2022' -A Win32 -DHLCLIENT_BUILD_GAME_HALFLIFE=OFF -DBUILD_TESTING=ON -DHLCLIENT_ENABLE_ADDRESS_SANITIZER=OFF '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /MP4' "-DFETCHCONTENT_SOURCE_DIR_BZIP2=$repo/build/_deps/bzip2-src" "-DFETCHCONTENT_SOURCE_DIR_CATCH2=$repo/build/_deps/catch2-src" "-DFETCHCONTENT_SOURCE_DIR_SDL3=$repo/build/_deps/sdl3-src"
cmake --build "$repo\build-core-g1" --config Release --target hlclient_engine_core hlclient_core_api_tests --parallel 4
ctest --test-dir "$repo\build-core-g1" -C Release -R '^(hlclient_core_api_offline|hlclient_game_boundary_guard)$' --output-on-failure
```

On a fresh source checkout the ordinary preset obtains the pinned public
dependencies; the explicit source overrides above are the local offline reuse
form. No private asset is a build dependency. All required source, headers,
tests, CMake guards and contracts are ordinary repository files.

`tests/test_game_client_module.cpp` supplies a project-owned alternate module
to the same production host and replay transaction. It uses independent
messages, bindings/speeds, sequence 13, HUD text and camera offsets, and tests
owning lifetime, failure atomicity, duplicate notifications, reset and teardown
without creating Half-Life. `test_game_camera_intent.cpp` independently checks
that the shared camera follows supplied policy even when Half-Life eligibility
would differ. This proves substitution at this API, not CS support.

## Evidence and manual handoff

The pre-edit snapshot preserved 83 dirty/untracked source files; all hashes
still match. Inventory SHA-256:
`AF15DA4925F44BFA53FF7DBE813C2F94144A623562A97FF4E1871B8641EA12E1`.
The pre-edit Release baseline passed 188 cases / 37,814 assertions with one
explicit optional-local-assets skip (seed 2532982175).

Completed G1 gates:

- Core-only engine libraries and executable built; 93 core/API cases and
  1,318 assertions passed without Half-Life linkage or installed assets.
  The same 93 cases passed under ASan Debug.
- All eight architecture guard fixtures passed. Generated core projects,
  objects, linker inputs and compiler dependency logs contain zero concrete
  Half-Life dependencies. The normal module's dependency closure has no
  signon/session/live/replay host backedge.
- Normal Debug, Release and ASan Debug builds passed. In each configuration,
  focused tests passed 202 cases with one optional-local-assets skip and
  38,111 assertions. New module/controller/transaction expectations
  coexist with unchanged B/B1/A1/H3/H4/C regression expectations.
- Actual Release OpenGL controls: three cases / 356 assertions passed.
  Supplemental read-only local first-person model control: one case /
  260 assertions passed.
- Eight network-free application replay tests passed, including the original
  basic-mixed canonical hash `18197719904158225405`.
- Existing private capture read-only control: 323/323 records, zero errors,
  canonical hash `7014210005320501317` unchanged; it is not a core dependency.
- Native log observation, PowerShell failure retention and absolute-path
  fake-process publication roundtrip passed; stock launch absent.
- Launcher `-CheckOnly` passed from `C:\Windows\System32`.

- The single final allowed Release offline suite passed 2,087 cases /
  386,671 assertions, with 21 capability/opt-in skips. Excluded categories:
  udp, network, isolation, orchestrator, steam, live, loss, security.
  Skips concerned optional assets, symlink/reparse/second-volume capabilities
  and hidden SDL focus/relative-mouse support; they were not marked passed.
- Normal Debug/Release core/API executables also passed all 93 cases /
  1,318 assertions. Source whitespace checks passed and staging stayed empty.

Final `build/bin/Release/hlclient.exe` SHA-256:
`2821F1BA2D6E2136A8458B2163A573871493ED977E920FB5F638C4C8641631D0`.
The final new-file whitespace check removed two trailing blank lines; Release
was refreshed and its 202 focused cases / 38,111 assertions passed again.
This was formatting only; the broad offline suite was not repeated.
Detailed local command logs are under `manual-artifacts/task-records/g1-*`;
no required implementation is stored there. Offline/fake-peer checks are
separate from stock or manual validation.

Preserved `attribution=user_report`: smooth WASD/Shift/jump/duck/crouch-walk,
HUD and Glock/crowbar selection, viewmodel attachment through pitch/yaw,
Glock fire/reload animations and crowbar swing. Separate visible-recoil
confirmation was not reported. Historical reports retain their original
attribution and budgets. The newly refactored executable requires a new
manual test: `manual_validation=not_run`.

The unchanged next manual command is:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

One invocation remains one managed session. The launcher also retains the
existing `-Prediction off` alternative. G1 itself performs zero new managed,
HLDS or stock-client sessions, zero Steam initialization/live connections and
zero WFP activations. Capture/ETW/campaign and new manual validation are
`not_run`; commit/push are `none`. Previous B1/C budgets are not renewed.

## E2 additive API extension

`IGameClientModule::drain_audio()` / `GameClientHost::drain_audio()` now carry
fixed owning local sound cues and preload references. The host stamps its
reset lifetime; the concrete module owns action, life/switch cancellation and
Studio event semantics. Core audio only consumes neutral references, channel
intent and due times through the existing E1 pipeline. The alternate project
module emits a distinct test sample through the same method, with no HL1
factory. See [E2 contract](LOCAL_WEAPON_AUDIO_E2.md). Earlier G1 results above
are historical, not rerun or relabelled as E2/live verification.
