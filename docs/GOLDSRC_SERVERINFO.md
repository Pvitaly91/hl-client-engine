# GoldSrc server-info and pre-resource boundary

## Scope and status

M2.4.2 continues the owning M2.4.1 service payload at its exact opcode-11
cursor. It decodes only the stock-confirmed server-info grammar, one confirmed
simple control, and the next exact complex-message boundary:

```text
M2.4.1 owning decompressed payload
    -> opcode 11 server-info body
    -> opcode 54 neutral simple control
    -> opcode 14 confirmed category-C pre-resource boundary
    -> STOP, body untouched
```

The bounded single-client implementation and fake-HLDS profile are present.
The milestone is **not fully evidence-complete**: two safe attempts to launch a
second signed stock client never emitted the canonical `getchallenge`, so the
one-byte field at body offset 29 remains an opaque slot candidate and is not a
public `ClientSlot`. M2.4.3 subsequently confirms opcode 14 as the bounded
delta-description sequence; this M2.4.2 boundary and stop behavior remain
unchanged. M2.4.4 subsequently decodes the numeric opcode-44
movement/environment body and stops at a later neutral boundary; M3.1
resource-list discovery remains separate.

Evidence labels in this document are:

- **stock-confirmed** — isolated by repeated signed-stock differential runs;
- **strongly inferred** — a repeatable correlation whose narrower product name
  is intentionally neutral;
- **project policy/tested** — a bounded fail-closed rule, not a stock maximum;
- **opaque/pending** — width or position may be known, but no public semantic
  field is claimed.

## Compatibility profile and capture method

The reference profile is the signed Valve Half-Life client launcher
VERSIONINFO 1.1.1.1 and signed Valve HLDS launcher VERSIONINFO 4.1.1.1. The
separately observed engine/wire profile is Half-Life 1.1.2.2, Protocol 48,
build 10210; the local Steam App 70 manifest used for research was build
15961492.

The tracked `scripts/verify_stock_serverinfo.ps1` drives an explicit
user-supplied byte-preserving relay on private IPv4 loopback. It validates
explicit non-reparse Valve-signed paths, exact process identity, one immutable
learned client endpoint, one upstream socket, exact server endpoint, bounded
packet/datagram/total-byte/time counts, one whitelisted metadata document, and
clean process/port teardown. It never accepts raw packet bytes,
authentication/identity data, opaque fixed-field values, the map-list text,
server command text, or the unparsed `sendres` tail in metadata. Ignored local
research projections remain under `manual-artifacts/serverinfo-captures/` and
are not committed.

The accepted primary set contains 16 bounded runs:

| Scenario | Accepted evidence |
| --- | ---: |
| identical `boot_camp`, maxplayers 2 baselines | 6 |
| controlled `crossfire` map differential | 2 |
| maxplayers 1 | 2 |
| maxplayers 8 | 2 |
| controlled hostname values | 2 |
| same-process second map start | 1 |
| additional first-client structural run | 1 |

The six fresh baselines also provide more than the required two clean server
restarts. Two second-client attempts and one incomplete maxplayers-8 proof were
rejected and do not count. Every accepted run ended with zero observed stock
processes and zero selected-port owners.

## Opcode 11 semantic and exact body grammar

The message is named `server_info` from primary behavior: its controlled body
contains the supported protocol, maximum-client count, game directory, server
label, and map path. No third-party layout was used to find the cursor.

The opcode-11 body is variable length. It is not a packed 153-byte struct. Its
confirmed grammar is:

```text
u32le protocol
u32le opaque map-start ordinal candidate
u32le opaque map/mode-dependent value
byte[16] opaque fixed binary value
u8 maximum clients
u8 opaque client-index candidate
u8 multi-client-mode flag
nul_string game directory
nul_string server label
nul_string map file path
nul_string opaque map-list text
u8 captured zero trailer
```

The body size is exactly:

```text
31 fixed bytes
+ four string byte lengths
+ four NUL terminators
+ one final byte
```

Observed bodies were 148 bytes with a four-byte server label and 153 bytes
with a nine-byte server label. The parser returns this exact dynamic
`bytes_consumed`; it neither scans for opcode-looking bytes nor consumes the
following opcode.

## Field evidence table

Offsets are relative to the first body byte after opcode 11.

