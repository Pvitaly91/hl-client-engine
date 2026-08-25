# GoldSrc post-resource client response

> M4.5.1 continuation note: the stage now has a second private, one-consumer
> seam that may retain the same driver plus the bounded owning decoded source
> payload for `PostResourceEntitySnapshotStage`. Raw payload bytes remain
> private and are cleared during exactly-once cleanup. The historical
> precache/local-preview seam and every public response-boundary behavior are
> unchanged. Because no accepted entity capture exists, the production
> continuation still stops before the first post-response body and sends no
> client request. See [post-resource sign-on](GOLDSRC_POST_RESOURCE_SIGNON.md).

M3.1.3 continues from the exact M3.1.2 end-of-payload boundary. It separates
one reliable semantic unit from a contemporaneous suffix, builds the semantic
unit from typed provider material, queues it once through the persistent
`NetchanDriver`, waits for its covering acknowledgement, and stops at the first
opcode of the next complete server payload. M3.2.1 supplies an explicitly
selected production local provider without changing this wire codec, stage, or
path-free provider API.

The public wire name is intentionally neutral: `Opcode5ResourceResponse`.
Evidence strongly supports a client custom-resource advertisement, but the
numeric opcode is not mapped by the pinned public Valve headers and the
invalid-response behavior has not independently closed the semantic gate.

## Evidence and clean-room method

The reconstructed corpus contains 54 signed Valve client-to-stock-HLDS
sessions. All contain the same selected 41-byte reliable semantic fragment.
The ignored raw bodies were decoded by the project's independently authored
Protocol 48 netchan transform and strict fragment descriptor codec. Tracked
evidence contains only ranges, widths, counts, hashes, and lifecycle metadata;
it excludes response bytes, opaque material, raw names, authentication,
identity, executables, and game assets.

New active scenarios use only a user-supplied isolated copy with marker
`.hlclient-research-isolated` containing
`HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1`. Preflight rejects `steamapps`,
registered Steam roots, the primary Half-Life installation, marker mismatch,
and reparse points; it starts no process and does not establish file drift.
Every accepted run instead needs a
`hlclient.stock-resource-response-restoration.v1` attestation. Its bounded
`snapshot_entry_count` and `restored_entry_count` must match, its
`pre_manifest_sha256` and `post_manifest_sha256` must be equal, and its bounded
`created_file_count` must equal `created_file_removal_count`. Absent/changed
local-resource scenarios additionally require
`local_mutation_target_restored=true`; other scenarios require it false. Only
then may the attestation state `external_file_drift=none`.

No active M3.1.3 run currently has a complete restoration attestation. The
isolated runner was fail-closed while the primary `hl.exe` was running, and
earlier attempts stopped when the isolated HLDS could not initialize Steam.
Consequently `verify_stock_resource_response.ps1 -ProjectEvidenceSet` refuses
projection and `docs/evidence/GOLDSRC_RESOURCE_CLIENT_RESPONSE_STOCK.json` is
intentionally absent. The reconstructed 54-session corpus supports the codec
and provider boundary below; it does not count as completion of the requested
active loss, duplicate, differential, or next-payload scenarios.

For the absent/changed local-resource differential only, a bounded active run
may end with `scenario_incomplete` and no response. The verifier records
`response-not-observed` only when response transmission and ACK arrays are
empty, the post-response boundary and raw files are absent, and no response
mutation was applied. Every observed response and every other scenario still
requires the complete response, covering ACK, and exact next-payload boundary.

Reference binaries:

- Valve `hl.exe` VERSIONINFO 1.1.1.1, Steam App 70 build 15961492;
- Valve `hlds.exe` launcher VERSIONINFO 4.1.1.1, engine 1.1.2.2,
  Protocol 48 build 10210.

## Carrier geometry

The response is not the entire decoded carrier. The descriptor is validated
first and determines the ranges:

| Range | Meaning |
|---|---|
| `[0,10)` | normal-fragment descriptor area |
| `[10,51)` | selected reliable semantic fragment |
| `[51,N)` | contemporaneous non-reliable suffix |

The confirmed descriptor has slot 0 present, packed index/count 1/1, offset 0,
length 41, and slot 1 absent. `ResourceResponseCarrierGeometry` records all
three ranges, full decoded size, source sequence, and reliable generation. A
descriptor mismatch, nonzero selected offset, range overrun, wrong fragment
count, or configured bound violation rejects the carrier atomically.

