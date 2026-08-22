# GoldSrc Protocol 48 fragmentation profile

## Scope and status

M2.3.3 implements the project-owned fragment codec, bounded incoming normal
reassembly, persistent driver, and deterministic outgoing normal fragmentation.
It does not make `hlclient` fully stock-compatible: stock multi-fragment
client-to-server scheduling, slot-1/file semantics, compression conventions,
live project-client-to-stock-HLDS operation, and general service/resource
parsing remain pending. M2.4.1 recognizes the separately confirmed first
`BZ2\0` service envelope after this layer has delivered one owning normal
payload; M2.4.2 continues only through its bounded typed server-info and
pre-resource boundary above this layer; M2.4.3 continues that owning payload
through its bounded delta-description registry and numeric opcode-44 stop;
M2.4.4 decodes the confirmed movement/environment state and simple controls,
then stops before the neutral opcode-13 body.

The labels in this document are strict:

- **Stock-confirmed** means a behavior repeated with signed Valve `hl.exe` and
  stock Valve HLDS through a bounded byte-preserving loopback relay.
- **Project deterministic/tested** means a project policy covered by local
  fixtures, state-machine tests, or fake transports. It is not promoted to a
  stock-engine rule.
- **Project fail-closed** means an ambiguity is rejected without accepting,
  exposing, or replacing bytes.
- **Pending** means the stock behavior was not established and the corresponding
  compatibility claim is not made.

The implemented profile is intentionally narrow:

| Area | Status |
| --- | --- |
| Two-slot descriptor boundary and slot-0 stock shape | **Stock-confirmed** |
| Incoming slot-0 normal reassembly | **Project deterministic/tested**, informed by the captured shape |
| Persistent same-transport `NetchanDriver` | **Project deterministic/tested** |
| Project outgoing slot-0 fragmentation | **Project deterministic/tested** mirror; stock multi-fragment C2S verification pending |
| Slot 1 or a file/download interpretation | **Pending**; bytes fail closed before retention |
| Compression detection/decompression | **Pending in this layer**; fragment bytes remain opaque, while M2.4.1 strictly decodes only the confirmed first `BZ2\0` envelope above it |
| Project client to stock HLDS | **Pending** |
| Opcode-14 delta-description registry above this layer | **Implemented/tested in M2.4.3** |
| Opcode-44 movement/environment metadata above this layer | **Implemented/tested in M2.4.4** |
| Resource bodies, snapshots, or gameplay | **Pending**, beginning with M3.1 |

The bounded project integration proof uses real loopback UDP with production
`UdpDatagramTransport`: M1 challenge, connect, and `ACCEPT` complete before a
production `NetchanDriver` continues on the same socket and exact source. The
incoming case passes 20/20 runs with final-fragment-first ordering, one exact
same-sequence duplicate, per-fragment ACKs, one complete owning payload, bounded
packet accounting, and no extra datagrams. The outgoing case passes 20/20 runs
with independent fake-server descriptor decoding/reconstruction, one dropped
fragment, a fresh-sequence ACK-gap retry of the same canonical range, final
matching-generation clear, bounded packet accounting, and no extra datagrams.
Separate loopback cases prove fixed missing-fragment timeout without partial
delivery or extra ACK, and `secondary_stream_pending_m3` without payload
delivery, filesystem persistence, or sequence commit. These are **Project
deterministic/tested** results, not stock-server interoperability evidence.

The current application/coordinator composes a driver on the same socket and
owns it through the selected explicit boundary. The `netchan-bootstrap` stop
completes only after the first unfragmented or supported reassembled slot-0
opaque payload has been acknowledged. The separate `signon-boundary` stop keeps
the same transport/driver semantics through the bounded M2.4.1 decoder. The
`pre-resource`, `delta-schemas`, and `movevars` stops retain that same
transport/driver only through their exact bounded continuations; the last
publishes typed movement/environment metadata and leaves opcode 13 wholly
unconsumed. No mode exposes raw fragment/reliable bytes or claims a live
stock-HLDS channel.

## Clean-room evidence

