# E33 — WON-style console and loading presentation

## Scope and acceptance boundary

Task: `HLC-E33-WON-STYLE-CONSOLE-AND-LOADING-SCREEN`.
Requested result: `won_style_console_loading_screen_integrated`.
Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`; accepted source checkpoint
`a2db33e83a1ce5c4eb5c13832c95d7bee0d2b8cf`.

The user's statement that Crossfire multiplayer gameplay is complete is recorded
as `attribution=user_report`, limited to that gameplay baseline. It is not proof
of every map/server, E33 manual acceptance, or stock-binary visual parity. E33
does not reopen the E32 movement audit, change WON/Steam networking, or modify
the accepted Studio chrome/lighting, weapon effects or physics.

New E33 manual acceptance remains `manual=not_run`. Agent-created live game,
Steam and HLDS sessions: **0**. Staging/commit/push: **none**. Per-edit originals
are retained under `manual-artifacts/task-records/e33-console-loading/source/`.
Offline test outcomes and executable identities belong to the final validation
section below; implementation presence alone is not a passing test result.

### Subsequent component-level manual report — E33.1, 2026-10-16

The `manual=not_run` statements and executable identities later in this document
retain their historical meaning at the original E33 handoff. The subsequent
E33.1 request supplies new evidence with `attribution=user_report`, not a blanket
acceptance of every E33 component:

- Confirmed: idle backdrop without autoconnect; acceptable background appearance;
  visible loading stages; EN/UA toggle; smooth half-screen opening and correct
  background proportions; toggle/Esc close.
- Confirmed: no gameplay input while typing and no stuck movement/attack;
  history, Tab and scrolling within the checked scope; volume, sensitivity and
  crosshair controls.
- Confirmed: disconnect preserves the application/backdrop; same-process
  reconnect; no observed stale session effects; quit does not stop the server.
- Reported unresolved/failing: `console_output_readability`,
  `reported_single_row_state`, `fullscreen_scrollback_area`, and
  `console_during_pre_world_loading`.

English in-game defaults and automatic upward loading-background retraction
are newly clarified E33.1 requirements, not fabricated prior manual failures.
The action preceding the single-row report is unknown; neither `clear` nor
Ctrl+L is attributed to the user. The correction, separate causal findings and
new-build validation are recorded in
[CONSOLE_OUTPUT_LAYOUT_LOADING_E33_1.md](CONSOLE_OUTPUT_LAYOUT_LOADING_E33_1.md).
The new E33.1 executable remains `manual=not_run` until tested by the user.

## Components and ownership

`ConsoleModel`, `ConsoleController`, typed command/cvar registries, the bounded
executor, bindings, configuration store and loading state are separate components
under `include/hlclient/console` and `src/console`. Scrollback/editor/history are
application-owned, not a living player's HUD. `ConsoleApplication` in
`include/hlclient/app/console_application.hpp` and `src/app/console_application.cpp`
joins the single SDL input pump, renderer and safe update boundary. The normal
application entry point wires this owner to fresh connection attempts; it does
not recursively run `main` or spawn a replacement client.

The existing platform, verified local resource opener, resource preparation,
typed gameplay action path, inventory catalogue, server request encoder, logger,
normal OpenGL renderer, image/HUD passes and durable reporting are reused.
No second asset engine, OS command shell, gameplay simulator or console renderer
with game-specific resource rules is introduced.

The game boundary exposes `game_api::ConsoleVisualTheme`, with a neutral default
for an alternate/core-only client. `HalfLifeClientModule::console_theme` supplies
the Half-Life title, ochre palette, emblem strokes and approved background name.
`src/games/halflife/console_commands.cpp` owns HL actions, weapon names and
server-command adapters. The module receives no SDL window or GL object.

The renderer receives only bounded neutral `ScreenUiFrame` image/text/rectangle
commands, each with optional clipping. The composition order is world, viewmodel
and effects; existing scene image correction and screen effects; existing HUD;
then console/loading UI. Consequently low health, player absence, death,
`hud_draw 0` and image gamma cannot make the console unreadable. The same final
pass works with no loaded world. Screen UI image uploads retain immutable owning
pixels, with an eight-entry/32 MiB cache. Per-command and total text/geometry
limits are checked before drawing.

`OpenGlRenderer::reset_session_resources()` is called only between attempts on
the GL context owner thread. It releases old world/brush/visibility, Studio
scene, HUD asset, decal, sky, image-copy and MSAA resources; zeros per-attempt
statistics and discards pending GPU timings. It preserves the application
window/context and screen UI resources. A former successful scene cannot provide
stale upload/first-frame evidence for a new connection.

## Appearance, font and resource provenance

The immediately available fallback is project-authored procedural artwork in
`src/app/console_screen.cpp`: a dark ochre panel with subtle grid/scanline detail
and module-described emblem. It needs no BSP, player model, server precache or
external font. Source-generated pixels are part of the normal client, not a
missing optional future asset. No Windows font file is distributed.

The background is cover-fitted without changing aspect ratio. In-game opening
slides and clips the image instead of squeezing it into half-height. The final
panel is 50% of the current gameplay viewport. Elapsed monotonic time drives the
180 ms local-compatible animation; changing direction while moving keeps the
current position. Exact WON duration, font metrics and pixel colors have not
been established from a matching binary and are **not stock-exact claims**.

The project-authored five-column bitmap font in
`include/hlclient/renderer/screen_ui_font.hpp` has uppercase/lowercase ASCII,
digits/punctuation and all 33 Ukrainian letters in both cases, including
`Ґґ Єє Іі Її`. It renders decoded UTF-8, not transliteration. Invalid codepoints
use a visible replacement glyph. It is deliberately not a general Unicode font
or an extracted Valve/System font. Input/history can retain valid UTF-8 beyond
the displayed glyph repertoire. Editor display scrolls horizontally on complete
UTF-8 boundaries, shows selection/caret and displays pasted newlines as spaces
until explicit Enter submission; original editor contents remain intact.

Read-only audit of the selected licensed copy
`D:\DEV\HLCLIENT-RESEARCH\Half-Life` found:

| Resource | Observed format and availability | E33 use |
| --- | --- | --- |
| `valve/gfx/conback.lmp` | 64,778 bytes; little-endian width/height 320×200, 64,000 indexed pixels, palette count 256, 768 RGB palette bytes | Optional original background, only from the selected approved root |
| `valve/gfx.wad` | WAD3; `LAMBDA`, `LAMBDA64`, `LAMBDA32`, `PAUSED`, `BACKTILE` type 66; `CONCHARS`, `CREDITSFONT` type 70 | Audited only; these are not interchangeable with a miptexture or BMP |
| `valve/fonts.wad` | WAD3 `FONT0/1/2`, type 70 | Audited; Ukrainian coverage and matching WON metrics not established |
| `valve/gfx/shell` | Key binding list files; no console background raster | No invented shell image |
| VGUI font TGA candidates | Present, but not established as the requested WON console font | Not used as an unverified replacement |

Audited original `conback.lmp` SHA-256:
`82830871562E8C54DD05198F272405A5991EEF91649B2DA4349F4E9F57783305`.
Palette index 255 is opaque for this format, not a masked-texture convention.
No original resource was copied into source control, rewritten, or redistributed.

`src/goldsrc/console_background.cpp` adds a narrow filesystem-free LMP decoder
and a preparation adapter over the existing root resolver, stable source identity
and verified opener. The decoder requires exact byte size, 1..2048 dimensions,
256 palette entries and bounded owning RGBA output. The adapter does not fall
through to another game installation. Missing, invalid, cancelled or rejected
original resources keep the ready project fallback. Preparation does not delay
the first fallback frame. The pinned official SDK revision remains
`b1b5cf5892918535619b2937bb927e46cb097ba1`; this is a source reference, not proof
of old WON engine UI internals. ReHLDS/Xash/proprietary dumps are not sources.

## Input, focus and command processing

The physical SDL grave scancode (left of `1`) toggles independently of EN/UA
layout and does not require Shift. Repeats do not retrigger and the opening
character is suppressed. SDL physical events and bounded owning Unicode text
events remain distinct. Text input starts/stops with console focus and window
focus; it is not a second event pump. Clipboard text is bounded and SDL-owned
allocations are released.

When the console owns input, text/navigation/wheel cannot reach movement,
weapon inventory or attack. Opening/focus loss/disconnect release held actions
through the existing typed input path; closing never resurrects the held attack.
Esc closes the console first and otherwise retains the existing release-mouse
behavior. F8 remains the explicit diagnostic marker. Grave/Esc/F8 bindings are
rejected instead of accepting ineffective overrides. Left/right modifier key
names are distinct (`CTRL/RCTRL`, `SHIFT/RSHIFT`, `ALT/RALT`).

The editor supports Left/Right, Home/End, Backspace/Delete, Up/Down history,
PageUp/PageDown, wheel scroll, Tab completion, Ctrl+A/C/V and Ctrl+End to return
to the bottom. Completion/history selection never executes. A multiline paste
requires Enter. Limits are 4,096 editor bytes/32 pasted lines, 128 history
entries, and 1,024 scrollback lines/256 KiB. Resize rewraps retained text.

Commands execute on a bounded safe update boundary, never a render callback or
prediction replay. Parsing supports whitespace, quoted arguments, escaped quote
and backslash, separators outside quotes, and line comments. An execution keeps
its source and session generation through aliases/config expansion. There are
operation-wide depth/command/byte/expansion budgets as well as per-update limits;
cycles terminate rather than persisting as an infinite small-per-frame loop.

## Command compatibility matrix

Subsequent message integration is recorded in
[E33.2](CONSOLE_GAMEPLAY_SERVER_MESSAGES_E33_2.md): committed DeathMsg text,
destination-aware TextMsg with approved titles lookup, chat/notify/center
presentation and explicit server failure reasons. The original E33 descriptions
and validation below remain historical. E33.1 console output/layout/loading was
accepted by the user (`manual=passed`, `attribution=user_report`, limited to
reported behavior); the new E33.2 build remains `manual=not_run`.

The tests below name real component/backend routes; final outcome counts are
reported separately. No unknown command is forwarded as arbitrary raw stringcmd.

| Commands | Status | Actual backend / constraints | Offline fixture coverage |
| --- | --- | --- | --- |
| `toggleconsole`, `clear`, `echo` | Implemented | Application controller/model; clear never deletes evidence | Console model/controller and application tests |
| `cmdlist`, `cvarlist`, `hlc_help [name]` | Implemented | Registered typed descriptors; `hlc_help` is a project extension | Registry/executor and application command tests |
| `connect <IPv4:port>`, `retry`, `disconnect`, `quit`, `exit` | Implemented with lifecycle constraints | Existing connection/auth/close path; retry is a new attempt, disconnect retains application, quit closes this client only | Application/lifecycle/receipt fixtures |
| `bind`, `unbind`, `unbindall`, `hlc_bindlist` | Constrained | Physical-key bindings; one command per bind; stable +action release ownership; reserved keys explicit | Binding/rebind/multi-key/focus tests |
| `+/-forward`, `back`, `moveleft`, `moveright`, `jump`, `duck`, `speed`, `attack`, `attack2`, `use`, `reload`, `showscores` | Implemented, local input intents | Existing typed gameplay actions, in-game state only; not raw server commands | HL registration/backend and application input tests |
| `slot1`–`slot5`, `invnext`, `invprev`, `lastinv` | Implemented, local menu/selection intents | Existing HUD menu and validated inventory selection, including previously confirmed weapon; accepted menu input is not a server-queued/success acknowledgement | HL catalogue/selection callback fixtures |
| `weapon_crowbar`, `weapon_9mmhandgun`, `weapon_357`, `weapon_9mmAR`, `weapon_shotgun`, `weapon_crossbow`, `weapon_rpg`, `weapon_gauss`, `weapon_egon`, `weapon_hornetgun`, `weapon_handgrenade`, `weapon_satchel`, `weapon_tripmine`, `weapon_snark` | Implemented, server-authoritative | Validated selection request against current catalogue; no locally granted inventory | HL registration/validated-selection tests |
| `exec`, `alias`, `hlc_writeconfig` | Constrained | Own bounded local config scope; aliases cannot implement compound held +/- scripts; explicit save extension | Parser/alias/exec/config/secret fixtures |
| `say`, `say_team` | Constrained | Explicit validated server adapter; quoted printable ASCII, at most 120 bytes; UTF-8 editor is not assumed to be GoldSrc wire text | Server-request encoder and HL adapter tests |
| `status`, `kill`, `drop [weapon]` | Constrained | Existing typed outgoing requests; queued request is not a server-success acknowledgement | Server-request encoder/adapter tests |
| `name`, `model`, `topcolor`, `bottomcolor` | Unsupported at runtime | No validated outgoing userinfo update path; use launcher for a later connection | Explicit rejection tests |
| `reconnect`, `wait` | Unsupported | No asserted reconnect/retry equivalence; no artificial sleep scheduling | Explicit rejection tests |
| `map`, `changelevel`, `rcon`, `god`, `noclip`, `record`, `playdemo`, `stop` | Unsupported | No fake local server/admin/demo authority | Registry rejection fixtures |
| `net_graph`, `rate`, `cl_cmdrate`, `cl_updaterate`, `ex_interp` | Deferred/unsupported | No invented measurement or decorative network scheduler setting; E32 unchanged | Explicit rejection fixtures |

## Cvar mappings

Querying a registered cvar reports requested/applied/default/range and status.
Values are validated before mutating a real setting. These mappings are the
supported E33 profile, not full GoldSrc cvar compatibility.

| Cvar | Range and mapping | Application/persistence |
| --- | --- | --- |
| `sensitivity` | 0.1..30; default 3 preserves existing 0.10 degrees/pixel; existing zoom multiplier retained | Live input multiplier; own config |
| `volume` | 0..1 mapped to nearest integer 0..100 backend volume; actual value reflects quantization | Live master volume; shared launcher setting |
| `crosshair`, `hud_draw`, `hud_fastswitch` | Boolean 0/1 | Actual HUD/inventory policy; console independent; own config |
| `cl_showfps` | Boolean 0/1 | Actual presentation frame timing, no invented server statistics; own config |
| `fps_max` | 0 unlimited or 20..1000 | Presentation cap only, no physics/usercmd timestep alteration; own config |
| `hlc_gamma` | 0.25..4 | Existing scene image correction; shared launcher setting |
| `hlc_brightness` | -0.5..0.5 | Existing additive image correction; shared launcher setting |
| `hlc_contrast` | 0..2 | Existing contrast mapping; shared launcher setting |
| `hlc_hud_scale` | 0 automatic or 0.5..2 | Existing HUD scaling; shared launcher setting |
| `hlc_msaa` | Requested 0, 2, 4 or 8 | Existing scene target, actual GPU fallback observed after render; shared launcher setting |

`hlc_*` image/video names avoid asserting stock numeric equivalence. Console
text/background are not tinted by these scene-only corrections.

## Trust, configuration and logs

Local console, local cfg and local bindings are explicit execution sources.
Server-origin data cannot execute local aliases/exec/bind/connect/file operations.
Server print/TextMsg/SayText are display data. Existing narrow `svc_stufftext`
protocol policy remains separate. Server requests are constrained adapters, not
an arbitrary command forwarding escape hatch, and report pending server results.
TextMsg formatting is bounded, inert `%s`/`%%` substitution, not an executable
format string. Unresolved `#titles` keys and unused parameters remain visible;
`%n` remains literal. This does not claim stock localization/table lookup parity.

