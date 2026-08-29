# Interactive Preview

`InteractivePreviewController` is renderer-neutral. It consumes an immutable
`InputSnapshot`, builds a project `GameplayInputIntent`, updates either the
free-flight or entity-first-person camera, and transactionally publishes the
camera plus compact revisions to `ClientWorldState`.

The controller does not poll SDL, read files, load assets, decode packets,
create commands, call a renderer, or access a network driver. Invalid input or
configuration leaves the previously published camera intact.

`ClientWorldState` retains only neutral input/camera revision metadata, camera
mode, and optional controlled-entity status. `RenderScene` continues to expose
only the resulting `RenderCamera`; no input event, snapshot, binding, or
intent reaches OpenGL or NullRenderer.

Camera movement may produce a new CPU visibility/draw-list revision. It does
not alter the world package, entity scene package, Studio asset, Sprite asset,
or their GPU cache keys. Zero input leaves a local first-person camera
bit-stable and does not increment its camera revision.

Before frame sampling, `seed_world_state_camera` publishes the exact canonical
render-camera payload represented by the controller. The viewer then refreshes
visibility once. Consequently, the first empty input frame keeps both camera
bits and visibility revision stable instead of changing only target distance
under an unchanged gameplay-camera revision.

For interactive entity frames, `InteractiveEntityVisibilityRefilter` rebuilds
only the immutable per-frame visibility result from the current camera. It
resets prior camera-derived PVS/frustum culls, applies the new frustum and an
available validated camera-leaf PVS row, and publishes an explicit monotonic
frame revision strictly newer than its source while retaining package, pose,
interpolation, and asset
identities. This is CPU work only and cannot upload or mutate renderer assets.

The world viewer accepts `--camera free-fly`. The entity viewer accepts
`--camera free-fly` and the synthetic-only pair
`--camera entity-first-person --controlled-entity <number>`. Interactive
controls are click-to-capture, Escape-to-release, WASD movement,
Space/Control vertical motion, Shift speed, and relative mouse look. Bounded
hidden smoke runs remain stationary unless tests provide a
`ScriptedInputSource`.

For its synthetic entity-first-person fixture, the entity viewer uses an
explicit `(0, 0, 28)` viewer-fixture eye offset, the project 0.1 near plane,
and a deterministic facing direction toward the nearest different fixture
entity.
This keeps the bounded mixed Studio/Sprite diagnostic visible without
inferring a stock eye height or stock view angles.

The same synthetic viewer explicitly retains its controlled camera-anchor
entity from camera-derived PVS/frustum culling. Other entities are refiltered
normally; unavailable or unsupported controlled visuals remain unavailable or
unsupported. This is a project diagnostic draw policy, not a claim about stock
first-person self rendering.

These viewers are offline diagnostics. They provide no live multiplayer
control and send no input packet.

## Player-walk publication

The world viewer's `--camera player-walk` route uses the same input snapshot
and camera publication boundary but replaces noclip translation with
`LocalPlayerMovementController`. Its collision-valid spawn, immutable state,
fixed synthetic commands and world-only traces remain outside
`InteractivePreviewController`; free-fly behavior is unchanged.

`InteractiveCameraMode::player_walk` carries input and camera revision metadata
into `ClientWorldState`. The scene source refilters CPU PVS/frustum state from
the new camera without reopening assets or re-uploading world, brush, Studio or
Sprite resources. Click/Escape control relative capture, WASD walks, Space
jumps, Ctrl ducks and mouse motion looks. This remains an offline diagnostic
and sends zero network operations.

A typed player-walk failure is a one-way simulation latch, not permission to
publish a partial camera or close an otherwise healthy renderer. The last valid
camera/visibility state remains published, held input and relative capture are
released, and polling/render/swap continue until close. The process still
returns nonzero after close. Metadata-only summaries are available through
`--movement-diagnostics summary`.