The reference client was signed Valve `hl.exe` 1.1.1.1. The reference server
was stock Valve `hlds.exe` 1.1.2.2, Protocol 48, build 10210. Every accepted run
used IPv4 UDP on loopback, one fixed server endpoint, one upstream relay socket,
an immutable client endpoint learned from the canonical first `getchallenge`
leg, byte-preserving forwarding, and packet/byte/time bounds. Datagrams from any
other loopback source were neither forwarded, decoded, hashed, nor stored and
were bounded by a metadata-only counter: at most four are permitted before the
run fails closed.

The primary M2.3.3 set contains exactly 12 accepted `bounded_complete` research
runs, two for each controlled scenario:

| Scenario | Accepted run IDs | What the pair establishes |
| --- | --- | --- |
| baseline | `baseline-20260816-005832-209`, `baseline-20260816-010558-442` | descriptor profile, per-fragment ACK generations, completion, and a clean later transfer |
| drop middle | `dropmiddlefragment-20260816-010746-540`, `dropmiddlefragment-20260816-010803-869` | only the missing middle fragment is retried |
| exact duplicate | `duplicatefragment-20260816-011000-958`, `duplicatefragment-20260816-011018-492` | original is admitted once; same-sequence duplicate is ignored |
| reorder control | `reorderfragments-20260816-011129-573`, `reorderfragments-20260816-011146-715` | an already accepted index 2 replayed after newer index 3 is old and ignored |
| drop first | `dropfirstfragment-20260816-011246-580`, `dropfirstfragment-20260816-011303-928` | stop-and-wait begins at index 1 and retries it before progress |
| drop final | `dropfinalfragment-20260816-011402-344`, `dropfinalfragment-20260816-011419-658` | the next transfer waits for the final fragment's covering matching ACK |

The `reorder` name must not be overread: the byte-preserving relay forwarded
index 2, then index 3, then replayed the old index-2 datagram. Stock stop-and-wait
did not emit an unseen index 3 while index 2 was withheld, so true unseen
out-of-order stock admission remains pending.

Rejected attempts are not counted:

- `baseline-20260816-004644-943`, `baseline-20260816-004840-721`, and
  `baseline-20260816-005122-497` ended `scenario_incomplete`; none counts and no
  semantic conclusion relies on them;
- the other early baseline launch directories with no metadata were pre-evidence
  launcher/endpoint refusals and establish no protocol fact;
- `secondtransfer-20260816-011533-364` performed the requested mutation but
  ended `scenario_incomplete`, while `secondtransfer-20260816-011643-053` was a
  pre-launch refusal with no metadata;
- one later explicit second-transfer metadata set passed the semantic schema but
  the whole run was rejected because post-run process cleanup could not be
  proven; that rejected metadata was removed;
- two earlier committed-verifier self-tests were rejected for a non-integer JSON
  field and then a runtime filter defect. Both defects were corrected, both
  artifacts were removed, and neither run is evidence.

Both accepted baselines naturally completed a later transfer beginning at
ordinal 1 after the preceding final fragment was covered. That is the two-run
second-transfer evidence. Deliberately replaying an old completed-transfer
fragment after the next transfer starts has no accepted cleanup-complete pair
and remains pending as a stock claim.

No raw datagram, authentication material, identity byte, opaque fragment byte,
or process log is tracked. The accepted research summaries were produced during
discovery and are not claimed to have passed through the later strengthened
wrapper. The opt-in wrapper is:

```powershell
.\scripts\verify_stock_netchan_fragments.ps1 `
  -RelayPath C:\Tools\bounded-fragment-relay.ps1 `
  -HalfLifePath C:\Games\Half-Life\hl.exe `
  -HldsPath C:\Servers\Half-Life\hlds.exe `
  -Game valve -Map boot_camp -Port 27420 `
  -Scenario drop-middle-fragment -TimeoutSeconds 45
