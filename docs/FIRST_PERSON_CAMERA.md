# First-Person Camera

M4.6.1 implements only `local_first_person_z_up_v1`.
`stock_view_angles_evidence_pending` remains fail-closed. The local convention
is right-handed GoldSrc-source Z-up: yaw rotates around +Z, yaw zero faces +X,
yaw +90 faces +Y, positive pitch looks upward, and roll is zero.

Yaw is normalized deterministically to `[-180, 180)`. Pitch is clamped to the
configured interval, project-default `[-89, +89]` degrees. Render-camera
conversion is pure and validates finite forward/up vectors, FOV, and near/far
planes through the existing renderer-neutral camera contract.

The project safety range is 0.001–10 degrees/pixel sensitivity, 1–10,000
source units/second camera speed, 1–16 speed multiplier, 20–140 degrees
vertical FOV, at most 1 second applied frame duration, and at most 10,000,000
source units of camera-position magnitude. Near depth is strictly positive;
far depth is greater than near, representable after conversion to renderer
floats, and no greater than 100,000,000. Camera revisions start at one, never
wrap, and are capped by the immutable configuration. Project defaults narrow
the frame-duration cap to 0.1 second, speed to 320, multiplier to 2, and pitch
to `[-89, +89]` degrees.

The diagnostic free-flight controller uses horizontal forward/right vectors
for WASD and world +Z for Space/Control. The movement vector is normalized
when its length exceeds one, preventing diagonal speed gain. Keyboard motion
uses bounded elapsed time, a default speed of 320 source units/second, and a
default Shift multiplier of 2.0. Mouse displacement is frame-time
independent.

The controller implements no collision, gravity, acceleration, friction,
ground state, player hull, movement simulation, prediction, or
reconciliation.

An entity-first-person camera consumes an explicit synthetic entity number,
interpolated origin, local eye offset, and source-frame identity. The default
fixture offset `(0, 0, 28)` is project metadata, not a stock eye height. If
the anchor disappears, the controller freezes the last valid camera and
publishes `anchor_missing`; it never attaches to another entity. Camera
orientation does not mutate snapshots and is never sent to a server.
The frozen state must already match the active pitch, FOV, and depth-plane
configuration; a caller that changes configuration without republishing a
compatible camera receives a typed `invalid_input` result rather than stale
camera metadata.
