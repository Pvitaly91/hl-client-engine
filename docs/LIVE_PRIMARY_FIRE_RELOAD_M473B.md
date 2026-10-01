# M4.7.3B primary fire and reload slice

The physical left mouse button is a capture gesture while relative mouse is
inactive. After capture it supplies the held primary attack action. Physical R
supplies reload. The existing SDL tracker clears both on focus loss. The
reference adapter accepts only Valve `IN_ATTACK` (bit 0), `IN_JUMP` (bit 1),
`IN_DUCK` (bit 2), and `IN_RELOAD` (bit 13) as gameplay bits. The immutable
quantized command is used for both transport and movement history; the movement
kernel reads jump and duck and has no weapon side effects. The 20 ms scheduler,
backup transmission, and reconciliation replay remain single-owner.
At an initial carrier anchor without an exact retained prediction slot,
weapon-only buttons may use neutral movement-edge state. Jump or duck still
requires the exact retained button state; the carrier ACK is not treated as an
execution ACK.

The live observation projects optional `clientdata.punchangle[0..2]` and
weapondata `m_iId`, clip, reload flag, and timers from negotiated fields.
`svc_weaponanim` is an exact two-byte fixed control message. Its source
identity controls Studio restart; retained records do not restart a sequence.
The Studio evaluator applies the model's looping or terminal-frame policy.
Invalid sequence values retain a prior valid pose for the same model, or
report unsupported pose. Camera rendering applies server punch to a copy of
the local view direction; local mouse state and outgoing view angles stay
independent. Positive Valve pitch is converted to the renderer's positive-up
camera convention.

The dedicated first-person camera-space pass from A1 and screen-space HUD are
unchanged. HUD clip and reserve values come from committed server observations.
No local ammo decrement, reload completion, shot, damage, or effect is inferred
from button transmission. The bounded `scripted-fire-reload-check` route asks
for an eligible current-session Glock, submits three short primary presses,
submits reload, asks for an eligible crowbar, and submits one primary press.
Selection is still pending until server state confirms it. The action summary
keeps command submissions separate from fresh clip/reload transitions and
weapon animation messages. A transport ACK is never called a weapon execution
ACK.

Source references: Valve Half-Life SDK revision
`b1b5cf5892918535619b2937bb927e46cb097ba1`, specifically
`common/in_buttons.h`, `cl_dll/input.cpp`, `cl_dll/view.cpp`, and
`network/delta.lst`. The implementation uses semantic information only.

Scope excludes secondary attack, weapon prediction, damage and hit proof,
tracers, impact decals, muzzle effects, shell casings, sounds, and bob/sway.
Manual fire/reload validation remains pending until a new user run.

The first managed B run (`7b3f7df916074252bfa683094375fe39`)
confirmed three Glock clip decrements and a server clip/reserve reload
transition, but the 11.6 s command script outlasted the 60 s live stage's
remaining active window after preparation. It stopped after 437 of 580
commands. The fixed five-phase script now takes 7.0 s at the same 20 ms
command cadence, retains three Glock presses, one reload press, one crowbar
press, and a final moving control window. The bounded diagnostic excerpt now
preserves the final `live_fire_reload` summary before its line budget fills.
Neither adjustment changes command encoding, weapon simulation, or the
server-authoritative success criteria.

The final permitted managed run (`13c55c8f89f349839da9a9da0244bf98`)
submitted 16 attack and 4 reload commands. Fresh server state confirmed three
Glock shots (clip 17 to 14), reload start and completion (clip 14 to 17,
reserve 68 to 65), and a switch to crowbar. Viewmodel and HUD pixels were
distinct, and movement prediction was active during the script. The run ended
after 324 of 350 commands because the shared live-stage deadline still had
less active time than the shortened script needed. Most importantly, this
connection delivered zero `svc_weaponanim` events, so fire/reload/crowbar
animation and their framebuffer changes were not observed. This is an honest
partial result; the two-session B budget is exhausted. The pinned Valve SDK
`dlls/weapons.cpp` has a conditional skip-local path for `SVC_WEAPONANIM`,
which is a possible explanation for its absence, not proof about this binary.
No client weapon prediction or synthetic animation event has been added.
The wrapper exited nonzero because the client returned 2, while owned-process
cleanup and external-state restoration both reported `exact`. The manual
keyboard/mouse path remains available from the unchanged launcher; no manual
fire/reload smoothness or animation claim has been recorded.
