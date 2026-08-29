# GoldSrc usercmd boundary

## Status and evidence split

M4.6.2 provides a bounded typed usercmd model and a deterministic synthetic
tool/test path. It does **not** claim that the project can submit a move command
accepted by stock Protocol 48. The clean-room stock corpus currently contains
zero accepted runs and zero verified move packets. In particular, the stock
move opcode, envelope, checksum, input mapping, command cadence, backup policy,
and server acceptance remain evidence-pending.

The profiles keep those facts separate:

| Boundary | Executable profile | Reserved stock/pending profiles |
| --- | --- | --- |
| state | `synthetic_usercmd_v1` | `stock_protocol_48_build_10210`, `stock_protocol_48_evidence_pending` |
| input mapping | `synthetic_explicit_v1` | `stock_protocol_48_controlled_profile_v1`, `stock_protocol_48_evidence_pending` |
| schema binding | `synthetic_usercmd_schema_v1` | `stock_protocol_48_build_10210_schema_only`, `stock_protocol_48_evidence_pending` |
| sampling | `synthetic_fixed_step_v1` | `stock_protocol_48_controlled_profile_v1`, `stock_evidence_pending` |

Any non-synthetic state, mapping, binding, scheduler, codec, envelope,
checksum, or transmission route fails before usercmd encoding/submission.
Explicit pending profiles return typed evidence-pending results where the
boundary defines one. A synthetic success must never be reported as stock
compatibility.

## Clean-room verifier

`scripts/verify_stock_usercmd.ps1` is the research boundary. With no research
root it is a zero-write, zero-process pending check and reports:

```text
accepted-active-stock-runs=0 verified-move-packets=0
```

The guided form is an operational bounded capture harness. It accepts only
`boot_camp`, `crossfire`, or `stalkyard` under explicit game `valve` in a
marked isolated Half-Life copy. The exact root marker is
`.hlclient-research-isolated` containing
`HLCLIENT_STOCK_RESEARCH_ISOLATED_COPY_V1`. The script rejects filesystem
volume roots, repository overlap, primary or registered Steam-library paths,
reparse points, alternate data streams, hard links, wrong marker content,
noncanonical root binaries, wrong Valve launcher versions/signatures, and
pre-existing `hl.exe`/`hlds.exe` processes.

After validating the exact server process/loopback endpoint and client
process/path/top-level window, the harness owns one private IPv4-loopback
byte-preserving UDP relay. It learns exactly one client endpoint owned by the
validated client process and uses one connected upstream socket to the exact
server. The supported scenarios are `Baseline`, `DropOneClientSequenced`,
`DropTwoConsecutiveClientSequenced`, `DropOneServerSequenced`,
`DuplicateOldClientSequenced`, and `ReorderTwoClientSequenced`. Mutation
selection uses candidate-sequenced packet order only: it does not classify a
packet as a stock move and never rewrites payload bytes. Packet, byte,
datagram-size, readiness, and wall-time bounds apply to every run.

The harness performs no Windows input injection, so controlled movement,
button, mouse, and auxiliary-field scenarios remain pending rather than being
fabricated. Per-event output is structural metadata only: monotonic time,
endpoint roles/address/port, byte length and SHA-256, bounded framing/header
classification, relay disposition, and byte-preservation result. It contains
no datagram payload, config content, movement/view value, identity, or
authentication material.

Cleanup terminates only owned processes, disposes the relay sockets, restores
the protected root and `valve/` config/log/screenshot/save state, removes every
new research-root entry, restores directory metadata, and compares the whole
bounded root inventory by path/kind, SHA-256, size, creation/last-write time,
and attributes. A transport capture is accepted only after the before/after
manifest matches with `external-file-drift=none`; if restoration fails, its
temporary backup is retained for recovery. Even a restored transport capture
records `accepted_stock_evidence_run=false`, because independent move
classification and projection review have not happened. It therefore does not
increase the zero accepted-stock-run or verified-move-packet counters.

Guided metadata belongs only under ignored
`manual-artifacts/usercmd-captures/<run>/metadata.json`; no raw datagram file is
created. The tracked
`docs/evidence/GOLDSRC_USERCMD_STOCK.json` must remain absent until accepted,
restoration-attested runs can populate and independently validate a
metadata-only projection. Raw packets, input values, view angles, movement
values, config contents, identity and authentication material are not tracked.

## Exact accepted 15-field descriptor

The accepted M2.4.3 delta-description evidence includes an exact 15-field
descriptor named `usercmd_t`. This proves descriptor metadata only; it does not
prove that a stock client-move body uses the synthetic delta grammar described
in [GoldSrc usercmd delta](GOLDSRC_USERCMD_DELTA.md).

