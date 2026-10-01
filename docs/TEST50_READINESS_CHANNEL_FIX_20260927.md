# Test-50 helper readiness console channel correction

Result: `test50_readiness_channel_corrected_offline_verified`.
New live sessions=0; Steam initialization/WFP/capture=not_run;
staging/commit/push=none. Corrected-build manual validation=pending.

## Bound failure and limits

Run `3c79bb308947422494d4cc5c7f8261f0`, crossfire/reference/TestStartHealth=50,
server PID 43364. `attribution=user_report`: the supplied terminal report
stopped after server readiness, with client PID 0 and client stages not reached.
The retained native report names
`test-start-health-helper-startup-not-confirmed`; no client process or client
exit code exists for this run. The server log confirms Metamod loaded the
owned `HLClient owned start health` plugin, but contains neither readiness
marker. The final same-run wrapper reports `local_server_ready_client_blocked`,
owned-process cleanup=exact, restoration=exact, cleanup errors=0.
Historical reports and raw/redacted run files remain unchanged.

The helper used `AlertMessage(at_console)` for both readiness markers.
The pinned [HLSDK engine notes](https://metamod-p.sourceforge.net/doc/html/engine_notes.html#AlertMessage)
document that this channel requires `developer`; `at_logged` also depends on
multiplayer state, which need not exist at early plugin attach. The run did
not retain the developer cvar value. The deterministic fake-engine reproduction
proves this code defect with developer=0, rather than claiming an observed cvar
value or proof of successful first-spawn health application.

## Change

`test_server/start_health/plugin.cpp` emits the same two fixed, newline-ended
readiness strings via `pfnServerPrint`. This engine API is available at attach
and prints to the owned server console without those alert-channel filters.
`configured=false` remains explicit; incomplete configuration can still retry.
The native host still requires both attach and configured ServerActivate
markers before launching the client, with its existing bounded deadline.
No readiness bypass, developer override, timeout increase or new launch flag.

Health application/rejection evidence remains on `at_logged` after multiplayer
startup. The single post-ClientPutInServer health write, target checks,
max-health preservation, run-bound evidence, and receiving-client fresh-50
confirmation are unchanged. No core, HalfLifeClientModule, client, native host,
launcher, profile/restoration or wire code changed in this correction.

`test_server/start_health/offline_test.cpp` now models developer filtering and
early non-multiplayer alert suppression, supplies ServerPrint, and tests all
14 existing scenarios with developer=0 and developer=1. It checks attach
before multiplayer startup, explicit invalid configuration, incomplete then
valid configuration, and exactly one successful application record across
duplicate callbacks/healing/respawn/reconnect.

## Preservation and verification

Branch `codex/stock-runtime-campaign-5e48b7c1`, HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66` unchanged. Before editing, the existing
source-copy/hash inventory mechanism preserved 166 dirty/untracked project
source files in
`manual-artifacts/task-records/test50-readiness-channel-prechange/source`.
All saved hashes remain intact; only the two helper source/test counterparts
changed. This report is an additional ordinary project file.

- Prechange Debug/Release DLL tests passed 182 checks each. The strengthened
  Debug test, built alone and run against the old unchanged DLL, failed at
  startup attach evidence as expected. This demonstrates regression detection.
- Incremental standalone helper Debug/Release builds and DLL/ABI fake-engine
  tests passed 423 checks each. Existing pinned local dependencies were reused.
  The normal preparation script refreshed `verified-components.json`, and
  source/helper hash verification passed.
- Existing native startup-argument/readiness/redaction and functional-log
  contracts passed in Debug and Release, with stock processes started=0.
- Independent PowerShell profile/evidence/failure fixtures and all 12 no-stock
  launcher cases passed, preserving reference/off and damage-respawn-check.
- Actual launcher `-CheckOnly` passed from system32 for reference/TestStartHealth
  50 and ordinary off. Neither invocation started the managed runner.
- `git diff --check` and whitespace checks against the saved helper sources
  passed. The standalone helper is outside engine/core targets; core-only,
  ASan and OpenGL suites were not rerun for this two-file helper-only change.
  Previously verified client/native binaries were not rebuilt or replaced.

Release helper SHA-256:
`0CCAB02C9931BEAE6ECE6DF7BEA41D088C7A82385CAFFEDD85B262895F48DA1F`.
Unchanged Release client SHA-256:
`0DA3D959ABD294AD386F459568D078936AF6941868345C96C5209D394B043CC3`.
Unchanged Release native host SHA-256:
`FA1E87C74B1A3B44FB614F8954E75FDC26CADA8E3261D52B24B561F8EEC09BC8`.

## Manual handoff

Actual owned-server pipe delivery, configured startup, fresh receiving-client
50 HP and charger healing remain pending the next user-run session. Offline
checks are not live confirmation. The earlier scheduler-stall correction and
historical user confirmations remain intact.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```
