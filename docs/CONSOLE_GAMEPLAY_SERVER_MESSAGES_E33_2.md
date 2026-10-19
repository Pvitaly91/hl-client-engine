# E33.2 — Gameplay and server messages in the application console

## Scope and acceptance

Task: `HLC-E33-2-CONSOLE-GAMEPLAY-AND-SERVER-MESSAGES`.
Result: `console_gameplay_and_server_messages_integrated`, production integrated
and offline verified; new-build manual acceptance remains not_run.
Authoritative worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`.
Repository: `Pvitaly91/hl-client-engine`;
branch: `codex/stock-runtime-campaign-5e48b7c1`;
starting HEAD: `a2db33e83a1ce5c4eb5c13832c95d7bee0d2b8cf`.
The dirty E33/E33.1 tree is authoritative. Its 1,457 pre-edit source/config/doc
files and SHA-256 manifest are preserved under
`manual-artifacts/task-records/e33-2-console-gameplay-messages/source/` and
`source-manifest.json`; baseline status and binary identities are alongside them.

E33.1 **manual=passed**, `attribution=user_report`,
`scope=reported console output/layout/loading behavior`. This is the user's
confirmation that the console now works, not an assertion that every command,
resolution or ASan runtime was tested. Historical E33/E33.1 build-time manual
statuses remain in their original records.

This E33.2 build remains **manual=not_run**. Agent-created live game, Steam and
HLDS sessions: **0**. No server, bot, map, stock resource or launcher setting is
modified. No reset/clean/restore/stash/rebase/amend/branch switch, staging,
commit or push is part of this task. The launcher source/executable is unchanged.

## Routing audit and correction

The existing path is the strict `RuntimeControlDecoder`, followed by
`RuntimeReplaySession` framing borrowed message views, module staging and the
existing atomic observation/module commit. `GameClientHost::commit_record`
publishes owning text values through the typed core queue; the application
drains it into its persistent `ConsoleModel` in `begin_frame`. HUD drawing does
not publish console history. No new wire parser or command execution path exists.

| Input | Before E33.2 | Current source and destination |
| --- | --- | --- |
| DeathMsg | Strictly decoded; committed event-time HUD notices, no console adapter | The same notice construction in `multiplayer.cpp` stages one owning sentence before the four-notice HUD FIFO can evict it; commit emits `[game]` |
| SayText | Strictly parsed and committed; explicit `[server]` routing already survived E33.1 | Same once-only console route; separate bounded talk presentation uses existing neutral HUD primitives |
| TextMsg | Zero to four optional strings already accepted; all destinations incorrectly mirrored to console; no titles lookup | Same exact body parser; safe formatting and approved catalogue lookup; explicit notify/console/talk/center destinations |
| svc_print | Framed text reached console after runtime initialization; pre-baseline control was discarded | Same decoder/body span; initialization retains it in the existing bounded pre-baseline handoff, then ordinary commit publishes it |
| svc_centerprint | Decoder supported framing but display adapter absent | Same framed string reaches separate center presentation; no automatic console copy |
| Join/leave | Real TextMsg/SayText already supplied text, but notify destination was not respected | Actual received announcements only; no synthetic event from roster, entity/PVS presence, death or respawn |
| Rejection/kick | Coarse failure status; exact terminal disconnect reason was inspected but discarded | Existing accepted connection rejection and strict terminal-disconnect grammar provide prefixed, sanitized reasons |
| Local errors | Warnings/errors and command results already visible | Preserved; application adds a concise local failure category/context before teardown; renderer failure still relies on the existing diagnostic channel |
| Runtime progress | Diagnostic channel established in E33.1 | Still diagnostic/status-only; no per-packet/per-frame console stream is enabled |

Thus E33.1 did not blanket-filter accepted SayText or svc_print. The missing
death adapter and incorrect TextMsg destinations/absent catalogue were separate
integration gaps. The optional-string grammar was preserved rather than
misreported as a newly repaired four-required-parameter parser.

## Pinned source behavior

Verified official SDK revision:
`b1b5cf5892918535619b2937bb927e46cb097ba1`.
Read base-Half-Life `cl_dll/death.cpp`, `saytext.cpp`, `text_message.cpp`,
`dlls/client.cpp`, `multiplay_gamerules.cpp`, `util.cpp` and the destination
constants in `dlls/cdll_dll.h`. SDK code is a behavioral reference; no
implementation body, historical printf pattern or third-party engine code is
copied into this independent client.

| Death event | Console sentence, after the `[game]` source prefix |
| --- | --- |
| Ordinary | `Alice killed Bob with 9mmhandgun` |
| Egon | `Alice killed Bob with gluon gun` |
| Gauss | `Alice killed Bob with tau cannon` |
| World with world token | `Bob died` |
| Self, or killer 0 with another token | `Bob killed self with grenade` |
| Teammate | `Alice killed his teammate Bob`; no weapon suffix |
| Non-player victim sentinel 255 | `Alice killed a sentrygun` |

Self/world precedence and the teammate suffix exception follow the pinned
handler. Empty weapon metadata retains its empty `with ` suffix; the renderer's
`d_` icon-role prefix never becomes the weapon's console name. Other bounded
inert weapon tokens remain literal display data. No headshot, damage amount,
score change or death/respawn authority is inferred from DeathMsg.

Names come from the same event-time snapshot as the accepted HUD notice.
Rename, departure and slot reuse never rewrite previous sentences. Missing
names use the existing `Player <slot>` fallback; 0/255 are never array indices.
Player-display metadata retains the pre-existing bounded ASCII projection;
received UTF-8 chat/server text uses the existing repaired Unicode console path.

Pinned InitHUD announces joins with TextMsg/NOTIFY; ClientDisconnect sends a
SayText leave string. This integration displays those actual messages, including
bot announcements when supplied by the server. A populated TAB row is not
evidence that an announcement was received. A mod that omits announcements does
not cause the client to manufacture them.

Unlike pinned `ScrollTextUp`, which moves chat to console on HUD scrolling,
this client's SayText console append occurs immediately on commit. HUD wrap,
expiry, visibility and reopening cannot append a second copy. Local `say`
reports a pending server request; it does not manufacture a provisional accepted
chat event. The later server SayText remains the acceptance source.

## TextMsg destinations and approved titles

The pinned destination values are NOTIFY=1, CONSOLE=2, TALK=3, CENTER=4.
NOTIFY and TALK publish console text plus their separate HUD presentation;
CONSOLE publishes only console text; CENTER and svc_centerprint use center
presentation without an automatic console mirror. Screen presentation uses the
existing bounded neutral HUD text pass: four notify entries, four talk entries
and one center entry, five-second local-compatible visual lifetime. This does
not assert original engine font, timing or layout parity. Its ASCII fallback
does not alter the full Unicode console record, and hud_draw does not gate
history publication.

There was no existing titles resolver. `goldsrc/text_messages.cpp` now parses a
narrow bounded `titles.txt` catalogue, with 1,024 entries, 63-byte inert keys,
4,096-byte entry text and a 256 KiB source/retained-text bound. Negative `$effect`
values -1..-4 override destination; the other title visual directives remain
inert. Duplicate/incomplete/oversized catalogues fail as a whole. Lookup is one
level only, including parameter lookup, so cyclic text cannot recurse.

Half-Life declares its compiled `titles.txt` dependency through a neutral API.
The existing `ApprovedHudAssets` worker opens it through `ExactWorldHudSource`,
the selected approved world's exact root, locator identity verification and
read-only source opener. There is no CWD/other-install fallback, server-selected
native path, new asset framework, per-frame file I/O or copied original resource.
The application binds the resulting owning neutral catalogue after preparation
and rebinds it after the existing module reset serial changes.

Read-only original-resource control uses the selected licensed copy
`D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\titles.txt`: 11,762 bytes,
160 entries, SHA-256
`9CFAD58A728DF6081271F0B1D53F3F382DDFDA23A39500B1026AFAF4856C465A`.
This identifies the inspected local resource rather than every HL installation.
Missing/unavailable/rejected optional catalogue data leaves unknown `#tokens`
visible; messages committed before catalogue readiness are not replayed or
rewritten later. The resource status remains in prepared metadata.

