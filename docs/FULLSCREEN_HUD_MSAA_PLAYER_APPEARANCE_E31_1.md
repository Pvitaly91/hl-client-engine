# HLC-E31-1 — fullscreen HUD, scene MSAA and player appearance

Result: `fullscreen_hud_msaa_player_appearance_integrated` — latest client
`62D34E…` has `manual=fullscreen_playability_user_confirmed`, scoped to the
successful 1920×1080/MSAA4 connection and normal user close recorded below.
Earlier build hashes, failures and validation scopes remain historical.

| Status | Result |
|---|---|
| Fixed, offline verified | Compact native-pixel HUD/kill feed with saved independent scale; real scene MSAA with resolve before sharp HUD; approved per-player visual model/color selection through existing Studio presentation |
| Defined fallback | Lower supported AA count or Off with actual reason; visible base player model for unavailable/rejected/incompatible custom resources |
| Not verified | Every live bot's visual appearance, OS mode transitions/Alt+Tab, stock-exact palette/layout parity and the precise cause of the superseded driver's failures |

## Scope and evidence

Authoritative checkout: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, repository
`Pvitaly91/hl-client-engine`, branch `codex/stock-runtime-campaign-5e48b7c1`,
baseline HEAD `00505f71ff2547a9a6b083e53edf69ec45d48213`.
Existing staged/unstaged/untracked work is preserved. Before edits, the local
rollback set retained and hash-verified 1364 source files, 36 Release files and
3 configuration files. Evidence lives under
`manual-artifacts/task-records/e31-1-display-appearance-20261011/`.
No reset, clean, stash, branch switch, staging, commit or push is part of this task.

New manual issue statements are `attribution=user_report`:

- `fullscreen_1080p_hud_scale=user_report_failed`;
- `anti_aliasing_quality=user_report_unsatisfactory`;
- `remote_player_appearance=user_report_all_default`.

The named 1920×1080 user screenshot is an issue reference, not a measurement of
the previous internal framebuffer, GPU state, executable identity or actual
fullscreen mode. Its pixels were not present in the text attachment available
to this work. The earlier LAN receipts available before the fullscreen follow-up
did not expose individual bot model values; later E31.1 receipts below now retain
bounded appearance observations. No claim that every bot requests a unique model
is made.

The earlier successful LAN connection/playability and E30 user acceptance
(blood decals, spray/drop scale, Snark pop, HUD/kill notices and normal close)
remain historical, scoped user reports, not acceptance of this new build.
The earlier narrowly scoped E31 suffix-parser follow-up retains its own record;
no completed milestone has been renamed.

**Endpoint clarification:** the user explicitly selected `192.168.0.104:27016`,
overriding the stale `.102` address in this prompt. Saved launcher settings and
defaults retain `.104`. Endpoint choice is not invented run telemetry.

## Findings and production changes

### HUD and kill feed

The audit did **not** find a width/640 × Windows DPI double multiplication.
The selected 1280/2560 tiers are valid original assets and the pinned SDK's
width thresholds support their selection. The previous implementation emitted
their native crop sizes with no independent source-to-layout/user scale.
Read-only approved original assets established:

| Role | 640 tier | 1280 tier | 2560 tier |
|---|---:|---:|---:|
| `number_0` crop | 20×24 | 40×48 | 60×72 |
| Egon death icon | 32×16 | 64×32 | 96×48 |

At 1920×1080, the old executable really rendered 48-pixel-high digits in a
controlled production-OpenGL fixture. This is not inferred from the screenshot.
The default compact layout is an explicitly **local-compatible** 720-height
reference, not a claim of stock-binary pixel parity. With a ready selected tier
above 640, height above 720 and both reference/selected digit crops available:

`automatic = clamp((640_digit_height / selected_digit_height) × clamp(height/720, 1, 2), 0.25, 2)`.

Otherwise automatic scale is 1. Explicit user scale is 0.5–2.0 relative to this
layout; Auto uses 1. Effective source-to-pixel scale is their product, applied
once. Layout uses the corresponding virtual canvas; rendering receives final
framebuffer-pixel commands. Tier, crop, UV and original resources are unchanged.
At 1080p the selected tier remains 1280, scale is 0.75 and digits are 30×36.
At 1440p the 1280 tier yields 40×48. Native launcher DPI does not enter this math.

