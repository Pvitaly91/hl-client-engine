# GoldSrc opcode-43 resource list

M3.1.2 identifies numeric opcode 43 as `ResourceListMessage` for the
completed, bounded stock standard profile and decodes it into an ordered,
owning `ResourceListState`. The decoder starts only at the exact opcode-43
cursor published by M3.1.1, consumes the list through the exact end of the
owning service payload, and publishes a metadata-only client-response
boundary. It does not send that response or touch the filesystem.

Status: **standard profile completed; custom/player-resource profile typed
unsupported and pending**. The separate M3.1.3 continuation now provides a
bounded neutral response codec and provider boundary, while M3.2.1 adds an
explicit production local provider plus a separate metadata-only resolution
foundation. Neither addition alters this stage or its historical zero-response
stop. Readiness/precache and download/cache remain outside M3.2.1.

## Stock evidence and isolation

The reference profile is signed Valve `hl.exe` VERSIONINFO 1.1.1.1, Steam
App 70 build 15961492, against signed Valve `hlds.exe` launcher VERSIONINFO
4.1.1.1 with observed engine profile 1.1.2.2, Protocol 48/build 10210. The
source captures used a private IPv4-loopback byte-preserving relay, one
upstream socket, exact endpoint checks, and hard packet, byte, and time bounds.

The offline projector accepts 54 ignored canonical second-service payloads
from the preceding movevars and resource-transition research sets. All 54
parse exactly under one grammar and collapse to three coherent, ordered map
profiles:

| Standard profile | Accepted runs | Payload bytes | Entries | Entry end, absolute bit | Terminal zero fill |
| --- | ---: | ---: | ---: | ---: | ---: |
| `boot_camp-standard` | 50 | 10,713 | 540 | 85,700 | 4 bits |
| `crossfire-standard` | 2 | 12,169 | 607 | 97,344 | 8 bits |
| `stalkyard-standard` | 2 | 10,815 | 532 | 86,516 | 4 bits |

Some historical run-directory IDs contain `m244-sky-night-*`. Their captured
configuration metadata identifies the actual map as `stalkyard`; therefore
the accepted evidence and public profile are named `stalkyard-standard`, not
an invented stock map named `night`.

The isolated maxplayers differential has a separate outcome and is not added
to the 54-list denominator. Two bounded `maxplayers 1` runs ended before any
resource transition or list was observed. Both retained the same 7,395-byte
first service payload with SHA-256
`4B471507120A85056658E05B2422488A1B478759203795BF7C5ADBED060F7A2C`,
and both have zero resource-service payloads and zero post-list client bodies.
Their historical capture metadata uses the generic
`hlclient.stock-initial-signon-failure.v1` wrapper, but this is a stable stock
flow outcome, not a resource-list parser failure. Resource grammar and count
are **not applicable** because the flow never reached `sendres` or opcode 43.
At least two `maxplayers 8` runs do contain the complete standard list, so this
maxplayers-1 outcome does not weaken the supported multiplayer standard gate.
The accepted offline projection preflight found zero live `hl`/`hlds`
processes.

The verifier never launches `hl.exe`, `hlds.exe`, a relay, or the project
client. It reads the existing ignored artifacts and writes only the sanitized
tracked projection
[`docs/evidence/GOLDSRC_RESOURCE_LIST_STOCK.json`](evidence/GOLDSRC_RESOURCE_LIST_STOCK.json).
Raw payloads, raw resource names, digest bytes, authentication material, and
game assets remain below ignored `manual-artifacts/` and are not projected.

Run the offline projector and strict projection validator with:

```powershell
.\scripts\verify_stock_resource_list.ps1 -ProjectEvidenceSet
.\scripts\verify_stock_resource_list.ps1 `
  -ValidateMetadataPath .\docs\evidence\GOLDSRC_RESOURCE_LIST_STOCK.json