Formatting evaluates only sequential `%s` and literal `%%`; zero to four exact
optional string parameters are supported. Missing arguments are empty; parameter
end-newlines are stripped. `%n`, numeric/positional/width directives and commands
remain inert text. Unresolved tokens retain their parameters as visible data.
Output is bounded to 4,096 bytes and the existing maximum 32 console lines per
display request. No received string enters printf, exec, alias, bind or a path
opener. Existing stufftext restrictions are unchanged.

## Commit, ordering, deduplication and lifecycle

`stage_multiplayer` and the existing text handler produce owning values in the
same staged `GameRecordState`. The combined text list is ordered by existing
record ordinal/end cursor before the host commit. Malformed message bodies or
wire suffixes abandon staged output along with the rejected record. Two distinct
events with identical text remain two events; text hashes and timestamp windows
are not used for deduplication.

The host's existing source high-water cursor delivers each accepted text event
once. Its reset/teardown remains generation-owned, and normal session callbacks
detach before owner destruction. No new subscriber list is created. Console
clear affects visible history only; it does not reset the source cursor or
reconstruct records from the four active HUD notices. A separately received
server print that repeats a kill's meaning remains its own server event.

The live initialization owner can accept several bounded pre-baseline
subrecords inside one service payload. A small `ConsolePublicationBatch` in the
same host defers their text until the complete owning payload succeeds. It holds
at most 128 events/64 KiB, publishes in source order, discards on failure and
invalidates across reset serial changes. It changes console publication only;
it does not introduce another decoder, rewrite protocol state or alter gameplay
commit semantics. The ordinary replay transaction is still the staging owner.