Observed full sizes 62, 64, 66, and 68 produce suffix lengths 11, 13, 15, and
17. The semantic range remains exactly 41 in every case. The suffix begins with
candidate opcode 2, but its grammar is not complete enough for execution or a
builder. `ResourceResponseConcurrentTail` therefore owns only bounded length,
SHA-256, and confirmed carrier controls. A synthetic tail may be submitted once
through `NetchanDriver::submit_unreliable()` in tests. It is never part of the
canonical reliable bytes and is absent from retransmission.

## Exact neutral body grammar

The supported response is byte-aligned and exactly 41 bytes:

| Offset | Width | Field | Validation/source |
|---:|---:|---|---|
| 0 | 1 | opcode | constant profile value 5 |
| 1 | 2 | entry count | little-endian, exactly 1 |
| 3 | 14 | NUL-terminated wire identifier | constant supported profile; 13 ASCII bytes plus NUL |
| 17 | 1 | structural type | constant profile value 3 |
| 18 | 2 | structural index | little-endian, constant profile value 0 |
| 20 | 4 | local byte count | little-endian, provider material |
| 24 | 1 | structural flags | constant profile value `0x04` |
| 25 | 16 | opaque material | fixed-width private provider material |

There is no padding or trailing byte. The pinned Valve
`third_party/halflife-sdk/engine/custom.h` independently agrees with the
name/type/index/download-size/flags/16-byte field order, `t_decal == 3`, and
`RES_CUSTOM == 1 << 2`; it is only a secondary semantic cross-check and is not
used as a packed wire struct.

Field dependency classification:

| Input | Dependency |
|---|---|
| opcode, count, identifier, type, index, flags | supported compatibility profile |
| local byte count, 16 opaque bytes | `IResourceConsistencyProvider` |
| map/resource list/session/user-info | no observed field dependency in the 54-session corpus |
| tail | separate opaque transport-time metadata |

The M3.2.1 local implementation maps the supported provider profile—not any
server resource name—to the fixed virtual target `tempdecal.wad`. It streams
that file through one sandboxed read-only handle and supplies the exact handle
byte count plus 16-byte MD5-compatible material. MD5 here is a GoldSrc
compatibility primitive only, not security or trust evidence. The wire name
remains the existing neutral compatibility-profile constant; M3.2.1 does not
promote a new opcode semantic claim.

The parser accepts the semantic fragment only plus an explicit evidence
profile. It validates exact opcode, length, little-endian fields, constants,
terminator, widths, and end of input; it returns exact bytes consumed and never
publishes a partial candidate. The builder accepts only
`ResourceClientResponseInput` and provider material, and returns owning
canonical semantic bytes. It cannot add a netchan header, fragment descriptor,
transform, or tail and exposes no arbitrary production byte injection API.

The independently authored fixture uses `synthetic.wad`, a synthetic byte
count, and opaque bytes `A0..AF`. It is a literal 41-byte array with SHA-256
`77AF845BE1360A3C3E0D92E129D0E05D36C4F0C826B118A0B56196D7041BD154`;
no production builder generates the expected test value.

## Reliable lifecycle

`ResourceClientResponseStage` composes `ResourceListStage` through a private
retained-driver constructor. Calling `ResourceListStage` directly preserves
M3.1.2 behavior: `response_queue_count() == 0`, no response is queued or sent,
and `--stop-after resource-list` closes at its historical boundary.

The continuation performs this bounded sequence:

1. publish path-free provider requirements once;
2. fail as `provider_required` without sending when no provider exists;
3. poll one move-only provider operation;
4. build the canonical semantic fragment once;
5. call `NetchanDriver::queue_reliable()` once;
6. let the driver fragment, transform, sequence, and retransmit;
7. complete only on the driver's exact covering-ACK event;
8. retain at most one complete pre-ACK server payload;
9. reassemble and decode the first following `BZ2\0` service envelope;
10. read byte 0 only and publish `PostResourceResponseBoundary`.

