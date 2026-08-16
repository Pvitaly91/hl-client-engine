# GoldSrc initial sign-on boundary

## Scope

M2.4.1 adds the first semantic layer above the persistent GoldSrc netchan:

```text
connectionless ACCEPT
    -> same-socket NetchanDriver
    -> one typed reliable client request
    -> normal-stream reassembly
    -> BZ2-NUL service envelope
    -> bounded service-message stream
    -> first complex-message boundary
    -> stop
```

The milestone does not parse the boundary body, server information, resource
lists, delta descriptions, snapshots, or gameplay. It never executes server
text and sends no resource or spawn continuation.

Facts below use three labels:

- **stock-confirmed**: repeated observations of a signed Valve client and
  signed Valve HLDS through a byte-preserving private-loopback relay;
- **project policy/tested**: a bounded, deterministic behavior enforced by the
  project and its synthetic/fake-HLDS tests;
- **pending**: no compatibility behavior is implemented or claimed.

## Stock reference and capture method

The signed launchers reported Valve `hl.exe` VERSIONINFO 1.1.1.1 and Valve
`hlds.exe` VERSIONINFO 4.1.1.1; the local Steam App 70 manifest was build
15961492. The server's separately observed engine/protocol profile was
executable 1.1.2.2, Protocol 48, build 10210. Each run used private
IPv4 loopback ports, one immutable learned client endpoint, one upstream socket,
explicit signed binary paths, hard packet/byte/time limits, and byte-preserving
relay actions. Only structural metadata was retained. Raw datagrams,
authentication bytes, identity material, server text, decompressed payload
bytes, and game assets were neither logged nor committed.

The accepted primary research set contains twelve bounded runs:

| Scenario | Accepted | Result |
| --- | ---: | --- |
| baseline | 6/6 | stable request, envelope, service order, offsets and sizes |
| drop initial client request | 2/2 | one driver-owned retransmission with a fresh sequence; no semantic requeue |
| drop the first request-covering server datagram | 2/2 | the next padding-only server packet repeated the covering ACK; the dropped first fragment retried later; no client request requeue/retry |
| duplicate the complete five-datagram server batch | 2/2 | old packet sequences did not produce a second downstream semantic continuation |

Every accepted run passed post-cleanup process and selected-port checks and
produced one metadata document with `raw_packet_bytes_stored=false`. The tracked
`scripts/verify_stock_initial_signon.ps1` is a reproducer/validator for a
user-supplied bounded relay; raw research artifacts remain below ignored
`manual-artifacts/signon-captures/`.

## Exact initial client request

The stock-confirmed semantic client message is exactly five bytes:

```text
03 6E 65 77 00
```

Its fixed-profile representation is:

| Offset | Value | Meaning |
| ---: | --- | --- |
| 0 | `03` | captured client string-command opcode |
| 1..3 | `6E 65 77` | lowercase ASCII `new` |
| 4 | `00` | required NUL terminator |

`InitialSignonRequestBuilder` has no arbitrary string argument. The strict
test/capture parser rejects the wrong opcode, empty or alternate command,
embedded NUL, CR/LF, missing terminator, and trailing data.

The semantic codec does not own transport padding. The first stock netchan
canonical body was eight bytes:

```text
03 6E 65 77 00 01 01 01
```

The last three `01` bytes are the existing Protocol 48 minimum netchan padding.
They are added by the transport builder and are not part of the client-message
fixture.

In all baselines the request was a separate reliable-present client packet at
numeric sequence 1, acknowledgement 0. The internal outgoing reliable
generation became 1. A server acknowledgement covering that client sequence
with reliable-ack generation 1 completed the request lifecycle. The first
server fragment naturally carried that covering acknowledgement before the
five-fragment service transfer completed.

In the two dropped-request runs, the canonical request body was retransmitted
unchanged under a fresh numeric sequence (20 or 21 in those bounded traces);
only the retry was forwarded. In the two dropped-ACK runs, the request stayed
queued/sent once. The dropped S2C sequence-1 datagram contained both fragment 1
and the covering reliable acknowledgement. An unfragmented eight-byte transport
body at S2C sequence 2 repeated the same covering generation and cleared the
client request; the missing fragment itself was retransmitted unchanged later
at fresh S2C sequence 4 or 9. These are normal M2.3.2 ACK-gap/reliable
semantics; `InitialSignonStage` has no timer or second request queue.