`GoldSrcUserCmdSchemaBindingEntry` is the single explicit table used to build
and validate the synthetic registry. Every entry has storage-size metadata 1.
Description offsets reproduce descriptor metadata and are never interpreted as
C or C++ object offsets. Wire multiplier values use the delta-description scale
of 4000. `synthetic` codec support does not enable stock encoding or decoding.

| Wire | Exact name | Base | Signed | Bits | Pre/post | Offset | Presence | Semantic target | Controlled stock scenario | Confidence | Encode/decode |
| ---: | --- | --- | :---: | ---: | --- | ---: | ---: | --- | --- | --- | --- |
| 0 | `lerp_msec` | short | no | 9 | 4000/4000 | 0 | `0x7b` | interpolation milliseconds | timing: interpolation | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 1 | `msec` | byte | no | 8 | 4000/4000 | 2 | `0x7f` | command duration | timing: command duration | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 2 | `viewangles[1]` | angle | no | 16 | 4000/4000 | 8 | `0x7f` | yaw | yaw positive/negative | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 3 | `viewangles[0]` | angle | no | 16 | 4000/4000 | 4 | `0x7f` | pitch | pitch positive/negative | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 4 | `buttons` | short | no | 16 | 4000/4000 | 30 | `0x7f` | button mask | controlled button mask | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 5 | `forwardmove` | float | yes | 12 | 4000/4000 | 16 | `0x7f` | forward move | forward/back movement | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 6 | `lightlevel` | byte | no | 8 | 4000/4000 | 28 | `0x7f` | light level | auxiliary light level | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 7 | `sidemove` | float | yes | 12 | 4000/4000 | 20 | `0x7f` | side move | left/right movement | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 8 | `upmove` | float | yes | 12 | 4000/4000 | 24 | `0x7f` | up move | controlled vertical movement | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 9 | `impulse` | byte | no | 8 | 4000/4000 | 32 | `0x7f` | impulse | auxiliary impulse | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 10 | `viewangles[2]` | angle | no | 16 | 4000/4000 | 12 | `0x7f` | roll | view-roll policy | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 11 | `impact_index` | integer | no | 6 | 4000/4000 | 36 | `0x7f` | impact index | auxiliary impact index | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 12 | `impact_position[0]` | float | yes | 16 | 32000/4000 | 40 | `0x7f` | impact X | auxiliary impact X | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 13 | `impact_position[1]` | float | yes | 16 | 32000/4000 | 44 | `0x7f` | impact Y | auxiliary impact Y | descriptor accepted; stock runtime pending | synthetic/synthetic |
| 14 | `impact_position[2]` | float | yes | 16 | 32000/4000 | 48 | `0x7f` | impact Z | auxiliary impact Z | descriptor accepted; stock runtime pending | synthetic/synthetic |

The evidence classification applies independently to every row: the exact
schema metadata comes from the accepted M2.4.3 delta-description capture, while
controlled stock input/move scenarios remain pending at zero accepted runs.
Field-name semantic targets are public-header cross-checks, not transmitted
stock-behavior proof. Synthetic encode/decode support is explicit for all 15
rows; stock runtime encode/decode support is disabled pending evidence.

`bind_goldsrc_usercmd_schema()` requires the exact case-sensitive name, field
count, order, names, base types, signed modifiers, storage sizes, significant
bits, multipliers, and presence masks. `make_synthetic_usercmd_schema_registry()`
is a tooling/fake-peer factory backed by the same table; it does not enable a
stock runtime profile.

## Public struct reconciliation

The pinned Valve `common/usercmd.h` is a public semantic cross-check. It lists
`lerp_msec`, `msec`, three view angles, three movement axes, `lightlevel`,
`buttons`, `impulse`, `weaponselect`, `impact_index`, and three impact-position
components. The accepted descriptor has 15 scalar fields and omits
`weaponselect`; it also orders yaw before pitch and interleaves movement and
other scalars differently from the public declaration.

`GoldSrcUserCmdState` retains a fixed-width `weapon_select` semantic field so
the mismatch is explicit. The current adapter rejects nonzero weapon selection
and the delta codec requires it to be zero. No public struct size, alignment,
offset, native cast, aliasing, or SDK object storage is used.

## Typed state and limits

`GoldSrcUserCmdState::create()` publishes an immutable owning value only after
validating a complete `GoldSrcUserCmdCreateInfo`. It stores:

- fixed-width semantic fields for durations, angles, movement, light level,
  buttons, impulse, weapon selection, and impact metadata;
- `GoldSrcUserCmdSequence`, a nonzero project-local `uint32` identity that is
  not claimed to be transmitted;
- compatibility, input-mapping, and schema-binding profiles;
- source input sequence plus caller-provided sample time and duration metadata.

