# Stock Protocol 48 runtime transcript

## Status

M4.7.1 provides a bounded clean-room research and evidence boundary, and
M4.7.1.1.1 provides its exact resumable campaign and two-generation reconnect
lifecycle. No accepted signed-stock runtime session is available in this
checkout, so the runtime grammar remains
`stock_protocol_48_build_10210_evidence_pending`. This document does not assign
numeric runtime opcodes or body layouts.

| Claim class | Current result |
|---|---|
| Stock-confirmed | The existing resource-response handoff retains the exact first unconsumed byte/bit cursor and transport metadata. |
| Public-header cross-check | Valve SDK structures and `network/delta.lst` describe semantic fields only. |
| Project-derived | Bounds, immutable metadata containers, structural hashes and transactional publication. |
| Pending | Runtime opcode catalog, body lengths, ordering, alignment, ready condition and all entity/client-local message grammar. |

## Exact boundary

`StockRuntimeMessageCatalogDecoder` accepts the owning `OwnedServicePayload`,
the exact `PostResourceResponseBoundary`, and the already-published delta schema
registry. In the pending profile it publishes one neutral
`unsupported_runtime_message` entry whose start and end are the exact incoming
cursor. It consumes zero body bits, performs no scan, emits no client request
and publishes no runtime frame.

An unknown body is never skipped and a later byte that resembles a known
opcode is never used for resynchronization. Byte alignment is not assumed. A
future confirmed catalog must record opcode/body/end alignment and padding for
every message before it can advance the cursor.

Reserved `runtime_v1` and `delta_v1` profiles remain fail-closed until the
minimum accepted-run gates and an independently reviewed sanitized evidence
record exist. Merely selecting a profile value cannot enable decoding.

## Research boundary

M4.7.1.1 adds a separate Windows-only research transaction. It is off by
default and requires the exact case-sensitive active token. A capable elevated
host must prove the signed/versioned client and server, App 70 build, HLDS
engine/Protocol/build banner, a dynamic temporary WFP loopback-only policy and
an OS-classified non-loopback denial before stock launch. Persistent firewall
rules are not created, and lack of WFP/canary capability keeps capture
unavailable.

The relay remains byte preserving and records bounded raw datagrams plus an
exact JSONL transport journal below ignored
`manual-artifacts/stock-runtime/`. Observed and delivered streams are separate;
drop/duplicate/delay/reorder select directional transport ordinals, not decoded
runtime, entity or usercmd messages. Offline replay follows emitted order and
reuses the existing connectionless, netchan transform, fragment/reassembly,
bounded BZip2 and confirmed sign-on/resource codecs. It generates no packet.

The campaign has exactly 24 resumable slots: 14 baseline runs across
`boot_camp`, `crossfire` and `stalkyard`; four `crossfire` idle-runtime runs;
four transport-ordinal perturbation runs; and two `boot_camp` reconnect runs.
An accepted run contributes only after exact restoration and independent
checker/walker agreement and must contain at least 100 threshold-eligible
sequenced S2C packets. Evidence promotion additionally requires all 24 slots,
at least 1,000 aggregate S2C packets, four reconnect generations, at least 26
exact post-resource boundaries and 26 mutually matching neutral candidates.
The final corpus summary must be deterministic across two checker invocations
and two independent-walker invocations before sanitized evidence can exist.

Each reconnect uses one continuous guard/relay/HLDS transaction and two fresh
owned stock-client processes. Before A is stopped, the relay creates a private
send-only loopback tail emitter, redirects later A-bound server traffic to that
routing-only sink and acknowledges readiness. After A is proved absent, a
second handshake proves a 250-ms quiet interval from A's source; only then may
B launch from a distinct learned endpoint and complete a fresh connect/ACCEPT
lifecycle. Exact-HLDS sequenced A-tail traffic is still journaled and counted
as retired-tail traffic, but it is excluded from B replay, both generation
packet counts and all campaign packet gates. A and B are replayed independently
from reset state and must yield separate exact boundaries and identical neutral
candidates.

Successful replay reconstructs the exact post-resource byte/bit cursor and
retains one unconsumed `first_post_resource_runtime_candidate`. That candidate
has no assigned service semantic and its body is never scanned or consumed.
Only exact restoration, no external Steam-state drift, checker determinism and
independent walker agreement permit the PowerShell wrapper to publish a final
accepted run manifest. This checkout has zero accepted real M4.7.1.1.1 runs,
zero observed boundaries/candidates and no tracked first-observation evidence
JSON. The completed campaign lifecycle does not satisfy or lower that real
evidence gate. M4.7.1.2 owns runtime grammar; M4.7.2 owns stock usercmd. The
pending catalog decoder remains separate from production `PostResourceSignon`.
