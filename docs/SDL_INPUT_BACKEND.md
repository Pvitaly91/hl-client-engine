# SDL Input Backend

`SdlWindow` is the sole owner of `SDL_PollEvent`. Its platform event pump
translates each native event once into either a neutral window event or a
neutral input event while preserving queue order. There is no second input
backend consuming the SDL queue. Construction claims a process-global owner;
a second live `SdlWindow` is rejected before it can consume events. Supported
window-scoped events are accepted only for the claimed window ID. Automatic
and requested capture transitions wait in a bounded eight-entry FIFO, so
multiple transitions cannot overwrite one another before polling. Acquisition
reserves the final queue slot for a fail-safe release; saturation therefore
cannot leave operational capture active without room to report its release.
Every successful native poll is counted before translation or window-ID
filtering. One SDL poll cycle accepts exactly the project hard maximum of
8,192 native events; discovery of event 8,193 releases operational capture
and emits the terminal typed `native_event_limit_exceeded` event. This bounds
even streams made entirely from unknown or foreign-window events. Once the
pump is terminal, capture acquisition is rejected and explicit release remains
available as checked cleanup; no transition can be queued behind an
undeliverable terminal event.

Physical keyboard identity uses SDL scancodes, not localized keycodes. The
bounded public set contains W/A/S/D, Space, Left Control, Left Shift, E, R,
Escape, Tab, arrows, F1, and F2. Unknown scancodes and mouse buttons are
ignored or classified without exposing arbitrary integers. Supported mouse
buttons are left, right, middle, X1, and X2.

Mouse motion uses relative signed deltas. Absolute desktop coordinates never
drive camera look. Wheel values are finite bounded metadata only; M4.6.1 does
not map the wheel to weapons or commands. SDL event timestamps are not
gameplay or server time.

## Relative capture

Interactive viewers start released. A focused left click requests SDL3
relative mouse mode. Escape releases capture; focus loss also releases it and
restores the cursor. An uncaptured Escape retains the historical local viewer
quit behavior. Capture transitions are published as neutral events.

A failed SDL relative-mode transition returns a typed failure with the queried
actual capture state and attempts an immediate released/cursor-visible
recovery. Interactive viewers retry release and terminate safely if recovery
cannot be confirmed; window destruction makes a final unconditional release
and cursor-show attempt. No failed transition is silently treated as a
successful capture/release. Hidden smoke windows never require capture or
physical input.

Capture is considered operational only when SDL reports both the per-window
relative-mode flag and actual keyboard focus for that same window. A queued or
synthetic focus event alone cannot make a hidden window appear captured.

Focus and Escape cleanup reconcile against SDL's actual relative-mode query.
If automatic release or cursor restoration fails, the sole event pump emits
the typed `input_capture_recovery_failed` window event; viewers fail closed
instead of continuing with tracker/window capture states that disagree.

Text input, IME, clipboard, chat, console input, command binding, and cursor
warping are outside M4.6.1.
