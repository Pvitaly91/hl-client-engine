# E28.1 — graceful disconnect and durable session receipts

Task: `HLC-E28-1-GRACEFUL-DISCONNECT-AND-DURABLE-SESSION-RECEIPTS`.
Normal VS2022/Win32 application, static engine/game boundary retained. No E29,
arsenal, server, map, asset, timeout or isolation-policy changes.

## Historical facts, not new manual acceptance

The two audited candidates remain `d4d53289a56b4cc9941fee7e6d325529` and
`b85665337b8f4486a0d4b00b36a59d09`. A/B exited 0/0; both runners failed with
`client-map-entry-not-observed`. Owned cleanup was exact, scoped restoration
was exact with **zero managed files**, and no relay was launched. Whole-tree
restoration was not verified. Historical client SHA was not recorded; the
pre-build current E28 SHA is not retroactively assigned to either run.

Preserved `attribution=user_report`: A survived B exit, B was eventually removed
from world/TAB, A continued gameplay, and peer loop/beam cleanup passed. These
are scoped confirmations, not full E28 manual acceptance. Approximately 70 s
is a user estimate; `historical_timeout_cause=not_established`. The old opcode13
userinfo body remains unavailable and is not an E28.1 blocker/root cause.

## Defects and normal production paths

Before E28.1, SDL quit stopped the application loop before its next session
update. Final `cancel()` cleared the coordinator/driver lifetime without any
client disconnect builder/request. UDP stayed alive until later destruction,
but could no longer send a terminal operation. This was missing protocol work,
not an already-queued packet being discarded. The extracted unchanged
application-close helper was tested first: expected request/update failed,
and the observed old operation was cancellation (see pre-fix log).

The runner's retained text parser could evict the one-time spawn/sendents
transmit/ACK markers. Fresh parsing reset those flags and falsely failed an
otherwise ready long manual session. Exit0 alone remains insufficient.

New path: SDL quit or user duration expiry → stop input/render loop and cancel
uncommitted game actions → application intentional-close helper → coordinator
→ live stage's terminal-only driver access → current netchan and current UDP
socket → bounded terminal send/poll → logical channel cleanup → explicit native
socket close → immutable terminal receipt → normal result → owned runner Job
cleanup/restoration/report. The receipt writer outlives the session.

Force cancel/startup failure, runtime/parser failure, server-initiated disconnect
and runner termination are not relabeled as a successful window close. Existing
driver/stage errors and child exit codes retain priority. Close cannot create
attack/charge/usercmd/weapon actions; terminal pumping does not apply gameplay.
B exit0 does not end A or the shared server/Job. After A closes, B gets a bounded
2 s normal-exit grace before existing owned-session termination.

## Fixed protocol and bounded policy

The GoldSrc-only builder emits exactly 13 bytes:
`03 64 72 6f 70 63 6c 69 65 6e 74 0a 00` (`clc_stringcmd`, `dropclient\n`, NUL).
There is no arbitrary command/CLI payload API. Existing production netchan
serializer, sequences, reliability/fragments and COM_Munge2 are reused; no new
endpoint/socket/handshake is created. Pending reliable data is preserved. If a
full reliable unit cannot coexist with the fixed terminal payload, bounded
support flushes precede a fresh framed terminal packet; no reliable state is
forcibly cleared to make it fit.

