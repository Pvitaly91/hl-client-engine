# HLC-E32 — LAN movement, prediction timeline and vertical support

## Contact and stale-seed recovery follow-up — 2026-10-15

The user requested repair after audit of run
`9cd4fd47545ff90c567c97f7632b245e`, bound to the previous client
`12E8729E31E551EEB593123B1B420BCB536E95734E830D53FBE8FE9A81A365A7`.
The 160.22-second run closed normally. Controlled/alive data contains four
`player_allsolid` entries (one around 19.3 seconds, three around 59.6-59.9),
one 16.1658-unit unclassified correction with 159 inactive frames around
90-92 seconds, and one correction-horizon expiry with 43 inactive frames
around 144-146 seconds. No F8 markers were recorded. The successful previous
velocity-camera bridge remains observed, including an 8.51-unit correction
with zero measured same-time camera jump around 34.19 seconds.

### Reproduced causes and bounded production changes

1. The movement kernel accepted a generated upward contact endpoint directly
   as a speculative horizontal step start. BSP equality/float rounding can put
   that endpoint in the solid ceiling child, making the speculative route
   return allsolid and abort otherwise valid direct wall movement. An owned
   floor/wall/low-ceiling fixture at the reported 59-second coordinate range
   reproduced this on its first command. This establishes the defect, **not**
   the exact missing trace/geometry of all four user incidents. The endpoint
   now requires complete clearance; only one outward binary32 neighbour with
   a clear hull path is eligible. A blocked candidate is discarded, not a real
   collider. Invalid provider data still fails. Existing scratch now retains
   actual failed initial/ground/slide queries so future incidents have evidence.
2. The live owner threw away a successful physical rebase solely because its
   raw error exceeded the 16-unit visual threshold. Losing exact command slots
   could then prevent held jump/duck reseeding. Valid large unclassified
   corrections now retain corrected history and suffix with an explicit hard
   camera snap. No old residual/predecessor bridge is retained or velocity
   cause fabricated. True seed/replay/collision/life failures still reject.
   Ten former visual-negative controls now assert stricter authoritative snap
   semantics, including blocked old-to-new paths, setangle and EF_NOINTERP;
   the denied impulse-smoothing policy itself is not relaxed.
3. Delayed server records could each replay an expired suffix before normal
   command submission checked the 250 ms horizon. A four-record owned backlog
   test reproduced 74 replayed commands / 1,480 ms of simulated command time
   in one update. Preflight now verifies the exact immutable boundary/suffix,
   actual sample clocks and 64-command bound before ground queries, simulation
   or audio rewind. Only recovery from inactive/missing/stale authority applies
   the 250 ms boundary-age and summed-suffix limit. Fresh continuous authority
   may repeat an old transport ACK without becoming stale; the existing
   64-command rebase contract remains intact. Canonical/protocol/game records
   still commit in order; a fresh valid carrier can recover immediately.

The broader regression suite caught an initially overbroad age check against
a still-active stream with fresh server time and repeated ACK. The guard was
narrowed to recovery, preserving that old test unchanged. New independent
inactive fixtures test the inclusive 250 ms and rejected 251 ms boundaries.

The session's maximum 58.60 ms frame included 26.40 ms prediction CPU and
2,600 ms **summed simulated** replay time, not 2.6 seconds CPU. The affected
second contains 27 correction advances across 17 observed frame samples and
220 replayed commands, demonstrating repeated work. Missing detail prevents
assigning each command to one exact delayed suffix. The separate 52.20 ms
frame around 117 seconds included Studio CPU work and is not claimed fixed
by this prediction change. Per-seed limits are not an aggregate frame-time cap
or a guarantee of constant FPS. Network-gap recovery still does not invent
missing jump/duck edges, and genuine unclassified teleports/corrections can snap.

Preservation and verification evidence lives in
`manual-artifacts/task-records/e32-contact-stale-recovery-20261015`: all 1,418
pre-edit sources, old Release executables and the original user run were saved
and hash checked. Production changes are confined to the existing movement
kernel, its scratch-field comment, and `live_runtime_stage.cpp`. No server,
network grammar, input sampling, remote animation, assets or weapon changes.
New build manual status is `not_run`; no agent live sessions, staging, commit
or push. Endpoint remains `192.168.0.104:27016`.

### Post-edit acceptance

VS2022 / v143 / Win32 `ALL_BUILD` succeeded for Debug, Release and the existing
`HLCLIENT_BUILD_GAME_HALFLIFE=OFF` core-only tree (evidence label `06-final`).
The clean pre-fix run (`03-red`) failed on all three reproduced defects: the
first low-ceiling speculative step, discarded held-jump history, and 74 stale
replayed commands / 1,480 ms summed simulation. The physical occupied-seed
negative control already passed. No old collision assertion was relaxed.

| Final gate | Result |
| --- | --- |
| Release movement/prediction/live-runtime/E32/E321/E21 | 380 passed, 1 optional Gauss asset skip; 1,001,429 assertions |
| Debug same selection | 380 passed, 1 optional Gauss asset skip; 1,001,441 assertions |
| Core-only Release CTest | 44/44 passed, including architecture guard; 27.80 s |
| Full serial Release CTest after last production edit | 3,633 passed, 66 explicit skips, 0 failed / 3,699 registered; 595.41 s |
| Full Release wrapper / exit | `scripts/build_source.ps1 -TestOnly -Parallel 1`: passed / 0 |
| Normal binary and launcher offline controls | Version/help, Null renderer, owned loopback transport, functional-log observation, no-stock fixtures, `-CheckOnly -SourceOnly`: passed |

Owned fixture coverage includes standing/ducked movement with both BSP plane
orientations, real wall/floor/ceiling embedding, six provider-failure controls,
fresh failure diagnostics, held-jump correction, delayed multi-record backlog,
fresh repeated ACKs, independent 250/251 ms inactive recovery, and occupied
authoritative seed rejection. Existing server-velocity camera tests retain
at most 9.54e-7 unit same-time eye displacement and no added fallback in their
eligible cases. Offline timing samples are not a live FPS guarantee.

The 66 full-suite skips include explicit installed-asset/audio-device opt-ins,
disabled WFP live-capability tests, and unavailable OS link/capture/provider
capabilities. They are not counted as passed. The serial full-suite log lists
every skipped test. No installed asset/device/live validation was introduced.
`final-audit.json` records source preservation, Git/staging/SDK boundaries,
unchanged launcher image/settings and exact executable identities.
ASan runtime remains historically unconfirmed and was not retested.
New Release client SHA-256:
`7A4F2D6118A8A6908C957C58866A29C9C51A106E4F90EB1D501F4906863A4204`.
Use the normal `build/bin/Release/HLClientLauncher.exe`; no extra feature flag
or server change is needed. New client manual status remains `not_run` until
the user plays it. For comparison, keep movement/FPS diagnostics enabled and
press F8 at any remaining jerk; close normally to finish the session receipt.

