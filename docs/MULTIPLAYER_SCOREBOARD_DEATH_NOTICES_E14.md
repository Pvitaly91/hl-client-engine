# E14 — multiplayer scoreboard and death notices

Subsequent E15 preparation evidence: user “перевірив все ок”,
`manual=passed`, `attribution=user_report`. This does not rewrite the original
build report's `manual=not_run` below. Teamplay/reconnect manual remain
not_run; the report does not establish MP5 acceptance.

## Scope, audit and prior acceptance

Authoritative checkout: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, starting HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. Local E11–E13 changes were preserved;
the historical remote checkpoint is not a checkout/reset instruction.

E13 `manual=passed`, `attribution=user_report`: the user reports correct spawn
and respawn direction, authoritative Glock kills, smooth death animation,
surviving hit/death/respawn, correct corpse/respawn lifecycle and continuing
movement/gameplay after respawn. No executable hash was supplied for this
confirmation. Historical failures and historical `manual=not_run` entries are
not rewritten. E14's new executable remains `manual=not_run`.

Pre-E14 audit:

| Boundary | Existing implementation | E14 change |
| --- | --- | --- |
| DeathMsg | Exact bounded decoder in HL `server_messages.cpp`, diagnostic life event | Reuse event for a display notice; no second wire decoder |
| ScoreInfo / GameMode / TeamInfo / TeamScore | Registered framing, bodies otherwise ignored | Exact HL semantic parsers |
| Names | Private owning userinfo parser; only metadata exposed | Only bounded display-name projection and private same-occupant comparison |
| Identity | Zero-based userinfo client index and existing serverinfo local entity mapping | Central checked client-index → player-slot helper |
| TAB | Existing scoreboard action, excluded from wire buttons | Held UI state plus viewport through a narrow presentation context |
| HUD | Generic text/rectangles, old 8/8 limits | Shared explicit command/glyph/byte budgets, validated before GL submission |

Pinned official SDK revision was verified as
`b1b5cf5892918535619b2937bb927e46cb097ba1`. Inspected `cl_dll/death.cpp`,
`scoreboard.cpp`, `hud_msg.cpp`, `dlls/player.cpp`, `multiplay_gamerules.cpp` and
`teamplay_gamerules.cpp`. This is source-contract evidence, not new stock capture
or live parity. No SDK implementation body was copied.

## Normal application path and ownership

```
registered svc/user-message framing + owning protocol userinfo
  → RuntimeReplaySession staged safe display updates / immutable body views
  → GameClientHost / IGameClientModule
  → HalfLifeClientModule staged multiplayer state
  → existing atomic world + module commit
  → normal application's held TAB + viewport context
  → owning neutral HudDrawCommands → existing OpenGL HUD pass
```

New concrete code: `include/hlclient/games/halflife/multiplayer.hpp` and
`src/games/halflife/multiplayer.cpp`, in existing `hlclient_game_halflife`.
Owning bounded DTOs are in `include/hlclient/game_api/multiplayer.hpp`.
Generic host/replay/renderer targets do not link the concrete module and do not
interpret HL score sorting, team rules or death-message bodies.

The module stages from its previous committed multiplayer state. Malformed
suffixes reject the entire record: scores, names, notices, teams and the private
protocol occupant cache remain unchanged. Borrowed names/body views are valid
only for the synchronous call; retained names and notices own their strings.
Initial signon names seed the directory, and the existing bounded pre-baseline
queue now retains userinfo as well as HUD messages, preserving source order.
RX, baseline grammar, canonical hashes, TX receipts, prediction, assets, audio,
Studio animation and E13 visual authority are unchanged.

## Exact semantics and authority

Bindings are selected by exact registered name, not numeric ID. Actual IDs are
session-assigned; this task performed no live ID discovery. Replay tests use
independent IDs 201/202 to prove there is no fixed-ID assumption.

| Message | Accepted body | Effect |
| --- | --- | --- |
| ScoreInfo | Exactly 9: u8 slot, four little-endian i16 (frags, deaths, class, team number) | Set only that player's authoritative score values; negatives remain valid |
| DeathMsg | u8 killer, u8 victim, unique final NUL weapon token, ≤63 token bytes | One notice, never a score increment |
| GameMode | Exactly one byte, 0 or 1 | Non-team / team display policy |
| TeamInfo | Valid u8 slot + exact final NUL team string, ≤31 bytes | Player display team |
| TeamScore | Exact NUL team string + two i16, ≤31 name bytes | Explicit authoritative team aggregate |

