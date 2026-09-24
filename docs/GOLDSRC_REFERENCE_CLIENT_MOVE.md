# Public GoldSrc 48 client-move wire contract (M4.7.2A)

This contract was recorded before implementation. It is an explicit offline
reference profile, not a promotion of the synthetic profiles or strict evidence
gates. No transmission, simulation, command execution or server acceptance is
part of this slice.

Follow-up: [M4.7.2B](GOLDSRC_REFERENCE_MOVE_TRANSMISSION.md) integrates this
unchanged codec with the existing planner/history/stage for an owned loopback
test session. This document's capture mode remains read-only and its results
are not a stock-server acceptance claim.

## Pinned protocol facts

Sources inspected at exact revisions (no implementation bodies/tests copied):

- Valve `b1b5cf5892918535619b2937bb927e46cb097ba1`:
  [network/delta.lst](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/network/delta.lst),
  [common/usercmd.h](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/common/usercmd.h):
  exact 15 descriptor fields/order, semantic names and unsigned 16-bit buttons;
  native offsets/layout are not the wire representation; weaponselect absent.
- ReHLDS `6266cd23faee4a6e9cf3974f9605b2cadd86f0a4`:
  [sv_user.cpp](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_user.cpp)
  establishes move header, count ordering, null-base chain and checksum context;
  [common.cpp](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/common.cpp)
  establishes per-usercmd byte alignment, sign-first magnitude and COM_Munge data;
  [delta.cpp](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/delta.cpp)
  establishes three-bit mask-byte count, ascending selected fields and scaling;
  [crc.cpp](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/crc.cpp)
  establishes the CRC32 table-byte salt and 60-byte coverage cap.
