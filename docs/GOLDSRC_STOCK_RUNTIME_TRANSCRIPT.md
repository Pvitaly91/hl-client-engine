# Stock Protocol 48 runtime transcript

## Status

M4.7.1 provides a bounded clean-room research and evidence boundary. No
accepted signed-stock runtime session is available in this checkout, so the
runtime grammar remains
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

Current tooling stores raw UDP datagrams and flat transport metadata only below
ignored `manual-artifacts/stock-runtime/`. Parsed netchan headers, transformed,
fragment, reassembled, decompressed, cursor-annotation, and runtime-message
layers are not implemented in the capture path. Their configured bounds are
validated future limits, not proof that those layers were consumed. The relay
forwards bytes independently of parsing and whole-datagram perturbation labels
select only a directional ordinal, not a decoded runtime/move packet.

Active research is disabled before process launch or output creation until
OS-level outbound isolation and complete stock app/engine/protocol/build
observation are implemented. The current zero-run verifier opens no socket,
writes no file, reports versions/restoration as `not_observed`/`not_run`, and
does not treat absence as successful stock evidence. The standalone catalog
decoder is not yet composed into production `PostResourceSignon`.