The per-user console scope is `%LOCALAPPDATA%\HLClientEngine\console` on Windows.
Only safe local `.cfg` basenames are accepted. Absolute paths, traversal, device
names and reparse escapes are rejected. Reads/writes are bounded, versioned and
UTF-8; writes use unique temporary files, flush and atomic replacement. Windows
directory handles prevent a reparse/rename swap while a file is accessed. Stock
`config.cfg`, asset roots and server downloads are never auto-executed or written.

A native probe explained one pre-handshake startup failure in the packaged task
environment: the broad `%LOCALAPPDATA%` directory handle had an exact ordinary
path, but Windows redirected its newly created private capability lock into the
Codex package's `LocalCache\Local` directory. The unchanged secure opener rightly
rejected that lock's different final path. The same lock under the already-existing
exact `HLClientEngine` child retained its requested path. Application receipt scope
preparation therefore securely opens that fixed existing application directory
first; creation when absent still requires a verified pinned parent. Exact final
path, ordinary-file/directory, link-count and reparse protections are not relaxed.
Both unique diagnostic locks were removed automatically on handle close, with no
old files deleted. Evidence:
`manual-artifacts/task-records/e33-console-loading/startup-native-msix-capability-probe.log`.
This observed MSIX behavior is not asserted for every Windows installation.