Player slots must be 1..negotiated max, max ≤32. DeathMsg additionally accepts
killer 0 (world), equal killer/victim (self), victim 255 (non-player object) and
the SDK `teammate` token. No array access uses 0/255. Weapon text is inert
printable ASCII metadata, never a file/console/resource lookup (including inert
punctuation accepted by the pre-existing diagnostic decoder).
Empty tokens are safe; malformed/truncated/trailing bodies fail with typed
bounded metadata. Unknown registered messages preserve existing explicit ignore
behavior, never scan/resync.

DeathMsg cannot create a corpse, choose a death sequence, reset visual life or
decide respawn. Existing E13 server-entity classification and local lifecycle
evidence remain independent. The pre-existing local pending-death diagnostic
hint is preserved, not promoted into visual authority.

Only ScoreInfo updates player counters. Notices and ScoreInfo can arrive in
either order without double-counting. Repeated identical scores do not advance
their value revision; older source ordinals/cursors cannot overwrite newer
multiplayer state. Normal replay also rejects repeated record identities.

## Privacy, identity and lifecycle

Only the exact `name` entry produces display text: at most 31 printable ASCII
bytes; unsupported/control bytes become `?` deterministically. This matches the
existing ASCII bitmap renderer, which uppercases glyphs. It is not a UTF-8
shaper or an invented GoldSrc encoding conversion. Empty names retain the
existing strict parser rejection. Raw info values, private user ID, suffix and
unrelated keys have no new getter; no raw name logging was added.

`player_slot_from_client_index` validates the existing zero-based routing
contract before mapping index 0→slot 1 and index 31→slot 32. The local highlight
uses the existing receiving-player entity, never a name/lowest-slot guess.
The generic protocol owner holds a fixed 32-entry cache of immutable private
parser objects to compare occupants. Only a `new_occupant` boolean crosses the
game seam; neither ID nor private object crosses it.

Same occupant rename retains scores; changed occupant clears prior name/score/
team associations. Absence hides and clears the row, regardless of retained
ScoreInfo. A runtime-only exact vacancy profile accepts zero user ID + empty
info string + fixed 16-byte suffix; positive-ID signon parsing stays strict.
The ordinary name profile has existing capture evidence; this vacancy extension
is project-owned/offline-verified, **not newly stock-capture-verified**. Other
disconnect encodings are not guessed, and PVS/entity absence does not mean
disconnect. Manual reconnect/vacancy observation remains a separate check.

Each notice freezes event-time display names. Missing names use `Player <slot>`;
object sentinel uses bounded token text. Rename, departure and slot reuse never
rewrite an old notice. Map/network generation reset and teardown clear the
directory, teams, notice queue, high-water source and held UI context. Respawn
does not reconnect/reset Netchan or reset match scores. Explicit TeamScore
remains server-owned across membership changes, displayed only for active teams;
reset clears it. Without it, aggregates use current player ScoreInfo.

## HUD policy and bounds

TAB down/held shows the table; release/focus loss hides it. No forced-dead or
invented intermission visibility. W/A/D, mouse-look and reload keep their normal
input and wire behavior. Table is centered, width ≤760 pixels and ≤viewport−16;
rows compact deterministically to fit, without scrolling. Normal player sort:
frags descending, deaths ascending, stable slot tie. Teamplay groups active
players, sorting teams by the same score criteria then normalized team name;
players within a team keep the player sort. Only the local row has a green
highlight. Missing scores display `?`, never an invented server value. Numeric
team aggregates compact into their columns without clamping the sum of 32
signed-short player values. Neutral `[`/`]` bitmap glyphs render the text
fallback correctly; the renderer has no weapon/token interpretation.

Kill feed is upper-right: `killer [token] victim`; world/self use explicit
WORLD/SELF text. `death_notice_icon=not_implemented`: the current neutral HUD
has no approved HUD-icon pass; no new sprite system/assets were introduced.
`latency=unavailable`, column omitted; local RTT is not other players' ping.
This is Half-Life-inspired/local-compatible layout, not a pixel-exact stock UI.

Four FIFO notices, oldest evicted on fifth; 6 seconds from committed receipt in
the existing project-relative monotonic presentation clock. Clockless fixtures
start on first HUD sample; delayed live first render does not replay expired
notices. Render/update repetition never re-adds a notice. No wall clock/timer.

