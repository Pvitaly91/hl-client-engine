# GoldSrc protocol 48 packet-entity snapshots

M4.7.1.2C adds the executable
`public_goldsrc48_packet_entities_v1` profile. It consumes an owning,
decompressed server-to-client service payload and decodes `svc_packetentities`
(40) and `svc_deltapacketentities` (41) into immutable, owning snapshots. The
profile is based on public protocol references and project-owned literal
fixtures. It is not evidence that the path has been exercised against stock
build 10210.

The earlier `synthetic_neutral_v1` builder and the strict
`stock_protocol_48_build_10210_evidence_pending` profile remain separate. The
reference decoder does not rename, enable, or satisfy either evidence gate.

## Wire contract

All multi-bit values in the entity bitstream are LSB-first. Both opcodes are
followed by a little-endian 16-bit count of entities in the reconstructed
current packet set. This is not the number of update records. A delta header
then carries the low eight bits of the exact source transport sequence used as
its base. Bit reading starts at the next byte.

Full-message record numbers use these branches relative to the preceding wire
record number, initially zero:

- one bit `1`: previous number plus one;
- otherwise one bit `0` and a six-bit positive difference: previous plus the
  difference;
- otherwise one bit `1` and an absolute 11-bit entity number.

Delta-message records first carry the removal bit. Their next bit selects a
six-bit relative difference (`0`) or absolute 11-bit entity number (`1`). The
result must be nonzero and records must be strictly ascending. Duplicate,
descending, zero, out-of-range, and truncated forms are typed errors; the
decoder never sorts malformed input.

A non-removal record next carries the custom-entity bit. Custom selects
`custom_entity_state_t`; otherwise entity numbers from 1 through `max_clients`
select `entity_state_player_t`, and higher numbers select `entity_state_t`. If
the decoded sign-on baseline table has any instanced baselines, every
non-removal header also carries an instanced-baseline flag and an optional
six-bit slot. A full, non-instanced record additionally carries an
intra-message-baseline flag and optional six-bit backward record offset. The
selected common delta schema then determines the exact end of that record.

Sixteen zero bits terminate the entity record stream. They are consumed in
full and the remaining bits through the next byte boundary must be zero. The
next service opcode starts at that exact byte. No terminator/opcode scanning or
guessed-length recovery is used.

The contract is established from the GoldSrc route in [Xash3D FWGS
`cl_parse_gs.c`](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/parse/cl_parse_gs.c)
and its [shared client frame-validation path](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/parse/cl_parse.c)
at pinned revision
`e9b63241616c390d4d1720ced80e88de2acf83a6`. It is independently
cross-checked against the declarative/server-side writing rules in [ReHLDS
`sv_main.cpp`](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_main.cpp)
at revision `6266cd23faee4a6e9cf3974f9605b2cadd86f0a4`: opcodes and headers,
number branches, custom/instanced/intra-message flags, final entity-count
semantics, explicit removals, and the 16-zero-bit terminator. The Half-Life 1
[`svc_*` engine-message table](https://wiki.alliedmods.net/Half-Life_1_Engine_Messages)
independently identifies opcodes 40 and 41. No external
implementation function is compiled or copied into this project.

This profile covers GoldSrc protocol 48 only. Xash protocol 49, legacy Xash 48,
HLTV/demo-only behavior, and ReHLDS extensions are not silently accepted as
this grammar.

## Full and delta state

A full message starts an empty candidate packet set. Each entity is decoded
from exactly one explicit source: its identity baseline, an indicated
instanced baseline, or the indicated earlier compatible entity in the same
full message. It never inherits the previous runtime snapshot. Consequently,
an entity absent from a new full packet set is absent from the published
snapshot; this says nothing about final server-side object destruction.

A delta message resolves its stated base first. Existing records decode from
the exact entity in that base. New records decode from an identity or explicit
instanced baseline. An explicit removal removes an entity that must exist in
the base, while an entity omitted from the wire records is structurally shared
unchanged into the result. The 16-bit header count is validated only after the
complete result has been reconstructed. This bounded reference profile treats
an advertised/reconstructed count mismatch as malformed input instead of
publishing the inconsistent candidate.

Entity values use `GoldSrcDeltaValueDecoder` with the staged `svc_time` for the
same atomic service stream and generation. The sign-on baseline decoder keeps
its distinct protocol time base of `1.0f`; runtime packet snapshots do not
reuse that sign-on rule. A category change for an existing entity is rejected
with `schema_mismatch` because mixing objects decoded under incompatible
schema descriptors would not be a valid base selection.

## Frame identity and history

Transport sequence, eight-bit wire tag, resolved frame reference, generation,
and ring/history position are distinct types. The current transport sequence
is in the existing 30-bit netchan domain. For a tag, the decoder computes
`distance = (current_low8 - tag) mod 256`; only distances 1 through 62 are
accepted. The exact reference is `(current - distance) mod 2^30`, then looked
up by its complete 30-bit identity in the current generation. It is never
replaced with the latest, nearest, or any history entry sharing low bits.

The 62-frame limit follows the GoldSrc multiplayer 64-slot update history and
the GoldSrc client rejection of distance zero or distance at least the
63-valued update mask. This also makes a wrapped tag deterministic. Missing,
evicted, incompatible, or invalid bases fail with a typed result and
`full_snapshot_required`; no network request is sent by this offline layer.

History publishes immutable owning snapshots and is bounded by snapshot count,
entity/value limits, and aggregate retained value bytes. It permits skipped
source sequences because transport packets need not contain an entity message.
It rejects duplicate, conflicting same-sequence, old, and modularly ambiguous
source frames. Resetting the server/session generation replaces current state
and history, so an old-generation base cannot be selected even when numeric
tags match.

An established netchan normally rejects stale/duplicate transport packets
before service dispatch. The decoder retains independent frame identity and
payload fingerprint checks because its offline API can be called directly and
must not trust that upstream filtering occurred. Transport ACKs are never
treated as usercmd acknowledgements, and snapshot references are not command
numbers.

## Transactions and checker

The shared dispatcher stages supported runtime controls and packet-entity
messages together. A stream such as `svc_time`, entity message, then another
supported control message commits its control state, current snapshot,
history, fingerprints, and events only after the entire supplied payload is
valid. A malformed suffix, unknown opcode, limit failure, or retention failure
leaves all committed state unchanged and reports the exact first failing
cursor.

`hlclient_entity_snapshot_check` is a network-, Steam-, renderer-, WFP-, and
Administrator-independent executable. Its project-owned literal wire inputs
decode sign-on baselines, a five-entity full frame, update/remove/add delta,
an omission-preserving delta based on an older retained frame, missing-base
rollback, full recovery, and generation reset. These bytes are golden fixtures,
not stock captures.

From a configured Visual Studio Win32 build tree:

```powershell
cmake --build build --config Debug --target `
  hlclient_entity_snapshot_check hlclient_tests
.\build\bin\Debug\hlclient_entity_snapshot_check.exe
.\build\bin\Debug\hlclient_tests.exe `
  '[goldsrc][packet-entities]' --reporter compact
```

`svc_clientdata`, weapon data, stock usercmd, local-player authority,
filesystem/model loading, visual projection, and renderer integration remain
outside M4.7.1.2C.