Startup launcher/direct CLI values initialize the application. The own
`console.cfg` provides persisted console-only cvars/binds/aliases. Shared launcher
image/audio/MSAA/HUD fields are not emitted into that file: console edits merge
changed fields into the shared launcher configuration instead of rewriting its
entire stale snapshot. Explicit config commands can still request validated
runtime changes. `hlc_writeconfig [safe.cfg]` saves only this project's scope.

The canonical shared file is `%LOCALAPPDATA%\HLClientEngine\launcher.json`.
Client and launcher serialize read/three-way-merge/atomic-replace: a field changed
relative to that editor's baseline wins, while unchanged fields retain the newer
stored value. Explicit same-field edits win a conflict. The disabled launcher
form reloads this file after its child exits; merely closing the launcher does
not write its old launch snapshot back. Invalid current settings are not silently
replaced by the merge. Direct `--open-client` can reuse saved launcher choices;
an explicitly supplied resource root or Steam runtime path remains authoritative.
With no saved resources it still opens the fallback console, and a later connect
reports the missing configuration instead of silently selecting an installation.

Implicit startup processing of this own `console.cfg` cannot run `connect`,
`retry`, `disconnect`, `quit` or `exit`, including through alias/exec expansion.
This preserves the user's choice to open idle and prevents configuration from
redirecting an automatic managed attempt or closing the window. Cvars, bindings
and aliases still load. After startup, explicitly entered console commands or
an explicit `exec own.cfg` may request those session actions through the same
validated local dispatcher. This restriction does not grant server text a local
execution source.

