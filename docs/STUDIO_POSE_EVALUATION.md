# Studio pose evaluation

Pose evaluation is a bounded CPU operation over immutable imported model data.
Inputs explicitly name sequence, fractional frame, body, skin, four controllers,
two blend controls, mouth, and uniform scale. Invalid sequence groups, frames,
controllers, body values, skin families, meshes, or non-finite data are errors;
there is no sequence-zero or family-zero fallback.

Looping sequences wrap by `max(frame_count - 1, 1)` and non-looping sequences
clamp to their final frame. Animation channels are sampled at adjacent frames;
translations lerp, rotations are converted to radians and composed as
quaternions. One-, two-, and four-blend sequences are supported, with
deterministic linear/spherical and bilinear composition. Other blend counts and
unknown controller kinds are typed unsupported results.

Motion-bone translation suppression follows the retained source axis flags and
does not apply root motion to entity origin. Local transforms are evaluated in
validated source order, then child world matrices are `parent * local`.
Bodypart selection uses the public Valve base/model-count formula and skin is an
exact family. A bounded per-playback-frame cache may share identical poses and
is reset between frames; events are retained metadata and never executed.