## Server correction camera follow-up — 2026-10-15

`attribution=user_report`: the user noticed jerks while running with someone
shooting from behind, then authorized repairing the abrupt camera transition
found in the latest run `89cea1c830b4944b3ac1486ee496eb8d`. Its receipt binds
client `BDBB789B6E32FF207F716DC86CB3542F42A8EE0AB336C32C5BDFF69382A71BF8`;
the run closed normally. The log does not retain sufficient damage/source
details to establish that a rear hit caused either correction.

Two living fallback entries remain in 8,977 controlled/alive frames. At
23.445288 seconds the raw correction is 18.035925 units and the rendered eye
moves 17.716107 units within an 11.8633 ms frame. The matched/server velocity
difference is approximately (-118.404, 368.506, 149.375), aligned with position
delta (-5.077, 15.979, 6.648). Prediction resumes about 62.51 ms later. A second
retained aggregate reports 18.566169 units at 71.5-72.5 seconds, without the
first incident's complete detail. Death/new-life windows are separate.
The controlled intervals average about 128.95 FPS; maximum frame is 36.4149 ms
and none exceed 50 ms. The fallback windows do not show a major frame stall.

### Cause and implementation

`accept_reference_prediction_seed` successfully rebased the acknowledged
history, then discarded it solely because raw error exceeded 16 units.
Suspension cleared the visual residual, and normal camera composition selected
the canonical origin immediately. This explains the measured camera jump;
it does not prove a server gameplay or hit-direction defect.

The normal live owner now accepts a narrow velocity-backed correction through
32 units, subject to fresh authority, coherent pose/life and clear full-hull
and eye paths. Unclassified corrections retain the existing 16-unit gate.
Simulation accepts server origin/velocity and replays immutable commands
immediately. The existing camera-only residual converges over 100 ms, with a
32-unit total bound. Further accepted ACKs preserve the original deadline and
cannot double the previous residual. Pose/context are rechecked between ACKs;
duck, ladder/liquid, new setangle, EF_NOINTERP, death/reset and obstruction
retain conservative cancellation. The small exterior-corner route is not used.
No damage, ammo, input, server, assets or remote-player interpolation changes.
The exact thresholds and limitations are documented in
`PREDICTION_VISUAL_CORRECTION.md`; this is local-compatible, not stock-exact.

The existing preservation mechanism saved all 1,418 source files and the prior
Release executables before editing. Evidence and the original user run are in
`manual-artifacts/task-records/e32-server-correction-camera-20261015`.
Only `src/goldsrc/live_runtime_stage.cpp` changes production behavior; tests
exercise its normal module/transport/ACK composition using project-owned data.
New-image manual status remains `not_run`; agent live sessions, staging,
commit and push are zero. Endpoint stays `192.168.0.104:27016`.

### Regression evidence

The initial new regression failed the preserved production code: an eligible
6.5-unit correction produced a 6.825148-unit same-time camera jump, while the
18-unit correction suspended with `large_unclassified_server_correction`.
After the final production edit the seven new Release cases pass all 3,755
assertions. Across diagnostics on/off, zero/two replay suffix commands and
6.5/18/18.625-unit corrections, same-time camera displacement is at most
0.000001 units, prediction stays active and canonical authority changes
immediately. Independent time samples converge by 101 ms; duplicate reads
do not add traces, commands or weapon notifications.

The reported three-axis position/velocity delta is separately encoded onto a
project-owned floor fixture, with an independent grounded command oracle and
no jump button. It passes the grounded-to-airborne bridge. This is reproduction
of the observed mechanism, not replay of a complete captured game session.
Ordinary one-unit refinements and repeated impulses retain the first deadline.
Negative controls cover no/low/opposite/sideways velocity, excess distance,
stale authority, perpendicular mismatch, a finite convex obstruction with both
endpoints free, new setangle and EF_NOINTERP. Between-ACK duck and control-only
setangle cancel the residual; an unchanged persisted spawn setangle does not.
Death and new-life controls reject the previous camera/command lifetime.

The final core-only Release build (`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`,
VS2022/v143 Win32) passes all 44 CTest registrations, including the architecture
boundary guard and no-stock movement-log analyzer (35.35 seconds).

VS2022/v143 Win32 Debug and Release `ALL_BUILD` pass. The Debug neighboring
suite (`[e32],[e321],[prediction],[live-runtime],[e21]`) passes 256 cases /
856,519 assertions; one opt-in installed Gauss-resource case is skipped.
No old assertions were relaxed. ASan runtime remains historically unconfirmed
and was not retested. The normal Release client SHA-256 is
`12E8729E31E551EEB593123B1B420BCB536E95734E830D53FBE8FE9A81A365A7`.
The launcher image, saved settings, branch/HEAD, empty staged set and pinned
SDK are unchanged; preservation audit finds only the production source,
its regression-test file and these three architecture/task documents changed.

After the last production edit, the full serial Release offline suite passes:
3,627 passed, 66 explicitly gated/capability skips, zero failures across 3,693
CTest registrations (613.45 seconds). The same `build_source.ps1 -TestOnly`
run passes version/help, Null bootstrap, transport/functional-log controls,
G/H1 summaries, no-stock H2 launcher fixtures and `-CheckOnly -SourceOnly`.
The latter verifies source/argument wiring, not external game/Steam/server
readiness. Optional installed assets, device and host capability skips are
not positive live evidence. See `release-offline-suite-05-final.log` in the
task record. No retry or weakened assertion was required for this final suite.

Manual confirmation uses the same launcher/server with movement diagnostics:
run while receiving fire, also jump/duck and use the lift, then press F8 at any
remaining camera jerk and close normally. This image has not had a new user
gameplay confirmation; offline results do not prove elimination of every jerk.

## FPS and lift-exit contact follow-up — 2026-10-15

