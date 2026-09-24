# GoldSrc protocol 48 entity baselines

`GoldSrcEntityBaselineDecoder` implements the documented `svc_spawnbaseline`
(opcode 22) grammar for a server-to-client, decompressed service payload. The
message is LSB-first: an 11-bit entity number, a 2-bit type, then a delta from
the null state. `type & 2` selects the custom schema; otherwise entity numbers
1..`max_clients` select the player schema and all others select the ordinary
schema. At each record boundary the GoldSrc branch first reads a complete
16-bit word: `0xFFFF` terminates the ordinary section. A non-terminator word is
rewound and its low 11 bits are the entity number. The terminator is therefore
not merely the 11-bit maximum entity index. It is followed immediately by a
6-bit instanced-baseline count; each instanced record uses the ordinary schema
and is keyed by `alternate_slot`. The final partial byte must contain zero
padding.

Initial baselines are delta-decoded from the protocol-defined null
`entity_state_t` with reference time base `1.0f`, matching the GoldSrc branch
of `CL_ParseBaseline`. They do not inherit a preceding runtime `svc_time`;
packet-entity values use staged runtime time separately.

This contract is based on the public [Half-Life 1 engine message reference](https://wiki.alliedmods.net/Half-Life_1_Engine_Messages)
and the GoldSrc path in [Xash3D FWGS `cl_parse.c`](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/parse/cl_parse.c)
(pinned source revision `e9b63241616c390d4d1720ced80e88de2acf83a6`, GoldSrc
protocol branch), cross-checked against ReHLDS `sv_main.cpp` revision
`6266cd23faee4a6e9cf3974f9605b2cadd86f0a4`, which writes `0xFFFF` in 16 bits
and then the 6-bit count. These are reference facts, not observations from our stock capture. Xash protocol 49, legacy
variants, HLTV/demo encodings, and unverified stock-build differences remain
outside this profile.

The decoder stages every record in `EntityBaselineRegistryBuilder`; any
truncation, unknown schema, malformed delta, duplicate key, or bad padding
returns a typed error and publishes no partial registry. Source cursor,
generation, payload sequence/acknowledgement and geometry are retained as
bounded metadata. A new server generation must use a new registry.
