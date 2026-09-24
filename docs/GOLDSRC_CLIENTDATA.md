# GoldSrc protocol 48 clientdata and embedded weapondata

M4.7.1.2D adds the executable
`public_goldsrc48_clientdata_v1` profile for the ordinary game-client form of
`svc_clientdata` (15). It reconstructs an owning `clientdata_t` delta object
and 64 indexed `weapon_data_t` slot objects, publishes them in an immutable
generation-bound client frame, and composes that state with M4.7.1.2A/C's
runtime-control and packet-entity transaction. The implementation and literal
fixtures are project-owned. They are not a stock build 10210 capture or
interoperability proof.

## Wire contract

The opcode is byte-aligned. Its ordinary-client body is LSB-first:

1. one bit states whether an explicit delta base is present;
2. when present, an eight-bit tag is the low byte of the referenced server
   transport/frame sequence;
3. one `clientdata_t` delta follows, using the shared public GoldSrc delta
   grammar;
4. each weapon record begins with continuation bit one, a six-bit wire slot
   index, then one `weapon_data_t` delta;
5. continuation bit zero terminates the weapon list;
6. remaining bits through the next byte boundary must be zero, and the next
   service opcode begins at that exact byte.

There is no client-field count or weapon-record count in this message. The
delta masks and the terminating continuation bit determine its length. Even
after all 64 possible weapon slots have appeared, the terminator is consumed.
The public server writer visits slots 0 through 63 once in ascending order, so
this profile rejects duplicate and descending indices instead of sorting or
silently replacing them. Index 0 and index 63 are both valid.

The contract is established from ReHLDS
[`SV_WriteClientdataToMessage`](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_main.cpp)
at revision `6266cd23faee4a6e9cf3974f9605b2cadd86f0a4`: opcode, base flag and
eight-bit tag, null/exact prior client-frame base, `clientdata_t` delta,
0..63 weapon loop, continuation/index fields, `weapon_data_t` delta, and final
zero bit. Xash3D FWGS's pinned GoldSrc dispatcher
[`cl_parse_gs.c`](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/parse/cl_parse_gs.c)
routes opcode 15 through the GoldSrc clientdata parser, and its shared
[`CL_ParseClientData`](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/parse/cl_parse.c)
cross-checks the same fields, null/prior bases, slot loop, and current
`svc_time` context at revision
`e9b63241616c390d4d1720ced80e88de2acf83a6`.

Valve's pinned
[`network/delta.lst`](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/network/delta.lst)
at revision `b1b5cf5892918535619b2937bb927e46cb097ba1` confirms the public
`clientdata_t` and `weapon_data_t` descriptor families and representative
signed, quantized, string, and timer fields. Runtime decoding still uses the
validated server-supplied schema registry: it does not hardcode Valve's list
as the only schema a mod may use.

The proxy/HLTV sender form can have an opcode-only body. It is a different
receiver contract. `proxy_or_hltv` is therefore a typed
`unsupported_receiver_mode` in this slice; truncation never retries that form.
Xash protocol 49, legacy Xash 48, demo-only variants, and ReHLDS extensions are
not accepted under this profile.

## State and base selection

The client frame owns a generic `DeltaObjectState` bound to `clientdata_t`, 64
wire-indexed generic objects bound to `weapon_data_t`, an exact transport
reference, optional exact base reference, generation, staged server time,
source payload/cursors, statistics, and compatibility profile. Packet bytes
are never cast to an HLSDK native structure. Unknown field names remain inert
typed values when their descriptor type and limits are supported. Unsupported
types, invalid widths/multipliers, non-finite results, or unbounded strings are
typed failures.

With no explicit base, client and weapon slots start from schema-defined
zero/empty values. This rule is used only for the wire no-base branch. With an
explicit base, both clientdata and every weapon slot start from the exact
committed client frame in the current generation. No entity baseline, entity
snapshot, latest client frame, ring index, or zero-filled fallback substitutes
for a missing reference.

The eight-bit tag is resolved in the same acknowledged server-frame sequence
domain as packet entities: `distance = (current_low8 - tag) mod 256`, with
distances 1 through 62 valid in the GoldSrc 64-frame update window. The full
30-bit transport identity is reconstructed and looked up exactly. Generation,
profile, client schema, weapon schema, and committed client substate must all
match. Missing, evicted, incompatible, current-tag, or over-window references
fail with `no_base_message_required`; this offline layer sends no request.

A weapon record changes the addressed wire slot relative to that same frame's
slot. An omitted slot is inherited; an empty update list therefore means no
weapon slot changes, not empty inventory. A transmitted delta may set every
field back to its schema default. Wire slot index, `m_iId`, active weapon, and
inventory ownership remain distinct and are not inferred from one another.
Signed clip/timer values are retained. Timer-like ordinary float fields use
their descriptor scaling; only declared time-window types use
`server_time - raw / scale`. No wall-clock or render-time adjustment occurs.

## History and atomic dispatch

Client history is immutable, bounded by frame count and logical retained value
bytes, and separate from entity history. It accepts gaps in transport
sequences, resolves older retained frames rather than the latest frame, and
rejects duplicate, conflicting same-sequence, old, ambiguous, evicted, and
cross-generation references. An entity-only source frame creates no client
observation and cannot become a clientdata base. A clientdata-only source frame
creates no entity snapshot.

The shared packet dispatcher stages controls, clientdata/weapondata, packet
entities, histories, fingerprints, and current pointers in one candidate. Thus
`svc_time`, `svc_clientdata`, `svc_packetentities`, and a supported control
suffix can share one source-frame identity without a false duplicate between
the independent substates. A repeated clientdata message in one payload, a
malformed weapon record, entity body, or suffix discards the candidate and
leaves every committed substate unchanged. Eviction occurs only inside a
candidate that is ultimately published.

An established netchan normally drops stale and duplicate transport packets
before service dispatch. These decoder/history checks are additional defenses
for direct offline use and exact base identity. The transport ACK is not a
usercmd ACK; a client frame reference is not a command number or prediction
authority.

## Offline checker

`hlclient_clientdata_check` uses literal GoldSrc-layout schema, baseline,
clientdata, weapondata, entity, and control bytes. It demonstrates no-base
frames with zero and multiple weapon records, an exact delta, an older retained
base, missing-base rollback, no-base recovery, a mixed client/entity frame, and
generation isolation. It performs no network, Steam, WFP, OpenGL, game launch,
filesystem command, or privileged operation.

From a configured Visual Studio Win32 tree:

```powershell
cmake --build build --config Debug --target hlclient_clientdata_check hlclient_tests
.\build\bin\Debug\hlclient_clientdata_check.exe
.\build\bin\Debug\hlclient_tests.exe '[clientdata]' --reporter compact
```

Weapon simulation, HUD/renderer application, prediction/reconciliation,
usercmd transmission, authentication, general game user messages, and resource
downloads remain outside M4.7.1.2D.

## Receiving-client movement observation extension

M4.7.2E projects exact optional `origin[0..2]` values from the already decoded
`clientdata_t` object alongside the existing receiving-client velocity. The
projection uses the current session's validated schema and generic delta
object; it does not inspect packet entities, infer a player entity number,
consume `svc_setview`, or run local movement/prediction. Missing or
type-incompatible fields remain unavailable or fail the typed projection.

This later origin member is intentionally excluded from the stable A-H
`runtime_observation_canonical_hash` contract. Historical canonical values
therefore remain comparable and are independently regression-checked, while E
evaluates the typed origin/velocity observations directly.