| Offset | Width/encoding | Observed values | Stability / controlled scenario | Interpretation/confidence | Public |
| ---: | --- | --- | --- | --- | :---: |
| 0 | `u32le` | 48 | stable in all 16 runs | Protocol 48, stock-confirmed | yes |
| 4 | `u32le` | 1 on fresh starts; 2 on the same-process second map | dynamic in the map-change scenario | ordinal-like correlation; exact semantic strongly inferred/pending | no |
| 8 | `u32le` | value suppressed | dynamic across map and single/multi-client-mode differentials | semantic/checksum name opaque | no |
| 12 | 16 bytes | value suppressed | stable in all 16 runs | algorithm and semantic opaque | no |
| 28 | `u8` | 1, 2, 8 | dynamic exactly with controlled `maxplayers` | maximum clients, stock-confirmed | yes |
| 29 | `u8` | 0 | stable in accepted first-client runs; both second-client attempts rejected | client-index candidate pending | no |
| 30 | `u8` | 0 for maxplayers 1; 1 for 2 and 8 | dynamic in maxplayers differentials | multi-client-mode boolean, stock-confirmed | yes |
| 31 | bounded NUL bytes | `valve` (5 bytes) | stable under the controlled game profile | game directory metadata, stock-confirmed | yes |
| `32 + G` | bounded NUL bytes | `Half`, `Half-Life`, `M242Alpha`, `M242Bravo` | dynamic only in controlled hostname scenarios | server label metadata, stock-confirmed | yes |
| `33 + G + H` | bounded NUL bytes | `maps/boot_camp.bsp`, `maps/crossfire.bsp` | dynamic only in controlled map scenarios | map file path metadata, stock-confirmed | yes |
| `34 + G + H + M` | bounded NUL bytes | 85 bytes; value suppressed | stable in the 16 accepted runs | map-list-like text remains opaque | no |
| `35 + G + H + M + O` | `u8` | 0 | stable in all accepted runs | reserved/semantic pending; exact zero validated | no |

Here `G`, `H`, `M`, and `O` are the game-directory, server-label, map-path,
and opaque-fourth-string byte lengths respectively, excluding their NUL
terminators. Thus every field offset is derived exactly from already bounded
lengths rather than found by scanning for a later opcode.

All integers are decoded explicitly as unsigned little-endian values through
`ByteReader`; no packed casts, alignment assumptions, host-ABI structs, or
`strlen` on wire bytes are used. The fixed 16-byte field never becomes a C
string and is never logged or passed to a filesystem consumer.

## Public state and parser contract

`ServerInfoParser::parse()` receives a bounded span beginning immediately
after opcode 11. It builds a local candidate and returns either:

- an owning immutable `ServerInfoState` plus exact `bytes_consumed`; or
- a typed `ServerInfoError` with zero consumed bytes and no partial state.

The public state exposes only:

- existing strong `ProtocolVersion`;
- strong `MaximumClients`;
- the confirmed multi-client-mode boolean;
- owning game-directory, server-label, and map-path metadata strings;
- fixed compatibility/evidence profiles.

The server strings are untrusted metadata. They are never normalized, opened,
executed, used as URLs, passed to an asset system, or sent to a renderer.
The explicit pre-resource success log presents only game-directory and map
metadata through the existing bounded terminal sanitizer; it omits the server
label and every opaque field. Network trace carries numeric fields, offsets,
and string lengths only, never string contents.

Project string limits are 1,024 bytes by default and 4,096 bytes at the hard
cap, per field. Maximum clients is supported in the project range 1..32. The
captured profile additionally requires a boolean mode byte, consistency
between the mode byte and maximum-client count, and the exact zero trailer.
These are fixed-profile validation rules, not claims about every GoldSrc
derivative. The offset-4 candidate is consumed without semantic validation.
Strings are bounded wire-compatible byte sequences excluding their NUL
terminator; the parser applies no UTF-8, locale, or path normalization.
Presentation escapes terminal controls and other non-printable bytes through
the bounded sanitizer.

## Exact continuation and pre-resource state

`ServiceMessageStreamDecoder::continue_to_pre_resource()` accepts the existing
owning M2.4.1 payload and its exact `ServiceMessageBoundary`. It validates the
opcode, offset, remaining-byte geometry, server-to-client direction,
decompressed/payload/message bounds, and then calls `ServerInfoParser` without
repeating BZip2 or opcode-8 decoding.

Across all accepted runs, the next order was:

```text
dynamic body end: opcode 54
                  empty NUL string
                  u8 zero
next byte:        opcode 14
                  body remains wholly unconsumed
```

