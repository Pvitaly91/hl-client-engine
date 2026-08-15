# GoldSrc Protocol 48 netchan profile

## Scope and evidence status

This document records the M2.3.2 persistent unfragmented reliable transport
profile derived independently from bounded black-box observations. Payloads
remain owning and opaque. The profile does not decode `svc_*`, assign sign-on
meaning to any payload byte, load a map, create a resource, or update
`ClientWorldState`.

Every interoperability statement uses one of these labels:

- **Stock-confirmed** — repeated observation of the signed stock
  Valve client and stock Valve HLDS through a byte-transparent bounded relay.
- **Project fail-closed** — a conservative rule used where the exact stock
  matrix row could not be isolated; it preserves state instead of accepting an
  ambiguous ACK, packet, or transition.
- **Project deterministic/secondary-inferred** — deterministic project behavior
  exercised with independent fixtures or state-machine tests and, where noted,
  informed by a secondary behavioral cross-check; it is not claimed as a
  stock-captured engine rule.
- **Pending** — not established by the captures and unsupported by this
  profile.

The current overall evidence status is deliberately split:

| Path | Status |
| --- | --- |
| stock client through relay to stock HLDS | **Stock-confirmed:** M2.3.1 base wire plus the M2.3.2 reliable behaviors explicitly listed below |
| project client to deterministic fake HLDS | **Project deterministic/secondary-inferred:** the same-socket M2.3.1 bootstrap/first ACK and the exact M2.3.2 reliable UDP scope documented below pass; loss, lost-ACK, and A/B remain deterministic driver tests |
| project client to stock HLDS | **Pending:** no production Steam authentication provider exists, so live stock acceptance/bootstrap is not claimed |

M2.3.2 is complete for the bounded stock evidence and the persistent,
transport-independent unfragmented reliable session. That status does not turn
the pending live project-to-stock path into a compatibility claim. M2.3.3 is the
next milestone and retains the fragment boundary; M2.4 sign-on remains later.

## Compatibility profile and methodology

**Stock-confirmed:** the reference client was signed Valve
`hl.exe` 1.1.1.1, Steam App 70 build ID 15961492. The reference server was
stock Valve `hlds.exe` 1.1.2.2, Protocol 48, build 10210. Transport was IPv4
UDP on loopback with separate client-facing and server-facing relay sockets.
The relay preserved one upstream socket and source endpoint through challenge,
connect, `ACCEPT`, and sequenced traffic. Capture time, packet count, and byte
count were bounded.

**Stock-confirmed:** the M2.3.1 base-wire research set contains six controlled
sessions: two passive, one drop-first-server-sequenced, one
duplicate-first-server-sequenced, and two
reorder-first-two-server-sequenced runs. Raw captures were used only as local
evidence; the committed wrapper and documentation contain bounded metadata,
not those datagrams.
Raw datagrams, authentication material, client identity data, and process logs
remain only under ignored `manual-artifacts/netchan-captures/`; none is a Git
fixture or documentation attachment. Only structural metadata and independently
constructed golden vectors may enter the repository.

The opt-in wrapper is:

```powershell
.\scripts\verify_stock_netchan_capture.ps1 `
  -ClientPath C:\Games\Half-Life\hl.exe `
  -HldsPath C:\Servers\Half-Life\hlds.exe `
  -RelayPath C:\Tools\bounded-netchan-relay.ps1 `
  -Game valve -Map boot_camp -Port 27128 `
  -CapturePacketCount 64 -CaptureByteLimit 1048576 -TimeoutSeconds 30
```

It requires an explicitly supplied bounded relay; it is not a proxy
implementation. A successful wrapper run means only that the relay exited
successfully and emitted bounded-completion metadata matching the wrapper's
limits. It does not itself assert packet semantics or print captured bytes.

The M2.3.2 reliable research set contains exactly 16 primary
`bounded_complete` sessions, two per scenario:

| Scenario | Runs | Purpose |
| --- | ---: | --- |
| baseline with consecutive generations | 2 | presence and ACK-generation transitions |
| drop first client reliable | 2 | first retransmission trigger and canonical body reuse |
| drop first client reliable and suppress every server sequenced packet | 2 | bounded no-ACK/no-time-only-retry control |
| drop first server ACK | 2 | lost-ACK clearing behavior |
| duplicate first client reliable | 2 | duplicate wire/state admission |
| replay a stale server ACK after the second reliable generation | 2 | stale generation isolation |
| drop the second distinct client reliable | 2 | next-generation retransmission and fresh-key remunge |
| drop the first two transmissions of one reliable generation | 2 | `first_sent` versus `most_recent_sent` trigger basis |

Three additional `bounded_complete` baseline runs exercised the verifier end to
end; their summaries also satisfy the strengthened baseline action/accounting
rules. The opt-in verifier is:

```powershell
.\scripts\verify_stock_reliable_netchan_capture.ps1 `
  -RelayPath C:\Tools\bounded-reliable-netchan-relay.ps1 `
  -HalfLifePath C:\Games\Half-Life\hl.exe `
  -HldsPath C:\Servers\Half-Life\hlds.exe `
  -Game valve -Map boot_camp -Port 27320 `
  -Scenario drop-first-client-reliable -TimeoutSeconds 30
```

