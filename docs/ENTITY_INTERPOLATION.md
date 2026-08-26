# Entity snapshot interpolation

The pure interpolation contract and implementation live in
`hlclient::entity_interpolation`. That target depends only on the visual
projection boundary (and its transitive renderer-neutral snapshot/asset value
contracts); it does not depend on visual-asset import, readiness, local-resource,
or filesystem targets. `hlclient::entity_interpolation_stage` is the separate
client-composition target that sequences an `EntityVisualAssetStageResult` into
the pure interpolator. Its terminal publication occurs only after the external
pose/Sprite composer has produced an immutable renderer-neutral
`EntityRenderFrame`: the result retains both the exact interpolated input and
that render frame, and rejects mismatched snapshot identities, sample/alpha,
entity candidates, transforms, or frame statistics. The client-stage target
privately links only the renderer-neutral `hlclient::entity_scene_render`
contract; neither the pure interpolation target nor renderer implementations
gain this client dependency. Consumers that use only pair selection or
interpolation must link the pure target; composition roots that use the
orchestration stage must link the stage target explicitly.

`synthetic_seconds_v1` is the only implemented time domain. Callers supply a
finite typed seconds value; `EntityServerTime::synthetic_raw()` is deliberately
not converted to seconds. Stock server-time units remain evidence-pending.

The selector chooses the bracketing snapshots and computes
`(target - previous) / (current - previous)`. It does not extrapolate: targets
before or after history hold the oldest or newest snapshot. Duplicate or
misordered times and excessive gaps are typed failures.

`EntityInterpolationProjectionAdapter` is the production boundary between
`EntityVisualProjectionState` and interpolation. It requires an exact
synthetic snapshot reference, evidence profile, entity count and ascending
entity order, then publishes an owning
`EntityInterpolationProjectionFrameState`. The owning frame contains no wire
objects, filesystem state, native paths or renderer resources. Its `view()` is
the bounded non-owning input accepted by `EntitySnapshotInterpolator`.

The adapter preserves the typed model reference, sequence/body/skin and Sprite
frame controls, controller and blending arrays, mouth value, render
mode/amount/color, animation-start time and inert effects metadata. Invalid
profiles, mismatched typed/numeric model references, unknown enum values,
non-finite controls and configured entity/byte-limit violations fail before
publication. Synthetic model slot zero is valid and is never treated as a
sentinel. Stock visual projection remains evidence-pending.

Positions use finite linear interpolation. Euler axes use a shortest-path delta
normalized to `[-180, 180)`; therefore an exact positive 180-degree tie resolves
to -180 degrees. Added entities appear at the current timestamp and removed
entities remain visible until it. Model, sequence, body, skin, render mode,
sprite category, and effects step at the current timestamp. Explicit `step` and
`teleport` modes never infer a distance threshold. No fades, collision repair,
dead reckoning, prediction, or extrapolation are introduced.

Controller/blending/mouth and render amount/color/animation-start controls also
use the explicit current-timestamp step policy in M4.5.3. This preserves all
canonical pose/render inputs without inventing unverified between-snapshot
semantics. `InterpolatedEntityState` exposes these values through typed
accessors for the renderer-neutral frame composer.
