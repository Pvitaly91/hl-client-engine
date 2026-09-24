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

M4.7.1.2G adds a deliberately narrower application projection for the
project-owned `visual-entities` runtime replay fixture. It consumes the neutral
committed observation produced by A/B/C/D/E, requires complete finite decoded
origin and angles, and maps replay entity identity to one project-generated
diagnostic Studio model. It neither interprets `modelindex` nor changes the
stock evidence-pending provider above. The complete candidate is validated by
the existing package/frame builders and published atomically to the same
`ClientWorldState`; clientdata-only records retain the prior frame.

The synthetic record carries bounded, finite transform, Studio, Sprite, render,
scale, animation-time, effect, and interpolation controls. Its model reference
is an exact type-local `PrecacheManifestState::model_slots()` index. It is not a
resource-list ordinal and is not a stock `modelindex` interpretation.
