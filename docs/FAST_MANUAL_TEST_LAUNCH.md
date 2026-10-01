# Fast manual test launch — 2026-09-28

Current manual timing extension (2026-10-01): `-DurationSeconds 1..86400`
or `-NoTimeLimit`; see the appended section below. The initial 45-second/90-
second timing report is historical, not the current opt-in timing contract.

Current correction: [relative Metamod staging](FAST_RELATIVE_METAMOD_FIX_20260928.md).
With explicit user approval, Fast+50 HP now stages one verified 226304-byte
Metamod DLL inside the existing scoped transaction (five files total).
The initial four-config-only/absolute-DLL design below is historical: installed
stock swds rejects absolute game-DLL paths. No full asset copy/scan was restored.

Follow-up: the first user runs exposed a stock HLDS substring-option conflict
in the prepared DLL command-line path. See
[the offline corrective report](FAST_HLDS_ADMINSERVER_FIX_20260928.md) for the
fix, regression evidence and current helper hash. The initial report below is
retained as historical evidence, not a claim that those manual runs passed.

Result: `manual_fast_launch_integrated_offline_verified`.
Live startup timing: **not measured**. No new stock/HLDS/Steam/WFP sessions,
capture or ETW. No staging, commit or push. Gameplay/client sources unchanged.

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66` (unchanged).
The pre-edit source snapshot and existing offline baseline are retained under
`manual-artifacts/task-records/fast-manual-launch-20260928/`.
Pre-existing work, historical reports and user-attributed manual results were
not replaced. This is not a new gameplay/manual validation result.

## Commands

The usual command now chooses Fast, without another required argument:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```

Omit `-TestStartHealth 50` for ordinary server health. Add
`-ValidationMode Strict` for the original full validation/backup path.
`-Prediction off`, the existing map choices and `-Scenario damage-respawn-check`
are preserved. HP assistance still requires crossfire/reference/manual.
`-CheckOnly` inspects files/parameter contracts only: no runner, game, audio
device, Steam initialization, WFP, socket binding, lease or config writes.
It is not live readiness or full content attestation, even with Strict selected.

## Actual changes

| Work | Before / Strict | Fast |
|---|---|---|
| Research preparation | Recursive inventory/content comparison | Exact root/marker, bounded preparation metadata, selected BSP and critical files |
| Research backup | Every bounded tree entry | Only the explicitly planned mutable files |
| Native preflight | Separate preflight, then validation again inside active owner | Same active-owner validation; redundant separate invocation omitted |
| 50 HP profile | Copy prepared DLLs into a per-run research addon directory | Reuse the verified prepared project DLLs by absolute path; scoped configs only |
| Restoration | Full tree bytes/metadata, including removing new entries | Only scoped files; unknown and historical files remain |
| Result | Full exact-restoration verdict when verified | `scoped_exact`; `full_tree_restoration=not_verified` |

Code is in the existing launcher, `scripts/capture_stock_runtime_state.ps1`,
its small `scripts/stock_manual_fast.ps1` policy helper, and the existing native
orchestrator. No second launcher, process owner, engine loop, cache or plugin
framework was introduced. Standalone capture/campaign tools still default to
Strict. Fast is restricted to the manual visual-control scenarios.

Fast never inventories/hashes/copies all maps, WADs, models, sprites, sounds,
`hlds.exe` or `hl.dll`. Critical individual binary identity/signature checks
remain. The physical-volume/root separation from the repository and configured
Steam libraries, no-alias checks at accessed paths, exact isolation marker,
appid, selected BSP, current port ownership, active same-image process checks,
owned Jobs and dynamic WFP isolation/canary remain. Fast assumes a trusted,
already prepared installation; it does not attest arbitrary unvisited assets.
No hardlink replacement or primary Steam-tree writes were added.

## Scope and recovery

Without HP assistance, the managed research-file mutation/backup set is empty.
An unexpected non-stock `gamedll` is rejected, not silently used or repaired.
With HP assistance the exact mutable set is:

- `valve/liblist.gam`
- `valve/addons/metamod/config.ini`
- `valve/addons/metamod/hlclient_test50_plugins.ini`
- `valve/addons/metamod/hlclient_test50_empty.cfg`

The existing verified DLLs in `build/test-start-health-deps/` and
`build/test-start-health/Release/` are reused. Their bytes/receipt/source
freshness are verified by the runner; the Fast launcher only checks presence,
avoiding a duplicate component hash pass. No helper is built, downloaded or
installed per launch. The helper's HP=50, max HP=100, first-spawn-only and fresh
server/client confirmation logic is unchanged. `test-server-assisted` is still
explicit and never treated as stock evidence.

