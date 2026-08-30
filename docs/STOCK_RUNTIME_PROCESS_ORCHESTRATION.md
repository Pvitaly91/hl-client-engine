# Stock runtime process orchestration

## Ownership

`hlclient_stock_runtime_orchestrator` is the only active stock process owner.
PowerShell launches this one reviewed project tool; it does not launch or kill
`hl.exe`, `hlds.exe` or the relay itself. The orchestrator uses `CreateProcessW`
with explicit executable, arguments and working directory. It performs no
`cmd.exe`, `ShellExecute` or PATH lookup.

The relay, HLDS and stock client share one campaign Job Object with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`; the WFP guard is held in a separate
wrapper-owned kill-on-close guard Job so campaign exit can be proved before
the dynamic session is released. Every approved executable is created
suspended and atomically enrolled with `PROC_THREAD_ATTRIBUTE_JOB_LIST`, then
image-identity checked and only then resumed. The launch boundary also applies
`PROCESS_CREATION_CHILD_PROCESS_RESTRICTED`; relay, guard, canary, HLDS and
client therefore cannot create descendant processes. The deterministic fake
integration asserts the OS returns `ERROR_CHILD_PROCESS_BLOCKED`, creates no
requested child-side-effect file and leaves exact Job accounting at zero.
Cleanup uses retained process/Job handles and exact active-process accounting,
never a
process-name-only kill. An unrelated Steam,
`hl.exe` or `hlds.exe` process is not terminated; a pre-existing executable
from the exact research root instead blocks the run.

## Ordered transaction

The active transaction is ordered as follows:

1. PowerShell completes static research/binary/tool/manifest checks and invokes
   the stock-free environment-validation canary. Capability failure occurs
   before a backup or run directory exists.
2. PowerShell records research and external Steam snapshots and creates a full
   private restoration backup.
3. The active orchestrator repeats/verifies the dynamic-WFP guard and canary
   inside the owned transaction.
4. Only then it creates the new ignored 32-lower-hex run directory.
5. The byte-preserving relay starts and proves readiness.
6. HLDS starts, and bounded output must prove engine `1.1.2.2`, Protocol 48,
   build 10210, `valve`, requested map and requested port. A project-issued
   stock `+status` follows `+map`; readiness requires its exact
   `map     : <requested-map> at: ...` map-active line, not a fabricated ready
   marker.
7. Stock `hl.exe` starts only after server readiness and must retain its exact
   image identity.
8. Client readiness requires the sanitized HLDS connection/entered-game
   category, a live client, bidirectional relay traffic and a sequenced minimum.
9. After the bounded session, exact client and HLDS exit is confirmed first,
   then relay exit and campaign-Job zero. Only that zero-process proof releases
   the guard, which exits last and removes its dynamic filters.
10. PowerShell restores/compares state and performs offline publication gates.

The server profile is local (`-console -game valve -port`, requested map,
`+maxplayers 8`, `+sv_lan 1`, `-nomaster`, followed by `+status`). The client
uses a bounded window and sanitized `HLCLIENT_A`, not the operator's personal
player name.

## Logs and failure

Redirected logs remain below the ignored run directory. Process log byte, line
length and line-count limits are enforced, and truncation is explicit.
Log and attestation publication uses cryptographically random `CREATE_NEW`
staging files, flush, same-handle identity validation and no-replace rename
while exact output-directory capabilities and private delete-on-close child
locks prevent directory/ancestor swaps. Existing destinations are never
truncated or replaced.
Committed metadata may contain only categories/counts—not full logs, paths,
names or authentication material.

Timeout, profile mismatch, relay/client/server/guard failure, Ctrl+C and tool
exceptions converge on closing the owned Job. The terminal summary reports
actual process counts, relay/server/client readiness, duration and
`job-cleanup=exact` or a typed failure. A run cannot be published accepted
until later restoration, drift, offline replay and walker gates also pass.

Before guard launch, the orchestrator starts and attests a redundant dynamic
WFP session for the same five approved images. That outer-scope session remains
active through both Job exit barriers, so abrupt guard death cannot create an
unfiltered campaign window. The guard inherits retained handles to both the
campaign Job and its own guard Job. If the wrapper and orchestrator die
together, heartbeat EOF cannot remove WFP: after a bounded clean-release grace,
the surviving guard terminates the campaign Job, polls `ActiveProcesses` to
exact zero and exits nonzero only after that proof. An accounting/termination failure keeps the guard and WFP session
alive while retrying; it never converts an unproved campaign exit into cleanup.