The separate kill-feed defect was proven in code: bitmap font size was derived
from sprite tier (2/4/6-pixel units). At 1080p this made a 28-pixel-high font and
24-pixel character advance; long names could occupy almost the full viewport.
Font units are now independently 2 pixels through 1440p and 3 above, multiplied
only by explicit user scale. Icons independently use the source-backed scale.
The row budget is `min(viewport_width − 8×user, 640×user)`, with balanced bounded
ASCII-name elision using the supported `-` marker, right anchor and aligned
icon/text centers. Network names are not changed. Six-second lifetime,
DeathMsg time domain/dedup and A→B→A focus fixes are retained.

Launcher settings schema v2 adds Auto/explicit HUD scale and reset independently
of image reset. Strict v1 settings remain readable with defaults. Both normal
application HUD-context handoffs carry the same settings; inventory click
consumption, camera FOV, world/effect dimensions and gameplay are unchanged.

### Scene antialiasing

Before: scene passes wrote directly to the single-sample destination; optional
image adjustments copied that color to a single-sample texture. There was no
scene MSAA target. Merely changing SDL default-framebuffer attributes would not
establish this scene contract.

After, inside the existing OpenGL 3.3 renderer:

`world / Studio / decals / effects / viewmodel → RGBA8 + DEPTH24_STENCIL8 MSAA target → native-size single-sample resolve → destination → image adjustments once → native sharp HUD → present`.

The resolved color copy preserves the existing color-adjustment pass (or its
exact neutral bypass). There is no render-scale, hidden upscale or new AA
framework. SDL's native framebuffer remains single-sample, avoiding double MSAA.
Launcher selections are Off/2/4/8, requested default 4. Trial allocations query
actual color and depth sample counts and both FBO completeness states; a driver
silently rounding a count upward is rejected. Candidates descend 8/4/2 at or
below the request and device limit. Color/depth/resolve storage is bounded to
512 MiB; failures fall back to a lower supported count or the existing Off path.
Requested, actual and typed fallback reason remain distinct.

Owned targets are cached by native extent/request, released on change and
destroyed with the renderer before its context. Caller read/draw FBOs, MRT draw
buffers, renderbuffer and multisample state are restored even on exceptions.
Inherited stencil/scissor cannot gate fresh scene attachments. Resolve does
not change dimensions. Existing texture mipmaps/filtering are unchanged;
MSAA coverage is not claimed to fix texture-detail or alpha-mask aliasing.

### Per-player appearance

The strict committed userinfo decoder already retained `model`, `topcolor` and
`bottomcolor`, but only the display name crossed the presentation boundary.
The normal materializer selected only immutable network-manifest model slots,
therefore `models/player.mdl` remained the final visual model for everyone.

The typed display projection now carries only bounded appearance fields and
occupant/revision identity, not raw userinfo/auth payload. HL policy selects
`models/player/<safe-token>/<safe-token>.mdl` for the ordinary base player
resource. Explicit non-base server-selected models retain precedence in this
bounded profile. The canonical network modelindex is never rewritten.

The existing approved exact-world-root and Studio importer pipeline provides
asynchronous local appearance dependencies without fictitious server precache
slots. Token/path, source/bundle/time/memory/count limits and companion checks
apply. No disk-wide search, other installation fallback, download or game asset
modification occurs. Supplemental immutable presentation Studio owners feed
the existing pose/render package and E10/E11/E13 pipeline. A pending update
retains that occupant's last published variant (or the base if none exists);
missing/invalid/incompatible resources use the valid base instead of hiding
the player. Ready selection changes model-dependent pose/cache identity, not
netchan, gameplay prediction or server collision/damage bounds.

Diagnostics distinguish `model_not_advertised`, `default_model_requested`,
`model_token_rejected`, `approved_asset_missing`, `asset_import_failed`,
`pose_incompatible`, `asset_pending`, `selected_model_ready`, explicit server
selection, authorization and resource bounds. No random model substitution is
used to claim that all actual bots are different.