## Service envelope

The first complete normal-stream payload was stock-confirmed in every run as
4,186 bytes:

```text
42 5A 32 00 | one standard bzip2 stream
   BZ2 NUL  | 4,182 bytes, no trailing input
```

Strict in-memory decompression produces exactly 7,480 owning bytes in the
reference profile. `ServicePayloadEnvelopeDecoder` requires the exact `BZ2\0`
magic followed by one `BZh1`..`BZh9` stream, uses the pinned bzip2 1.0.8
streaming decoder, caps output before publication, and rejects missing,
truncated, corrupt, oversized, or trailing compressed data. It has no path or
stdio API and publishes nothing on failure.

Envelope framing is separate from the service opcode stream: byte `0x42` in
`BZ2\0` is not a service-message opcode.

## Confirmed service stream and boundary

The decompressed stream is byte-aligned and has no observed separate
service-stream total-length prefix. Its stable first order across all accepted
runs was:

```text
offset 0:  opcode 8 + 40-byte server text + NUL
offset 42: opcode 11 + complex body owned but not parsed
```

Opcode 8 is exposed under the neutral profile name `text_control`. Its only
implemented wire rule is one bounded NUL-terminated owning string. Captures
recorded length 40, four control bytes, and no ESC, but deliberately retained no
text. The project does not infer or execute a console/shell semantic from it.

Opcode 11 is exposed under the neutral name `complex_signon_boundary`.
`ServiceMessageBoundary.byte_offset` is the position of its opcode (42 in the
stock profile), while `remaining_byte_count` counts the unconsumed body after
the opcode (7,437 bytes). The owning `boundary_payload` retains the decompressed
bytes only inside the sign-on result. No boundary-body byte is interpreted or
passed to rendering/world state.

Whether a resource-list opcode occurs inside or after that complex body is not
determined in M2.4.1. Because the body has no confirmed length yet, the opaque
remainder is not scanned for opcode-looking bytes and neither resource presence
nor resource absence is claimed.

An unknown opcode before the expected boundary fails closed. The decoder does
not scan for a later known value or guess an unknown message length. A payload
containing only confirmed simple messages is a valid nonterminal batch; the
stage waits boundedly for another owning payload.

## Ownership and stage lifecycle

`hlclient_goldsrc_signon` contains the pure request codec, envelope decoder,
service stream decoder, and `InitialSignonStage`. It depends on core GoldSrc,
network, netchan, and the private bzip2 decoder target. It has no renderer, SDL,
asset, world-state, or filesystem dependency.

For `--stop-after signon-boundary`, `GoldSrcHandshakeCoordinator` creates the
stage immediately after connectionless `ACCEPT`. The stage owns one persistent
`NetchanDriver` over the coordinator's already-bound external transport. It
queues the five semantic bytes once, lets the session own all retransmission,
buffers at most one owning service payload before the matching request ACK,
decodes bounded batches, and closes at the boundary. The driver lifetime guard
keeps `AuthenticationSession` alive through every sign-on terminal path and
releases it exactly once.

Public stage states cover idle, request transmit/ACK wait, server-payload wait,
decode, boundary success, timeout, cancellation, network/protocol failure,
unsupported service input, event backpressure, and the neutral secondary-stream
pending-M3 boundary. Terminal calls are idempotent.

No distinct disconnect service opcode appeared before the boundary in any of
the accepted stock runs. The decoder therefore does not guess one from SDK or
third-party names: an unconfirmed opcode fails closed as unsupported, and a
separate disconnect-control outcome remains evidence-gated.

The sign-on event ring stores metadata only and never silently drops an event.
Publication is preflighted before semantic state commit; insufficient capacity
produces a typed backpressure outcome. Trace events may expose opcode, offset,
counts, sizes, endpoints, and transmit counts, but never request/auth bytes,
server text, compressed/decompressed payload, or the boundary remainder.