- Xash3D `e9b63241616c390d4d1720ced80e88de2acf83a6`, **PROTO_GOLDSRC only**:
  [engine/client/cl_main.c](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/client/cl_main.c)
  independently establishes capped length, checksum-before-munge and oldest-first
  chain; [public/crclib.c](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/public/crclib.c)
  explicitly uses little-endian CRC table bytes; [engine/common/protocol.h](https://github.com/FWGS/xash3d-fwgs/blob/e9b63241616c390d4d1720ced80e88de2acf83a6/engine/common/protocol.h)
  confirms opcode 2. Other Xash opcodes/protocol variants are not substituted.

## Independent grammar

At a validated byte cursor: opcode 2, one-byte munged-prefix length, one-byte
checksum, then a body containing one-byte loss/voice-loopback metadata,
one-byte backup count, one-byte new count, followed by that many usercmd deltas.
The low seven metadata bits are packet loss; high bit requests voice loopback.
They remain inert observations. Each count is representable in 8 bits (sum
0..510); the selected compatible bounded receiver subset allows total 0..62,
matching the pinned receiver's less-than-63 guard. This is separate from the
older simulation/planner limits and permits new=0 and msec=0.

Commands are oldest first: backups, then new commands. The first delta base
is the schema-defined all-zero command, not previous-packet history. Every
later base is the preceding reconstructed command. Every command starts a new
LSB-first bit block and ends at the next byte boundary; there is no uint16
per-command length. A delta has a 3-bit mask-byte count, mask bytes and ascending
selected fields. This profile accepts redundant masks but requires zero byte
padding and rejects out-of-schema bits. Widths/scales come from the session's
validated usercmd_t registry, not a synthesized registry.

Exact descriptor order (pre/post multipliers below use wire scale 4000 = 1):

| Index | Field | Type / bits | Pre / post |
| --- | --- | --- | --- |
| 0 | lerp_msec | unsigned short / 9 | 4000 / 4000 |
| 1 | msec | unsigned byte / 8 | 4000 / 4000 |
| 2, 3 | viewangles[1], viewangles[0] | angle / 16 | 4000 / 4000 |
| 4 | buttons | unsigned short / 16 | 4000 / 4000 |
| 5 | forwardmove | signed float / 12 | 4000 / 4000 |
| 6 | lightlevel | unsigned byte / 8 | 4000 / 4000 |
| 7, 8 | sidemove, upmove | signed float / 12 | 4000 / 4000 |
| 9 | impulse | unsigned byte / 8 | 4000 / 4000 |
| 10 | viewangles[2] | angle / 16 | 4000 / 4000 |
| 11 | impact_index | unsigned integer / 6 | 4000 / 4000 |
| 12, 13, 14 | impact_position[0..2] | signed float / 16 | 32000 / 4000 |

Binding validates names/order/type/signedness/width/scales against the captured
registry, ignoring native offsets/storage metadata and descriptor presence bits.

Unsigned values retain every bit. Signed fields have sign in the lowest bit
and magnitude in the remaining width-1 bits, NOT two's complement. Movement
quantization truncates toward zero; movement ticks have unit 1, impact ticks
unit 1/8. Angles retain unsigned 16-bit turns, one tick = 360/65536 degrees.
Own typed wire commands retain these integral quantities without float
roundtrip or fabricated command numbers. msec=0, independent impact fields
and the complete button mask are network values, not simulation policies.

M4.7.2G uses the separately typed project jump/duck actions in the controlled
live adapter. Valve's pinned
[common/in_buttons.h](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/common/in_buttons.h)
defines `IN_JUMP = 1 << 1` and `IN_DUCK = 1 << 2`; those are exact reference
wire bits, independent of the project's action-enum values. The codec retains
the full existing 16-bit field; the live adapter restricts which bits it may
produce for this slice.

The header length is min(decoded body bytes, 255), not a complete-message
length when saturated. Only complete four-byte blocks in that prefix are
transformed; its final 0..3 bytes and the remaining body are unchanged. Parse
counts/deltas to establish actual end, then verify the capped length. Following
client messages begin at that exact byte. No opcode scanning is allowed.

## Transformation and checksum

Move transformation is distinct from already-undone netchan transformation.
Its 16 data bytes, sourced from the pinned COM_Munge table, are:
`7a 64 05 f1 1b 9b a0 b5 ca ed 61 0d 4a df 8e c7`.
For block i and destination byte j, encode is byte reversal with XOR key:
`out[j] = in[3-j] XOR byte(~sequence,3-j) XOR byte(sequence,j)
XOR (a5 OR (j<<j) OR j OR table[(i+j)%16])`.
Decode applies the same per-byte key into the reversed destination. This
bytewise equation avoids native word pointers, alignment and host endianness.

Checksum input is the first min(body size,60) **unmunged** body bytes, including
metadata/counts/command padding, followed by four salt bytes. Generate the
standard reflected CRC32 table with polynomial 0xedb88320; salt starts at byte
offset sequence%1020 in its 1024-byte little-endian serialization. CRC initial
state is 0xffffffff, final XOR is 0xffffffff; only the low eight result bits
are transmitted. Header opcode/length/checksum are excluded. Compute before
encoding the move transform, and after decoding it. Use the enclosing C-to-S
source sequence, never ACK/ordinal/time. This short check is not cryptographic.

## Reuse and isolation

The new profile uses the existing schema binding table, BitReader/BitWriter,
GoldSrcDeltaValueDecoder, GoldSrcMoveChecksum, corpus loader, transport replay
and sign-on schema registry. The public usercmd-specific signed delta profile
does not alter A-I's existing entity/runtime profile interpretation or hashes.
The previous synthetic codec, command history, planner and transmission stage
remain unchanged and cannot accept the new wire-command type by accident.

Failures publish no partial message or command collection. Source generation,
delivery/payload ordinal, netchan sequence and reliable/fragment provenance
remain distinct. No input command number is inferred from a packet number.

## Verification status

Primary result: `client_move_codec_capture_verified` (offline only).

Measured against the unchanged functional capture
`ededd068a0444ff497d98204eacec8a4`, structural SHA-256
`614a07db0d29c13cc7a121c636deeda9e75e12bb75956dcb3ac609896c78137c`:

| Measurement | Result |
| --- | --- |
| Delivered datagrams / transport payloads / C-to-S payloads | 1184 / 1169 / 835 |
| Move messages / matched checksums / mismatches | 768 / 768 / 0 |
| Declared backup / new command records | 1536 / 1506 |
| Semantic decode-encode-decode / byte-exact re-encoding | 768 / 742 |
| Unsupported boundaries / failures | 0 / 0 |
| Last C-to-S payload ordinal / end byte:bit | 1168 / 20:0 |
| Deterministic result hash (two identical runs) | 6979080153849632397 |

All 26 non-byte-exact messages contain redundant explicit unchanged fields
(29 fields total). The canonical encoder omits them; decoded command values
are identical. Captured mask-byte counts were minimal. Negative zero and
nonminimal masks are additional equivalent representations covered by own
literals; original bytes are never returned as newly encoded output.

There are 3042 command records including repeats/backups, not 3042 unique
actions. Nonzero field counts: lerp_msec=3040, msec=3040, yaw=2961, pitch=1939,
lightlevel=2947; two records have msec=0. Movement, buttons, impulse, roll and
impact fields are zero in this capture. It does not validate observed walking,
jump/fire or weapon switching; weaponselect is absent from this descriptor.
No ACK is treated as execution acknowledgement.

The narrow walker encountered only clc_nop (507), clc_move (768),
clc_stringcmd (15), clc_delta (745), clc_resourcelist (1). Nop consumes one byte;
stringcmd consumes its bounded NUL-terminated string; delta consumes its one
byte argument; resource framing reuses the existing resource-response parser
and exact zero-entry three-byte form. Strings and metadata remain inert.
Other opcodes stop at a typed unsupported boundary, without scanning ahead.
At most one move is accepted per payload, matching the pinned receiver guard.

Independent literal tests include the three-command signed/full-buttons chain,
zero/default command, different sequence salts, known checksum-changing
metadata, transform prefixes, redundant masks and negative zero. Exhaustive
bounded roundtrips additionally cover command totals 0..62, backup/new splits,
body sizes above 255, byte/bit truncation, invalid masks/counts/padding/context,
owning lifetime and resource limits. Literals are own minimal inputs, not
private packet extracts or foreign engine tests.

Focused tests: 7 cases / 8465 assertions pass in Win32 Release, Debug and
ASan Debug. Affected checker/application/test builds pass in all three.
The selected 341 offline usercmd/checksum, A-I and shared transport regression
cases have 340 passes, one skip and zero failures in each configuration. The
existing corpus mutation case skips unavailable directory-symlink creation on
this host; this is not counted as passed. ASan Debug actual-capture checking
also matches the Release result and reports no sanitizer failure.
All eight application offline replay smoke tests pass in each configuration.
The historical pre-M4.7.2F signed-wire correction A-I null-renderer replay was
input=323, applied=323, failed=0, pending=0, entities=29, health=200,
slot2_clip=34, canonical_hash=14031366596970435596. With the same canonical
field set and correct public GoldSrc sign-plus-magnitude scalar decoding, the
current replay remains 323/323 with zero failures and has health=100 and
canonical_hash=7014210005320501317.

From the existing worktree:

```powershell
Set-Location 'D:\DEV\CPP\HLC-steamcfg-5e48b7c1'
.\build\bin\Release\hlclient_client_move_check.exe --capture '.\manual-artifacts\research-runtime-capture\ededd068a0444ff497d98204eacec8a4'
```

The checker only accepts this capture-root argument, never checksum/sequence
overrides. It uses the production functional corpus loader, transport replay
and captured sign-on registry; no second netchan or raw-filename reader exists.
Per-message defaults are 62 commands / 2048 bytes (hard byte ceiling 8192);
the walker bounds payload bytes to 1 MiB, message count to 1024 and retained
commands to 4096. Source generation and full masked 30-bit sequence are explicit.
Failure publishes no partial command collection and cannot modify history.

Local logs and preservation inventories are under the ignored
`manual-artifacts/research-runtime-capture/move-a-prechange-20260920` directory.
No network submission, live server acceptance, prediction or simulation is
claimed. Private capture bytes and game assets are not fixtures or Git inputs.

Proposed commit: `Implement offline reference GoldSrc client-move codec`.
Scope: new reference codec header/source, checker, own literal tests and this
contract; narrow additions to schema binding, delta/checksum profiles, CMake,
README and the five usercmd boundary documents. Stage only this slice's hunks
in already-dirty shared files. No commit or push is performed in this pass.
