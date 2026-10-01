# Fast manual HLDS AdminServer correction — 2026-09-28

Follow-up: user run `86dfc089ff7e45d98dd5486f6f78ba0b` got beyond the
AdminServer conflict but exposed stock swds rejection of the absolute DLL path
in liblist. See [the relative-path correction](FAST_RELATIVE_METAMOD_FIX_20260928.md).
This original report and its offline/manual distinction remain historical.

Result: `fast_hlds_console_path_conflict_fixed_offline_verified`.
Manual validation of this corrected build: **not_run**.

Worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66` unchanged. Existing dirty work was
preserved; only the startup policy, its tests and documentation were touched.
The pre-edit source snapshot and inventory are in
`manual-artifacts/task-records/fast-adminserver-fix-20260928/`.

## Failure evidence and cause

The screenshots (`attribution=user_report`) show stock `hlds.exe`, not
`hlclient.exe`, asserting `g_hAdminServerModule != NULL` at `sys_ded.cpp:891`.
Exact runs:

- `23674f3f5af24409a2f6094f97c054cb`
- `fef200ac98534891a409c7aa3bc6c6ba`

Their final wrapper reports retain `local_server_startup_failed` and
`server-profile-missing-field`. The latter is the downstream absence of the
server banner, not the cause of the assertion. The client was not created.
Both reports confirm `owned_process_cleanup=exact`,
`restoration_status=scoped_exact`, `cleanup_error_count=0`. These do not claim
full-tree restoration in Fast. No pending Fast journal or active game/build
process was present before this fix. Research `valve/liblist.gam` remained the
original file with SHA-256:
`0D4034EBAABCC54AD9F00BB89E9677D5F34C3D3BEDA9185A69D5AC719E0E2FFB`.

Read-only disassembly of the installed stock `hlds.exe` established the cause;
the binary was not run, patched or copied into source. Its SHA-256 is
`BE96B878E5F4BE6BDD4E1AD21603C570D8AB4238542F36A7F84C4AB573CCCE8C`.
At preferred image base `0x00400000`:

- `CheckParm` at VA `0x00405660` calls the `strstr` import thunk at
  `0x0043C0DB`; there is no option-token boundary check before returning a match.
- Startup checks `-steam` at `0x00406C09` before `-console` at `0x00406C2A`.
  A match branches to the GUI path at `0x00406C80`.
- GUI initialization loads `Platform/Admin/AdminServer.dll` at `0x004068A0`
  and asserts on a null module, with source line `0x37B` (891).

Fast added `-dll D:/DEV/CPP/HLC-steamcfg-5e48b7c1/.../metamod.dll`.
The path contains `-steam`; therefore the legacy substring parser selects GUI
even though `-console` is present. Quoting the path does not solve this.
This is unrelated to prediction coverage, crowbar/audio decoding or the
previous gameplay exit-code-2 reports.

## Correction

`apps/hlclient_stock_runtime_orchestrator/main.cpp` now selects the Fast health
launch arguments by mode, without passing any prepared component path to the
stock command line. The already managed `valve/liblist.gam` selects the exact
verified prepared Metamod DLL. The helper path stays in its plugin config.
No DLL installation/copy, additional managed file or new launch flag was added.

The run UUID, profile sentinel, player name, `+meta require HLC50`, command
ordering and health-readiness gates are unchanged. Strict retains its existing
relative `-dll` and per-run `mm_configfile` route. Ordinary health, reference/off
and damage-respawn scenarios are unchanged. Config verification, the four-file
Fast transaction, process ownership, restoration and failure propagation were
not weakened. No gameplay/client, Steam installation or research file was
modified by this correction task.

## Offline verification

1. Original native health contract and Fast fixture baseline passed before
   source edits. A new native regression using the exact `HLC-steamcfg-...`
   spelling then **failed on the old launch policy**, Debug exit 2,
   `fast-stock-console-regression=failed`, `stock-processes-started=0`.
2. After the correction, targeted Debug and Release orchestrator builds passed.
   Both `--validate-test-start-health-contract` and
   `--validate-functional-log-observation` passed in both configurations.
   The independent legacy substring model covers the failing quoted old path,
   genuine `-steam`, safe `-console`, the production Windows command-line
   builder, no Fast `-dll` or prepared path, exact startup commands, and the
   unchanged Strict path. It does not execute proprietary code.
3. `scripts/test_manual_fast.ps1` passed with inert component files under
   `HLC-steamcfg-5e48b7c1`. It checks exact liblist/plugin/default config bytes
   plus real scoped restore, interrupted recovery, foreign-file refusal,
   zero-file off mode, timeout/startup failure/nonzero exit preservation.
   The new exact-byte expectation was corrected to preserve the existing
   liblist replacement's absent final newline; production config bytes were
   not changed to satisfy the test.
4. `scripts/test_h2_manual_launch.ps1` and
   `scripts/test_test_start_health.ps1` passed, including fake-runner results,
   Fast/Strict forwarding and server/client health evidence gates.
5. Eight selected Release CTests passed (20.11 seconds): restoration guard;
   orchestrator startup PowerShell 7/5.1; failure retention 7/5.1; functional
   research projection 7; publication roundtrip 7; native log observation.
6. Actual launcher `-CheckOnly` from `C:\WINDOWS\system32` passed for Fast and
   Strict reference+50 HP, Fast off and Fast damage-respawn-check.
7. `git diff --check` passed; staged changes remain empty. No full engine,
   core-only, ASan or OpenGL rerun was needed for this startup-only correction.

These checks prove the corrected argument/config contract, not actual stock
DLL initialization or live health=50. The next user session must confirm that
boundary. No new stock/HLDS/client/Steam/WFP session, capture or ETW was run.
Only inert fixture processes and pure offline validator modes were executed.
No staging, commit or push.

## Ready handoff

Rebuilt Release `build/bin/Release/hlclient_stock_runtime_orchestrator.exe`:
`76D50F9C33D1164CE43476C6421E476BE7FACDEC55994019995F82DC90CBAF10`.

Unchanged Release `build/bin/Release/hlclient.exe`:
`D4ABE861BBD6FF2C428D5DDDB49FE92D5024744E4BF8EDB52419DED7582F939C`.

The user's manual command is unchanged:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```
