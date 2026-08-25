# GoldSrc delta-description schemas

> Runtime-value boundary: opcode-14 evidence proves schema metadata only. It
> does not prove the runtime changed mask, integer representation, multiplier
> formula, angle/time-window/string encoding, baseline defaults, or entity
> framing. M4.5.1 reuses `DeltaSchemaRegistryState` through a separate
> fail-before-read stock decoder and a sealed synthetic-neutral test profile;
> it never reuses the description presence-mask grammar by assumption. See
> [runtime delta values](GOLDSRC_RUNTIME_DELTA_VALUES.md).

M2.4.3 extends the exact retained M2.4.2 service payload from its opcode-14
cursor through the complete delta-description sequence. The implementation is
an owning metadata registry only. It does not apply deltas to runtime memory,
decode snapshots, parse the following message body, send a resource response,
or touch filesystem, asset, world, SDL, or renderer state.

## Evidence and profile

Primary evidence came from 16 accepted private IPv4-loopback stock runs using
the signed Valve `hl.exe` launcher (`VERSIONINFO 1.1.1.1`) and signed Valve
`hlds.exe` launcher (`VERSIONINFO 4.1.1.1`) with the Protocol 48/build 10210
server profile. The byte-preserving relay kept one immutable learned client
endpoint, one upstream socket, exact server endpoint validation, and bounded
packet, byte, and time budgets. Raw decompressed research payloads remain only
under ignored `manual-artifacts/`; no packet, payload, authentication, or game
data is tracked.

The accepted projection set comprises:

- six repeated `boot_camp`, maxplayers-2 baselines, plus the earlier accepted
  first-client reference run;
- two `crossfire` map differentials;
- two maxplayers-1 and two maxplayers-8 differentials;
- two controlled server-label differentials;
- one accepted same-process map-change run.

Every projection produced the same schema names, order, field counts,
field-definition hashes, type masks, message lengths, alignment, and numeric
next opcode. Existing M2.4.1 transport research separately covers two
duplicated whole server batches and bounded loss behavior. Those older runs did
not retain a delta payload for a second projection, so they are not counted as
new delta-grammar stock runs. Deterministic project tests, rather than a stock
internal publication counter, prove exact-once registry publication and no
partial parse after missing fragments.

After the primary captures, the pinned public Valve Half-Life SDK was used only
to cross-check the delta schema/field names and public field-type constants. No
server implementation, leaked engine dump, packet rewrite, fabricated ACK, or
third-party reverse-engineered struct was used.

`scripts/verify_stock_delta_descriptions.ps1` independently resumes from the
exact boundary offset in each accepted server-info projection, parses the
ignored canonical payload in memory, checks the complete fixed profile, and
writes exactly one metadata-only projection below the ignored
`manual-artifacts/delta-description-captures/` root. Its tracked schema stores
selected safe schema names, counts, masks, message geometry, and hashes of
canonical field definitions—not the full stock field list. The script and all
16 projections validate under PowerShell 7 and Windows PowerShell 5.1.

## Opcode and exact wire grammar

Opcode 14 is independently confirmed as a delta-description message category:
stock capture supplies the repeated wire structure, while the public Valve SDK
supplies matching delta schema/field names and type constants. Each of the
seven messages uses this grammar:

```text
u8    opcode = 14                         (byte aligned)
char  schema_name[] + NUL                 (ASCII-compatible captured names)
u16le field_count

repeat field_count times, LSB-first within each byte:
    3 bits  presence-mask byte count = 1
    8 bits  presence mask = 0x7f, or 0x7b when offset is zero/omitted
    32 bits field type flags
    bytes   field_name + NUL
    16 bits offset, only when presence bit 0x04 is set
    8 bits  storage-size metadata = 1
    8 bits  significant-bit count
    32 bits premultiply unsigned fixed-point wire value
    32 bits postmultiply unsigned fixed-point wire value

0..7 zero bits to the next byte boundary
u8 next opcode
```

