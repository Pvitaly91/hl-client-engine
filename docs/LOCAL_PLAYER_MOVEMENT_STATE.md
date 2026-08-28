# Local player movement state

`LocalPlayerMovementState` is the immutable, renderer- and network-neutral
publication boundary for deterministic local movement. It lives in
`hlclient_movement_api`; construction is available only through the validating
`create` factory. Copy and move construction are allowed, while assignment is
deleted so an already published state cannot be partially rewritten.

## Compatibility profiles

The only executable movement profile is
`public_valve_pm_shared_dry_walk_subset_v1`, with evidence profile
`public_valve_pm_shared_and_independent_fixtures`. This names a bounded dry-walk
subset informed by the pinned public Valve movement source; it is not a claim
that the full original `PM_Move` was reproduced.

The executable command profile is `synthetic_usercmd_semantics_v1`. The typed
`stock_pm_move_full_compatibility_evidence_pending` and
`stock_usercmd_semantics_evidence_pending` profiles fail closed.

## Published state

The state owns:

- origin, velocity and pitch/yaw/roll view angles;
- standing or ducked player hull;
- `walking`, `airborne`, or a typed unsupported/invalid movement mode;
- immutable `PlayerGroundState`;
- standing or ducked view offset;
- previous synthetic button mask for press-edge detection;
- source command sequence, simulation time and state revision;
- last valid normalized contents category;
- entity-gravity and friction multipliers;
- movement, evidence and command profiles.

It contains no renderer handle, collision pointer, socket, network command,
native entity pointer, file path or source byte view.

## Validation

`LocalPlayerMovementState::create` rejects invalid limits, non-finite vectors,
coordinates/velocities/angles outside their configured magnitude limits,
unknown hull or mode values, invalid ground invariants, non-positive gravity or
negative friction multipliers, revision zero/exhaustion, and
pending/unsupported profiles. Values
are never repaired by converting NaN or infinity to a fallback.

A grounded state must also be walkable, have a hit identity, a normalized
finite plane, a finite contact point, and a probe fraction in `[0, 1]`.
Airborne state has no ground hit. These constraints make the state safe to
copy between the kernel, controller and camera without exposing collision
storage.

## Ground, touches and statistics

`PlayerGroundState` retains the world or explicit synthetic-brush identity,
collision plane, contact position, probe fraction and
`deterministic_collision_trace_v1` evidence profile. Hit identities are stable
values: source model index plus optional instance/entity ordinals.

`PlayerMovementTouch` is inert metadata emitted by a successful command. It
records hit identity, plane, fraction, movement phase and source command
sequence. No touch or trigger callback is invoked.

`PlayerMovementStatistics` reports bounded command/substep, ground-probe,
trace, hit, slide-plane, step, jump, duck/stand, solid-start and distance
counters. Grounded and airborne counters currently count simulated substeps,
not rendered frames.

## Transaction and signature

`GoldSrcLocalMovementKernel::simulate` receives the previous state by const
reference. Success publishes one complete state, touch list, statistics and a
deterministic state signature. Any validation, collision, unsupported-contents
or arithmetic error publishes no successor state; the caller retains the
unchanged previous state.

`local_player_movement_state_signature` hashes the typed state metadata in a
stable order. It is a deterministic project summary, not a server checksum or
prediction acknowledgement.

See [GoldSrc local movement](GOLDSRC_LOCAL_MOVEMENT.md) for the simulation
order and [player-walk viewer](PLAYER_WALK_VIEWER.md) for the local controller.