```

The accepted summary is:

```text
payloads=54 profiles=50/2/2 opcode43=resource-list-standard
maxplayers1=2/no-list responses=51+3 custom=pending
external-file-drift=none
```

The `external-file-drift=none` result is exact for this offline run: the
verifier started no process and made no external installation write. The
optional research-root command is a read-only preflight for future capture
work:

```powershell
.\scripts\verify_stock_resource_list.ps1 -ValidateResearchRoot `
  -ResearchHalfLifeRoot C:\research\Half-Life-isolated
```

The supplied directory must be a non-reparse, user-owned isolated copy, carry
`.hlclient-research-isolated` with the exact marker text
`HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1`, contain the accepted signed
`hl.exe` and `hlds.exe`, and have a `valve` directory. Paths under
`steamapps`, registered Steam roots, and Steam library roots are rejected with
no override. Passing preflight still starts zero processes. New stock capture
work is not accepted from the primary Steam installation.

## Exact wire grammar

The complete second service payload begins with the M3.1.1 control:

```text
absolute byte 0       u8 opcode 45
absolute bytes 1..8  exact opcode-45 body
absolute byte 9       u8 opcode 43
absolute bit 80       resource-list body begins
```

Starting immediately after opcode 43, every field is read least-significant
bit first. No field or entry is byte-aligned before the read:

```text
u12-lsb  resource count

repeat count times:
    u4-lsb   resource type slot
    u8-lsb[] resource-name byte units at the current bit cursor
    u8-lsb   zero terminator
    u12-lsb  resource index
    u24-lsb  raw declared-size code
    u4-lsb   flags/profile slot

zero bits through the exact end of the owning service payload
```

The 44 fixed bits around each variable-length name make successive entry
starts and ends alternate between bit offsets 0 and 4. There is no per-entry
alignment. A reader that aligns before a name, begins at byte 9 rather than
byte 10, or uses most-significant-bit-first order does not reach the observed
entry count or exact endpoint.

The terminal region is required to contain 1 through 8 zero bits and to end at
the exact end of payload. The corpus observes four bits for `boot_camp` and
`stalkyard`, and eight bits for `crossfire`. The exact zero values and EOP are
confirmed; attributing the whole region specifically to padding rather than a
zero marker plus alignment is strongly inferred. The decoder does not scan
for a later opcode and rejects nonzero fill, truncation, or trailing data.

The exact opcode-43 coverage is:

| Profile | Bits from opcode 43 through EOP | Bytes covered from opcode 43 | Ordered body SHA-256 |
| --- | ---: | ---: | --- |
| `boot_camp-standard` | 85,632 | 10,704 | `BA4318498B7A49FFCE648611538975B075EE9320B88D7A2EB51F450499997197` |
| `crossfire-standard` | 97,280 | 12,160 | `D434E105020E7A214B67DA731620D09644697756DFE6C05D6C17743FF37F7456` |
| `stalkyard-standard` | 86,448 | 10,806 | `CE74503D20B9A49AA5F3243BFA05EBDA25C4421F25BD45F0FA2EDB39FB90473B` |

These hashes cover the opcode-43 body beginning at byte 10, including count,
entries, and terminal zero fill. They prove stable order and exact bytes within
each accepted profile without publishing those bytes.

## Field evidence

| Field | Width and encoding | Observed profile | Public interpretation | Confidence |
| --- | --- | --- | --- | --- |
| count | 12 bits, LSB-first | 532, 540, 607 | exact entry count | confirmed |
| type slot | 4 bits, LSB-first | 0, 2, 3, 4, 5 | typed only for confirmed values | confirmed slot and listed mappings |
| name | NUL-terminated 8-bit units at the current bit cursor | lengths 2..37; printable ASCII in stock corpus | owning byte-preserving metadata, never a path | confirmed encoding |
| index | 12 bits, LSB-first | 0..221 | unsigned routing/index metadata; identity is `(type,index)` | confirmed width and identity profile |
| raw size code | 24 bits, LSB-first | 0..`0xFFFFFF` | unsigned opaque code; never an allocation or trusted file size | confirmed width, bounded semantics |
| flags/profile slot | 4 bits, LSB-first | raw masks 0 and 1 | raw mask only; bit meanings not public | confirmed slot, semantic attribution opaque |
| optional custom metadata | absent | no custom entry or digest bytes observed | typed unsupported custom profile | pending |
| terminal zero fill | 1..8 zero bits to EOP | observed 4 or 8 | exact terminal geometry | values/EOP confirmed; padding attribution strongly inferred |

