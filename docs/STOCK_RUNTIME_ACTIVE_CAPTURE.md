# Stock runtime active capture

## Status

M4.7.1.1 adds a Windows-only, capability-gated research transaction. Active
capture is explicit opt-in and remains unavailable on a host that cannot prove
all binary, privilege, dynamic-WFP, process-ownership and restoration gates.
This checkout contains no accepted M4.7.1.1 stock run or committed raw corpus;
the reported accepted count is therefore zero until a real campaign completes.

The ordinary invocation is deliberately inert. Without both
`-EnableActiveCapture` and the case-sensitive literal
`HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1`, the wrapper stops before resolving
caller paths, creating a backup/run, opening a socket, launching a process or
starting a WFP session. The token has no default, environment-variable or
configuration-file source. The C++ orchestrator requires the same literal as a
defence-in-depth active-only argument.

## Preparation and elevated preflight

Create a new isolated copy. The helper rejects an existing destination,
Steam-library overlap, reparse points, ADS and hard links; it copies through a
GUID staging sibling and atomically renames the verified tree. It never changes
the source Steam tree.

```powershell
.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -SourceHalfLifeRoot "D:\Steam\steamapps\common\Half-Life" `
  -DestinationHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life"
```

Run environment validation from an elevated PowerShell. There is no automatic
self-elevation or weaker firewall fallback.

```powershell
.\scripts\capture_stock_runtime_state.ps1 `
  -ValidateActiveCaptureEnvironment `
  -ResearchHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life" `
  -ClientPath "D:\DEV\HLCLIENT-RESEARCH\Half-Life\hl.exe" `
  -HldsPath "D:\DEV\HLCLIENT-RESEARCH\Half-Life\hlds.exe" `
  -CaptureToolPath ".\build\bin\Debug\hlclient_stock_runtime_capture.exe" `
  -NetworkIsolationGuardPath `
    ".\build\bin\Debug\hlclient_stock_runtime_isolation_guard.exe" `
  -AppManifestPath "D:\Steam\steamapps\appmanifest_70.acf"
```

Success is exactly a read-only stock-process result:

```text
active-environment=valid
isolation-canary=success
binary-profile=valid
stock-processes-started=0
capture-files-written=0
result=success
```

Validation may temporarily create a dynamic WFP session for its canary. It
does not launch stock processes, create a run directory or change a game file.
Active capture repeats this exact stock-free validation before it creates the
restoration backup; the owned active transaction then establishes and verifies
its own dynamic guard/canary before run-directory publication.

## One baseline transaction

Run active capture only after the elevated preflight succeeds:

```powershell
.\scripts\capture_stock_runtime_state.ps1 `
  -EnableActiveCapture `
  -ConfirmActiveCapture HLCLIENT_STOCK_RUNTIME_ACTIVE_CAPTURE_V1 `
  -ResearchHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life" `
  -ClientPath "D:\DEV\HLCLIENT-RESEARCH\Half-Life\hl.exe" `
  -HldsPath "D:\DEV\HLCLIENT-RESEARCH\Half-Life\hlds.exe" `
  -CaptureToolPath ".\build\bin\Debug\hlclient_stock_runtime_capture.exe" `
  -NetworkIsolationGuardPath `
    ".\build\bin\Debug\hlclient_stock_runtime_isolation_guard.exe" `
  -AppManifestPath "D:\Steam\steamapps\appmanifest_70.acf" `
  -Game valve -Map boot_camp -Scenario baseline `
  -RelayPort 27140 -ServerPort 27141 -MaximumDurationSeconds 45
```

PowerShell owns full research/external snapshots, a private restoration backup,
post-run exact restoration, external Steam-state comparison, independent
walker agreement and one-time final-manifest publication. C++ exclusively owns
WFP, relay/HLDS/client processes, sockets and the kill-on-close Job. A run is
never accepted merely because the relay finished. Failure after run-directory
creation publishes `accepted_evidence_run=false` with a typed category.

No movement, keyboard or mouse automation belongs to this milestone. Accepted
active scenarios are `baseline`, `idle-runtime`,
`drop-server-to-client-transport-ordinal`,
`duplicate-server-to-client-transport-ordinal` and
`reorder-server-to-client-transport-ordinal`. The historical `*-server-runtime`
spellings are accepted only as wrapper aliases and are never published.
Perturbations select directional transport ordinals;
they are not runtime/entity/move packet labels.

`reconnect` remains in the requested campaign matrix but is fail-closed as
`reconnect_lifecycle_pending`: the current orchestrator cannot yet prove two
controlled sessions, distinct generations and two exact post-resource
boundaries. It returns before backup, run creation or stock launch.

## Observable result

On a capable host, a successful active run visibly starts stock `hl.exe` in a
bounded window and enters the requested map through the loopback relay. The
`hl-client-engine` renderer is unchanged. No new project-renderer gameplay
image is produced by M4.7.1.1.

Do not share raw captures, authentication bytes, Steam tickets, BSP/WAD files,
private configuration or full process logs. A failure report needs only run ID,
scenario, exit code, typed category, bounded metadata summary and the
version/isolation/restoration statuses.
