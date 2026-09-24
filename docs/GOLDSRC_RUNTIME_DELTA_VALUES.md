# GoldSrc protocol 48 runtime delta values

Profiles `public_goldsrc48_delta_v1` and the retained entity-specific
`public_goldsrc48_entity_delta_v1` decode the GoldSrc delta mask as a 3-bit
mask-byte count followed by the raw mask bytes, then selected fields in schema
order. The generic profile is used for `clientdata_t` and `weapon_data_t`; the
entity profile remains distinct for A/B/C compatibility. Both use the
validated `DeltaSchemaRegistryState`; offsets are schema metadata, never
native-struct addresses. Public GoldSrc signed scalar fields put the sign in
the first transmitted bit and the magnitude in the remaining bits; they are
not two's-complement values to sign-extend. Synthetic fixtures retain their
separate two's-complement contract. Floats use declared multipliers, angles map
unsigned wire values to degrees, strings are NUL-terminated bytes, and
`DT_TIMEWINDOW_8`/`DT_TIMEWINDOW_BIG` require a finite explicit server-time
context. The decoder accepts unaligned bit cursors and reports the exact next
bit; no opcode scanning or guessed resynchronisation is performed.

The field rules are cross-checked against
[Xash3D FWGS `net_encode.c`](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/common/net_encode.c),
and the alternate GoldSrc signed representation against
[`MSG_ReadSBitLong` in `net_buffer.c`](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/common/net_buffer.c).
The implementation is original and uses this repository's `BitReader`, schema
registry, value types, limits, and typed errors. The existing synthetic and
strict evidence-pending profiles are unchanged.

This signed-wire correction changes decoded semantics without changing the
canonical observation field set. For functional capture
`ededd068a0444ff497d98204eacec8a4`, the corrected null-renderer replay remains
323/323 applied with zero failures, but its canonical hash is
`7014210005320501317`. The former value `14031366596970435596` is retained only
as historical pre-correction evidence; it represented public signed fields as
two's-complement values and must not be used as the current replay oracle.

`DeltaObjectBuilder::build_default` creates schema-typed zero or empty values
only when an enclosing reference grammar explicitly selects a null/default
base (currently ordinary `svc_clientdata`'s no-base branch). It is not a
fallback for a missing referenced frame.

For time-window fields, the GoldSrc wire value is an unsigned integer even
though the reconstructed result can be earlier than the time base. The public
profile applies `time_base - raw / scale` (equivalently
`(time_base * scale - raw) / scale`) after validating a finite staged time.
This is distinct from ordinary signed integer/float decoding. The rule is
checked against pinned revision
`e9b63241616c390d4d1720ced80e88de2acf83a6`; the source link intentionally
does not track `master`.