The parser returns exact absolute bit and byte cursors, bits/bytes consumed,
the source payload geometry, and the exact end-of-payload boundary. Offsets in
the table above are evidence observations; the parser accepts an explicit
opcode cursor and does not hard-code byte 9 as a general input position.

## Resource type evidence

The 4-bit type slot is independently separable from the following name. Map
differentials, coherent name-category changes, and the pinned public Valve
header cross-check support these standard-profile values:

| Value | Public type | Stock observation | Evidence status |
| ---: | --- | --- | --- |
| 0 | `sound` | all three profiles | confirmed |
| 1 | — (`skin` in public header) | not observed | unsupported/pending |
| 2 | `model` | all three profiles, including the coherent map/world-model entry | confirmed |
| 3 | `decal` | all three profiles | confirmed |
| 4 | `generic` | `crossfire` only | confirmed |
| 5 | `event_script` | all three profiles | confirmed |
| 6 | — (`world` in public header) | not observed as a distinct wire value | unsupported/pending |

Production does not infer a type from a filename extension. The sanitized
projection uses extension categories only to show aggregate map/profile
coherence; raw names are neither tracked nor logged.

## Names, identity, sizes, and flags

`ResourceName` owns the exact decoded bytes. It does not normalize separators,
fold case, canonicalize, join to a root, open, stat, mount, download, cache, or
load anything. Default output reports only aggregate counts; diagnostic events
carry ordinal/type/index/name length and exact cursors, not raw names or
digest bytes. Synthetic malicious-looking names remain metadata in tests.

All 54 lists have unique `(type,index)` pairs and no exact duplicate names.
The completed profile therefore rejects a duplicate `(type,index)`
transactionally, preserves exact wire order, and does not merge entries.
Duplicate names across different types and case variants are not collapsed;
the corpus does not establish a stronger name-uniqueness rule.

The size field is the exact unsigned 24-bit wire code. Seven `crossfire`
entries use `0xFFFFFF`; no boot-camp or stalkyard entry does. Its sentinel or
file-size meaning is not established. The evidence projection consequently
keeps a raw-code sum and a separate known-value sum that excludes those seven
opaque maximum codes. `ResourceListState::total_size_code_sum()` is checked
arithmetic over raw codes only and is explicitly not a trusted byte total or
an allocation request.

The 4-bit flags/profile slot contains only 0 and 1 in the standard corpus.
The meaning of observed bit 0 remains opaque. Values containing bit 1 are
unobserved. The pinned header labels bits 2 and 3 in an in-memory resource
flags enum, but the stock corpus does not confirm their wire semantics or any
dependent bytes; custom/reserved attribution and optional-field presence are
therefore pending. The standard parser fails closed for masks outside 0/1 and
reports a typed unsupported custom profile where applicable.

## Owning state and safety limits

`ResourceListParser` publishes only after the complete list and exact
post-list boundary validate. `ResourceListState` owns an immutable ordered
vector of `ResourceEntry` values, exact count, checked raw size-code sum,
aggregate name bytes, per-entry wire ordinals and bit ranges, source geometry,
compatibility/evidence profile, and exact `(type,index)` lookup. No public
state retains a span or pointer into the received payload.

Named defaults and hard caps are project safety policy, not stock maxima:

