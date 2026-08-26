# Entity visual projection

M4.5.3 keeps decoded entity objects wire-neutral. Rendering consumes a separate,
immutable `EntityVisualProjectionState` produced by
`IEntityVisualProjectionProvider`; projection never mutates snapshot state and
never opens files or creates renderer resources.

Two profiles are explicit. `synthetic_entity_visual_v1` accepts only
caller-supplied typed records and is implemented. The production
`stock_protocol_48_evidence_pending` route returns an evidence-pending status.
Field spellings such as `modelindex`, `origin[0]`, or `sequence` are not evidence
and are never interpreted. Consequently this milestone does not claim stock
Protocol 48 entity rendering.

The synthetic record carries bounded, finite transform, Studio, Sprite, render,
scale, animation-time, effect, and interpolation controls. Its model reference
is an exact type-local `PrecacheManifestState::model_slots()` index. It is not a
resource-list ordinal and is not a stock `modelindex` interpretation.