`attribution=user_report`: the user noticed approximately 77 FPS and requested
repair of the frame-time and prediction issues found in run
`4614a73163f1bf262dc41866b8dea0d1`. The receipt binds client
`E38D6CFCAA7C073750F2149B08640F7A9FC9EAEFFB4C90CBA53D56F539AF1D5F`.
The 305.18-second run closed normally; no F8 markers were recorded. Its 37,309
controlled-alive measured intervals average 7.855 ms (127.3 FPS-equivalent),
with fully controlled one-second windows around 70-80 FPS. There are 26 frames
over 33.3 ms, maximum 47.0392 ms and none over 50 ms. The slowest retained
frame has CPU update 40.4645 ms, parse/commit 13.5833 ms, Studio 6.8819 ms and
prediction 6.1582 ms. These inclusive timings must not be summed. The slowest
one-second windows retain active prediction; a 77 FPS display is not itself
evidence of a collision/reconciliation failure.

Seven counted fallback entries and 58 controlled-alive inactive frames remain.
The detailed windows at 22.347238 and 91.384853 seconds show world-grounded
replay at the lift landing, with an upward-derived brush 67 overlapping the
final hull. Each fails the final support-clearance occupancy check, then
recovers after approximately 41.5/41.8 ms. New-life waiting contributes inactive
frames but is not one of the seven counted entries. Detail retention does not
reconstruct every later fallback; no claim is made that all seven share a cause.

### Production corrections and bounds

`simulate_reference_movement` previously carried only the kernel-selected
ground identity. A world floor wins a coplanar landing tie, so an arriving
rising platform was treated as an unexplained final overlap instead of a new
support contact. The new narrow `rising_support_contact` path requires a live
grounded walking hull with zero vertical velocity, a free start-scene endpoint,
an exact newly blocking brush identity with positive derived motion, and a
walkable top-plane witness. Its displacement cannot exceed that command's
derived rise (at most 12.8 units, plus one outward binary32 neighbour for
rounding). A full world/other-brush/entity-box sweep and complete end-scene
occupancy must both be clear. Side/deep/descending contact, real ceiling or
actor blockage and a genuinely detached landing retain the conservative result.
No command, own velocity, canonical server origin or server activation changes.
Unobserved future stops remain unknowable; a newly observed stopped authority
removes the derived carry through the existing rebase. This is a local-compatible
contact correction, not exact server pusher physics.

Two independent CPU paths contained provably redundant work. Buffered player
and ordinary-entity sampling called full interpolation only to validate input,
discarding complete owning output copies and repeated canonical hashes. They
now call the same extracted input validator directly before real sampling.
Same-object previous/current validation also runs once. The real sample retains
the same formulas, clocks, result ownership, finite values and resource limits.
No Studio animation rate, geometry, effects, interpolation delay or quality is
reduced. The per-frame presentation benchmark uses 16 players and 64 entities.

`RuntimeReplaySession::apply_record` copied all prior record identities and
then grew the copy on every commit. Its configured bounded capacity is now
reserved before generation publication; the already checked trivial append
cannot allocate at commit. Duplicate/conflicting/old-record checks and reset
atomicity remain. Decoder history publication also removes two const qualifiers
that made an existing `std::move` copy instead. These optimizations establish
less CPU work; the original log alone does not identify an exclusive cause for
every FPS drop, and offline timings are not a new gameplay FPS measurement.

The existing preservation mechanism saved all 1418 current source files,
preceding Release executables, launcher settings and the exact run evidence in
`manual-artifacts/task-records/e32-fps-lift-exit-20261015`. Branch/HEAD and the
pinned SDK remain unchanged. New-image manual status is `not_run` until a user
plays it; agent live sessions, staging, commit and push are zero.

### Follow-up verification

The new lift test first failed the old production implementation at the expected
positive handoff assertion (standing hull, 92 units/s, 20 ms); its negative
controls passed. With the fix, the five new regression cases pass 42,488
assertions. They cover standing/ducked arrival, deterministic repeat, stopped
authority rebase, blocked/deep/detached/descending controls, validation
rejections without consuming presentation state, and bounded record retention
through reset and exact capacity exhaustion.

On the same project-owned Release fixture, 1,200 combined player/ordinary-entity
samples averaged 355.776 microseconds before and 276.796 / 238.846 microseconds
in two separate post-change runs (22.2-32.9% lower sampling time). These samples
contain 16 players and 64 entities. The separate 8,192-record fixture totals
1131.594 ms before and 906.409 / 971.979 ms afterwards. Timings are descriptive,
host-dependent and have no pass/fail speed threshold. They measure these paths,
not an equivalent whole-frame FPS increase or a new live-session result.

VS2022/v143 Win32 Debug and Release `ALL_BUILD` pass. The targeted neighboring
suite passes identically in Debug and Release: 387 passed, one optional
installed-resource case skipped, 945,369 assertions. The core-only Release
build with `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` passes all 44 CTest registrations,
including 407 core API cases / 404,609 assertions, the no-stock movement-log
analyzer and architecture boundary guard. Evidence is under the task record
above; no old E32 acceptance count is substituted for these runs.

After the final production edit, the complete serial Release offline suite
passes: 3,617 passed, 66 explicitly gated/capability skips, zero failures out of
3,683 CTest registrations (747.40 seconds). Skips cover optional installed
assets/device playback and Windows capability/input controls; they are not
positive gameplay evidence. The same `build_source.ps1 -TestOnly` run then
passes version/help, Null bootstrap, transport and functional-log controls,
G/H1 summaries, no-stock H2 launcher fixtures and `-CheckOnly -SourceOnly`.
The latter checks source/argument wiring, not live resource/server readiness.
`release-offline-suite-final.log` records successful completion; its enclosing
runner returns `FULL_RELEASE_EXIT=0`. ASan runtime remains historically
unconfirmed and was not retested.

Normal Release output remains `build/bin/Release/hlclient.exe`, SHA-256
`C2CC636BCEB7B7C99E395ADD2BB86B315C67591F879614056DED45D88F67B05C`.
The launcher image remains
`6BBF0102FF183B70BE3C55FEBEA0820F85829FEA77BA886B1826193042B98FF0`;
its existing settings and endpoint `192.168.0.104:27016` are unchanged. No
hidden runtime flag or asset/server change is required for these fixes.
For manual confirmation, use the existing launcher with movement diagnostics,
walk/run on and off the shotgun lift, and play under the usual server load.
F8 can identify any remaining stutter. New-image `manual=not_run`; the offline
results do not establish removal of every FPS dip or every fallback in the old
bounded log.

## Long-session, lift presentation and crouch-jump follow-up — 2026-10-14

`attribution=user_report` for run `105ed70dac274e3db1f7bc6f526aea3b`:
the previous prediction jerks and complete shotgun-lift entrapment did not
recur. The user separately reports uneven lift ascent, long-jump jerks, RPG
laser and Egon contact sprites visible through walls. This is limited manual
feedback for client `43DA685E21B72B25D9BDD19B503E5569FF24D29F32CDD39D2E3BFF1E0267E6DF`,
whose identity is bound by the run receipt; it is not blanket acceptance.

