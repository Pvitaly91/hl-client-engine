# GoldSrc client-move message boundary

## Profile status

`GoldSrcClientMoveMessageCodec` executes only
`synthetic_client_move_v1`. The
`stock_protocol_48_build_10210_evidence_pending` profile fails before encoding
or decoding because the clean-room corpus currently has zero accepted runs and
zero verified move packets. The synthetic opcode `0xE1` is deliberately outside
the stock claim; it must not be called `clc_move` or submitted to an arbitrary
production server.

The codec owns a complete byte-aligned synthetic envelope. It does not own a
socket, a `NetchanDriver`, scheduling, history mutation, or a native
`usercmd_t`.

## Synthetic envelope

The exact `synthetic_client_move_v1` layout is:

| Byte/region | Width | Meaning |
| --- | ---: | --- |
| 0 | 1 byte | synthetic opcode `0xE1` |
| 1 | 1 byte | synthetic checksum |
| 2 | 1 byte | `synthetic_loss_metadata` |
| 3 low nibble | 4 bits | backup-command count |
| 3 high nibble | 4 bits | new-command count |
| each command | 2 bytes | little-endian padded delta bit length |
| each command | `ceil(bits/8)` bytes | one byte-padded synthetic usercmd delta |

The codec requires strictly increasing commands encoded oldest first. The
packet planner supplies selected already-submitted backups first, followed by a
contiguous history range beginning at the first never-submitted command. At
least one new command is required. The count fields must agree exactly with the
owning command collection, and each nibble is limited to 15 in addition to the
configured backup/new/aggregate limits.

The first delta uses a verified all-zero synthetic default command as its base.
Every later delta uses the immediately preceding command. The project-local
`GoldSrcUserCmdSequence` is not in the envelope; decode receives the first
identity in `GoldSrcClientMoveDecodeContext` and assigns consecutive identities
with checked exhaustion. Sample-time metadata is also supplied by the caller,
not recovered from the envelope.

The per-command `uint16` length is the delta's padded bit length. Empty or
truncated deltas, length overflow, packet-budget overflow, wrong command order,
count mismatch, and sequence exhaustion are errors. The decoder's
`require_exact_end` policy rejects trailing bytes; `leave_trailing_bytes`
returns the exact consumed and next byte offsets.

## Checksum coverage

The synthetic checksum excludes bytes 0 and 1. Its body begins at byte 2 and
continues through the final encoded delta, including the count/loss metadata,
each length prefix, every changed field, and each delta's zero padding. The
outgoing Netchan sequence is an explicit input to the checksum. See
[GoldSrc usercmd checksum](GOLDSRC_USERCMD_CHECKSUM.md) for the complete
synthetic algorithm.

`synthetic_loss_metadata` is an inert project fixture. It does not establish a
stock packet-loss byte, its meaning, or its relation to server loss reporting.

## Packet planning and carrier

`GoldSrcUserCmdPacketPlanner` with `synthetic_backup_v1` selects up to the
configured new-command maximum from the first unsent history entry and then up
to `desired_backup_commands` already-submitted entries immediately before it.
The default split allows 2 desired backups, at most 7 backups, 8 new commands,
16 total commands, 1024 bytes, and 8192 bits. It encodes the message during
`prepare()` against an exact caller-supplied outgoing Netchan sequence.

`GoldSrcUserCmdPacketPlan` is move-only and carries ordered owning command
references, counts, history/planner revisions, plan identity, outgoing
sequence, encoded message, and expected byte/bit budgets. The transmission
stage calls `commit()` only after the matching driver send succeeds;
`abandon()` consumes the plan without mutating transmission counts. Foreign,
stale, reused, over-budget, and revision-exhausted plans fail transactionally.

The transmission stage submits the encoded message as one unreliable suffix
through the retained `NetchanDriver`. Existing reliable composition may share
the owning Netchan packet, but this codec neither labels nor rewrites that
prefix. Usercmd data is never queued as a reliable workaround.

## What remains pending

The following stock facts remain unproven: semantic opcode, envelope offsets,
loss/metadata field, count widths/order, per-command framing, default/base
policy, trailing-message policy, reliable-prefix coexistence, checksum
coverage, backup/new selection, and server acceptance. Synthetic bytes are not
stored as stock evidence and do not close any production sign-on gate.
