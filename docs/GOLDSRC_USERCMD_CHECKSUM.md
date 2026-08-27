# GoldSrc usercmd checksum boundary

## Evidence gate

`GoldSrcMoveChecksum` implements only `synthetic_crc8_v1`. The reserved
`stock_protocol_48_build_10210_evidence_pending` profile returns
`stock_evidence_pending`. Public Valve CRC declarations are function-name and
semantic references only; without controlled captures they do not establish a
Protocol 48 move checksum algorithm, table, seed, coverage, padding policy, or
sequence relationship.

The stock checksum status is therefore pending: zero accepted stock usercmd
runs and zero verified move packets.

## Exact synthetic algorithm

The synthetic checksum is an 8-bit, MSB-first CRC with polynomial `0x07`,
initial value `0xA7`, and no final XOR. For every covered byte:

```text
crc ^= byte
repeat 8 times:
    crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1)
    retain the low 8 bits
```

The update order is exact:

1. four bytes of `outgoing_netchan_sequence`, least-significant byte first;
2. every byte of the caller-owned body in order;
3. one domain byte equal to `body_bit_length & 7`.

The final domain byte distinguishes equal owning byte strings with different
meaningful-bit counts. All owning body bytes participate, including zero
padding in the final byte; `GoldSrcMoveChecksumResult::padding_bits_participate`
is therefore true.

For `synthetic_client_move_v1`, the body starts at envelope byte 2. It includes
the synthetic loss byte, count byte, every little-endian delta-length prefix,
and every padded delta byte. The synthetic opcode and checksum slot are not
covered. Because that envelope is byte-aligned, its domain byte is zero.

## Geometry and bounds

`GoldSrcMoveChecksumContext` contains only the outgoing sequence and exact body
bit length. A nonempty body must describe meaningful bits in its final owning
byte: more than `(size-1)*8` and no more than `size*8`. An empty body permits
only zero bits. The default coverage bound is 1024 bytes and the hard synthetic
bound is 8192 bytes. Invalid geometry, invalid configuration, and coverage
overflow fail without returning a checksum.

The checksum API cannot override or advance a Netchan sequence. The
transmission stage obtains the next sequence from a move-only
`NetchanOutgoingContextPlan`, passes that value into packet planning and
encoding, and submits the result only through the matching driver context.
Stale or foreign context prevents submission; checksum bypass is not available.

## Transaction boundary

Checksum computation is pure and does not mutate a command, history, packet
plan, Netchan session, or input one-shot state. A checksum is placed into the
synthetic envelope only after complete body construction succeeds. Decode
recomputes it over the exact consumed message body and rejects mismatch before
publishing `GoldSrcClientMoveMessage`.

Stock evidence still must establish the checksum width, algorithm, constants,
coverage start/end, padding participation, decoded-body versus encoded-byte
coverage, exact outgoing-sequence input, and interaction with simultaneous
reliable data or retransmission. No synthetic result may be promoted to the
stock profile.