Sensitive commands are excluded from history/config persistence. The bounded
console sink redacts known secret-bearing diagnostics before queuing. The
existing file/terminal logger is retained; no blocking GUI callback replaces
`std::clog`. Worker/network output is queued and drained at update, with at most
512 records/256 KiB, 4,096 bytes per record, 256 queued messages per second and a
reported dropped-line count. No GL calls occur under the logging mutex. These
limits/redactions are defensive bounds, not permission to log authentication
material in other diagnostics.

## Loading and reconnect lifecycle

The application lifetime is longer than any connection attempt. The first usable
frame presents the ready fallback; approved background work and resource jobs
then report real stages. Stages distinguish connection, authentication, sign-on,
resource checks, map, textures/models and scene readiness. Unknown totals are
indeterminate; a percentage is allowed only for real done/total and cannot reach
100 before readiness. Sign-on, world-ready and first successful presentation
remain separate observations.

Loading work has a session generation and cancellation signal. Stale cancelled
results cannot publish into the next attempt. CPU preparation does not perform
GL operations on its worker. The sole application event/update/render loop stays
responsible for console input and cancellation; importers do not recursively
pump events. Errors remain visible, and Ctrl+C without a selected editor range
copies the last loading error. Each new connection uses fresh session/parser/
gameplay state, with old entities, sounds and queued commands discarded.