`-Scenario` accepts only `baseline`, `drop-first-client-reliable`,
`drop-first-server-ack`, `duplicate-client-reliable`, and `delay-stale-ack`.
The wrapper requires private loopback, exact endpoints, one upstream socket,
Valve-signed reference executables, hard packet/byte/time bounds, a
scenario-specific action set, and the exact metadata schema
`hlclient.stock-reliable-netchan-metadata.v1` with
`raw_packet_bytes_stored=false`. Raw datagrams, authentication/identity bytes,
opaque bodies, and process logs are neither emitted by the wrapper nor tracked.
The relay remains user-supplied; successful schema validation is not by itself
a claim about payload semantics.

## Post-`ACCEPT` order and initial state

**Stock-confirmed:** the client sends the first sequenced datagram
after connectionless `ACCEPT`.

Two passive runs differed only in the number of client padding transmissions
scheduled before the first server packet:

```text
legacy observation: C->S sequence 1, 2, 3, then S->C sequence 1
fresh observation:  C->S sequence 1, 2, 3, 4, then S->C sequence 1
```

The number of pre-server padding sends is therefore scheduling-dependent; it
is not a fixed wire constant.

**Stock-confirmed:** the first client word was raw
`0x80000001` (numeric sequence 1 with reliable-present set) and its
acknowledgement word was zero. Its decoded body remains opaque and is not
hardcoded as a protocol command. Later pre-server client packets used increasing
numeric sequences and the minimum decoded padding body described below.

**Stock-confirmed:** the first server packet used numeric sequence
1 with reliable-present and fragments-present set. Its acknowledgement named
the latest processed client sequence and carried reliable acknowledgement 1.
Both channel directions begin from numeric baseline 0, next outgoing sequence
1, and reliable toggles 0.

**Project deterministic/secondary-inferred:** because the stock client's first
reliable body is opaque sign-on/application content, the project does not copy,
hardcode, or transmit it. The M2.3.1 fake-HLDS profile deliberately sends the
first server sequenced packet after `ACCEPT`; the project waits for that packet
and sends only the synthetic transport acknowledgement documented below. This
is not a claim that the project substitutes for the stock client's client-first
message-level content on a live stock server.

## Base header and flags

**Stock-confirmed:** both directions use the same untransformed,
eight-byte base header. There is no client-only qport field and no checksum or
validation byte in this captured Protocol 48 profile.

| Decoded offset | Width | Encoding | Meaning | Evidence |
| ---: | ---: | --- | --- | --- |
| 0 | 4 | little-endian `uint32` | sequence word | **Stock-confirmed** |
| 4 | 4 | little-endian `uint32` | acknowledgement word | **Stock-confirmed** |
| 8 | 0 | — | qport absent | **Stock-confirmed** |
| 8 | 0 | — | checksum absent | **Stock-confirmed** |

Sequence word masks:

| Bits | Mask | Meaning | Evidence |
| --- | ---: | --- | --- |
| 0..29 | `0x3fffffff` | numeric sequence | **Stock-confirmed** |
| 30 | `0x40000000` | fragment descriptors present | **Stock-confirmed** |
| 31 | `0x80000000` | reliable payload is present in this packet; not a generation bit | **Stock-confirmed** |

Acknowledgement word masks:

| Bits | Mask | Meaning | Evidence |
| --- | ---: | --- | --- |
| 0..29 | `0x3fffffff` | acknowledged numeric sequence | **Stock-confirmed** |
| 31 | `0x80000000` | receiver's acknowledged reliable generation/toggle | **Stock-confirmed** |
| 30 | `0x40000000` | unsupported/reserved in this profile | **Pending:** never observed; the project rejects it |

