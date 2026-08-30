# Stock runtime process orchestration

## Ownership

`hlclient_stock_runtime_orchestrator` is the only active stock process owner.
PowerShell launches this one reviewed project tool; it does not launch or kill
`hl.exe`, `hlds.exe` or the relay itself. The orchestrator uses `CreateProcessW`
with explicit executable, arguments and working directory. It performs no
`cmd.exe`, `ShellExecute` or PATH lookup.

The relay, HLDS and every sequentially owned stock client share one campaign
Job Object with
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
7. Stock `hl.exe` generation A starts only after server readiness and must
   retain its exact image identity.
8. Client readiness requires the sanitized HLDS connection/entered-game
   category, a live client, bidirectional relay traffic and a sequenced minimum.
9. A reconnect scenario completes the two-phase A-to-B transition below while
   the same guard, relay and HLDS remain live. Other scenarios remain one
   bounded stock-client session.
10. After the bounded session, exact client and HLDS exit is confirmed first,
   then relay exit and campaign-Job zero. Only that zero-process proof releases
   the guard, which exits last and removes its dynamic filters.
11. PowerShell restores/compares state and performs offline publication gates.

The server profile is local (`-console -game valve -port`, requested map,
`+maxplayers 8`, `+sv_lan 1`, `-nomaster`, followed by `+status`). The client
uses a bounded window and sanitized `HLCLIENT_A`, not the operator's personal
player name.

## Two-generation reconnect transaction

Reconnect is not a parser reset, duplicated connect packet, long single client
session or merge of unrelated run directories. It uses two separately owned
stock-client processes within the one transaction:

1. Generation A completes connect, ACCEPT, sequenced traffic and its first
   post-resource boundary.
2. While A is still alive, the relay binds a private send-only loopback tail
   emitter, changes subsequent A-directed server sends to that emitter and
   acknowledges the transition capability.
3. The orchestrator terminates only owned client A and proves that process is
   absent. It then signals phase two; the relay requires a 250-ms quiet interval
   from A's exact source before acknowledging.
4. Only after that acknowledgement does the orchestrator launch fresh client B.
   B must produce a distinct learned source endpoint, new connectionless
   connect, fresh ACCEPT and first sequenced packet.
5. Generation B reaches an independently replayable post-resource boundary.
   Cleanup and restoration occur once, after both generations.

Exact-HLDS sequenced packets sent after A retirement and before B's ACCEPT are
byte-preserved through the routing-only tail sink and reported separately.
They cannot count toward B replay, either generation's accepted packet count,
the per-run 100-S2C floor or the campaign's 1,000-S2C gate. Strict staged relay
and orchestration attestations both bind emitter readiness, A shutdown/quiet,
continuity and B freshness; the final reconnect observation is published only
after independent A/B replay proves two boundaries and matching candidates.

## Resumable campaign ownership

The campaign runner first owns a separate accepted `boot_camp`/`baseline`
canary in the exact ignored `stock-runtime-canary` sibling. It must reach the
same accepted evidence gate and 100-S2C floor before any campaign process is
started, and it is never counted as a matrix slot. Its root and manifest are
held by retained no-share-delete directory capabilities; campaign progress
manifest replacement requires the exact previously validated bytes, moves that
old file aside by retained handle, and publishes the prepared successor with a
no-replace rename. First publication is no-replace too. Root or leaf
substitution therefore cannot redirect or silently repair publication. An
accepted canary left without its binding manifest is quarantined rather than
recovered or rebound; the operator preserves it for diagnosis, removes only the
exact ignored sibling and reruns for a fresh canary.

If a finalized run exists but its successor progress manifest is absent after
a crash, resume reports `campaign_progress_manifest_missing_or_unsafe` and
starts no new process. The runner cannot distinguish that window from hostile
manifest deletion, so it does not reconstruct the leaf. This limitation affects
only progress metadata: the capture wrapper has already closed the Job/guard
and completed restoration and drift verification before any campaign-manifest
operation begins.

After the canary, the runner fills only missing slots in a fixed 24-run matrix: six
`boot_camp` baseline, four `crossfire` baseline, four `stalkyard` baseline,
four `crossfire` idle-runtime, two `boot_camp` drop, one `crossfire` duplicate,
one `stalkyard` reorder and two `boot_camp` reconnect. Accepted, rejected and
incomplete runs remain separate, and an existing accepted slot is never
overwritten. Promotion stays closed until the canary, all 24 accepted slots, four reconnect
generations, at least 1,000 threshold-eligible S2C packets, at least 26 exact
boundaries/candidates, and identical results from two checker passes and two
independent-walker passes exist. The current real corpus remains empty, so this
process capability has not published evidence.

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