Disconnect returns to the full backdrop and console without discarding history.
Retry/connect do not launch another process or own the external server. The
normal close path continues to provide bounded protocol disconnect and durable
per-attempt results, without overwriting an earlier failed attempt with a later
success. Managed A/B invocation preserves its scoped owned-process contract.

Normal interactive transitions are `idle → loading → in_game → disconnecting →
idle`; a failed attempt returns to a visible recoverable error console, from
which `connect`/`retry` create a new clean attempt. Requesting another connection
while loading/in-game first cancels and closes the old attempt. Outstanding CPU
jobs are cancelled and awaited before the next generation is installed; session
callbacks are detached before their owners die. A finite managed test retains
its duration/terminal-failure contract. Native socket-close or durable-output
integrity failures remain application failures, not recoverable successes.

The receipt close reason `console_disconnect` is distinct from `startup_cancel`,
`window_close`, `server_disconnect` and `fatal_failure`. A clean explicit
disconnect may be a bounded graceful goodbye or cancellation before a protocol
disconnect is possible. Neither implies that gameplay was reached. `quit`/`exit`
and native window close finish the application; they are not aliases for
`disconnect`.

`ConsoleSessionAttempts` permits at most 64 sequential attempts per application.
The first attempt preserves the managed launch's original root, 32-hex run ID
and role. Subsequent attempts receive fresh IDs under `attempts-A/<id>` or
`attempts-B/<id>`. Start/result leaves are immutable and role-qualified:
`console-attempt-A-start.json`, `console-attempt-A-result.json` (and B equivalents).
The final `console-application-A-attempts.json` or B index retains every result,
including earlier failures. Thus two managed roles sharing an original root/run
ID do not overwrite one another. A direct client creates its own secured scope
under `%LOCALAPPDATA%\HLClientEngine\runs` on the first attempt; opening and
closing idle without connecting creates no gameplay attempt. These bookkeeping
records explicitly contain `gameplay_acceptance=false`; accepted gameplay still
requires the existing identity-bound durable facts, not a zero return code.

The launcher separates historical failures from the current phase. It retains
the original failure/cleanup reasons in the report, while a validated later
`connecting`, `in_game`, `idle` or recoverable-error marker updates the visible
phase. An earlier failed attempt cannot pin the form permanently in its old
error state. Final `completed/application_closed` with native exit 0 and no
forced termination means only that this application closed cleanly. A previous
attempt failure stays recorded; a nonzero native exit or output-integrity
failure cannot be reclassified as success by that marker.

## Offline application hooks and launch commands

The existing `HLCLIENT_SMOKE_TEST_FRAMES` environment hook is used only by bounded
offline acceptance. When engaged, the console owner does not load user startup
configuration. The `--open-client` idle smoke additionally skips saved launcher
settings and original-background I/O, presents the project fallback and exits
after two idle frames. `scripts/test_console_idle_e33.ps1` verifies the actual
first-frame marker and rejects network/authentication/gameplay activity. The
deferred-handshake fixture uses a deliberately high frame cap plus a finite
managed attempt, leaving its real owning challenge deadline in control. These
are deterministic test-isolation policies, not production launch prerequisites;
ordinary user launches must not set this environment hook.

Open the updated graphical launcher from PowerShell:

```powershell
& 'D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\HLClientLauncher.exe'
```

