# GoldSrc Protocol 48 netchan profile

## Scope and evidence status

This document records the M2.3 transport profile derived independently from
bounded black-box observations. It stops at an owning, opaque netchan payload.
It does not decode `svc_*`, assign sign-on meaning to any payload byte, load a
map, create a resource, or update `ClientWorldState`.

Every interoperability statement uses one of these labels:

- **Confirmed by stock capture** — repeated observation of the signed stock
  Valve client and stock Valve HLDS through a byte-transparent bounded relay.
- **Inferred and independently tested** — project behavior selected from the
  observations and exercised with independent fixtures or state-machine tests;
  it is not claimed as an exhaustively observed stock-engine rule.
- **Pending** — not established by the captures and unsupported by this
  profile.

The current overall evidence status is deliberately split:

| Path | Status |
| --- | --- |
| stock client through relay to stock HLDS | **Confirmed by stock capture:** post-`ACCEPT` capture is complete for the profile below |
| project client to deterministic fake HLDS | **Confirmed by project tests:** production same-socket bootstrap, reassembly, exact acknowledgement, and quiet terminal boundary pass deterministically |
| project client to stock HLDS | **Pending:** no production Steam authentication provider exists, so live stock acceptance/bootstrap is not claimed |

M2.3 implementation and deterministic fake-HLDS validation are complete for
this bounded profile. That completion is distinct from the pending live
project-client-to-stock-HLDS path.

## Compatibility profile and methodology

**Confirmed by stock capture:** the reference client was signed Valve
`hl.exe` 1.1.1.1, Steam App 70 build ID 15961492. The reference server was
stock Valve `hlds.exe` 1.1.2.2, Protocol 48, build 10210. Transport was IPv4
UDP on loopback with separate client-facing and server-facing relay sockets.
The relay preserved one upstream socket and source endpoint through challenge,
connect, `ACCEPT`, and sequenced traffic. Capture time, packet count, and byte
count were bounded.

Passive runs were followed by controlled drop, duplicate, and reorder runs.
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

## Post-`ACCEPT` order and initial state

**Confirmed by stock capture:** the client sends the first sequenced datagram
after connectionless `ACCEPT`.

Two passive runs differed only in the number of client padding transmissions
scheduled before the first server packet:

```text
legacy observation: C->S sequence 1, 2, 3, then S->C sequence 1
fresh observation:  C->S sequence 1, 2, 3, 4, then S->C sequence 1
```

The number of pre-server padding sends is therefore scheduling-dependent; it
is not a fixed wire constant.

**Confirmed by stock capture:** the first client word was raw
`0x80000001` (numeric sequence 1 with reliable-present set) and its
acknowledgement word was zero. Its decoded body remains opaque and is not
hardcoded as a protocol command. Later pre-server client packets used increasing
numeric sequences and the minimum decoded padding body described below.

**Confirmed by stock capture:** the first server packet used numeric sequence
1 with reliable-present and fragments-present set. Its acknowledgement named
the latest processed client sequence and carried reliable acknowledgement 1.
Both channel directions begin from numeric baseline 0, next outgoing sequence
1, and reliable toggles 0.

**Inferred and independently tested:** because the stock client's first
reliable body is opaque, the project does not copy or hardcode it. The default
project/fake bootstrap probe instead uses numeric sequence 1, acknowledgement
0, no reliable flag, and the decoded eight-byte `0x01` padding body. This is a
safe deterministic bootstrap profile, not a claim that the probe substitutes
for the stock client's message-level content on a live stock server.

## Base header and flags

**Confirmed by stock capture:** both directions use the same untransformed,
eight-byte base header. There is no client-only qport field and no checksum or
validation byte in this captured Protocol 48 profile.

| Decoded offset | Width | Encoding | Meaning | Evidence |
| ---: | ---: | --- | --- | --- |
| 0 | 4 | little-endian `uint32` | sequence word | **Confirmed by stock capture** |
| 4 | 4 | little-endian `uint32` | acknowledgement word | **Confirmed by stock capture** |
| 8 | 0 | — | qport absent | **Confirmed by stock capture** |
| 8 | 0 | — | checksum absent | **Confirmed by stock capture** |

Sequence word masks:

| Bits | Mask | Meaning | Evidence |
| --- | ---: | --- | --- |
| 0..29 | `0x3fffffff` | numeric sequence | **Confirmed by stock capture** |
| 30 | `0x40000000` | fragment descriptors present | **Confirmed by stock capture** |
| 31 | `0x80000000` | reliable unit present | **Confirmed by stock capture** |

Acknowledgement word masks:

| Bits | Mask | Meaning | Evidence |
| --- | ---: | --- | --- |
| 0..29 | `0x3fffffff` | acknowledged numeric sequence | **Confirmed by stock capture** |
| 31 | `0x80000000` | reliable acknowledgement toggle | **Confirmed by stock capture** |
| 30 | `0x40000000` | unsupported/reserved in this profile | **Pending:** never observed; the project rejects it |

Connectionless `0xffffffff` is classified before this decoder. The
`0xfffffffe` special/split marker is kept out of normal netchan parsing by a
conservative project classifier. That split-marker policy is **inferred and
independently tested**; split packet decoding is pending.

## Payload transform

**Confirmed by stock capture:** bytes 0 through 7 are never transformed. Every
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
32-bit. **Inferred and independently tested:** independent golden vectors
cover both directions, different keys, more than one word, and unchanged
tails. One safe structural vector is that a decoded eight-byte body of eight
`0x01` padding bytes under numeric sequence 2 transforms to
`59 19 01 03 19 01 11 43`; it contains no authentication or sign-on data.

