# E33.1 — Console output, layout, English defaults and loading transition

## Subsequent manual acceptance — E33.2 request

The user confirmed that the console now works: **E33.1 manual=passed**,
`attribution=user_report`, `scope=reported console output/layout/loading behavior`.
This does not establish every command, resolution or ASan runtime. The original
new-build `manual=not_run` records below remain historical. The next integration
is tracked in `CONSOLE_GAMEPLAY_SERVER_MESSAGES_E33_2.md`.

## Scope and baseline — 2026-10-16

Task: `HLC-E33-1-CONSOLE-OUTPUT-LAYOUT-ENGLISH-LOADING-TRANSITION`.
Requested result: `console_output_layout_and_loading_transition_corrected`.
Repository: `Pvitaly91/hl-client-engine`.
Authoritative worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`.
Branch: `codex/stock-runtime-campaign-5e48b7c1`.
Published checkpoint: `a2db33e83a1ce5c4eb5c13832c95d7bee0d2b8cf`.

The local uncommitted E33 implementation is the source baseline, not that older
published tree. Its existing changes were preserved. Pre-edit source copies are
under `manual-artifacts/task-records/e33-1-console-output-layout/source/`;
`baseline-status.txt` and `baseline-binaries.txt` in that task artifact directory
record the initial state. No reset, clean, restore, stash, rebase, amend or branch
switch is part of this correction. Staging/commit/push: **none**.

This task changes application console presentation and output delivery. It does
not reopen prediction/E32, collision, weapon timing, server authority, inventory,
Studio chrome/lighting, MSAA, blood/decals, the network protocol or server setup.
Agent-created live game, Steam and HLDS sessions: **0**. New-build manual status:
**`manual=not_run`**. Final validation and binary identities below come from
the completed E33.1 runs, not inherited E33 results. Production/offline result:
`console_output_layout_and_loading_transition_corrected`, with the exact
reported manual single-row trigger and pre-world interval still not established.

## Manual evidence retained by component

The E33.1 request supplies the following E33 results as
`attribution=user_report`, limited to the user's exercised scenarios:

| Component | Reported result |
| --- | --- |
| Idle entry | Backdrop without autoconnect; acceptable background appearance |
| Loading | Loading stages were visible |
| Console visibility | EN/UA toggle; smooth half-screen opening; correct background proportions; toggle/Esc close |
| Input isolation | No gameplay input while typing; no stuck movement or attack |
| Editing/navigation | History, Tab and scrolling worked within the checked scope |
| Settings | Volume, sensitivity and crosshair worked |
| Disconnect/reconnect | Disconnect preserved the application/backdrop; same-process reconnect worked; no stale session effects were observed |
| Quit ownership | Quit did not stop the external server |

Four separately reported unresolved/failing areas are
`console_output_readability`, `reported_single_row_state`,
`fullscreen_scrollback_area` and `console_during_pre_world_loading`.
English defaults and automatic loading-background retraction are **new clarified
requirements**, not invented failures of a previously promised/manual-tested
feature. This is not a blanket `E33 manual=passed` verdict.

The request names `image(10).png` and `image(20261016-190705).png`. At the time of
this initial record their actual local image files have not been located; no
pixel inspection of those named user screenshots is claimed. The reported key
or action preceding the single-row symptom remains **unknown**. In particular,
the report is not reinterpreted as a user pressing `clear` or Ctrl+L. Controlled
offline framebuffer captures are distinct from those user screenshots.

## Output audit and production correction

### Producers and ownership

| Kind | Production route and storage |
| --- | --- |
| Command echo | `ConsoleApplication::route_event` submits the editor and appends a safe `] command` record directly to `ConsoleModel` |
| Command results | Bounded `CommandExecutor::process` callbacks append results directly; `cmdlist`/`cvarlist` return multiple lines |
| Useful session notices | Application-owned connection/ready/disconnect/error messages append directly; explicit logger notices use `LogChannel::user_notice` |
| Server display text | Committed server-message adapters call `core::log_server_text`; bounded UTF-8 repair, per-line prefix and sensitive-data filtering precede `LogChannel::server_text` delivery |
| Technical session/runtime telemetry | Producers use the diagnostic channel; the existing terminal/report output remains, but ordinary info/debug do not enter user scrollback |
| Warnings/errors | Logger delivery remains visible independently of the diagnostic channel; queue limits and dropped counts remain explicit |
| Loading status | `LoadingPresentation`/generation-owned `LoadingWork` update a status element, not a repeated history record |
| Editor/prompt | Independent editor state and a separate text command; not a mutable final log record |

The user-output path after storage is `ConsoleModel::visible_lines` wrapping and
anchor selection, `ConsoleApplication::compose`, neutral clipped `ScreenUiText`
commands, and the existing OpenGL screen-UI pass. Screen-UI scissor state is
restored by that pass; it does not own history or execute displayed data.

### Established cause of service spam

`src/goldsrc/live_runtime_stage.cpp` emits
`LiveRuntimeStageEventType::usercmd_packet_submitted` after each successfully
submitted usercmd packet. `log_live_runtime_trace` in `apps/hlclient/main.cpp`
mapped every such event to the same info-level
`[session-progress] live_usercmd_transmission=active` message, without a state
change requirement. Service-payload and runtime-record diagnostics also repeat
as the session runs. This is packet/event-frequency output, not a single
connection transition; no fixed frequency is inferred from the user's run.

Before E33.1, `core::log` queued every enabled log for the console.
`ConsoleApplication::begin_frame` drained and appended those records. The queue
rate/size limits could discard a real error behind telemetry; the model's
1,024-line/256 KiB FIFO could evict earlier command output. This producer did
**not** call `clear`, replace a mutable status row or set the visible-row count.

`include/hlclient/core/log.hpp` and `src/core/log.cpp` now distinguish typed
`diagnostic`, `user_notice` and `server_text` channels. Generic technical logging
defaults to diagnostic. Existing minimum severity, redaction, message contents
and terminal/report sink are retained; there is no English-substring filter or
global logging/FPS reduction. Warnings/errors remain visible. A separate error
rate allowance and reserved queue capacity keep the first critical error from
being consumed by an ordinary warning/server-notice burst. Hard limits remain
512 records/256 KiB, 4,096 bytes per record; ordinary delivery is limited to
256/s with 480 records/248 KiB available, critical delivery to 64/s within the
overall capacity. Counts describe bounds, not guaranteed unlimited history.

### Established layout restriction

The previous composition used
`min(viewport_rows, 6000 / columns)`. It reserved every possible column for every
row rather than counting the actual text, so even short command-list rows lost
usable vertical area. The failing 1280x720 full-panel fixture drew **28 rows
where 56 fit**, with the first row starting at y=374 rather than near the
available output top. This is actual composition evidence, not an inference
that the screenshot proved a font, alpha or OpenGL failure.

`console_output_layout` in `src/app/console_screen.cpp` derives one rectangle
from actual framebuffer pixels, stable full/half panel height, header/status,
editor and padding. Capacity is `floor(output.height / line_height)`. History
uses that capacity; short real history may remain bottom-aligned. Actual text
bytes/glyphs are budgeted separately. Only pathological dense output is visibly
shortened per row with an ellipsis, without deleting retained records or
reducing the number of real available rows. No fake blank history records,
global HUD/FOV changes or arbitrary font resizing are used.

### Single-row reproduction versus unknown user action

An independent old-code defect is established: `ConsoleModel::scroll` allowed
an offset of `stored_lines - 1`. `visible_lines` then excluded that many logical
records **before** wrapping, so scrolling to the oldest endpoint left only the
first record even when a 12-row viewport and 120 records existed. A single long
record also could not be scrolled by its wrapped visual rows, and resize/new
output could shift what the user was reading.

The new model uses a logical record identity and UTF-8 byte-end anchor with a
wrapped-row window. Its oldest legal window is full when enough history exists;
append, resize and FIFO eviction retain the logical reading location as far as
possible. Tail mode remains tail; old-reading mode is not forcibly reset every
frame. CR, LF and CRLF are separate line boundaries. `clear` remains explicit
and clears displayed output, not command history or evidence files. A genuinely
one-line history is still valid.

The fixture establishes a mechanism capable of the reported single-row result.
The **exact action and root-cause attribution for the user's particular
screenshot remain `not_established`** without the missing image/action evidence.
Neither lost storage nor a specific key press is claimed for that manual run.

## Pre-world responsiveness boundary

The E33 event/update loop and asynchronous resource/BSP/texture/model preparation
already provided pre-world UI pumping; these paths were not all absent.
The separately established blocking gap was authentication:
`SteamAuthenticationOperation::update` in
`src/app/steam_authentication_provider.cpp` called native initialization,
callbacks and game-connection initiation synchronously from the application's
session update. The staged native-API fixture measured a **300.0143 ms** blocked
poll, exceeding its 100 ms responsiveness check, with all ten observed native
calls on the caller thread. This is offline fake-native evidence, not a live
Steam measurement or proof of the precise interval encountered by the user.

`make_console_steam_authentication_provider` in
`src/app/console_steam_authentication.cpp` adapts only the normal console-owned
Steam path to one serialized native worker. Material delivery is once-only;
session-lifetime ownership remains on that worker. A bounded 64-event trace
mailbox is drained on the UI owner. Cancellation may leave native cleanup
pending while the application keeps pumping UI before final join. Native ABI
calls cannot be forcibly preempted. The existing non-console/scripted route is
not redefined, and no importer gains recursive event pumping or GL ownership.

Toggle, editor, echo/cmdlist and cancel do not depend on a live player, alive
state, world readiness or gameplay capture. Application/session generation and
cancellation boundaries continue to reject stale work. No new authentication,
server or Steam live session is required for these offline fixtures.

## English defaults and visibility policy

Own in-game defaults are English: welcome/help/status, idle/loading labels,
connection/cancel/error results, command/cvar descriptions and validation,
unsupported capabilities and console hints. Existing command/cvar/backend text
was already English; the remaining Ukrainian welcome, loading labels and hint
were changed. No locale-selected default or localization framework was added.
The separate Windows launcher language is unchanged. Commands, protocol names,
assets, player names, user input and server text are not translated. EN/UA
physical toggle and Unicode editing remain supported.

`LoadingCurtain` is an automatic visibility owner distinct from the manually
opened `ConsoleController`. A candidate ready scene does not remove the full
background. The actual owner calls `in_game()` after successful render/swap and
the existing presentation/input-handoff readiness checks, then automatic height
retracts 1 to 0 using monotonic elapsed time. The existing manual half-panel
speed is retained: **180 ms** for 0.5 viewport; automatic full retraction is
**360 ms** at the same speed. Repeated ready notifications do not restart it.

`translate_console_panel` shifts stable-size content and clips exactly once;
the backdrop is not vertically squeezed. Gameplay renders beneath at the same
full viewport/FOV. There is no artificial loading delay or animation sleep.
When the user explicitly opened the console during loading, automatic full
visibility retracts but the manual half console and draft remain. Error/cancel
keep a full backdrop with the reason; disconnect returns to full idle; another
connect resets its independent curtain generation. Session receipts retain
their existing meanings and are not promoted by a UI animation.

## Failing-before and visual evidence

Evidence directory: `manual-artifacts/task-records/e33-1-console-output-layout/`.

- `failing-before.log`: the initial seven E33.1 regression cases all failed on
  the saved E33 production baseline (1,285 assertions: 896 passed, 389 failed).
  These cover telemetry starvation/eviction, oldest-row/wrapping behavior,
  output-area usage, premature backdrop disappearance and non-English defaults.
- `layout-before.log`: the dedicated old composition case failed 10 of 841
  assertions; actual row counts, positions and clips are checked, not merely
  whether some pixels changed.
- `auth-before-and-first-fixes.log`: 12/14 cases passed; the delayed native-auth
  fixture still failed as intended. The layout fixture also counted only its
  `layout-row-` markers, excluding the legitimate new `Scene ready` history row
  in half mode. It was corrected to identify all output by its actual clip;
  the exact capacity/position assertions were retained, not weakened.
  This mixed run is **not a final passing run**.
- `focused-release-first.log`: 83 cases / 81,881 assertions passed before the
  final timed-curtain GPU case was added. It is retained as an intermediate run.

Actual offline framebuffer captures were visually inspected (lossless PPM to
PNG conversion; no artwork editing). They are synthetic UI fixtures over a
constant scene backdrop, not a new live game or the unavailable user images:

- [Before, 1280x720 full panel](../manual-artifacts/task-records/e33-1-console-output-layout/screenshots-before/e331-layout-full-1280x720.png)
- [After, 1280x720 full panel](../manual-artifacts/task-records/e33-1-console-output-layout/screenshots-after/e331-layout-full-1280x720.png)
- [After, 1920x1080 half panel](../manual-artifacts/task-records/e33-1-console-output-layout/screenshots-after/e331-layout-half-1920x1080.png)
- [First candidate remains covered](../manual-artifacts/task-records/e33-1-console-output-layout/screenshots-after/e331-retract-candidate-000ms-1280x720.png)
- [Successful presentation: 180 ms into retract](../manual-artifacts/task-records/e33-1-console-output-layout/screenshots-after/e331-retract-ready-180ms-1280x720.png)
- [Retract complete at 360 ms](../manual-artifacts/task-records/e33-1-console-output-layout/screenshots-after/e331-retract-ready-360ms-1280x720.png)

The before case showed only 28 rows at 1280x720 despite room for 56. Afterward
the oldest complete window contains rows 0..55 from the same retained history,
and the prompt stays separate. The 180 ms capture retains an unsquashed upper
curtain with its lower edge at y=360; the lower viewport is exactly the fixture
RGB(20,36,46). At 360 ms no automatic UI remains. Tests additionally verify
constant source-image dimensions and translated source-pixel agreement.
Non-100% DPI is covered by 1.25/1.5 logical-to-framebuffer fixtures, not a claim
that the physical monitor DPI or a visible fullscreen live session was changed.

The final regression scope includes filled full/half history, 1280x720 and
1920x1080 framebuffers, suitable non-unit-DPI fixtures, editor/navigation/scroll
sequences, logical anchor preservation, real OpenGL clips/pixels/state restore,
loading/manual ownership, fake-peer/native work, English defaults and unchanged
Unicode/server data. Window/fullscreen mode policies retain the existing E31
offline coverage; full/half console captures use hidden real GL framebuffers.

## Changed production files

- `include/hlclient/core/log.hpp`, `src/core/log.cpp`: typed console routing and
  reserved error delivery; existing terminal diagnostics preserved.
- `include/hlclient/console/console.hpp`, `src/console/console_model.cpp`:
  logical scroll anchors, visual-row windows and line-ending normalization.
- `include/hlclient/app/console_screen.hpp`, `src/app/console_screen.cpp`:
  framebuffer output geometry and shared one-time panel translation/clipping.
- `include/hlclient/app/console_application.hpp`, `src/app/console_application.cpp`,
  `include/hlclient/app/loading_presentation.hpp`: actual text budgets, English
  defaults, manual/automatic visibility ownership and post-presentation retract.
- `include/hlclient/app/console_steam_authentication.hpp`,
  `src/app/console_steam_authentication.cpp`, `apps/hlclient/main.cpp`:
  nonblocking native authentication adapter on the normal console path.
- `CMakeLists.txt`, `tests/CMakeLists.txt`: new adapter/test compilation;
  E33/E33.1 tests live in the existing console targets plus
  `tests/test_console_preworld_e33_1.cpp`.

No launcher source, SDL input backend, OpenGL scissor implementation, game
module, prediction, weapon, asset file or server setting was changed by E33.1.

## Final validation and identities — 2026-10-16

All final checks below run after the last production edit (the native-auth UI
adapter). The later timed-curtain fixture changed tests only. No E33 historical
pass count is reused. Normal configuration is VS2022/Win32, warnings-as-errors
ON; the existing core-only cache has warnings-as-errors OFF.

| Required verification | Current record |
| --- | --- |
| Normal Release client and focused/full test targets build | PASS, `build-release-final.log`; preceding complete build also passed |
| Normal Debug client and focused target build | PASS, `build-debug-final.log` |
| Focused Release E33/E33.1 | **84 cases / 83,338 assertions pass**, no skips; `focused-release-final.log` |
| Focused Debug E33/E33.1 | **84 cases / 83,264 assertions pass**, no skips; `focused-debug-final.log` |
| Actual OpenGL console/loading | PASS in both focused runs, including approved original read-only background and timed curtain frames |
| Actual Debug idle and Null startup | PASS, `debug-idle-null-final.log`; no assets, sockets, Steam or gameplay attempt |
| Core-only complete CTest suite | **45/45 pass**, 40.97 s; `core-release-first.log` (this is the sole/final core suite after production edits), `build-core-first.log` |
| Alternate module and core Null | **29 cases / 598 assertions pass** plus Null startup; `alternate-core-null-final.log` |
| Architecture boundary | PASS in both core-only and normal final Release suites |
| Explicit A/B launcher `reference`, `RemoteAudioPeer`, `NoTimeLimit`, `CheckOnly`, `SourceOnly` | PASS, `launcher-ab-checkonly-final.log`; external resource/Steam readiness deliberately not checked |
| Complete final Release offline suite | **3,753 passed / 70 skipped / 0 failed** out of 3,823 CTest entries, 407.44 s; `full-release-final.log` and `full-release-final-ctest-details.log` |
| Post-suite normal startup, Null, transport, reporting and launcher/no-stock controls | PASS; `build_source.ps1 -Configuration Release -TestOnly -Parallel 2` completed with exit 0, including final `CheckOnly/SourceOnly`; no live readiness claim |
| Final diff check | PASS, `diff-check-final.log`; only pre-existing LF/CRLF conversion notice, index empty, HEAD/branch unchanged |
| ASan build | PASS, `build-asan-final.log`; existing test-driver-only `/Od /Ob0` retained |
| ASan runtime | Unavailable before test execution; `asan-runtime-final.log` |

The focused assertion totals differ because bounded native-worker polling and
teardown callbacks execute a scheduling-dependent number of assertions. Both
configurations run the same 84 cases without skipped controls. Asset-backed E33
controls were explicitly executed in both focused runs, not inferred from skips.

The final full-suite skip inventory is the exact 70-entry list in
[full-release-final.log](../manual-artifacts/task-records/e33-1-console-output-layout/full-release-final.log);
individual reasons are retained in
[full-release-final-ctest-details.log](../manual-artifacts/task-records/e33-1-console-output-layout/full-release-final-ctest-details.log).
They cover optional approved local assets/Steam-runtime selection not enabled
for the broad run, explicit playback and dynamic-WFP opt-ins, unavailable
file/directory symlink or custom-reparse fixtures/second writable volume, and
hidden-SDL focus/relative-mouse limitations. No skipped case is called a pass.
The two optional original-console-resource cases skipped in this broad run
were executed and passed in both focused E33 runs with the approved root.

There is also an internal optional skip inside the aggregate core API test:
**444 cases = 443 passed + 1 skipped, 409,037 assertions**. It requires
`HLCLIENT_SKY_ASSET_ROOT` for the read-only original sky audit. This appears in
both the standalone core-only and normal full-suite aggregate logs, even though
the enclosing CTest entry passes; thus the core-only **45/45 CTest** result does
not claim that this optional original-asset audit ran. See
[core-final-ctest-details.log](../manual-artifacts/task-records/e33-1-console-output-layout/core-final-ctest-details.log).

Normal client target:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
Final client SHA-256:
`F5D72FAF716181366ECA2D939766CBBA758524C3271C24E475EEE4164C701719`.

Launcher target:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\HLClientLauncher.exe`.
Final launcher SHA-256:
`4954BD85FF7B661D0AE9CFDC807CD01141AA90D0FCEE2E1D08F73C2ECEDD5688`.
It is unchanged and was not rebuilt; its normal sibling-client contract is
unchanged. The managed launcher's `CheckOnly/SourceOnly` also passed.