The same run closed at 340088 ms with exit 2, `usercmd_transmission_failed`,
16385 transmitted packets and no nested driver failure. The normal live owner
had a diagnostic vector capped at 16384; it treated the next valid send receipt
as fatal. This matches the exact recorded boundary. Normal keyboard sessions
now retain bounded chronological diagnostic windows with O(1) eviction for
both transmitted ranges and server samples. Lifetime totals and first/last
submitted command identities remain separate; prediction-carrier ownership
does not depend on this diagnostic window. Scripted evidence checks keep their
strict complete-history bounds. Typed metadata/retention failures now reach
durable external diagnostics. New tests exercise small capacities 1/3 across
multiple wraps, valid ACK/replay after eviction, duplicate updates and malformed
metadata, plus strict scripted exhaustion.

The lift window around 21.73–22.31 s shows active prediction with repeated
`anchor_before_history` presentation. In particular, source 414 acknowledged the
latest command 659, had approximately 3.88 units vertical correction on support 67,
and dropped the render predecessor because the old gate allowed only 0.25 units.
Other records also show a real change in observed support direction near 22.27 s;
the client's presentation repair must not conceal a physical reversal. The
scope is retaining a verified same-support predecessor for a bounded vertical
correction, with unchanged hull, contact identity, plane and collision checks.
The additional path requires consecutive boundaries, a walking grounded player,
no duck transition, at most 0.25 units horizontal discrepancy, a 250 ms interval
and at most 12.8 units contact displacement. The existing overall 16-unit
correction bound remains. Both contact deltas must agree with the current
support direction; actual direction reversal rejects the old pair. The ordinary
support sampler verifies the start, midpoint and endpoint against the full
collision scene. Three bounded support samples are added only for this eligible
ACK path, not every frame; normal small corrections retain their old fast path.

Long-jump support was missing from local prediction: decoded server `physinfo`
did not reach the movement state, so a crouch jump always used the ordinary
jump trajectory. The neutral observation now retains the bounded server value;
the HL movement policy selects its exact `slj` key and pinned-SDK boost profile.
Only coherent server capability enables the immutable predicted-state flag.
The ordinary kernel requires the existing reference-input path, a ground jump,
duck transition/hull and button, a live duck timer and sufficient speed.
No item grant, server state, wire grammar, ammo or damage is changed.
The boost follows the pinned public jump equations and eligibility, but the
existing reference movement profile does not reproduce the complete
PM_CheckParameters punch-angle path. Nonzero punch-angle steering therefore
remains a compatibility limitation; this is not a stock-exact movement claim.
The retained incident windows do not identify the user's exact long-jump event;
the missing production path is established by source and regression evidence.

The RPG/Egon visibility defect is a separate shared glow-renderer correction,
recorded in the existing E20/E22 documents. World depth testing now applies to
those additive sprites. New tests use actual OpenGL and project-owned sprites
through ordinary resource materialization, including hidden and visible controls.

Pre-edit 1414 source files, preceding executable identities and the exact run
were preserved in `manual-artifacts/task-records/e32-long-session-jump-occlusion-20261014`.
New image `manual=not_run`; agent live sessions: 0.

### Verified long-session/jump/occlusion build — 2026-10-14

VS2022/v143 Win32 Debug and normal Release ALL_BUILD succeed. Final focused
Debug and Release each execute 425 cases: 419 pass, 6 explicit optional local
asset checks skip, no failures. Debug passes 554718 assertions; Release passes
554726. The new glow checks execute an actual OpenGL context using project-owned
resources, not a live-game screenshot. Lift checks cover positive/negative ACK
corrections and a real direction reversal; crouch-jump checks cover server
enable/disable, exact eligibility, canonical ACK/rebase and no duplicate boost.

Separate `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` Release ALL_BUILD succeeds. Its
core API suite passes 403 cases / 379088 assertions; all 44 CTest registrations
pass in 35.76 s, including the architecture guard. Normal Release architecture
guard also passes.

The first complete normal Release run executes 3659 registrations: 3592 pass,
66 capability/asset/device/private-corpus checks skip and 1 existing process
test fails (250.30 s). In `Suspended launch keeps identity mismatch and query
failure distinct`, only the immediate Job active-process count fails (1 vs 0);
17/18 assertions, including the final bounded cleanup, pass. This test and its
process implementation match the preserved snapshot. The implementation waits
for the process handle, whereas Job accounting can settle later; a neighbouring
existing test already documents that distinction. All five isolated repeats
pass 18/18 assertions. Neither assertions nor timeouts were relaxed.

The complete final repeat, without a concurrent build, passes: **3593 passed,
66 explicit skips, 0 failures out of 3659**, 164.04 s. The unchanged E28.2 normal
close reporting test passes in both full runs (130.09 s / 126.28 s); its historical
timeout remains recorded below, not erased. The full build_source.ps1 tail also
passes: version/help, Null startup, synthetic transport, functional-log checks,
G/H1 summaries, no-stock H2 fixtures and launcher `-CheckOnly -SourceOnly`.
Both full logs and all isolated repeats are retained in this task record. These
offline run timings are not gameplay FPS measurements. Historical ASan runtime
remains unconfirmed and was not retested.

The unchanged manual launcher selects
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
The GUI launcher resolves the same-directory `hlclient.exe`; its source and
saved settings are unchanged, and normal use needs no hidden flag.
New client SHA-256:
`8D71AEDE3787A487516AE4634B30103ECD00501850BDD9390E0230AC496ED982`.
Rebuilt launcher SHA-256:
`6BBF0102FF183B70BE3C55FEBEA0820F85829FEA77BA886B1826193042B98FF0`.

All 1414 preserved source copies verify. This follow-up changes 32 existing
source/test/doc files, adds/removes no source files, and preserves branch/HEAD,
staging, pinned SDK and launcher settings. `git diff --check` passes. Exact
inventory and identities are in `final-audit.json` within this task record.
No server, installed asset, other worktree or MDL-Viewer change; no live session,
staging, commit or push.

Manual follow-up on the unchanged `.104:27016` target: ride the lift through
ascent/start/stop; use a server-granted long-jump module for several crouch jumps;
check RPG/Egon contact sprites from opposite sides of a wall; keep the session
running past the previous roughly six-minute closure boundary. Mark any new
jerk with F8 and close normally. New executable manual status remains `not_run`;
the accepted prior no-entrapment/no-general-jerk report is not promoted to it.