Remap acts only on supported indexed `DM_Base` regions (top 160–191, bottom
192–223), retaining alpha and all other texels. Hue selection preserves original
chroma/value as a documented local-compatible conversion; it is not claimed as
verified proprietary renderer parity. Per-player immutable variants prevent
one occupant recoloring another. Variant caches are bounded, not per-frame
imports/uploads. Corpses freeze the originating appearance and slot reuse does
not inherit it; model-dependent pose and p-model bone matching use the selected
asset rather than old skeleton indices.
Server toss-position changes no longer create a new corpse appearance lifetime;
body sequence/animation/source identity and removal/reuse still delimit it.

## Durable observed diagnostics

Installed SDL is 3.4.14; no SDL2 size API was substituted. `external-video.v2`
separates requested mode/extent, actual SDL mode, coordinates, logical window
and pixel framebuffer extent, display content scale, window pixel density and
native framebuffer sample observation. After a real render it adds actual GL
viewport, scene/resolve/post-process extents, actual MSAA/completeness/reason,
HUD canvas/tier/automatic/user/effective scale and kill-feed font/icon metrics.
Startup explicitly says scene/HUD not observed rather than presenting a
request as actual state. At most 16 changed-state observations plus final report
are written; no per-frame file writes. Final state is independent of that cap.

The launcher reads an identity-bound bounded `external-msaa.json`, showing
actual samples/fallback. Updates replace only the client's exact retained file
handle; the reader keeps its last valid observation during the replacement gap.
Optional observation publication failures do not disrupt gameplay/disconnect.
If a concurrently held reader leaves the old leaf temporarily delete-pending,
publication retries on later frames at least 50 ms apart, at most eight
consecutive attempts and 256 total attempts per session. Only exact owned
handles may be removed; a foreign leaf is never overwritten. Retry counters
are reported and terminal failure is logged once; GUI labels the last confirmed
observation rather than inventing current samples.
`external-player-appearance.json` retains at most 128 changed per-slot typed
observations with model/status/revision/colors and approved virtual names only.
No full userinfo, native asset locator or auth material is logged.

### Production integration points

- `apps/hlclient_launcher/main.cpp`, `src/core/external_launch_config.cpp`,
  `src/core/command_line.cpp`: controls, versioned settings and normal launch.
- `src/platform/sdl_window.cpp`, `apps/hlclient/main.cpp`: actual SDL display
  state, both HUD handoffs, scene settings and durable observed receipts.
- `src/games/halflife/sprite_hud.cpp`: `PixelLayout`, automatic source-backed
  scale and independent bounded death-notice metrics.
- `src/renderer/opengl/opengl_renderer.cpp` and new
  `src/renderer/opengl/opengl_scene_target.hpp`: actual scene storage/resolve;
  neutral `RenderScene` settings and OpenGL diagnostics, no HL-specific policy.
- `src/platform/windows/external_launcher.cpp`:
  `ExternalMsaaObservationPublisher` and identity-bound GUI observation reader.
- `src/goldsrc/user_info_update.cpp`, `src/goldsrc/runtime_replay_session.cpp`,
  `src/games/halflife/multiplayer.cpp`, new
  `include/hlclient/game_api/player_appearance.hpp` and
  `src/games/halflife/player_appearance.cpp`: strict committed fields and HL policy.
- New `src/goldsrc/player_model_assets.cpp` and
  `src/app/runtime_replay_local_assets.cpp`: approved async import, bounded
  immutable variants and final selected Studio/pose/p-model/corpse integration.
  Existing entity-render packages carry supplemental immutable owners.

## Validation and ready outputs

Genuine old-build HUD baseline: one approved-original-asset production GL test, 1,113,558
assertions, exit 0, 40 PPM images; four selected images converted losslessly to
PNG and pixel/hash verified. These are offline synthetic scenes with approved
read-only original HUD resources, not live LAN captures.

Core-only VS2022/Win32 Release (HL1 disabled) was rebuilt successfully after
the last publication helper change. All 43 registered
core tests passed; the core/alternate-module API exercised 317 cases and
143,414 assertions. Architecture guard cases, launcher offline UI and no-stock
fixtures passed. The normal core executable with `--renderer null` returned 0
and rendered one frame without a game asset root. Logs:
`core-build-release-03.log`, `core-ctest-release-02.log`,
`core-ctest-details-release-02.log`, `core-null-release-02.log`.

