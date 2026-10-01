# Fast stock game-DLL path correction — 2026-09-28

Result: `fast_relative_metamod_staging_offline_verified`.
Manual validation of this correction: **not_run**.

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, unchanged HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`. Pre-edit files and inventory:
`manual-artifacts/task-records/fast-relative-metamod-fix-20260928/`.
All unrelated dirty work and historical reports were preserved.

## Confirmed second failure

User-provided run `86dfc089ff7e45d98dd5486f6f78ba0b`
(`attribution=user_report`) used corrected native helper SHA-256
`76D50F9C33D1164CE43476C6421E476BE7FACDEC55994019995F82DC90CBAF10`.
Its retained server log shows `Host_Error: Couldn't get DLL API from MLu!`.
The trailing text is not a meaningful filename. The wrapper retained
`server-early-exit`; client PID was zero/not created. It completed in 3365 ms,
with `owned_process_cleanup=exact`, `restoration_status=scoped_exact`, zero
cleanup errors and complete publication. There was no pending journal.

The AdminServer fix removed the command-line substring conflict, but the
installed stock **engine** has an additional game-DLL path gate. Read-only
inspection of `swds.dll`, SHA-256
`64B6872F9C2875EA7DA88EB694A34E6E46F2AAC7CB35FB436744F462611C03AE`,
showed (preferred image base `0x10000000`):

- `0x1021DF66` onwards rejects a leading slash/backslash, any `:` and repeated
  `..`, before loading the library. `0x102AC174` is the colon string.
- Rejection branches to `0x1021E032`, with the diagnostic
  `Skipping library with illegal characters in path: %s` at `0x102C0B68`.
- The missing API error is downstream; the DLL path buffer is not populated
  on this rejected-path branch. Do not treat `MLu` as a requested DLL name.

Thus the absolute `D:/.../metamod.dll` path left in liblist was incompatible
with this installed binary. Earlier ReHLDS source/filename checks did not
establish compatibility with that stock path gate. No stock binary was run,
modified, replaced or copied into project source to inspect it.

## Approved narrow correction

The user explicitly approved the exception to the no-repeat-addon-copy goal:
stage **only** the existing verified Metamod, 226304 bytes (221 KiB), under
`valve/addons/metamod/hlclient_test50_metamod.dll`. No rebuild/download/install
of dependencies occurs per launch. No maps/WADs/models/sprites/sounds,
`hlds.exe`, `hl.dll`, or full research tree are copied or rehashed for this fix.

`scripts/stock_manual_fast.ps1` now writes the fixed game-relative value
`addons/metamod/hlclient_test50_metamod.dll` in the scoped liblist. It does not
put any prepared path back on stock HLDS argv, preserving the AdminServer fix.
The already prepared health plugin remains referenced by its absolute path
in Metamod's plugin config, not in stock's game-DLL field or command line.
The pinned Metamod `mplugin.cpp` / `support_meta.cpp` path code accepts this
absolute plugin path. Whitespace/quote and length restrictions still apply.

`scripts/test_start_health_profile.psm1` exposes its already pinned Metamod
digest (`MetaSha`) to the plan. Fast opens the source without shared writes,
bounds the read, verifies the exact retained bytes against that digest, and
verifies the staged destination against the transaction's expected hash before
any native/game launch. Source changes fail before research activation.

Scope with 50 HP is now exactly:

- `valve/liblist.gam`
- `valve/addons/metamod/config.ini`
- `valve/addons/metamod/hlclient_test50_plugins.ini`
- `valve/addons/metamod/hlclient_test50_empty.cfg`
- `valve/addons/metamod/hlclient_test50_metamod.dll`

The four config bounds remain 64 KiB each. Only the exact DLL path permits up
to 256 KiB. No arbitrary DLL path, recursive scan or new directory whitelist
was added. Existing v1 journals with four files remain recoverable; new ones
may have five. Files that existed are backed up/restored with bytes and
metadata. Absent-before files are removed only if their current bytes match
the owned staged content. Unknown edits fail closed and remain recoverable.
Owned process cleanup still precedes restoration. Fast without 50 HP has an
empty mutation set. Strict uses its original full transaction and relative
per-run DLL route unchanged.

The new binary staging is included in `wrapper_file_work.copied_bytes`; source
and staged-file verification are included in `hashed_bytes`. These counters
still exclude opaque native checks and the separate component receipt pass.

## Verification

- Original Fast fixture baseline passed. A new independent model of the stock
  path gate then failed on the old production plan with
  `stock rejects planned gamedll path`, before research activation. After the
  fix it passes absolute/UNC/traversal rejection and the real plan's relative
  game-DLL path.
- `scripts/test_manual_fast.ps1` passed: pinned-source change rejection;
  oversized-source/config/DLL rejection; exact staged hash and I/O accounting;
  ordinary mode empty; removal of absent-before DLL; restoration of all five
  pre-existing files, including >64 KiB DLL bytes/metadata; actual terminated
  inert-wrapper recovery; legacy four-file recovery; preservation/refusal of
  foreign post-crash DLL and config edits; busy lease; history/asset
  preservation; startup failure, timeout, exit 17 and publication roundtrip.
  Only inert fake DLL bytes were written/exercised, never loaded.
- Backup-only fake 8 MiB fixture: full backup copied 8388974 bytes and hashed
  16777948 (590 ms); Fast backup copied 57 and hashed 154 (851 ms). Separately,
  profile activation staged exactly 226304 binary bytes and hashed 226361.
  This does not claim a small-fixture latency improvement or live startup time.
- Launcher fake-runner tests and independent test-health evidence tests passed.
- Eight selected Release CTests passed (21.27 seconds): restoration guard;
  startup boundary PowerShell 7/5.1; failure retention 7/5.1; functional
  projection 7; publication roundtrip 7; native log observation.
- Existing Debug and Release native validators passed
  `--validate-test-start-health-contract` (including the previous console
  regression) and `--validate-functional-log-observation`.
- Read-only plan using actual verified components produced five files,
  verified the 226304-byte DLL against pinned SHA-256
  `16B849F1CBF1503266A1B89D9B4ED38807091FABE724A8607D544338D155A005`,
  and confirmed no writes, no copied bytes, no tree scans, no pending lease.
- Actual launcher `-CheckOnly` from system32 passed for Fast/Strict
  reference+50 HP, Fast off and Fast damage-respawn-check.
- `git diff --check` passed; no staged changes. No C++ source changed, so no
  rebuild, engine/core-only/ASan/GL suite or clean build was performed.

Actual DLL initialization, health=50 and gameplay still need the next manual
session. This is not a live-success claim. Agent live/HLDS/client/Steam/WFP
launches=0; capture/ETW=not_run; staging/commit/push=none.
Real research liblist was not changed during these offline checks; SHA-256
remains `0D4034EBAABCC54AD9F00BB89E9677D5F34C3D3BEDA9185A69D5AC719E0E2FFB`.

## Ready command and binaries

Existing Release client SHA-256 (unchanged):
`D4ABE861BBD6FF2C428D5DDDB49FE92D5024744E4BF8EDB52419DED7582F939C`.
Existing Release orchestrator SHA-256 (unchanged):
`76D50F9C33D1164CE43476C6421E476BE7FACDEC55994019995F82DC90CBAF10`.
The correction is in the scripts the existing launcher reads on invocation.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```