When `--resource-consistency-provider local` is selected for
`--stop-after resource-response-boundary`, the application validates explicit
`--basedir`/`--game` roots and fully prepares the one-shot provider before
creating the network runtime or UDP socket. Invalid roots or provider material
therefore cause zero packets. The default `--game` remains `valve`; a non-Valve
game searches its root before the `valve` fallback. Without the provider option,
the historical typed `provider_required`/zero-TX route remains unchanged.
Earlier stop points neither prepare nor consult the local provider.

The stage has no retry timer. A dropped response datagram is retransmitted by
the existing ACK-gap policy with a new packet sequence and transform key but
the same canonical semantic bytes and reliable generation. A dropped covering
ACK does not requeue semantics. Stale ACKs are no-ops; future ACKs are protocol
errors. Completion requires an advanced acknowledgement whose numeric sequence
covers the latest response send and whose reliable acknowledgement bit matches
the response toggle, with no pending/in-flight response left.

A provider wait has its own five-second manual-clock deadline (60-second hard
cap), independent of channel activity. Two more fixed deadlines use the same
default and hard cap: semantic queue to covering ACK, then covering ACK to the
next boundary. Header-only traffic cannot extend either phase. A server payload
is not eligible as a post-response continuation until an update after the first
response TX, and its numeric source ACK must equal or advance past that first
TX sequence, so a delayed pre-response payload cannot satisfy the boundary.
The driver also publishes the completed local fragment-transfer generation
with its ACK; the stage requires exact generation equality before completion.

Stock observations show a separate padding-only covering packet before the
next client reliable unit in the reconstructed corpus. Progress can then branch:
most sessions send a distinct `spawn` string command, while direct crossfire
sessions first send a bounded `BZ2\0` batch of `dlfile` commands for unresolved
entries. Readiness/precache belongs to M3.2.2 and download/cache behavior belongs
to M3.3; neither branch is implemented in M3.2.1.
They also mean the exact following stock server opcode remains active-evidence
pending; fake-server boundary tests do not promote a synthetic opcode to stock
evidence.

## Next-server boundary

`PostResourceResponseBoundary` contains the first opcode or exact
end-of-payload, byte/bit offset, remaining bytes, owning source-payload
metadata, and evidence profile. If the first message is complex, its opcode is
read at offset zero and its body remains entirely unconsumed; there is no opcode
scan and no permissive next-message parser.

## Safety limits

These are project policy, not stock-engine maxima:

| Limit | Default | Hard cap |
|---|---:|---:|
| resource response size | 41 | 4,096 |
| response field count | 16 | 256 |
| response opaque bytes | 16 | 4,096 |
| concurrent tail | 64 | 4,096 |
| complete pre-ACK server payloads | 1 | 1 |
| response-stage events | 64 | 256 |
| post-response payload | 65,536 | 1,048,576 |
| provider wait | 5 seconds | 60 seconds |
| response queue to covering ACK | 5 seconds | 60 seconds |
| covering ACK to next boundary | 5 seconds | 60 seconds |

Defaults and hard caps are validated, and exact-limit/limit+1 paths are tested.
Events and traces contain metadata only: state, sizes, ranges, opcode, reliable
generation, sequence/ACK numbers, and error codes. They never contain raw
response/tail bytes, opaque provider material, local paths, resource names, or
authentication identity.

## M3.2.2 retained continuation

`PrecacheManifestStage` privately retains the completed response stage instead
of cleaning its driver at the response stop. It carries the same transport,
netchan driver, authentication lifetime, owning response state, and shared
local-resource environment through local inventory/readiness/manifest work.
The public historical response-boundary stop still cleans up exactly once.
The manifest continuation queues no reliable unit and emits no additional
semantic network message.

## Explicit scope boundary

M3.2.1 adds the production local provider and sandboxed resolution foundation;
M3.2.2 adds only strict readiness and immutable manifest metadata. Neither adds
download/cache writes, resource request generation, `dlfile`, BSP/WAD/MDL/SPR
parsing, asset loading, renderer changes, client world state, or captured
response replay. The `hlclient_goldsrc_signon` target remains free of filesystem
and local-provider implementation dependencies. A separate M3.1.4 is required
only if sufficient active stock evidence establishes a substantial next-message
codec. See [local readiness](LOCAL_RESOURCE_READINESS.md) and
[precache manifest](PRECACHE_MANIFEST.md); M3.2.3 is the next asset-source
boundary.