Final normal VS2022/Win32 builds: Release05 and Debug03, both `ALL_BUILD`,
exit 0 with no compiler warning/error matches. Focused Release02 after those
production changes: **391 cases, 386 passed / 5 optional skips / 0 failed;
3,000,391 assertions passed**. It includes E10–E13, E27 blood/pop, E28 shutdown/
receipts, E29 HUD/focus/input, E30 blood and E31 launcher integration. The E27.1
regressions carry the existing `[e27]` tag, not a new `[e271]` tag.
Focused Debug01 repeated the identical selection after Debug03: **386 passed,
5 optional skips, 0 failed; 3,000,391 assertions passed**.

After the test-only E14 bounds correction, Release06 and Debug04 rebuilt the
`hlclient_tests` target and dependencies successfully. The expanded focused
Release03 includes `[e14]`: **417 cases, 412 passed / 5 optional skips / 0 failed;
3,015,702 assertions passed**. Production executable identities did not change.
Expanded focused Debug02 repeated that final selection: **417 cases, 412 passed /
5 optional skips / 0 failed; 3,015,702 assertions passed**. All 64 final selected
GPU readbacks were converted losslessly and verified after these runs.

Final complete Release CTest attempt02, after all production and test fixes:
**3504 registered, 3439 passed / 65 optional or capability skips / 0 failed**,
566.44 seconds. Architecture/core API, application startup/runtime replay,
launcher and no-stock shutdown/reporting tests passed. No scene-MSAA GPU
capability test was skipped. The full skip list is retained in
`final-release-suite-02.log`; it includes explicit installed-asset/audio/Steam
opt-ins, unavailable private corpus, WFP capabilities, reparse/link capabilities
and interactive SDL capture controls, rather than treating them as passed.
Approved original-resource controls have the separate focused runs above.
The encompassing `scripts/build_source.ps1 -Configuration Release -Parallel 1
-TestOnly` also completed with exit 0: version/help/null application, owned
transport self-test, functional-log observation, G/H summary tests, no-stock
manual/external-map wrapper regressions and `-CheckOnly -SourceOnly` all passed.
The log ends with `Source build/offline verification passed: Release. External
game/live validation NOT RUN.` No configure/submodule update or new build was
performed by this final `-TestOnly` invocation.

The five optional skips were: a separate optional player companion bundle;
two E30 actual-WAD controls without their explicit asset-root opt-in; an
audio-device opt-in; and an explicit Steam-runtime preflight opt-in. There was
no GPU/MSAA capability skip. Required project-owned variants remain exercised.
The separate approved original HUD/Gordon/Barney controls passed without
network or server changes. `managed-checkonly-final.log` confirms the unchanged
managed A/B launcher with `-Prediction reference -RemoteAudioPeer -NoTimeLimit
-CheckOnly`: exit 0, native contract dry-runs passed, `process-launches=0`.

The first complete focused Release run is deliberately retained as **failed**:
253 cases, 243 passed / 6 failed / 4 skipped; 2,283,772 assertions with 1,372
failures. It revealed fractional HUD anchoring and nearest-texel oracle issues,
and appearance fixtures missing required committed-state continuity/frame-rate
fields. All seven scene-MSAA GPU tests passed in that run. On this GPU, request
4 produced actual 4, device maximum 16, complete 320×240 attachments, and no
fallback. Fractional diagonal-edge pixels increased from 0 to 578 (world/rails)
and 0 to 132 (Studio silhouettes), with unchanged camera and geometry. These
are controlled offline GPU readbacks, not live-session screenshots.

### Controlled visual evidence

All paths below are relative to the task record above. PPM→PNG conversion is
lossless with decoded-pixel equality and SHA-256 records in
`hud-rendered-evidence.json`; source game resources are not edited.