Connectionless `0xffffffff` is classified before this decoder. The
`0xfffffffe` special/split marker is kept out of normal netchan parsing by a
conservative project classifier. That split-marker policy is **project
fail-closed**; split packet decoding is pending.

## Payload transform

**Stock-confirmed:** bytes 0 through 7 are never transformed. Every
byte from offset 8 onward is in the transform range. Only complete four-byte
words are transformed; a trailing zero to three bytes remains unchanged. The
key is the low eight bits of the packet's numeric sequence. Encoding and
decoding apply in both directions.

The independently reconstructed operation is COM_Munge2-shaped; that name is
descriptive and does not make a source-code provenance claim. Its 16-byte table
is:

```text
05 61 7A ED 1B CA 0D 9B 4A F1 64 C7 B5 8E DF A0
```

For each little-endian 32-bit word `v` at zero-based transformed word index
`i`, with `k = numeric_sequence & 0xff`, encoding is:

```text
v ^= ~uint32(k)
v = byte_swap_32(v)
for byte j in 0..3:
    byte_j(v) ^= A5 | (j << j) | j | table[(i + j) & 15]
v ^= uint32(k)
```

Decoding reverses those operations. All arithmetic is unsigned and exactly
32-bit. **Project deterministic/secondary-inferred:** independent golden vectors
cover both directions, different keys, more than one word, and unchanged
tails. One safe structural vector is that a decoded eight-byte body of eight
`0x01` padding bytes under numeric sequence 2 transforms to
`59 19 01 03 19 01 11 43`; it contains no authentication or sign-on data.

Decoded 16-byte padding-only datagrams prove the offset and also rule out a
hidden qport/checksum: eight header bytes followed by eight decoded `0x01`
bytes.

## Fragment descriptor observations and the M2.3.3 boundary

**Stock-confirmed:** when sequence bit 30 is set, the transformed
body decodes into two ordered descriptor slots. Each slot starts with an exact
presence byte. A present slot continues as:

```text
present:u8 = 1
fragment_id:u32 little-endian
offset:u16 little-endian
length:u16 little-endian
```

An absent slot is the single byte `0`. In the observed server packets, slot 0
was present, slot 1 was absent, and opaque fragment bytes began at datagram
offset 18 after transform decoding.

The captured slot-0 transfer was:

| Fragment ID | One-based index | Total | Offset | Length |
| ---: | ---: | ---: | ---: | ---: |
| `0x00010005` | 1 | 5 | 0 | 1,024 |
| `0x00020005` | 2 | 5 | 0 | 1,024 |
| `0x00030005` | 3 | 5 | 0 | 1,024 |
| `0x00040005` | 4 | 5 | 0 | 1,024 |
| `0x00050005` | 5 | 5 | 0 | 90 |

Thus the high 16 bits of `fragment_id` are the one-based index and the low 16
bits are the total count for this captured profile. Offline analysis of indices
1 through 5 produced an opaque 4,186-byte message. Repeated and perturbation
runs produced the same length and digest without retaining that payload in the
repository.

**Project fail-closed:** M2.3.2 preserves only strict
recognition of the fragment flag and confirmed descriptor boundary. A
fragmented first packet returns the typed terminal
outcome `fragmented_payload_pending_m2_3_3`; it never becomes
`netchan_bootstrap_complete`. Production code does not accumulate, order,
deduplicate, concatenate, or expose fragment bodies as a complete payload.

The observed traffic establishes only that slot 0 carried the captured opaque
message. **Pending:** complete normal-stream behavior, stock semantics for slot
1, and stock file-stream traffic. M2.3.3 will define bounded normal/file
fragmentation and reassembly. Until then no remote filename or path is derived,
no fragment transfer is retained, and nothing is written to disk.

## Reliable presence, generation, retransmission, and clearing

**Stock-confirmed:** acknowledgement is always the second header word; there
is no separate ACK opcode. It may ride a 16-byte padding-only send or an
ordinary payload send. A generic small/empty sequenced packet has an eight-byte
decoded body; the independent M2.3.1 fixture proves those eight transport
padding bytes are `0x01`. They are not interpreted as queued application
reliable content.

### Presence versus generation