```

Its scenario allowlist is `baseline`, `drop-middle-fragment`,
`duplicate-fragment`, `reorder-fragments`, `drop-first-fragment`,
`drop-final-fragment`, and `second-transfer`. It requires signed reference
binaries, private loopback, exact endpoint/accounting metadata, fixed resource
bounds, scenario-specific actions, `raw_packet_bytes_stored=false`, an empty
pre-run stock-process snapshot, and empty post-cleanup stock-process and selected
port snapshots. It terminates only exact direct PID/path/start-time identities;
an unproven descendant causes rejection rather than broad process termination.
Only `metadata.json` is permitted under an ignored, reparse-checked run
directory, and success is printed only after cleanup gates pass.

## Stock-confirmed wire profile

The netchan header remains two little-endian 32-bit words. Numeric sequences use
the low 30 bits. Sequence bit 30 (`0x40000000`) means fragment descriptors are
present, and sequence bit 31 means reliable bytes are present in that datagram.
The acknowledgement word's bit 31 carries the acknowledged reliable generation.

The offset-8 payload transform still covers complete 32-bit words only. Any
zero-to-three-byte tail is unchanged. Transform decoding therefore occurs before
descriptor parsing, and a retransmission under a fresh numeric sequence has
different wire bytes even though its decoded descriptor-selected bytes are
identical.

The decoded fragment body starts with exactly two ordered slots. Each slot has
one presence byte. An absent slot is only `00`; a present slot is:

```text
presence:u8       = 1
packed_id:u32 LE  = (one_based_index << 16) | declared_count
offset:u16 LE
length:u16 LE
```

The captured field often called a fragment ID is not a stable transfer ID: its
high 16 bits change with the one-based ordinal, and its low 16 bits carry the
declared count. Project APIs therefore expose `NetchanPackedFragmentId` for this
wire value and use separate channel-local identities for owning state.

In every accepted stock fragment packet, slot 0 was present and slot 1 was absent. A
slot-0-present/slot-1-absent descriptor area is ten bytes, so its shared payload
area begins at datagram offset 18. Descriptor offsets index that per-datagram
area; every accepted stock normal descriptor used offset zero. Offset is not a
transfer-global byte position. The project derives the canonical transfer
offset from `(index - 1) * 1,024` only inside its supported normal profile.

Repeated complete server transfers included:

| Transfer | Count | Descriptor-selected lengths | Opaque size |
| --- | ---: | --- | ---: |
| first | 5 | 1,024, 1,024, 1,024, 1,024, 90 | 4,186 bytes |
| second | 6 | 1,024, 1,024, 1,024, 1,024, 1,024, 105 | 5,225 bytes |

The first descriptor decoded as
`01 05 00 01 00 00 00 00 04 00`; its final descriptor decoded as
`01 05 00 05 00 00 00 5A 00 00`. These are independently bounded descriptor
fixtures, not retained opaque payload bytes.

A descriptor-selected fragment range does not necessarily consume the complete
post-descriptor body. Natural stock C2S count-one packets had 41-byte and
20-byte selected ranges plus 11-byte and 15-byte contemporaneous suffixes. The
codec therefore owns the entire post-descriptor payload and records
`fragment_payload_size` as the end of the validated contiguous descriptor-owned
prefix; the remaining bytes are preserved separately as opaque one-shot data.
The captured suffix fact is C2S. Applying the same strict split to S2C delivery
is project deterministic/tested behavior.

Complete accepted transfers did not begin with standard BZip2, GZip, or zlib
markers. This is only a marker-negative observation, not proof that stock never
compresses. The project does not derive an output size, filename, or path from
opaque fragment bytes and does not perform decompression in M2.3.3.

## Stock-confirmed reliable behavior

Fresh M2.3.3 capture supersedes the earlier unsupported assumption that one
reliable generation covers a complete fragmented transfer. Stock admission and
reliable acknowledgement are per fragment. Every observed fragment carried
sequence bit 31, and the client's reliable-ACK generation alternated across
successive fragments:

```text
transfer 1: 1, 0, 1, 0, 1
transfer 2: 0, 1, 0, 1, 0, 1
```

The numeric ACK covered the sequence of the individual fragment packet. A
fragment may be ACKed by later piggybacked traffic; capture does not justify a
fixed latency or exactly one immediate standalone ACK datagram.

The loss scenarios repeated these exact transmission counts for the first
five-fragment transfer:

| Scenario | Transmissions by index 1..5 | Result |
| --- | --- | --- |
| baseline | `1,1,1,1,1` | completes once |
| drop middle | `1,1,2,1,1` | only index 3 retries |
| drop first | `2,1,1,1,1` | no later index before index 1 retries |
| drop final | `1,1,1,1,2` | no next transfer before index 5 clears |

For each drop, ordinary server sequence traffic advanced until the client's ACK
numerically passed the missing fragment's latest send while retaining the wrong
generation. The server then resent the exact same packed ordinal/count, range,
length, and opaque bytes under a fresh numeric sequence. This is the same
ACK-gap behavior established for unfragmented reliable traffic; no reusable
time-only retry interval is inferred from the measured delays.

Forwarding the same middle-fragment datagram twice did not cause a second source
transmission, a second reassembly unit, or reliable-ACK rollback. Replaying the
already accepted index-2 datagram after newer index 3 likewise left the ACK on
index 3. These captures prove same-sequence duplicates and lower-sequence old
datagrams are filtered before reliable/reassembly mutation. They do not prove
what stock does with the same range and bytes re-encoded under an otherwise
admissible fresh sequence.

The final fragment's newest covering matching-generation ACK gates transfer
release and the next reliable unit. Both baselines then began a new transfer at
index 1 with a new count. Exact behavior for a fresh index-1/count replacement
while an older transfer is incomplete and for an old completed-transfer packet
re-encoded under a new sequence remains pending.

Natural outgoing stock-client fragmentation was observed only as count-one
slot-0 datagrams. Matching stock-server ACKs covered those datagrams and their
generations alternated, but no natural C2S transfer with count greater than one
was captured. Project multi-fragment C2S construction and scheduling therefore
remain a deterministic mirror pending stock verification.

## Project fragment codec

`netchan_packet.hpp` exposes a pure, transport-free boundary:

- `NetchanFragmentStream` names slot 0 as `normal` and slot 1 as
  `unconfirmed_slot_1`;
- `StockProtocol48FragmentProfile` records two slots, the 1,024-byte normal
  chunk, absence of a stable wire transfer ID, per-fragment reliable admission,
  and a possible contemporaneous suffix;
- `NetchanPackedFragmentId` validates and splits the packed ordinal/count;
- `NetchanFragmentDescriptor`, `NetchanFragmentSlots`, and
  `NetchanDecodedFragmentPacket` own the typed descriptor boundary and complete
  post-descriptor payload;
- `decode_netchan_fragment_body()` and `encode_netchan_fragment_body()` operate
  only after/before the offset-8 transform. Direction-specific full-packet
  codecs compose that pure body codec with the header and transform.

The codec rejects invalid presence values, truncation, zero length, invalid
ordinal/count, range overflow/out-of-bounds, overlapping descriptor ranges,
reserved header flags, inconsistent fragment flags, and packets above their
configured datagram bound. Decode and encode failures return typed errors and
do not mutate session or reassembly state.

## Project incoming normal reassembly

`NetchanNormalReassembler` is filesystem-free and owns at most one active slot-0
normal transfer. `NetchanNormalTransferId` is a local monotonic state token and
is never serialized. The move-only `NetchanFragmentInsertPlan` carries the
candidate transaction. The public flow is:

```text
prepare_insert(descriptor, exact selected range, now)   // read-only
    |-> abandon_insert(plan)                            // no mutation
    `-> commit_insert(plan)                             // atomic state change
         -> optional owning completion payload
```