The project-owned bit reader reads least-significant bit first within each
byte. Reads are bounded to 0..32 bits; zero-width reads are valid; failed reads
preserve the cursor. The parser receives an explicit payload bit length, so
every truncated byte prefix and every intermediate bit length can be tested
without exposing a partial schema.

The multiplier representation is not IEEE-754. It is an unsigned little-endian
32-bit fixed-point wire integer with divisor 4,000. The owning field retains the
exact integer and offers a derived `double` value. Consequently NaN, infinity,
and float-endianness cases are not representable in the confirmed profile;
tests instead cover zero/invalid fixed-point values, truncation, and exact wire
preservation.

The public field base flags are:

| Mask | Project type |
| ---: | --- |
| `0x01` | byte |
| `0x02` | short |
| `0x04` | float |
| `0x08` | integer |
| `0x10` | angle |
| `0x20` | time-window-8 |
| `0x40` | time-window-big |
| `0x80` | string |
| `0x80000000` | signed modifier |

Exactly one base bit is required; the signed modifier is separate. Conflicting
base flags, missing base flags, reserved flags, incomplete/unknown presence
masks, invalid storage size, invalid significant-bit ranges, offset/size
overflow, zero multipliers, duplicate field names, and nonzero alignment bits
fail closed. Unknown bits are never masked away.

## Observed schema sequence

All 16 accepted projections contain seven opcode-14 messages and 219 fields:

| Index | Schema | Fields | Message bytes | Body bits | Zero pad bits |
| ---: | --- | ---: | ---: | ---: | ---: |
| 0 | `event_t` | 14 | 380 | 3,032 | 6 |
| 1 | `weapon_data_t` | 20 | 613 | 4,896 | 4 |
| 2 | `usercmd_t` | 15 | 454 | 3,624 | 3 |
| 3 | `custom_entity_state_t` | 19 | 539 | 4,304 | 7 |
| 4 | `entity_state_player_t` | 49 | 1,368 | 10,936 | 5 |
| 5 | `entity_state_t` | 52 | 1,454 | 11,624 | 4 |
| 6 | `clientdata_t` | 50 | 1,386 | 11,080 | 2 |

The sequence occupies exactly 6,194 bytes from its first opcode through the
last schema alignment, ending on a byte boundary. Schema and field names are
case-sensitive, owning, bounded, and ordered. Duplicate fields within one
schema and duplicate schemas within one registry are rejected transactionally.
The registry provides exact linear lookup because preserving wire order is the
primary contract; no unordered container is the source of truth.

## Typed parser and registry

`DeltaDescriptionParser::parse()` consumes exactly one opcode-14 message and
returns either one owning `DeltaSchema` with exact bit/byte accounting or one
typed error. `DeltaFieldDefinition` owns its name, validated type flags, offset,
storage size, significant-bit count, both exact multiplier integers, wire
index, and presence mask.

`DeltaSchemaRegistryBuilder` accumulates a local candidate under checked schema,
field, name-byte, and accounted-memory totals. `publish()` produces immutable
`DeltaSchemaRegistryState`; the stage never exposes the builder or publishes a
prefix. The stream decoder repeatedly invokes the single-message parser only
at the exact returned cursor. It does not scan for opcode 14, resynchronize on
unknown data, or skip an unknown message.

Project limits are safety bounds, not claimed stock maxima:

| Bound | Default | Hard maximum |
| --- | ---: | ---: |
| schema name | 64 bytes | 256 bytes |
| field name | 64 bytes | 256 bytes |
| schemas | 32 | 256 |
| fields per schema | 256 | 1,024 |
| total fields | 2,048 | 8,192 |
| total name bytes | 65,536 | 1,048,576 |
| bits per message | 1,048,576 | 8,388,608 |
| accounted registry bytes | 262,144 | 2,097,152 |
| stage events | 32 | 512 |

Limits have exact and limit-plus-one tests. Size arithmetic is checked before
allocation or cursor advancement. Parser, registry, stream, event publication,
and stage result construction are transactional.

## Exact post-delta boundary

The byte immediately following the seventh aligned schema is consistently
numeric opcode 44. Its absolute service-payload offset varies with the earlier
server-info/opcode-8 prefix (for example 6,393 in the first-client reference
and 6,388 in the common short-prefix baseline), but it is always exactly 6,194
bytes after the first opcode 14.

