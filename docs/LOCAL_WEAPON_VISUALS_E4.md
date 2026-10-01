# E4 local Glock muzzle flash and brass casing

State: integrated in the normal `hlclient.exe` production path; offline and
OpenGL verification is recorded separately from live observation. The first
E4 build was only partially confirmed by `attribution=user_report`:
`muzzle_flash_visual=visible`, `muzzle_lighting=not_observed`,
`shells=not_observed`. This report has no verified launched-process SHA. The
E4.1 replacement build remains `manual=not_run`. `attribution=user_report`: before E4, the user
confirmed audible E3 steps, surfaces, ladders and eligible landings, with no
regression noticed in that manual check. No run ID or numerical measurements
are inferred from that report. Historical E3 reports remain unchanged.

## Reference and ownership

Pinned Valve SDK revision `b1b5cf5892918535619b2937bb927e46cb097ba1`:
`cl_dll/ev_hldm.cpp::EV_FireGlock1` fires the local muzzle flag, selects the
Glock shoot/empty animation and requests one `models/shell.mdl` brass entity.
`cl_dll/ev_common.cpp::EV_GetDefaultShellInfo` gives the eye-relative
`forward*20 + up*(-12) + right*4` origin and player-velocity-relative ejection
profile. `cl_dll/entity.cpp::HUD_StudioEvent` handles event 5001 at attachment
zero. Read-only inspection of installed `models/v_9mmhandgun.mdl` confirms
frame-zero event 5001, option `11`, on sequences 3 and 4, attachment zero on
the Glock model, and a silencer bodygroup. It does not establish that the
bodygroup itself is flash geometry. Installed `models/shell.mdl` and
`sprites/muzzleflash1.spr` are importable; the latter is additive but is not
currently drawn by this E4 profile.

| Input | HL1 module | Shared mechanism | Space/resource |
|---|---|---|---|
| Accepted B1 Glock primary-fire identity | Match the exact bound model and sequence 3/4 frame-zero Studio marker 5001; reject empty, reload, crowbar and stale actions | Typed `LocalMuzzleFlash` | Attachment zero evaluated at the marker frame in camera-local first-person coordinates |
| Same action identity, separate shell occurrence | Apply SDK origin/velocity profile; vary velocity deterministically from action identity | Bounded 120 Hz `TransientVisuals` point sweep and world Studio instance | World-space `models/shell.mdl` from approved server resource binding |

The Half-Life module owns eligibility and marker semantics. `GameClientHost`
forwards neutral visual output; the runtime owns the transient pool, Studio
pose/resource lookup, BSP point tracing, and renderer submissions. The
renderer contains no Glock/HL1 weapon rules. Audio has an independent outbox,
so consuming E2 sound does not consume the visual occurrence. World casing
motion never feeds authoritative entities, hit/damage, player collision or
canonical replay hashes. The flash is rendered after the first-person Studio
pass and before the HUD, using that pass's camera-local projection and depth.

## Explicit compatibility profile

The visible flash is a short, attachment-anchored additive quad, **not** a
stock-exact `R_MuzzleFlash` sprite implementation, MDL mesh, EF flag, or a
crosshair overlay. It is separate from the E4.1 transient light. Its radius is 2 source units and lifetime is
75 ms from the accepted action start; a late cue is dropped. It is evaluated
at Studio marker frame zero even if the current viewmodel pose advanced by
the time this render frame arrived. The viewmodel's current pose remains
unchanged. No old flash is replayed after a long frame gap.

A casing is a shared imported Studio mesh instanced in world coordinates. It
inherits the current player velocity once at ejection, adds a fixed forward
component and deterministic per-action side/up variation, then no longer
follows the camera. The visual-only simulation uses gravity 800 units/s²,
point BSP sweeps at 120 Hz, a damped bounce, rest below a small velocity
threshold, a 32-instance pool and a 2.5 s lifetime. Catch-up is bounded to
250 ms; an older unupdated casing expires instead of consuming unlimited
CPU. Moving-brush/platform rigid-body collision and platform carry are not
implemented. This is not bullet physics or a complete temp-entity system.

The approved `models/shell.mdl` is loaded with the normal resource projection,
not opened on each shot and not taken from arbitrary network text. If the
server does not expose/import that model, the optional shell is skipped and
the typed `visual_shell_resource_status` is reported; the game continues. No invented
server model slot or substitute cube is used. The original sprite is not a
dependency for this generated-quad compatibility profile.

One active fire action schedules one flash and one casing. Repeated sampling,
confirmation and prediction replay retain the exact action identity, so they
do not resubmit a second casing. The module retains at most 32 recent action
identities to suppress a nonconsecutive replay; the bounded pool independently
rejects an active exact duplicate. Weapon switch, death or life-epoch change cancels
pending/attached flash cues; already emitted world casings may finish their
lifetime. Map/session reset clears transient state. A correction cannot
retroactively erase a flash already seen. An uncorrelated server effect is
not treated as an ACK or as a second local fire action.

## Verification and handoff

