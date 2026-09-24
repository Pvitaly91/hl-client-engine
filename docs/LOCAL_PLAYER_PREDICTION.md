# Local player prediction

M4.6.3.3 adds a deterministic local prediction boundary for the executable
`synthetic_authoritative_reconciliation_v1` profile. It is an offline,
project-owned authority model for tests and diagnostic tools. It is not a claim
that a stock Protocol 48 server transmits the normalized state or command
acknowledgement used here.

The stock profiles remain fail-closed:

- `stock_protocol_48_authoritative_reconciliation_evidence_pending`;
- `stock_usercmd_acknowledgement_evidence_pending`;
- `stock_ack_and_player_state_semantics_pending`.

In particular, the local-player entity projection, acknowledgement width and
wrap rules, server time scale, and live prediction lifecycle remain pending.

## Data flow

The synthetic route is:

```text
InputSnapshot -> GameplayInputIntent -> GoldSrcUserCmdState
    -> prepared local movement commands and pre/post states
    -> LocalPredictionHistoryState
    -> typed AuthoritativePlayerState acknowledging command N
    -> rewind to authoritative state after N
    -> replay retained commands N+1 ... latest
    -> corrected simulation state immediately
    -> camera-only visual correction -> RenderScene
```

`hlclient_prediction_api` owns renderer- and platform-neutral prediction values.
`hlclient_local_prediction` composes those values with the GoldSrc local
movement kernel and collision API. `hlclient_prediction_visual` consumes only
the corrected camera, a visual-correction state, explicit sample time, and a
collision query. Prediction code does not poll SDL, open files, access a wall
clock, issue OpenGL calls, or send packets.

## Session identity

`create_prediction_session_identity` derives a validated
`PredictionSessionIdentity` from explicit scalar generations, the collision
session identity, immutable movement environment and configuration, and the
initial movement state. The retained identity covers:

- session and prediction generations;
- collision-world primary/secondary identity, revision, scene signature and
  movement-collision profile;
- movement-environment and movement-configuration signatures;
- spawn/initial-state signature;
- movement command, prediction and acknowledgement profiles.

It retains no native path, socket, credential, authentication material, raw
packet, or native handle. Authority from another map, collision revision or
collision profile,
MoveVars environment, movement configuration, command profile, spawn, session,
or prediction generation must not be replayed.

## Atomic local publication

`LocalPlayerMovementController::prepare_update` stages scheduler state,
one-shot input state, exact immutable commands, per-command pre/post movement
states, the final state and camera, statistics, and bounded touch summaries.
Preparation does not mutate the controller. A prediction composition
controller must preflight every history limit before it commits the prepared
movement update and publishes the replacement history.

The logical transaction is all-or-nothing:

1. prepare local movement;
2. construct the candidate prediction entries;
3. preflight history insertion and controller revision;
4. commit the movement plan and matching history together;
5. consume pending one-shot input only once.

A stale plan, allocation failure, history backpressure, movement error, or
publication failure leaves the previous movement state, scheduler, pending
one-shots, camera, and prediction history authoritative. Existing
`LocalPlayerMovementController::update` remains the non-prediction compatibility
route.

An active camera residual reserves its next presentation revision before the
prepared movement plan or candidate history can commit. Active camera sampling
also requires a raw world-only collision identity to match all session fields,
including its world-only profile and scene signature; non-world-only sessions
cannot substitute a raw BSP query for their composed movement-collision scene.

## Authority and correction

An accepted normal or teleport update names one exact retained command and
provides a complete normalized state after that command. Reconciliation
validates its session and collision position, compares it with the exact
predicted post-state, replaces the replay base with the authoritative state,
and replays every unacknowledged retained command in sequence. A hard reset
requires a lexicographically newer `(session generation, prediction
generation)` pair and clears the old history.