History persists in the application after disconnect or a recoverable failure.
The new record's visibility does not depend on console-open state, HUD drawing,
player life, window focus, notice lifetime or HUD capacity. There is no delayed
HUD-to-console expiry callback.

## Error provenance and spam protection

The existing terminal-disconnect inspector exposes a borrowed reason only after
the entire exact terminal record validates. The live owner copies it into the
sanitized server-text route before teardown. Rejection text comes from the
already decoded ConnectRejected object; local timeouts/client failures retain
an explicit local label and project-authored category/context. Resource exceptions
and unsuccessful commands retain their existing visible error routes. No fatal
exit code or receipt meaning is changed. A broken renderer cannot guarantee a
visible error screen; existing diagnostic/report output remains independent.

Server text retains its `[server]` prefix, newline/control filtering, bounded
UTF-8 repair and secret redaction. Accepted death sentences use `[game]` and
only the public event-time names/inert weapon token; legitimate display names
containing words such as `password` are not hidden as if they were raw userinfo.
No raw userinfo, tickets, credentials or private identity fields are accessed.

Technical info/debug still defaults to the diagnostic channel. Command results
append directly, and loading progress still updates its status model. Combat
events bypass the diagnostic/ordinary notice rate counter, so many bot kills
are neither collapsed nor filtered by repeated text. All output remains subject
to bounded queue capacity/bytes and normal history eviction: 32 events/4 KiB per
module record, 512 queued records/256 KiB overall, and the existing 1,024-line/
256 KiB scrollback. Ordinary server/warning rate bounds and reserved critical
error capacity remain. Server disconnect/reject reasons use the critical lane.

## Validation and ready binary

Evidence root: `manual-artifacts/task-records/e33-2-console-gameplay-messages/`.
All final build/suite results and binary identities are filled after the frozen
production source is verified. No E33.1 suite count is reused.

- `build-failing-before.log`, `failing-before.log`: new regressions built against
  the saved production baseline; **5 cases failed, 33 assertions = 25 passed /
  8 failed**. Death output was absent and CENTER incorrectly entered console.
- `build-release-first.log`: production compiled; a new test used an incorrect
  framebuffer observation member/operator. Corrected to the real API.
- `focused-release-first.log`: **10/11 cases passed**, one fixture incorrectly
  sent an input event without beginning its tracker frame. Corrected fixture;
  existing production assertions were not weakened.