Fixed arrays: 32 players, 32 team scores, 4 notices, 64 worst-case table rows.
Shared neutral HUD limits: 16 rectangles, 224 text commands, 160 bytes/command,
16,384 total text bytes, 8,192 glyph-budget bytes. Worst team table uses 192 row
texts + 2 headers + 4 notices + existing local HUD, within these limits. Shared
validation rejects nonfinite/negative geometry, invalid colors/text and overflow;
host rejection clears the draw with `draw_rejected`, and GL validates again.
The layout is tested down to 160×120; the older basic HUD uses normal clipping
at tiny windows. Per-frame sorting/allocation is bounded by those fixed caps;
no filesystem, network parsing or asset import occurs in HUD composition.

## Verification record

Pre-edit source snapshot: `manual-artifacts/task-records/e14-multiplayer-20261002`
contains a 1,192-file manifest and copied current source. The pre-edit focused
Release baseline passed 72 cases / 4,665 assertions, one opt-in installed-asset
skip. This is not the final E14 acceptance result.

Required E14 tests include strict signed bodies, suffix rollback, safe names,
source ordering/duplicate suppression, slot replacement, notice-only score
authority, event-time receipt expiry, TAB with other input, all viewport bounds,
independent core-only module, production replay and normal OpenGL pixel A/B.

### Final acceptance — 2026-10-02

Result: `multiplayer_scoreboard_and_death_notices_integrated` in normal
`hlclient.exe`, offline-verified. The last production changes (world notice
label, aggregate column fit, neutral bracket glyphs) preceded every final build
and the complete final Release run. This documentation update does not alter
production code. No expected canonical hashes were changed. Three existing
life fixtures addressing player slot 2 were corrected to explicitly negotiate
two players in both their baseline and runtime context; old assertions were
not weakened.

| Check | Final result | Evidence under `manual-artifacts/task-records/e14-multiplayer-20261002/` |
| --- | --- | --- |
| VS2022 / Win32 normal Release build, helpers included | Passed | `build-release-final.txt` |
| Affected Debug client/tests build | Passed | `build-debug-final.txt` |
| E14 Release focused | 14 cases, 13,784 assertions; no skips | `release-e14-final.txt` |
| Debug E14 + E10–E13 + prediction/input/control/HUD regressions | 245 passed, 3 opt-in skips; 180,232 assertions | `focused-debug-final.txt` |
| Complete final Release offline suite | 2,668 passed, 38 skips, 0 failures of 2,706 results | `release-offline-final.txt` |
| Four process-sensitive offline checks, serialized | 4/4 passed | `release-serial-final.txt` |
| Combined final Release | **2,672 passed / 38 skipped / 0 failed**, 2,710 results | Both preceding logs |
| Core-only Release, actual HL module excluded | 41/41 CTest checks, including architecture guard; core/API 195 cases / 7,669 assertions | `core-only-final.txt`, `build-core-only-all.txt` |
| Bounded existing ASan Debug core/API target | Build passed; 29 cases / 1,635 assertions passed | `build-asan-core.txt`, `asan-core-final.txt` |
| Launcher `-CheckOnly`, reference/off and no-stock launcher/fast fixtures | Passed; selects normal Release, no game launched | `launcher-checkonly.txt`, `launcher-fixtures.txt`, `fast-fixtures.txt` |
| Source preservation / whitespace | Passed; 1,192 snapshot files, 23 intended existing-file changes, 1,169 unchanged, no missing/unrelated changes; `git diff --check` clean | `source-preservation-final.json` |

The full Release suite includes Null/application replay, real OpenGL tests,
existing E10–E13, weapon/audio/effects and prediction tests. E14's real OpenGL
A/B requires >500 changed table-region pixels, score changes confined to the
table, upper-right notice pixels, actual bracket-glyph differences and exact
baseline return after TAB release/notice expiry. This is GPU pixel evidence,
not a user's visual acceptance or live multiplayer evidence. The production
fake-peer startup test also verifies signon names and pre-baseline rename
delivery into the same module/HUD path; it is not an actual HLDS session.