**Stock-confirmed:** sequence-word bit 31 means that reliable payload bytes are
present in that particular packet. It is not the reliable generation. Baseline
run A carried distinct reliable bodies at client sequences 1, 73, 136, and 141;
baseline run B used 1, 78, 136, and 144. The server acknowledged those four
messages with acknowledgement bit 31 values `1, 0, 1, 0` in each run. Thus the
alternating generation is represented by the acknowledgement high bit, while
the sequence high bit returns to zero on intervening packets without reliable
bytes and is set again on every new send or retransmission that carries them.

The earlier server-fragment observations agree: client reliable
acknowledgements alternated `1, 0, 1, 0, 1`. Their observed 20–54 ms immediate
ACKs and roughly 354 ms piggyback are measurements, not protocol deadlines.

### ACK-gap retransmission and canonical bytes

**Stock-confirmed:** retransmission is ACK-gap-driven at the next transmit
opportunity. In two drop-first-client-reliable runs, client sequence 1 was
dropped and the identical decoded canonical body reappeared at sequence 16 or
20 with sequence bit 31 set. In the two no-server-sequenced controls, no retry
occurred across 134 or 138 client sequenced packets and 1.329041 or 1.352336
seconds. These bounded controls rule out an autonomous time-only retry in the
observed profile; the measured delays are not a reusable timer constant.

**Stock-confirmed:** in the captured non-wrap sequence range, the repeated
trigger is an admitted newer peer packet whose numeric ACK itself advances past
the current in-flight `most_recent_sent_sequence` while its reliable-ACK
generation mismatches the in-flight generation. A requested retry remains
pending until the next explicit transmit opportunity. The resent packet keeps
the same generation and presence set, but uses a fresh numeric sequence.

**Project deterministic/secondary-inferred:** the session generalizes that
ordering with its low-30-bit wrap-safe comparator. The exact stock trigger at
sequence wrap remains **pending** rather than being inferred from the low-range
captures.

**Stock-confirmed:** `first_sent_sequence` is the lifecycle origin and does not
move; `most_recent_sent_sequence` advances after every successful copy. In the
two double-drop runs, ACK 15 did not request another copy after latest send 16,
whereas ACK 24 did; the second run likewise distinguished ACK 18 from latest 19
and triggered only at ACK 29. The trigger therefore compares against the most
recent successful send, not merely the first.

**Stock-confirmed:** the second-distinct-message drop runs reproduced a 37-byte
canonical decoded body under a new numeric sequence and transform key. The
project consequently retains owning, unencoded reliable bytes and applies the
normal sequence-keyed transform afresh for every committed send; it never
retains a munged datagram as the reliable message.

### Clearing, stale ACKs, and duplicates

**Stock-confirmed:** every observed successful clear had both the matching
reliable-ACK generation and numeric coverage of
`most_recent_sent_sequence`. When the first matching server ACK was dropped in
two runs, the next covering same-generation ACK arrived and no initial body was
resent; a later distinct body then used the next generation. A replayed stale
server datagram from generation 1 did not clear the active generation-0 message;
the later correct generation-0 ACK did.

**Project fail-closed:** a matching-generation ACK numerically at or after
`first_sent_sequence` but before `most_recent_sent_sequence` could not be
isolated with a byte-preserving stock relay. Once stock accepts a copy, its
matching ACK naturally covers that accepted copy, and replaying an older server
datagram is rejected by the server packet sequence before ACK mutation. The
project therefore clears only from an admitted newer peer packet whose ACK
observation advances, whose generation matches, and whose numeric ACK equals or
is wrap-newer than `most_recent_sent_sequence`. Wrong-generation, stale,
non-covering, future, and exact-half-range ACKs preserve the in-flight message;
future and half-range cases are typed errors.

**Stock-confirmed:** forwarding the exact first client reliable datagram twice
changed the server reliable-ACK generation only once and left it stable until
the next distinct generation. This proves duplicate-once wire/state behavior;
because the bodies remained opaque, it does not claim an independently parsed
application command executed exactly once.

**Project deterministic/secondary-inferred:** receive inspection first filters
duplicate, older, malformed, and fragmented packets. Only a committed newer,
unfragmented packet with sequence presence bit 31 set toggles the independent
incoming reliable-ACK generation and reports new opaque reliable data.
Duplicate/older packets never toggle or deliver. A byte-identical reliable body
reappearing under a newer sequence after it was already accepted could not be
naturally isolated in the stock profile; this exact same-body/newer-sequence
case remains **pending** as a stock claim.

### M2.3.1 first-ACK compatibility fixture

**Project deterministic/secondary-inferred:** the deterministic fake-HLDS
fixture sends server numeric sequence 1 with the reliable-present flag,
acknowledgement 0, and an unfragmented opaque payload. The project replies
exactly once with this synthetic transport-only packet:

