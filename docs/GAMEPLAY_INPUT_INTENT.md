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

The M4.6.1 intent layer intentionally performs no conversion to `usercmd`
bytes, stock button bits, `msec`, impulse, weapon selection, checksum, command
identity, or network TX. M4.6.2 adds a separate
`synthetic_explicit_v1` adapter that consumes this immutable intent, maps only
its documented project actions, and keeps diagnostic vertical movement out of
`upmove`. Stock input mapping remains evidence-pending with zero accepted
stock usercmd runs and zero verified move packets. See the
[GoldSrc usercmd boundary](GOLDSRC_USERCMD.md) and
[transmission lifecycle](GOLDSRC_USERCMD_TRANSMISSION.md).

M4.6.3.2's local player controller is a separate consumer of the same intent.
It samples intent through the fixed synthetic scheduler, retains pressed-button
one-shots until the first successfully simulated command, and clears focused
movement when the intent becomes neutral after focus loss. WASD produces only
horizontal forward/side command values, Space and Ctrl map to the named
synthetic jump/duck bits, and mouse deltas update the player-walk camera.

This does not change the intent contract into a stock input mapping. The
synthetic speed/run bit does not scale dry-walk movement, and no intent is sent
over the network by the local controller. See
[player-walk viewer](PLAYER_WALK_VIEWER.md).

M4.7.2F adds a deliberately narrower production consumer for an explicit live
visual-control session. It takes only the intent's W/S and A/D axes plus
captured relative mouse look, normalizes diagonals, and submits bounded 20 ms
samples through the already verified reference usercmd
history/planner/transmission chain. It does not map Space, Control, mouse
buttons, wheel, use, reload, weapon selection, or the older synthetic button
masks. This project amplitude and sensitivity policy is not claimed to be the
original GoldSrc configuration.

Mouse yaw/pitch is local view state shared by rendering and subsequent
usercmds. Camera translation is never derived from that local intent: it is
published only from a fresh receiving-client origin plus the validated
clientdata view offset. Consequently a held W key cannot move the camera until
the server returns a new position, and there is no prediction or
reconciliation in this mode.

M4.7.2G extends only the explicit live visual consumer. Project `jump` and
`duck` actions from Space and Left Ctrl map through a separate reference-wire
button policy to Valve `IN_JUMP` (`1 << 1`) and `IN_DUCK` (`1 << 2`), as defined
by pinned `common/in_buttons.h` revision
`b1b5cf5892918535619b2937bb927e46cb097ba1`. The old synthetic mapping
type remains distinct. Other actions remain rejected by the reference adapter;
the application passes only the jump/duck project mask. A bounded pending press
is consumed after the command enters history, not at render publication or
Netchan ACK. `upmove`, impulse, attack and weapon fields remain zero.
