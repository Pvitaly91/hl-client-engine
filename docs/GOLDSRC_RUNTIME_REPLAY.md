# GoldSrc protocol 48 runtime replay and client-state bridge

M4.7.1.2E adds the executable
`public_goldsrc48_runtime_replay_v1` profile. Its input is an ordered sequence
of project-owned records containing already extracted, owning, reassembled and
decompressed server-to-client service payloads. It is deliberately not a UDP,
netchan, PCAP or DEM parser. The pure in-memory session sends no packets and
does not read files, clocks, assets, Steam state or renderer state.

Each record keeps generation, a project record identity, replay ordinal,
source transport metadata, exact initial cursor, owning payload bytes and
compatibility profile as distinct values. Records are applied in supplied
order; the session never sorts them by server time or frame number. An unknown
opcode is returned at the exact A/C/D dispatcher cursor without scanning for a
later supported opcode.

## Session and transaction boundary

`RuntimeReplaySession::initialize` receives the validated schema registry,
the generation-bound baseline registry produced by the B decoder, max-client
context, exact schema names and bounded limits. `apply_record` copies the
existing `PacketEntitySnapshotState`, then calls the same
`GoldSrcPacketEntityDecoder::decode_and_apply` used by A/B/C/D tests and
checkers. That shared dispatcher remains the only control, clientdata,
weapondata and packet-entity wire parser.

After a complete successful A/B/C/D candidate, E performs exact semantic
projection into a copied `ClientWorldState`. Decoder state, both protocol
histories, record identity retention and the renderer-neutral observation are
moved into place only after projection and client-boundary validation succeed.
Malformed bodies or suffixes, missing bases, allocation/limit failures,
descriptor mismatches, and a bridge rejection therefore leave control state,
client and entity histories, current substates, publication revision and
`ClientWorldState` unchanged. History eviction occurs only in the staged
decoder candidate that is ultimately committed.

The default recovery policy is fail/stop. A caller may explicitly submit a
later recovery record. `entity_full_snapshot_required` and
`clientdata_no_base_required` remain distinct: an entity full snapshot cannot
repair clientdata history, and a clientdata no-base message cannot repair
entity history. No network resync request is emitted.

Record identity is not source sequence. Two control-only records with the same
source sequence but different identities/ordinals can be applied; a repeated
identity is `duplicate_record` when its complete retained fingerprint matches
and `conflicting_record` when it does not. Old replay ordinals are rejected.
Fingerprint retention is bounded and reaching the configured bound is a typed
failure rather than silently forgetting a prior identity. C/D continue to
enforce their own source-frame duplicate, conflict, modular ordering and exact
base rules for their substates.

`reset_generation` accepts a new validated schema/baseline initialization,
replaces A/B/C/D state, clears both histories, current observations and record
identities, and publishes an empty observation for the new generation. Static
world/resource attachments and `world_revision` are not changed. `finish`
closes the session; rewind is reset followed by replay from the beginning.

## Renderer-neutral ClientWorldState observation

`ClientWorldState` now owns an immutable `RuntimeClientObservationState`. It
contains generation and monotonic runtime publication revision, observed
server time, reconstructed packet-entity observations, receiving-client
observations, weapon-slot observations and separate source/freshness metadata
for control time, clientdata and entities. It contains no raw packet bytes,
delta descriptors, SDK memory layout, socket, filesystem path, resource handle
or ETW/WFP object. Existing scene/render code does not consume it, move the
camera, load a model or activate prediction.

An entity-only record retains the last client values but marks client
freshness `retained`; it does not claim a new clientdata observation. The
inverse applies to a clientdata-only record. Each retained substate preserves
the record identity, ordinal, source sequence and exact message cursor where
it was actually observed.

The narrow projection uses exact names and validates the corresponding
registered descriptor before reading the canonical `DeltaObjectState` value:

- entity `origin[0..2]`: signed float; `angles[0..2]`: angle, or the
  signed-float form used by the public custom-entity schema;
- receiving client `health`, `velocity[0..2]`, `view_ofs[0..2]`: signed
  float;
- weapon slot `m_iClip`: signed integer; `m_fInReload`: unsigned integer;
  `m_flNextReload` and `m_flNextPrimaryAttack`: signed float.

Names are exact; there is no fuzzy or wildcard binding and schema offsets are
not interpreted as native structures. A field absent from a valid mod schema
is `std::nullopt`, not zero. For example, Valve's reference `clientdata_t`
declares only `view_ofs[2]`, so X/Y remain unavailable in the literal checker.
A present field with the wrong descriptor or canonical value type is a typed
transaction failure. All unknown generic fields remain owned by the A/B/C/D
canonical state even when this narrow client DTO does not assign gameplay
meaning to them.

The semantic descriptor facts are cross-checked against Valve's pinned
[`network/delta.lst`](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/network/delta.lst)
at revision `b1b5cf5892918535619b2937bb927e46cb097ba1`. Wire decoding and base
selection continue to use the A/B/C/D contracts documented in
[runtime control](GOLDSRC_RUNTIME_CONTROL.md),
[entity baselines](GOLDSRC_ENTITY_BASELINES.md),
[packet entities](GOLDSRC_ENTITY_SNAPSHOTS.md), and
[clientdata/weapondata](GOLDSRC_CLIENTDATA.md), with ReHLDS revision
`6266cd23faee4a6e9cf3974f9605b2cadd86f0a4` and the GoldSrc-compatible Xash3D
route at revision `e9b63241616c390d4d1720ced80e88de2acf83a6` as their pinned public
cross-checks. No third-party implementation body is compiled or copied.

## Determinism and offline checker

The canonical observation hash uses explicit field order and numeric bit
representations. It excludes pointers, native padding, wall/presentation
clock, paths, publication revision and replay record geometry. Applying the
same ordered records immediately or between presentation-clock advances
produces the same decoded state/hash; each successful record is still a
separate transaction and publication.

`hlclient_runtime_replay_check` freezes independent literal bytes for the
three minimal schemas, four entity baselines and all runtime payloads. Its path
is bytes -> B baseline decoder -> shared A/C/D dispatcher -> E session -> the
actual `ClientWorldState` attachment. It demonstrates mixed full state,
client/entity deltas, add/remove/omission, older retained bases, client-only
and entity-only records, both missing-base outcomes, independent recovery,
malformed-suffix rollback, generation reset and deterministic replay.

From a configured Visual Studio Win32 tree:

```powershell
cmake --build build --config Debug --target `
  hlclient_runtime_replay_check hlclient_tests
.\build\bin\Debug\hlclient_runtime_replay_check.exe
.\build\bin\Debug\hlclient_tests.exe `
  '[goldsrc][runtime-replay]' --reporter compact
```

This is public-reference, offline interoperability work. At the E milestone it
had not decoded a stock runtime corpus. H subsequently completed functional
replay of `ededd068a0444ff497d98204eacec8a4` (323/323 records, zero failures),
which remains distinct from an accepted campaign corpus. I adds an explicit
[local-asset visual slice](STOCK_CAPTURE_LOCAL_ASSET_REPLAY.md) from that capture.
Live transport wiring, visual scene
projection, renderer/HUD integration, local-player authority, prediction,
weapon simulation, usercmd transmission, authentication and resource loading
remain outside M4.7.1.2E.

I preserves the A-H canonical hash field set for before/after comparison and
adds a separate visual semantic hash covering exact optional model/pose/render
fields and their missingness. These fields are projected by the same session
transaction after exact descriptor-name/type checks, never by another decoder.