Its **«Відкрити клієнт»** action opens the application without connecting. Its
**«Підключитися»** action uses the form's saved endpoint/settings. For a direct
idle entry using saved resource choices when available:

```powershell
& 'D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe' --open-client
```

In that client's console, explicitly enter `connect 192.168.0.104:27016` for the
user's currently selected LAN server, then `disconnect`, `retry` or `quit` as
needed. That address is an example of the preserved user choice, not an E33
compatibility result or a request for an agent-created live test. No additional
client/server process is created by these in-client commands.

Read-only acceptance of the existing managed two-client launch contract remains:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File 'D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1' -Prediction reference -RemoteAudioPeer -NoTimeLimit -CheckOnly -SourceOnly
```

`-CheckOnly -SourceOnly` validates arguments/files without creating a live
client/server session. Do not omit those switches when performing offline
acceptance.

## Offline visual evidence and validation record

Production GPU fixtures are in `tests/test_console_screen_e33.cpp`. They verify
opaque palette decoding, actual Ukrainian glyph/caret pixels and scissoring,
console above HUD/image correction, unchanged lower-half game pixels, and exact
top-half crop agreement with the full image at 640×480, 1920×1080 and 3440×1440.
The optional `HLCLIENT_E33_SCREENSHOT_DIR` writes actual framebuffer PPM evidence:
`startup-before-WxH.ppm`, `startup-after-WxH.ppm`, `console-half-WxH.ppm`, and
`console-cyrillic-caret.ppm`. These are not screenshots of a live game session.
The optional approved-asset control uses `HLCLIENT_CONSOLE_ASSET_ROOT` and never
copies original asset bytes into the repository.

The first focused run encountered a missing screenshot output directory; that
fixture failure was retained, not relabeled as a renderer failure or a skip.
After explicitly creating the evidence directory, the Release screen-only run
passed **5 cases / 10,130 assertions**, including the approved original source
pixel comparison and session reset controls. See
`manual-artifacts/task-records/e33-console-loading/screen-release-retry2.log`.
This is a component result, not the final full-suite or manual verdict. PPMs were
converted losslessly to PNG only for inspection. Visible samples:

- [Prior empty startup framebuffer, 1920×1080](../manual-artifacts/task-records/e33-console-loading/screenshots/startup-before-1920x1080.png)
- [Project fallback, 1920×1080](../manual-artifacts/task-records/e33-console-loading/screenshots/startup-after-1920x1080.png)
- [Project fallback half panel, 3440×1440](../manual-artifacts/task-records/e33-console-loading/screenshots/console-half-3440x1440.png)
- [Approved original background, 1920×1080](../manual-artifacts/task-records/e33-console-loading/screenshots/original-startup-1920x1080.png)
- [Exact Cyrillic glyph/caret/clipping control](../manual-artifacts/task-records/e33-console-loading/screenshots/console-cyrillic-caret.png)

The actual local `conback.lmp` is a dark diagonal patterned image with Valve
lettering near the lower edge, not the authored ochre lambda fallback. Both are
shown honestly; neither is asserted to be a specific historical WON binary's
pixel-exact screen. The expanded composed-application fixture adds real Unicode
input/echo output, status lines, editor and caret screenshots at native and
ultrawide sizes. These actual composed UI captures were visually inspected:

- [Application startup console, 1920×1080](../manual-artifacts/task-records/e33-console-loading/screenshots/application-console-startup-1920x1080.png)
- [Application half console, 1920×1080](../manual-artifacts/task-records/e33-console-loading/screenshots/application-console-half-1920x1080.png)
- [Application startup console, 3440×1440](../manual-artifacts/task-records/e33-console-loading/screenshots/application-console-startup-3440x1440.png)
- [Application half console, 3440×1440](../manual-artifacts/task-records/e33-console-loading/screenshots/application-console-half-3440x1440.png)

The half panel ends at y=540/y=720 respectively. The fixture's lower-half
backdrop remains exactly RGB(20,36,46) across 1,036,800/2,476,800 pixels.
The emblem is cover-cropped without nonuniform scaling, and Ukrainian output,
history, selection/editor and caret are legible. The flat backdrop is an offline
GPU fixture, not a claimed screenshot of live gameplay. Likewise the initial
empty framebuffer comparison is a controlled pre-UI fixture, not historical
stock or user-run evidence.

### Final normal-build validation — 2026-10-16

The final production change is the verified application-directory receipt scope
and its diagnostic reporting. All normal results below were rerun after that
change. Later documentation edits and the ASan-only test-driver optimization
option do not change normal production code or its assertions. Evidence files
are under `manual-artifacts/task-records/e33-console-loading/`.

| Validation | Actual result | Retained evidence |
| --- | --- | --- |
| VS2022 / Win32 complete Release build, warnings-as-errors ON | PASS | `build-release-final.log` |
| VS2022 / Win32 Debug client, launcher and complete focused E33 target | PASS | `build-debug-final.log` |
| Focused Release `[e33]`, including approved original resource and actual GPU | **65 cases / 76,597 assertions, all pass, no skips** | `focused-release-complete-final.log` |
| Focused Debug `[e33]`, same resource/GPU controls | **65 cases / 76,597 assertions, all pass, no skips** | `focused-debug-final.log` |
| Complete final Release source/offline suite | **3,804 registered CTest entries: 3,734 passed, 70 explicitly skipped, 0 failed**; 370.69 s CTest wall time; remaining source verification commands also pass | `full-release-final.log`, `full-release-final-ctest-details.log` |
| Normal application preparation before first challenge and bounded owning timeout | PASS: real client UI/multiple presentations, owned silent UDP peer, typed timeout/exit 2; no auth initialization/game session | `startup-verified.log`; repeated successfully in the full Release suite |
| Actual Debug idle first frame, Null renderer and launcher offline UI check | PASS | `debug-application-final.log` |
| Core-only complete Release build, `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` | PASS; existing core cache has warnings-as-errors OFF, unlike normal/ASan caches | `build-core-final.log` |
| Final core-only CTest suite | **45/45 CTest entries pass**; 37.84 s; includes no-assets console startup and architecture guard | `core-release-final.log`, `core-release-final-ctest-details.log` |
| Explicit alternate/core game API subset | **29 cases / 598 assertions pass**, including the E33 non-HL module text/provenance route; core Null startup also passes | `alternate-core-null-final.log` |
| Architecture/game boundary | PASS in both normal Release and core-only suites | Full/core CTest logs above |
| Launcher, no-stock A/B reporting/restoration/failure fixtures | PASS; normal source verification also runs `test_h2_manual_launch.ps1` | `full-release-final.log` |
| Explicit two-client `reference`, `RemoteAudioPeer`, `NoTimeLimit`, `CheckOnly`, `SourceOnly` | PASS; no live processes/sockets/Steam initialized | `launcher-ab-checkonly-final.log` |
| `git diff --check` | PASS (line-ending conversion notice only, not whitespace failure) | Final source check; index remains empty and HEAD unchanged |

The 70 skipped CTest entries are not counted as passes. Their exact names are
listed at the end of `full-release-final.log`; the retained 4.8 MB detailed CTest
log records each reason. They cover unselected optional installed-asset audits,
non-enabled live WFP capability controls, unavailable symlink/reparse capability,
foreground mouse-capture prerequisites, optional actual audio output, and
external/private corpus prerequisites. The two optional E33 original-background
cases skipped in the asset-free full suite were separately executed and passed
in **both** focused runs with `HLCLIENT_CONSOLE_ASSET_ROOT` pointing to the
selected licensed copy. The aggregate core API binary also reports its optional
original-sky asset audit as an internal skip; a passing aggregate CTest entry
does not turn that internal skip into an asset validation pass.

The final source verification includes `--version`, `--help`, normal Null,
transport self-test, functional-log observation, G/H summary tests, manual
launcher no-stock fixtures and `-CheckOnly -SourceOnly`. Neither a successful
idle frame nor these fixtures is evidence of gameplay/map entry. Agent-created
live/Steam/HLDS sessions remain **0**.

### Retained first failures and retries

No failure log was overwritten or relabeled as a pass:

- `build-focused-first.log`, `build-focused-retry2.log`,
  `build-integration-first.log`, `build-integration-retry2.log`: initial compile
  issues included ambiguous UTF-8 helper naming, source encoding/narrowing,
  an unused callback parameter and an SDL3 key constant. Corrected before the
  final production build; old assertions were not relaxed.
- `focused-release-first.log`: initial screenshot output-directory failure and
  console loopback endpoint-policy failure. The evidence directory was created;
  console endpoint validation was integrated without relaxing the GUI launcher's
  existing default endpoint policy.
- `build-release-all-first.log`: new core-api test link dependency plus an
  attempted assignment of immutable test resource state. Fixed linking and
  fixture construction; `build-release-all-retry2.log` passed.
- `focused-release-final.log` is an earlier **nonfinal failed attempt**, despite
  its historical filename: 42/43 cases passed. Its new fixture alias collided
  with the explicitly unsupported built-in `stop`; the fixture alias was renamed
  without weakening assertions. Subsequent 43-case intermediate runs passed.
- `full-release-first.log`: 3 failed CTest entries represented **two causes**:
  the new receipt fixture omitted the QPC frequency required by its own timed
  close record (both aggregate and individual entry), and direct startup rejected
  the redirected broad-parent capability file. The fixture was corrected while
  retaining strict receipt validation; production now uses the verified specific
  application directory. `startup-native-before.log` and
  `startup-native-msix-capability-probe.log` retain the native evidence.
- The complete focused target was expanded to include E33 cases added in older
  launcher, receipt, application-close, CLI, asset-loading and game API files,
  rather than testing only newly named console files. Final focused coverage is
  65 cases, not the earlier 43-case subset.

### ASan — separate build and runtime verdict

The initial instrumented focused build passed (`build-asan-focused.log`). Runtime
did not start: direct invocation exited `0xC0000135` (DLL not found); with the
matching installed MSVC 14.34.31933 x86 runtime directory added only to the child
process PATH it exited `0xC0000142` (DLL initialization failed), including
`--help`, with zero output. This is **not an ASan runtime pass**.

The expanded final `/O2` driver build was deliberately interrupted after over
15 minutes of active compilation in `test_runtime_replay_local_assets.cpp`;
`build-asan-final.log` preserves the resulting `CL.exe` exit -1. Only the verified
task-owned compiler was stopped, not a user client or unrelated build. The
focused ASan **test driver only** now uses `/Od /Ob0`; address instrumentation
and production-library optimization are unchanged. No test assertions were
removed. The final expanded build **passed** with that diagnostic driver profile
(`build-asan-driver-od-final.log`). The final binary was then probed again:

| Final runtime probe | Native result | Output |
| --- | --- | --- |
| `--help`, unmodified child search path | `0xC0000135` / -1073741515 | stdout/stderr both 0 characters |
| `--help`, matching toolchain directory in child-only PATH | `0xC0000142` / -1073741502 | stdout/stderr both 0 characters |
| `[e33]`, same child-only PATH | `0xC0000142` / -1073741502 | stdout/stderr both 0 characters |

See `asan-runtime-final.log`. **Final ASan build=pass; runtime=unavailable before
test execution**, not a passing sanitizer test suite. E33 sanitizer wiring covers
the console, application console, console asset adapter, external launcher and
focused test targets in addition to previously instrumented targets; it is not
a claim that every renderer dependency is instrumented. The historical absence
of a confirmed ASan runtime therefore remains explicitly unresolved.
No DLL was copied, no machine/global PATH was changed and no sanitizer-runtime
infrastructure repair was attempted.

### Ready Release identities

Normal client:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`

