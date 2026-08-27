# GoldSrc runtime delta values

## Confirmed inputs and evidence boundary

The existing immutable `DeltaSchemaRegistryState` is the sole schema source.
Accepted opcode-14 captures confirm seven ordered schema descriptions, field
names/order, base-type flags, significant-bit metadata and two fixed-point
multiplier metadata values. They do **not** confirm the runtime changed-mask,
scalar representation or enclosing entity-message grammar.

`GoldSrcDeltaValueDecoder` therefore has two sealed profiles:

- `stock_protocol_48_build_10210_evidence_pending` rejects before reading a bit
  and leaves the cursor and base object unchanged;
- `synthetic_neutral_v1` is an independently authored test/fake grammar and is
  never presented as stock GoldSrc behavior.

No field offset is used and decoded values are never written into an HLSDK or
other native C struct.

M4.6.2 has a separate usercmd-specific binding for the exact 15-field
`usercmd_t` descriptor. It validates every descriptor property and feeds only
the sealed synthetic usercmd delta codec; it neither changes this generic
decoder nor promotes its grammar to stock. The clean-room stock usercmd corpus
still contains zero accepted runs and zero verified move packets. See
[GoldSrc usercmd](GOLDSRC_USERCMD.md) and
[GoldSrc usercmd delta](GOLDSRC_USERCMD_DELTA.md).

## Generic owning state

`DeltaObjectState` owns the exact per-field schema descriptor used to construct
it (type flags, signedness, offsets, storage sizes, significant bits,
multipliers, wire indices and presence masks), plus an ordered value vector
aligned to that wire order. It has no pointer into the payload. Unchanged fields
are copied only from a base whose retained descriptor exactly matches the
selected schema. A missing required base, descriptor/profile mismatch or
value-count mismatch is a typed error. Publication is transactional.

The generic state remains the source of truth. Entity semantic projections stay
disabled until accepted stock captures correlate exact schema field names with
coherent runtime values.

## Synthetic-neutral grammar

This grammar exists only to exercise bounded mechanics:

1. A byte-aligned `u8` mask-byte count.
2. Exactly that many raw mask bytes.
3. Mask bit `i` (least-significant bit first) selects schema wire field `i`.
4. Selected scalar payloads are contiguous.
5. A zero count preserves an exact base object.

The decoder rejects excessive mask length, a bit beyond the schema, truncation,
non-exact bounded ranges and forbidden non-zero enclosing padding. This mask is
not reused from the opcode-14 field-description presence mask.

Synthetic scalar rules are explicit test policy:

- byte: fixed-width unsigned value; the reused published schema grammar rejects
  the signed modifier for byte fields, so signed-byte runtime support remains
  unavailable;
- short/integer: fixed-width unsigned or two's-complement signed value;
- float: quantized value multiplied by `postmultiply / premultiply`, with
  checked finite arithmetic and a configured magnitude bound;
- angle: unsigned quantized value multiplied by `360 / 2^bits`;
- string: little-endian 16-bit byte length followed by bounded owning bytes;
- time-window-8/time-window-big: typed evidence-pending, even in the synthetic
  decoder, until a captured server-time reference and wrap formula exist.

There is no wall-clock input, silent clamp, Unicode conversion, path
interpretation or command execution.

## Limits

`GoldSrcDeltaValueLimits` names default and hard project caps for fields per
object, mask bytes, string bytes, total value bytes, objects per message, delta
bits and numeric magnitude. Every configured value is validated; exact-limit
and cap-plus-one behavior is covered by tests.

The current decoder is deliberately a single-object API. The validated
`maximum_object_count_per_message` value is reserved for the future
evidence-backed enclosing message walker and is not claimed as an exercised
loop bound in M4.5.1.

The decoder reports the exact bit cursor and consumed bit count. Failed reads do
not publish a candidate object or mutate its base.