The narrow snapshot feeds the existing retained-directory-capability guard.
It records original existence, bytes, creation/write timestamps and attributes.
The existing UUID temporary backup format is reused. Each run retains its small
backup and `scoped-transaction.json`; no historical backup is pruned.
A single exclusive `.hlclient-manual-fast.pending.json` lease is flushed before
profile writes. After interruption the next Fast launch checks exact root,
root creation identity, isolation marker hash, run UUID, exact backup path,
owner PID/start time, bounded allowlisted entries and absence of active native
images. It restores only recognized before/intended-after bytes. It does not
select the latest directory. Active owners, changed backup bytes, invalid
records, links or foreign file edits fail closed and retain evidence.

Cleanup first uses the existing typed owned-process/Job boundary. Only then
are existing files restored and verified, newly created owned files removed,
and newly created **empty** profile directories removed. Foreign files prevent
directory removal and remain intact. The pending lease is removed only after
successful scoped restoration. A killed wrapper is not protected by `finally`;
the next-launch recovery is the protection. Torn/invalid journal or unrecognized
partial/foreign bytes require explicit review rather than guessed recovery.

Stock HLDS can leave its own `valve/logs`, console/debug logs, caches and runtime
config output in the persistent research installation. These are not deliberate
wrapper replacements and are not broadly rolled back/deleted in Fast. Native
redacted diagnostics and wrapper reports remain in the exact current run under
`manual-artifacts/research-copy-smoke/<run-id>/`. Strict still rejects content
outside its existing policy; switching to Strict does not silently clean up or
re-attest unexpected persistent files. A pending Fast transaction must be
recovered before Strict is used.

## Timing and result contracts

One-shot phases: preflight, preparation, native server startup/readiness,
client startup, runtime-payload observation **if actually seen**, completed
owned cleanup, restoration and publication. Native stdout phases are drained
and forwarded while waiting, not only at exit. A five-second wrapper heartbeat
names the current phase. Wrapper and native monotonic clocks are explicitly
separate; native reported times are retained in the wrapper JSON. Existing
client-reported runtime intervals remain separate. No first-frame timestamp
or graphical readiness is invented from a process/connection observation.

The old banner said 90 seconds, but the existing native code actually supplies
`--live-session-seconds 45` for keyboard/mouse (min(requested budget minus 10,
45)). This task **preserves** that value and now describes it honestly. The
requested managed budget remains 90; the existing native client-readiness
bound is 60 seconds, server banner 15 seconds plus bounded readiness probes,
and wrapper outer process bound is budget + 90 seconds. Exact process exit is
observed immediately; there is no sleep to fill the full 90-second budget.
Existing bounded owned cleanup/output-drain waits remain; scoped restore checks
a 30-second deadline over at most four files of at most 64 KiB each.

Fast wrapper fields include:

```text
validation_mode=fast
backup_scope=managed_mutable_files
full_tree_backup=false
full_asset_hash_scan=false
restoration_status=scoped_exact              # only on scoped success
scoped_restoration_status=scoped_exact
full_tree_restoration=not_verified
evidence_eligible=false
```

Process cleanup, gameplay outcome, file restoration and report publication are
separate. Typed application failures and child exit codes are retained; coverage
does not become the primary error or make a nonzero result successful. Early
Fast native-preflight failure can publish its own failure directory after
attested cleanup. Preflight/invalid-input failures before a transaction cannot
invent a run ID/report. The launcher reports unavailable explicitly in that
case, never selects a “latest” run. Native and wrapper validation modes must
match. Normal/incomplete JSON publication remains no-overwrite, same-run only.

`wrapper_file_work` counts bytes passed through the PowerShell SHA helper and
backup/restore copy calls. It excludes opaque native/Authenticode hash work and
the separate prepared-component verification. It is not a claim of zero total
file I/O. Tests directly verify that immutable fake asset bytes do not enter
the Fast snapshot/backup/hash path.

## Offline verification

- Existing pre-edit launcher and 50 HP baseline passed.
- `pwsh -NoProfile -File scripts/test_h2_manual_launch.ps1`: passed default
  Fast/explicit Strict forwarding, map/reference/off/scenario/profile rules,
  system32/space paths, exact/incomplete reports and nonzero exit propagation.
- `pwsh -NoProfile -File scripts/test_manual_fast.ps1`: passed real narrow
  snapshot/backup/metadata restore; zero-file ordinary guard; stale helper
  rejection; removal of owned absent-before files; preservation of history,
  foreign files and an unrelated live **inert** process; exclusive lease;
  actual exited-wrapper recovery; foreign-change refusal; startup failure,
  one-second timeout and handled exit 17; phase allowlist and JSON roundtrip.