Opcode 44 is not the resource list. At this layer the public API deliberately
retains `PostDeltaBoundary` with category
`stock_observed_opcode_44` and evidence status
`stock_confirmed_opcode_44_body_unconsumed`. Its opcode, exact byte/bit offset,
remaining byte count, and direction are typed; its body is neither copied into
the registry nor consumed. No `ResourceListBoundary` name is exposed.

M2.4.4 independently decodes that retained body as movement/environment
metadata and continues only through confirmed simple controls. The M2.4.3 stop
itself remains unchanged and sends no `sendres` or other client continuation.

## Stage, coordinator, and CLI boundary

`DeltaDescriptionStage` owns one nested `PreResourceSignonStage`. A private
friend-only retention mode carries the same bound UDP transport,
`NetchanDriver`, decompressed owning service payload, and authentication
lifetime across the former M2.4.2 stop. There is no second socket, session,
request, decompression, opcode-8 parse, server-info parse, or opcode-54 parse.

After a complete candidate decode, the stage preflights all events, publishes
one event per schema in order, one registry-ready event, and one post-delta
boundary event, then closes the retained driver/auth lifetime exactly once.
The success state is `post_delta_boundary_reached`; coordinator success is
`delta_schemas_ready`. Invalid configuration/start, timeout, cancellation,
network/protocol failure, secondary stream, unsupported input, decoder failure,
and event backpressure are terminal and idempotent.

The explicit route is:

```powershell
.\build\bin\Debug\hlclient.exe --renderer null `
  --connect 127.0.0.1:27128 --stop-after delta-schemas `
  --auth-provider file `
  --auth-material-file C:\private\hl-auth-material.bin --net-trace
```

The bounded success log reports schema index, sanitized schema name, field
count, exact aggregate bits/bytes, and numeric next opcode/offset. There is no
raw delta injection, registry bypass, schema application, resource reply,
server-path mount, or arbitrary reliable payload CLI.

## Deterministic integration evidence

The production coordinator/driver path is exercised with real loopback UDP and
an independently constructed fake-HLDS fixture on the same socket and exact
source endpoint:

- baseline delta stream: 20/20;
- fragmented final-first delivery with exact duplicate and reordered
  completion: 20/20;
- multiple schemas with differential offsets/names/flags: 20/20.

Each iteration proves one semantic `new` request, one owning registry
publication, one post-delta boundary event, zero resource-continuation packets,
bounded completion, and exactly-once authentication cleanup. Parser/registry/
stage tests additionally cover every byte and bit truncation, unknown opcode,
missing terminal boundary, nonzero padding, duplicate schema/field names,
limits and limit-plus-one, event backpressure with no partial publication,
wrong endpoint, timeout, cancellation, callback reentry, and terminal
idempotence. These are deterministic project-to-fake-HLDS results, not a live
project-client-to-stock-server claim.

## Deliberately pending at this layer

- opcode-44 interpretation inside M2.4.3 (implemented only by the separate
  M2.4.4 continuation);
- opcode-13 and resource-transition interpretation inside M2.4.3 (implemented
  only by the later M3.1.1 continuation);
- opcode-43 body semantics, resource count/entries, consistency data, resource
  response, precache, or download behavior;
- runtime use of the registry for entity/clientdata/usercmd/snapshot decoding;
- live project-client-to-stock-HLDS sign-on, pending a production Steam
  authentication provider;
- independent stock duplicate-batch and truncated-fragment delta projections;
  their transport behavior is covered by earlier accepted evidence and current
  deterministic integration, but no new raw delta projection is claimed.

See [GoldSrc movement-environment state](GOLDSRC_MOVEVARS.md) for M2.4.4,
[GoldSrc user info](GOLDSRC_USERINFO.md), and
[GoldSrc resource transition](GOLDSRC_RESOURCE_TRANSITION.md) for M3.1.1.
M3.1.2 resource-list body discovery remains next; none of these continuations
changes this layer's historical stop.
