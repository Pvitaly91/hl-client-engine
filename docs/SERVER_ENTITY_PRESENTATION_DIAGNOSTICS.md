# Server entity presentation — 2026-10-18

The reported session is `ef69995bca53b35fa9967fb605ca2432`, on Crossfire.
It closed normally. F8 was recorded at application time 100.096765 seconds.
The local incident retains 96.303722–101.082667 seconds; remote actor evidence
retains 99.190558–100.096765 seconds around this marker. The marker was pressed
after the reported encounter, so it does not identify the lift passenger.

The old remote record includes source/sample/rendered positions and animation
selections, but lacks render mode, amount and supporting brush identity. Its
positions alone cannot establish whether the observed player was on a lift,
jumping, or deliberately transparent under server rules. The screenshot also
cannot establish the Tentacle animation's timing.

## Confirmed implementation gaps

Ordinary Studio models previously used the quantized network frame directly.
They did not advance animation using `animtime`, signed `framerate` and the
imported sequence FPS. Tentacle plays several transitions backward, which also
exposes the limitations of simply interpolating increasing network frames.

Remote players use a delayed presentation clock. Runtime brush rendering uses
the local movement support clock, including bounded lift extrapolation. An
ascending platform can therefore render ahead of its correctly delayed rider.

The local materializer rejected every nonzero Studio render mode. A server
could legitimately make a player translucent, but the model was omitted rather
than rendered with its advertised amount. The old diagnostics cannot prove
this was the exact reason for this encounter.

## Generic corrections

`StudioEntityAnimationPresentation` samples raw authoritative animation anchors
around the delayed render clock. It implements signed forward/reverse playback,
loop wrapping, nonlooping endpoints, controller interpolation and the Studio
200 ms sequence transition. Missing timing fields retain discrete sampling.
Model, generation, removal, discontinuity and no-interpolation boundaries reset
transition history. The same helper is used for ordinary Studio models without
matching any model name or server address. Player animation keeps its existing
dedicated composition path.

Remote rider correction requires two raw standing/duck hull endpoints in exact
contact with the same vertical `MOVETYPE_PUSH` brush. Brush identity, model,
library, transform continuity, timestamps, speed and relative foot height must
remain valid through the retained history. The visible rider receives only the
vertical difference between the delayed support and the exact final rendered
support. Position is adjusted before posed bounds, lighting and weapon/socket
composition. A complete hull sweep against the world and other brushes rejects
blocked paths, including finite thin ceilings. Jumping, falling, death, support
loss, rotation and missing evidence do not acquire this correction. The raw
snapshot, collision state and input commands are not changed.

The main loop samples the final prediction brush scene once and passes that
same owning scene to rider presentation and brush rendering. Historical brush
contexts use that scene's approved model library and are cached by publication,
so the 16-observation history is not rebuilt on every render frame. Broadphase
rejection avoids support traces for ordinary players away from pushers.

Studio rendering now has neutral per-instance alpha/additive mode, opacity and
tint. Standard server mode/amount/color fields reach this instance; attached
weapons inherit the owner's values. Blended Studio and sprite instances render
back to front after opaque geometry, retain depth testing and do not write
depth. Zero amount remains invisible for blended modes; normal opaque models
retain their previous rendering. The draw path restores blend/depth state.

F8 actor evidence now records raw `render_mode`, `render_amount`, `render_effect`,
effective rendered opacity, and the proven support entity/model/vertical offset.
Render-state and support identity changes retain exact-frame events. These
observations remain bounded and do not grant visibility or gameplay authority.

Reference behavior comes from the pinned Valve SDK
`cl_dll/StudioModelRenderer.cpp` (`StudioEstimateFrame`, `StudioSetupBones`),
`dlls/tentacle.cpp`, `network/delta.lst`, and the Xash OpenGL Studio renderer:
<https://github.com/FWGS/xash3d-fwgs/blob/master/ref/gl/gl_studio.c>.

## Verification

Release client and `hlclient_server_entity_presentation_tests` build successfully.
The 24 targeted cases pass 3,731 assertions. The full focused executable passes
all 200 cases / 283,131 assertions with the already-approved installed Half-Life
root enabled for the test process; no cases are skipped.

The real `tentacle2.mdl` control compares every rendered bone matrix against an
independent SDK frame formula at three presentation times between two fixed
network snapshots, for both positive and negative playback. Installed source
timestamps and retained observations remain unchanged. Lift cases include
ascending/descending motion, stops, reversal, duck hulls, jumping/falling/death,
support/model/library changes, missing data and finite thin-ceiling obstruction.
Actual OpenGL controls verify alpha/additive contributions, amount zero, tint,
opaque depth occlusion, back-to-front order and state restoration.

The movement analyzer passes 262 synthetic checks, including both new event
bits, optional fields, measured zero, signed support offsets, malformed values
and compatibility with older reports. It also successfully reads the user's
original session above. A two-frame null-renderer startup exits 0.

A bounded 20-second OpenGL connection to the reported customized server exits
0 on Crossfire: 1,455 successful presentations, nonzero world/Studio/sprite draws,
`assets_installed=1`, `renderer_resources_presented=1`, `unsupported=0`,
`primary_error=none`, `gl_errors=0`. Timed disconnect is server-confirmed.
This smoke control establishes map entry and rendering; it does not establish
that the reported moving rider was observed again or manually exercise combat.

Evidence lives in `manual-artifacts/task-records/custom-server-signon-20261018`:
`test-server-entity-focused.log`, `test-server-entity-full.log`,
`test-server-entity-analyzer.log`, `last-user-session-entity-analysis.json`,
`smoke-server-entity-verified.log`, and
`live-rendered-server-entities-verified.log`. Its receipt directory is
`rendered-entities/7e52f7c47490483185cbd0e4f0353ab8`.

Release `build/bin/Release/hlclient.exe` SHA-256:
`61B4BA12326B29031651436DB0BA4685DC0BBC68471DD033FA70859F3FD7EC73`.
The launcher executable remains unchanged and uses this client. No server
settings, launch configuration or file-check permissions are changed.