The 38 Release skips are explicit opt-in installed assets/audio-device/SDL
capture and unavailable symlink/reparse/WFP/capture capabilities. No unavailable
capability was activated to remove skips. Three focused Debug skips are opt-in
installed-asset controls. Required E14 project-owned fixtures have no skips.
ASan success applies only to the bounded core/API test executable with the
official runtime on that process's PATH. **Full ASan application runtime remains
not run/not verified**; earlier application runtime failures are not relabeled.

Build commands against the retained configurations (no clean/reset):

```powershell
cmake --build build --config Release --parallel 6
cmake --build build --config Debug --target hlclient hlclient_tests --parallel 6
cmake --build build-core-g1 --config Release --parallel 6
ctest --test-dir build-core-g1 -C Release --output-on-failure
ctest --test-dir build -C Release -j 6 -E 'hlclient_stock_runtime_orchestrator_startup_|Fake HLDS profile variants|Suspended launch keeps identity mismatch' --output-on-failure
ctest --test-dir build -C Release -j 1 -R 'hlclient_stock_runtime_orchestrator_startup_|Fake HLDS profile variants|Suspended launch keeps identity mismatch' --output-on-failure
```

`build-core-g1` has `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`. Its actual
`game-boundary-manifest.txt` source/direct/transitive dependency lists exclude
the HL module. Core/API tests use an independent TestGameClientModule through
the same GameClientHost for context, output rejection, reset and teardown, with
no Half-Life creation/fallback. Normal application composition links the
existing host/API and concrete `hlclient_game_halflife`; lower host/core targets
do not acquire a concrete HL dependency.

Changed existing files (relative to the pre-E14 snapshot, not all current dirty
files):

```
CMakeLists.txt
apps/hlclient/main.cpp
docs/ARCHITECTURE.md
docs/REMOTE_PLAYER_DEATH_CORPSE_RESPAWN_E13.md
include/hlclient/game_api/game_client_host.hpp
include/hlclient/game_api/game_client_module.hpp
include/hlclient/game_api/presentation.hpp
include/hlclient/goldsrc/runtime_control_decoder.hpp
include/hlclient/goldsrc/runtime_replay_session.hpp
include/hlclient/goldsrc/user_info_update.hpp
src/game_api/game_client_host.cpp
src/games/halflife/client_module.cpp
src/games/halflife/server_messages.cpp
src/goldsrc/live_runtime_stage.cpp
src/goldsrc/runtime_control_decoder.cpp
src/goldsrc/runtime_replay_session.cpp
src/goldsrc/user_info_update.cpp
src/renderer/opengl/opengl_renderer.cpp
tests/CMakeLists.txt
tests/test_game_client_module.cpp
tests/test_resource_client_response_stage.cpp
tests/test_runtime_control_decoder.cpp
tests/test_runtime_replay_session.cpp
```

New ordinary project files: this document,
`include/hlclient/game_api/multiplayer.hpp`,
`include/hlclient/games/halflife/multiplayer.hpp`,
`src/games/halflife/multiplayer.cpp`, `tests/test_halflife_multiplayer.cpp`.
Required code is not hidden in generated artifacts. All earlier unrelated
local E11–E13 changes, worktrees and historical records remain preserved.

Ready executable:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,
3,587,584 bytes. SHA-256:

```
A0991DA9A6192795CCAF7664CA370F96BFFAD3E9C9765771BE30A33CF3440F10
```

Unchanged launcher's SHA-256:
`7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`.
No hidden E14 flag, debug/demo executable, resource copying or new manual
parameter is required. Known limits: ASCII text fallback/no icons, no remote
ping evidence, local-compatible compact layout, runtime vacancy profile only
offline-verified, live teamplay/reconnect not verified. E14 `manual=not_run`;
E13 earlier acceptance remains `attribution=user_report`.

No new live session, stock client, HLDS, Steam initialization, WFP activation or
managed capture. Staging/commit/push: none. `manual=not_run`,
`teamplay_manual=not_run`, `reconnect_manual=not_run` for this new build.

## Manual handoff (run by the user only)

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

On crossfire, hold/release TAB on both clients; check distinct names, one local
highlight and authoritative kills/deaths. Kill/respawn both directions: notice
appears once, scores arrive from server, E13 stays smooth/connected. Perform
several kills and wait >6 seconds: FIFO stays bounded and expires. Hold TAB
while moving/looking/reloading. Where practical, disconnect/reconnect/name change
should update rows without changing old notices. Close both windows and wait
for the unchanged launcher's cleanup; no new manual option is required.
