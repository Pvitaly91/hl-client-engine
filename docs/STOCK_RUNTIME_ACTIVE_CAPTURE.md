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

Inspect the source first, then create a new isolated copy. The research-copy
helper uses Windows handles and stable volume/file identities; it does not
treat a path string, `Get-Item.Attributes`, or `LinkType` as an identity. Root/ancestor
junctions and physically-contained directory junctions/symlinks are
materialized as ordinary directories. External targets, cycles, file symlinks,
mount points, unsupported reparse tags and ADS remain fail-closed by default.
Source hard links are copied from verified handles into independent
single-link files. The destination is a new local fixed-volume tree with zero
reparse points, hard links and ADS. See
[research-copy topology and materialization](STOCK_RUNTIME_RESEARCH_COPY.md).

The diagnostic is read-only even when the supplied destination does not exist:

```powershell
.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -InspectSourceTopology `
  -SourceHalfLifeRoot "D:\Steam\steamapps\common\Half-Life" `
  -DestinationHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life"
```

Unsafe topology is a successfully completed diagnostic (`exit 0` with
`result=unsafe`); it is not permission to materialize that topology.

M4.7.1.1.3 permits only exact eligible non-executable, non-mutable external
directory targets after a separate read-only review and explicit local,
expiring approval. Arbitrary external links remain blocked. Private review
records remain ignored and local; raw review paths/identities never enter the
v3 preparation manifest or runtime evidence. Review and approval contain no
runtime semantics or evidence and cannot replace active preflight. Follow
[external-target review](STOCK_RUNTIME_EXTERNAL_TARGET_REVIEW.md) and
[external-target approval](STOCK_RUNTIME_EXTERNAL_TARGET_APPROVAL.md). Every
new copy must carry the exact v3 preparation manifest; a reviewed copy must use
`reviewed-external-targets-v1`, `reviewed-non-executable-v1` and
`evidence_eligibility=eligible`. Its ignored
`external-target-approval.json` must remain in the exact repository-local
32-lower-hex review directory. Preflight screens that artifact as bounded,
ordinary, single-link and no-ADS, then requires its freshly computed SHA-256 to
equal the v3 `external_approval_sha256`; absence or ambiguity blocks WFP and
process launch.

M4.7.1.1.4 adds an earlier candidate-source gate and exact reparse provenance.
A completed dangling or unsupported-target diagnostic is still ineligible and
cannot reach this active-capture boundary. `ERROR_PATH_NOT_FOUND` is not an
empty-directory interpretation or eligibility signal; unavailable inventory
is not zero. If the current source fails, validate a clean secondary Steam
installation before materialization. No source link is repaired or opaque tag
followed, and no runtime semantics are inferred by this gate.

```powershell
.\scripts\prepare_stock_runtime_research_copy.ps1 `
  -SourceHalfLifeRoot "F:\SteamLibrary\steamapps\common\Half-Life" `
  -DestinationHalfLifeRoot "D:\DEV\HLCLIENT-RESEARCH\Half-Life"
```

The `F:` example denotes a source that already returned
`research-copy-eligible=true`; do not substitute the current ineligible `D:`
source. Subsequent preflight and capture commands keep the matching `F:`
AppID-70 manifest from that same source installation.

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
  -AppManifestPath "F:\SteamLibrary\steamapps\appmanifest_70.acf"
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

## Mandatory pre-campaign canary transaction

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
  -AppManifestPath "F:\SteamLibrary\steamapps\appmanifest_70.acf" `
  -PreCampaignCanary `
  -OutputRoot ".\manual-artifacts\stock-runtime-canary" `
  -Game valve -Map boot_camp -Scenario baseline `
  -RelayPort 27140 -ServerPort 27141 -MaximumDurationSeconds 45
```

This switch is valid only for the exact `boot_camp`/`baseline` canary root.
This direct diagnostic run is intentionally unbound and the campaign runner
will never recover or relabel it. After inspection, preserve it elsewhere if
needed and remove only the exact ignored `stock-runtime-canary` sibling before
running the campaign command; the runner captures and binds its own fresh
canary, which is not one of the 24 matrix slots.

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

`reconnect` uses one uninterrupted guard/relay/HLDS lifetime and two separately
owned stock-client processes. The A-to-B transition is a bounded two-phase
capability handshake. While generation A is still alive, the relay first creates
and binds a private send-only loopback tail emitter, switches subsequent
A-directed server sends onto it, and acknowledges readiness. Only then does the
orchestrator stop A and prove that its owned process is absent. A second signal
starts the relay's bounded quiet interval from A's exact source; the second
acknowledgement is required before generation B can launch. The retained A
address is routing state only, and any exact-HLDS sequenced tail before B's fresh
ACCEPT is forwarded only to that retired sink. The emitter is never read from,
never learns an endpoint and never exposes its ephemeral port. Generation B
must start from a distinct loopback endpoint and prove a new connectionless
connect, ACCEPT, first sequenced packet and offline post-resource boundary. Thus
`one_learned_client_endpoint=true` continues to mean at most one active learned
endpoint at an instant; the separate reconnect observation proves the two
sequential endpoints.

The relay and orchestrator first write sanitized staged reconnect attestations.
Both independently require
`generation_a_tail_emitter_ready_before_shutdown=true`; a missing or false
claim fails the strict checker, corpus loader and independent walker.
Those files alone cannot publish accepted evidence. The wrapper/checker/walker
must independently replay both generation ranges, find two exact boundaries and
two identical neutral candidates, and prove exact cleanup/restoration before the
run manifest can report `connection_generation_count=2`,
`generation_distinct=true` and `candidate_conflict=false`. Candidate bodies are
never consumed or assigned a semantic message name.

The campaign runner first requires a distinct accepted `boot_camp`/`baseline`
canary under the exact ignored `stock-runtime-canary` sibling. It binds the
implementation/profile/checker hashes, is independently verified, and never
counts toward a campaign slot; failure starts zero campaign runs. The campaign
runner then resumes only missing slots after proving that retained history has
no rejected/fatal run; only `bounded-session-incomplete` is resumable. The
runner and verifier each check the canary with two deterministic checker passes
and two deterministic independent-walker passes. Normal slots aggregate
peer-delivered checker counts; reconnect aggregates only the independently
replayed A+B `sequenced-*` counters. It never uses the reconnect flat journal
count to satisfy the 100-S2C per-run floor or 1,000-S2C global gate, so a
retired-A tail cannot inflate acceptance. Evidence remains
absent until the exact 24-slot matrix also proves four reconnect generations,
26 boundaries and 26 matching candidates. The fake resume/threshold scripts
exercise these gates without launching stock binaries or writing artifacts.

## Observable result

On a capable host, a successful active run visibly starts stock `hl.exe` in a
bounded window and enters the requested map through the loopback relay. The
`hl-client-engine` renderer is unchanged. No new project-renderer gameplay
image is produced by M4.7.1.1.

Do not share raw captures, authentication bytes, Steam tickets, BSP/WAD files,
private configuration or full process logs. A failure report needs only run ID,
scenario, exit code, typed category, bounded metadata summary and the
version/isolation/restoration statuses.