| Limit | Default | Hard cap |
| --- | ---: | ---: |
| resource message bits | 1,048,576 | 8,388,608 |
| resource message bytes | 131,072 | 1,048,576 |
| resource count | 1,024 | 4,095 |
| resource name length | 255 | 4,096 |
| total resource-name bytes | 65,536 | 1,048,576 |
| one raw declared-size code | `0xFFFFFF` | `0xFFFFFF` |
| checked total raw size-code sum | 8 GiB | 64 GiB |
| accepted standard flags mask | `0x01` | 4-bit wire maximum `0x0F` |
| resource-list stage events | 2,048 | 8,192 |

Configuration is validated before parsing. Count allocation, name totals,
size sums, event capacity, every bit read, padding, and endpoint arithmetic
are bounded. A failure publishes no candidate list.

## Stage, events, and CLI

`ResourceListStage` composes the existing `ResourceTransitionStage`; it does
not decompress the second envelope again or create a second socket, driver,
session, or authentication lifetime. It requires the retained owning payload,
driver, and exact `Opcode43Boundary` geometry. The stage then parses a
candidate list, decodes the exact post-list boundary, preflights capacity for
the complete event batch, and publishes the owning `ResourceListSignonState`
atomically. Failure leaves no partial list or advanced public cursor.

Its explicit states are `idle`, `waiting_for_transition_state`,
`decoding_resource_list`, `resource_list_ready`,
`decoding_post_list_messages`, `post_list_boundary_reached`, and terminal
`client_response_required`, plus typed unsupported-profile, timeout,
cancellation, backpressure, secondary-stream, network, and protocol outcomes.
Terminal cleanup is idempotent and releases the retained driver/authentication
lifetime exactly once.

The bounded event surface includes list-ready, per-entry metadata,
post-resource-control, post-resource-boundary, client-response-required,
unsupported-profile, timeout, cancellation, backpressure, secondary-stream,
network, and protocol events. Per-entry events may carry ordinal, confirmed
type, index, raw size code, raw flags value, name length, and exact offsets;
they never contain a resource name, digest, payload span, path, or file object.
The stage's response queue count is permanently zero in M3.1.2.

The two CLI stops deliberately have different semantics:

```text
--stop-after resource-list-boundary
    historical M3.1.1 stop; opcode 43 validated but wholly unconsumed

--stop-after resource-list
    parse the bounded standard list and stop before response or resolution
```

All earlier stop points remain unchanged. The new route uses the same UDP
source endpoint and the same retained driver through list publication, sends
exactly the already-established `new` and `sendres` requests, and emits no
additional response packet.

## Exact post-list and client-response boundary

Every accepted stock list ends exactly at the end of its decompressed service
payload. There is no confirmed following server opcode in that payload. The
post-list decoder therefore publishes
`PostResourceListBoundaryKind::exact_end_of_payload` and a separate
`ResourceClientResponseBoundary` without consuming or fabricating another
server message.

The outbound stock evidence is metadata only:

- 51 normal runs contain a 62-byte reliable carrier: a 10-byte fragment
  descriptor followed by a 41-byte semantic fragment and an 11-byte
  contemporaneous tail;
- the leading raw byte value 1 belongs to the fragment descriptor and is not
  a semantic opcode;
- the 41-byte semantic fragment starts with neutral opcode candidate 5 and has
  SHA-256
  `451A85ADDBF2B6B2D05E9F424BDFCF711655803706EF6D59B116299D7B5D17C9`;
- three same-process/reconnect coalescing variants have reliable-body lengths
  64, 66, and 68 bytes.

No official mapping establishes the candidate's command semantics. M3.1.2
records only that a response is required by the reconstructed stock profile;
it provides no builder, queues no response, and sends no post-list packet.
M3.1.3 handles the later step in a distinct `ResourceClientResponseStage`: it
keeps the 10-byte descriptor and 11/13/15/17-byte contemporaneous tail outside
the selected 41-byte semantic unit, names the wire message neutrally
`Opcode5ResourceResponse`, and requires path-free provider material for the
local byte-count and fixed 16-byte opaque fields. M3.2.1 can supply that
material from an explicitly selected, prepared local provider whose fixed target
is `tempdecal.wad`; it is not derived from a list entry. Without provider
selection, the continuation still returns typed `provider_required` and sends
nothing. See
[GoldSrc post-resource client response](GOLDSRC_RESOURCE_CLIENT_RESPONSE.md)
and [resource-consistency provider boundary](RESOURCE_CONSISTENCY_PROVIDER.md).