The corrected physical state becomes current immediately. Collision, future
movement, history and subsequent authority comparisons never use a smoothed
state. Only the displayed first-person camera position may carry a bounded,
time-based residual offset. Large corrections, teleports, hull/mode changes
and hard resets snap.

Each controller operation also returns a bounded, allocation-free, ordered
batch of metadata-only lifecycle events. The batch has room for every command
in the hard replay limit plus all surrounding authority, acknowledgement,
measurement, history, visual-correction, reset, backpressure, and failure
events. Per-command replay entries describe retained-command processing only;
they are not gameplay effects and do not redispatch touches, sounds, footsteps,
input edges, or packets. The legacy single `event` field remains the
operation's principal/terminal event for existing callers.

`LocalPredictionStatistics` counts live prediction and replay separately.
Hard reset authority carries the explicit no-command acknowledgement and does
not increment `accepted_acknowledgements`; it increments `hard_resets` instead.

## Side effects and network isolation

Replay executes immutable `GoldSrcUserCmdState` objects already retained by
prediction history. It does not rebuild commands from input, consume keyboard
or mouse edges again, re-run gameplay effects, reinsert commands into
transmission history, or resend packets. The existing usercmd delta codec,
checksum, packet planner, Netchan driver and transmitted bytes are unchanged.
There is no synthetic production network opcode.

Prediction history is separate from `GoldSrcUserCmdHistoryState`: transmission
history tracks sent/new/backup metadata, while prediction history tracks
commands and movement-state lifecycle. They may share the same immutable
command and exact sequence without sharing retention policy.

## Required regressions

Prediction preserves the accepted M4.6.3.2.1 wall-contact boundary. Direct and
glancing wall contact, parallel sliding, corners, zero-progress contact,
duplicate planes, jump-wall, duck-wall, bounded diagnostics, the movement
failure latch, and the 10,000-command wall campaign remain mandatory. A wall
replay must not publish a blocking origin, corrupt history, or turn ordinary
contact into a fatal movement error.

See [prediction history](PREDICTION_HISTORY.md),
[authoritative player state](AUTHORITATIVE_PLAYER_STATE.md),
[command replay](COMMAND_REPLAY.md),
[reconciliation](PREDICTION_RECONCILIATION.md), and
[visual correction](PREDICTION_VISUAL_CORRECTION.md).

## M4.7.1 stock evidence boundary

The stock runtime layer may publish an unresolved identity, domain-tagged ACK
candidates and a partial movement observation. It cannot manufacture a
`PredictionSessionIdentity`, `AuthoritativeCommandAcknowledgement` or complete
`AuthoritativePlayerState`. Consequently the controller remains strictly
synthetic and no stock frame triggers replay or camera correction. See
[stock authoritative projection](GOLDSRC_AUTHORITATIVE_STATE_PROJECTION.md).

## H2 reference carrier and seed adapter (partial)

The live usercmd stage now retains a bounded, byte-free receipt ledger after
actual `move_packet_submitted` events. Each receipt binds a 30-bit outgoing
packet sequence to its ordered, immutable new and backup command identities and
wire values. Prepared packets and `would_block` do not enter the ledger. A
fresh clientdata record can bind only to an exact retained move carrier in the
same generation, using the ACK and source sequence copied from that record's
own payload metadata. The resulting boundary is the last **new** command in
that carrier; backup commands keep their earlier identities. An ACK for an
unknown or non-move carrier does not choose the closest command. Old reliable
or reassembled bodies are excluded because their completion header may not
date the body. Stale/duplicate records and ambiguous modular sequence order
are rejected. This is a reference-derived carrier/history boundary, not a
separate wire field or direct observation of internal HLDS execution.