```text
decoded header: sequence=1, no sequence flags
                acknowledgement=1, reliable-ack bit 31 set
decoded body:   01 01 01 01 01 01 01 01
wire bytes:     01 00 00 00 01 00 00 80 5A 19 01 00 1A 01 11 40
```

The first eight wire bytes are the untransformed base header; the final eight
bytes are the sequence-key-1 transform of the padding-only decoded body. There
is no qport, checksum, `new`, `clc_*`, `svc_*`, or other sign-on payload. These
bytes are an independent project/fake fixture, **not** a stock-captured client
packet. This compatibility primitive remains separate from the persistent
M2.3.2 outgoing transaction lifecycle.

## Project safety and timeout policy

These are **project deterministic/secondary-inferred** safety limits, not
claims about original engine maxima:

| Boundary | Default | Hard maximum |
| --- | ---: | ---: |
| decoded/encoded netchan datagram | 4,096 bytes | 16,384 bytes |
| decoded unfragmented body: reliable plus current unreliable | 4,088 bytes | 16,376 bytes |
| one unfragmented in-flight canonical reliable message | 4,088 bytes | 16,376 bytes |
| accumulated pending-next reliable bytes | 16,376 bytes | 16,376 bytes |
| fragment-transfer bytes retained/reassembled | 0 bytes | 0 bytes |
| first-packet wait | 5 seconds | 30 seconds |
| received datagrams per `update()` | 8 | 64 |
| outgoing packets per `update()` | 1 | 8 |

The configured datagram budget has a hard minimum of 16 bytes: the confirmed
8-byte base header plus the project-required minimum 8-byte decoded padding
body. Configurations below that size, an unfragmented reliable limit larger
than the body budget, or a pending limit outside the fixed hard ceiling are
rejected before use. A pending body may be larger than the configured
unfragmented limit; it remains pending and produces
`requires_fragmentation_pending_m2_3_3` rather than being truncated.

Updates must poll a bounded number of datagrams, stop on `would_block`, ignore
wrong-endpoint traffic before parsing, and use injected monotonic time. There
is no background network thread or production sleep. Cancellation, timeout,
network failure, malformed packets, and a fragmented-payload-pending outcome
are distinct terminal results.

**Stock-confirmed:** post-`ACCEPT` datagram sizes included 16, 45,
108, and 1,042 bytes. Those observations inform fixtures but do not replace the
project limits.

## Persistent session ownership and transactions

**Project deterministic/secondary-inferred:** the session has one bounded
pending accumulator and at most one owning in-flight message. Repeated
`queue_reliable()` calls append deterministically to pending bytes. If message A
is in flight, later message B remains in the pending-next accumulator and never
modifies A. B is eligible only after A clears and a later explicit transmit
opportunity occurs; there is no unbounded queue and receive commit never sends B
implicitly.

Outgoing state changes are transactional:

```text
queue/committed state
    -> prepare_outgoing_packet()       (read-only plan)
    -> encode and attempt UDP send
       |-> failure: abandon plan; sequence/toggle/A/B remain unchanged
       `-> success: commit_outgoing_send()
           -> advance numeric sequence
           -> promote pending to in-flight, or update retry latest/send-count
