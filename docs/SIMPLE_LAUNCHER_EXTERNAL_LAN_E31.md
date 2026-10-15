# E31 — simple launcher and external LAN server

Task: `HLC-E31-SIMPLE-LAUNCHER-EXTERNAL-LAN-SERVER`.
Status: E31 baseline offline complete; E31.1 fixes the observed Linux challenge
suffix and preserves precise startup diagnostics. The new candidate connects
and is playable according to the user; final offline verification is complete.
Result: `simple_launcher_external_lan_play_integrated`.
Current candidate: `manual=passed`, `attribution=user_report`,
`scope=connection_and_playability_on_user_LAN_server`;
`external_server_live_verified=true` for this scope only;
agent-initiated gameplay sessions=0.

## Image/audio slider wheel follow-up — 2026-10-15

`attribution=user_report`: the highlighted brightness, gamma, contrast and
volume sliders should not change when the user scrolls the launcher form.
The ordinary native trackbar procedure previously consumed `WM_MOUSEWHEEL`,
even though the parent form already had smooth scrolling.

The existing child-control subclass now forwards vertical wheel messages
from these four exact controls once to the form, bypassing the native value
change. Horizontal wheel input is consumed without changing their values.
Focused/unfocused controls use the same rule; deliberate dragging, keyboard
arrows, reset and save remain ordinary controls. HUD scale and other controls
are outside this requested change. No configuration migration or game setting
change is introduced, and the previous smooth-scroll implementation remains.

