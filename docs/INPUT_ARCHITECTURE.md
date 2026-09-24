# Input Architecture

M4.6.1 introduces the project-owned `local_keyboard_mouse_input_v1`
profile. It is a local preview input contract, not a reconstruction of
GoldSrc key bindings, `kbutton_t`, `usercmd_t`, cvars, command sampling, or
prediction. Every stock mapping remains `evidence_pending_m4_6_2`.

The dependency flow is deliberately one-way:

```text
SDL3 event -> SdlWindow event pump -> PlatformEvent/InputEvent
    -> InputStateTracker -> immutable InputSnapshot
    -> GameplayInputIntent -> InteractivePreviewController
    -> ClientWorldState camera -> RenderScene -> renderer
```

`InputEvent`, `InputSnapshot`, and input sources contain no SDL types. SDL3 is
private to the platform implementation. The renderer receives only the
resulting camera; it never polls input and never retains a snapshot.

## Frame lifecycle

`InputStateTracker` accepts one ordered event stream between `begin_frame()`
and publication. Held states survive a frame boundary. Press/release edges,
relative mouse displacement, and wheel displacement are frame-local. A
snapshot sequence increments exactly once for every successfully published
frame and never silently wraps.

Snapshots also retain the bounded held-key/button state from the start of the
frame and the frame-start capture state. Gameplay binding reduction uses that
immutable boundary state to emit
action edges for the OR of all physical bindings, so a second binding cannot
re-press an already-held action and releasing one binding cannot release an
action still held by another. Focus-loss and capture-acquisition resets begin
a new action domain: an explicit post-reset press in the same frame remains a
real press rather than being masked by pre-reset held state.

Events are treated as untrusted. Counts and accumulated deltas are bounded;
integer arithmetic is checked; invalid events do not partially publish a
snapshot. Fixed-size key/button storage prevents unchecked enum indexing and
unbounded ownership.

Focus loss clears held keys and buttons, clears motion, releases capture, and
records an explicit reset. Focus regain synthesizes no held state. Repeated
key-down preserves held state but creates no duplicate press edge.
Press edges from before a focus loss are discarded even if focus returns in
the same platform frame; only a new post-regain press can request capture or
produce an action.

Capture acquisition starts a fresh mouse-button domain: buttons already held
while uncaptured are discarded until their physical release and a subsequent
press. Consequently the click that requests capture can never become a held or
pressed attack on the next frame. The immutable snapshot retains a bounded
per-button capture-discard mask (covering held, pressed, and released state
that the transition clears), so the intent reducer can close a previously
published non-gesture mouse action exactly once without fabricating an attack
edge from the uncaptured left-click gesture.

Capture release preserves physical button tracking but ends the captured
action domain. A left button that was an attack at the captured frame boundary
therefore emits exactly one action release even when its physical button-up is
delayed; an ordinary uncaptured capture click emits no attack release.

`NullInputSource` provides deterministic zero input and is sampled by the
headless `--renderer null` route, including legacy CPU stop points.
`ScriptedInputSource` provides bounded project events directly to tests
without Windows input injection, raw SDL records, files, command strings, or
network data.

## M4.7.2F live visual control

The explicit `--stop-after live-visual-control` route reuses this event owner
and intent reduction in the normal `hlclient.exe` application loop. It does
not add a second SDL poller. `--live-input keyboard-mouse` starts uncaptured;
a focused left click acquires relative mouse, Escape releases it, an
uncaptured Escape or window close ends the bounded session, and focus loss
clears held state and all pending deltas. The capture click has no fire
meaning.

Only W/S forward movement, A/D side movement, and mouse yaw/pitch are mapped
to the production usercmd path. Opposite keys cancel and a diagonal is
normalized before applying the project amplitude of 100. `upmove`, buttons,
and impulse remain zero. Mouse displacement is consumed once per published
input frame; held axes are sampled by the existing fixed 20 ms command
scheduler, independently of render FPS. Sampling starts only after the same
live connection reaches the M4.7.2E production handoff and the selected input
source is activated. Activation additionally waits for one complete swapped
OpenGL frame with the retained world uploaded and drawn. Optional entity
visibility is evidence only and cannot deadlock input activation.
Authentication, CPU asset loading, and the first potentially slow GPU upload
therefore remain outside the fixed command scheduler's time domain and cannot
create a movement-command backlog.

The application loop remains the sole owner of window events, Steam callback
progress, network update, runtime publication, visual projection, rendering,
and teardown. Renderer code receives camera and scene values only; it owns no
network or input handles. The `scripted-check` source is an explicit automated
F mode using the same application, OpenGL, driver, scheduler and publication
path; it never runs implicitly in keyboard-mouse mode.

The explicit `scripted-side-check` source uses the same bounded five-phase
path for neutral/left/neutral/right/neutral server-motion evidence. Both
scripted sources complete through the same scenario gate; keyboard-mouse
control remains active until its session stop. A successful scripted side
check does not establish that physical A/D keys were pressed.

## M4.7.2G live buttons

The same SDL event pump, tracker, intent builder and 20 ms reference-command
path now take Space as typed jump and Left Ctrl as typed duck in the explicit
keyboard-mouse live mode. The physical press edge is retained in a two-bit
bounded latch until the corresponding command is inserted into history; a
failed insertion leaves it pending. A held key remains a button on following
commands without generating new press edges. Release clears following new
commands, while focus loss clears held and pending input. Existing driver-owned
history and backups retain their command identities. Mouse capture clicks do
not set attack. `upmove` and `impulse` remain zero.

The explicit `scripted-jump-duck-check` source is bounded to 9.2 seconds of
commands after the first presented scene: 2 s settling, 1.2 s jump hold,
2 s jump release, 1.5 s duck hold, then 2.5 s released (including a 1 s
neutral tail). It uses the same stage and does not activate in keyboard mode.
Server-derived receiving-client origin, velocity and view offset provide its
observations; camera translation stays `origin + view_offset` without local
vertical movement or prediction.