| Project semantic | Owning type |
| --- | --- |
| `lerp_msec` | `uint16_t` |
| `msec`, `light_level`, `impulse`, `weapon_select` | `uint8_t` |
| `view_angles[3]`, movement axes, `impact_position[3]` | `float` scalars |
| `buttons` | `uint16_t` |
| `impact_index` | `int32_t` |
| command identity | validated nonzero `uint32_t` wrapper |
| source input identity | `uint64_t` |
| sample time / optional duration | `int64_t` / `uint64_t` nanoseconds |

All floating-point values must be finite. Default limits include 255 command
milliseconds, 511 interpolation milliseconds, movement magnitude 2047, 64
history entries, 16 commands per packet, 7 backup commands, 8 new commands,
8192 encoded bits, and 1024 encoded bytes. Hard limits cap history at 256,
commands per packet at 32, backup/new counts at 15 each, and encoded output at
65536 bits/8192 bytes. Invalid profiles, ranges, profile tuples, sequence
exhaustion, nonfinite values, impossible impact pairs, and unsupported fields
fail without partial publication or silent clamping.

The synthetic build context supplies `lerp_msec` and `light_level` explicitly;
both default to zero. Impact defaults are index zero and an all-zero position.
Validation requires index zero exactly when all three position components are
zero; a nonzero index is bounded to 1..63 and requires a nonzero bounded
position.

`GoldSrcUserCmdDurationQuantizer` converts finite nonnegative seconds to
rounded nanoseconds, carries a sub-millisecond remainder, and splits whole
milliseconds into bounded `uint8` segments. It has no wall-clock access.

## Synthetic input adapter

`GoldSrcUserCmdInputAdapter` is executable only for
`synthetic_explicit_v1`. It maps focused `GameplayInputIntent` and the absolute
camera state as follows:

- a positive forward axis uses `forward_speed`, a negative axis uses
  `backward_speed`, and the signed side axis uses `side_speed`; the three
  deterministic defaults are 400;
- diagnostic preview vertical movement never enters `up_move`;
- semantic angle order is pitch, yaw, roll, with roll zero;
- walk and scoreboard remain unmapped metadata, or an error in strict mode;
- focus loss produces zero movement/buttons/impulse;
- impulse consumption is represented by a move-only
  `GoldSrcUserCmdOneShotPlan` and commits only after the matching command is
  inserted into history.

The exact synthetic held-button fixture is:

| Gameplay action | Synthetic bit | Mask |
| --- | ---: | ---: |
| primary attack | 0 | `0x0001` |
| jump | 1 | `0x0002` |
| duck | 2 | `0x0004` |
| use | 5 | `0x0020` |
| secondary attack | 11 | `0x0800` |
| speed | 12 | `0x1000` |
| reload | 13 | `0x2000` |

Those bit values and speed defaults are deterministic project fixtures, not
claims about stock input behavior. The adapter does not emit command strings,
poll SDL, access a renderer, or send a network packet.

## Scheduling and milestone boundary

`GoldSrcUserCmdScheduler` is caller-driven and fixed-step only under
`synthetic_fixed_step_v1`. It accepts caller-supplied monotonic nanoseconds,
emits zero, one, or a bounded number of sample requests, carries fractional
millisecond duration, and rejects time reversal, lag beyond its catch-up bound,
time overflow, and sequence exhaustion. One rendered frame is not assumed to
equal one command.

M4.6.2 itself contains no collision, gravity, movement simulation, command
prediction, server-time mapping, replay/reconciliation, or entity projection.
M4.6.3.1 subsequently added an independent immutable BSP collision/query API
without changing this usercmd wire/session boundary.

M4.6.3.2 applies only `synthetic_usercmd_v1` to local movement. The kernel uses
`msec`, view angles, forward move, side move, buttons and contiguous command
sequence. Duration is `msec * 0.001`; pitch is retained for the camera while
movement direction uses yaw only. `up_move`, light level, impulse, weapon and
impact fields are not executed. Named synthetic jump/duck bits drive press-edge
jump and immediate hull selection. The synthetic run bit is inert in the
kernel; command movement values determine wish speed.

`LocalPlayerMovementController` uses the scheduler and adapter without placing
local commands in network history or a transport. Pending one-shot edges are
consumed only after the first successfully simulated command is inserted in
prediction history and its prepared update commits. M4.6.3.3 keeps a separate
immutable local prediction history and replays those already-built commands;
it does not touch transmission history, packet planning, delta bytes,
checksum, sequence rules, or the retained driver. Replay never resends a
command and never consumes an input edge twice. Stock command/acknowledgement
semantics remain M4.7 evidence work.