```

A stale, foreign, already-consumed, or state-mismatched plan fails without a
partial transition. Promotion moves the owning canonical bytes and flips the
outgoing generation exactly once on successful commit. Retransmission preserves
that generation and `first_sent_sequence`, advances
`most_recent_sent_sequence`, and remunges the canonical body with the newly
committed sequence key.

M2.3.2 intentionally has no production post-bootstrap polling scheduler or
timeout owner. The application/coordinator reaches its terminal
`--stop-after netchan-bootstrap` outcome; an embedding owner that continues the
non-owning session access must drive transmit/receive opportunities and call
`clear_reliable_state()` on timeout, cancellation, network failure, or protocol
failure. Table-driven tests map each of those terminal outcomes to the same
bounded cleanup primitive; this is not a claim that the current coordinator
runs a post-bootstrap timeout loop.

**Project deterministic/secondary-inferred:** when both forms are present, the
decoded packet body is the reliable prefix followed by the current one-shot
unreliable suffix. Only the reliable prefix is retained; a later retry may carry
different current unreliable bytes. The bounded stock runs did not isolate this
opaque boundary—the 37-byte retry bodies were wholly equal—so this byte order is
not labeled stock-confirmed. If either component or the combined body does not
fit, preparation returns a typed error and neither bytes nor sequence state are
lost.

**Project deterministic/secondary-inferred:** all numeric sequence decisions
use the low 30 bits modulo `0x40000000`; flag bits never enter comparison. Pure
tests cover a first reliable send before wrap, a retry after wrap, a covering
ACK after wrap, stale pre-wrap coverage, rejection of a future post-wrap ACK
without mutation, pending B while A crosses the wrap, promotion of B at sequence
2 with the opposite generation, and B's final clear. The exact half-range
remains fail-closed as ambiguous.

## Layering and opaque runtime boundary

The M2.3.2 layering is:

```text
connectionless ACCEPT
    -> same IDatagramTransport and UDP socket
    -> netchan packet codec and payload transform
    -> coordinator-owned persistent NetchanSession
       -> wrap-safe sequence/ACK inspection and atomic receive commit
       -> bounded pending B plus one in-flight A
       -> transactional new send/retry/ACK clearing
       |-> fragmented descriptor boundary: pending M2.3.3, no ACK/completion
       `-> owning unfragmented opaque payload
           -> exactly one minimal transport acknowledgement
           -> current CLI terminal netchan-bootstrap result
```

The codec owns byte layout only. The session owns reliable sequence/generation,
pending/in-flight canonical bytes, and transaction state, but no socket,
fragment transfer, authentication, message parser, world, or renderer. The
bootstrap stage owns exact remote-endpoint validation, unchanged local endpoint,
polling, cancellation, and timeouts. The coordinator hands the already-open
transport to bootstrap and retains the resulting session.

The deterministic fake-HLDS UDP integration reuses that exact transport,
source endpoint, and coordinator-owned session after the full
challenge/connect/`ACCEPT`/byte-exact-first-ACK path. It proves one canonical
client reliable send, a covering ACK clear with no extra transmission, one
owning server reliable marker with the correct outgoing ACK bit, and duplicate
plus older delivery exactly once. Lost-packet, lost-ACK, and pending A/B paths
remain deterministic session/driver tests rather than additional real-UDP
claims.

The opaque payload is not a `ClientWorldState` field and never reaches a
renderer. The stock client's first reliable body and the captured fragmented
server body remain opaque; neither is hardcoded and neither is scanned for
`svc_*` values. The public application CLI still stops after netchan bootstrap
and exposes no raw reliable-send option. M2.3.3 fragmentation/reassembly is the
next milestone; M2.4 then defines the initial sign-on state machine.

The public M2.3.2 boundary is project-owned and typed:

- `NetchanSequence`, `NetchanSequenceFlags`, and wrap-safe comparison helpers;
- `NetchanDatagramClassification`, `NetchanDirection`, `NetchanHeader`, and
  strict direction-specific packet encode/decode results;
- pure `encode_netchan_payload` / `decode_netchan_payload` transforms;
- `NetchanSequenceState`, `NetchanAcknowledgementObservation`,
  `ReliableTransmitDecision`, and wrap-safe ACK dispositions;
- persistent `NetchanSession`, owning `InFlightReliablePayload`, move-only
  incoming inspection, and move-only outgoing transmit plan/commit boundary;
- `NetchanBootstrapConfig`, `NetchanBootstrapStage`, typed terminal state/error,
  `OwnedNetchanPayload`, and `NetchanBootstrapResult`.

`OwnedNetchanPayload` owns its bytes and records source sequence,
acknowledgement, confirmed sequence/acknowledgement flags, direction, and
receive time. It contains no UDP-buffer pointer, renderer/world state, or
parsed sign-on value.

## Pending and unsupported behavior

The following remain **pending** in this profile:

- project-client live `ACCEPT` and bootstrap against stock HLDS;
- stock behavior exactly at sequence wrap and half-range ambiguity;
- acknowledgement bit 30;
- split/special packet decoding and public protocol extensions;
- stock isolation of reliable-prefix/unreliable-suffix ordering;
- stock behavior for a matching-generation ACK between first and latest send;
- stock behavior for an already-accepted identical reliable body under a newer
  packet sequence;
- M2.3.3 normal/file fragmentation, reassembly, slot-1 semantics, and every
  persistence policy;
- `svc_*`, serverdata, signon, resource, snapshot, command, and gameplay
  parsing.

No pending item is filled from a third-party field name or implementation.
Capture observations have priority over secondary behavioral cross-checks.