- Eight selected Release CTests passed: restoration guard, startup boundary
  (PowerShell 7 and 5.1), failure retention (7 and 5.1), functional research
  projection (7), publication roundtrip (7), native log observations.
- Strict restoration guard passed again after final copy instrumentation.
- Native orchestrator Release and Debug built; both pure
  `--validate-test-start-health-contract` and
  `--validate-functional-log-observation` passed. No other C++ target was
  requested for rebuild; no client relink.
- Independent `scripts/test_test_start_health.ps1` evidence cases passed.
- Actual-installation **read-only** Fast root/marker/map/component/plan check
  passed: zero tree scans, copied bytes, leases or game processes.
- Actual launcher `-CheckOnly` from `C:\WINDOWS\system32` passed in both modes
  for reference+50HP, off, and damage-respawn-check. Research liblist SHA stayed
  unchanged and no pending Fast marker was created.
- `git diff --check`: passed. No blanket engine, ASan or GL suite was run for
  this orchestration-only change.

Same 8 MiB project-owned fake asset fixture, common capability implementation
warmed before timing. The “before” path is the original full snapshot/backup
algorithm, not a separate production implementation. Last measured run:

| Preparation only, no stock processes | Full before | Fast after |
|---|---:|---:|
| Copied bytes | 8,388,974 | 57 |
| Hashed bytes | 16,777,948 | 154 |
| Recursive tree scans | 1 | 0 |
| Elapsed milliseconds | 329 | 438 |

Fast includes its process/lease ownership check; this small warm-cache fixture
does **not** show an elapsed-time speedup. Earlier samples were 404/576,
452/620 and 376/459 ms. The evidence is removal of asset-size-dependent file
work, not a promised number of seconds saved in a real session. Live timing
and the new prepared-DLL launch route still require the user's manual run.

## Binary handoff

Unchanged Release `build/bin/Release/hlclient.exe` SHA-256:
`D4ABE861BBD6FF2C428D5DDDB49FE92D5024744E4BF8EDB52419DED7582F939C`.

Rebuilt Release `hlclient_stock_runtime_orchestrator.exe` SHA-256:
`CEC3D3B7C26EFE625FB7CDFD4E5395357C053E25789590AB8DD1B4E34BB39FBF`.

Prepared DLL paths must be short and whitespace-free for the existing Metamod
plugin filename grammar; the current worktree satisfies this. Fast needs
PowerShell 7. Strict's PowerShell 5.1 offline contracts remain tested.
Full-install attestation, first-frame timing and live 50 HP confirmation are
not inferred from these offline results.

## Manual session timing — 2026-10-01

Requested result: choose a gameplay duration or finish by closing the clients,
without a gameplay timer. The usual launcher and normal application path now
support:

