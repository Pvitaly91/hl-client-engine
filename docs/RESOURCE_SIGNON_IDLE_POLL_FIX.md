# Sign-on resource-transfer liveness fix

User-authorized implementation following manual run
`1fddd047260a4645a8021bc1d20a7601` on 2026-09-26.
Worktree: D:\DEV\CPP\HLC-steamcfg-5e48b7c1.
HEAD remains ea738a80e5a66a500c245f95b96eed32198e2d66; commit/push=none.

## Observed failure and limits of attribution

The final functional-smoke-wrapper.json reports
local_server_ready_client_blocked, application exit 2, connection accepted,
delta schemas ready, and owned_process_cleanup/restoration_status both exact.
Its protocol_progress confirms sendres queued, transmitted and acknowledged.
Resource transition/list, spawn and runtime publication were not reached.
The redacted client diagnostic reports Resource-transition stage timed out.
Assets/input were never activated; no weapon-presentation acceptance occurred.

The live evidence does not distinguish channel inactivity from incomplete
fragment timeout or prove the exact internal stock-server scheduling path.
No packet capture, raw auth log or new live session was used to infer that.

## Reproduced code defect

The persistent driver previously sent only queued payloads and reliable ACKs.
An ordinary non-reliable header-only sendres ACK removed reliable work but
did not schedule any subsequent TX. The resource stage then waited silently.

Reference scheduling supports this failure shape:
[ReHLDS SV_ReadPackets / SV_SendClientMessages](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/sv_main.cpp)
sets an unspawned client's send-message gate on an admitted client packet,
checks packet pacing and a recent receive window, then transmits.
SV_SendRes_f queues the resource fragment response. This is a reviewed
reference/inference, not proof of the exact stock binary's implementation.
No reference gameplay/engine code was copied.

The deterministic fake-server regression models a resource response queued
behind that fresh-client-packet gate. The unchanged strict configuration
reproduces silence; the opt-in live configuration releases the response and
reaches the opcode-43 boundary after its poll.

## Narrow change

NetchanDriverConfig.idle_poll_interval defaults to zero (disabled).
The existing live compatibility profile explicitly sets 200 ms. It sends an
otherwise-empty packet through the current same-socket codec/session, using
existing minimum-packet padding; no resource/auth bytes or reliable commands
are invented. No new socket, thread or simulation loop.

Only successful transport sends advance the poll clock. ACK/usercmd traffic
therefore suppresses unnecessary polls. Committed contextual commands retain
priority; would-block preserves sequence ownership and cannot emit a second
poll. There is no burst/catch-up schedule. Existing per-update TX limits,
receive inactivity, fragment deadlines and overall live bounds remain.
Own TX does not refresh received-packet activity. Cancel/terminal state stops
polling. Strict/historical stop modes and all timeout values remain unchanged.
Primary sendres/new semantic request queue counts remain one.

## Verification

Release MSVC Win32 build of hlclient and hlclient_tests passed.

- New focused idle-poll regressions: 5 cases / 224 assertions, all passed.
  Includes strict-versus-live fake-server release of queued resource data,
  one new/sendres semantic request, TX pacing, would-block sequence retention,
  contextual command priority, RX during TX, inactivity and cancellation.
- Wider regression filter: 422 cases, 421 passed, 1 capability skip;
  all 150282 assertions passed. The skip is the existing file-symlink test
  in test_resource_transition_udp_integration.cpp (symlinks unavailable in
  this execution context), not a skipped new liveness test.
  Covers sign-on/resource/reliable fragments, reference transmission and
  prediction/movement, input safety and deterministic B1 presentation.
- Actual OpenGL owned Studio fixture: 1 case / 236 assertions, passed.
- Read-only local Glock/crowbar MDL OpenGL: 1 case / 260 assertions, passed.
  Neither OpenGL gate skipped.
- Offline native functional-log-observation and PowerShell failure-retention
  gates passed. Neither starts stock processes or capture.
- git diff --check passed; staged diff empty; HEAD unchanged.

Reports: manual-artifacts/task-records/resource-fix-focused-tests.txt,
resource-fix-regression-tests.txt, resource-fix-owned-opengl.txt,
resource-fix-local-mdl-opengl.txt, resource-fix-native-parser.txt,
resource-fix-wrapper-parser.txt.

No automatic stock/client/manual/live retry was authorized or launched.
Scoped pre-fix dirty-file snapshots were copied and hash-verified under
manual-artifacts/task-records/resource-timeout-fix-prechange.
Existing B1 source/tests/docs and historical run artifacts were preserved.

The liveness defect is implemented; resolution of this user's exact stock
run still requires a separately authorized live/manual check. Do not claim
live success from fake-server tests.

Manual launch command remains unchanged, in Administrator PowerShell 7:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Do not mistake this command for a live check performed in this fix turn.
The user's previous manual run counts conservatively as session 1/2 of the
B1 managed budget (not an accepted scripted presentation run). This fix
turn consumed zero additional sessions; no budget renewal or blind retry.
manual_weapon_presentation_prediction_validation remains not_run: the user's
failed launch never reached a weapon scene.

## SHA-256 after build

```text
build/bin/Release/hlclient.exe
79088CFDB9AEBD9681E97624B71FACA6BE1F0C8CFDD3016D8EE13C0424649800
build/bin/Release/hlclient_tests.exe
B0E82415F8C9F028FE09E18E8F6DAE360CE06DBDDBD45FE04FAAA2BCF14FB039
src/goldsrc/netchan_driver.cpp
897882808EAFD6C2BD8BDDB21AFBC5FCAD481C45210E306BCAB1CD168F51C633
include/hlclient/goldsrc/netchan_driver.hpp
EF8D52BD3E380182D9457DBE83815E98C2FC1EEE8FC4B70D416EFDEEFB7E195A
src/goldsrc/live_runtime_stage.cpp
CD20E91A9440EB98CB141F0F822BB5322E867245D341B7284EFE1470EFD471E2
tests/test_netchan_driver.cpp
16CC2FE6369DED04E418BFCEC825950B0CAEF186B88607905975550517F45733
tests/test_resource_transition_stage.cpp
AB58C42D4781283C920209E248B7DCDFD5E50497F53300D426B8A4A0312960D3
tests/test_resource_client_response_stage.cpp
1E2F8D8D6909B89FD78FC7D6725C347041C929ADC9923BC008F377A2EC18C316
resource-fix-focused-tests.txt
6CDE3976A23FF5D87C3F28CC423D7A7E81B17276F863A43E8F55920FD36A5825
resource-fix-regression-tests.txt
163E5245EFD7D356FAC80BC0791E5BE0B947B90CA5F3236A4A77F0304EB8DCEF
```
