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
