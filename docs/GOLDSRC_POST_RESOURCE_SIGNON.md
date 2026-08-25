# GoldSrc post-resource sign-on boundary

M4.5.1 extends the ownership model beyond the opcode-5 resource response while
keeping the first unconfirmed stock message body untouched. This is currently a
bounded evidence-pending profile, not a claim that the project can enter a live
stock entity stream.

## Evidence status

The source-of-truth gate is a restoration-attested capture between Valve
`hl.exe` 1.1.1.1 (Steam App 70 build 15961492) and stock HLDS Protocol 48 build
10210. There are currently zero accepted entity-snapshot runs. Consequently:

- the first post-response opcode remains a numeric, neutral boundary;
- the exact continuation-request count, order, bytes, triggers and ACK lifecycle
  are pending;
- the stock sign-on-completion condition is pending;
- baseline, full-snapshot, delta-snapshot and client-frame opcode names are not
  assigned;
- no `docs/evidence/GOLDSRC_ENTITY_SNAPSHOT_STOCK.json` is present;
- stock-to-stock and project-to-stock entity snapshot support are not claimed.

The decoder validates that the boundary metadata and owning decoded payload
refer to the same direction, sequence, reliability/reassembly state and byte
count. In the stock profile it publishes one bounded
`unsupported_boundary` metadata item and returns the exact input cursor
unchanged. Its stock `body_byte_count` is unavailable: only the exact number of
bytes remaining in the owning payload is reported, because those bytes may
contain more than one message. It never scans forward for a familiar opcode.

## Typed requests

`PostResourceClientRequestBuilder` has no text or raw-byte input. Its stock
profile returns `stock_request_layout_evidence_pending` before producing bytes.
An independently authored, sealed `synthetic_neutral_v1` profile has one fixed
three-byte binary request used only by unit tests and fake peers. It is not a
GoldSrc protocol claim and cannot be selected through the production CLI.

There is deliberately no public `send_string_command`, raw-message injection,
snapshot replay or console-command API. Reliable sequencing, retransmission and
covering ACKs remain responsibilities of `NetchanDriver` once a stock request is
evidenced.

## Sealed synthetic stage profile

The independently authored `synthetic_neutral_v1` stage recognizes four exact,
three-byte control fixtures in order: request trigger, baseline publication,
full-snapshot publication and delta-snapshot publication. The trigger queues
the one fixed typed request. The later fixtures cause the stage to manufacture
caller-owned typed records: three ordinary entity-keyed baselines; a full
snapshot containing entities 1 and 2; and an exact-base delta that changes
entity 1, adds entity 3 from its matching baseline and explicitly removes
entity 2. The final history retains
both full and delta snapshots. Tests may stop after the baseline registry, the
full snapshot, or the first applied delta.

Decoding the delta control fixture alone remains `synthetic_sequence_in_progress`.
Only the ordered stage promotes completion after the delta was successfully
applied and the two-snapshot history was published.
Likewise, the standalone decoder reports baseline/full/delta control tokens as
`*_publication_observed`; `baseline_registry_ready` and `full_snapshot_ready`
are published only after the corresponding typed builders succeed.

Those four fixtures are test control tokens, not serialized baseline/entity
bodies, assigned stock opcodes, or proof of a GoldSrc wire grammar. Malformed
stock runtime masks and entity bodies therefore remain outside this profile and
still fail at the stock evidence boundary.

The manufactured-record stage requires an exact `entity_state_t` in the reused
registry and rejects any selected schema containing a time-window field. The
fake route intentionally supplies a small, independently authored,
time-window-free two-schema registry; it is not a runtime validation of the
accepted seven-schema/219-field opcode-14 description registry.

The deterministic fake-HLDS integration uses an in-process
`IDatagramTransport`; it does not open a real OS UDP socket or launch a server
process. It drives the complete coordinator from
challenge/connect through the resource response and these four BZip2-wrapped
fixtures on one stable logical endpoint, transport/session and driver. It
verifies the typed reliable request,
covering ACK, baseline/full/delta publication, origin/angle change, add/remove,
two-entry history, one cleanup and no asset/renderer work. Baseline,
dropped-request and dropped-ACK routes each run 20 times; retransmission is
induced by the driver's ordinary ACK-generation-gap mechanism, not by sleeping
or by a second request queue operation. Additional bounded variants cover a
duplicate baseline datagram, wrong endpoint, full/delta timeouts, terminal
delta replay immutability, cancellation, aggregate-message limits and event
queue backpressure including a saturated acknowledgement event.

## Ownership and stage boundary

`ResourceClientResponseStage` has a private continuation seam that can retain
the same driver and the bounded owning decoded source payload for the dedicated
post-resource consumer. The payload never enters the immutable public sign-on
state. Historical precache/local-preview continuations keep their existing seam
and cleanup behavior.

The `server-baselines` and `entity-snapshot` CLI spellings require `--connect`,
explicit authentication material, `--resource-consistency-provider local` and
`--basedir`. With the current stock evidence-pending profile they run the proven
pipeline through the resource-response boundary, send no invented continuation,
report the pending boundary and return non-success. They do not require or load
BSP, WAD, renderer or GPU resources and work with `--renderer null`.

## Safety policy

Post-resource limits are named project policy, not stock maxima. Defaults cap a
decoded payload at 65,536 bytes, a transcript at 64 messages and client requests
at 8; hard caps are 1,048,576 bytes, 512 messages and 32 requests. Configuration
outside those caps is rejected.

Events and default logs may contain only bounded geometry and categories. They
must not contain authentication bytes, player identity, raw packets, entity
field values, model paths or server-command text. Stufftext and any other server
command is never executed, passed to a shell, interpreted as a path, persisted,
or dispatched to an engine console.

## Capture tool

`scripts/verify_stock_entity_snapshots.ps1` validates an explicitly marked
isolated research root, performs fail-closed restoration checks, and refuses to
project stock evidence until every required scenario is restoration-attested.
After owned processes stop, restoration refuses any reparse point in the
research tree and rechecks each mutation target; a restoration failure retains
the bounded temporary backup and reports its recovery path instead of deleting
the only pre-run copy. The current candidate schema structurally checks exact
scenario/map tokens, counts and metadata hashes, but it has no per-run typed
observation trace capable of independently proving retry, duplicate, replay or
reset predicates. Therefore tracked validation and `ProjectAcceptedCaptures`
are deliberately disabled; those declarations cannot promote stock semantics.
The prompt-compatible `-Game valve -Map <map>` form performs the same read-only
isolated-root preflight and reports that no capture/restoration attestation was
created.
Raw runs belong only below ignored
`manual-artifacts/entity-snapshot-captures/`. The tracked projection is metadata
only and is not created when the accepted-run count is zero.