ASan runtime probes: default child PATH `--help` exited `0xC0000135`; with the
matching MSVC 14.34.31933 x86 runtime in child-only PATH, both `--help` and `[e33]`
exited `0xC0000142`. All three produced zero stdout/stderr characters and ended
without timeout/forced termination. Instrumented compilation is not a runtime
pass; no DLL copying, machine PATH changes or sanitizer infrastructure repair
was performed.

The recorded pre-edit client SHA was
`208F897CC3E841DBBFB3D9D92BEEFC779AFB7C0D35B38F117A0B5BC02D4186AE`;
the pre-edit launcher SHA was
`4954BD85FF7B661D0AE9CFDC807CD01141AA90D0FCEE2E1D08F73C2ECEDD5688`.
These are local baseline identities, not identities attributed to the user's
screenshots or a final E33.1 build.

## Focused manual acceptance

Use the current launcher's **«Відкрити клієнт»** action and the normal client;
no new full-arsenal gameplay campaign is needed for this UI correction.

- [ ] Own in-game messages are English; Ukrainian input remains unchanged.
- [ ] Console input/output remain available while connecting/loading, before a map appears.
- [ ] `cmdlist`/`cvarlist` use the full panel's available output height.
- [ ] `echo` remains readable without repeated service telemetry.
- [ ] Scroll/toggle/resize do not collapse a filled viewport to one row.
- [ ] Ordinary connect smoothly retracts the full background automatically.
- [ ] A console explicitly opened during loading retains its draft and half-height panel.
- [ ] In-game output fills the available half panel; disconnect/reconnect/quit and input isolation still work.

New-build manual acceptance remains **not run** until the user actually tests it.
