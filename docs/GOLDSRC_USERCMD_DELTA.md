# GoldSrc usercmd delta codec

## Compatibility gate

`GoldSrcUserCmdDeltaCodec` implements exactly one executable grammar:
`synthetic_usercmd_delta_v1`. The reserved
`stock_protocol_48_build_10210_usercmd_v1` and `stock_evidence_pending`
profiles return `stock_evidence_pending` before binding, state, or input-byte
inspection. Therefore the grammar below is a deterministic project
fixture and is not a claim about a stock client-move payload.

The codec requires a `GoldSrcUserCmdSchemaBinding` using
`synthetic_usercmd_schema_v1` and synthetic-profile base/current states. The
binding is the exact 15-entry descriptor table documented in
[GoldSrc usercmd boundary](GOLDSRC_USERCMD.md); the codec does not duplicate
that metadata or use public `usercmd_t` offsets.

## Synthetic changed-mask grammar

Every delta begins at a byte boundary and has this LSB-first layout:

```text
u8 mask_byte_count             canonical value 0, 1, or 2
u8 mask[mask_byte_count]       bit i selects wire field i
selected field bit values      contiguous, ascending wire index 0..14
0..7 zero padding bits         to the next byte boundary
```

The mask byte count is minimal: zero changed fields encode exactly as `{00}`;
a nonzero last mask byte is mandatory; and bit 7 of mask byte 1 is rejected
because it would select nonexistent field index 15. No fixed struct-sized mask,
native bitfield, byte scan, or alignment guess is used.

`BitWriter` and `BitReader` serialize both selector and scalar values
least-significant bit first: value bit 0 occupies the current byte's lowest
available bit, then the cursor advances monotonically.

For all 15 changed fields the meaningful length is 211 bits and the padded
length is 216 bits. `GoldSrcUserCmdEncodedDelta` reports bytes, padded
`bit_length`, `meaningful_bit_length`, padding count, changed-field count,
canonical mask-byte count, and the two mask bytes. Decode reports exact bits
consumed and next bit/byte offsets. `require_exact_end` rejects trailing bits;
`leave_trailing_bits` leaves the caller-selected remainder untouched.

## Field formulas

Unsigned byte/short/integer values are accepted only when they fit the exact
significant-bit width.

For a signed scaled float, with descriptor wire multipliers `pre` and `post`:

```text
q = round-half-away-from-zero(value * pre / post)
wire = q encoded as width-bit two's complement
value = sign_extend(wire, width) * post / pre
```

The 12-bit movement fields use `pre=4000`, `post=4000`, so their synthetic
step is 1. The 16-bit impact-position fields use `pre=32000`, `post=4000`, so
their synthetic step is 0.125. Overflow is an error; there is no saturation or
silent clamp.

Angles are normalized to `[0,360)` and use:

```text
q = round-half-away-from-zero(angle * 2^width / 360) mod 2^width
angle = q * 360 / 2^width
```

Pitch, yaw and roll use 16 bits. Equality and changed-mask decisions compare
these quantized wire values, not raw host floating-point bit patterns.

## Base and identity policy

`explicit_command` compares against the supplied immutable base.
`synthetic_default_state` is accepted only when every one of the base's 15
quantized wire values is zero. The first command in a synthetic client-move
message uses a verified default base; every later command uses the previously
decoded/encoded command.

The descriptor contains no command identity, input-frame identity, sample
time, or duration metadata beyond its `msec` field. Decode therefore requires
the caller to provide a valid `GoldSrcUserCmdSequence`, source input sequence,
sample time, and optional sample duration in
`GoldSrcUserCmdDeltaDecodeContext`. These values are project-local metadata and
are not claimed to be transmitted.

The accepted descriptor omits `weaponselect`. Both base and current commands
must consequently have `weapon_select == 0`; nonzero input fails explicitly.

## Bounds and transactionality

`BitWriter` writes only into a caller-owned fixed span with explicit start and
bit-length bounds. The encoder precomputes mask/value/padding geometry with
checked additions, allocates the final bounded vector once, and requires an
exact end. The decoder uses `BitReader` with explicit selected geometry,
requires byte-aligned start, rejects truncation and nonzero padding, and builds
the immutable command only after every field passes validation.

Failures publish no partial delta, no partial command, and no advanced success
cursor. Covered cases include invalid profile/binding/base, numeric overflow,
encoded limits, every mask/value truncation, nonminimal masks, out-of-schema
bits, nonzero padding, unexpected trailing bits, and invalid decoded state.

Stock field-mask grammar, value formulas, base-command rules and object
boundaries remain evidence-pending despite the accepted descriptor metadata.