## Shotgun-room lift seed recovery follow-up — 2026-10-14

New `attribution=user_report`: remote-player jumps were not noticed in the new
manual test; this is a limited observation, not blanket remote/stress acceptance.
The shotgun-room lift stopped and the user could not leave it; only mouse look
worked. Gauss last-ammo secondary discharge also jerked in first person; its
separate correction is recorded in the existing E21 document.

The two closed runs1157bc0fd06bc17b33faa1c46f73d6d2 and
22496943166d1ba1975667e3e49f8ef1 bind the preceding6661897B... image and both
exit0 normally. The second run enters continuous `solid_start` around156.54s
until close173.25s. Its last473 frames retain server origin
(-827.359375,1311.96875,-1660.0078125), brush blocker67 and inactive prediction;
forward/jump commands still exist, maximum final-window frame15.249ms. No F8
marker or retained detailed onset window identifies the exact first blockage.
The earlier run binds support67/model35/BSP submodel34; approved read-only
Crossfire inspection identifies this as lift4, not its button(submodel35).

Source cause of the unrecoverable pattern: the old BSP-boundary recovery only
tries26 adjacent binary32 values. At these coordinates one vertical ULP is
about0.000122, insufficient for a quantization-scale overlap. The recorded feet
would be0.0078125 below lift4's initial top, but the exact live brush transform
was not recorded: that depth is inferred, not proof of the server's stuck cause.
Server origin also froze; this client repair is not server door activation or a
server-side unstick. Fresh manual verification of the complete symptom remains
necessary and is `not_run` for the new image.

`derive_reference_prediction_ground` now adds a seed-only grounded brush-top
recovery after unchanged ULP clearance, retaining the existing box fallback.
It requires an exact brush identity,
existing upward-velocity limit, free full-scene position at most1/8unit above
the observed seed, entry into the same brush, a finite walkable hull plane,
its geometrically solved clear position and matching downward support. No
horizontal movement, canonical-origin edit, kernel epsilon widening, noclip,
server/config/asset change or camera-only workaround. Deeper, airborne, world,
wall, ceiling, overlapping and invalid-witness cases stay rejected. The bound
is a documented local-compatible profile, not a claimed stock constant.

Project-owned neutral fixtures cover48 standing/ducked grid penetrations,
large negative coordinates, repeated reseeding,64 escape commands and jump,
and rising/falling/stopped support carry once per command. Old ULP failure is
an explicit negative control. Acceptance results below were recorded after
the final production edit and full Release offline suite.
Source/Release/configuration/user-run hashes were preserved before edits in
`manual-artifacts/task-records/e32-lift-gauss-recovery-20261014`.
No new agent live/stock/HLDS/LAN session; staging/commit/push=0.

### Final follow-up verification — 2026-10-14

VS2022/v143 Win32 Debug and normal Release targets build with existing `/WX`.
Final targeted Debug and Release each run104 cases:103 pass,1 optional approved
asset skip,146745 assertions pass. The11 neutral mechanism cases pass23553
assertions in Debug. Separate `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` Release builds
and passes400 core API cases/378904 assertions; its44 CTest entries, including
architecture guard, pass(38.68s). Normal Release architecture guard also passes.
Existing assertions were not relaxed.

The final complete normal Release CTest run is NOT fully green:3649 registered,
3582 passed,66 capability/asset/device/private-corpus skips,1 timeout; elapsed
1978.90s. The only failure is unchanged `hlclient_normal_close_verdict_e282_no_stock`,
which exceeds its existing180s limit while completed child scenarios have no
assertion failure. An isolated unchanged-limit retry also times out180.05s.
The reporting test, its assertions and time limit were not edited to hide this.
This is a validation limitation, not evidence of a Gauss/lift assertion failure;
the underlying reporting-test slowdown is not established by these results.
Both logs are retained in the existing task record. Release CLI/launcher tail
verification is recorded separately because `build_source.ps1` stops on that
CTest failure. The separate tail passes: version/help, Null startup, synthetic
transport, functional-log observations, G/H1 summaries, no-stock H2 manual
launcher fixtures and unchanged launcher `-CheckOnly -SourceOnly`.

New normal-stage regressions use a project-owned neutral module through actual
GameClientHost/LiveRuntimeStage/decoder/cache/anchor/rebase/kernel paths. They
prove the quantized lift boundary stays active with a single replayed suffix,
and canonical upward postZ400/1000 plus fresh repeated ACKs neither repeat the
module action nor change same-time camera/origin/velocity. The next command
becomes airborne. Actual HL Gauss eligibility, last-ammo1/2/3 actions, phase,
audio/beam dedup and delayed timer/phase controls are separately tested in E21;
the neutral live stub is not called literal stock-HL end-to-end play.

Normal launcher `-CheckOnly -SourceOnly` selects exactly
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
Client SHA-256:
`43DA685E21B72B25D9BDD19B503E5569FF24D29F32CDD39D2E3BFF1E0267E6DF`.
Launcher SHA-256:
`D855D6A13FBD635619B819FE541669A6FA730E1D2E5C816549FF09899AFAD5C1`.
Launcher source/settings remain unchanged; no hidden feature flag is needed.
All1397 pre-edit source copies verify; exact scope is23 existing source/build/
doc files plus2 new regression files, no missing source. HEAD/branch, pinned SDK
and staging remain unchanged; `git diff --check` passes. MDL-Viewer and other
worktrees remain untouched. Historical ASan runtime remains unconfirmed/not
retested. New image `manual=not_run`, not promoted from the prior user report.

Manual follow-up: use the normal launcher and the unchanged `.104:27016` target.
Ride the shotgun-room lift, press E, try leaving it; then charge Gauss on the
last1–3 uranium until it fires automatically, also aiming downward. Real recoil
is preserved. If the original symptom repeats, mark F8 and close normally for
fresh source-bound diagnostics. The client recovery does not claim server door
activation/unstick or prove the exact previous live overlap.

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, repository
`Pvitaly91/hl-client-engine`, branch `codex/stock-runtime-campaign-5e48b7c1`.
Baseline HEAD `00505f71ff2547a9a6b083e53edf69ec45d48213`; this is not a reset
target. All pre-existing tracked/untracked implementation remains preserved.
User clarification explicitly retains `192.168.0.104:27016`; the `.102` in
the attached E32 request is superseded. No server configuration is changed.

## Evidence and scope

`attribution=user_report`: external LAN connection passed; movement smoothness
and lift contact stability failed; reported FPS approximately100, not a measured
E32 frame-time result. Bot-load causality is not established. Prior fullscreen
playability confirmation is preserved in its limited scope, not promoted to
blanket E31.1/movement acceptance. New E32 executable: `manual=not_run`.