Defaults and hard ceilings are project safety limits, not stock maxima:

| Reassembly boundary | Default | Hard maximum |
| --- | ---: | ---: |
| selected bytes per normal fragment | 1,024 bytes | 1,024 bytes |
| owning normal transfer | 65,536 bytes | 1,048,576 bytes |
| fragments per transfer | 64 | 1,024 |
| stored fragment ranges | 64 | 1,024 |
| active normal transfers | 1 | 1 |
| fixed transfer deadline | 5 seconds | 30 seconds |

Non-final normal fragments must be exactly 1,024 bytes; the final fragment may
be shorter. Completion requires every declared ordinal exactly once and a
contiguous range ending at the final fragment's boundary. A state-changing
prepared insert owns at most one additional bounded candidate image, so peak
reassembly image storage is at most twice the configured transfer limit. Exact
duplicate plans own no extra bytes or ranges and are revision-neutral.

**Project deterministic/tested:** unseen out-of-order normal ordinals may be
buffered within one active transfer, then completed only when coverage becomes
contiguous. Conflicting duplicates, changed boundaries, partial overlaps,
different declared counts during an active transfer, invalid final boundaries,
oversize ranges, foreign plans, and stale plans fail without replacing valid
bytes. This out-of-order policy is not stock-confirmed.

