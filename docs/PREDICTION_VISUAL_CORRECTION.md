# Prediction visual correction

Prediction reconciliation corrects simulation immediately. Visual correction
is a separate camera-only presentation layer that may temporarily preserve a
small portion of the former displayed eye position. It never changes player
origin, velocity, hull, ground state, collision, history, authority or replay.

The executable profile is
`project_linear_decay_collision_constrained_v1`. Large corrections and explicit
snap cases use `no_smoothing_snap_v1`. The
`stock_visual_correction_evidence_pending` profile is not executable; the
project formula is not a stock GoldSrc smoothing claim.

## State and configuration

`PredictionVisualCorrectionState` retains:

- correction class;
- initial and current residual position offsets;
- correction start monotonic time and duration;
- source authority update ordinal;
- old/new prediction revisions;
- the exact last published `GameplayCameraState` and its publication revision;
- collision-constrained flag;
- visual-correction profile.

`PredictionVisualCorrectionConfig` defaults to a 0.100-second duration and a
16-source-unit maximum offset. Duration must be finite and within the validated
0..1 second range. `inactive()` publishes a zero-offset snap state.

`begin_prediction_visual_correction` receives the previous correction, former
physical eye, corrected eye, correction class, explicit caller time, authority
ordinal and prediction revisions. It performs no movement or collision query.

## Linear decay

For a small correction:

```text
initial_offset = old_displayed_eye - corrected_physical_eye

t = clamp(
        (sample_time - correction_start) / duration,
        0,
        1)

residual = initial_offset * (1 - t)
displayed_eye = corrected_physical_eye + residual
```

Decay is based on absolute caller-supplied monotonic time, never rendered frame
count. Sampling the same timeline at 30, 60, 144 or irregular FPS therefore
produces the same residual at equal absolute times and never changes simulation
or replay results. A time before the start clamps to the full offset; a time at
or after completion yields zero.

## Repeated corrections and snaps

When a new correction arrives during active smoothing, the previous residual
is evaluated at the new correction timestamp and combined with the new
old-versus-corrected eye offset. A non-finite combination fails. A combination
above `maximum_offset_magnitude` becomes `large_snap`; it is not accumulated
without bound.

Only `small_visual_correction` under the linear-decay profile and a positive
duration can remain active. `large_snap`, `teleport_snap`, `hard_reset`, hull or
movement-mode discontinuity, duration zero, and explicit no-smoothing policy
publish zero residual immediately.

Yaw and pitch remain the local player-walk camera orientation. M4.6.3.3 does not
smooth authoritative angles. Replay may restore movement-state angle metadata,
but camera look continues through the existing local first-person policy.

## Collision-constrained sampling

`sample_prediction_visual_correction` receives the corrected
`GameplayCameraState`, explicit sample time, optional world collision query,
caller-owned collision scratch, explicit query limits, and the movement
camera's maximum revision. An active nonzero offset requires a collision
query. The prediction controller accepts that query only when its world
identity, world revision, scene signature, and collision profile match the
active prediction session. Because the camera API supplies a raw world-only
query, a session using the synthetic-static-brush collision profile rejects it
even when the underlying BSP fingerprint and revision are identical. A
world-only query with a different scene signature or one cloned from another
session likewise fails closed.

The sampler traces the point camera from corrected eye to desired smoothed eye
against world model zero. A hit clamps the displayed eye to the verified trace
endpoint and sets `constrained`. The final point is position-tested; a
start-solid/all-solid trace, blocking endpoint, non-finite result or query
failure returns `visual_correction_collision_failed`.

Only the returned camera position and, when needed, its revision change. A
publication revision advances exactly when the published camera content
changes; repeated identical or inactive samples retain the last revision.
Start, changed intermediate samples, and completion therefore advance
monotonically, while completion followed by an ordinary physical-camera update
cannot reuse or regress a revision. Revision exhaustion is preflighted before a
local movement/history commit, so it is transactional. While smoothing is
active, this preflight reserves a revision for the remaining content-changing
residual publication against the greater of the candidate physical-camera and
last presentation revisions; exhaustion leaves movement, history, camera, and
visual-correction state unchanged. The corrected
simulation origin remains untouched. Once residual reaches zero, the state
becomes inactive and later samples create no camera revision solely for old
smoothing metadata.

The sampler does not rebuild a scene or change world, brush, Studio, Sprite,
geometry or texture resource identity. The renderer consumes only the final
camera and normal `RenderScene`.

## Normal live reference velocity-correction profile (E32 follow-up)

The normal live owner reuses this neutral API for a narrowly classified server
velocity correction. It does not change the API defaults above or infer damage.
The exact acknowledged position error must be greater than 0.25 and at most
32 units, authority age at most 100 ms, and the velocity difference at least
32 units/s. Projecting position error onto that velocity difference must give
a positive interval at most 100 ms, with at most 2 units perpendicular error.
These are local-compatible bounds, not claimed stock constants.

Walking/airborne transitions are allowed, including an upward server impulse
without a local jump. Hull/view offset must remain equal, duck transitions and
ladder/liquid modes are excluded. New setangle sources, advertised EF_NOINTERP,
dead/reset states and blocked body/eye paths deny the profile. The persisted
spawn setangle slot alone is not a new discontinuity. Presentation rechecks
context and pose even between clientdata ACKs.

Successful rebase publishes corrected simulation and replays the saved suffix
immediately. Only the displayed eye retains its previous position, decaying
over 100 ms with a 32-unit total-offset limit. During that transition ordinary
accepted corrections (up to the existing 16-unit gate) and further qualifying
velocity corrections retarget from the already displayed eye, after removing
the old residual from the new physical sample. They keep the original end
time, so repeated ACKs cannot prolong smoothing indefinitely. Same-time cache
reads do not repeat collision queries. All samples retain world/brush/full-hull
collision checks; the separate small exterior-corner path is disabled here.

The normal diagnostic reason is `visual_server_velocity_correction`; no new
feature flag is required. Larger/unclassified corrections now retain a
successfully validated physical rebase with a hard visual snap (E32
contact/stale-seed follow-up). They clear all prior residuals and predecessor
bridges instead of discarding exact command slots solely because the visual
16-unit gate was exceeded. Invalid authority, failed collision/replay and
expired command seeds still suspend; no correction is reclassified as an
impulse to make it eligible. Explicit discontinuities snap, and genuinely blocked smoothing may
still produce a visible correction. An unmarked server displacement is not
provably distinguishable from an impulse by position/velocity alone. This is
bounded presentation policy, not stock-binary parity or proof of a damage cause.

See [prediction reconciliation](PREDICTION_RECONCILIATION.md) and the
[prediction viewer](PREDICTION_VIEWER.md).