The hidden native UI regression compares direct-parent and child-delivered
positive/negative/fractional wheel input at 96/120/144/192 DPI, including a
focused-control message and Ctrl modifier. It verifies unchanged values,
single form scrolling, horizontal-wheel suppression and working keyboard
increments/decrements. Evidence is retained with the combined Gauss/zoom task
under `manual-artifacts/task-records/e21-zoom-sliders-20261015`.
Debug and Release native checks passed3/3 repeats each; core-only Release
CTest passed44/44 including this native UI test. Normal Release launcher
SHA-256: `BD14AD497DA65A0692DDBF074DAB975BAAFF6FF2EA3B1A993DE289B2FE8F55E6`.
The existing settings file is hash-identical to its pre-edit copy. Combined
verification is recorded in [the E21 follow-up](GAUSS_PRIMARY_CHARGE_BEAM_E21.md#combined-gausszoomlauncher-verification).
New-image manual=not_run; no live game/server run or Git write.

## Smooth launcher scrolling follow-up — 2026-10-15

`attribution=user_report`: scrolling in the normal launcher feels uneven.
The old wheel handler truncated partial wheel deltas and jumped 48 logical
pixels per notch. Each jump called full layout, deleted/recreated the font,
and synchronously moved/redrew every native child separately.

The launcher now uses a bounded 120 ms cubic ease-out with a dedicated 16 ms
UI timer, independent of the existing 100 ms session-worker timer. Fractional
wheel input is retained, reverse input cancels pending forward travel, and
both bounds prevent accumulated overscroll. Windows wheel line/page settings
and the client-area animation preference are respected. Scrollbar dragging,
line/page/end actions and keyboard focus visibility stay immediate and cancel
pending motion. Resize/DPI changes clamp and cancel the old trajectory.

Fonts are cached per DPI and replaced only after all children have received
the new font. Child movement is deferred in one batch, using one shared
physical-pixel offset at fractional DPI. One queued redraw and native
`WS_EX_COMPOSITED` painting replace per-child synchronous erase/paint. Textless
trackbars retain their own optional font-message behavior. No `WM_SETREDRAW`,
game animation, network, configuration schema or gameplay changes.

Changed production entry point: `apps/hlclient_launcher/main.cpp`; the small
app-local `smooth_scroll.hpp` owns only launcher coordinates/timing. Its
deterministic checks plus the existing hidden `--offline-ui-check` exercise
partial deltas, monotonic convergence, reversal, limits, shared child geometry,
font stability, focus, scrollbar and resize at 96/120/144/192 DPI. Tests never
show the hidden form, connect, launch a game or save settings.

VS2022/v143/Win32 launcher builds passed: Debug, Release and core-only Release.
Native UI checks passed 3/3 Debug, 5/5 Release and 3/3 core-only repeats.
Existing Release `[e31],[launcher]`: 69 passed, 2 opt-in asset/runtime skips,
3,003 assertions. Core-only CTest: 44/44, including native UI and architecture.
The unrelated full game suite was not rerun for this UI-only change; the
previously tested `hlclient.exe` is unchanged byte-for-byte.

Evidence and the verified 1,418-file pre-edit snapshot:
`manual-artifacts/task-records/e31-launcher-scroll-20261015`.
The initial test compile exposed the Windows `near` macro (test helper renamed);
the first native check was corrected to respect textless control font semantics
and use a DPI-independent test viewport. Final checks above are post-fix.
New normal Release `HLClientLauncher.exe` SHA-256:
`CFD98728275A5736467F7E1A445C8C0B0E1F76ECE29350F7B202C9C867BDB83B`.
Manual smoothness/paint confirmation remains `not_run`; hidden geometry checks
are not a human visual confirmation. No live game/server sessions, staging,
commit or push. The user's launcher settings and game executable are preserved.

## Baseline and read-only audit

Authoritative worktree `D:/DEV/CPP/HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`; origin
`https://github.com/Pvitaly91/hl-client-engine.git`. Existing dirty files preserved,
staged changes0, initial diff check clean. No applicable AGENTS files found in
ancestors/root/relevant source trees. Before edits, existing source-snapshot
mechanism copied and verified1346 source and34 Release rollback files under
`manual-artifacts/task-records/e31-simple-launcher-20261011`.
The subsequent `preservation-audit.json` verified all 1380 retained files and
reported no missing baseline source. The unchanged old managed launcher SHA-256
is `AF6560D326D696EE6A03386DE2009AD967A3E6D4BDCC0DE286A180E0102DD049`.

E30 `manual=passed`, `attribution=user_report`,
`scope=reported blood decals/sprites and regression checklist`.
The actual E30 report/current pre-edit client SHA is
`D38E036659AF3B9EB2E03B2671FB882FED363D65282766B6044DD5651817DC8A`.
Prior historical test failures/skips and older stage records are retained.

Audit findings before implementation:

- `external_connect_current=normal --connect/+connect accepts non-loopback`.
  `open_challenge_socket` chooses127.0.0.1:0 for loopback,0.0.0.0:0 otherwise.
  One retained UDP transport is used for challenge/auth/connect/netchan/close.
  The old general endpoint parser alone does not enforce E31 unicast policy.
- `live_client_startup_dependencies=normal hlclient, SDL3, OpenGL, selected
  licensed local assets, explicit official x86 Steam API, logged-on AppID70`.
  Server challenge owns SteamID/security and accepted connection does not prove
  completed server authentication. Current live stop prepares resources through
  its existing implicit path; it rejects the explicit diagnostic consistency flag.
- `research_only_dependencies=owned HLDS, A/B orchestration, loopback WFP guard,
  managed tree restoration`. These are outside the direct client path.
  Existing dynamic WFP filters can cover the same executable and must never be
  silently removed by external launcher.
- `video_options_current=resizable 1280x720 SDL window with pixel-size reads`.
  `fullscreen_current=not exposed`; SDL actual version3.4.14, pinned commit
  `147a8ee32dbf9ac02f3794964490687b6bbda1bc`. VSync was forced1 with0 fallback.
- `image_adjustment_current=none`; direct default-backbuffer rendering, no scene
  correction or framebuffer-sRGB enable; final HUD pass already separate.
- `gui_framework_current=no existing suitable launcher GUI`; Win32 controls
  fit existing C++/CMake/MSVC toolchain.
- `settings_persistence_current=no shared launcher settings`; existing CLI
  already exposes audio volume0..100 reaching listener master gain.
- Actual map/game/protocol/maxplayers originate from serverinfo, not a forced
  Crossfire. Existing normal composition needs an explicit external non-valve
  rejection before Half-Life application. Current safe maxplayers bound32 remains.
- E28.1 immutable receipts and bounded same-channel graceful close are independent
  of HLDS ownership. E28.2 research verdict additionally depends on owned-server
  evidence; external verdict must evaluate only its own client commitments/exit.

## Implemented entry point and GUI

The Windows CMake target is `hlclient_launcher`; its output name is
`HLClientLauncher.exe`. It uses native Win32 controls, the existing SDL3 runtime
for display information, and the existing C++/MSVC toolchain. No browser, Qt,
Electron, .NET, Python or additional audio/renderer runtime is introduced.
The packaging rule copies the existing `SDL3.dll` beside the launcher; Valve
game assets and a replacement Steam runtime are not bundled.

The built and hash-observed Release entry point is:

`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\HLClientLauncher.exe`

The GUI derives the absolute sibling `hlclient.exe` from its own executable
location, not `PATH`, the current directory or a PowerShell command. Opening
the GUI does not start a connection. Only **«Підключитися»** starts one normal
client. The managed `Start-HLClient-H2-Manual.ps1` workflow remains separate.

Actual Ukrainian controls:

- Server, initially `192.168.0.104:27016` after the user's address correction,
  and player name, initially `Player`.
- Actual monitor list, window resolution, windowed/borderless mode, and VSync.
- Independent brightness, gamma, contrast and volume sliders with numeric
  values; **«Скинути»** restores only image controls to their neutral values.
- A secondary local-resource group with Half-Life root folder picker, official
  `steam_api.dll` file picker, dependency explanation and actual client path.
- Connect, cancel/disconnect, save, open report and an asynchronous status area.

The form declares per-monitor DPI awareness, handles DPI changes, supports
keyboard Tab/Enter/Escape, and scrolls the focused control into view on a short
desktop. Standard Windows dialogs choose folders/files. Controls explicitly
state that changed settings apply to the **next game launch**; there is no
separate approximate image preview or live in-game settings IPC.

On first launch the default window dimensions are derived from approximately
80% of the actual first desktop, subject to supported minimum dimensions. The
resolution list contains bounded window sizes fitting the selected desktop and
the saved size when applicable; these are client-area choices, not advertised
exclusive display modes. Display changes refresh the list while idle and again
before Connect, preserving the selected SDL display ID where available. A
hidden 1-by-1 non-GL SDL utility window receives display notifications so the
SDL display cache is current; native enumeration is deferred until its display
notification has been processed. Losing the selected monitor recovers the UI
to the first monitor and a fitting windowed choice.

## Shared settings and precedence

`core::ExternalLaunchConfiguration` is the single GUI/client contract. Its JSON
schema is `hlclient.external-launch.v1`, `version=1`, with a 16 KiB input cap.
It carries `endpoint`, `name`, `asset_root`, `steam_api_runtime`, `display_index`,
`window_mode`, `width`, `height`, `vsync`, `brightness`, `gamma`, `contrast` and
`volume`. Unknown/duplicate/missing fields, wrong types, invalid integers,
non-finite numbers and out-of-range values are rejected, not silently applied.

Precedence is explicit launch snapshot → saved launcher settings → defaults.
The GUI loads `%LOCALAPPDATA%\HLClientEngine\launcher.json`, edits a typed value,
validates it, atomically saves it and writes an immutable per-run
`launch-config.json`. The child receives `--external-config` and the exact
receipt/run/role identity arguments (plus its inherited cancellation event).
This CLI form rejects unrelated or conflicting ordinary CLI options. The
normal client independently validates the file, schema and run-directory
relationship before mapping the snapshot to production options.

Corrupt saved settings recover to defaults with an attempted uniquely named
`launcher.corrupt-<id>.json` backup; backup/read failures remain distinct
statuses. Settings are not written into `config.cfg` or licensed assets.
Selected local paths are stored in this local per-user configuration, not
replaced by machine-wide settings. No password or authentication ticket is part
of the configuration.

Bounds and mappings:

| Setting | Contract |
| --- | --- |
| Name | 1–31 printable ASCII characters, at least one non-space; no quote, backslash or semicolon |
| Display | Actual enumerated index, bounded to 0–31; availability checked at launch |
| Window dimensions | Configuration range 640–7680 by 480–4320; actual desktop and renderer bounds also apply |
| Mode | GUI offers `windowed` and `borderless`; explicit `exclusive` configuration recovers safely, not silently claimed applied |
| Brightness | −0.50 to +0.50, neutral 0; slider value divided by 100 |
| Gamma | 0.25 to 4.00, neutral 1; slider value divided by 100 |
| Contrast | 0.00 to 2.00, neutral 1; slider value divided by 100 |
| Volume | 0–100%, default 35%; existing mixer master gain is value / 100 |

Absent E31 configuration leaves ordinary client defaults unchanged: windowed
1280×720, VSync requested on, neutral image controls and the existing volume
default. No hidden E31 flags are required by the old managed launcher.

## External session, endpoint and authentication

`app::apply_external_launch_configuration` selects explicit
`external_server_client` mode and the normal live application composition:
Half-Life `valve`, OpenGL, keyboard/mouse, reference prediction and unlimited
game duration. This is not the owned-HLDS A/B test with renamed controls.
It has no server executable, client B, bot installation, RCON, forced map,
server configuration or server cleanup operation.

The endpoint parser accepts a literal unicast IPv4 address, an optional port
1–65535, and an optional exact `connect ` prefix as **data**, not a console
command. Missing port means 27015. Surrounding spaces/tabs are trimmed;
additional commands, newlines, semicolons, malformed/overlong input,
unspecified/loopback/multicast/class-E destinations and `.255` destinations are
rejected. Hostnames and IPv6 are not E31 features. This conservative literal
policy is not subnet discovery or a claim to classify every directed broadcast.

The remote IP/port is a destination, never the local bind address. For this
non-loopback mode the existing transport binds `0.0.0.0:0`; the OS selects the
local endpoint. `app::EndpointBoundTransport` retains the same underlying
socket through challenge, authentication, connect, netchan and close, permits
sends only to the selected endpoint and ignores datagrams from other sources.
It creates no global firewall allowance, LAN scanner or automatic redirection.

Read-only WFP preflight detects the existing named research non-loopback block
for the same executable. A detected conflict is reported; no filter is removed.
An unavailable query remains `query_unavailable`, not a false claim that no
restriction exists. The GUI has no mandatory elevation and never changes
another active research session's isolation.

Authentication reuses the existing Steam provider and official x86 runtime.
The launcher performs stable file/PE identity and cache-only Authenticode/Valve
signer checks; the child performs its existing runtime/auth checks. Steam must
be logged on with the required Half-Life entitlement/AppID 70. A LAN address
or bots do not imply `sv_lan`, no-auth, a particular server build, or successful
authentication. No fake SteamID/ticket, copied auth material, VAC alteration or
new authentication protocol is introduced. Server-password entry is not a new
E31 feature.

Actual protocol, game directory, map and maximum clients come from serverinfo.
The expected profile is `valve`; another mod gets `unsupported_game` rather
than being passed to the wrong module. The existing bounded player limit is
preserved, not hardcoded to two or eight players. Remote players/bots use the
normal entity, Studio and audio paths; arbitrary bot/mod messages are not
declared compatible without evidence.

## Local resources and error boundaries

`D:\DEV\HLCLIENT-RESEARCH\Half-Life` and its `steam_api.dll` are initial local
candidates only. The user can select another licensed local Half-Life root and
official runtime. There is no access requirement to the server computer's
filesystem and no need for local `hlds.exe`.

The normal approved resource preparation, path validation and existing
importers load the server-selected map, models, textures, sprites, HUD and
audio. Resources are prepared through `RuntimeReplayLocalAssets`, not copied
into a new launcher asset system. Missing/invalid resources retain bounded
virtual-resource reason categories; fatal map preparation is distinguished
from a connection timeout. The existing generated checkerboard fallback, when
permitted by that resource class, is reported as incomplete source textures,
not a successful stock-texture validation.

WAD diagnostics join the retained, validated BSP worldspawn declaration basename
to the existing texture importer's archive ordinal and outcome. They perform no
additional resource open or path search. Bounded issues survive failed world or
brush preparation even when no projection is published. For these dependencies,
`type=255` and `index` means the worldspawn declaration ordinal, not a server
precache slot. Missing, malformed/catalog, rejected/open and unsupported-profile
outcomes remain distinct; texture validation and failure policy are unchanged.

No map substitution, FastDL/HTTP downloader, download of executable server
assets, server `client.dll`, arbitrary `stufftext` shell execution or server cfg
execution is added. Protocol/parser failures retain the existing transactional
boundary: unsupported messages are not skipped by guessing byte lengths.

## Video, image composition and recovery

`core::VideoSettings` and `core::ImageAdjustments` are neutral engine types;
they contain no Half-Life, SDL, OpenGL or game-asset dependency. `SdlWindow`
uses the pinned SDL3 API and reports both the requested settings and actual
logical window size, framebuffer pixel size, desktop size, display/scale,
mode, VSync and recovery reason.

- Windowed mode requests the selected client-area dimensions and selected
  display. Unavailable display, oversized window or unsupported exclusive mode
  selects a bounded safe window instead.
  Desktop-fit shrinking applies only to interactive windows: existing hidden
  rendering fixtures retain their explicitly requested HD/ultrawide framebuffer
  extents. This distinction preserves the old E29 HUD pixel-space contract.
- Borderless explicitly selects the display's desktop mode without an exclusive
  mode switch. Its disabled resolution control explains this desktop behavior.
  Completion is synchronized and checked against actual fullscreen flags,
  display and framebuffer pixels; an unconfirmed transition recovers the owned
  window to safe windowed mode or reports a failure.
- VSync reaches `SDL_GL_SetSwapInterval`; fallback and actual swap interval are
  observed instead of assuming the requested value was accepted.
- The existing frame loop reads actual framebuffer pixels for viewport,
  projection and HUD composition. Python/Crossbow FOV values, input capture,
  focus loss, Escape and window resize continue through the normal paths.

Before borderless launch the GUI durably writes `pending-video.json`. After a
successful session it explicitly asks whether the image was correct before
clearing that marker. A rejected/failed/unconfirmed session keeps the marker;
the next launcher startup forces windowed mode. This is a conservative
last-known-safe recovery contract, not an exclusive-mode revert timer. Neither
desktop resolution nor the desktop gamma ramp is changed.

The image controls are normalized application controls, **not stock gamma cvar
equivalents**. Each RGB channel uses:

```text
adjusted = pow(clamp((rgb - 0.5) * contrast + 0.5 + brightness, 0, 1), 1 / gamma)
```

Alpha is copied unchanged. Brightness is an additive offset, contrast scales
about 0.5, and gamma changes the midtone curve; they are not three labels for a
single gain. Invalid settings are rejected before shader execution. The
composition order in both production renderer branches is:

```text
world + players + viewmodel + world/weapon effects
    → scene image adjustment
    → HUD, inventory, notices and crosshair
```

Neutral `{0,1,1}` bypasses the new pass entirely, preserving accepted E30 output.
For non-neutral settings the pass reuses one bounded RGBA8 copy texture plus
one shader/VAO. Pixel-exact `texelFetch`, nearest/clamp sampling and no mipmaps
avoid resampling the scene. Storage is resized only when framebuffer size
changes. The existing color pipeline uses RGBA8 rather than sRGB texture
decoding; this pass disables framebuffer-sRGB during its draw and restores it,
so it does not introduce a second implicit gamma conversion. Copy, depth,
blend, stencil, scissor, texture/program and write-mask state are guarded.

No texture/lightmap reimport, palette mutation, blood/decal geometry change,
new renderer framework or scene-resource reload is needed for a slider change.
`app::apply_application_scene_settings` is shared by main and the config-to-GPU
fixture. Volume similarly uses `app::application_audio_master_volume` and the
existing bounded audio mixer, not another device or sound API.

## Process ownership, cancellation and durable reporting

`ExternalClientProcess` starts a verified absolute client using explicit
`CreateProcessW`, quoted argv, a defined working directory and restricted
inherited handles. It creates the child suspended, checks its image identity,
then resumes it without a console window. The GUI worker drains bounded
stdout/stderr asynchronously; repeated Connect is disabled until the owned
session finishes. Process creation is only a launch milestone, not gameplay.

The GUI distinguishes resource checks, connecting, authentication, sign-on,
map preparation, in-game, disconnecting, completed and error. Durable E28.1
receipts are bound to run ID, role, process ID and creation identity. Positive
gameplay additionally needs actual sign-on, world/gameplay publication and
nonzero usercmd/server-sample evidence. Authentication success is not inferred
from a created window or accepted UDP send.

Cancel signals the inherited event. Main retains the same-channel bounded
E28.1 graceful close and records queued/sent/transport ACK/server-confirmed
states separately. `sent_but_unconfirmed` is not described as server ACK.
Startup cancellation has its own close/verdict path. Closing the GUI during a
session asks whether to disconnect and wait for its report. A stalled normal
close offers explicit force termination after eight seconds, restricted to the
owned child handle. For a launcher-internal exception, the documented emergency
path requests graceful cancellation for 1600 ms, then may terminate that exact
owned child with exit 125 and wait up to 2000 ms; the failure/forced outcome is
reported, never upgraded to a successful close. No process-name kill or server
process ownership exists.

Per-run files live under `%LOCALAPPDATA%\HLClientEngine\runs\<run-id>`:

- `launch-config.json`: exact validated settings snapshot.
- `launch-manifest.json` and `process-manifest.json`: observed launcher/client/
  config identity, process creation identity and run identity.
- Existing immutable session receipts: milestones, map generation, traffic
  evidence, close result and terminal reason survive bounded log eviction.
- `external-video-start.json` and `external-video-final.json`: requested versus
  actual video/image/volume observations bound to the run and process. Final
  video publication happens after the graceful network close, so a report I/O
  failure cannot skip the disconnect attempt.
- `external-diagnostics.json` (`hlclient.external-diagnostics.v1`): an immutable
  run/process-bound record published during receipt finalization, including
  exception finalization. It records observed server game/map/protocol/player
  limit, typed failure/protocol codes and optional wire opcode/source sequence/
  record ordinal. Resource diagnostics contain at most 32 validated virtual
  names with typed status/type/index, plus omitted and missing-texture-placeholder
  counts. Fatal map preparation retains the observed virtual map and typed
  manifest status. It excludes arbitrary server text, raw protocol payloads,
  auth material, native asset roots/handles and raw userinfo; unavailable fields
  remain unobserved rather than fabricated.
- `client-status.log`: bounded, whitelisted/redacted status output with capture
  truncation/failure metadata; not a raw userinfo/auth dump.
- `external-run-report.json`: terminal launcher result, endpoint, observed
  launch-time binary/config identities, exit, gameplay evidence and references
  to the actual video, diagnostics and status records. An early failure does
  not invent absent video or gameplay evidence. Detailed resource/protocol
  observations are retained in the structured diagnostic file rather than
  depending on the bounded stdout tail.

The report explicitly records `mode=external_server_client`,
`server_process_owned=false`, `server_cleanup=not_applicable`,
`owned_helpers=0` and `server_live_compatibility=manual_confirmation_required`.
It does not require owned HLDS readiness/exit, a B client, a restored server
tree or research verdict evidence. Report publication failure remains a
failure; the GUI exposes the run folder when the final report cannot be written.

## Production paths and offline evidence scope

Principal implementation locations:

- `apps/hlclient_launcher/main.cpp`: native controls, persistence composition,
  display recovery, worker/session lifecycle and report presentation.
- `core/external_launch_config` and `core/command_line`: shared validated
  contract and isolated external-config argument form.
- `platform/windows/external_launcher` and `external_launch_preflight`: safe
  child/process/settings operations, receipts, official-runtime check and
  read-only research-isolation conflict check.
- `app/external_session`, `app/application_presentation_settings` and
  `apps/hlclient/main.cpp`: one-endpoint normal gameplay integration and
  durable external observations.
- `core/presentation_settings`, `platform/sdl_window`, `renderer/render_scene`,
  OpenGL renderer and Null renderer: neutral configuration, actual video state,
  scene correction and input validation.

Executed offline coverage exercises parser injection/ranges/schema,
Unicode filesystem paths, persistence/corrupt recovery, owned fake-child
lifecycle, repeat Connect/cancel, one-endpoint fake transport, no owned-server
capability, settings-to-normal-options mapping, actual SDL windows, actual
OpenGL pixels and isolated PCM volume. The native launcher's
`--offline-ui-check` operates the actual Win32 controls through collection and
JSON round-trip, including Ukrainian paths, monitor/resolution, VSync/volume,
borderless resolution disablement and the neutral-reset button, without a
client launch or settings-file write. The image tests use project-owned world
fixtures, neutral A/B comparison, independent controls, unchanged HUD/crosshair,
FOV 20/40/75, resize/state restoration and bounded texture reuse. The integration
test goes through the same JSON encoder/decoder, external CLI parser,
application options and scene helper as the normal client, not just JSON
equality. PCM tests isolate volume in the production mixer; they are not a
human listening confirmation or a LAN session.

## Initial E31 offline acceptance (retained history, not E31.1 final gates)

Evidence is retained under
`manual-artifacts/task-records/e31-simple-launcher-20261011`. The table records
observed results in order; a later focused pass does not erase an earlier full
suite failure or substitute for that checkpoint's final full Release run.
All "final"/"last production correction" references in this section describe
the initial E31 checkpoint, before the later E31.1 diagnostic and suffix fixes.
Its manual-not-run status is historical, not the current candidate status.

| Evidence | Observed result |
| --- | --- |
| `configure-release-01.log`, `configure-release-02.log` | Release configuration completed; the later configure includes the current registered targets/fixtures. |
| `build-release-01.log` | Initial GUI compile failed on missing `ShellExecuteW` declaration and mixed `int`/`LONG` arguments to `std::min`. Fixed with the native header and explicit integer conversion; failure log retained. |
| `build-launcher-release-02.log` through `build-launcher-release-04.log` | GUI retries compiled successfully. Early basic UI checks were subsequently extended to operate the real settings controls and neutral-reset button. |
| `build-tests-release-01.log` | Test compilation failed on `std::array` deduction in the video fixture. The test construction was corrected without weakening its assertions; failure log retained. |
| `build-all-release-02.log` | Full Release target build succeeded before the first full offline run. |
| `focused-release-01.log` | `[e31]`: 32 cases, 31 passed, 1 optional installed-Steam check skipped; 927 assertions passed. This is earlier evidence, not the final focused count. |
| `official-runtime-release-01.log` | Explicit read-only official-runtime selection: 1 case and 5 assertions passed. File/PE/signature observation only; no Steam initialization, game, server or LAN connection. |
| `gui-offline-release-03.log` | Hidden native GUI/control round-trip check passed; test duration 0.53 s (CTest invocation total 43.66 s). No client launch, settings-file write or network session. |
| `full-release-offline-01.log` | First full offline run **failed**: 3456 total tests, 2 failed, 64 skipped; 582.44 s. Failures were the existing E29 HD/ultrawide HUD framebuffer and E29.1 kill-feed tier tests. |
| `src/platform/sdl_window.cpp` correction | Desktop-fit shrinking had changed the explicitly requested dimensions of hidden HD/ultrawide rendering fixtures. The correction limits that interactive-window safeguard to non-hidden windows; hidden fixture dimensions remain exact. Existing E29 assertions were not relaxed. |
| `build-all-release-03.log` | Full Release target build succeeded after the hidden-window correction. |
| `focused-release-02.log` | E31 plus the two named E29 regression selections: 35 cases, 34 passed, 1 optional installed-Steam check skipped; **46986 assertions passed**. This confirms the targeted correction, not a final full-suite pass. |
| `build-debug-01.log` | Debug client, native GUI, main test binary and core API test targets built successfully. |
| `focused-debug-01.log` | The same E31/E29 regression selections: 35 cases, 34 passed, 1 optional installed-Steam check skipped; **46986 assertions passed**. |
| `gui-offline-debug-01.log` | Actual native GUI/control round-trip passed in Debug: test 1.36 s; CTest invocation total 49.02 s. No game or network session. |
| `core-api-debug-01.log`, `core-api-release-01.log` | Both configurations passed **316 cases / 143396 assertions**, including the alternate-module API fixture. |
| `configure-core-only-01.log` | Core-only configuration with Half-Life disabled passed the architecture/actual-target dependency check. This existing build tree keeps `warnings_as_errors=OFF`; it was not newly disabled to make E31 pass. |
| `build-core-only-01.log` | Core-only Release client, GUI and core API test targets built successfully without HL1. |
| `core-boundary-gui-release-01.log` | Core-only Release checks passed 2/2: GUI 1.30 s, architecture guard 0.95 s; CTest total 2.31 s. |
| `core-client-version-01.log`, `core-client-null-01.log` | Core-only version/startup checks passed; the Null renderer completed one bootstrap frame without Half-Life assets. |
| `official-runtime-release-02.log` | Repeated explicit read-only official-runtime check passed: 1 case / 5 assertions. Still no Steam initialization or live connection. |
| `configure-asan-01.log`, `build-asan-01.log` | Separate Win32 Release ASan client target configured and built successfully after the last production correction. Normal Release binaries were not replaced by ASan outputs. |
| `asan-runtime-01.json` | Fresh ASan executable `--help` exited immediately with **0xC0000135** (signed decimal −1073741515), empty stdout/stderr, no timeout. **ASan runtime remains unavailable/unconfirmed**; this is not a successful sanitizer test. No infrastructure repair or live session was attempted. |
| `old-launcher-checkonly-01.log` | Unchanged old launcher passed `-Prediction reference -RemoteAudioPeer -NoTimeLimit -CheckOnly`, including selected-installation file checks and native receipt/peer contracts; `process-launches=0`. No managed session, Steam API, WFP or network socket started. |
| `full-release-offline-02.log` | **Final full Release suite passed after the last production correction:** 3457 registered tests, **3393 passed, 64 skipped, 0 failed**; CTest 595.21 s. Both previously failing E29 tests passed. E28.2 no-stock normal-close verdict passed in 90.05 s. |
| Final driver continuation in `full-release-offline-02.log` | Entire `build_source.ps1 -Configuration Release -Parallel 1 -TestOnly` returned **exit 0** after CTest, normal version/help/Null bootstrap, local transport self-test, functional-log observation, G/H summary fixtures, the old manual-launch no-stock fixtures and `-CheckOnly -SourceOnly`. Its final line confirms Release offline verification and external game/live validation **NOT RUN**. |
| Latest preservation/Git audit | All 1380 retained snapshot files verified, missing baseline source `[]`, old managed launcher SHA unchanged; `git diff --check` exit 0, staged changes 0, branch/HEAD unchanged from the baseline above. |

The default focused-run Steam skip means `HLCLIENT_E31_STEAM_RUNTIME` was not
explicitly selected for that invocation. The separate read-only pass above is
reported separately rather than relabeling the skipped invocation. Both full
invocations reported 64 skips. The final skips retain their individual reasons
in `full-release-offline-02.log`, including
opt-in installed assets/device tests and unavailable privileged/link/input
capabilities; they are not counted as passed acceptance evidence.

Initial E31 checkpoint gates:

| Gate | Final status / evidence |
| --- | --- |
| Final Release build and focused E31/regression checks | **Passed**: `build-all-release-03`, `focused-release-02`, final file identities and full-suite driver exit 0. |
| Focused Debug build/tests | **Passed**: Debug build, focused 35-case selection and native GUI/control check, as recorded above. |
| Core-only build/tests without HL1 | **Passed**: Release build, 316-case core API run, GUI/guard and version/Null bootstrap checks. |
| Alternate-module/core API boundary | **Passed**: Debug and Release, each 316 cases / 143396 assertions including alternate-module coverage. |
| Architecture guard | **Passed**: core-only configure boundary check and explicit guard test. |
| Old managed launcher `-CheckOnly` | **Passed** with actual local file checks and native offline contracts, as above. The full-suite driver additionally covers `-CheckOnly -SourceOnly`. |
| No-stock/preservation fixtures | **Passed** in the final suite/driver; preservation audit verified 1380 files with no missing baseline source and unchanged old-launcher SHA. |
| `git diff --check` / staging / repository identity | **Passed**, exit 0; staged changes 0; baseline branch/HEAD unchanged. Final observations retained in `final-identity-audit.json`. |
| Full Release offline suite after the last production correction | **Passed**: 3393 passed, 64 skipped, 0 failed, final driver exit 0. `full-release-offline-01` remains a failed historical run, not the final verdict. |
| ASan build | **Passed**, separate `build-asan-01.log`, target `hlclient`. |
| ASan runtime | **Unavailable/unconfirmed**: fresh `--help` reproduced `0xC0000135`, no output. Not counted as passed. |
| Final launcher/client SHA-256 and packaged runtime identity | **Observed** in `final-identity-audit.json`; exact files below. |
| Manual external LAN gameplay | **Not run**; `external_server_live_verified=false`, new live sessions=0. |

**E31 offline implementation is complete.** GUI creation, a fake child,
read-only official-runtime validation, synthetic transport, OpenGL pixels and
isolated PCM do not prove entry into the user's LAN server. Live acceptance
requires the manual checklist below. ASan runtime remains unavailable and is
not included as a passed test. At that initial checkpoint, no production edit
followed its final Release build/focused checks/full-suite run; only the report
was completed. The E31.1 follow-up below subsequently changed production code
and therefore has its own final verification and binary identities.

## Initial E31 Release file identities (retained history)

Actual files in `D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release`,
observed after the last production correction and Release rebuild:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `HLClientLauncher.exe` | 242176 | `731A076AD2F6EF9D5A7DFDCC866638456EF6B0FEE126688CD98713D0B4643158` |
| `hlclient.exe` | 4221952 | `2D8A59948273DE97BAC0ECAF51872E6F8E6325508B553CC1F8E78F5CB3592830` |
| `SDL3.dll` | 2285568 | `4D33E061E361E9D18B6821830D591324108314A2B44F8E87FA56CC256A7DD19C` |

The GUI resolves exactly this sibling normal client; both use the project SDL3
runtime beside them. No Valve assets/auth DLL were copied into this directory.
These are new E31 identities, not attributed to an earlier manual run.
The separately built ASan client SHA is
`97AF4D955C93CBEEBE6D7B2436CD8E3FFB21C810ADBB01B7C0F9F8E9C0B44EA3`;
it is not the launcher's normal client and its runtime test did not pass.

## 2026-10-11 follow-up — corrected address and early startup failures

The user corrected the target to **192.168.0.104:27016** and reported that the
game window immediately closes. Two existing process-bound reports were read
without another network request:

- `9eaddd86a92aaf595c183be98ef1b200`: old `.102` endpoint, `network_error`,
  exit 2, no connection/map/gameplay receipt.
- `6e452fd438a0e3ea2cc606bfd80b35c8`: corrected `.104` endpoint,
  `protocol_error`, exit 2, transport closed at process 698 ms, no auth/sign-on,
  connection, map or gameplay evidence. This is an explicit early failure, not
  an observed access violation or renderer crash. The report identifies the
  initial E31 client/launcher hashes listed above.

`attribution=user_report` covers the observed brief window and failed entry;
the stages, exit and identities are independently observed in the local reports.
These attempts do **not** establish server compatibility. Agent-initiated live
sessions remain 0. The user's saved `launcher.json` already has `.104`; it was
not rewritten or reset. Source defaults, GUI example/self-check and default
configuration tests now use `.104`; historical evidence and arbitrary parser
test addresses remain unchanged.

Two reporting bugs were proven and corrected:

1. `GoldSrcHandshakeCoordinator` exposed only generic context for the early
   challenge failure to the application. The durable report consequently lost
   the parser's typed code and location. Parser-owned field categories and byte
   offsets now survive exchange/coordinator into `external-diagnostics.json` as
   `failure_phase`, `failure_category`, `protocol_code`, `protocol_byte_offset`
   and `protocol_field`. Challenge/connect-request/connect-response remain
   distinct. No raw preview, challenge value, server identity or auth material
   is added to durable logs. The accepted grammar and auth behavior are unchanged.
2. Main preferred fatal-shutdown `close_failed` over the original error, even
   with `close_requested=0`, `eligible=0`, and no disconnect sends. The launcher
   also took the latest marker from a whole polling interval. Main now preserves
   the original typed failure. The launcher scans bounded complete status lines,
   retains the first primary error across polls, and reports
   `primary_failure_reason` separately from `cleanup_failure_reason`. Fatal
   receipts remain failures; no failed session is promoted to normal close.

The old redacted log retained no rejected field/code. Its `observed_bytes=13012`
means bytes were drained, **not** that a recoverable raw packet capture exists.
At this diagnostic-only stage, the exact compatibility issue at `.104` was not
yet proven. That correction alone did not make the unsupported response valid.
The subsequent user retries and bounded passive observations below established
the boundary before the separate narrow parser correction was implemented;
no speculative auth relaxation or server configuration change was made.

New offline fixtures include the exact five saved status lines, fatal startup
without a requested disconnect, six synthetic malformed challenge variants
through the actual coordinator and durable encoder, distinct connect-response
errors, and absence of packet/auth values in reports. They are project-owned
fixtures, not invented captures attributed to this server.

Before edits, `manual-artifacts/task-records/e31-1-startup-failure-20261011`
preserved and hash-verified **1364 source files and 36 Release files**; the two
user-run report sets were retained separately there. Final build/test results
and current binary identities will be recorded below. The corrected candidate
was initially `manual=not_run`; the subsequent user retry below supersedes that
status with `manual=failed`, `external_server_live_verified=false`.

Follow-up verification history: `build-release-01.log` records a failed
serialized build (`C1060`, compiler heap exhaustion, followed by dependency
failures). Retries `02` and `03` likewise retained compiler heap failures
(`C1060` / `C1002`); `03` also recorded a failed post-build helper. These logs
are retained, not overwritten. Retry `04` uses the installed MSBuild
`UseMultiToolTask` path, one compiler process at a time, and the x64-hosted
compiler for the same Win32 target. No foreign process was stopped or system
memory configuration changed. These toolchain failures are not evidence about
the user's LAN server or runtime exit. The already rebuilt normal Release
core-API executable passed **317 cases / 143413 assertions** before this retry.

### Updated candidate — user retry and proven parser boundary

The user retried `.104:27016` with run
`b270b9bbcc4b49f26f3d432c86caac52`. Its saved report set is retained under
`manual-artifacts/task-records/e31-1-startup-failure-20261011/user-run-b270b9bbcc4b49f26f3d432c86caac52`.
The process-bound `external-run-report.json` identifies these actual candidate
binaries; these hashes are not attributed to the earlier attempts:

| File | SHA-256 recorded for this user run |
| --- | --- |
| `hlclient.exe` | `B60CE515C71D328C85E8565321A0398EA187454DFE5343E55D86332ECFC34A79` |
| `HLClientLauncher.exe` | `3F757F466D01023BBB6812B28DD6DBD1A3FD400BD6E5A9D6376719943E83369E` |

The client exited **2**, with no connection, sign-on, map entry or gameplay
receipt. The corrected reporting path preserved
`terminal_reason=challenge_protocol_error` and
`primary_failure_reason=challenge_protocol_error`; `cleanup_failure_reason=none`.
`external-diagnostics.json` now identifies `failure_phase=challenge`,
`failure_category=protocol`, `protocol_code=unsupported_variant`,
`protocol_field=server_identity`, and `protocol_byte_offset=43`.
This diagnostic-only candidate was **`manual=failed`**, not `not_run` or a successful LAN
compatibility test. The reporting correction is observed working; connection
compatibility was still unresolved at that point.

The parser boundary can be established without reconstructing sensitive field
values. The accepted prefix, canonical 1–10-digit uint32 challenge and exact
` 3 ` place the identity's first byte at absolute offset 18–27. Invalid,
overflowing or zero identities fail at that starting offset. Offset 43 therefore
selects the later check in `parse_challenge_response`: a valid nonzero uint64
identity has already been parsed, but the next byte is not the required ASCII
space, or the packet ends there. Secure-flag parsing and Steam authentication
have not begun. The report does **not** identify the actual following byte,
prove an omitted secure flag, or establish the server's engine implementation.
No arbitrary separator acceptance or guessed `secure=false` default follows
from this evidence.

The user separately authorized read-only SSH inspection of the server. The
inspection connected using an existing verified host key and observed an HLDS
process running as root in Docker container `hlds-jk-botti`, using image
`hlds-jk-botti:gauss-route-20260727`. A subsequent read-only inspection of its
startup log observed protocol **48**, executable version **1.1.2.2/Stdio
(valve)**, executable build **01:34:04 Oct 8 2024 (10211)**, **STEAM Auth
Server**, and **Metamod 1.21p38**. The log also reports **VAC secure mode
disabled**; this is observed existing state, not a setting changed for E31.
Neither the image/container name nor these version strings alone establish
stock-versus-ReHLDS provenance. The server log does not identify the rejected
challenge delimiter or independently supply the missing on-wire field shape.
No credentials are retained in this task document. No server files/settings
were changed, no server/container was restarted, and no new gameplay session
was initiated by this inspection.

A bounded passive server-side `tcpdump` observation, filtered to the exact
challenge-response traffic, was configured with a 55-second timeout. The
downstream Python shape check returned `shape_captured=false`: no matching
packet was observed. The capture process's own exit status and stderr were not
retained, so capture failure versus timeout is not established. No newer local
run record was observed at the audit; the latest observed record remained
`b270b9bbcc4b49f26f3d432c86caac52`, without establishing whether the user retried.
No packet/capture file was saved, no UDP request/authentication/gameplay traffic
was sent by the agent, and the SSH session closed normally. This result is
inconclusive: the actual server wire byte at the rejected delimiter was still
unknown at that checkpoint. It was resolved by the later observations below.

### E31.1 — confirmed opaque suffix and connection repair

Two later user-initiated retries, `876663323677d58680e8e1d78ad22cba` and
`6298e797884661bf56d3b87950d67db5`, still used diagnostic-only client `B60CE5...`
and failed at the same `server_identity` byte 43. During these retries, bounded
180-second, response-only passive observation confirmed the decimal server
identity is immediately followed by exactly lowercase **`m` (`0x6d`)**, then
ASCII space, an **explicit security flag `0`**, LF and NUL. The parser formerly
required the space immediately after the identity, so it rejected the suffix
before invoking the authentication provider. This was not a renderer crash,
invalid decimal identity, omitted secure flag, or proof of server rejection.

The safe observation record is
`manual-artifacts/task-records/e31-1-startup-failure-20261011/server-challenge-shape-20261011.json`.
Capture startup was observed for these later probes. Only digit-run lengths,
structural bytes and the explicit security flag were retained; no raw capture,
challenge token, actual server identity or auth data was saved. Timing links
these observations to the user retries; no retained token/endpoint correlation
is claimed to cryptographically bind a particular packet to a run.

`src/goldsrc/challenge_protocol.cpp::parse_challenge_response` now permits one
optional `m` after a canonical nonzero uint64 identity. It is an **opaque wire
suffix**, not an inferred authentication or security setting. The decimal
identity, challenge and independently parsed explicit `0`/`1` security flag
reach the existing provider unchanged. Missing flags, other suffixes, repeated
markers, malformed/overflowing numbers, alternate whitespace and malformed
terminators remain rejected. Datagram bounds, exact sender matching, deadlines,
auth material production, connect construction and server authority are unchanged.
The original suffix-free profile remains valid. This is a narrow observed
compatibility extension, not a promise of arbitrary ReHLDS/mod compatibility.

Eight added cases in `tests/test_challenge_protocol.cpp` cover both explicit
security flags, integer boundaries, every alternate nondigit suffix byte,
decimal continuation, malformed marker/number/separator combinations, original
datagram error offsets, every truncation and exact termination/size bounds.
Two added coordinator cases in `tests/test_handshake_connect_response.cpp`
verify unchanged provider inputs, provider failure remaining terminal (no auth
bypass), successful existing connect flow, duplicate update suppression and
single lifetime release. Fixtures are synthetic/project-owned; old assertions
were not weakened.

### Corrected Release — user acceptance and current identity

The user reports **«підключився грати можна»**. Current scoped acceptance is
`manual=passed`, `attribution=user_report`: connection and playability on
`192.168.0.104:27016`. This does not complete every video/weapon/UI regression
checkbox below or establish compatibility with all servers.

Independent local reports for user runs
`0ad783b28dd432e8395740e4481193b9` and `f265e0a45feac04233a55ce81c529e45`
bind the following client/launcher hashes to actual processes. Both record
protocol 48, `maps/crossfire.bsp`, completed sign-on/world readiness/gameplay,
`client_exit=0`, `normal_close_completed`, identity-bound receipts and no forced
child termination. Their shutdown outcomes respectively are
`server_disconnect_confirmed` and `transport_acknowledged`. These are two
successful user-initiated sessions and normal closes, not two agent-run tests.

| Current file | SHA-256 |
| --- | --- |
| `build/bin/Release/hlclient.exe` | `F5BEAAE55E3D45A5F81963172532FA2E796BD54C1DADAD877B7B87F3FBAF1C6D` |
| `build/bin/Release/HLClientLauncher.exe` | `3F757F466D01023BBB6812B28DD6DBD1A3FD400BD6E5A9D6376719943E83369E` |
| `build/bin/Release/SDL3.dll` | `D88767FB3F3D1AA4BB4C67BD0465C3EEC4258D8A83A8B0ED8CE9A114265C41A4` |

Six reports/receipts from each successful run were copied and hash-verified in
the existing E31.1 task record without changing originals. `manual-acceptance.json`
separates user testimony from independently observed receipts and limits the
acceptance scope. The automatic run reports deliberately retain their original
`server_live_compatibility=manual_confirmation_required`; user testimony is
recorded separately rather than rewriting machine-generated evidence.

The bounded status capture reached its byte cap in both sessions; complete
session stdout is not claimed. Durable identity-bound receipts, not truncated
stdout, establish gameplay/normal-close facts. WFP query status was
`query_unavailable`; no filter was altered or research-isolation success inferred.

Release build `05` and Debug build `02` passed after the last production edit.
Focused Release `02` and Debug `02` each passed **122 cases / 49933 assertions**,
no skips, including read-only validation of the explicitly selected official
x86 Steam API candidate (no Steam initialization or authentication during tests).
GUI offline CTest passed **1/1** in each configuration. The unchanged managed
launcher passed `-Prediction reference -RemoteAudioPeer -NoTimeLimit -CheckOnly`:
no game/Steam/WFP/network socket startup. Separate core-only Release `02`
built the client, launcher and API test target; **317 cases / 143413 assertions**
passed, null-renderer bootstrap without game assets exited 0, and offline GUI
plus architecture guard passed **2/2**.

The final `full-release-offline-02.log` in the **E31.1** task record passed after
the last production change: **3471 registered tests, 3407 passed, 64 skipped,
0 failed**, CTest **545.65 s**. The no-stock normal-close verdict test passed
in **107.58 s**. The complete `build_source.ps1 -Configuration Release
-Parallel 1 -TestOnly` driver subsequently exited **0**, including normal
version/help/Null bootstrap, local transport, functional-log and G/H summaries,
manual-launch/no-stock fixtures and `-CheckOnly -SourceOnly`. No configure,
submodule update or new production build ran during this final test-only suite.
All source/test changes were frozen before Release build `05`; later edits
only completed documentation and evidence records.

The 64 skips are not passes. Individual reasons remain in the CTest log,
including opt-in installed game assets/audio device checks, disabled live WFP
capabilities and unavailable filesystem link/input capabilities. The full-suite
optional Steam-file check was skipped because no runtime was selected for that
invocation; the explicitly selected read-only check passed in both focused
runs and is not retroactively counted as a full-suite pass. E31.1 did **not**
rerun ASan: the earlier `0xC0000135` runtime result remains unavailable/unconfirmed,
and its old build does not establish sanitizer coverage for this suffix fix.

Final identity/Git and preservation gates passed: unchanged branch/HEAD, staged
changes **0**, `git diff --check` exit **0**, all **1400** preserved files
hash-verified, missing baseline source `[]`, and the old managed launcher's SHA
unchanged. New successful run hashes match the final executable files exactly.
The long-running verification script started before user confirmation and
printed its old launch-time `new binary manual acceptance NOT RUN` sentence at
completion. That stale sentence is not the final verdict: the separately
executed `final-identity-02.log`, `final-identity-audit.json` and
`manual-acceptance.json` record the observed, identity-matched user success.

Earlier Debug `01` and core-only `01` executions lost tool sessions
without an observed final exit; process inspection showed no surviving build
writer before resuming. They are neither recorded as passed nor assigned an
unproven compiler-failure cause. No foreign process was terminated.

All SSH sessions closed normally. Server files/settings/VAC and container were
unchanged. Agent-initiated LAN/gameplay sessions and server UDP/auth requests=0.

## Known limits

- Windows native GUI and the current Win32 client/runtime; no new cross-platform
  launcher framework.
- Printable ASCII player names only under the existing name/wire contract;
  Ukrainian filesystem paths are a separate supported configuration concern.
- Literal IPv4 only, no DNS/IPv6/browser/scanner, no exclusive display mode and
  no live settings editor. Borderless uses actual desktop resolution.
- Half-Life `valve` and the existing protocol/auth/resource bounds only. No
  promise of arbitrary server, mod, bot-plugin or map-change compatibility.
- Required assets must already exist in the selected approved local copy;
  no FastDL, game-asset packaging, server-side installation or auth bypass.
- No agent-initiated live connection to `.102` or `.104` has been performed.
  GUI startup, child creation, synthetic transport, GPU pixels and PCM output
  do not establish successful LAN gameplay.

## Ручна перевірка користувачем

Відкрийте готову Release-збірку подвійним кліком:
`build\bin\Release\HLClientLauncher.exe`. PowerShell не потрібен. Власний
сервер із ботами має бути вже запущений, а Steam — відкритий. За потреби
виберіть свою ліцензовану папку Half-Life та офіційний `steam_api.dll`.

- [ ] Лаунчер відкривається без консолі й не підключається сам.
- [ ] Адреса `192.168.0.104:27016` уже заповнена. Ім’я, адреса, вибрана папка,
      відео, зображення та гучність зберігаються після перезапуску лаунчера.
- [ ] Tab/Enter працюють; при масштабуванні Windows усі поля доступні.
- [ ] Вибрана віконна роздільність справді застосовується. Для режиму без рамки
      використовується робочий стіл вибраного монітора; Alt+Tab та Escape
      звільняють/повертають керування штатно.
- [ ] Яскравість, гамма й контраст змінюють саме зображення гри після наступного
      запуску; «Скинути» повертає нейтральне зображення. HUD і приціл читаються,
      масштаб прицілів/FOV не зламаний, gamma інших програм не змінюється.
- [ ] «Підключитися» запускає рівно один клієнт, без HLDS та клієнта B.
- [ ] Статус проходить фактичні стадії; «У грі» відповідає завантаженій карті
      саме вашого сервера, а не примусово Crossfire.
- [ ] Видно ботів/інших гравців; рух, зброя, звуки, HUD, інвентар, kill notices,
      кров, Snark pop, смерть і респавн працюють.
- [ ] Закриття клієнта або «Від’єднатися» завершує лише клієнт. Сервер із ботами
      продовжує працювати, є фінальний звіт, можна підключитися повторно.
- [ ] Скасування під час підключення та недоступний сервер дають зрозумілий
      результат, не залишають другий/завислий клієнт і не позначають вхід успішним.
- [ ] Після повноекранної сесії підтверджуйте правильний режим лише якщо він
      справді працював. Відмова/незавершена перевірка повертає наступний запуск
      у віконний режим.

Якщо немає карти, не пройшла автентифікація чи сервер використовує непідтриманий
протокол/мод, відкрийте звіт і передайте точний `run_id` та причину. Саме відкрите
вікно не означає успішний зовнішній gameplay test.

Current candidate acceptance is `manual=passed`, `attribution=user_report`,
limited to the successful connection/playability reported above and supported
by two identity-bound gameplay/normal-close receipts. The broader feature
checklist remains unconfirmed; the historical `B60CE5...` candidate still
retains its failed-run record.
Agent-initiated gameplay sessions=0; authorized read-only SSH inspection is
separate from gameplay acceptance.
Staging, commit and push: none.
