# Manual stall recovery and test-50 startup correction

Result: `manual_stall_test50_corrective_offline_verified`.
New live sessions=0; commit/push=none. New-build manual validation=not_run.

## Bound failure evidence

Run `fdae886bab084ace9bb4d71e339cb377`, crossfire/reference/TestStartHealth=50,
server PID 22856/client PID 43624. `attribution=user_report`: the client opened,
HUD showed 100 HP, then the client closed. The final wrapper confirms exact
owned-process cleanup and research restoration, zero cleanup errors.
Historical preparation reports and run logs are not rewritten.

Retained client log: `usercmd_scheduler_failed` / `lag_limit_exceeded`, 20-ms
interval, 19 due commands, cap 8. Longest frame=601.024 ms, maximum update
gap=601.158 ms. Steam initialized, connection accepted, entered game observed.
Scripted coverage was not evaluated; coverage is not the exit-2 cause. The
operation causing the maximum delay is unknown; aggregate maxima do not prove
that the longest frame was exactly the fatal frame. Exit codes are not remapped.

Server-derived observations contain health=100; no retained helper application
success exists. 50 HP/max-health therefore are not confirmed. The original
helper-load/configuration failure remains unresolved: old redaction omitted
startup stages/rejections. An absent retained banner does not prove no loading.

## Production corrections

Generic scheduler has explicit `GoldSrcUserCmdLagPolicy`, strict by default.
Only manual keyboard-mouse hosting selects `discard_unsampled_wall_time`.
If catch-up exceeds eight samples, advance to the latest slot on the original
20-ms grid and emit one neutral 20-ms command. Earlier wall-clock slots were
never sampled and are not synthesized. Command identities stay contiguous;
Netchan, history, generation and duration remainder are retained. Clear the
pending press latch on recovery; fresh input resumes on the following slot.
No giant physics step, extra attack/Use, reset, or raised catch-up cap.
Scripted scheduling and allocation/backwards-time/overflow/exhaustion errors
remain fail-closed. `live_visual_scheduler` retains `stall_recoveries` and
`discarded_wall_time_samples`.

This does not recover lost wall-clock simulation or remove the underlying
stall. There is a timestamp gap with one fixed-duration neutral command;
existing server anchors and H3/H4 reconciliation remain authoritative. No
movement kernel, collision, game policy or presentation rules are rewritten.
The neutral release notification remains in the existing presentation stream.

Optional test-server profile now passes a run-scoped `-dll` selector as well
as the temporary liblist selector, and provides the documented default
`valve/addons/metamod/config.ini` alongside the scoped config. Configuration
therefore exists even if loading precedes `+localinfo` execution. Existing
default configuration/reparse ancestors are rejected before mutation. All
new files remain in the existing guarded restoration tree, with no global
allowlist/isolation change or replacement of original executable/game/map bytes.

Fixed `meta require HLC50` rejects an absent plugin. Before creating the client,
native host requires attach and configured ServerActivate markers from the
owned server's bounded log; the pipe-reader handoff has a bounded wait. These
are not health-success evidence. Confirmation still requires exactly one
run-bound application record AND fresh receiving-client health=50.

The helper retries incomplete configuration only on bounded lifecycle callbacks;
only successful configuration is immutable. Its sole health write remains
ClientPutInServer-post, once for the exact accepted loopback player/edict serial.
No frame/think/respawn health writer, fake HUD, local healing/damage command,
or max-health modification. Startup/rejection and pending fresh 100 markers
now survive redaction/evidence extraction; authentication stays redacted.

Contracts: [Metamod startup/configuration](https://metamod-p.sourceforge.net/doc/html/metamod.html),
[require contract](https://metamod-p.sourceforge.net/doc/html/release_notes.html).
The compatible selector is visible in [ReHLDS source](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/sys_dll.cpp);
this is not proof that the installed stock binary executed it. No ReHLDS or
replacement game DLL is installed. Real addon loading remains manual-pending.

## Preservation and checks

Branch `codex/stock-runtime-campaign-5e48b7c1`, unchanged HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`. Snapshot: 162 hash-verified
dirty/untracked source files in
`manual-artifacts/task-records/stall-test50-corrective-prechange/source`.
Prechange baseline passed: 29 cases/1906 assertions, helper 153 checks,
native contract. Source/headers/tests/docs are ordinary checkpointable files.

Final verification:

- Affected incremental Debug/Release client, native helper host and tests built.
  Focused suite: 227 cases / 145220 assertions per configuration. Existing
  B1/C/H3/H4/A1/input/HUD/game-API behavior expectations were not mass-rewritten.
  Added fake-peer production-host case proves RX progress during recovery,
  contiguous identities, one neutral release, no retroactive attack/Use,
  unchanged cap/cadence and resumed fresh motion.
- Existing ASan Debug build passed 31 focused cases / 2041 assertions.
  Its first process attempt lacked clang_rt.asan_dbg_dynamic-i386.dll (0xc0000135);
  adding the installed MSVC runtime to the test process's PATH resolved that
  environment issue. No global PATH change or sanitizer error was hidden.
- Core-only live-runtime library and API tests built with HL1 OFF; 95 cases /
  1424 assertions. Actual API/dependency-guard CTest gates passed 2/2 without
  linking the Half-Life module or the standalone addon.
- Actual OpenGL synthetic Studio/HUD/A1 camera-space check passed: 1 case /
  236 assertions, no context skip. Offline Null/process checks passed 8/8.
- Helper Debug/Release fake-engine checks: 182 each, including actual DLL/API
  loading, initially incomplete configuration recovery, unchanged max health,
  one-shot target identity, duplicate/healing/respawn/reconnect safeguards.
- Debug/Release socket-free native startup argv/readiness/redaction and existing
  functional-log contracts passed. Independent PowerShell missing/foreign/
  duplicate/success/rejection/pending-100 fixtures passed.
- No-stock launcher cases preserve reference/off/maps/damage-respawn-check.
  Actual test50/reference and normal off CheckOnly passed from system32.
  Restoration guard passed on a private fake tree, including removal of both
  scoped addon files and default startup configuration. No research install
  or real session was modified by these tests.
- All 162 saved snapshot hashes remain intact; only ten of their current source
  counterparts changed for this task. Three previously clean tracked scheduler
  source/header/test files also changed; the new report is a normal source file.
  HEAD/branch/staging remain unchanged; git diff --check passed. Original
  research hlds.exe/hl.dll/client.dll/crossfire.bsp hashes match the prior audit;
  no real run-scoped addon/default configuration was installed.

Release client SHA-256:
`0DA3D959ABD294AD386F459568D078936AF6941868345C96C5209D394B043CC3`.
Release native host SHA-256:
`FA1E87C74B1A3B44FB614F8954E75FDC26CADA8E3261D52B24B561F8EEC09BC8`.
Release test helper SHA-256:
`95D5A744EA644A03B9E6A2C1FDA09BFCD11E1940BF03EBE68A3DC4318BDAA536`.

No blanket CTest/live scripts, clean build, old build deletion, Steam
initialization, WFP activation, capture/ETW campaign, staging or commit/push.
Real installed-stock loading, first-spawn 50 HP and charger healing remain
unverified until a user-run session. The exact old helper failure and the
specific operation causing the observed frame stall remain unknown.

One user-run session (not executed here):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```

Normal reference/off and Scenario damage-respawn-check remain unchanged without
TestStartHealth. New-build manual validation remains pending.