The basis is pinned [ReHLDS `sv_user.cpp` and `sv_main.cpp`](https://github.com/rehlds/ReHLDS/tree/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine), plus the [Xash3D GoldSrc-compatible prediction receive path](https://github.com/FWGS/xash3d-fwgs/blob/7500a6b3647e71d9b21691671957a0e06731019e/engine/client/dll_int/cl_pmove.c). ReHLDS parses backup/new commands in a received move and later constructs a clientdata response; Xash uses the incoming carrier ACK to index its command/frame history. Packet sequence and command ordinal remain distinct domains.

The H2 observation projection retains optional clientdata `flags`, `maxspeed`,
`flDuckTime`, `bInDuck`, `waterlevel`, and `deadflag`, and optional
`entity_state_player_t` `movetype`, `usehull`, `gravity`, `friction`,
`basevelocity`, and `spectator`. Types come from pinned [Valve `network/delta.lst`](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/network/delta.lst); missing descriptors stay unavailable. These fields are outside the stable canonical replay hash. A strict seed candidate requires fresh, same-record clientdata and the player entity mapped from the zero-based ServerInfo client slot to entity `slot + 1`, complete movement fields, and a supported dry walk context. Its origin/velocity/view offset/flags are server-observed; the player fields are observed from the matching entity snapshot. Old buttons require an exact retained prediction slot, or a neutral command at the bound history boundary. This follows pinned [Valve `HUD_TxferPredictionData`](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/entity.cpp), which retains client-only prediction fields from the matching prior prediction state.

The seed is deliberately not a complete movement state: ground/contents and
collision scene identity still require collision queries. An explicit
`reference_wire_dry_walk_v1` movement profile adapts immutable quantized wire
commands and executes ordinary action-free commands of at most 50 ms as one
collision-aware kernel step, following the pinned Xash split threshold. It
does not execute jump/duck transitions or establish full stock PM_Move
equivalence. The reconciliation authority remains synthetic-only. The live
stage publishes passive readiness counters but keeps the existing
server-sample camera. There is no `--prediction reference` option or active
live predictor at this partial boundary.

## H2 continuation: executable reference dry-walk path

The later H2 continuation adds an explicit `--prediction reference` option for
`live-visual-control`; the default remains `off`. The managed wrapper passes
`-ProjectClientPrediction reference` through the native orchestrator into that
same client process. The current map's already imported collision package is
attached to the live owner after the first rendered scene is installed. The
sign-on MoveVars construct the movement environment. No second BSP parse,
socket, scheduler, or input sampler is used.

Ground is derived read-only at the observed origin with the active hull. The
world-only collision query checks start position and contents, probes down two
units, and requires a walkable plane and agreement with the received
`FL_ONGROUND` flag. The contact hit, normal, fraction, hull, and collision
identity are retained separately from server-observed origin and velocity.
Solid starts, unsupported contents and disagreement suspend prediction without
moving the observed player. A pair of omitted lateral `view_ofs` fields is
handled by the existing vertical-only camera policy, with its own provenance;
an isolated missing component remains unavailable.

The reference prediction history starts at a valid current-session carrier
boundary, which may have a nonzero command identity. Each later locally
committed immutable wire command is adapted once and simulated by the existing
collision-aware kernel. A fresh coherent server correction is compared to the
predicted post-state at exactly the bound command. The replacement history is
built from that correction and the exact retained suffix, then published only
after all replay succeeds. A newer server record with the same command
boundary may rebase again; an identical source record is ignored. This carrier
boundary is reference-derived, not a direct HLDS execution ACK.

Presentation may interpolate two adjacent local predicted states with one
command interval of lag when a world hull trace leaves the segment clear.
Blocked interpolation chooses the latest validated endpoint. It never commits
simulation, changes the canonical receiving-client state or alters a usercmd.
Prediction suspends for jump/duck commands, invalid ground/authority, missing
history, collision failure, or a raw correction above the stated 16-unit
presentation scope. The server-sample camera resumes, and a later valid anchor
may reactivate prediction. Dry walk/air on world collision is the supported
slice; dynamic support, ladders, liquids and full PM_Move equivalence are not
claimed. Live verification and manual feel remain separate results.
