# Stock Protocol 48 client-local data

## Status

`clientdata_t`, `local_state_t`, `weapon_data_t` and the pinned
`network/delta.lst` are public semantic/schema references. They do not prove
the runtime message opcode, envelope, changed mask, base object, time-window
context, weapon batch boundary or command acknowledgement.

The evidence boundary therefore retains these results:

- client-local message: `evidence_pending`;
- clientdata base policy: `evidence_pending`;
- server-time context: `server_time_required`/pending;
- weapon-data boundary: pending;
- decoded clientdata field count: zero.

A future `StockClientDataState` must own a schema-identified
`DeltaObjectState`, source cursor, base/reference metadata, runtime generation,
server-time association and per-field provenance. It must never expose or
reinterpret a native SDK struct.

Time-window values cannot be decoded with fabricated zero time. Clientdata is
not assumed to use the previous message as its base. A command-like field is a
tagged candidate until its domain is correlated with exact stock usercmd
sequences under loss and batching scenarios.
