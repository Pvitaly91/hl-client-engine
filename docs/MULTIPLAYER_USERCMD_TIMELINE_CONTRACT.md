# Multiplayer command / transport timeline contract

This audit concerns the normal `hlclient` producer path. No live server was
started or probed for this task. A transport acknowledgement does not establish
server command execution, a weapon action, a hit, a miss, or damage.

## Pinned public-source model

The public [Valve usercmd declaration](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/common/usercmd.h)
distinguishes command duration (`msec`) from interpolation duration (`lerp_msec`).
The [Valve delta descriptor](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/network/delta.lst)
specifies the selected wire field order/widths. The project preserves quantized
angle turns and signed whole-unit movement; the normal reference wire profile
does not contain a weapon-selection field.

The pinned [ReHLDS command consumer](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_user.cpp)
is a **source model**, not evidence that a connected server executes that build.
`SV_ParseMove` reads backup/new commands, verifies framing and checksum, and
executes commands separately from channel receipt. Paused/frozen state, command
limiters, dropped-command recovery, and game-DLL callbacks can affect execution.
`SV_RunCmd` can subdivide durations above 50 ms. `SV_SetupMove` requires the game
lag-compensation callback, `sv_unlag`, active multiplayer state and client `lw`
and `lc`. It uses `lastcmd.lerp_msec`, caps that interpolation term at 100 ms,
then floors it by the next message interval; latency and `sv_maxunlag` are
separate terms. Dead/non-interpolated/teleporting targets can be excluded.
`lastcmd` is updated outside that rewind lookup, so a client carrier ID cannot
identify the exact rewind used for a particular server action.

The [Valve weapon event path](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/crossbow.cpp)
uses `FEV_NOTHOST` in client-weapons builds. Consequently absence of an echoed
local fire event is not evidence of a rejected shot.

## Audited local ownership boundaries

| Boundary | Actual owner | Observation, not inference |
| --- | --- | --- |
| Normalized input | application → `LiveVisualControlInput` | Accepted input revision and actual publication time; revisions rejected as stale are not another input |
| Scheduled command | `LiveRuntimeStage` / scheduler | Due deadline in the explicit owner clock, independent actual callback timestamp |
| Final command | `build_reference_wire` result | Post-normalization pitch/yaw/roll turns, buttons including releases/neutral ticks, movement, msec, lerp, impulse |
| Immutable history | successful `queue_reference_command` | Exact command ID and insertion bracket; unsuccessful insert has no accepted-history event |
| Move plan / encoding | `GoldSrcUserCmdTransmissionStage` | Exact ordered command IDs including backups and codec-owned quantized command results; no diagnostic second parser |
| Socket send | `NetchanDriver::send_datagram` | Successful transport call, actual begin/end, carrier sequence; would-block is not sent |
| Command carrier | matching driver context receipt | Exact plan bound to the successful carrier; retries do not duplicate a first-send receipt |
| Receive | existing driver parser | Read-call brackets, validated header, duplicate/reordered rejection, separate advanced transport ACK |
| Runtime record | existing `RuntimeReplaySession::apply_record` | Begin/end, then atomic committed record or rollback; rejected records do not publish gameplay state |

`producer_event_id` is local to its producer and generation. It is never a
command number, carrier sequence, server sequence, or render-frame identity.
`input_revision` is the application's identity; the temporary input tracker
created by the wire adapter is not used as that identity. Command IDs continue
through release/neutral ticks. Backups are explicitly marked by packet membership,
not treated as new weapon actions. The diagnostic sink is borrowed, optional,
numeric-only, and cannot change history/send/commit outcomes when it refuses an
event. Recorder overflow/sampling remains explicit in the enclosing report.

## Interpolation metadata

The render policy requests 100 ms for Half-Life players. The existing bounded
presentation clock initially fills a smaller reserve and increases it towards
that request. Its `delay_` is the effective buffer reserve. It is **not** the
distance from the latest source timestamp to a held actor sample, nor a measured
network latency. The presentation owner exposes that reserve to the next accepted
input snapshot. This is a previous-frame policy observation, not a claim that an
exact future frame or server rewind is already known.

Audit identified that the normal live reference builder did not assign its
`GoldSrcUserCmdBuildContext::lerp_msec`; its default zero survived into immutable
history and the wire although remote presentation selected a nonzero reserve.
The regression `[lerp-contract]` supplies a literal 75 ms effective reserve,
checks actual captured command bytes and final/history diagnostics, and covers
an immediately following release. It deliberately distinguishes the configured
100 ms maximum from the effective 75 ms value. No weapon-specific value or
server/IP-specific profile is involved.

After the pre-fix fixture established zero on the actual wire, the normal builder
now copies the accepted presentation reserve into each new immutable command as
`floor(seconds * 1000)` milliseconds. The input boundary rejects non-finite,
negative, out-of-policy and out-of-wire-range values. An absent reserve stays
zero with `effective_interpolation_seconds` absent, not a fabricated configured
reserve. Backups retain their original value. This path is identical with the
diagnostic sink enabled, disabled or refusing events; the fixture covers those
controls and explicit zero separately from unavailable metadata.

Normal `ClientConnectionSettings` emits `cl_lc=1` and `cl_lw=1` through
`build_stock_user_info`; the normal launcher changes name/model, not those
fields. They are advertised requests, not confirmed server execution settings.
This audit does not change them, `cl_updaterate`, command pacing, prediction,
weapon timing, spread, or the server.

## Offline evidence and limits

- `[lerp-contract]`: normal owning handshake/runtime fixture, final wire values,
  accepted input revision, neutral release, exact carrier membership, malformed
  record rollback with transport ACK kept separate.
- `[multiplayer-diagnostics]` transmission fixture: rejected stale history
  generation, would-block retries, recorder refusal, exact backup/new IDs,
  duplicate receive and advanced ACK without invented execution.
- Existing independent reference-move literal fixtures retain their checksum,
  COM_Munge and delta-bit coverage; diagnostic code does not decode wire bytes.

Build and test pass/fail results belong to the task receipt, not this contract.
No live verdict is inferred from synthetic loopback fixtures. Manual game test:
**not_run**.