SHA-256:
`208F897CC3E841DBBFB3D9D92BEEFC779AFB7C0D35B38F117A0B5BC02D4186AE`

Updated GUI launcher:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\HLClientLauncher.exe`

SHA-256:
`4954BD85FF7B661D0AE9CFDC807CD01141AA90D0FCEE2E1D08F73C2ECEDD5688`

These are new E33 build identities, not identities attributed to any earlier
user screenshot/run. The existing managed launcher resolves the normal Release
client; the updated GUI launcher uses its normal sibling client. No copied
Valve UI resources, debug executable or hidden feature flag is required.
HEAD remains `a2db33e83a1ce5c4eb5c13832c95d7bee0d2b8cf`; no staging, commit or push
was performed. Normal binaries are ready; **E33 manual=not_run**.

## Manual acceptance

Normal launcher: `build/bin/Release/HLClientLauncher.exe`. Use its existing saved
endpoint (currently user-selected `192.168.0.104:27016`) or its open-client action;
E33 preserves that saved user choice. Normal client: `build/bin/Release/hlclient.exe`.
The `--open-client` entry opens the application idle without autoconnect; use the
normal selected licensed resource/Steam runtime settings when connecting.

- [ ] Ready HL-style background appears before map preparation, with real stages.
- [ ] Physical key left of 1 works in EN and UA; panel opens to half viewport.
- [ ] Esc/toggle close without a spurious new shot; held actions require a fresh press.
- [ ] History, completion, Cyrillic editor, clipboard and scrolling work.
- [ ] Rebinding changes real controls and releases cannot get stuck.
- [ ] Volume, sensitivity, HUD settings and actual MSAA reporting work.
- [ ] Server messages display as data; failed connection leaves a useful visible reason.
- [ ] Disconnect retains the window/history; connect/retry work in the same process.
- [ ] New attempts have no stale entities/effects and preserve separate receipts.
- [ ] Quit closes only the client; the external server remains running.

Known limits: no stock/WON pixel parity claim, no general Unicode font, constrained
ASCII server chat, no live userinfo edits, no compound held-action scripts, no
legacy WON networking and no extra server/admin/demo functionality. Offline
fixtures do not replace this manual checklist.