Because the wire has no stable transfer ID, an active transfer cannot be safely
replaced by a new count. After completion a metadata-only tombstone requires
ordinal 1 to start the next lifecycle; a later ordinal fails as
`old_fragment_after_completion`. No completed opaque payload is retained by the
tombstone. This is **project fail-closed** for the capture ambiguity.

The timeout is fixed when the first accepted fragment creates the transfer.
Later progress and exact duplicates update neither the deadline nor its bound.
`expire()` emits the owning local transfer identity once and releases bytes and
ranges. `clear()` releases active bytes/ranges and invalidates prepared plans.

Slot 1 returns `secondary_stream_pending_m3` before its bytes enter reassembly.
The project deliberately does not call it a file stream, parse a remote name,
or write it to disk without stock evidence and a separate safe output contract.

## Project outgoing normal fragmentation

`NetchanSession::queue_reliable()` retains canonical decoded bytes. If pending
bytes exceed the configured unfragmented reliable limit, packet preparation
automatically selects the deterministic slot-0 fragment path. The hard canonical
outgoing-transfer bound is 16,376 bytes, inherited from the reliable pending
queue, which yields at most 16 fragments at 1,024 bytes each.

`NetchanOutgoingFragmentTransferId` is local-only. `NetchanFragmentBuildPlan`
records its local transfer, one-based ordinal/count, canonical range, and retry
classification. Each new fragment:

- sets sequence bits 30 and 31;
- encodes slot 0 with `(index << 16) | count`, offset zero, and the selected
  length while slot 1 is absent;
- flips the outgoing reliable generation once on successful send commit;
- remains the sole in-flight reliable unit until its latest covering
  matching-generation ACK arrives;
- advances to the next ordinal only after that clear; and
- retries the same canonical range/generation under a fresh numeric sequence
  after the normal ACK-gap trigger.

The complete canonical transfer remains owned until the final fragment clears.
Reliable message B can accumulate in the existing bounded pending queue while A
is fragmented, but B cannot change A and is not exposed until A's final clear.
`clear_reliable_state()` releases pending bytes, the current in-flight fragment,
and the canonical outgoing transfer.

A caller-provided unreliable payload is appended after the descriptor-selected
fragment prefix as one-shot contemporaneous suffix data. It is cleared after a
successful send and is not retained for a retry. This suffix composition and
all count-greater-than-one project C2S scheduling are **project
deterministic/tested**, not stock-confirmed.

Outgoing preparation is read-only. Encode/send failure must abandon the plan;
only `commit_outgoing_send()` advances the numeric sequence, generation,
send-count, or canonical fragment lifecycle. Foreign, stale, already-consumed,
or state-mismatched plans fail without partial mutation.

**Project deterministic/tested wrap policy:** a fragmented sequence word would
collide with the reserved split marker at numeric sequence `0x3ffffffe` and the
connectionless marker at `0x3fffffff`, because fragment packets set both bits
30 and 31. The session therefore prepares one ordinary non-fragment,
non-reliable eight-byte-padding acknowledgement packet for each of those two
numeric sequences. A successful transactional commit advances only the packet
sequence; it does not advance the fragment cursor, flip the reliable
generation, change in-flight send metadata, consume a retry request, or consume
a caller's pending one-shot suffix. The unchanged fragment is then encoded at
numeric sequence zero with its new transform key. Send failure or abandonment
leaves all state unchanged. Exact stock fragment behavior at this wrap remains
**pending**; the policy exists to guarantee that the project never emits a
fragment datagram whose first word is classified as a different packet family.

## Persistent driver contract

`NetchanDriver` owns one `NetchanSession`, one `NetchanNormalReassembler`, one
bounded one-shot unreliable payload, a bounded owning event queue, and an
optional opaque `INetchanDriverLifetime`. It borrows an externally owned
`IDatagramTransport`; that transport must be the already-bound socket selected
for the connection and must outlive the driver. `start()` verifies the expected
local endpoint, and every received datagram must come from the exact remote
endpoint before it is parsed.

The public control surface is deliberately small:

```text
start(now, expected_local_endpoint)
update(now)
cancel(now) / close(now)
queue_reliable(bytes)
submit_unreliable(bytes)
poll_event()
```

`update()` is single-threaded and bounded. It processes at most eight received
datagrams and sends at most one packet by default; hard maxima are 64 and 8.
Wrong-endpoint traffic is ignored before parsing and cannot refresh activity.
Duplicate and older numeric sequences are ignored without mutation after a
bounded header peek but before body decode or reassembly. An exact half-range
sequence is a typed protocol error.