## M3.2.1 local-resolution projection

`ResourceListState` remains owning untrusted wire metadata. A separate
`GoldSrcResourceNameMapper` applies an evidence-gated, byte-exact mapping before
any open:

| Resource type | M3.2.1 result |
| --- | --- |
| sound | file-backed virtual name `sound/` plus the wire name |
| model | file-backed wire name |
| generic | file-backed wire name |
| event script | file-backed wire name |
| decal | metadata-only; no local-file mapping claimed |
| unsupported type/profile | typed `unsupported_mapping` |

These mappings are limited to the supported standard profile. Their evidence is
repeated stock resource-name shape plus a user-owned installation layout, with
the pinned public Valve headers used only as a secondary category cross-check.
They are not a universal mapping inferred from filename extensions.

The supported name encoding is printable ASCII with `/` separators. The mapper
does not decode, normalize, expand, repair, or convert bytes through a Windows
code page. It rejects empty names, NUL, absolute/drive/UNC/device forms,
backslashes, `.`/`..`/empty components, ADS colons, control/DEL/non-ASCII bytes,
trailing dot/space, reserved Windows device components, and configured
component/path/count limits before the resolver is called.

Resolution tries exact ASCII spelling first. Only when no exact directory entry
exists may it perform a bounded ASCII-only case-insensitive lookup. One match is
accepted with its actual spelling; multiple matches are `ambiguous`. There is no
locale-dependent, Unicode, or Windows-current-code-page case folding.

`LocalResourceInventoryBuilder` walks the list in wire order and transactionally
publishes an immutable `LocalResourceInventoryState`. Each entry retains only
correlation metadata and one of `resolved`, `missing`, `unsafe_name`,
`unsupported_name_encoding`, `unsupported_mapping`, `ambiguous`, or `io_error`.
A resolved entry may retain an equality-only file identity, root ID, and handle
size, but no handle, absolute path, raw bytes, digest, or interpreted `u24`
declared-size value. Fatal configuration or allocation failure publishes no
partial inventory.

This inventory is deliberately not readiness, precache, download, cache, asset,
or renderer state. It neither decides whether sign-on may proceed nor generates
the fixed provider response. M3.2.2 owns readiness and precache semantics.

## Deterministic verification and CI

The committed fixture is an independently written synthetic literal; it is
not produced by a production resource-list encoder. Parser tests cover the
exact fixture, explicit opcode cursors, every shorter byte prefix, every
shorter bit length, wrong opcode, zero/count limits, all confirmed types,
unsupported type/flag/custom profiles, unterminated and bounded names, raw
malicious byte strings, duplicate identity, raw size-code and checked-total
limits, ownership, terminal fill, nonzero fill, and trailing-data rejection.
Post-list tests prove exact EOP, geometry mismatch rejection, no opcode scan,
and no response action. Stage tests prove atomic publication, event-capacity
preflight, unsupported custom profile, timeout, cancellation, secondary stream,
endpoint drift, and exact-once cleanup.

The production-composed loopback fake HLDS repeats these sets:

| Integration profile | Result required by the test |
| --- | --- |
| baseline resource list | 20/20 |
| fragmented/reordered second transfer | 20/20 |
| map/list differential | 20/20 |
| malicious names with no filesystem behavior | 20/20 |

Every successful run checks one learned source endpoint, one `new`, one
`sendres`, one list publication, one exact post-list boundary, one
client-response-required event, zero response packets, and exact-once lifetime
release. The historical `resource-list-boundary` integration remains separate
and unchanged.

