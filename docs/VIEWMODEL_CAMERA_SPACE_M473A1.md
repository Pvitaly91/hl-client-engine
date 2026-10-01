# M4.7.3A1: first-person camera space

The user confirmed the live HUD, health/ammo display, Glock/crowbar selection,
visible Studio viewmodel, and smooth H4 movement. Looking sharply up or down
exposed an incorrect viewmodel pitch attachment. This record preserves that
observation as `attribution=user_report`; a new manual check of this fix is
pending.

## Reproduced cause and transform contract

The former production binding placed the Studio instance at the predicted eye
in world coordinates and assigned camera yaw/pitch to its world entity Euler
rotation. The OpenGL first-person pass then applied the ordinary world camera
view/projection. Positive camera pitch points the target toward +Z, while the
old positive Studio Y rotation points the model's +X axis toward -Z. The
transforms do not cancel. A production provider regression first failed on the
old eye origin and pitch/yaw rotation.

The current binding emits a model-local identity transform. The separate
OpenGL first-person pass uses a camera at the origin with +X forward and +Z
up, while copying the world camera's FOV and near/far planes. Thus neither
predicted eye translation nor mouse yaw/pitch enters the weapon matrix.
Those values still control the world camera and transmitted view angles.
World entities keep the ordinary view/projection and their world transforms.
The HUD remains a screen-space overlay. This is an explicit project projection
profile; stock-exact weapon FOV is not claimed.

The pass draws after world/entities, clears depth, and retains depth testing
within Studio geometry. It restores the normal pass ordering for the HUD.
No model scale, asset bytes, importer axes, canonical network state, input,
weapon selection, or movement code changed for this fix. Valve SDK revision
`b1b5cf5892918535619b2937bb927e46cb097ba1` (`cl_dll/view.cpp`) was
consulted for viewmodel versus world camera semantics; no functions copied.

## Evidence boundary

Source-controlled OpenGL fixtures compare viewmodel pixels with/without the
first-person pass over nonempty world geometry and check camera yaw/pitch and
translation, world entity separation, aspect ratios, depth and HUD order.
An opt-in local asset test imports `v_crowbar.mdl` and `v_9mmhandgun.mdl` via
the rooted provider and checks 1280x720 framebuffer pixels/bounds under
pitch ±30/±80 and yaw 0/90/180. Assets are read only and never committed.

The explicit `scripted-weapon-check` live mode additionally retains one
current-session Studio frame and takes at most three model-only OpenGL
readbacks at pitch 0/+80/-80. They use a single pose and require nonempty
identical pixel bounds and signatures. Same-scene weapon/HUD A/B and pitch
readbacks run after the scripted command source has stopped. The first A1
managed run exposed a 12-command scheduler backlog against the 8-command
cap when the pitch readback ran inside the 20 ms update loop; this placement
change removes that verification workload from the active scheduler.
The regular keyboard/mouse mode performs none of these pitch probes. The
bounded native summary includes camera-space status and pixel evidence; the
orchestrator and PowerShell runner preserve typed result/boolean/count fields.
The player's physical assessment of the corrected camera attachment remains
`manual_viewmodel_camera_validation=not_run` until another user check.

## Managed result

The first A1 run (`90de9b1c6b444da29be1c9ff1e6db810`) retained the
in-loop readback scheduler failure described above. After the placement fix,
run `cf4d31c147b2498f922b97b4ff40cd90` used the owned HLDS and current
project client on `valve/boot_camp` with reference prediction and OpenGL. It
reported 280 new commands, active reconciled prediction, one confirmed
Glock/crowbar selection, distinct live weapon/HUD pixels, and three identical
current crowbar model-only framebuffer signatures/bounds at pitch 0/+80/-80.
Each image had 44,629 non-clear model pixels and bounds `814,0,1112,578`.
The native A1 result was `live_viewmodel_camera_space_verified`; the wrapper
retained that typed result and finished with exact owned-process cleanup and
research restoration. The A1 budget is exhausted at two runs. The manual
mouse pitch check remains pending user validation.