Standalone driver consumers receive const session/reassembler inspection only.
`NetchanBootstrapStage::persistent_session()` is a narrow compatibility seam
that becomes available only after the first owning payload and all required ACK
commits; it does not expose mutable reassembly state or transfer socket
ownership.

For an admitted fragment the driver reserves all required event capacity,
prepares reassembly and session inspection, commits both owning states, attempts
the transport ACK, and only then publishes ordered metadata/owning events. ACK
failure is terminal and clears the committed channel/reassembly state before
any payload event is exposed. Exact fragment
retransmission advances the numeric channel observation and sends the current
ACK without toggling or inserting the reliable unit again. One admitted
fragment/update can publish at most one completed normal transfer followed by
at most one bounded contemporaneous suffix. They are separate owning
`payload_ready` events, with completion first.
`reliable_payload_acknowledged` is emitted only when the final outstanding
reliable unit clears, not for every intermediate fragment ACK. Other typed
events cover normal-transfer start/completion/fixed-deadline timeout, the
pending secondary stream, channel timeout, cancellation, and network/protocol
terminal outcomes.

The event queue defaults to 16 entries, has a minimum valid capacity of 5 for
the worst one-datagram event set, and a hard maximum of 256. Backpressure
becomes terminal before a packet can partially mutate session/reassembly state.
Traces are metadata-only: endpoint, sizes,
sequence/ACK numbers, flags, coverage, and counts; no datagram or opaque payload
bytes are exposed. Diagnostic context has a 256-byte hard presentation cap.

Driver safety defaults and hard maxima include:

| Driver boundary | Default | Hard maximum |
| --- | ---: | ---: |
| channel inactivity | 30 seconds | 300 seconds |
| fragment transfer deadline | 5 seconds | 30 seconds |
| datagram / fragment datagram | 4,096 bytes | 16,384 bytes |
| selected fragment bytes | 1,024 bytes | 1,024 bytes |
| owning opaque/normal payload | 65,536 bytes | 1,048,576 bytes |
| one-shot unreliable payload (also bounded by configured datagram body) | 4,088 bytes | 16,376 bytes |
| received datagrams per update | 8 | 64 |
| outgoing packets per update | 1 | 8 |
| queued events | 16 | 256 |

Timeout, cancellation, network/protocol failure, backpressure, and explicit
close converge on idempotent cleanup: reliable/fragment/unreliable bytes are
released, prepared plans become stale, and the optional lifetime is destroyed
exactly once. The driver does not own or close the externally supplied socket.

`INetchanDriverLifetime` is authentication-agnostic. The client-layer
composition moves the emptied authentication session behind this boundary so
the bootstrap stage/coordinator can keep its provider/session guard alive
through the driver terminal without giving the netchan target an authentication
type or bytes. An embedding composition may own the same driver persistently;
the current CLI intentionally stops after its first complete opaque payload.

## Remaining compatibility work

The following stay explicit rather than being inferred:

- true unseen out-of-order stock delivery and replacement while incomplete;
- an accepted two-run old-after-completion replay scenario;
- slot-1 semantics, file/download naming, file persistence, and simultaneous
  slot-0/slot-1 traffic;
- compression markers/framing beyond the stock-confirmed first M2.4.1
  `BZ2\0` service envelope, and universal stock compression behavior;
- stock-client multi-fragment C2S transfer shape, retry order, ACK clearing, and
  interaction with contemporaneous suffix bytes;
- same-range/same-bytes replay under a fresh admissible sequence;
- split/special packets and acknowledgement bit 30;
- a production Steam authentication provider and live project-client-to-stock
  HLDS acceptance/channel proof;
- every service semantic beyond the neutral opcode-13 boundary and every
  resource, snapshot, gameplay, and rendering consumer. M2.4.1 consumes the
  reassembled first `BZ2\0` envelope through its typed opcode-11 stop; M2.4.2
  parses the confirmed server-info continuation; M2.4.3 publishes the bounded
  delta registry; M2.4.4 publishes the typed opcode-44 movement/environment
  state and leaves the later complex body untouched.

No pending row is filled from a third-party implementation name. Fresh bounded
capture takes priority over earlier assumptions and secondary behavioral
cross-checks.