Decoded 16-byte padding-only datagrams prove the offset and also rule out a
hidden qport/checksum: eight header bytes followed by eight decoded `0x01`
bytes.

## Fragment descriptors and normal reassembly

**Confirmed by stock capture:** when sequence bit 30 is set, the transformed
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
bits are the total count for this profile. Concatenating indices 1 through 5
produced an owning opaque 4,186-byte payload. Repeated and perturbation runs
produced the same length and digest without retaining that payload in the
repository.

**Inferred and independently tested:** project normal-stream reassembly accepts
out-of-order indices, ignores an exact byte-identical duplicate, rejects a
conflicting duplicate or count mismatch, and completes only when every index
is present. Packet offsets and lengths are bounds-checked before the reassembler
copies a span. No remote count or length directly controls an unbounded
allocation.

The observed traffic establishes only that slot 0 carried this normal opaque
message. **Pending:** stock semantics for slot 1 and stock file-stream traffic
were not observed. The project exposes slot 1 only as an opaque secondary
stream pending M3. It does not derive a filename or path, retain its bytes, or
write anything to disk.

## Acknowledgement and reliable behavior

**Confirmed by stock capture:** acknowledgement is always the second header
word; there is no separate ACK opcode. It may ride a 16-byte padding-only send
or an ordinary payload send. For the five captured server fragments, client
reliable acknowledgements alternated `1, 0, 1, 0, 1`. The first four were sent
roughly 20–54 ms after arrival; the final one was piggybacked roughly 354 ms
later. These timings are observations, not protocol deadlines.

Controlled perturbations established:

- **Confirmed by stock capture:** duplicating the first server sequence caused
  only one reliable-toggle transition and one fragment admission.
- **Confirmed by stock capture:** when a newer padding packet arrived before an
  older held fragment, the client acknowledged the newer numeric sequence and
  ignored the late old fragment before reliable/reassembly mutation.
- **Confirmed by stock capture:** after the server retransmitted the same first
  fragment under a fresh numeric sequence, reassembly continued and completed
  without duplicate bytes.
- **Confirmed by stock capture:** dropping the first reliable fragment produced
  later padding sends followed by an identical fragment retransmission under a
  fresh numeric sequence after the numeric acknowledgement advanced while the
  reliable toggle still mismatched.

**Inferred and independently tested:** the project compares low-30-bit numeric
sequences modulo `0x40000000`, treats equality as duplicate, rejects the exact
half-range as ambiguous, and does not let flag bits enter numeric comparison.
A reliable outgoing buffer remains in flight until an acknowledgement both
reaches its sent sequence and carries the expected reliable toggle. Unrelated,
stale, or duplicate acknowledgements do not clear it.

## Project safety and timeout policy

These are **inferred and independently tested project safety limits**, not
claims about original engine maxima:

| Boundary | Default | Hard maximum |
| --- | ---: | ---: |
| decoded/encoded netchan datagram | 4,096 bytes | 16,384 bytes |
| one fragment payload | — | 16,384 bytes |
| observed normal fragment payload | 1,024 bytes | observation only |
| normal reassembled message | — | 1 MiB |
| fragments per normal message | — | 1,024 |
| active normal transfers | — | 1 |
| secondary-stream bytes observable in one single-descriptor datagram | 4,078 bytes | 16,366 bytes |
| secondary-stream bytes retained or persisted | 0 bytes | 0 bytes |
| first-packet/fragment wait | 5 seconds | 30 seconds |
| received datagrams per `update()` | 8 | 64 |
| outgoing packets per `update()` | 1 | 8 |

Updates must poll a bounded number of datagrams, stop on `would_block`, ignore
wrong-endpoint traffic before parsing, and use injected monotonic time. There
is no background network thread or production sleep. Cancellation, timeout,
network failure, malformed packets, fragment failure, and an unexpected
secondary stream are distinct terminal failures.

The named secondary-stream observation ceiling subtracts the eight-byte base
header and ten-byte one-present-slot descriptor area from the configured
datagram ceiling. It bounds parsing only; M2.3 immediately reports the M3
boundary and retains zero secondary-stream bytes.

**Confirmed by stock capture:** post-`ACCEPT` datagram sizes included 16, 45,
108, and 1,042 bytes. Those observations inform fixtures but do not replace the
project limits.

## Layering and opaque terminal boundary

The intended M2.3 flow is:

```text
connectionless ACCEPT
    -> same IDatagramTransport and UDP socket
    -> netchan packet codec and payload transform
    -> sequence/reliable channel state
    -> bounded normal reassembly
    -> owning opaque payload
    -> required outgoing acknowledgement/piggyback
    -> terminal netchan-bootstrap result
```

The codec owns byte layout only. Channel state owns sequence,
acknowledgement, and reliable lifecycle. The bootstrap stage owns exact remote
endpoint validation, unchanged local endpoint, polling, cancellation, and
timeouts. The coordinator only hands the already-open transport from the
connect-response stage to the bootstrap stage.

The opaque payload is not a `ClientWorldState` field and never reaches a
renderer. The first client reliable body and the reassembled server body remain
opaque; neither is hardcoded and neither is scanned for `svc_*` values. M2.4,
not M2.3, is the next milestone and will define the initial sign-on state
machine.

## Pending and unsupported behavior

The following remain **pending** in this profile:

- project-client live `ACCEPT` and bootstrap against stock HLDS;
- stock behavior exactly at sequence wrap and half-range ambiguity;
- acknowledgement bit 30;
- split/special packet decoding and public protocol extensions;
- slot-1/file-stream wire semantics and every persistence policy;
- `svc_*`, serverdata, signon, resource, snapshot, command, and gameplay
  parsing.

No pending item is filled from a third-party field name or implementation.
Capture observations have priority over secondary behavioral cross-checks.