Opcode 54 is represented by a neutral `PreResourceControl`; no third-party
semantic name is assigned. Opcode 14 is represented by
`ResourcePhaseBoundary` with `server_message` direction. The boundary/order is
stock-confirmed under prompt category C, so its evidence value is
`confirmed_pre_resource_boundary_body_pending`. M2.4.3 now independently
confirms opcode 14 as the delta-description sequence and parses that sequence
through an exact retained cursor; M2.4.2 itself still stops without consuming
the body. A baseline with the usual 40-byte opcode-8 prefix has opcode 11 at
absolute offset 42, opcode 54 at 196, and opcode 14 at 199. A four-byte server
label moves those latter offsets to 191 and 194. The maxplayers-1 profile may
omit opcode 8 entirely, placing opcode 11 at offset 0; continuation always uses
the retained cursor rather than assuming 42.

The owning immutable `PreResourceSignonState` contains the typed server info,
confirmed neutral controls, boundary metadata, and complete source payload
metadata. It contains no raw service payload, socket, auth bytes, renderer,
filesystem handle, resource files, download state, or world structures.

Fourteen of fourteen accepted multi-client-mode runs later emitted one
37-byte reliable client body whose bounded prefix was opcode 3 plus `sendres`
and NUL, followed by 28 unparsed bytes. Both maxplayers-1 runs emitted none.
This proves a later stock resource action after the first batch; it does not
prove immediate opcode-14 causality or the tail layout. M2.4.2 therefore has no
`sendres` builder and sends no resource continuation.

## Stage and ownership boundary

`PreResourceSignonStage` owns one nested `InitialSignonStage`. A private
friend-only mode retains the existing driver and authentication lifetime at
the M2.4.1 boundary; the public `InitialSignonStage` constructor retains its
historical close-on-boundary behavior. The pre-resource facade synchronously
continues the already-owned decompressed payload, preflights its fixed-capacity
events, publishes one server-info event, one opcode-54 control event, and one
opcode-14 boundary event, then closes the same driver/lifetime exactly once.

No second socket, transport, session, envelope decode, opcode-8 decode, request
queue, or reliable continuation is created. Timeout, cancellation, network or
protocol failure, unsupported input, secondary-stream pending, backpressure,
destruction, and success all retain idempotent cleanup.

The explicit `--stop-after pre-resource` route uses this facade after
connectionless `ACCEPT`. The existing `--stop-after signon-boundary` route is
unchanged. Neither route exposes raw server-info injection, resource-send,
skip/bypass, download, or arbitrary server-command CLI options.

## Security and deliberate boundaries

- every server byte is length-bounded and treated as untrusted;
- strings use bounded NUL searches and owning copies;
- unknown opcodes are not skipped or resynchronized;
- state and events are published only after a complete transactional parse;
- opcode-14 body is not parsed, copied into typed state, or scanned;
- server text and metadata strings are not executed or printed raw;
- no resource command, resource-list parser, download, filesystem action,
  renderer mutation, client world state, or later-stage state exists inside
  the M2.4.2 parser/stage;
- no stock binary, raw capture, auth material, opaque fixed-field value,
  map-list text, or game data is tracked.

## Evidence-gated limitations

- body offset 29 has no accepted second-client differential and stays opaque;
- opcode 14 is now independently confirmed and implemented as the bounded
  M2.4.3 delta-description sequence; M2.4.2 retains its original stop behavior;
- the numeric opcode-44 body following delta schemas is now the independently
  confirmed M2.4.4 movement/environment metadata codec;
- the later `sendres` prefix is now independently confirmed and implemented
  only by the separate M3.1.1 transition stage; this M2.4.2 stop still sends
  nothing;
- the map/mode-dependent `u32` and fixed 16-byte value remain opaque;
- the fourth NUL string is cursor-bounded but not public semantic state;
- live project-client to stock-HLDS pre-resource sign-on remains pending a
  production Steam authentication provider.

See [GoldSrc delta descriptions](GOLDSRC_DELTA_DESCRIPTIONS.md) and
[GoldSrc movement-environment state](GOLDSRC_MOVEVARS.md) for the M2.4.3 and
M2.4.4 continuations. [GoldSrc user info](GOLDSRC_USERINFO.md) and
[GoldSrc resource transition](GOLDSRC_RESOURCE_TRANSITION.md) document the
separate M3.1.1 continuation through exact first-batch end and the neutral
opcode-43 boundary. M3.1.2 resource-list body discovery remains next; no later
milestone changes this layer's historical stop.
