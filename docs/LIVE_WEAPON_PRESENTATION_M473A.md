# M4.7.3A live weapon presentation

This slice uses the existing live runtime owner, replay transaction, sparse
resource namespace, Studio importer and renderer. It does not predict weapon
logic, fire, reload or execute server supplied strings. Movement prediction
remains the H4 reference path.

The H4 manual baseline is `attribution=user_report`: basic WASD/Shift/mouse
controls passed; standing and Shift walking smoothness passed; jump/landing,
duck/stand and crouched walking smoothness passed twice on the current local
build. New weapon/HUD manual validation is `not_run` until the user tests it.

## Sources and state

The pinned Valve SDK revision is
`b1b5cf5892918535619b2937bb927e46cb097ba1`. The relevant public
contracts are `network/delta.lst` (`clientdata.viewmodel`, `weapons`,
`weaponanim`), `dlls/player.cpp` (WeaponList, AmmoX, Battery),
`dlls/weapons.cpp` (CurWeapon and `svc_weaponanim`), and
`cl_dll/ammo.cpp` (active versus inactive CurWeapon, clip sentinel and HUD
selection). ReHLDS revision
`6266cd23faee4a6e9cf3974f9605b2cadd86f0a4` confirms that
`clc_stringcmd` is dispatched to its string-command parser in
`rehlds/engine/sv_user.cpp`. No Xash-specific protocol branch is used.
These references supply format and semantic facts; the project
decoder, state model and renderer are independent implementations.

The runtime replay transaction accepts only currently registered user message
IDs. It validates exact fixed lengths and the bounded WeaponList variable
body before publishing the complete observation. WeaponList is a catalogue;
the current clientdata `weapons` bitset supplies ownership. CurWeapon selects
the active ID only when its state is active. Inactive updates may refresh a
clip. ID zero or a signed negative sentinel explicitly clears active state.
Unknown clip, reserve and armor remain unknown. An absent weapondata delta
does not revoke ownership. A separate typed weapon/HUD hash covers these
observations without changing the historical canonical replay hash. Catalogue
entries, active selection and animation carry their own record sources; the
HUD observation also retains the most recent recognized message source and
monotonic transaction revision within the current generation.

The resource response handoff retains bounded user messages and animation
events seen before the first runtime baseline. Their original record bytes
and cursors are replayed in order through the same transaction once runtime
state exists. A malformed recognized message fails the candidate atomically;
an unknown correctly framed message stays opaque.

## Model, HUD and selection

The first-person model is bound by fresh clientdata `viewmodel` index to the
current sparse precache entry. Index zero hides it. Only a ready local
`models/v_*.mdl` Studio asset from the rooted research installation can be
shown. A CurWeapon/viewmodel half-transition hides the previous binding until
the channels agree. Unknown or no-weapon active state clears the binding;
the first valid CurWeapon observation can then establish it. Resource
imports are performed by the existing loader, outside the per-frame renderer.
Observed nonpositive health hides the model and weapon-specific HUD values;
health itself remains visible as the received value.

The Studio pose evaluator consumes a valid explicit weapon animation event
for the model. The same retained event does not restart on every frame.
Without an event, clientdata `weaponanim` is used when available; otherwise
sequence zero is a marked neutral presentation pose. Animation events are
data, not executable callbacks. This does not claim full Valve animation or
effects compatibility.

The first managed run exposed a real material gap: `v_crowbar.mdl` imported,
but its Chrome material was classified unsupported, so the first-person pass
had no frame to draw. The shared Studio material policy now accepts Chrome
when its other metadata bits are known. The OpenGL Studio shader derives its
texture coordinates from the posed normal and camera basis, scaled by the
material texture dimensions. This is a bounded presentation policy informed
by the pinned Valve `utils/mdlviewer/studio_render.cpp`; it does not claim
bit-exact GoldSrc Chrome lighting. Additive, alpha and unknown material
combinations remain unsupported. An opt-in local-asset test loads the actual
research-copy `v_crowbar.mdl` through the production provider and verifies
both framebuffer pixels and the production sampled signature change over a
nonempty BSP. The ordinary fixture tests remain independent of installed
game assets.

The live A/B check samples actual framebuffer color with and without the
first-person pass. It retains at most two candidate frames per bound asset,
separated by 250 ms, and at most six frames in the scripted session. Readbacks
run after the scripted command source stops, so GPU verification cannot stall
the 20 ms scheduler. A successful distinct sample stops the readbacks. The
local real-asset test covers both crowbar and pistol
at a 1280x720 OpenGL context, matching the managed run's pixel extent.
In the physical keyboard-mouse session, A/B rerenders are disabled and the
ordinary framebuffer diagnostic runs only before input activation. A user
manual run exposed a 628 ms update gap and bounded scheduler failure while
these GPU diagnostics still ran after activation. This policy removes that
verification work from the live input loop without changing the 20 ms
command timeline or the model/HUD rendering itself.

The separate first-person pass uses camera-local Studio geometry and a fixed
+X-forward, Z-up view matrix. It inherits only the world camera's optical
parameters, so H4 eye translation and local mouse yaw/pitch cannot displace
the weapon across the screen. The pass clears depth after the world pass,
then preserves depth testing within the Studio model. The simple project
glyph HUD displays health, armor, active weapon, clip and reserve only when
the corresponding current-session observations exist. Unknown values use
`?`; no-clip uses `-`. HUD geometry uploads occur only when text or extent
changes. No external font is embedded.

Digits 1 through 5 and mouse wheel choose from current owned catalogue
entries in slot/position/ID order. The builder accepts only a bounded ASCII
`weapon_` token from that catalogue and emits one typed `clc_stringcmd` on
the existing reliable queue. Focus loss removes pending input. Pending
selection stays separate from the authoritative CurWeapon and viewmodel; a
queued or acknowledged request is never counted as a successful switch.

The scripted `scripted-weapon-check` mode follows the existing 20 ms input
scheduler and sends at most one selection request if a second owned eligible
weapon is actually observed. Its partial result is expected if the spawn
offers only one weapon. The manual launcher retains its existing name and
`-Prediction reference` / `-Prediction off` contract.

## Verification boundary

The source-controlled fixtures cover dynamic message IDs, mixed payload
atomicity, signed sentinels, catalogue versus inventory, safe selection, and
actual OpenGL framebuffer differences over a nonempty world. A managed live
result is separate from these tests: it must observe server-derived weapon
and HUD values, first-person pixels, and a new server-confirmed active ID and
viewmodel after selection. Manual feel and visual judgment remain a user
test.
