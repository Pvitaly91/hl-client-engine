# Gameplay Input Intent

`GameplayInputBindings` maps bounded physical inputs to project actions. The
default `project_default_v1` profile maps W/A/S/D, Space, Left Control, Left
Shift, E, R, mouse left/right, Tab, and Escape. Bindings contain enums only:
there are no command strings, filesystem paths, console evaluation, or
network side effects. Exact duplicates and ambiguous bindings are rejected
unless the caller deliberately enables the current explicit shared-input
policy; the project-default profile keeps sharing disabled.
The named default profile is reserved for that exact canonical binding set;
arbitrary mappings must use `project_custom_v1` and cannot masquerade as the
default.

`GameplayInputIntent` is an immutable owning value with profile
`local_keyboard_mouse_intent_v1` and evidence
`project_owned_input_semantics`. It contains the input sequence, three local
axes, yaw/pitch deltas, project button masks and edges, wheel metadata,
focus/capture flags, and bounded sample duration. The future stock mapping is
`stock_usercmd_mapping_evidence_pending`.

Opposite movement actions cancel exactly. Mouse look is produced only while
focused and captured. The default sensitivity is 0.10 degrees per pixel and
is a project preview value, not the GoldSrc sensitivity formula. Mouse
displacement is never multiplied by frame duration; keyboard translation is.
Because positive project yaw turns +X toward +Y (camera-left), mouse motion to
the physical right produces a negative yaw delta and turns the view right.

Space and Control remain the typed `jump` and `duck` buttons. The diagnostic
free-flight adapter may additionally interpret them as local up/down movement
under `diagnostic_free_flight_v1`; this is not player movement semantics.

`GameplayInputLimits` publishes the complete inspectable M4.6.1 safety
envelope while each owning layer enforces its narrower configuration. Project
defaults cap a frame at 1,024 events, 64 bindings, relative mouse magnitude at
1,000,000 per axis, wheel magnitude at 10,000, sampled camera time at 0.1 s,
camera position magnitude and speed at 10,000,000 and 10,000 source units,
respectively, and camera revisions at `uint64_t` maximum. The hard event cap is
8,192. Mouse sensitivity is 0.001–10 degrees/pixel and vertical FOV is
20–140 degrees. Exact hard bounds are accepted; limit-plus-one and non-finite
values fail before publication.

There is intentionally no conversion to `usercmd` bytes, stock button bits,
`msec`, impulse, weapon selection, checksum, command number, or network TX.
M4.6.2 owns that evidence and codec boundary.
