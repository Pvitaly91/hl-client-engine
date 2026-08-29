# Authoritative player state

`AuthoritativePlayerState` is a normalized, immutable simulation-state boundary
for M4.6.3.3. Its only executable construction path is
`from_synthetic_complete_state`. It does not claim that a stock Protocol 48
server transmits every retained `LocalPlayerMovementState` field.

The executable profiles are:

- prediction: `synthetic_authoritative_reconciliation_v1`;
- evidence: `project_typed_authoritative_states_and_independent_fixtures`;
- acknowledgement: `synthetic_uint32_non_wrapping_v1`.

Stock acknowledgement mapping and stock local-player entity projection remain
`stock_usercmd_acknowledgement_evidence_pending` and
`stock_ack_and_player_state_semantics_pending`. Netchan acknowledgements,
entity snapshot references and entity field names are not substitutes.

## Typed acknowledgement

`AuthoritativeCommandAcknowledgement` has only two construction forms:

- `none()` for an initial or hard-reset command-zero anchor;
- `for_sequence(GoldSrcUserCmdSequence)` for one exact predicted command.

There is no unchecked raw-integer authority API. Synthetic sequence zero is
represented by `none`, sequences strictly increase, and wrap is unsupported.
Normal and teleport updates require an exact acknowledgement. A
`respawn_or_hard_reset` update requires `none` and a new prediction generation.

## Update identity

`AuthoritativePlayerUpdateIdentity::create` validates:

- a complete `PredictionSessionIdentity`;
- a nonzero authority update ordinal;
- non-negative synthetic authority time in nanoseconds;
- acknowledgement/discontinuity consistency.

Update ordinals are non-wrapping and must advance monotonically for one
session. Reconciliation classifies older updates as stale, exact duplicates as
idempotent, and a repeated ordinal or acknowledgement with a different state
signature as `conflicting_authoritative_update`.

`AuthoritativePlayerDiscontinuity` is explicit:

- `normal` replays unacknowledged commands and may permit a small visual
  correction;
- `teleport` still replays later commands but snaps the visual camera;
- `respawn_or_hard_reset` requires a new prediction generation, clears old
  history and forbids cross-generation replay.

These discontinuity values are synthetic tool/test semantics, not stock wire
values.

## Complete normalized state

`from_synthetic_complete_state` requires the complete movement state's source
command sequence to match the acknowledgement, or to be zero for a no-command
hard-reset anchor. It also validates revision, synthetic command profile, the
declared dry-walk movement/evidence profiles, finite origin, velocity, angles
and view offset, and rejects unsupported liquid, ladder or invalid/stuck modes.

The resulting object owns the update identity, complete movement state,
deterministic state signature, compatibility profile and evidence profile.
The signature is project metadata, not a server-provided checksum.

Structural construction is followed by active collision validation during
reconciliation. The authoritative active hull is tested through the current
session's collision provider. The returned contents must be a supported dry
category and must match `last_valid_contents()` in the normalized state. Query
failure, unsupported liquid, contents mismatch, or a blocking position returns
`invalid_authoritative_state` or `authoritative_state_blocking`. Authority is
never nudged out of solid automatically.

## Session validation

Before replay, authority must match the active collision-world identity and
revision, collision scene signature, MoveVars environment signature, movement
configuration, command profile, spawn/session generation and prediction
generation. A mismatch returns a typed session, collision, environment or
configuration error. Map changes never replay the previous map's commands.

## Sources and evidence boundary

`IAuthoritativePlayerStateSource::poll_next` is caller-driven and returns a
typed `AuthoritativePlayerStatePollResult`. Synthetic tools may feed complete
states through an in-memory source or simulator. The production
`StockAuthoritativePlayerStateSourceEvidencePending` publishes no fabricated
state and returns `stock_evidence_pending`.

No adapter may infer authority by:

- treating a Netchan ACK as a usercmd acknowledgement;
- treating an entity snapshot reference as an acknowledgement;
- selecting the local player by entity number;
- looking up `origin`, velocity, hull, ground or view-offset fields by name.

A future stock adapter requires separate evidence for the exact player-state
message, local-player projection and acknowledgement lifecycle. This boundary
adds no packet, opcode, socket, file, or production fake-server route.

See [local player prediction](LOCAL_PLAYER_PREDICTION.md) and
[prediction reconciliation](PREDICTION_RECONCILIATION.md).