E4 fixture tests cover the production G1 host/module path, capture-click
suppression, action identity, empty/crowbar exclusions, retained samples,
Studio marker metadata, deterministic casing motion, pool/lifetime, BSP wall
sweep, alternate-module neutral output and actual OpenGL framebuffer A/B
pixels for first-person flash and world Studio geometry. Installed Valve
assets are an opt-in read-only control, not a core-only build dependency.
Further suite/build outcomes and the Release hash belong to the task handoff;
offline tests do not assert live visual success.

No new managed/HLDS/Steam/WFP/capture/ETW session was launched for E4.
Manual E4.1 result remains `not_run`; the earlier partial E4 user report above
is preserved separately. The standard Fast launcher, reference/off,
Map, Strict, TestStartHealth and Scenario options are unchanged.

## E4.1 repair and evidence boundary

The first manual run's retained `live_application_outcome` reported 25 accepted
visual fire actions, 25 scheduled flashes, 25 created casings, but zero casing
render submissions and 2962 frame-level `visual_resource_missing` increments.
The approved local `models/shell.mdl` imported, posed and built as a Studio
render asset in a separate read-only control. The actual materializer supplied
zero previous/current interpolation state identities to
`EntityRenderFrameBuilder`, which rejects such frames. A project-owned
synthetic shell-resource test reproduced a ready resource and failed frame
before the fix, then passed after nonzero presentation-frame identities were
supplied. This was a materialization failure, not an established missing
resource, scale or PVS failure.

There was no transient-light request, scene field or shader application in
E4. Only the procedural flash quad was drawn. The HL1 module now requests
one bounded `LocalMuzzleLight` for the same accepted Glock action identity,
with a 75 ms lifetime, 96-unit radius and linear fade from intensity 0.8.
The host resolves the same marker-frame attachment as the flash. It provides
separate camera-local and world-space centers to the generic scene API; the
world center uses the current camera basis, while the viewmodel pass remains
in its own projection. Generic OpenGL world/lightmap, brush and Studio shaders
apply a distance-limited additive factor to the existing material lighting;
the flash billboard is unchanged. This is one local light without shadow
casting, not stock-exact dynamic-light parity or a general multi-light system.

The shell's ejection point remains the documented SDK eye-relative profile,
not an arbitrary Studio attachment. The casing remains a world-space Studio
instance outside server snapshot/PVS membership, with normal depth testing
and no screen-space overlay. The renderer does not claim a submitted instance
is visible in every live camera pose. Production diagnostics distinguish
`visual_shell_resource_status` (including missing/dependency/import states),
`visual_shell_frame_status`, frame rejections, visible submissions, culling,
creation and expiration. `visual_light_requested`, submitted and expired
counts are also bounded end-of-run diagnostics; they do not expose native
asset paths. If local assets are not yet prepared, status is
`pending_local_assets`. A missing/rejected shell does not suppress light,
flash or audio.

Automated E4.1 checks include exact-action duplicate/replay and one-shot
requests, short fade/expiry, a same-camera/time/geometry OpenGL wall A/B with
a distant control patch, first-person Studio light A/B without a flash quad,
positive approved-resource Studio frame materialization and world pixels,
negative missing/invalid resource cases, and an opt-in installed Valve
`shell.mdl` control that imports through the approved resource pipeline and
changes actual world framebuffer pixels. The installed file is read only;
the test's sandbox owns a temporary fixture copy. Offline tests are not a
new managed game session.
The next user manual run still must confirm visible light and ejected casings
in the actual map, and the exact launched binary SHA is not inferred from a
prior screenshot.

Final offline verification after the last production edit: Release
`hlclient_tests.exe` 2394 cases (2364 passed, 30 capability/opt-in skipped),
819046 assertions passed; Debug `[weapon-visuals]` 14 cases (12 passed,
2 opt-in skips), 269 assertions passed. Separate read-only installed Valve
asset controls passed 40 assertions in two cases, including actual shell
framebuffer pixels. Core-only `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` libraries and
API tests built; both `hlclient_core_api_offline` and
`hlclient_game_boundary_guard` passed. The no-stock PowerShell launcher
fixture and read-only `-CheckOnly` passed. ASan Debug test binary built, but
the executable still exits 1 without output even for `--help`, so ASan
runtime is **not** verified. `git diff --check` passed. The unchanged
launcher selects `build/bin/Release/hlclient.exe`; its E4.1 SHA-256 is
`285C6DF106BD17EE04EF4A098766C941CB426534911FF33A0CDCB7D2C4A57984`.
No new live managed/HLDS/Steam/WFP session, commit or push was performed.

After that offline handoff the user reported «Тепер все працює».
`attribution=user_report`, scope: the previously reported missing Glock shot
lighting and invisible shell casings in the E4.1 replacement were resolved in
the user's manual experience. This is not an independently measured process
SHA, a step-by-step negative/stress protocol, or validation of the later E5
build. The historical first E4 partial report and the above E4.1 offline
`manual=not_run` entry remain unchanged.

The later E5.1 user report about decal rectangles, impact audio and shell
contact audio is recorded in `LOCAL_GLOCK_WORLD_IMPACTS_E5.md`; it does not
relabel this E4.1 light/visible-casing confirmation as an audio confirmation.