| Comparison | Before | After | Scope |
|---|---|---|---|
| Native 1920×1080 HUD | `visuals/before/original-glock-normal-1920x1080.png` | `visuals/after/original-glock-normal-1920x1080.png` | Old/new executable, approved original HUD, fixed blank scene |
| Long-name kill feed | `visuals/after/killfeed-before-layout-reference.png` | `visuals/after/killfeed-after.png` | Explicit reconstruction of old layout, not an old-binary capture; project-owned icon fixture |
| World/rail edges | `visuals/aa/msaa-world-off.png` | `visuals/aa/msaa-world-4.png` | Same production GL scene at 320×240, actual 0/4 samples |
| Studio edges | `visuals/aa/msaa-studio-off.png` | `visuals/aa/msaa-studio-4.png` | Same posed Studio silhouette at 320×240 |
| Two player appearances | `visuals/appearance/appearance-before.png` | `visuals/appearance/appearance-after.png` | Same 640×480 camera/time, project-owned approved models, base vs userinfo selection |

The simultaneous appearance GPU control isolates warm/green regions (2158 and
3550 pixels in the Debug verification), stable repeated uploads and held-model
bone merges. These deliberately simple project-owned meshes are not screenshots
of actual LAN bots. Approved original Gordon/Barney import/remap is an additional
read-only control. At the initial integration review, original live bot model
values were unavailable; the later user-run receipts in the fullscreen follow-up
provide bounded actual model/status observations.

Reference SDK revision was verified read-only as
`b1b5cf5892918535619b2937bb927e46cb097ba1`. Its
`cl_dll/StudioModelRenderer.cpp` calls `SetupPlayerModel(player_index)`, then
uses player top/bottom colors clamped to 0–360 with `StudioSetRemapColors`.
The callback's proprietary palette implementation is not provided by that
source. No submodule update or external engine implementation was used.

### Retained failures and retries

The first renderer-only CMake attempt found an appearance test registered before
its file was complete. Renderer-only attempt02 then passed. Normal Release01
stopped on warning-as-error checks (shadowed UI variable, unsafe environment
lookup and signed comparison); Release02 exposed a 64-bit budget-to-size_t
narrowing; Release03 exposed Catch's unsupported unparenthesized `||` assertion.
Those were corrected without weakening warning settings or logical assertions.
Release04 built successfully and produced the failed focused01 recorded above.
Debug01 linked a newer publication test against the library built earlier in
that in-progress edit cycle; rebuilding dependencies in Debug02 resolved it.
Debug's first original-HUD diagnostic retained the exact nearest-texel-boundary
mismatch; the isolated appearance subset also caught the fixture incorrectly
sending empty disconnect through the strict signon parser instead of the existing
runtime-departure path. Production wire grammar was not relaxed.

Full Release attempt01 ran all 3504 registered tests in 577.45 seconds and
reported 3438 passed, 65 optional/capability skips and exactly one failure:
E14's production HUD scoreboard/notice pixel A/B. Its old
fixed y85–103 region no longer covered the intentionally compact kill feed.
The test now requires the exact fallback text, anchor (372,34), font unit 2,
advance 12, line height 18 and 262×14 ink bounds at 640×480. Both notice and
delimiter-substitution A/B require nonzero changes inside those bounds and
zero changes elsewhere. Expiry equality and scoreboard checks remain intact;
no production behavior or old logical assertion was weakened for this retry.
The failed log and full per-test details are retained separately.

Crosshair anchoring is now nearest physical pixel with legacy half-pixel ties,
and the ≤0.5-pixel center requirement remains unchanged. The HUD pixel oracle
still checks complete source-derived RGB tuples within 3 levels; only exact
interior GL_NEAREST boundaries (within 0.0001 source texel) may choose either
adjacent texel. Non-boundary, transparent/background and crop-exterior checks
remain strict. Independent tests cover that narrow oracle rule. Logs retain
every initial failure; only the final successful runs count as acceptance.

### Initial E31.1 Release outputs and preservation