A separate production-composed negative suite drives the same full handshake
through truncated count/entry/name bodies, unsupported types and profiles,
duplicate identities, per-entry and checked-total size bounds, nonzero fill,
trailing data, wrong endpoints, missing fragments, malformed BZip2, duplicate
completed batches, inactivity timeout, cancellation, and event backpressure.
It publishes no partial list. An out-of-range index cannot be represented by
the confirmed unsigned 12-bit field, and a `uint64_t` arithmetic overflow
cannot be formed under the hard-bounded `u12` count times `u24` size-code
geometry; tests therefore exercise the exact representable domain and the
configured checked-total bound. No optional/custom layout is fabricated to
manufacture an otherwise unobserved negative case.

GitHub Actions builds the normal Visual Studio 2022 Win32 Debug target and runs
the complete CTest set, so resource-list codec, stage, post-list, CLI, and full
fake-HLDS tests require no Steam client, `hl.exe`, `hlds.exe`, authentication
ticket, Internet, GPU, or Half-Life assets. The ignored stock projector is a
manual evidence verifier and is not needed by CI.

M3.2.1 resolver, mapper, inventory, MD5, and production-provider tests use only
temporary synthetic roots and original literal file bytes. They do not turn
those project tests into stock interoperability evidence or claim that an
optional user-owned installation check has been completed.

## Public Valve header cross-check

The pinned SDK commit
`b1b5cf5892918535619b2937bb927e46cb097ba1` supplies only an independent
semantic cross-check. `third_party/halflife-sdk/engine/custom.h` declares
`resourcetype_t` values 0 through 6, `MAX_QPATH` 64, an in-memory
`resource_t` containing a name, type, index, size, flags, a 16-byte digest
array and player byte, and `COM_SizeofResourceList`.

That header does not define numeric service opcode 43 or its serialization.
The implementation never uses `sizeof(resource_t)`, C/C++ packing, its pointer
fields, or the header's in-memory layout as wire evidence. In particular, the
digest declaration does not establish a digest presence flag, algorithm, or
wire width for the uncaptured custom profile.

## Remaining evidence gaps

- no captured custom/player-resource entry establishes an optional-field,
  digest, player index, or custom-body grammar;
- type values 1 and 6 are present only in the public header and are not enabled
  by the standard wire profile;
- the meanings of observed flags bit 0 and unobserved bits 1 through 3 are not
  independently confirmed on this wire;
- `0xFFFFFF` is preserved as an opaque raw size code, not named as a sentinel;
- the `maxplayers 1` stock path did not reach a list, so it supplies no count or
  grammar differential;
- controlled active local-file, response-loss, covering-ACK, duplicate, and
  next-payload scenarios have not yet produced a complete restoration-attested
  corpus or tracked stock projection;
- live project-client-to-stock-HLDS sign-on remains pending a production
  authentication provider and is separate from signed-stock capture evidence.

## Explicitly absent and continuation

M3.1.2 adds no response builder, consistency codec, arbitrary resource
command, filesystem lookup, path policy, VFS mount, resource resolution,
download, cache, precache, BSP/WAD/MDL/SPR parsing, asset-manager integration,
map loading, snapshots, gameplay, Steam authentication provider, or graphics
work. The historical `--stop-after resource-list-boundary` still stops before
opcode-43 body parsing; `--stop-after resource-list` decodes the bounded list
and stops before the required response or any resolution.

M3.2.1 supplies production local material only when the user explicitly selects
the local provider and supplies a validated root. The active stock verifier
still refuses projection because no completed restoration-attested M3.1.3 runs
exist; production implementation and deterministic fake-HLDS coverage do not
imply active project-client-to-stock success. M3.2.2 now correlates this owning
list with the independent inventory, without using its opaque `u24` size code
or flags as readiness policy, and builds the separate metadata-only manifest.
See [local readiness](LOCAL_RESOURCE_READINESS.md) and
[precache manifest](PRECACHE_MANIFEST.md). M3.3 remains the safe download/cache
boundary, and M3.1.4 remains conditional on sufficient next-message evidence.