```powershell
# Five minutes, two clients on the same owned server:
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -DurationSeconds 300

# No gameplay timer; user closes the window:
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Omit `-RemoteAudioPeer` for one client. `-Prediction off` remains supported.
`DurationSeconds` accepts integer1..86400 (seconds, maximum24 hours);
`NoTimeLimit` is genuinely user-ended gameplay, not a large substitute timeout.
They are mutually exclusive; zero/negative/overflow values fail before launch.
The ordinary default remains45 seconds. Timing overrides are limited to
`Scenario manual`; `damage-respawn-check` retains its existing scripted bounds
and rejects either override. Map, approved external-map, health and audio
profile restrictions are unchanged.

Closing A ends the whole two-client managed session, including B, as before.
Closing B alone does not end A. Wait for owned cleanup/restoration before
closing the PowerShell console; no second invocation or process-owner override
was added. Errors, server/guard exit and connection failures can still end a
session; no-time-limit does not mask them.

### Production timing and diagnostic retention

The launcher forwards one typed FunctionalSmoke parameter:
`ProjectClientDurationSeconds` or `ProjectClientNoTimeLimit`. Standalone capture
limits and `MaximumDurationSeconds`5..300 are unchanged. The normal native host
passes `--live-session-seconds` to both clients, or explicit
`--live-session-unlimited` with no duration value. The existing application
clock/close path is retained; no second loop or game/prediction policy changed.
Core parser and peer-plan regressions cover both modes and their conflicts.

Native manual wait allows at most60 seconds without observed runtime. Once
runtime is observed it permits user-ended gameplay, or the requested game
duration plus60 seconds of startup margin. Timed wrapper bound is requested
duration+180 seconds. Unlimited wrapper startup remains180 seconds; only a
strictly framed native runtime-observed transition removes that outer gameplay
deadline. A new owned-cleanup-started transition restores a90-second cleanup
deadline. Other wrapper modes retain their existing deadline. WFP, exact
process handles, kill-on-close Jobs, guard heartbeat, exclusive lease and
scoped restoration remain active; none was disabled to extend gameplay.

Long manual runs use opt-in diagnostic prefix+rolling-tail capture: per log
maximum1MiB, up to64KiB startup prefix (also bounded to half the retained line
budget),8192 retained newline rows and16KiB individual-line validation. Eviction
occurs only across whole lines; observed counts and truncation flags remain
reported. Raw byte/count-truncated logs are still **not complete capture
evidence**. Only this explicit manual timing path accepts a usable bounded
diagnostic window for publication; capture/read failures and oversized lines
still fail. Stock/campaign prefix-only capture and acceptance rules are
unchanged. Manual timing reports are evidence-ineligible even under Strict.
Normal redacted excerpts retain their existing16KiB/64-line privacy limits and
owned numeric visibility grammar. No unbounded disk log/archive was introduced.

Native staged summary and wrapper launch metadata separately record timing mode
and requested duration (null for unlimited); full-log completeness remains
separate from diagnostic-window usability. Source was preserved before edits
(1270 files) in
`manual-artifacts/task-records/e10-manual-session-timing-20261001/source`.
No historical manual report or user-attributed gameplay confirmation changed.

### Offline verification scope

No-stock launcher fixtures prove default/custom/unlimited parameter forwarding,
reference/off, peers, paths with spaces, conflicts, invalid values, scripted
rejection, exact report selection and real nonzero exit forwarding. Inert child
fixtures prove unlimited gameplay survives the old outer deadline but missing
runtime still times out; synthetic clock controls prove cleanup remains bounded.
Actual Windows pipe capture proves retained startup/terminal rows with bounded
bytes/lines and explicit incomplete-full-capture metadata. Debug/Release timing,
CLI and process-log regressions passed2086 assertions in21 cases. The initial
new CLI fixture omitted its required auth provider; its failing log is retained
and the fixture was corrected, without weakening production validation.

Actual launcher CheckOnly passed for peer/reference with300 seconds and no
timer, and for off/no timer. It did not launch the runner, game, Steam or WFP.
New build manual=not_run; new game/stock-client/HLDS/Steam/WFP/capture sessions0;
staging, commit/push=none. ASan runtime remains unconfirmed (the earlier runtime
exited1 without output even for help); it was not rebuilt or repaired here.

### Final timing verification and binary handoff

VS2022/v143/Win32 Release all-target build and affected Debug build passed.
The existing core-only tree (`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`) built without
the Half-Life module and passed41/41 tests, including the architecture guard.
Targeted Debug and Release each passed21 cases/2086 assertions. Native peer
contract, functional log observation, no-stock launcher and Fast runner
fixtures passed. Final read-only CheckOnly passed for timed reference/peer,
unlimited reference/peer and unlimited off/single-client.

The final Release offline suite covers all2637 registered tests:2600 passed,
37 capability/opt-in skips,0 failures. WFP activation and local installed-game
controls were explicitly disabled. Verification was partitioned into2636
parallel tests (94.85 seconds) and the one global-absence fake-client fixture
alone (2.43 seconds). That fixture conflicts with other concurrently running
fake-client tests; the isolated run preserved all330 assertions. Earlier logs
retain the parallel collision and the stale source-marker failure; the latter
was updated to the stronger manual-evidence-ineligibility predicate, without
weakening assertions. No suite test was omitted from the combined final result.

Results and source inventory are retained in
`manual-artifacts/task-records/e10-manual-session-timing-20261001/`, including
`release-offline-partition-final.txt`, `release-global-fixture-final.txt`,
`core-only-tests.txt`, `focused-debug.txt`, `focused-release-final.txt` and
`source-preservation-final.json`. This is offline evidence, not a live/manual
confirmation of the new timing modes or a fix for the reported player visibility
issue.

Current normal Release `build/bin/Release/hlclient.exe` SHA-256:
`E5D34F030202A2231C6EF08959F9391965E0602D002634BECBE574367EDB23D2`.

Current Release `build/bin/Release/hlclient_stock_runtime_orchestrator.exe`
SHA-256:
`EEB543271B8EBA3F33CE6193EFCBC85A3A039E16197C9DC6D092AA53257ABD70`.

These are the files selected by the unchanged launcher path; neither hash is
attributed to an earlier user session. Previous binary-handoff sections remain
historical.