Latest retained external runs are `39086837be4660c05223ec75ecfc4bc2` and
`d332edca1a1b652679bb99fd8bacf756`, both `.104`, Crossfire, valve, protocol48,
maxclients16, reference prediction, borderless1920×1080 and VSync. Actual MSAA
was8 and4 respectively. Both exited0 with `normal_close_completed`; latest
close is `server_disconnect_confirmed`. Their distinct bounded remote-player
slot observations cover1–12, excluding the receiving slot13. This is not a
simultaneous actor count or evidence that those actors are bots. Appearance
records above maxclients contain proxies and are not counted as player slots.
No exact stutter/lift instant, frame-time trace or bot count survives these runs.
`client-status.log` records stdout truncation; it is not a complete raw journal.

Both launch-time client hashes are
`62D34EB67A0A25ACB9BA5CA9AA8BCF419A3DAC9D9ED342DF79E01F42D673B94B`;
launcher hash is
`49D63DD1A2365D42DCA498A3E51616B21FC4EB2769EFC135D0AF8CC00D69D319`.
These are actual retained launch identities, not attributed to an unknown
screenshot. Baseline/source/build/config preservation and copied LAN evidence
are hash-verified in `manual-artifacts/task-records/e32-lan-movement-20261011`.
The pre-edit snapshot contains1374 source files,36 Release files and3 config
files, with separate manifests. No stash, reset, clean, restore or branch change.

## Reproduced defects and production corrections

| Class | Independently reproduced defect | Minimal correction |
| --- | --- | --- |
| B: command/presentation time | Presentation reconstructed deadlines as newest deadline minus command identity distance. After the scheduler discards unsampled wall slots, contiguous IDs are not contiguous wall intervals. The old camera interpolated across a nonexistent simulated interval. | `LiveRuntimeStage::live_reference_prediction_snapshot` reads the existing exact `command_sample_times_` ledger. A wall gap selects an already collision-validated endpoint with `command_wall_time_gap`; the next real pair can interpolate normally. Wire msec, scheduler, command simulation and effects are unchanged. |
| B: reconciliation boundary | A newer incoming source record can acknowledge an older exact move carrier. The former ledger bound a command older than the retained correction anchor, causing boundary-missing fallback and loss of subsequent valid rebase. | `ReferencePredictionCarrierLedger::bind_clientdata` returns `stale_command_boundary` below its generation-local monotonic frontier. Network/canonical state still commits; only prediction binding skips that obsolete boundary. A fresh correction at the same boundary remains legal. |
| D: lift context | A same-svc-time entity-only record changing an unrelated BBOX rebuilt support derivation with dt0. The valid lift velocity was lost although the pusher identity and pose were unchanged. | `ReferenceVerticalSupportMotion::refresh_same_time_context` preserves only unchanged solid pusher identity/complete transform estimates, but adopts the current scene and BBOX membership. Changed/removed/reused support is invalidated. Original observation interval and250ms horizon are not restarted. |

Pre-fix normal Release encoded runtime tests failed15 assertions in3 of6 cases
(`tests-build-release-red-01.log`), including wrong wall timestamps/alpha,
stale-ACK suspension and missing lift carry/presentation. The lift reproduction
uses committed entity42 with precache slot27 resolving BSP submodel1, plus
unrelated BBOX43 moved256→512 at the identical svc_time; no fresh clientdata
correction can conceal the failure. Old next predicted Z96 differs from the
independent expected100. These are project-owned reproductions, not a replay
of missing LAN packets. The user's particular incident remains unverified.

### Authority and support contracts

Behavioral reference is the locally pinned official Valve SDK
`b1b5cf5892918535619b2937bb927e46cb097ba1`: `common/usercmd.h`,
`pm_shared/pm_defs.h`, `pm_shared/pm_shared.c`, `network/delta.lst`,
`cl_dll/view.cpp`. Bodies are not copied or compiled into the client. No Source
tickrate/cvar policy, floating reference or third-party engine implementation.
SDK command duration is wire msec; ground trace identity is not an arbitrary
runtime entity/model slot. Server basevelocity is a velocity contribution,
not a platform transform displacement. Received unknown basevelocity remains
unknown, not observed zero.

Current D4 profile stays local-compatible, not complete stock PM_Move parity:
two coherent same-generation svc_time observations1–250ms apart, vertical
rigid PUSH only, maximum256units/s and bounded250ms derived horizon. Replay
uses the retained frozen/derived context, not invented exact server history.
Seed already contains server displacement; carry covers only each immutable
command's own interval and leaves own velocity unchanged. Nonzero/unknown
unsupported basevelocity fails explicitly rather than double-carrying. Jump,
edge and new ground detach; a newly received stop/reversal updates the model.
An unseen future server stop cannot be predicted exactly. Blocked carry fails
atomically, retaining actual existing query evidence; no new query, hull shrink,
noclip, crush callback, camera freeze or silent server-origin rewrite.

## Optional normal-launch diagnostics

GUI checkbox «Діагностика плавності та ліфтів»; focused nonrepeating **F8** is
«Позначити ривок». It adds no movement/weapon binding. Strict config v3 migrates
v1/v2 with diagnostics Off and preserves existing endpoint/graphics/audio. The
current user settings are not rewritten during development. Normal external
configuration forwards the flag into the same client/stage, not a demo binary.

Collection uses the existing application-owned receipt/report lifecycle:
2048 recent frames within4seconds; up to8 incidents, each at most512 samples
(bounded precontext and postcontext),1second before and1second after. Automatic
observational triggers: frame interval≥50ms, a new correction ordinal with raw
error≥8units, or a first observed eligible fallback/active→fallback transition;2second cooldown and
250ms coalescing. Manual marker timestamps remain explicit. These are diagnostic
thresholds, not physics tolerances. Incomplete/truncated/generation-ended
windows and dropped samples are reported. Two slots are reserved for manual
capture: at total window count6, automatic triggers can coalesce but cannot
open a new window; F8 can still open windows7/8. No unbounded eviction/history.
Retained capacity≤8MiB; encoded report≤16MiB; disabled collection allocates nothing. Encode/percentile sorting
and fixed-leaf atomic output happen at exit, not per frame.

`external-movement.json` is published in the same identity-bound run folder;
launcher «Відкрити звіт» exposes its path in the ordinary run report. Optional
allocation/encode/publication failure is diagnostic failure, not a gameplay
fatal verdict. No raw packet/auth/userinfo dump or native asset root/handle.