All are in `D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\`:

| File | Bytes | SHA-256 |
|---|---:|---|
| `hlclient.exe` | 4,315,648 | `8FD94885A454EF21404C9FA13112340BA3F3DBAA3A89D838C7C6DA8642ED2A4F` |
| `HLClientLauncher.exe` | 256,512 | `49D63DD1A2365D42DCA498A3E51616B21FC4EB2769EFC135D0AF8CC00D69D319` |
| `SDL3.dll` | 2,285,568 | `D88767FB3F3D1AA4BB4C67BD0465C3EEC4258D8A83A8B0ED8CE9A114265C41A4` |

`final-audit.json` verified all **1403** preserved source/Release/configuration
copies by SHA-256, zero original source files missing, unchanged managed-launcher
bytes, unchanged saved launcher configuration with `.104:27016`, unchanged
HEAD/branch and zero staged paths. `git diff --check` returned 0; existing Git
LF/CRLF conversion warnings are recorded separately from compiler diagnostics.
The new binary identities are not attributed to the earlier user-run session.

ASan is not repaired by this work: earlier runtime `0xC0000135` remains
unavailable/unconfirmed, distinct from any successful compilation.

### Known limits / not verified

- Existing GUI fullscreen is desktop **borderless**; no new exclusive display-
  mode switching was added. Actual desktop framebuffer may differ from the
  requested 1920×1080; diagnostics distinguish them, and scene/HUD use actual
  pixels. OS Alt+Tab/live external presentation still needs user confirmation.
  DPI 100/125/150/200% and window-mode comparisons use conversion fixtures,
  not an automated claim of physical OS display-mode switching.
- Compact HUD reference scaling and indexed palette hue remapping are documented
  local-compatible policies, not stock-binary pixel/color parity claims.
- GPU evidence proves actual 4× on this device. Requests Off/2/4/8 and bounded
  fallbacks were tested, but the report does not claim every device/count is
  supported. MSAA does not resolve aliasing within texture detail/alpha masks.
- Missing custom models remain base-model fallbacks; no FastDL, automatic
  downloads, random substitution or server userinfo/bot changes were introduced.
  Visual correctness of every actual live bot model is not yet user-confirmed.

## Fullscreen early-close follow-up, 2026-10-11

New issue: `fullscreen_connection_closes_seconds_after_entry=user_report_failed`.
The initial E31.1 candidate with SHA-256
`8FD94885A454EF21404C9FA13112340BA3F3DBAA3A89D838C7C6DA8642ED2A4F`
was actually used in three user-run LAN sessions. Their identity-bound reports
establish:

| Run ID | Actual rendering | Commands / packets | Terminal result |
|---|---|---:|---|
| `4de54ce3a350d5a282c8db00b1f32c8b` | Borderless 1920×1080, VSync, complete MSAA8 | 4 / 3 | Exit2, `usercmd_transmission_failed`, 71 ms after first gameplay |
| `78c59cc5ed171ad7b88701184830b6d6` | Windowed 1280×720, VSync, complete MSAA4 | 662 / 635 | Exit0, window close with server disconnect confirmed |
| `185b652ac6aa9e25c3b36372b2e0a06e` | Borderless 1920×1080, VSync, complete MSAA4 | 3 / 3 | Exit2, `usercmd_transmission_failed`, 46 ms after first gameplay |

The failed clients exited through the application failure path rather than a
native access violation. Their scene targets were complete and cached (one
allocation, no reallocation). Both diagnostics omitted nested usercmd codes,
so `pending_submission` is not retroactively attributed to those exact runs.
The normal-frame owner ordering exposes that specific defect in source and is
covered by the controlled regression described below.

`LiveRuntimeStage` samples commands, performs the one shared driver pump, then
accounts transmission. Its after-driver phase may prepare and bind a remaining
move when an earlier outgoing-context/capacity wait has cleared. That plan owns
the history revision until the next pump sends it. The next frame previously
advanced the 20 ms scheduler and attempted insertion before completing the
owned plan. `queue_reference_command` correctly rejected this with
`pending_submission`; the enclosing live owner incorrectly made that temporary
wait terminal. Longer frames make a new sample due while the plan is still held.
The initial world/MSAA upload occurs before input activation and is separate.

The correction gates sampling before scheduler advancement while an outgoing
reference submission still owns history, progresses its existing transmission,
and leaves the shared pump and receipt accounting in their established order.
It keeps command identity, pending input and exact-once submission together.
Existing history locks, send failures, 15-second stalled-transmission deadline,
neutral recovery after unsampled wall time and lifecycle cancellation remain.
No AA quality or render settings are reduced to hide the timing defect.

Safe scheduler/adapter/transmission/history enums now survive in
`external-diagnostics.json`. Runtime queue failures preserve the history enum;
arbitrary error context, packet data and authentication material are not added.
The current user-run appearance receipts also establish real safe model tokens
and ready asset selections (including Gordon, Barney and other local models);
they do not constitute a pixel-by-pixel visual acceptance of those live models.

Follow-up evidence and source/Release/user-run preservation:
`manual-artifacts/task-records/e31-1-fullscreen-usercmd-20261011/`.
The snapshot retained and hash-verified 1374 source files, 36 Release files and
62 original user-run files before edits.
Release red-build attempt01 stopped on missing `to_string` overloads for three
existing usercmd error enums; explicit bounded labels were then implemented in
their owning headers. Red-build attempt02 passed. With the old live-owner
ordering still present, the new regression failed with the exact context
`Pending plan owns history revision` in the blocked-send and long-stall cases.
The initial constant-small-packet-capacity section also exposed incorrect new
fixture assumptions about unsent, unprepared backlog and repeat-update delivery;
those are not counted as proof of the fatal ownership defect. Its initial
11 assertion failures and complete compact log are retained in
`test-release-red-01.log`.

Frozen green validation: normal VS2022/Win32 Release03 and Debug01 builds passed.
The two `[fullscreen-usercmd]` production tests passed in both configurations:
683 assertions, including true post-driver preparation, retry, contiguous
commands, exact-once notifications, fresh/stale press, focus loss, cancellation,
graceful close and retained stalled-progress timeout. The invalid new
constant-capacity section was replaced with an actual production shared-driver
context wait caused by preceding `clc_nop`; existing history-lock assertions
remain strict. The full live-owner blocked-send tests provide red/green proof;
the lower-stage test separately establishes post-driver preparation ownership.

Focused final Debug: 277 cases, 275 passed / 2 optional skips / 0 failed;
1,575,124 assertions. Focused final Release: the same case outcomes with
1,575,121 assertions. Both optional skips are the explicit installed Steam
runtime and approved original asset-root controls; mandatory fixtures and GPU
tests passed. No new game session was needed for this verification.

Follow-up ready client:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
4,320,256 bytes, SHA-256
`E70136D0F5205647D8CF34D01C8719D125B2458BDF6EC45C7AB8F165C7B1CBF5`.
It supersedes the initial client in the historical output table above.
The already-open normal launcher and SDL3 runtime are unchanged, with their
original hashes shown there. No new feature flag or configuration change is
required. All 1472 preservation copies verified, zero source files missing,
13 existing source files changed in this follow-up, HEAD/branch and staged set
unchanged. `audit.ps1` verifies launcher/runtime byte identity explicitly.
Core-only Release01 rebuilt with HL1 disabled; all 43 CTest entries passed in
24.46 seconds, including architecture guard and the alternate-module/core API
(318 cases, 143,422 assertions). Its standalone null-renderer smoke also returned
0 after one frame with no game asset root.

Final complete Release CTest after the last production edit: 3507 registered,
3442 passed / 65 optional or capability skips / 0 failed, 652.28 seconds.
The 65 skips retain the existing explicit installed-asset/audio/Steam, private
corpus, WFP, link/reparse and interactive SDL prerequisites. Both new ownership
tests and the typed durable-diagnostics regression passed in this full run.
Full details are preserved in `full-ctest-details-01.log`; the encompassing
offline wrapper status is recorded in `test-release-full-01.log`.
The complete `-TestOnly` offline wrapper returned 0 after version/help/null,
owned transport, summary, no-stock manual-launch and source-only CheckOnly
controls. Its final line confirms external game/live validation was not run.
New candidate manual acceptance remains `not_run`; the three actual user runs
above belong to the superseded client hash, not this repaired executable.

Production changes in this follow-up are confined to transmission ownership and
typed failure reporting: `src/goldsrc/live_runtime_stage.cpp`,
`src/goldsrc/usercmd_transmission_stage.cpp`, their public headers, the owning
usercmd history/adapter error-label headers, `apps/hlclient/main.cpp` and
`include/hlclient/app/external_diagnostics.hpp`. Regressions use the existing
resource-response/live-owner and reference-transmission fixture files plus the
existing external diagnostics tests. Renderer, server resources/configuration,
wire encoding and canonical gameplay authority were not changed.

## Manual checklist for this new build

The later user runs `034129cbed44c4a165babd2287ba1f6b` and
`20a646ebc2adb58fddd290696d75aa35` used the E701 client above and both failed
through a distinct underlying driver path. The windowed run reached 445 new
commands / 443 packets / 214 server samples before `driver_send_failed`;
the borderless 1920×1080 run failed before signon/world entry, 44 ms after
connection establishment, with `driver_failed`. MSAA4 was complete in both.
The pending-history correction remains a separately reproduced and fixed
defect; it did not establish the cause of these subsequent failures.

Their precise driver causes were lost because live driver event handling and
usercmd wrappers did not forward `NetchanDriver::last_error()`. The current
diagnostic follow-up preserves that first error before cleanup, keeps accepted
send receipts, and serializes only typed packet/session/reassembly labels,
native socket domain/code and structural packet metadata. It does not change
network rejection, framing or authentication rules. A control user-run on the
new diagnostic candidate was performed, as recorded below. The exact cause of
the earlier driver failures remains unobserved. Evidence/snapshot:
`manual-artifacts/task-records/e31-1-driver-failure-20261011/`.

Current normal Release client: SHA-256
`62D34EB67A0A25ACB9BA5CA9AA8BCF419A3DAC9D9ED342DF79E01F42D673B94B`,
4,332,544 bytes at the same `build\bin\Release\hlclient.exe` path. This supersedes
the E701 candidate. The normal launcher remains byte-identical.

`manual=fullscreen_playability_user_confirmed` for this exact current hash:
the user explicitly reported **"Гра працює"** for run
`d332edca1a1b652679bb99fd8bacf756`. Its identity-bound receipts and actual video
observation confirm borderless 1920×1080, VSync, complete MSAA4, HUD tier1280,
effective scale0.75 and entry to Crossfire. It ran 84.959 seconds, generated3964
commands, transmitted3963 packets and observed1844 server samples before a
manual window close. Exit0, `normal_close_completed`, no primary/cleanup error;
the disconnect was transport-ACK confirmed, not server-disconnect confirmed.
This establishes bounded successful connection/playability, not an unbounded
stress claim or identification of the old transient driver's precise cause.
Agent-launched live sessions0; one user control session.

Release build01 passed. The first new metadata-focused run retained a failed
new fixture expectation: it attributed the valid received trigger packet to an
ACK **send** failure. The production rule intentionally excludes RX-rejection
metadata for a TX failure; the fixture now verifies absence instead. No existing
strict network assertion or production rule was weakened. Test-only rebuild02
used `BuildProjectReferences=false` while the user played, and verified unchanged
client hash. Final relevant Release run: 351 cases / 351 passed / 0 skipped /
0 failed, 436,362 assertions. It covers netchan, live owner, usercmd, UDP adapter,
previous pending-ownership fix and typed durable diagnostics. Full suite counts
above belong to the earlier E701 candidate; they are not reattributed to this
metadata follow-up. SHA and scoped manual evidence are saved with this record.

Open `build\bin\Release\HLClientLauncher.exe`, keep
`192.168.0.104:27016`, select 1920×1080 fullscreen/borderless as appropriate to
the actual display, HUD Auto and MSAA4. Connect only when ready to test.

- [ ] HUD compact; kill-feed names/icons fit and notices continue after focus changes.
- [ ] Crosshair exactly centered; inventory selection does not fire.
- [ ] Explicit HUD scale saves; reset returns Auto without changing image controls.
- [ ] Launcher reports actual MSAA4 (or an explicit supported fallback).
- [ ] Wall/rail/model edges smoother; HUD remains sharp; gamma/brightness/contrast work.
- [ ] Players with different available server-requested models really differ.
- [ ] Missing custom models retain visible fallback and a typed report reason.
- [ ] Idle/run/crouch/aim/fire, held weapons, death/corpse/respawn remain correct.
- [ ] Alt+Tab, fullscreen/resize and normal close work; LAN server keeps running.

Latest candidate `manual=fullscreen_playability_user_confirmed`. Its single
control session was run by the user. Agent-launched live/Steam/HLDS/capture
sessions: **0**.
No server/bot/Steam/stock asset/map/userinfo modifications. No staging/commit/push.
