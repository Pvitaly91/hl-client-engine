# Deterministic prediction command replay

M4.6.3.3 replay starts from one accepted `AuthoritativePlayerState` and applies
only the exact immutable commands retained after its acknowledgement. If
authority acknowledges command `N`, the replay range is exactly:

```text
N + 1, N + 2, ... latest retained command
```

There is no nearest-command fallback, command synthesis, input resampling, or
replay from the former latest predicted state.

## Replay inputs

`LocalPlayerPredictionReconciler::reconcile` receives:

- an immutable `LocalPredictionHistoryState`;
- an immutable normalized `AuthoritativePlayerState`;
- an immutable `GoldSrcMovementEnvironment`;
- an explicit `ILocalMovementCollision` provider;
- caller-owned `GoldSrcLocalMovementScratch`;
- explicit movement, comparison and reconciliation configuration.

The reconciler reads no input device, wall clock, renderer, filesystem or
network state. The movement kernel remains unaware of prediction history.

## Operation

After exact acknowledgement lookup and validation, replay:

1. makes the authoritative state after command `N` the new anchor and replay
   base;
2. verifies that the first retained sequence is exactly `N + 1`;
3. verifies every later sequence is contiguous and non-wrapping;
4. calls `GoldSrcLocalMovementKernel::simulate` for each retained command in
   ascending order;
5. stages replacement pre/post states, statistics and bounded touch summaries;
6. publishes the replacement history and corrected current state only after
   every command and limit succeeds.

The replacement entry retains the same immutable command object, exact command
sequence, prediction generation and entry ordinal. Its pre/post movement state
and simulation metadata are rebuilt from the authoritative base. Commands are
never silently skipped.

When the acknowledged predicted state and authoritative state match exactly in
both physical comparison and deterministic state signature, the exact fast
path may retain the already validated post-state chain without re-simulating
it. It still verifies pre/post signature continuity and exact command order.

## One-shot and side-effect policy

Replay operates on commands, not `InputSnapshot` or `GameplayInputIntent`.
Therefore it does not consume a jump, duck, impulse, click or other input edge
again. The command's retained `old_buttons` continuity is part of movement
state, so held jump cannot create a second press edge merely because replay
occurred.

Movement touches are inert bounded metadata. Replay does not invoke triggers,
sound, particles, weapon actions, user messages or other gameplay effects. If
future effects are introduced, only newly committed local commands may emit
them; replay must remain suppression-only.

Replay also does not:

- insert commands into `GoldSrcUserCmdHistoryState`;
- call the packet planner or Netchan driver;
- regenerate a usercmd delta/checksum/envelope;
- send or resend any packet;
- change transmitted command bytes.

## Bounds and errors

Replay depth is limited by both `LocalPredictionHistoryLimits` and
`PredictionReconciliationLimits`. The reconciliation limits additionally bound
substeps, traces, touches, retained replacement bytes, revisions and authority
work. Sequence wrap, a missing command, or invalid pre-state continuity returns
`prediction_command_gap`. Excess work returns
`prediction_replay_limit_exceeded`; a movement failure returns
`prediction_replay_failed`.

There is no wall-time watchdog in the pure core. Work is bounded by explicit
deterministic counts and caller configuration.

## Transactionality

The input history, anchor and current predicted state are immutable. Replay
allocates and simulates into temporary replacement storage. Any command gap,
collision failure, non-finite result, allocation failure, counter exhaustion or
limit failure discards that replacement and leaves the caller's prior history
and simulation state unchanged. A controller must replace its movement state,
history, camera and visual correction only after a complete reconciliation
result succeeds.

Standing/ducked hull, ground state, movement mode, contents, view offset,
simulation time and old-button state come from the authoritative base and exact
command replay; they are not copied selectively from the old current state.

Wall replay retains the M4.6.3.2.1 guarantees: zero-progress contact and
duplicate planes remain bounded, tangential sliding remains available, and no
replayed command may publish a start-solid/all-solid successor.

See [prediction history](PREDICTION_HISTORY.md) and
[prediction reconciliation](PREDICTION_RECONCILIATION.md).