Clock domains remain separate: actual steady-clock receive-call begin/end
(not unknown kernel arrival), parse begin, atomic commit, prediction use and
presentation use; svc_time; command simulation time; host render sample time.
The last two source/simulation seconds and process-relative render seconds are
not interchangeable. Raw-error/camera-jump frame quantiles sample the last
observed correction value, not a unique-event distribution; triggers use the
fresh correction ordinal. Waiting/off readiness is not recorded as a fallback.
Reassembled payload completion is dated by final-fragment read. Missing stamps
remain null. Local usable-state age requires coherent same-record clientdata
plus entity reconstruction; unrelated HUD/audio does not refresh it. Network
queue/bytes/entities, replay/collision costs, anchor/error, fallback and actual
blocker/support identities are numeric observations. Runtime entity, runtime
modelindex, BSP submodel and unavailable physent index are distinct.
The captured hull is the neutral player-hull enum (0 standing,1 ducked), not
the BSP hull ordinal. A blocking position test has no observed plane normal;
the report does not substitute the older ground normal for that unknown.
Owned-peer datagrams, rejected sequences and actual committed svc_choke markers
are reported separately. Rejected sequences are not packet-loss percentage,
and choke markers are not proof of server CPU overload. Driver queue depth is
queued owning events, not a measurement of the OS socket backlog.
`observed_players` counts the latest committed packet-entity entries with
`player_movement_schema`, including the receiving slot when present. It is
not a count of live/visible players or bots, and is not negotiated maxclients.

CPU durations describe actual observed segments: update (inclusive), driver,
parse/commit, prediction append/replay, collision scene, main-thread asset
readiness/import handoff, Studio/remote materialization and viewmodel, render
submission, selected receipt/report work and swap wait. Worker asset CPU and
server CPU/FPS are unavailable. GPU GL_TIME_ELAPSED uses a bounded8-query pool;
only an available result is read, matched to its original frame identity.
Pending results are not_available, never zero or blocking waits. Normal loop
adds no glFinish/readPixels. The frame interval counts actual completed swaps,
not a requested cap or cached target. No project FPS-overlay implementation
was found; the user-visible≈100 label is not independent frame-time proof.

Per-incident A/B/C/D labels summarize observed symptoms, never infer a root
cause. Actual remote snapshot selection/previous/current/sample/alpha come
from the existing materializer; no guessed mode or larger interpolation delay.
Receive ordering, atomic publication, immutable asset cache, MSAA and models
remain unchanged. RX/TX bounds are not increased on the unproven starvation
hypothesis; no packet is discarded to choose a newer state or new thread added.

## Verification and performance

Final gate results and ready executable identities are recorded below after
the last production edit; earlier build/test failures remain in the evidence
directory and are not renamed as passes.

E32 fixtures include72 independent lift/actor/render combinations:2/8/16/32
synthetic actors,30/60/100/144/240FPS and irregular sampling, rising/falling/
stationary support. Negotiated limits are fixture-specific, not actual LAN bot
claims. Additional start/stop/reversal, mid-command correction, exact repeated
correction, identity/removal/reuse, membership, ceiling and zero/nonzero/unknown
basevelocity cases use production collision/history/replay and independent
endpoints. Existing D4/H4 fixtures cover neighboring ground/slope/step/jump/
duck/landing/ladder/water/Gauss and effects-dedup contracts. Existing netchan
fixtures cover bounded draining, delay, loss, duplicate/reorder fragments,
retry/backpressure and timeout with no changes to reliable/delta ordering.

The additional encoded load/backlog fixture negotiates32 clients, uses
2/8/16/32 player-schema records plus128 inert entities (>2KiB service bodies),
then drains13 received datagrams with11 fresh, one duplicate and one reordered
old sequence, alongside an absent sequence and180ms scheduler pause. It checks
four exact immutable commands, one attack/release, source order and independent
gravity endpoint. Its actors use inert render models; Studio/GPU load is not
measured by this transport control.

Interleaved warmed On/Off benchmarks run only after compilation is idle. Their
scope is the stated project-owned collection/collision/prediction test harness,
not an end-to-end LAN graphics frame or server/bot AI benchmark. They report
p95/p99, query/scratch high-water, errors/jumps and fallbacks. No claim of LAN
FPS improvement or Studio/GPU optimization is made from these controls.

ASan build/runtime status remains separate: E32 ASan not run; historical runtime
loader failure0xC0000135 is unavailable coverage, not a sanitizer pass.

### Final post-production-edit gates

| Gate | Actual result / evidence |
| --- | --- |
| VS2022/v143/Win32, warnings as errors, complete Release build | passed, `build-release-final-04.log` |
| Final affected Debug client/tests build | passed, `build-debug-final-02.log` |
| Focused E32 Release / Debug | each28 cases,63077 assertions passed; `tests-build-release-focused-final.log`, `tests-build-debug-focused-final.log` |
| Debug neighboring movement/collision/runtime/OpenGL/HUD/launcher/close |599 cases:591 passed,8 explicit installed-resource skips;1222966 assertions; `tests-build-debug-regressions-final.log` |
| Full final Release CTest |3541 registered:3476 passed,65 skipped,0 failed;556.12s, serial; `release-offline-suite-final.log`, `release-offline-summary-final.json` |
| Post-CTest source-build offline checks | passed: version/help/Null, real loopback-only move self-test, diagnostic-contract checker and actual no-stock manual wrapper fixtures; overall script exit0 |
| Core-only complete Release build, HL1 OFF | passed, `core-build-release-final-01.log` |
| Core-only CTest and actual graph/include/alias architecture guard |43/43 passed,23.06s; `core-ctest-final.log` |
| Module-free API / alternate module aggregate |330 cases,188033 assertions passed; `tests-build-core-g1-release-api-final.log` |
| Actual-context E32 GPU timer / pixels | passed in focused Release/Debug and full suite; identical baseline/timed framebuffer signature, delayed identity, pending≤8, foreign query preservation and exception cleanup |
| Unchanged launcher CheckOnly | passed, process-launches0, no game/Steam API/WFP/socket startup; `launcher-checkonly-final.log` |
| Source preservation and Git |1374 original sources present, every saved-copy hash valid, index empty, branch/HEAD unchanged, diff check passed; `source-verification-final.json` |