## Project limits

These are project safety policies, not claims about stock maxima:

| Limit | Default | Hard cap |
| --- | ---: | ---: |
| initial command | fixed 3 bytes | 64 bytes for strict parser rejection tests |
| compressed service envelope | driver/profile bounded | 1 MiB |
| decompressed service payload | 65,536 bytes | 1 MiB |
| service string | 1,024 bytes | 4,096 bytes |
| messages per payload | 64 | 256 |
| service payload accepted by stream decoder | 65,536 bytes | 1 MiB |
| sign-on events | 32 | 256 |
| driver events consumed per update | 32 | 64 |

Every configurable limit is positive, validated before start, and covered at
the exact limit and limit plus one. Network parsing uses explicit lengths and a
bounded NUL search; it does not use `strlen`, `strcpy`, or `sscanf` on packet
data.

## Text and command security

Server text is owned and bounded. Its optional presentation sanitizer escapes
ESC, CR, LF, tab, backslash, and every non-printable byte without splitting an
escape token; output is capped and carries an explicit truncation marker.
Runtime trace does not print text by default.

There is no public arbitrary `stringcmd` builder, raw service injection option,
server-command callback, console dispatcher, shell call, URL opener,
filesystem action, or renderer/world-state handoff in this layer. Unconfirmed
command-like opcodes are rejected as unsupported data. The CLI explicitly
rejects raw/injection/bypass spellings covered by its tests.

## Deterministic project proof

The production loopback fake-HLDS path uses one UDP socket/source endpoint for
challenge, connect, `ACCEPT`, the exact client-first request, ACKs, fragments,
and boundary completion. Independent server fixtures validate the canonical
request, build the service envelope and fragment descriptors, and verify no
resource/sign-on continuation or extra datagram after success.

The suite executes 20/20 baseline runs, 20/20 dropped-request runs, and 20/20
fragmented/out-of-order service-batch runs without production sleeps. Focused
tests also cover dropped/mismatching ACK behavior, pre-ACK single-payload
ownership, duplicates/old packets, malformed envelope/service input, unknown
opcode, unterminated string, boundary-opcode-only truncation, channel and
fragment deadlines, cancellation, secondary stream, backpressure, callback
reentry, terminal idempotence, and exact-once lifetime cleanup.

The invalid-active-state row is exercised by a transactional second `start()`.
The typed `initial_request_queue_failed` branch is defensive: after successful
configuration and driver start, the fixed five-byte request is below every
validated reliable-queue bound, so a deterministic queue rejection is
structurally unreachable without an artificial production injection seam.
Allocation exceptions are contained and mapped to the typed failure; queue and
limit failures remain covered at their owning driver/session boundaries.

In the deterministic dropped-request case, the fake discards the first C2S
datagram before decoding or semantically admitting it. It then supplies an
explicit advancing peer-ACK opportunity with the old reliable generation; the
existing M2.3.2 ACK-gap rule retransmits the same canonical request at a fresh
sequence, and only that retry is validated/admitted. This proves driver-owned
ACK-gap recovery, not an autonomous time retry. If a peer remains completely
quiet after the loss, the project sends no guessed keepalive/retry and reaches
its bounded channel timeout.

This is project-to-fake-HLDS evidence, not a claim that `hlclient` has entered
sign-on against a stock server. Live project-to-stock sign-on remains pending a
production Steam authentication provider. The stock evidence is separately the
signed-stock-client to signed-stock-HLDS research set described above.

## Deliberately pending

- semantic parsing of opcode 11 and any serverinfo fields;
- determination of whether a resource-list opcode occurs inside/after the
  opaque boundary body;
- a typed disconnect service control, pending primary wire evidence;
- resource-list/delta-description parsing or resource negotiation;
- command/stufftext execution (intentionally prohibited, not planned here);
- spawn/resource replies, snapshots, gameplay, or renderer state;
- slot-1/file semantics and filesystem output;
- full stock-server live sign-on by the project client.

The next milestone is M2.4.2: typed serverinfo and pre-resource sign-on state.