- `focused-release-retry2.log`: **11 cases / 957 assertions passed**, an
  intermediate run before the owning-payload publication scope and additional
  original-resource controls. It is not the final acceptance run.
- `build-core-final.log`/`core-final.log`: intermediate core-only build and
  45/45 CTest pass before the final publication-scope edits; the source-frozen
  core results are recorded separately below.

Final source-frozen results (VS2022/Win32; normal Debug/Release warnings-as-errors
ON, existing core-only cache warnings-as-errors OFF):

| Verification | Actual result / evidence |
| --- | --- |
| Normal Release client, complete tests, focused tests and core API targets | PASS, `build-release-final.log` |
| Normal Debug client and focused tests | PASS, `build-debug-final.log` |
| Focused Release E33/E33.1/E33.2 | **97 cases / 84,434 assertions passed, no skips**, `focused-release-final.log` |
| Focused Debug E33/E33.1/E33.2 | **97 cases / 84,317 assertions passed, no skips**, `focused-debug-final.log` |
| E14/E29 notices, userinfo, transactions and control protocol | **166 passed / 1 skipped**, 82,961 assertions, `notices-protocol-release-final.log` |
| Actual normal Debug idle and Null startup | PASS, `debug-idle-null-final.log`; isolated two-frame idle, no game/authentication attempt |
| Core-only client/API build and complete suite | **45/45 CTest entries passed**, 33.00 s, `build-core-source-frozen.log`, `core-source-frozen.log` |
| Independent alternate-module/API subset and core Null | **42 cases / 1,094 assertions passed** plus Null startup, `alternate-core-null-final.log` |
| Architecture | PASS in both core-only and normal final Release suites |
| A/B launcher reference/RemoteAudioPeer/NoTimeLimit/CheckOnly/SourceOnly | PASS, `launcher-ab-checkonly-final.log`; external resource/Steam readiness deliberately not checked |
| Complete final Release offline suite | **3,765 passed / 71 skipped / 0 failed**, 3,836 CTest entries, **416.93 s**; `full-release-final.log`, `full-release-final-details.log` |
| Post-suite normal CLI/Null, transport, reporting and no-stock launcher controls | PASS; `build_source.ps1 -Configuration Release -TestOnly -Parallel 2` completed with exit 0, including final CheckOnly/SourceOnly; no live readiness claim |
| Source preservation and whitespace | **1,457 snapshot hashes verified, no missing files**; 22 intended existing-file changes, `source-preservation-final.json`; `diff-check-final.log` PASS, pre-existing LF/CRLF notice only |
| ASan build | PASS, `build-asan-final.log`; existing test-driver-only `/Od /Ob0`, production instrumentation/options unchanged |
| ASan runtime | Unavailable before tests, `asan-runtime-final.json` |

Focused assertion totals vary with the scheduling-dependent native-worker poll
and teardown assertions; both runs execute the same 97 cases. The notices /
protocol subset's one skip is the explicitly unselected original killfeed
resource audit (`HLCLIENT_E29_ASSET_ROOT`). The core-only aggregate has an
internal optional original-sky skip (`HLCLIENT_SKY_ASSET_ROOT`): **444 cases =
443 passed + 1 skipped, 409,037 assertions**. A passing enclosing CTest entry
does not turn this optional asset check into a pass. Exact details are in
`core-source-frozen-details.log`.

The exact final 71-entry CTest skip list is retained in
[full-release-final.log](../manual-artifacts/task-records/e33-2-console-gameplay-messages/full-release-final.log),
with each individual reason in
[full-release-final-details.log](../manual-artifacts/task-records/e33-2-console-gameplay-messages/full-release-final-details.log).
The categories are unselected optional approved assets/Steam runtime, explicit
audio-playback and dynamic-WFP opt-ins, unavailable symlink/reparse/second-volume
fixtures, and hidden-SDL focus/relative-mouse capability. No skip is a pass.
The broad asset-free run skips the two original console-resource cases and the
new original titles case; all three are separately executed and passed in both
focused runs with the approved root. The full suite's aggregate core API entry
also retains the internal optional sky-asset skip described above.

