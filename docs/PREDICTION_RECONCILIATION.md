# Prediction reconciliation

`LocalPlayerPredictionReconciler` is the pure M4.6.3.3 boundary that combines
one immutable prediction history with one normalized authoritative update. It
supports only `synthetic_authoritative_reconciliation_v1`. Stock Protocol 48
player-state projection and usercmd acknowledgement mapping remain
evidence-pending and fail before replay.

## API

`LocalPlayerPredictionReconciler::reconcile` accepts the immutable history and
authority, immutable movement environment, explicit collision provider,
caller-owned movement scratch, movement configuration, and
`PredictionReconciliationConfig`.

The configuration contains `PredictionStateComparisonConfig`, explicit
`PredictionReconciliationLimits`, and the stale-update policy. The result owns:

- a replacement immutable history;
- the corrected current movement state;
- metrics at the acknowledged command and current-state correction;
- `PredictionCorrectionClass`;
- bounded replay statistics;
- stale/duplicate/history-change flags or one typed error.

No result is a network message or renderer object.

## Validation and algorithm

A normal or teleport reconciliation follows this order:

1. validate comparison, replay and movement configuration;
2. require the exact prediction session, collision profile and active
   collision-world identity;
3. verify MoveVars environment and movement-configuration signatures;
4. test the authoritative active hull at its supplied origin, reject a
   blocking or unsupported-liquid state without nudging it, and require the
   collision contents category to match the normalized state;
5. classify stale, duplicate or conflicting authority identity;
6. require one exact acknowledgement and locate its exact retained entry;
7. compare that predicted post-state with the authoritative state;
8. replace the replay base and anchor with authority after command `N`;
9. replay exact retained commands `N + 1 ... latest`, or use the validated
   exact fast path;
10. compare the former current prediction with the corrected current state;
11. trim acknowledged entries and atomically publish the replacement history.

The corrected current movement state is authoritative immediately after
success. Future collision and commands use it even when the camera is still
displaying a residual visual offset.

## State comparison and classes

`compare_prediction_states` reports finite position/velocity deltas and
magnitudes, horizontal/vertical position error, shortest-path angle deltas,
hull/mode/ground/contents/old-buttons/simulation-time mismatches, physical
equality and signature equality.

Comparison tolerances choose diagnostics and visual policy; they never suppress
authoritative correction. The supported correction classes are:

- `exact`;
- `replay_without_visual_offset`;
- `small_visual_correction`;
- `large_snap`;
- `teleport_snap`;
- `hard_reset`.

The project defaults treat a position correction up to 4 source units as
small, use 16 source units as the large snap threshold, and allow no visual
offset below 0.001 source units. Hull, movement-mode, ground or contents
mismatch snaps regardless of small positional error. Those thresholds are
project-owned synthetic policy, not stock GoldSrc behavior.

Normal updates beyond the configured authoritative position/velocity safety
bounds fail as invalid rather than being treated as a teleport. Teleports are
explicitly typed.

## Authority order and acknowledgement failures

Authority update ordinal is nonzero, monotonic and non-wrapping per session.
With the default stale policy, an older ordinal or an acknowledgement older
than the accepted anchor is an idempotent ignored result that preserves current
state. An identical accepted acknowledgement and state
signature is an ignored duplicate. The same ordinal or acknowledgement with a
different signature is a conflict.

An acknowledgement newer than the newest prediction is
`future_acknowledgement`. An exact sequence absent between retained anchor and
latest is `acknowledgement_missing`. `acknowledgement_evicted` remains a
defensive error for a future explicitly adopted history with a pre-session
retention floor. Current public history creation never evicts an
unacknowledged command: after an authority anchor is accepted, any smaller
acknowledgement follows the stale rule above. Reconciliation never applies
authority to a nearest entry and never ignores a command gap.

The same hard-reset session and update ordinal is governed by the same rule:
an exact identity/state duplicate is idempotent, while a different state is
`conflicting_authoritative_update`. Ordinals are not compared across a
strictly newer reset session because they are scoped to that session.

`AuthoritativeCommandAcknowledgement::none()` is not accepted for ordinary
normal/teleport reconciliation. It is reserved for initial/reset anchoring.

## Hard reset

`respawn_or_hard_reset` requires a command-zero authoritative state and a
strictly newer lexicographic `(session generation, prediction generation)`
pair. Compatible collision/environment/configuration and
command profiles are still required. Success clears all old entries, installs
the new authoritative anchor and returns `hard_reset`. No command crosses the
generation boundary, and any active visual correction is discarded by the
owning controller.

## Transactional failure

Session mismatch, blocking or unsupported-liquid authority, collision-contents
mismatch, missing/future/evicted acknowledgement, command gap, replay failure,
non-finite result, allocation failure or limit exhaustion publishes no partial
replacement. The caller retains its previous history and corrected state. A
viewer may keep rendering the last valid camera, but it must surface the typed
failure and eventually return nonzero.

The reconciler performs no input polling, effect execution, file access,
packet transmission or rendering. See [command replay](COMMAND_REPLAY.md) and
[visual correction](PREDICTION_VISUAL_CORRECTION.md).

M4.7.1 does not widen this reconciler. A partial stock observation, unresolved
identity, netchan ACK candidate or runtime-frame reference is rejected before
this API. Exact stock usercmd acknowledgement remains pending for M4.7.2, so
the only executable reconciliation profile remains the synthetic authority
profile documented above.