The65 full-suite skips are explicit optional installed-asset/provider/device,
interactive input/focus, WFP-disabled and host filesystem capability gates.
They are not counted as passes; exact names are retained in the summary.
No timeout or failing CTest remains. Required project-owned OpenGL controls
passed; an opt-in original asset test is a different scope. No production
assertion was weakened. The first load-fixture run failed4 assertions because
its shared fake configuration allowed16 reads and consumed all13 datagrams;
the fixture now explicitly uses RX4 and additionally checks9 remaining and
the intermediate committed source. Its failure log remains retained. The
initial missing launcher CMake target name is also retained separately; the
verified target is `hlclient_launcher`, output `HLClientLauncher.exe`.

### Measured diagnostic overhead and limitations

`tests-build-release-performance-final.log` was captured after a fresh inventory
showed no compiler/linker/CMake process. Same final Release build, paired
project-owned scene/immutable commands,32 warm-up commands and128 measured
commands per mode. Mode order alternates with actor count; the separate
collection microbenchmark interleaves modes over four equally warmed runs.

| Actors | Off command harness p95 / p99, µs | On command harness p95 / p99, µs |
| --- | --- | --- |
|2|292.4 /332.3|240.1 /248.8|
|8|301.5 /367.8|249.5 /256.7|
|16|299.4 /370.6|301.6 /355.1|
|32|316.9 /421.0|332.7 /482.8|

These include assertion-harness work and show sampling/order noise, **not** an
optimization speedup. Both modes retain identical final state. Each actor/mode
case records480 kernel traces,80 replayed commands (including warm-up), raw
correction maximum0, same-time camera discontinuity maximum0, fallback count0
and scratch high-water7340144bytes. At32 actors, Off/On replay p95 is182.7/
261.7µs and collision-context update p95 is83.7/83.1µs. Actual LAN commit
latency, bot AI and32-actor Studio/GPU cost are not measured by this harness.

Collection-only4000-sample Off p50/p95/p99/max is0/0.1/0.1/0.2µs; On is0.9/
1.1/1.2/108.3µs. Timer-resolution zeros and the recorded outlier are retained.
This does not measure all GUI/network/GPU diagnostic overhead in a live frame.
Off retained capacity0; ordinary On benchmark capacity2786496bytes. The
full-capacity regression observed1360-byte frame records,8357056 retained bytes
(below8388608) and10913948 encoded bytes (below16777216). Collection and complete
window publication bounds are positive tested paths, not hypothetical limits.

### Exact source inventory and ready binaries

Five new ordinary files:
`include/hlclient/app/movement_diagnostics.hpp`, `src/app/movement_diagnostics.cpp`,
`tests/test_movement_diagnostics_e32.cpp`,
`tests/test_reference_lift_stability_e32.cpp`, and this E32 document.
The40 changed existing files, compared with the task's full pre-edit snapshot:

```text
apps/hlclient_launcher/main.cpp
apps/hlclient/main.cpp
CMakeLists.txt
docs/ARCHITECTURE.md
include/hlclient/app/external_session.hpp
include/hlclient/app/runtime_replay_local_assets.hpp
include/hlclient/core/command_line.hpp
include/hlclient/core/external_launch_config.hpp
include/hlclient/goldsrc/connect_request_stage.hpp
include/hlclient/goldsrc/live_runtime_stage.hpp
include/hlclient/goldsrc/movement/goldsrc_local_movement.hpp
include/hlclient/goldsrc/netchan_driver.hpp
include/hlclient/goldsrc/reference_brush_collision.hpp
include/hlclient/goldsrc/reference_prediction_anchor.hpp
include/hlclient/goldsrc/reference_prediction_reconciliation.hpp
include/hlclient/goldsrc/reference_prediction_seed.hpp
include/hlclient/input/input_event.hpp
include/hlclient/renderer/opengl/opengl_renderer.hpp
src/app/runtime_replay_local_assets.cpp
src/core/command_line.cpp
src/core/external_launch_config.cpp
src/goldsrc/connect_request_stage.cpp
src/goldsrc/live_runtime_stage.cpp
src/goldsrc/netchan_driver.cpp
src/goldsrc/reference_brush_collision.cpp
src/goldsrc/reference_prediction_anchor.cpp
src/goldsrc/reference_prediction_seed.cpp
src/platform/sdl_platform_event_translator.cpp
src/renderer/opengl/opengl_renderer.cpp
tests/CMakeLists.txt
tests/core_api/CMakeLists.txt
tests/test_command_line.cpp
tests/test_external_launcher_e31.cpp
tests/test_external_session_e31.cpp
tests/test_input_events.cpp
tests/test_opengl_world_renderer.cpp
tests/test_reference_move_transmission.cpp
tests/test_resource_client_response_stage.cpp
tests/test_runtime_replay_local_assets.cpp
tests/test_sdl_input_translation.cpp
```

No production source is kept only in manual artifacts. Prior uncommitted
implementation, snapshots, other worktrees and MDL-Viewer remain untouched.
The existing manual launcher resolves `ClientPath` to the following ordinary
Release executable; normal GUI launch also selects its sibling, not a demo.

`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`,4510720bytes:

```text
06D79D22D54F660A90B1605C457C9CF1561E74415893C19137CCBD0F293B5325
```

`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\HLClientLauncher.exe`,258560bytes:

```text
C0BA2D246AF7691E97D1ED79C0A9AB5DE6BA643FF20EB482D0EA4E42D7E9D556
```

Sibling SDL3.dll remains SHA-256
`D88767FB3F3D1AA4BB4C67BD0465C3EEC4258D8A83A8B0ED8CE9A114265C41A4`.
The SDK remains pinned at the stated revision. No current hash is assigned
retroactively to an unknown user launch.

Offline results: `movement_stutter_causes_classified` (specific B/D reproductions;
A/C and actual LAN incident unknown), `reproduced_movement_defects_fixed`,
`moving_lift_prediction_regressions_verified`,
`lan_movement_and_lift_stability_offline_verified`.
New build `manual=not_run`; this is not a claim that every LAN stutter is fixed.

## One targeted user capture

Open the prepared normal `build\bin\Release\HLClientLauncher.exe` and connect
to **192.168.0.104:27016**, retaining current graphics settings. Enable the
diagnostic checkbox. Walk on ordinary ground among players, ride the actual
server lift up/down/start/stop, walk/duck on it and jump off. At a stutter press
F8 and note whether own camera/motion or only remote players jump. Close
normally, click «Відкрити звіт» and send the run ID/report. One such capture is
needed only because the existing incident has no retained timing/blocker data.
No full weapon-suite retest or owned-HLDS Crossfire substitute is requested.

New agent live/HLDS/Steam/managed captures0; RCON/server/bot changes0;
staging/commit/push none. The final offline result must not be confused with
manual acceptance of the unknown LAN incident.
