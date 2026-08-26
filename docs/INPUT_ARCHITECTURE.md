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
