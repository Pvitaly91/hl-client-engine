# Public Steam connection diagnosis — 2026-10-18

Status: the protocol/runtime connection now passes the previously observed
public-server failures. The subsequent rendered user run exposed a separate
local-asset size check; the final section records its generic correction and
the distinction between protocol, asset-loading and rendered verification.

## Observed failure

Run `324e57d97447d8a70173cc30d639c194` attempted
`2.28.69.179:27015` from the console. Its receipt fact mask `4103`
contains Steam initialization, fresh authentication material and connect sent,
but no connection acceptance. The server returned `STEAM validation rejected`.
The console recovered; the application subsequently exited normally. The user
confirmed that original Half-Life connects with the same account.

The launcher's saved target in the application report is not the console
attempt's destination. Use `console-attempt-A-start.json` and the new typed
authentication/connect endpoint fields for this comparison.

## Wire boundary finding

Before the follow-up fix, the production builder in
`src/goldsrc/connect_request.cpp` appended the binary ticket directly after
the closing userinfo quote without a separate line terminator.
Existing builder/self-parser round trips agreed
with one another and therefore did not detect this interoperability defect.

Xash3D writes LF after the textual connect header, then appends the ticket's
actual byte count. Reference: [FWGS xash3d-fwgs,
cl_main.c](https://github.com/FWGS/xash3d-fwgs/blob/03a396e208cfd50fd7a59ed0fa0cbe03ca4bb54c/engine/client/cl_main.c),
revision `03a396e208cfd50fd7a59ed0fa0cbe03ca4bb54c`.

ReHLDS consumes the connectionless text line before forwarding the remaining
bytes to Steam. Its line reader consumes a NUL or LF terminator; a space is
not a terminator. Thus the absent separator consumes a prefix of the ticket.
Byte `0xff` is an unsigned byte here, not EOF. The server emits the observed
rejection wording when authentication fails outside LAN mode. References:
[common.cpp](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/common.cpp),
[sv_main.cpp](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_main.cpp).
The actual public server's implementation and configuration remain unknown.

The independent offline receiver test uses the real project serializer and
four synthetic four-byte tickets. Without LF it sees 3, 1, 3 and 1 bytes;
inserting LF in a local control copy preserves all four bytes in every case.
It does not use the project's matching connect parser or a real Steam ticket.

The Xash Steam broker also continues pumping callbacks after ticket creation,
and its wrapper selects SteamUser v023 versus this client's v021. These are
separate differences, not established causes of this immediate rejection.
References: [broker](https://github.com/FWGS/steam-broker/blob/b716b381b0a998b938ba733262d4821c3583c7ee/src/broker.rs),
[wrapper](https://github.com/a1batross/steamworks-rs/blob/1ca2867fd14688d76bb3d896f5e606ab43ad36c6/src/user.rs).

## Added diagnostics

Each external connection attempt now writes `external-authentication.json`
in its existing receipt directory, after provider/session teardown. Reconnects
use their existing distinct attempt directories. Capture does not require F8
or raw network tracing.

The bounded report includes actual endpoint, protocol, SteamID class only,
secure flag, actual App ID/login state, callback count during acquisition,
returned ticket length, transmitted binary ticket length, header LF presence,
response timing/classification, and ordered initialization/send/termination
events. Missing values remain null. Connect ACCEPT is not declared completed
Steam validation. The old trace's combined size includes cdkey bytes; the new
binary suffix size is used for comparison with the Steam API result.
Event timestamps describe observation/draining by the client, not exact native
Steam worker call times. Phase elapsed values are reported separately.

No ticket bytes, ticket hashes, raw Steam/account IDs, challenge nonce, cdkey,
userinfo contents, packet previews or arbitrary server text are persisted.
Report failure remains an optional diagnostic failure and cannot change the
connection outcome. Auth behavior, packet serialization and server settings
were unchanged by the initial diagnostic task. The authorized follow-up
correction below changes only the connect framing and matching parser.

## Initial diagnostic-stage verification

Focused target: `hlclient_steam_diagnostics_tests`; the same cases also belong
to `hlclient_tests`. It covers provider failures/lifetime, byte-count metadata,
redaction, nullable fields, bounded event retention, reconnect isolation and
the independent wire-boundary control.

Verified on 2026-10-18:

- Normal Release `hlclient.exe` built successfully.
- Focused Release tests: 76 cases / 1585 assertions passed.
- Focused Debug tests: 76 cases / 1580 assertions passed.
- Independent Release wire-boundary test: 1 case / 40 assertions passed.
- Release null-renderer smoke completed two frames and exited with code 0.
- `git diff --check` passed. Launcher binary was not rebuilt or replaced.

Release client SHA-256:
`A91CF283AF3247F449F07EE3399A783A4E0A6BE15141F631E43458599AE3909B`.
Unchanged launcher SHA-256:
`4E01A6F33DE394F716B939736E818CD6289EB83D6046B3D2FCCF6481937F7A3F`.

Build/test logs and before copies are in
`manual-artifacts/task-records/steam-auth-diagnostics-20261018`.
No live server connection or Steam initialization was initiated by the agent.
This task identifies the serializer defect and adds diagnostics; it does not
claim that public-server compatibility has been repaired or verified.

## Follow-up correction — 2026-10-18

The production builder now emits one LF between the closing userinfo quote
and the unchanged Steam ticket. Packet-size/preflight accounting includes
that byte. No terminator, padding or trimming is applied to the ticket.
The parser requires and consumes exactly one LF. Independent replay/capture
fixtures use the same corrected framing; old historical capture length
constants remain historical metadata rather than emitted-packet assertions.

The independent receiver regression now requires the production packet to
preserve all bytes in six synthetic tickets, including leading/trailing NUL,
LF, CR, space and `0xff`. Removing the header LF in its negative control
reproduces the old data loss. Exact size limits, repeated serialization,
round trips, reconnect diagnostics, loopback UDP handshakes and replay
fixtures are also covered. Steam API selection/lifetime, identity and server
configuration were not changed. No authentication bypass was introduced.

Verification:

- Release client, focused tests and stock runtime capture tool built.
- Release: 117 test cases / 3654 assertions passed.
- Debug: 117 test cases / 3668 assertions passed.
- Explicit wire-boundary run: 2 cases / 171 assertions passed.
- Release null renderer completed two frames, exit code 0.
- `git diff --check` passed; launcher hash remained unchanged.

Updated Release client SHA-256:
`7245E77E4F55C051B2506D53E3697A2F57A0DF36BA630CA777C4374D3B36BAEA`.
Evidence directory:
`manual-artifacts/task-records/steam-connect-lf-fix-20261018`.

No public-server connection or real Steam initialization was performed by
the agent. The framing defect is fixed; successful authentication on
`2.28.69.179:27015` is not yet claimed. A subsequent attempt will record
`authentication_header_has_lf: true` and the actual transmitted ticket length
in the existing safe per-attempt authentication report.

## Resource-transition follow-up — 2026-10-18

User run `7321ac9669f77cd0d99e67513a5d5d0a` reached connection acceptance on
`2.28.69.179:27015` with the corrected LF boundary and matching Steam API /
transmitted binary ticket lengths (212 bytes). Its subsequent failure was
`response_stage_failed` / `resource_list_stage_failed`, specifically
`wrong_next_boundary_opcode`. Source sequence 13 carried a reassembled bzip2
resource response: 582 compressed bytes, 11371 decoded bytes.

The old failure metadata pointed at the initial opcode 45, not the actual
failing next boundary. The exact live byte at offset 9 was not retained;
therefore the presence of opcode 56 in that run is an interoperability
hypothesis, not a packet-level observation. Two other recent attempts still
reported Steam validation rejection; acceptance is not evidence of universal
Steam authentication stability or successful map entry.

The confirmed parser defect was a mandatory `45 -> 43` adjacency. ReHLDS can
emit `45 -> 56 + NUL-terminated resource location -> 43` when FastDL is
configured. The parser now consumes that single optional field with the
server's 128-byte bound, then requires opcode 43 at the exact next cursor.
It neither searches ahead nor initiates downloads. The location is not
copied into diagnostic fields. References and the exact grammar are in
[resource-transition documentation](GOLDSRC_RESOURCE_TRANSITION.md).

The durable external report now carries the specific typed transition error
and its numeric offset. Only validated service-message boundary bytes can
be recorded as opcodes; unterminated/oversized location errors retain their
numeric body offset without disclosing a URL byte as an opcode. Missing
next opcodes remain null rather than inheriting stale wrapper metadata.

Verification:

- Normal Release client and both focused test targets built successfully.
- Resource Release: 118 cases passed, 1 skipped; 405353 assertions passed.
  The skipped case requires Windows file-symlink creation, unavailable here.
- Resource Debug: 118 cases passed, 1 skipped; 405353 assertions passed,
  with the same file-symlink limitation.
- Steam Release regression: 117 cases / 4699 assertions passed.
- Release null-renderer smoke: two frames, exit code 0.
- `git diff --check` passed; launcher hash remained unchanged.

Updated Release client SHA-256:
`C67FDBCFC51AD251A85F637749D2FFCB78C4E9140B954EDEBE59F0874D36C9E0`.
Evidence directory:
`manual-artifacts/task-records/resource-fastdl-fix-20261018`.

Tests include compressed resource responses, nonzero control offsets,
downstream list parsing, legacy responses without FastDL, malformed bounded
strings, unknown following messages, exact failure positions and diagnostic
redaction. They use synthetic data and local loopback, not Steam or a live
public server. The updated binary still needs a user connection attempt to
confirm whether this resolves the observed failure and reaches the map.

## Runtime customization and related controls — 2026-10-18

User run `cfd63a80b37eef40acf93bf79ef1b757` used Release hash
`C67FDBCFC51AD251A85F637749D2FFCB78C4E9140B954EDEBE59F0874D36C9E0` and
reached preparing-map after accepted connection to `2.28.69.179:27015`.
Its durable report identifies `runtime_record_failed` / `unsupported_opcode`,
wire opcode 46, source sequence 21, record ordinal 3. The map was Crossfire
and the server maximum was 20 clients. Neither signon completion nor gameplay
was recorded. This is later than the previous resource-transition failure.

Opcode 46 is `svc_customization`. The new bounded parser consumes the exact
player/resource/name/index/size/flags grammar and the optional 16-byte digest
only when `RES_CUSTOM` is set. It retains numeric framing metadata, not a
filename or digest, and does not initiate downloads or register a custom
decal. Player slots, resource types, signed size, name bound and optional hash
length are validated. The independently authored tests are not a claim of
having recovered the unretained live packet body.

The lookahead audit also found standard control gaps. Exact inert framing is
added for pings (17), intermission (30), CD track (32), transfer failure (49),
director payloads (51), voice data (53), extra info (54), timescale (55),
resource location (56), and cvar queries (57/58). The old `svc_finale=32`
mapping is corrected to 31; CD track consumes exactly two bytes instead of a
string. Pings use the terminated bit grammar and byte alignment; voice data
uses the GoldSrc sender/16-bit-length body, not Xash's newer protocol layout.
The directly related player-spray event `TE_PLAYERDECAL` (23/112) also gets
exact inert framing so drawing a spray cannot re-trigger the same failure.

These compatibility envelopes do not add voice playback, custom spray
pixels, remote cvar access, arbitrary command execution, or file transfers.
Cvar queries are consumed without a reply; a server plugin requiring replies
can still time out or reject this unsupported capability. Other unknown
messages/subtypes continue to fail closed instead of guessing their length.
HLTV's subtype-dependent command 50 remains outside this change. GoldSrc's
observed `svc_signonnum 1` behavior stays unchanged; other protocols' internal
signon stages are not used to relax the stock wire contract.

Runtime-control failures now persist the typed nested code, phase/category
and message-start byte cursor in the existing external JSON. No raw body,
custom filename/hash, voice contents or cvar names are added to the report.

Primary references:

- [ReHLDS customization writer](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/sv_upld.cpp),
  `SV_Customization`.
- [ReHLDS server controls](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/sv_main.cpp),
  `SV_EmitPings_internal`, `SV_SendServerinfo`, `SV_WriteSpawn`.
- [ReHLDS voice sender](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/sv_user.cpp)
  and [opcode table](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/net.h).
- [Xash GoldSrc parser](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/parse/cl_parse_gs.c)
  and [shared parser](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/parse/cl_parse.c).
- Valve SDK `dlls/util.cpp`, `UTIL_PlayerDecalTrace`, and
  [Xash temporary-entity receiver](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/cl_tent.c)
  agree on the ten-byte player-decal body (no optional model field).

Verification:

- Normal Release client built successfully.
- Public-server runtime suite: 137 cases / 51421 assertions passed in Release
  and the same 137 cases / 51421 assertions passed in Debug.
- Resource-transition Release regression: 119 cases / 405366 assertions
  passed; one file-symlink case was skipped because creation is unavailable
  in this Windows environment.
- Steam Release regression: 117 cases / 8167 assertions passed.
- Release null-renderer smoke: two frames, exit code 0.
- `git diff --check` passed; the launcher binary remained unchanged.

Updated Release client SHA-256:
`D41943C7D71EDB60AF715559FE2969AAA9BC907DD17C72D07AAAFF2F1C1EE945`.
Evidence directory: `manual-artifacts/task-records/runtime-public-controls-20261018`.

These checks use offline fixtures and local loopback. No new Steam ticket,
public-server connection, remote configuration change or asset download was
performed. Live map entry with this updated binary has not yet been observed;
passing these tests does not assert complete compatibility with every server
plugin or every remaining unsupported service message.

## Successful public-server session and ping HUD — 2026-10-18

The subsequent user-run `831e45ccdb1733a24ebb364824ea7c13` used the above
`D41943...EE945` binary and confirms successful Crossfire map entry on
`2.28.69.179:27015`. The durable report records connection at 2.844 seconds,
signon/map entry at 2.984 seconds, and gameplay at 6.342 seconds. About
187.4 seconds of gameplay preceded normal `window_close`, exit code 0,
and a transport-acknowledged graceful disconnect after 52 ms. There was no
reported protocol or driver failure. This is live evidence, not an inference
from the previous offline tests.

The controlled/alive cohort contains 21587 frames, mean frame interval
7.739 ms (about 129.2 FPS), maximum 39.724 ms, five frames above 33.3 ms and
none above 50 ms. No MSAA was active. Prediction was not entirely clean:
there were nine fallback entries (including life-boundary cases), two
`movement_trace_failed` frames around 86.529 seconds in lift/support context,
and exact same-time camera corrections of 7.672 and 6.265 units at 47.593
and 61.790 seconds. The 421.772 ms coherent-state age observed later is not
a ping measurement. No F8 marker was recorded. This turn does not change
movement correction algorithms based on those observations.

Sources are `external-run-report.json`, `session-receipt-A-terminal.txt`,
`external-diagnostics.json`, `external-movement.json`, and
`external-video-final.json` under
`C:/Users/admin/AppData/Local/HLClientEngine/runs/831e45ccdb1733a24ebb364824ea7c13`.
The status text was byte-truncated; structured reports remain available.

The new ping HUD retains the numeric values in `svc_pings` and maps zero-based
wire player indices to the existing one-based display directory. The standard
`IN_SCORE` bit is sent while Tab is held in the normal Half-Life input profile;
it is preserved in immutable command history but has no movement effect.
The existing synthetic input profile remains unchanged.

Both local and remote players receive a `PING MS` scoreboard column. Values
are server-reported milliseconds, not estimated from FPS, correction size or
snapshot age. A missing measurement displays `-`, while an actual zero is
shown as `0`. Disconnect, occupant replacement and session reset clear old
measurements. Malformed packets cannot partially publish ping updates.

Reference: [ReHLDS SV_ShouldUpdatePing / SV_EmitPings_internal](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/sv_main.cpp)
and [Xash GoldSrc svc_pings dispatch](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/parse/cl_parse_gs.c).
The server controls the update cadence; ReHLDS caches measurements for two
seconds. Opening the scoreboard does not fabricate a fresh zero measurement.

Ping-build verification:

- Normal Release client built and completed the two-frame null-renderer smoke.
- Focused runtime/HUD/input regression suite: 183 cases / 71001 assertions
  passed in each of Release and Debug, with no skips.
- The suite includes actual OpenGL pixel checks for both ping rows, bounded
  32-player/team layout, exact wire decoding, duplicate-slot last-wins,
  multiple ping-list storage lifetimes, malformed-record rollback, slot reuse,
  session reset and unchanged walk/jump/duck/ladder motion with Tab held.
- `git diff --check` passed. Launcher unchanged; no live server was started
  or contacted for this new build, and no server settings were changed.

Release `hlclient.exe` SHA-256:
`00270E267B62855ADF7431DD069B01DE4AA2243C71D191B30843BB72F89CF770`.
Evidence: `manual-artifacts/task-records/ping-hud-20261018`.
The successful user-run audited above predates this ping addition; actual
network ping rendering in the user's next live session is not yet observed.

## Customized server early controls — 2026-10-18

User run `1f9083fe28880b9b93a7971fa17aac39` on `45.83.244.193:27020`
failed before server-info with `response_stage_failed`. The older durable
report did not retain the nested parser's numeric opcode. A bounded headless
connection with the new numeric diagnostics reproduced the failure exactly:
`svc_sendcvarvalue2` (58), offset 0, server sequence 1, in a 35-byte payload.
The connection was accepted; this was an early service framing failure,
not the previous Steam-ticket formatting problem.

The initial service decoder now frames `svc_nop` (1), `svc_stufftext` (9),
`svc_sendcvarvalue` (57), and `svc_sendcvarvalue2` (58), including an independent
control-only packet before server-info. The latter has an exact little-endian
32-bit request ID followed by a bounded NUL-terminated name. Commands and cvar
queries remain inert: they do not execute commands, read local settings or
generate a cvar response. Only framing metadata is projected. A server policy
that requires a cvar response is a separate compatibility concern, not a reason
to fabricate settings or disclose protected values.

The neighboring extra-info (54) body is now read as a bounded NUL string plus
one flag byte, not hardcoded to an empty string and zero flag. Benign 1/8/9
controls may occur before its mandatory delta-description boundary. The
resource-transition stage likewise accepts fully decoder-validated 1/8/9/57/58
intermediate messages, preserving the exact boundary and message limits.
Unknown opcodes, malformed strings and truncated identifiers still fail closed;
there is no scan-ahead recovery and no remote command execution or file download.

Initial service errors now retain typed code, exact byte offset, wire opcode and
source sequence even with network tracing disabled. Higher-level error wrappers
do not replace this metadata with null values. Console diagnostics contain only
numeric provenance; no authentication material or query names are added.

Wire references: [Xash common GoldSrc parser and CL_ParseCvarValue](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/parse/cl_parse.c)
and [ReHLDS server-info writer](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/sv_main.cpp).
Evidence directory: `manual-artifacts/task-records/custom-server-signon-20261018`.

A second bounded connection passed opcode 58, server-info, all seven delta
schemas (219 fields), movevars, eleven initial player records, and the exact
45/56/43 resource boundary. It exposed the next independent rejection: the
custom server advertises more than the former default 1024 resources.
Resource-list, inventory, readiness and manifest entry defaults now cover the
12-bit wire count (4095). Atomic list publication reserves count + 3 events
(4098). Explicit smaller caller limits still apply. Individual/aggregate name
bytes, payload bytes, paths, types, flags, loaded-asset memory budgets and
declared-size checks remain unchanged. Count rejections now include the safe
numeric count and limit. This does not enable FastDL or arbitrary file writes.

Further bounded probes also reproduced a timing-dependent one-slot buffering
bug, independently in the initial stage and the resource transition: more than
one valid owning service payload could arrive before the reliable request ACK.
Both stages now keep a count- and byte-bounded FIFO until that ACK. They decode
in arrival order, and return any post-boundary tail to the same driver's bounded
event ring before later events. The handoff does not reapply transport sequence
numbers, ACKs or receive statistics. Queue overflow still fails explicitly;
messages are not dropped, scanned past or interpreted before the request ACK.

The next exact rejection was a repeated decal identity: type 3, index 4095,
wire ordinals 0 and 1. Xash retains resource rows but only installs decal names
within its bounded decal table; 4095 is outside that table. The narrow supported
case now preserves ordinary decal-4095 rows as non-bindable metadata. It is not
described as a protocol-defined sentinel: a signed -1 origin has not been
established. Every name, size code and wire ordinal remains in the resource,
inventory, readiness and manifest rows. No slot 4095 is allocated for decals,
and indexed lookups return no match even for a singleton. Other duplicate
type-local identities still fail, including model/sound indices of 4095 and
ordinary decal slots. Resource flags, path rules and aggregate bounds remain
checked before publication.

Reference: [Xash CL_PrecacheResources decal registration](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/cl_main.c)
and [bounded decal table](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/common/common.h).
The initial FIFO also excludes empty transport-only bodies; their ACK events
are handled separately. Tests cover empty packets around the queued boundary,
raw/BZ2 packet ordering, exact reliable ACK gating and post-boundary handoff.

The same server then supplied a non-empty file-consistency tail after 1117
resource entries. This is a real protocol field, not arbitrary non-zero padding.
The parser now reads the enabled bit, bounded delta-5/absolute-10 resource-list
ordinals, terminator, and exact zero alignment. Invalid/repeated/out-of-range
ordinals and truncated tails cannot publish a partial list.

Requested exact-file responses use `clc_fileconsistency` (7), a positive signed
16-bit body length, list ordinals (not type-local resource indices), actual
MD5 first-four-byte values, and COM_Munge with the full server spawn count.
An owned cancellable worker prepares the complete response while the network
continues pumping. The complete response precedes the original typed spawn
command in one reliable generation, including retransmission. No-request
connections retain the previous spawn bytes. Reads are capped at 16 MiB per
file, 256 MiB total and ten seconds; file identity/size/timestamps and EOF are
checked before a response is published.

The observed decal-index pattern matches
[ReChecker's resource registration](https://github.com/rehlds/rechecker/blob/master/src/resource.cpp):
it prepends `delta.lst` and assigns later checks indices above 4095, which
ReHLDS clamps to the 12-bit maximum on the wire. This explains the metadata
rows without inventing a protocol sentinel. The narrow recognized profile
requires the actual approved game-root `delta.lst` baseline and only admits
subsequent known game-asset paths. For this profile, a verified `not_found`
uses ReChecker's previous-response hash convention to encode absence.
Denied, unsafe, unreadable and unsupported paths are not represented as
missing; no counterfeit clean-file hash is generated.

ReChecker can request parent-directory DLLs and configuration files, as its
[published rules](https://github.com/rehlds/rechecker/blob/master/dist/resources.ini)
demonstrate. Recognizing the protocol does not authorize arbitrary filesystem
reads. Search roots and asset allowlists remain restricted. Error diagnostics
include bounded classifications and an escaped server-supplied request name,
not resolved private local paths, file contents or computed hashes.

Another live attempt exposed `svc_updateuserinfo` (13) racing ahead of the
resource transition. Fully decoded updates are now retained in bounded
player-slot snapshots rather than rejected or discarded. Runtime initialization
merges the initial player batch, transition updates and response updates in
that order; newer identities and vacancies supersede earlier records. Unknown
or malformed service messages still fail at their exact boundary.

Live completion and final verification for this server are recorded below;
the earlier protocol fixes alone do not establish successful map entry.

The final bounded live probe, `live-consistency-profile.log`, passed all 1117
resource rows, the exact post-list boundary at byte 23999, client resource
response transmission and its covering ACK. It also received and retained a
real opcode-13 update while waiting for that ACK. The recognized consistency
list contains 400 requests: 342 parent-component names, one `delta.lst` schema,
zero allowed game-asset requests, 57 other names, and no absolute names.
The first denied request is ordinal 1, type 3, `../demoplayer.dll`.

The client correctly stops with `file_consistency_response_failed / unsafe_name`.
The real `delta.lst` baseline was read inside the approved game environment,
but no complete file-consistency reply or spawn command was sent. There were
zero gameplay command submissions/transmissions, no downloads and no server
configuration changes. **Map entry on this customized server is still blocked**
pending an explicit decision about non-asset file-check scope. Do not advertise
these changes as complete ReChecker or arbitrary customized-server support.

Final Release verification for this change:

- Resource/transition/consistency suite: 256 cases, 255 passed, one existing
  file-symlink case skipped because symlink creation is unavailable;
  1,400,592 assertions passed.
- Early-signon suite: 183 cases / 55,721 assertions passed.
- Public-server runtime/HUD/input suite: 184 cases / 71,016 assertions passed.
- Two-frame null-renderer startup smoke: exit 0.
- `git diff --check` passed (pre-existing line-ending warnings only).

Updated `build/bin/Release/hlclient.exe` SHA-256:
`31060B6C3C55495446F283C24BB4DF0FEB15540738696B08F45B23E50A298137`.
The launcher was not changed by this customized-server fix. No commit, push,
workspace cleanup or remote server configuration change was performed.

### Explicitly approved installation checks (follow-up)

The user subsequently approved checksum/existence checks only inside
`D:\DEV\HLCLIENT-RESEARCH\Half-Life` for `45.83.244.193:27020`. This is recorded
in a separate local `HLClientEngine/installation-file-checks.json` permission
file, not in server data or the existing launcher settings schema. Its exact
four-field schema (`hlclient.installation-file-checks.v1`, endpoint, asset_root,
game_directory) is checked on every connection/reconnect. Other endpoints,
installations and game directories retain the game-asset-only default. Missing,
oversized, malformed, duplicate-field or extended permission files deny access.
Removing this local permission file revokes the grant.

Only the recognized ReChecker profile can use the approved reader. A request
is relative to the exact selected GAME root or has one leading `../` to the
already pinned installation BASE. There is no Valve fallback, path folding,
native/UNC/device/ADS access or second parent step. The reader rejects
intermediate/final reparse points, multiply linked files, ambiguous Windows
names, changed root/file identities, and open/read races. Access denied and
failed reads never become a fabricated missing-file result. Real file checks
retain the existing byte, time and cancellation bounds. Only checksums and
the protocol's verified-absence representation are sent; no file contents,
downloads, remote commands or configuration writes are enabled.

The dedicated installation suite passed 1,868 assertions (31 passed cases,
two skipped because Windows file/directory symlink creation is unavailable).
The first approved live attempt passed all requested consistency reads and
queued the complete consistency-plus-spawn response. It exposed a separate
multi-packet reliable-send issue (`spawn_request_transmit_mismatch`); this
attempt alone is not evidence of successful map entry.

The reliable-send observer now validates each actual 1024-byte transport slice
against the complete consistency-plus-spawn bytes instead of comparing every
fragment to the whole response. Generation remains stable, while the first
sequence, toggle and retry count are tracked per fragment. Final ACK processing
uses an owning completion witness retained by the driver before session commit
clears the transfer. This preserves exact byte/generation/latest-send checks
even when several fragments, or the entire transfer, complete inside one
driver update. Intermediate ACKs cannot acknowledge spawn. The completion
witness is bounded by existing outgoing-transfer limits and is never emitted
in logs or trace callbacks.

Regression coverage includes a 400-row response larger than two transport
chunks, exact reconstruction, first/final-fragment retransmission, stale
pre-retry ACK rejection, single-update completion, and direct/fragmented
server ACK carriers. The small/no-consistency path remains covered separately.

The next live probe (`live-approved-fragment-fix.log`) confirmed the complete
spawn transmission and covering ACK, 255 baseline entities, runtime publication,
and transmitted/acknowledged `sendents`. It then reached a separate unsupported
standard HUD text notification: `svc_temp_entity` (23), `TE_TEXTMESSAGE` (29).

TE_TEXTMESSAGE now follows the pinned Valve SDK `dlls/util.cpp`
`UTIL_HudMessage` writer: channel byte, two signed fixed-point coordinates,
effect byte, two RGBA colors, three unsigned fixed-point time fields, an extra
time word only for effect 2, and a bounded NUL string of at most 511 non-NUL
bytes. The channel is present in the writer even though `common/const.h`'s
comment omits it. See the [Valve SDK definition](https://github.com/ValveSoftware/halflife/blob/master/common/const.h)
and [Xash TE_TEXTMESSAGE dispatch](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/cl_tent.c).
Unknown subtypes and malformed/truncated text still fail at the exact cursor.
Only typed scalar metadata and text length are retained; the text is neither
executed nor printed, and styled HUD text rendering is explicitly deferred.
Tests cover every truncated prefix, optional effect-2 timing, the length bound,
following-opcode boundaries, forged metadata rejection, and atomic replay.

The final bounded live probe (`live-approved-textmessage.log`) exited 0 with
`terminal_reason=live_runtime_state_ready` on `maps/crossfire.bsp`. The complete
consistency-plus-spawn generation and `sendents` were both transmitted and
acknowledged exactly once. It received 255 baselines and 194 service payloads,
applied 190 runtime records, and published revision 191 with a 2,013 ms stable
operational interval. Client data and packet entities were observed. There were
zero gameplay commands, no downloads, no server configuration changes and no
file access granted outside the selected installation. This establishes a
successful bounded protocol/runtime connection, **not** a rendered or extended
manual gameplay session, nor support for every customized-server feature.

Release `build/bin/Release/hlclient.exe` SHA-256 for this successful probe:
`1FCF4C47D593FC85929C8B6B594502D18809162672A7D2D8845287C9574AA891`.
The existing launcher remains unchanged and launches this updated client.

Final Release verification for the approved follow-up:

- Installation permission/resolver/consistency: 31 passed cases, two symlink
  capability skips; 1,868 assertions passed.
- Resource/transition/runtime continuation: 260 passed cases, one symlink
  capability skip; 1,401,331 assertions passed.
- Early signon/netchan: 183 cases / 55,860 assertions passed.
- Public-server runtime/HUD/input, including TE29: 188 cases / 72,508 assertions
  passed.
- Two-frame null-renderer startup: exit 0; the bounded live probe: exit 0.
- All five requested Release build targets completed successfully. No commit,
  push, cleanup, launcher-settings rewrite or server modification was performed.

### Generic live asset preparation (rendered follow-up)

User run `ea239a96003b93fe44f7abbac6727d27` reached local asset preparation,
then failed with `captured/local resource size mismatch at model slot 1`.
That check came from the offline replay contract and was also applied to
live world BSP, Studio models and sprites. A successful null-renderer
protocol probe did not exercise this preparation path.

`nDownloadSize` is transfer metadata, not identity of an installed asset.
The [Xash installed-file check](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/cl_custom.c)
deliberately does not require existing game-file sizes to equal the server's
download size. The live caller now explicitly selects `live_standard_hl`,
independent of the endpoint and camera policy. Advertised size differences
are counted as `size_mismatches`, not fatal admission failures. Recorded
offline replay still defaults to its strict `recorded_exact_size` contract.

Map identity is checked separately using the actual already-approved BSP
bytes. The algorithm follows [ReHLDS CRC_MapFile](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/crc.cpp)
and [Xash CRC32_MapFile](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/common/mod_bmodel.c):
initialize to `0xffffffff`, process lumps 1 through 14 in directory order,
and do not apply a final complement. Entity text, header offsets and padding
are not part of the multiplayer map checksum. Thus entity-only map edits may
change the file size without making geometry incompatible. Server-info CRC
is already decoded from its wire representation. Multiplayer CRC zero is
not a wildcard; stock singleplayer uses its distinct zero convention.

The CRC reader validates BSP30 version, bounded source size, signed lump
ranges, header exclusion and nonoverlap; the existing importer still checks
the complete geometry. Source-handle identity, actual local-file size/read
checks, rooted access, cancellation and server-requested consistency checks
remain intact. Failure diagnostics distinguish `map_crc_mismatch` from
`map_crc_invalid_bsp`; mismatch context contains only numeric CRC values and
a bounded escaped virtual map name, never a native path or file contents.

Non-bindable decal-index-4095 checker metadata is omitted from ordinary
missing-render-asset diagnostics. It remains in the protocol resource list
and consistency path. These rows can no longer consume all 32 diagnostic
slots before a genuine missing model is reported.

The change has no address-based branches. The existing endpoint-scoped
permission for installation DLL/config checks remains a separate privacy
grant; compatibility fixes do not silently authorize other servers to read
those files. No download mechanism or server modification was added.

Verification of this rendered follow-up:

- Release client and `hlclient_b080_cpu_tests` build successfully. Seven new
  focused cases pass 679 assertions, including independent literal BSP CRC,
  assigned-slot wire decoding, entity-only changes, all three asset formats,
  unchanged offline strictness, malformed BSP and real geometry mismatch.
- The complete focused asset/history executable initially passes 48,365
  assertions in 89 cases; 12 installed-asset cases skip without the opt-in
  test environment variable. Project-owned GPU cases execute successfully.
  Repeating with `HLCLIENT_LOCAL_GAME_ROOT` set only for the test process to
  the already-approved research installation passes **all 101 cases and
  254,113 assertions**, including installed-asset GPU/import/read-only checks,
  with no skipped cases (`test-live-resource-installed.log`).
- A 20-second **OpenGL** run on `45.83.244.193:27020` exits 0 with
  `size_matches=91`, `size_mismatches=1`, `world_crc_verified=1`,
  `assets_installed=1`, `renderer_resources_presented=1` and 1,892 successful
  presentations. World, Studio and sprite draw counters are nonzero;
  `primary_error=none`, `gl_errors=0`. The timed disconnect is confirmed by
  the server. Evidence: `live-rendered-resource-policy.log`.
- The same executable then completes a 15-second OpenGL control on
  `2.28.69.179:27015`, also on Crossfire, with `size_matches=87`,
  `size_mismatches=0`, `world_crc_verified=1`, 1,382 successful presentations,
  no primary/GL error and acknowledged disconnect. Evidence:
  `live-rendered-public-resource-control.log`.

Both logs are under `manual-artifacts/task-records/custom-server-signon-20261018`.
These are bounded rendered map-entry controls, not manual movement/weapon
acceptance or proof of every custom-server feature. Existing optional
unsupported sentence/custom presentation counters are not silently claimed
as implemented. No scripted movement, firing or server console commands were
sent. The normal live loop did send its idle client commands after activation.

Release client SHA-256:
`D2A70F9CB83D6E8E729A44B988A29B4837F31C3B8034BE1CF6FD70D8D3AF4979`.
`HLClientLauncher.exe` is unchanged and uses this updated `hlclient.exe`.

## Silent connect response: fresh console attempts — 2026-10-19

User run `6874caba52879e97374ad5b22a9da617` targeted `5.231.120.14:27017`
with client SHA-256
`D330C784622E0F5A70D1C51FFA7FBF6EEB1758D5FB22E097EAC42C84B2F0B797`.
Challenge was accepted at 837 ms, Steam material acquisition succeeded, and
one 489-byte connect was sent at 1559 ms. The response wait expired at
6565 ms, with no target response delivered to its parser. There was no
observed Steam rejection or malformed recognized response. The user confirmed
that original Half-Life connects. This does not prove whether a packet was
lost, filtered or delayed; foreign-source traffic can be discarded by the
existing endpoint capability before the response handler sees it.

The former one-shot normal application flow stopped after a single five-second
response wait. Xash3D's connection loop performs bounded fresh challenge
retries instead: [CL_CheckForResend](https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/cl_main.c#L1293).
The existing strict ACCEPT/general-REJECT grammars are not relaxed in response
to a silent peer. A separately confirmed standard password refusal is now
recognized as the exact `8BADPASSWORD` plus one NUL, with no LF. It produces
the existing typed rejection, not a retryable timeout. Independent codec/wait
tests cover exact ownership, all truncated prefixes, mutated markers, trailing
or embedded NUL, LF/CR and wrong endpoints. This was a general boundary gap,
not the observed cause on this endpoint (its unrelated-response count was zero).

The normal interactive Steam console now admits a 10-second response wait and
at most **three complete application attempts**, with a **one-second
nonblocking pause** between failures. This is a shared launcher/standalone
policy, with no server-address branch. It applies only to unlimited
keyboard/mouse live-visual sessions, not finite/scripted sessions, file
authentication or explicit M1/M2 protocol probes. Longer configured response
waits and invalid/zero configurations are preserved rather than repaired.

A typed retry cause is emitted only for a nonzero live runner result whose
actual coordinator state is `connect_response_timed_out`. Text from the server,
an exception or a diagnostic log cannot authorize retry. Rejection, protocol,
network/send, authentication, cancellation and durable/native-close failures
are not automatically retried. Local `connect`/`retry` overrides and resets the
chain; `disconnect`/shutdown cancels it, including during the pause. Invalid,
backwards or overflowing retry clocks also cancel rather than spin.

Every attempt finishes and persists its original result before another starts.
It uses the existing distinct console-attempt run ID and receipt directory.
The old `run()` stack fully unwinds: socket, challenge, provider, authentication
session and ticket are new for the next attempt. Within each attempt challenge
and connect still share one socket; across attempts the socket/source port may
change, so a delayed ACCEPT cannot become an unrelated fresh challenge reply.
No old authentication bytes are retained or retransmitted. No new packet
capture, ticket/identity logging, server mutation or broader file-check grant
is added. Existing per-stage bounds and the three-attempt cap apply; this is
**not** a claim of a single aggregate 32-second handshake deadline.

Rollback/evidence are under
`manual-artifacts/task-records/connect-retry-20261019/`. Its `prechange.json`
covers 1,498 SHA-verified ordinary source/binary files, preserving the prior
camera/ground fixes and all other dirty changes. No Git mutation, user-settings
change or cleanup was performed. No agent live/Steam/HLDS session was started
for this follow-up; compatibility on the reported endpoint remains a subsequent
manual check, not a result inferred from offline fixtures.

### Fresh-attempt verification

The first focused Release run passed 267 assertions in five lifecycle cases;
it preceded the explicit launcher-positive section and final password-boundary
tests, so it is retained as an intermediate run, not the final gate. The
following gates use the final source set. No failed test was suppressed or
reclassified as a pass; unavailable capabilities remain skips.

| Gate | Result | Evidence in the task directory |
| --- | --- | --- |
| All normal Release targets | PASS | `release-complete-build.log` |
| Retry/lifecycle/late-ACCEPT/password boundary cases | **9 cases, 519 assertions passed** | `retry-release-final.log` |
| Full Release C++ suite | **4,015 cases: 3,940 passed, 75 skipped; 4,756,173 assertions passed** | `full-release-final.log` |
| Debug normal client, console tests and Steam/protocol tests build | PASS | `debug-final-build.log` |
| Complete Debug console/application suite | **283 cases: 267 passed, 16 skipped; 67,092 assertions passed** | `console-debug-final.log` |
| Complete Debug Steam/protocol suite | **145 cases, 4,640 assertions passed** | `steam-debug-final.log` |
| Launcher offline UI, normal Null, three built-in replays and architecture guard | **6/6 passed** | `offline-smokes-final.log` |
| `git diff --check` | PASS; existing line-ending warnings only | `final-diff-check.log` |

The current normal Release `hlclient.exe` is **5,477,888 bytes**, SHA-256
`FAF4E7010155A65236422A00A3BBDD7798A9BA40A402E178191A5E51B6F2FD9E`.
Launcher SHA-256 remains
`5F3A4BDDEE4DCE1D051CC418BF876405369A83016EFFFE3E12D8528F4142466C`
and SDL3 remains
`AD833F09FAD8D99EADFDB1A765A76F9F20872A3AB6FADBEF648F09334F2FAC87`.
`final-worktree-receipt.json` binds source/binary changes to the verified local
dirty-worktree snapshot rather than the historical Git HEAD. Original user
authentication-report SHA-256 remains
`11266698B60E10F1D8806E0D54597E00F8EC8A4B01048AD89ACE2A9D924AB7F3`;
launcher settings retain their 03:34:57 local write time.

Independent code reviews covered typed cause ownership, full teardown before
fresh attempts, explicit command priority, timer bounds, launcher/standalone
scope and exact password-rejection bounds. Offline fixture timing collected
alongside builds is not a gameplay FPS/overhead or stock parity claim. Live
endpoint confirmation and new-code ASan verification remain **not_run**; the
earlier unavailable ASan loader result is not an ASan pass.

## Early `svc_director` during sign-on — 2026-10-19

User run `3b3bfa018711f64409c8269c53e71974`, using the Release client
`FAF4E7010155A65236422A00A3BBDD7798A9BA40A402E178191A5E51B6F2FD9E`,
performed its bounded fresh attempts. The first two timed out. Attempt
`90fd0d55057f8d1c6061a52ae2b153a9` received ACCEPT 34 ms after sending connect;
authentication/challenge/connect were therefore not the terminal failure.
Its durable diagnostic identifies `initial_signon`, source sequence 1,
`unsupported_service_opcode`, opcode **51 at offset 0**. No serverinfo/map
entry was observed. Cascading resource-transition wrapper messages do not
change that initial-stream ownership.

Opcode 51 is standard `svc_director`. The local primary ReHLDS reference
`D:/DEV/CPP/HL-Engine/out/reference/ReHLDS/rehlds/HLTV/common/DirectorCmd.cpp`,
`DirectorCmd::WriteToStream`, writes the opcode, a single unsigned byte
containing the complete body length, then that exact body. The existing
runtime decoder already understood this envelope. The early sign-on decoder
and pre-resource stream now share its bounded framing primitive, and the two
later resource-control admission gates allow the same inert decoded record.
No server-address branch, byte scanning, guessed body length or general
unknown-opcode bypass is added. Director commands remain unimplemented/inert;
this is envelope compatibility, not an HLTV Director feature claim.

The helper accepts N=0 through 255, consumes only length plus body and leaves
the following opcode untouched. On truncation its cursor is unchanged. This
also makes the runtime malformed-Director diagnostic point at the unconsumed
length byte, rather than after a partially consumed length. Malformed bodies
still fail, publish no partial state and authorize no action. Required
serverinfo/extra-info/delta/resource boundaries, source/ACK provenance,
duplicate suppression, ordered queues and all existing bounds remain intact.
Opaque body bytes cannot execute stufftext, read a cvar or become a boundary.

Rollback and verification evidence are under
`manual-artifacts/task-records/director-signon-20261019/`. Its pre-change
snapshot contains 1,498 SHA-verified ordinary source/binary files, including
the preceding retry, camera and ground-state fixes. Original user reports,
launcher settings and servers are read-only. No agent live/Steam/HLDS session
is started for this fix. The body was not captured: independently authored
fixtures exercise the wire grammar, not the exact unknown Director command
from this user's session. No controlled pre-fix test run is claimed.

The forward audit also identified strict legacy milestone profiles not changed
by this patch: Director interleaving within the contiguous delta-schema batch,
between delta/movevars or movevars/userinfo, or in the exact-end first-userinfo
batch is not newly supported. There is no such cursor in this session's
evidence. A hypothetical Director tail inside the resource-list bitstream is
likewise not accepted by guessing its alignment. After baseline handoff the
existing runtime decoder already frames Director records. These limits are
not presented as complete compatibility with every server or HLTV feature.

Original attempt report SHA-256 values:

- `external-authentication.json`:
  `766D688A38B130506C1AAF941A3B9F7393F70642983DBDE300CA531A90AE3DAB`.
- `external-diagnostics.json`:
  `E2D4E470AEC8EE3E60F37A276F2A9DBEE0F4D83199B6867B49504C55176FFB48`.

### Director framing verification

All new cases are tagged `[director-signon]`. The byte-reader case tests every
wire length 0..255 and every truncated prefix at a nonzero cursor. Stream and
real-stage fixtures also cover literal opcode 51 at offset 0 of native source
sequence 1, control-only carriers before ACK, ordered continuation, duplicates,
source metadata, malformed envelopes, genuine unsupported following opcodes,
opaque embedded boundary/command/query bytes and existing capacity limits.

| Release gate | Result | Evidence in the task directory |
| --- | --- | --- |
| Normal client build | PASS | `release-client-build.log` |
| All three focused targets build | PASS | `release-focused-build.log` |
| All normal Release targets build | PASS | `release-complete-build.log` |
| New early-sign-on/byte-reader cases | 10 cases, 142,867 assertions passed | `early-release-director.log` |
| New resource-control cases | 7 cases, 481 assertions passed | `resource-release-director.log` |
| New runtime-framing cases | 3 cases, 3,267 assertions passed | `runtime-release-director.log` |
| Complete early-sign-on target | 202 cases, 198,796 assertions passed | `early-release-complete.log` |
| Complete resource target | 275 cases: 274 passed, 1 skipped; 1,405,114 assertions passed | `resource-release-complete.log` |
| Complete public-server control target | 193 cases, 75,914 assertions passed | `runtime-release-complete.log` |
| Full normal C++ suite | 4,035 cases: 3,960 passed, 75 skipped; 4,902,750 assertions passed | `full-release-final.log` |
| Offline launcher, architecture guard, Null and three built-in replays | 6/6 passed | `offline-smokes-final.log` |
| Final `git diff --check` | PASS; existing line-ending advisories only | `final-diff-check.log` |

The resource-suite skip is the unavailable file-symlink capability; none of the
20 new cases is skipped. The full suite retains 75 unavailable/opt-in skips;
`HLCLIENT_RUN_WFP_CAPABILITY_TEST=0` keeps the privileged WFP capability probe
disabled. These tests do not start real Steam or HLDS sessions.
Concurrent offline build/test timings are not gameplay FPS/overhead evidence.
The existing ASan loader-unavailable result remains unavailable, not a pass;
new-code ASan and new-build live-server acceptance are **not_run**.

The delivered normal Release `hlclient.exe` is 5,479,424 bytes, SHA-256
`B12331B580E3A240E3DE242139BD1AE57D81CF1FB4040616F7BF6B19C4C1BD66`.
Launcher and SDL3 SHA-256 remain respectively
`5F3A4BDDEE4DCE1D051CC418BF876405369A83016EFFFE3E12D8528F4142466C` and
`AD833F09FAD8D99EADFDB1A765A76F9F20872A3AB6FADBEF648F09334F2FAC87`.
The existing launcher starts this updated client. Independent review found no
framing/admission defect and verified 17 expected ordinary-source changes
against the snapshot, with 1,478 other ordinary source files byte-identical
and none missing. No Git mutation, server mutation or cleanup was performed.
`final-worktree-receipt.json` binds the delivered binary to the actual dirty
worktree snapshot, verifies all original baseline files still exist, and
rechecks the original attempt reports and user settings without changing them.

## Initial sign-on event/history capacity — 2026-10-19

User application run `581e6be0ce1960158643422101cc9a1f` first timed out waiting
for a connect response, then attempt `fca33236e90d63067906503cd7ca9e46` passed
that boundary and stopped with the console context `Decoded service batch
exceeds bounded sign-on event/state capacity`. The original durable diagnostic
instead reports the outer `resource_list_stage_failed`/`resource_response`
wrapper and no source cursor. That is missing ownership information, not
evidence of a resource-list wire error. Original report hashes are retained in
the task receipt; the user session is never rewritten.

The failure context uniquely identifies the initial-stage admission check.
Its former default coupled a 32-slot progress event ring to a 32-message total
retained history, while the service decoder permits 64 records per payload.
Even a valid batch can exceed the ring once payload and ACK/boundary events
are counted. Polling between updates also could not fix the separate total
history limit. The report has no batch counts or body capture, so the exact
observed branch (ring versus retained state) is unknown. Independent fixtures
reproduce both defects without claiming captured wire replay.

The initial event ring now defaults to 128 slots, hard cap 256. Retained
history has its own `maximum_retained_messages`, default/hard cap 256, keeping
the previous absolute maximum owning-message bound. One fully decoded owning
batch can wait for consumer space without partial history/event publication,
redecoding, consuming another driver event or performing another network
update. The composite drains internal events between bounded updates and can
resume that candidate exactly once. All payload/string/pre-ACK/driver budgets
remain finite and unchanged. A batch that cannot fit an empty configured ring
or exceeds retained history still fails atomically, with numeric capacity and
validated opcode/cursor/source context. Cancellation/backwards-time cleanup
also releases the pending owner. A standalone consumer that never polls can
remain in bounded backpressure; this is not a network-timeout workaround.

The final atomicity review also repaired an earlier boundary-tail handoff:
returning already-owned following payloads to the same driver can fail under
an explicit small driver-event cap. That all-or-nothing return now occurs
after candidate allocation but before any batch event/trace/count publication,
so a failed tail return cannot expose a partially committed boundary batch.
An independent fixture uses six retained tail carriers with a five-slot driver
to verify this rejection path. No driver/queue hard cap is increased to hide it.

The forward audit reconciled three other *default* budgets with their existing
decoder publication formulas, without changing their hard caps or explicit
small-cap rejection:

| Stage | Previous default | New derived default |
| --- | ---: | ---: |
| Pre-resource | 32 | 64, the default service-message budget |
| Delta descriptions | 32 | 34, 32 schemas plus ready/boundary |
| Movement environment | 64 | 66, 64 controls plus ready/boundary |

User-info's default 64 already fits its default 32 messages plus completion;
the resource-list default already includes its resource/lifecycle overhead.
This is not a promise that every combination of independently configurable
hard maxima fits every event ring: explicit decoder maxima larger than a
configured ring continue to fail, rather than silently widening bounds.
Resource-transition streaming admission and all wire-message grammars are
unchanged. No server address is tested in these policies.

The always-on initial trace now includes its typed stage failure code even
when no parser error exists. Durable capacity reports retain
`initial_signon`/`capacity`, `service_event_backpressure` and available validated
numeric provenance through wrapper failures, without storing server text,
body previews or authentication material. Parser errors keep their existing
protocol classification.

Evidence and rollback are under
`manual-artifacts/task-records/signon-capacity-20261019/`; `prechange.json`
contains 1,498 SHA-verified ordinary source/binary files. Prior Director,
connect-retry, camera/ground and all other dirty changes remain preserved.
No agent live/Steam/HLDS session, server mutation, Git mutation or cleanup is
performed for this fix. New-build live-server acceptance remains **not_run**.

### Capacity-fix verification and delivered binary

The initial focused run passed eight new cases/1,009 assertions, all 210 early
cases/199,805 assertions, the resource target and six smokes. Those intermediate
logs are retained. The following final gates include the additional atomic-tail
repair and regression, rather than reusing the earlier pass as final evidence.

| Final Release gate | Result | Evidence in the task directory |
| --- | --- | --- |
| Normal client plus both affected focused targets build | PASS | `release-final-build.log` |
| `[signon-capacity]` | 9 cases, 1,137 assertions passed | `early-release-capacity-final.log` |
| Complete early-sign-on target | 211 cases, 199,933 assertions passed | `early-release-complete-final.log` |
| Complete resource target | 276 cases: 275 passed, 1 skipped | `resource-release-complete-final.log` |
| Offline launcher, architecture guard, Null and three built-in replays | 6/6 passed | `offline-smokes-after-atomic.log` |
| `git diff --check` | PASS; existing line-ending advisories only | `final-diff-check.log` |

The resource skip is the unavailable file-symlink capability. All nine new
cases execute. Fixtures cover both first-carrier sizes (40/62 Director controls
plus text/boundary), a drained ring with more than 32 retained messages, a full
ring retaining one decoded batch, ordered pre-ACK/duplicate handling, exact
source/receive metadata, no driver mutation while blocked, explicit permanent
caps, cancellation/backwards-time cleanup and failed retained-tail handoff.
Existing malformed/unsupported message, small-cap, timeout and resource tests
remain enabled. WFP capability probing remains disabled with
`HLCLIENT_RUN_WFP_CAPABILITY_TEST=0`; no capability skip is converted into a pass.

These are focused Release verification gates. The full monolithic suite,
Debug, new-code ASan and live-server gates are **not_run for this patch**; the
earlier full-suite result above belongs to the preceding Director binary.
Concurrent offline timings are not gameplay FPS/overhead measurements.

The delivered normal Release `hlclient.exe` is 5,483,520 bytes, SHA-256
`E27985A8E7173E249E1F934B9672E484C811137F93FC9A5CFD2384AEE57EA7AB`.
Launcher and SDL3 remain unchanged and the same launcher starts this client.
`final-worktree-receipt.json` binds this dirty-worktree source/binary delta to
the verified rollback snapshot and rechecks the unchanged original attempt
reports and user settings. No user process is stopped or launcher executable
overwritten by this task.