Behavioral facts, separately reviewed under the approved reference policy:
[Xash CL_SendDisconnectMessage, e9b6324](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/cl_main.c),
[ReHLDS SV_ParseStringCommand, 6266cd2](https://github.com/dreamstalker/rehlds/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_user.cpp),
[MSG_ReadString](https://github.com/dreamstalker/rehlds/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/common.cpp),
[dropclient registration](https://github.com/dreamstalker/rehlds/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_main.cpp),
[optional server notice](https://github.com/dreamstalker/rehlds/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/host.cpp).
These establish the profile's LF/NUL command/optional response facts, not
stock-binary parity. No implementation bodies were copied or compiled.

Deadline is established once on steady clock: 1500 ms. At most three terminal
attempts, 100 ms spacing, plus at most three reliable support attempts. Duplicate
close and traffic cannot extend it. The application independently fences at
1600 ms/80 turns with 20 ms pacing, and falls back to local cancellation if a
broken adapter fails to terminate. No response is required for bounded exit.

`queued`, OS `successful_send_count`, validated covering transport ACK and exact
server disconnect notice are distinct. `sent_but_unconfirmed` is legitimate,
not server confirmation. Only standalone/repeated typed svc_disconnect control
with permitted padding is recognized while closing; arbitrary server text is
not scanned/executed. ACK must cover an actually sent terminal sequence in the
current channel. Would-block/error, lost first send, stale/foreign ACK and
malformed terminal notice have explicit outcomes. `netchan_closed_at` is logical
cleanup; receipt `transport_closed_*` is stamped only after successful native
`UdpSocket::close()`. Native close is idempotent; late I/O cannot reuse the handle.

## Bounded durable evidence

Neutral `hlclient.session-receipt.v1` journal/codec has no Half-Life dependency.
Identity binds 32-hex run ID, A/B, own PID and process creation FILETIME; the
runner validates its owned handles. Network epoch is the current one-shot
coordinator's single established channel ordinal (1), not a global server ID.
Map epoch is actual validated serverinfo `server_count + 1`, separately from
replay/life generation; identifier is actual normalized `maps/name.bsp`.
Before map observation, network-only evidence cannot provide map readiness.

Connection, sign-on, map committed, world ready and first gameplay are separate
monotonic milestones. Internal auth/send/ACK callbacks supply narrow facts,
independent of diagnostic logging. Service payload received alone cannot stamp
a map. Map requires the owning committed immutable observation, complete
receiving origin and positive publication. World requires imported assets and
presentation; gameplay additionally requires activated input and positive
actual new/transmitted commands/server samples. Terminal visual proof uses the
same existing real render qualifiers. Planned config, log/server text, decoded
prefix/rollback and mere window creation cannot create readiness.

Milestone times are owning-loop receipt observations, not reconstructed earlier
packet times. Relative steady milliseconds are process-local. Optional QPC with
recorded frequency correlates processes; FILETIME is identity only. The A-side
B-removal delay uses B's server-derived receiving entity, same owned run/server,
actual map, terminal identity-validated receipts and matching QPC frequency.
It measures close intent → post-commit absence observation, not delivery time
or server acknowledgement. Unknown/mismatched evidence stays null.

Bounds: 4096 bytes per file, eight generation slots, six immutable progress
snapshots per generation plus one terminal snapshot (49 leaves/role), 32 numeric
peer-removal observations. Approved Windows secure write-new publication pins
directory ancestry, writes/flushed temporary bytes and non-replacing rename.
Only whitelisted leaves are read; reparse/hardlink/size/schema/identity/map/history
and truncation checks fail closed. Natural child exit requires terminal evidence;
an interrupted new generation or missing terminal cannot inherit an old pass.
Publication/cap errors are fatal evidence, not silent stale readiness.

Launch-time immutable manifest records actual client path/SHA before execution,
runner binary/source SHA and effective configuration/roles. A separate owned
process binding records PID/creation. Ending hash mismatch is reported without
rewriting the launch hash. Legacy runs remain `legacy_evidence_unavailable`.
No names, tickets, raw userinfo or packet dumps are retained in these receipts.

Runner final summary separates A/B map/exit/close, sent/ACK/server notice, measured
delay, verdict, cleanup, scoped restoration, managed-file count, whole-tree
verification state and report path. A successful zero-file scoped restoration
does not claim whole-tree restoration. Unknown HLDS/guard exit codes stay unknown
when only Job-contract completion was observed.

## Verification and handoff

Offline result: `graceful_disconnect_and_durable_session_receipts_integrated`.
All Release targets and targeted Debug normal client/tests/runner compiled.
Final source-frozen verification:

| Group | Cases / completed assertions | CTest / skips / failures |
| --- | --- | --- |
| E28.1 Release and Debug, each | 38 / 1926 | 0 skips, 0 failures |
| Full Release direct tests | 3186 / 1777103 | part of 3307 registered entries |
| Full Release overall | separate core 292 / 142940; 120 non-Catch CLI entries | 3251 passed, 56 skipped, 0 failed; 635.80 s |
| Core-only, concrete game OFF | 292 / 142940; E28.1 subset 15 / 1545 | 42 passed, 0 skips; alternate module + architecture guard green |
| Focused GL/PCM/prediction subset | 283 total, 272 passed, 11 skipped / 682512 | 0 failed; project-owned output proofs, not listening |

The first full run retained one 30 s CTest timeout in the existing anonymous
pipe/thread log-owner cancellation case. Its implementation is byte-identical
to preflight; exact stalled API/section is unavailable. Concurrent Debug compile
was context, not a proven cause. Isolated repeat passed 4.20 s; the final full
repeat without a concurrent build passed it in 4.06 s. No deadline/assertion was
weakened, and the initial failed log remains preserved. This timing boundary is
a known limitation rather than a claimed E28.1 production fix.

The first focused run retained two fixture failures (five assertions): an
assumed 1392-byte full reliable body was not full under actual default4096-byte
datagrams. Full body4088 and fragment input5112 now assert their preconditions;
all five original assertions are retained, no production behavior relaxed.
The literal pre-fix close regression failed 1 case/3 of5 assertions before the
implementation, then passes through the same application helper.

Actual base-command `-CheckOnly`, native receipt/peer contracts and complete
no-stock launcher fixture passed. Reference/off and `damage-respawn-check`
contracts remain. The compiled runner source identity matches its actual source
SHA. Git diff check passed (LF/CRLF advisories only); no files staged.

ASan current normal application compiled, but fresh `--help` exits
`0xC0000135` with zero output: runtime unavailable, no sanitizer cases executed,
not pass. No PATH/DLL/global-environment repair was attempted. The 11 focused
skips are explicit installed-resource/device opt-ins absent; full skips also
include unavailable reparse/link/WFP/platform capabilities.

Source snapshot1294 files and Release rollback34 files still match their saved
SHA manifests. Both historical wrappers and the original post-manual JSON match
preflight hashes. Only40 existing source/config/docs/tests files changed for
this task; Half-Life gameplay, renderer and prediction implementation files are
unchanged from snapshot. Exact results/provenance are recorded in
`docs/evidence/GRACEFUL_DISCONNECT_E28_1.json`.

Current normal Release `build/bin/Release/hlclient.exe`, selected by the launcher:
`F8A0050D1DC027B10AE3157EA8BE20CDF284A650D3573F2469C9CD89C846A75C`.
New build: `manual=not_run`; new live/Steam/HLDS/capture sessions=0;
staging/commit/push=none. ASan compilation and runtime capability are separate.

User-only test command:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Checklist: A/B connect; close B with its cross; B close does not hang; A keeps
moving/firing; B is removed from world and TAB by server state; new diagnostics
record the measured delay; close A; final runner/cleanup/restoration summary,
run ID and build SHA exist; no false `client-map-entry-not-observed`. A healthy
local delivered disconnect should remove B promptly, but no universal latency
is guaranteed. No local hide substitutes for server removal; no 14-weapon manual
retest is required for this narrow task.