The post-suite records explicitly report `stock-processes-started=0`,
`capture-files-written=0`, `Live sessions=0`, and successful Release source/offline
verification. The unchanged launcher still selects the actual new Release.

The new composed OpenGL control isolates **424 actual glyph pixels** for the
committed Gauss death sentence. It validates the text's clip and compares the
framebuffer with only that text command removed; all changed pixels stay inside
the half panel. Debug and Release PPM captures match byte for byte, SHA-256
`B01D9ED355C53BF80DE2AB7F4A6D4A08C68662239A640DECAE99898A4E1E58B8`.
The lossless PNG was visually inspected:
[committed console record, 640x480](../manual-artifacts/task-records/e33-2-console-gameplay-messages/screenshots-release/e332-committed-console-640x480.png).
This synthetic fixture clears history and then commits one event to isolate its
pixels; its single row is genuine one-record history, not a filled-history
collapse or a live game screenshot. Existing E33.1 filled full/half layout,
Unicode, pre-world input and curtain controls pass in both focused runs.

The original console backdrop and new original titles controls run with the
approved root in both focused configurations. The new owned-root fixture also
proves that an alternative game's `titles.txt` is not replaced by Valve fallback
and that an arbitrary/traversal resource name cannot authorize a read.

ASan probes completed naturally with no timeout/termination: default child PATH
`--help` exited `0xC0000135` (-1073741515); adding the matching installed MSVC
14.34.31933 x86 runtime directory only to each child's PATH made both `--help`
and `[e33_2]` exit `0xC0000142` (-1073741502). Each produced zero stdout/stderr
characters. Compilation passed; sanitizer execution did not start. No DLL was
copied and no machine PATH or sanitizer infrastructure was changed.

Ready normal Release:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
Actual SHA-256:
`F8E196FFEA2FF3076A1BA90F1B7B0B7FE64E420D91EA9DD8B7C2BB07AD36D46F`.
Unchanged normal launcher:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\HLClientLauncher.exe`.
Actual SHA-256:
`4954BD85FF7B661D0AE9CFDC807CD01141AA90D0FCEE2E1D08F73C2ECEDD5688`.
These identify the local files inspected during this task, not a historical
user session. The launcher retains its existing sibling-client selection and
does not require rebuilding for this change.

## Production files

- `src/games/halflife/server_messages.cpp`, `multiplayer.cpp`, `client_module.cpp`
  and the server-messages header: source-backed event formatting, destination
  policy, safe substitution and separate bounded HUD presentation.
- `include/hlclient/game_api/text_messages.hpp`, module/host headers and
  `src/game_api/game_client_host.cpp`: neutral catalogue/events, source
  validation, committed publication and payload scope.
- `include/hlclient/goldsrc/text_messages.hpp`, `src/goldsrc/text_messages.cpp`,
  existing HUD assets header/implementation: pure bounded titles parsing and
  exact-root read through the existing startup worker.
- `src/goldsrc/runtime_replay_session.cpp`, `live_runtime_stage.cpp`,
  `include/hlclient/goldsrc/disconnect_request.hpp`, `apps/hlclient/main.cpp`:
  existing control handoff, whole-payload console publication, accepted reasons
  and normal application binding/error output.
- Core log header/implementation: typed game-event delivery through existing
  sanitation/queue mechanisms and explicit critical server reasons.
- CMake/test files and E14/E33/E33.1/architecture task records: compilation,
  regression coverage and component-specific acceptance history.

## Manual acceptance for this new build

Use the unchanged launcher and its current saved server address. No new feature
flag or copied WAV/WAD/titles file is required.

- [ ] Player/bot kills show one console sentence with the weapon.
- [ ] The sentence remains after the HUD notice disappears and while console was closed.
- [ ] Actual join/leave announcements, chat and useful server prints appear.
- [ ] Locally routed events do not duplicate; command output remains readable.
- [ ] Scrollback, loading animation, disconnect and reconnect still work.

Join/leave acceptance requires a real announcement during the user's run; a
TAB roster alone is insufficient. Unavailable manual scenarios stay not_run.
No bot-count or server-setting changes are requested. This build is
**manual=not_run** until the user tests it.
