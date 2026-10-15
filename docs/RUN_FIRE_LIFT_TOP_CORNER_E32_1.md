# HLC-E32.1 — run/fire, lift upper stop, finite outer corner

## Scope and manual evidence

Authoritative worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, repository
`Pvitaly91/hl-client-engine`, branch `codex/stock-runtime-campaign-5e48b7c1`,
HEAD `00505f71ff2547a9a6b083e53edf69ec45d48213`. Existing dirty/untracked work
is preserved; no staging, commit, push, reset, cleanup or branch change.
Server remains **192.168.0.104:27016**. New agent live/Steam/HLDS/managed-capture
sessions: **0**. No server changes, RCON, client B or new bots.

The three residual results are independent `attribution=user_report`:

- `run_and_fire_at_player_smoothness=failed`;
- `lift_top_transition_stability=failed`;
- `convex_corner_smoothness=failed`.

The preserved E32 automated results are not overall LAN/manual acceptance.
The new build is `manual=not_run`; the cause of each particular LAN incident
remains unverified until incident evidence matches a reproduced mechanism.

Latest actual external run `c11f597e9441c77a946d3470e2d3426c` used `.104:27016`,
crossfire/valve/protocol48, receiving entity13 and negotiated maxclients16.
Client SHA was `06D79D22D54F660A90B1605C457C9CF1561E74415893C19137CCBD0F293B5325`;
launcher SHA was `C0BA2D246AF7691E97D1ED79C0A9AB5DE6BA643FF20EB482D0EA4E42D7E9D556`.
Process creation: `2026-10-11T20:52:58.1919493Z`; finished 23:55:42 Kyiv.
It closed normally, exit0, `normal_close_completed`, confirmed disconnect.
Movement diagnostics were **off**, and no `external-movement.json` exists.
The short client-status log has no per-incident timeline. Consequently A/B/C
before → first divergence → correction/fallback → recovery cannot be recovered
from that run. Aggregate counters do not identify these causes.

The named screenshot `HL Client Engine - Player 11.10.2026 23_55_06.png` was
not present in the attachment or bounded Pictures/Downloads/Desktop/attachment
search; the user replied that the image is unavailable. No coordinates,
plane IDs, entity IDs or exact run/SHA are inferred
from its name. The finite outer-corner fixture is project-owned synthetic
geometry, not a claim to reproduce a localized crossfire BSP face.

## Preservation and E32 baseline

Evidence/rollback directory:
`manual-artifacts/task-records/e32-1-run-fire-lift-corner-20261012`.
Before editing, the existing procedure copied and SHA-verified 1379 source
files, 36 Release files, three configurations and 21 latest-LAN evidence files.
Staged set was empty. Existing executable hashes and Git identity are retained
in `before-git.json`, manifests and `manual-evidence-audit.json`.

The three E32 fixes remain distinct and are not claimed as new E32.1 work:

1. Exact retained command wall deadlines through scheduler skips:
   `LiveRuntimeStage::live_reference_prediction_snapshot` / E32 production
   timeline regression.
2. Monotonic carrier binding: `ReferencePredictionCarrierLedger::bind_clientdata`
   rejects a lower already-obsolete command boundary; same-boundary fresh data
   remains legal / prediction-anchor and E32 production timeline regressions.
3. Same-source-time unchanged pusher trajectories:
   `ReferenceVerticalSupportMotion::refresh_same_time_context` preserves the
   individual trajectory while adopting new scene/actor boxes / E32 lift and
   same-time brush regressions.

SDK reference is the existing pinned official Valve revision
`b1b5cf5892918535619b2937bb927e46cb097ba1`, not Xash/ReHLDS code.
PM_FlyMove/WalkMove/ClipVelocity/CatagorizePosition/CheckStuck and view punch
contracts were checked. No SDK bodies, BSP edits, smaller player hull,
global epsilon change, widened correction horizon or removed recoil.

## Reproduction and correction evidence

### A — run/fire feedback

Investigation separates typed input, exact wire command, shot acceptance,
hit/blood feedback, damage, coherent clientdata commit, carrier binding,
replay and camera composition. A0–A4 and local return-damage controls use
Glock/MP5 primary, not Gauss impulse. The candidate transport ownership bug
is a contemporaneous ordinary suffix inheriting enclosing reliable/fragment
flags and being treated as an old reassembled body. Pre-fix, repeated coherent
suffix updates are rejected with `old_body_or_reassembly` (ordinal8); at
command step12 the existing250ms correction horizon suspends prediction.
The direct-envelope trajectory remains active under the same command/control
timeline. This mechanism also occurs in A0 without attack: firing is not a
necessary cause, and no speculative punch/movement rewrite is made.

Production ownership now follows the exact driver-separated suffix byte range:
`OwnedNetchanPayload::body_kind` → `make_owned_service_payload` → atomic runtime
source metadata → `ReferencePredictionCarrierLedger::bind_clientdata`.
The enclosing reliable/fragment header flags remain unchanged. Old reliable
bodies and completed transfers reject before reserving the suffix's source
chronology; svc_time alone never grants eligibility. The exact sent-carrier
reference contract and stale-command-boundary guard remain. A transport ACK
is still not claimed as observed execution acknowledgement.

The coherent completion+suffix profile exposed a second full-path defect:
clientdata frame history treated distinct completion and contemporary bodies
under the same completion-header sequence as an altered replay of one frame
(`svc_clientdata source frame was replayed with different payload bytes`).
This is separate from the carrier rejection and is retained in A error log08.
The normal and NOP-completion/suffix profiles passed before this failure.
`GoldSrcClientDataDecoder` and `PacketEntityDecoder` now keep bounded
completion/ordinary fingerprint domains. Driver-proven completion still
decodes and publishes its owning canonical current frame, but does not
register the later completion-header sequence as its unknown original delta
base. The ordinary suffix owns that fresh history entry; a following delta
resolves it normally. Completion/direct altered-byte duplicates, absent bases,
generation reset and transactional rollback remain guarded.

The existing higher-layer runtime capture converter forwards both facts.
`StockRuntimeTransportReplay` now emits completion→suffix, matching actual
`NetchanDriver` order; chronology guards are not relaxed. A project-owned
encoded transport/production-conversion/decoder regression covers canonical
health250→100→100 and history0→1→2. This is ordinary/full-completion ownership
proof, not a new filesystem corpus/capture-format subsystem or stock-exact
recovery of arbitrary old delta-tagged completion bodies. Final focused Release
including the cache cases passed, as did Debug/core and the full final suite.

The normal runtime fixture uses three envelope profiles, Glock/MP5, A0–A4
and separate return damage. Host model/ammo are explicit project-owned
fixtures; A2 point-query isolation is not a new production GL/audio impact
acceptance claim. The particular LAN run remains uncorrelated.

Post-fix normal runtime remains active with no fallback increment and bound
anchor at all16 fixed20ms steps, past the old correction-horizon failure.
The exact air oracle progresses x=10+0.6×(step+2),
z=64−400×[0.02×(step+2)]² (position tolerance0.002), not a frozen-player pass.
All six ordered30/60/100/144/240/irregular snapshot histories retain those
origins/velocities, canonical provenance, command/replay/history counters and
18 generated/submitted commands. Accepted action and attack-notification counts
are1 per firing control (0 in A0); target-feedback blood count1, not duplicated
by render reads/replay. Root camera test separates base0/0, server punch4/2 and
local pitch−2 from final pitch−2/yaw2, with unchanged translation. Real LAN
frame spikes, commit age, angular recoil and incident fallback duration are
**unmeasured**, not claimed zero/improved, because baseline capture was off.

Initial fixture failures (invalid held movement bit, expired flash sampling)
are retained, not counted as production defect proof.

### B — observed lift upper stop

Pre-fix witnessed command-pair test: exact raw correction≤0.001, predicted
Z106, observed physical support velocity0. A retained predecessor Z102 is
checked against a newly stopped brush at70: relative standing clearance is
32→36 instead of36, with startsolid/allsolid on the older samples. This is a
presentation-time mismatch, not evidence that raw movement was wrong.

The production correction is scoped to matching witnessed support planes
and the corresponding rendered pusher in
`sample_reference_support_presentation` (`reference_brush_collision.cpp`).
Old future carry is not continued after stop evidence. Complete body/eye
collision checks and other blockers remain. No fixed upper height or
rendered position fed into physics.
This local-compatible presentation guard requires the same grounded hull,
exact stable entity/model/plane witness and upward matching normal, the existing
≤250ms timing bound, and≤12.8-unit contact-plane translation (256×0.050).
Unsupported/nonmatching pairs retain the previous conservative behavior.

First post-fix B/C group is green. At every alpha0/.05/.25/.5/.75/.95/1 the
standing support-relative height is36 and complete body sweep is free, while
physical predicted Z106/stop velocity0/raw error≤0.001 are unchanged. The
alpha1 aligned sweep-start correction is included; it does not require a
scene transform change. Centre/edge/forward/back/duck/jump/wall/ceiling and
upper-floor handoff are covered at30/60/100/144/240/irregular rendering.
Separately, the encoded normal Runtime comparison at exactly the same now+5ms
requires camera discontinuity≤0.002, raw correction<0.002, unchanged command
steps and no fallback increase; four further neutral commands remainZ106/
own velocityZ0. These normal-path tolerances are not the standalone≤0.001 bound.
The two-unit ceiling fixture legitimately recategorizes the short clipped
jump as grounded; it requires ceiling contact and maximum Z≤108 instead of
claiming impossible airborne status. Fire-on-top with three authored off-route
collision boxes has per-command statistics and final physical signature parity
with ordinary forward movement. The independent friction/acceleration oracle
is7.84 +93×2 =193.84 units over100 commands, tolerance0.002 unchanged.

### C — finite convex corner

Pre-fix normal runtime: valid predicted/canonical endpoints, raw matched
correction0.1298085373, same-render-time camera jump0.9631477028;
`interpolated` becomes `visual_correction_blocked`. The straight residual
camera chord cuts a finite exterior vertex. An independent two-leg exterior
route is collision-clear. Tangential standing route controls already move
349.977–357.360 units with zero zero-progress commands; a broad kernel patch
is not justified by this witness.

The production correction uses a bounded collision-proven exterior route,
not noclip, a long freeze or camera position written back into simulation.
Unproven/blocked routes retain conservative rejection.
This is a narrow local-compatible same-height (≤0.001 vertical difference),
dry walking/static orthogonal world-corner route, not stock-exact general
platform/airborne/pathfinding support. Full body, eye and ground proofs remain
required and the original20ms deadline is preserved.

First green normal-runtime metrics: same-time jump0, raw0.129809 unchanged,
first blocked chord fraction0.916229 / `present_linear_body`.
Repeated exact correction during the original20ms fade changes epoch2→3 but
repeat-rebase jump stays0; physics step count stays1. Sampling covers
30/60/100/144/240Hz, irregular and dense1ms frames; cached repeats perform no
new queries. The exterior path is≤4 units and the original source/deadline is
not restarted. Neutral validated-camera publication keeps strict source and
revision checks. Forty-eight authored physical routes (standing/duck,
left/right, diagonal/tangent/clearance, floor/hole, with/without three off-route
collision actors) travel132.341–408.571 units with zero zero-progress commands.
IN_ATTACK is present every third command. These are collision-box presences,
not an acceptance claim about rendering remote Studio/weapon effects.

## Existing opt-in F8 capture

The same recorder/publication path is retained. Added causal observations:
exact accepted command sequence/sample/msec/movement/buttons/angle-turns,
source record and body provenance, correction anchor and matched origin
before rebase (both origin and velocity), typed anchor-status ordinal,
authoritative velocity, query purpose/plane, presentation
reason and separate base/server/local/final camera angles.
Camera angles do not feed back into input; translation remains independent.
Missing observations remain null, not zero. `authoritative_source_record_identity`,
`authoritative_source_sequence` and `authoritative_body_kind` form one canonical
clientdata provenance set, separate from the latest network `source_sequence`.
Thus a later blood/HUD-only commit cannot mix its transport sequence with an
older retained movement identity. `anchor_status` uses the typed enum in
`reference_prediction_anchor.hpp`:0 bound,8 old body/reassembly,11 stale command
boundary. Source/body and query tokens
identify operations, not automatic assertions of a LAN root cause.

The existing 8 MiB/16 MiB memory/encoding bounds, eight incidents, 512 samples
per incident, one-second pre/post windows and two reserved manual incidents
remain. The recent ring is 968 records (four seconds at240Hz); higher-rate
capacity drops are explicit. No per-frame disk flush, auth/userinfo content,
glFinish, blocking readback or new capture platform.

## Production paths changed

- A owning pipeline: `netchan_driver.{hpp,cpp}`, `service_message_stream.{hpp,cpp}`,
  `client/runtime_observation.hpp`, `runtime_replay_session.cpp`,
  `reference_prediction_anchor.{hpp,cpp}`, `client_data_decoder.{hpp,cpp}`,
  `packet_entity_decoder.{hpp,cpp}`, `runtime_replay_capture.{hpp,cpp}`,
  `stock_runtime_transport_replay.cpp` and normal `live_runtime_stage.{hpp,cpp}`.
- B: render-only `sample_reference_support_presentation` in
  `src/goldsrc/reference_brush_collision.cpp`.
- C: `reference_prediction_reconciliation.{hpp,cpp}` bounded route,
  `prediction_visual_correction.{hpp,cpp}` validated neutral publication, and
  normal live-stage presentation/lifecycle ownership.
- Existing capture/application composition: `movement_diagnostics.{hpp,cpp}`,
  `live_visual_control.{hpp,cpp}` observed camera angles and `apps/hlclient/main.cpp`.
  Normal/core test registrations and targeted fixtures are updated.

No movement kernel, SDK source, render shader, audio policy, resource assets,
server trace, ammo/damage/health rules or server configuration is changed.
No new parallel physics/audio/renderer/capture subsystem is introduced.

## Final gates and ready binaries

Final production edit is the cache/owning offline adapter correction in
build12. Subsequent cadence changes are test-only. No concurrent build or
live client was present at final Release suite startup. Initial failing builds
and RED logs are preserved separately; no final count copied from E32.

| Gate | Actual result | Evidence in the E32.1 task record |
| --- | --- | --- |
| Normal Release ALL / Debug ALL | passed, v143 Win32 /WX | build-release-12-final-cache-all.log; build-debug-14-final-all.log |
| Focused Release / Debug E32+E32.1 | each41 cases,589007 assertions,0 failures | tests-build-release-12-focused-final.log; tests-build-debug-13-focused-final.log |
| Debug movement/collision/replay/runtime/decoder/camera/OpenGL neighbors | 516 pass,10 explicit asset skips,0fail;1856808 assertions | tests-build-debug-15-neighbor-regressions-final.log |
| Core-only ALL (HLCLIENT_BUILD_GAME_HALFLIFE=OFF) | passed | core-build-release-15-final-all.log |
| Core-only CTest / architecture / alternate | 43/43 pass,26.52s | core-ctest-final.log |
| Core API, without concrete HL module | 339 cases,310137 assertions,0fail | tests-build-core-g1-release-14-core-api-final.log |
| Full final Release + SourceOnly/no-stock/Null/launcher postchecks | 3489 pass,65 explicit skips,0fail /3554 registrations;690.00s CTest; wrapper exit0 | release-offline-suite-final.log |

OpenGL checks are actual-context project-owned fixtures, including existing
E32 timer/pixel identity, bounded pending queries and foreign-query/exception
cleanup. They are not a real LAN smoothness measurement. The10 neighbor skips
require explicit approved original HUD/blood/decal/crossbow/viewmodel/local
asset roots; mandatory project-owned paths ran. No optional real-device audio
or original-asset opt-in is reclassified as passed. Core collection-only
microbenchmark ran with compilers idle (not a live frame-time benchmark).

Intermediate `tests-build-release-09-b-c-capture-green.log`:116809 assertions,
10 cases passed. Preserved E32 intermediate:28 cases/63326 assertions passed.
Current capacity proof:1656-byte frame,8387200 retained-capacity bytes,
11715217 encoded bytes, below unchanged8MiB/16MiB limits.
ASan E32.1 build and runtime: not run. Runtime remains unconfirmed (historical
loader blocker); no repair of
the ASan infrastructure or new live session is performed for this task.

Ready normal artifacts (no demo/debug feature flag or asset copying):

- `D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe` —
  4539904 bytes;SHA256 `F71F0D672F3FEC262477BDEE53A2060576457849B63DB973EFC8704742C7BD9A`.
- `D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\HLClientLauncher.exe` —
  258560 bytes;SHA256 `1CD952F32F1F46BD462EFDB5985C512579AB1E209A31076408E4487D4E3A51E4`.
- SDL3.dll — SHA256
  `D88767FB3F3D1AA4BB4C67BD0465C3EEC4258D8A83A8B0ED8CE9A114265C41A4`.

Actual saved configuration remains v3/.104:27016, Player, borderless1920×1080,
VSync,MSAA8,brightness0.13/gamma0.88/contrast1.13/volume35 and diagnosticsOff.
These were not silently changed. The historical run's gamma0.9 is a different
launch-time configuration, not evidence that the saved0.88 was overwritten.
Saved launcher.json SHA remains
`E2B58E14044928D3D1ADA3B304FA12E8A2687FC21A82D161B11A2556AB9C386E`,
identical to the pre-edit configuration snapshot. The unchanged manual wrapper's
actual dictionary `ClientPath` resolves to the normal Release path and F71F… SHA.
SourceOnly/CheckOnly performs no game/Steam API/WFP/socket startup, not live
readiness validation. Normal GUI uses that sibling client; no manual binary/WAV
copying, hidden effect flag or stock client is required.

Final `source-verification-final.json` verifies all1379 preserved original
files/copies (0 missing,0 invalid copies),37 scoped changes and four new fixture
sources plus this single report. SDK, unchanged manual wrapper, old E32 report
and unrelated source hashes are preserved. `git diff --check` passed; branch
and HEAD remain as above, staged set is empty. Commit/push/live/server changes
remain0. LF→CRLF notices concern pre-existing wrapper/script settings, not errors.

Independent production result: **run_fire_lift_top_corner_stability_corrected**
for the reproduced mechanisms. A/B/C LAN incident attribution and all three
new-build manual verdicts remain **unverified / not_run**, not overall movement
manual pass. The original E32 automated and user-reported histories are retained.

## Short user verification

Use the existing normal Release `HLClientLauncher.exe` to `.104:27016`.
Enable **«Діагностика плавності та ліфтів»**; no console/server changes.

1. Run without firing, then fire into air, wall and another player.
2. Lift rise → top stop → stand → walk onto upper floor.
3. Round the reported corner both ways, initially without firing.

At each residual jerk press **F8**. Exit normally so the existing receipt
path publishes the incident report. No full-arsenal retest is requested.

## Startup NOP/ACK follow-up — 2026-10-12

The user subsequently reported that the delivered F71F… client did not start
(`attribution=user_report`). Runs `427a246253c3b90de11d4e0a17a6c9ef`
(borderless) and `0a37ffe85368490dc805ff08053f9b64` (windowed) both reached
signon and deliberately exited2 with `response_stage_failed`, nested
`server_payload_acknowledgement_invalid`, source sequence12, before map/gameplay
observation. Video/MSAA startup succeeded. The historical receipts did not
persist the numeric ACK, first response TX or owning body kind. Their exact
payload type is therefore **unverified**, not retrospectively labelled NOP.

A separate offline diagnostic used existing Release production libraries and
the existing fake-transport stage fixture, without changing delivered binaries.
The same independently encoded `svc_nop` with ACK3 is allowed before resource
response TX4 but rejected after it with that exact error. Header-only ACK3 is
allowed; a semantic continuation with old ACK3 remains correctly rejected.
Evidence: `manual-artifacts/task-records/startup-ack-audit-20261012/probe-03.log`
(five modes,259 assertions) and `existing-controls.log` (four existing cases,
418 assertions). This proves a timing-sensitive client defect, not the exact
contents of the two historical server packets.

`ResourceClientResponseStage::handle_server_payload` now probes control traffic
under the existing explicit live `decode_nop_control` policy while waiting for
the genuine reliable response ACK. The renamed shared private helper
`decode_response_control_payload` validates the complete owning service envelope
and every decoded message. Only a non-empty, wholly decoder-proven NOP body is
consumed before the semantic continuation ACK guard. Mixed/malformed bodies
retain the original owning bytes and strict path. No prefix-only skip, raw
opcode heuristic, broad ACK exemption or wire grammar change is used.

Consumed controls never occupy `pre_ack_payload_`, acknowledge a reliable
generation, restart a fixed deadline or create a response completion. Counters
and events commit only after full proof and bounded admission. Existing count
and wire-byte budgets are shared across pre/post TX controls (32 payloads/4096
wire bytes by default); envelope decoding also retains its configured decoded
byte bound. No separate small decoded-message-count bound is claimed for the
existing `decode_one` loop. Pre-TX failure codes and opt-out policy remain;
post-TX controls have their own typed classification/event/overflow labels.
Reliable toggle/generation and transport sequence checks are unchanged.

The normal application now persists a compact optional `response_payload`
object in its existing durable diagnostic receipt: numeric ACK/first TX,
source sequence/ordinal, wire length, body kind, encoding/classification and
observed response flags. Phase/category are explicit for response failures;
driver/usercmd causal failures keep priority. Null is distinct from observed
zero. Reassembly follows the owning converter (a fragment's ordinary suffix is
not itself reassembled), and rejected controls do not increment committed
consumption counters. No raw payload/auth/server text/native asset locator is added, and no
launcher configuration/schema or resource-authority expansion is needed.

Fresh preservation uses the existing snapshot mechanism in
`manual-artifacts/task-records/e32-1-startup-nop-fix-20261012`:1384 source files,
36 Release files,3 configurations and13 latest-failed-run files, all copied and
SHA-verified before edits. Existing source/foreign changes are retained.
The user's current windowed launcher configuration is preserved, including
`.104:27016`, diagnosticsOff and SHA
`0DAA089A0CC8A037A39D5CBA02217259257DFB216DF32C38D7A219019D2ED46C`.
The earlier borderless configuration paragraph above records the previous
handoff, not a claim about this new saved configuration.

Five added production-stage regression cases cover raw/compressed old-ACK NOPs,
numeric coverage with wrong reliable toggle, repeated controls/updates, genuine
ACK-only completion, shared count/byte bounds, fixed timeout, malformed/mixed
payloads and explicit-policy gating. Two durable-metadata cases cover nullable
numeric identity, provenance and privacy bounds. Old assertions are unchanged.

Final production edit is the owning-provenance/rejected-counter correction in
Release build02. Subsequent ACK-fixture changes are test-only: a reliable NOP
actually emits a later transport ACK packet; the tests require its numeric
advance before sending the matching reliable-generation ACK. Earlier logs
retain failures from duplicate numeric ACKs and an invalid10ms idle-poll fixture
(the driver minimum is100ms), not a weakened production guard. Existing test
assertions remain intact. Builds used separate normal Debug/core-only output
trees; all tests below ran with compilers idle.

| Follow-up gate | Final actual result | Evidence in the new task record |
| --- | --- | --- |
| Release ALL / Debug ALL / HL1-OFF core-only ALL | passed, VS2022 v143 Win32 /WX | build-release-02-final-startup-all.log; build-debug-03-final-startup-all.log; core-build-release-04-final-startup-all.log |
| New Release startup/metadata regressions | 7 cases,1323 assertions,0fail | tests-build-release-03-new-startup-only.log |
| Debug startup/sign-on/E31/E32/E32.1/anchor regressions | 159 pass,3 explicit skips,0fail /162 cases;665735 assertions | tests-build-debug-04-final-focused.log |
| Core-only CTest including architecture guard | 43/43 pass,24.43s | core-ctest-final.log |
| Explicit core API | 341 cases,310171 assertions,0fail | tests-build-core-g1-release-14-core-api-final.log |
| Full final Release after last production edit | 3496 pass,65 explicit skips,0fail /3561 registrations;861.55s | release-offline-suite-final.log |

The full Release wrapper also passed Null/version/help, synthetic local UDP
transport, functional-log validation, no-stock manual/external-map fixtures
and actual launcher SourceOnly/CheckOnly. Wrapper exit0. The final normal-close
no-stock test passed in125.91s. Optional original-asset/audio-device/WFP/native
capability skips remain skips, not live acceptance. No new ASan build/runtime
was run; historical ASan runtime remains unconfirmed.

New normal Release `hlclient.exe` SHA256:
`2CDFDC283F07C70640AFD33C26460931D606A4E5678AE9EE9235CAB167E4C296`.
The unchanged normal launcher remains SHA256
`1CD952F32F1F46BD462EFDB5985C512579AB1E209A31076408E4487D4E3A51E4`.
Actual `Get-H2ManualConfiguration` dictionary `ClientPath` and GUI sibling
selection resolve to that normal Release client; no manual copying or hidden
flag is needed. This new identity supersedes the earlier ready-client F71F…
paragraph above, without attributing it to any historical user run.

`source-verification-final.json` verifies all1384 preserved source copies and
original files, only7 scoped file changes,0 missing/unexpected files, unchanged
SDK/manual wrapper/user configuration, the same branch/HEAD and empty staging.
`git diff --check` passed. The7 changed files are the response stage source/
header, existing external-diagnostics header, normal app composition, two
existing test TUs and this existing task report. No duplicate roadmap document.

Agent game/Steam/HLDS/LAN sessions, server/configuration changes and
staging/commit/push remain0. New build manual verdict remains **not_run** until
the user actually verifies the normal launcher connection to `.104:27016`.

## Residual running stutter: reliable-carrier follow-up — 2026-10-12

The user reports improved but intermittent running stutter with arbitrary
weapons (`attribution=user_report`). Subsequent user run
`1dc07652753f238313427263a8f4c385` positively identifies the preceding normal
client `2CDFDC283F07C70640AFD33C26460931D606A4E5678AE9EE9235CAB167E4C296`,
crossfire/protocol48, receiving entity13 (`max_clients=16`), diagnostics enabled and
normal acknowledged close/exit0. This supplies startup/manual-use evidence for
that preceding build, not manual acceptance of the following movement fix.
No new live session was started by the agent.

The preserved `external-movement.json` SHA256 is
`686C63155B3CE2FF551C9EDA41B5CB31D38B822AA63EF66B43968BD1F6A8EC9A`.
`manual-artifacts/task-records/e32-1-reliable-movement-20261012/`
contains verified copies of1384 source files,36 Release files,3 configuration
files and22 run-evidence files, plus a reproducible compact capture summary.
The actual saved configuration is now borderless1920x1080, MSAA8, brightness.16,
gamma.88, contrast1.13, volume35, `.104:27016`, diagnosticsOn. Its SHA256 is
`BBEE576BE1C2D33CFFF835328E52880D3944CBE78F5551A2E2F75E0D50A6243D`.
These are the user's intervening choices, not changes made by this fix.

### Captured evidence and limits

Manual incident8 has243 frames from101.565853s to103.562061s: all use
`reference_anchor_timeout` fallback. Server source sequences2274..2317 and
record identities2259..2302 advance, but command anchor4774 and accepted
correction ordinal1104 remain frozen. At F8/102.564663s the fallback has lasted
2521.6707ms despite fresh state age20.363ms, frame time7.0674ms and GPU7.08474ms.
All retained body labels are `reliable_body`;43 commit-bearing frames reject
the anchor as `old_body_or_reassembly` (58 frames retain that rejection status).
The other185 frames' historical status0
is **not** a correction success: a subsequent outgoing `record_sent` overwrote
the incoming diagnostic. State age stays below49ms, receive queues drain and
rejected transport sequences remain0. The complete late window demonstrates
fallback beyond5s before a direct body restores prediction. This is not
evidence of a multi-second GPU stall, packet loss or a particular weapon.

Incident7 is separate: eight `solid_start` samples around27.797..27.859s name
server BBOX entity335 at a standing seed position
(-705.203125,796.4921875,-1659.96875). Its model/owner/bounds were not retained;
runtime entity identity cannot be inferred from BSP entity order. Incident4
contains two brief large server corrections (51.67 and36.54 world units),
with possible external impulse but no causal proof. These remain **unresolved
attribution**, not automatically fixed collision defects or bot-load diagnoses.
No collision exclusions, penetration tolerance, seed unstick, knockback,
correction limits, command rate or250ms anchor deadline are changed.

### Reproduced mechanism and production change

The decoded `NetchanSequenceWord` flags describe the reliable bit of the
**whole packet**. An ordinary
unfragmented payload can contain reliable controls followed by a fresh movement
snapshot; the project's own `NetchanSession` composition supports this shape.
There is no encoded reliable-prefix length for that payload. The old
`ReferencePredictionCarrierLedger::bind_clientdata` rejected it wholesale,
starving the anchor while normal runtime publication continued. This explains
the independently reproduced timeout mechanism; the historical capture does
not preserve raw payload bytes or service cursors for a byte-exact replay.

The neutral movement policy gains an explicit, default-off coherent current
snapshot capability, selected by the Half-Life module. The normal live stage
uses `bind_observation`: fully reconstructed, fresh, same-generation
`svc_time -> clientdata -> packetentities` observations must have ordered,
nonempty source cursors, the same record/ordinal/transport sequence/ACK/raw
flags, and the receiving player's movement schema. For this new ordinary
reliable-frame admission, server time must be finite and strictly newer than
the accepted coherent-frame watermark. That watermark
advances only after successful exact sent-carrier binding; direct/suffix
observations also advance its maximum when coherent; their legacy admission
does not newly require a coherent witness. Suspension does not reset it.
A fresh session/map stage owns a fresh ledger.

The existing exact transmitted command receipt, nonrewinding command boundary,
sequence chronology, semantic seed and life-floor checks remain. Reassembled
bodies never qualify; a rejected completion cannot consume the same packet's
independently proven ordinary suffix. Retained data, time appearing after the
movement data, repeated time, missing/mismatched sources and rejected ACKs
cannot create this compatibility proof. The original bare-carrier API stays
strict. Raw reliable/reassembly flags are never falsified.

This is a documented **local-compatible current-snapshot profile**, not proof
of a stock server's internal command execution or byte membership. A previously
unseen delayed but internally coherent whole snapshot cannot be mathematically
distinguished using an unfragmented outer header alone. The pinned official
Valve SDK revision remains `b1b5cf5892918535619b2937bb927e46cb097ba1`; its
`UpdateClientData` documents current player fields, but not engine netchan
scheduling. No ReHLDS/Xash implementation was read or copied.

Incoming anchor status now retains ownership of the diagnostic field: an
outgoing send still updates its own counters but no longer erases a rejection.
The existing bounded provenance field distinguishes
`coherent_movement_frame_reference_profile` from
`exact_sent_carrier_receipt_reference_profile`; body kind remains unchanged.
No movement-frame recorder size/capacity growth, raw payload logging, public
feature flag, new resource access, launcher setting or server change is added.

### Follow-up acceptance

The RED production-path control used the new API scaffolding with the HL
compatibility selection disabled, retaining the original admission rule (not
an unchanged archived executable). The unreliable route and all four negative
sections passed; the reliable route froze at command1, accepted0 of20 new
snapshots and entered fallback after250ms. The retained log records72 failed
assertions/1786,1 failed case/2. It is the intended behavioral failure, separate
from the earlier C4099 class/struct forward-declaration compilation error;
that declaration was corrected without disabling /WX.

With the HL profile enabled, the same independently encoded route advances
anchor2..21 and accepts20 of20 new frames over400ms with0 fallback, for both
ordinary and reliable-bearing packets. The independently authored falling
oracle is `x=10+0.6*n`, `z=1024-0.16*n*n`, `vz=-16*n`, with1/8-unit wire
position quantization and an unchanged0.064-unit comparison bound. Coarse20ms
and intermediate7/13/20ms update schedules retain identical immutable commands
and authoritative receive times. Repeated updates cannot add commands or
corrections. Raw reliability remains observable and the new proof flag is
true only for the admitted compatible frame. Negative tests also preserve
the incoming rejection across an outgoing-only command update.

Five actual transmitted-carrier unit cases add35 malformed-witness variants,
default/legacy rejection, initial time0 admission, duplicate sequence/record,
same/lower ACK, missing carriers, rejected-time nonpoisoning, direct/suffix
time frontiers, same-packet fragment-completion/suffix and generation reset.
All original test assertions are preserved. New tags are regression labels,
not a renamed roadmap milestone.

New-only Release and Debug each pass7 cases/5881 assertions. HL1-OFF core-only
CTest passes43/43 in25.29s, including the architecture guard; its explicit
API suite passes341 cases/310171 assertions. The movement recorder remains
1656 bytes/record and8387200 retained capacity bytes, within its original cap.
Broader focused Debug and Release each pass397 cases with2 explicit optional
resource skips/399 cases and781477 assertions. The Debug command also included
an unmatched `[reference-replay]` tag; its other matched tags selected the same
399 cases as Release. This is not a separate replay-suite success claim.
The full final Release suite ran after the last production edit and final
test additions: **3503 passed,65 explicit skips,0 failed /3568 registrations**,
719.40s. The final normal-close/no-stock scenario passed in120.65s. The wrapper
then passed Null/version/help, synthetic local UDP, functional-log validation,
no-stock manual/external-map fixtures and actual SourceOnly/CheckOnly; exit0.
Original-asset/audio-device/native-capture/link/WFP capability skips remain
skips, not manual or real-server acceptance. Performance cases ran with
compilers idle; deterministic new-only tests were also run while a separate
configuration compiled, without using those runs as performance measurements.

| Follow-up gate | Actual result | Evidence under the new task record |
| --- | --- | --- |
| Legacy normal admission RED | reliable route fails72 assertions; direct route/negative sections pass | tests-build-release-red-profile-off.log |
| New Release/Debug regressions | each7 cases,5881 assertions,0fail | tests-build-release-green-new-regressions.log; tests-build-debug-green-new-regressions.log |
| Broader Release/Debug regressions | each397 pass,2 skips,0fail;781477 assertions | tests-build-release-final-focused.log; tests-build-debug-final-focused.log |
| Release executable targets / Debug ALL / HL1-OFF ALL | passed,32 Release targets; open unchanged launcher excluded | build-release-offline-targets.log; build-debug-green-final.log; core-build-release-final-core.log |
| Core-only CTest / explicit API | 43/43;341 cases,310171 assertions | core-ctest-final.log; tests-build-core-g1-release-14-core-api-final.log |
| Full final Release + wrapper | 3503 pass,65 skips,0fail;wrapper exit0 | release-offline-suite-final.log |

Production files are the existing reference prediction anchor source/header,
game movement policy header and Half-Life policy source, live runtime stage
source/header and normal app composition. Only two existing test translation
units and this existing task report accompany them. There is no parallel
movement/physics system or duplicate roadmap.

New-build manual verdict remains **not_run**. Separate collider335 and large
server-correction attribution remain unverified, even if the reproduced
reliable-carrier defect passes all offline regressions.

The prepared normal Release client is4548096 bytes, SHA256
`74D82864CCEA83B4F81F0FFC0765244C9419D08CCADA441D01ED8C95CE03456E` at
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
The unchanged launcher SHA remains
`1CD952F32F1F46BD462EFDB5985C512579AB1E209A31076408E4487D4E3A51E4`.
`verify-launcher-selection.ps1` successfully ran the actual unchanged
SourceOnly/CheckOnly path and resolved its `ClientPath` to that client/hash.
The GUI selects the sibling `hlclient.exe` in `apps/hlclient_launcher/main.cpp`.
Release rebuilt32 executable/test targets while excluding the open, unchanged
launcher; Debug and HL1-OFF core-only ALL builds passed, VS2022 v143 Win32 /WX.
No successful ASan runtime is claimed: no new ASan build/run was attempted,
and the historical runtime/loader limitation remains unconfirmed.

Final preservation audit verifies1384 original source files and their saved
copies,0 missing/unexpected files and only10 scoped changed files (seven
production files, two existing test TUs and this existing report). SDK, manual
wrapper and user configuration remain unchanged. Branch remains
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`, staged set empty;
`git diff --check` passes. No other worktree, Steam installation or server was
changed. Agent live sessions and staging/commit/push:0.

For the next user check, use the same normal launcher and `.104:27016`; no
configuration change or full-arsenal retest is required. Run normally, then
optionally run and fire. Existing diagnostics remain enabled. At a residual
jerk press F8 and close normally to retain the incident. This verifies the
new build; it does not retroactively convert offline proof into a LAN pass.

## Whole-session smoothness recording follow-up (2026-10-12)

`attribution=user_report`: large jerks are no longer noticeable after the
reliable-movement fix, but the game still sometimes feels uneven. This is
partial manual feedback, not proof that all prediction or frame pacing is
correct. The user requested recording a normal play session for later analysis.

The existing E32 report retained only four seconds of recent frames and eight
bounded incident windows. It could not describe an entire long session, and
automatic incident slots could fill early. The normal application now feeds
accepted, sanitized observations into `MovementSessionRecorder`, owned by the
same receipt/diagnostic recorder. The existing launcher checkbox enables this
automatically. Its help now says that the whole session is recorded and F8 is
optional. No extra command-line flag or special executable is needed.

The additive `session_summary` object in `external-movement.json` retains:

- Whole-session frame, CPU-stage, present-wait, GPU and coherent-state-age
  distributions, including approximate histogram p50/p95/p99 upper bounds and
  exact observed maxima. Inclusive CPU stages must not be summed. State age
  is not ping, packet loss, server CPU load or bot causality.
- Separate focused/captured/alive/control-ready, background, inactive/dead and
  unknown contexts. An interval crossing a context boundary is excluded from
  the controlled-play frame distribution while remaining in the session total.
- Prediction active/inactive/unknown observations, fallback entries and
  bounded reason categories. Correction samples follow advancing observation
  ordinals; repeated render snapshots are not new corrections. Intermediate
  unobserved ordinal advances, resets and missing identities are explicit.
  Camera jump remains a labelled sticky snapshot; a rejected large correction
  can appear separately as raw error at fallback entry.
- At most 512 initially one-second bins. Deterministic pair compaction doubles
  their width as the session grows, preserving counts/maxima and whole-session
  coverage; gaps do not manufacture observations. F8 counts remain on this
  timeline even after all detailed incident windows fill.
- Cumulative owned-peer receive counters as observed deltas with initial
  baselines/resets, and delayed GPU results bound to original frame/time/context
  through a 1024-identity ledger. Pending/unmatched GPU data remain unavailable.

The original detailed-record allocation stays within 8 MiB, unchanged frame
layout. The new aggregate allocation has its own 512 KiB bound, separately
reported with the combined total. The aggregate JSON is bounded at 2 MiB and
the complete existing report still at 16 MiB. Disabled collection allocates
nothing. Collection performs no file I/O or per-frame JSON formatting; normal
close uses the existing atomic identity-bound output. Force-killing a process
does not promise a complete report. There is no automatic causal verdict from
the thresholds and no change to prediction, collision, network or server rules.

`scripts/analyze_movement_session.ps1` reads an explicit report/run or the newest
movement report under the ordinary local run directory. It validates schema,
identity, bounded UTF-8 JSON and reports measured facts in Ukrainian, including
controlled-play distributions and noteworthy time windows. Legacy reports are
explicitly identified as recent-window-only. It launches nothing and writes
nothing. Project-owned fixture tests cover both schemas and rejected input.

Preservation and verification evidence is under
`manual-artifacts/task-records/e32-session-smoothness-20261012`. Before edits,
1384 source files, 36 Release files, three configuration files and the previous
22-file LAN evidence set were copied and hash-verified. Current endpoint remains
`192.168.0.104:27016`; diagnostics were already enabled in the saved settings.
New recording build: `manual=not_run` until the user actually plays it. No new
agent live session, Steam/HLDS launch, server mutation, staging, commit or push.

### Recording follow-up verification and ready image

Release, Debug and HL1-OFF core-only ALL builds passed under VS2022/v143 Win32
with warnings as errors. Targeted Release and Debug diagnostics/receipt/launch
tests each passed 43 cases / 56,635 assertions. The new session/integration
selection passed seven cases / 2062 assertions, including export through the
ordinary `MovementIncidentRecorder::encode`; the PowerShell analyser then read
that production JSON and verified 1400 accepted frames/whole-session coverage.
The analyser's own isolated fixtures passed 34 checks. Core-only CTest passed
44/44, including the architecture guard and 348 core API cases / 312,230
assertions. These are offline fixtures, not a new LAN listening/play test.

The new heap owner is 459,960 bytes on the acceptance target. A densely populated
512-bin summary encoded to 1,313,626 bytes; the existing combined detailed-report
stress fixture encoded to 11,747,521 bytes. Detailed-frame size is unchanged at
1656 bytes and its maximum allocated capacity remains 8,387,200 bytes. Release
collection microbenchmark, with builds complete: On p50/p95/p99/max =
1.4/1.5/1.8/16.1 microseconds; Off = 0/0.1/0.1/0.2 microseconds at this timer's
resolution. This measures collector calls only, not all live CPU/GPU/output
overhead or user-perceived smoothness. No new ASan runtime test was performed;
the historical unconfirmed runtime status remains unchanged.

Ready normal Release `build/bin/Release/hlclient.exe` SHA256:
`F0C8B7794F26C1E800B739B7286AEED552D771922A3040955913B212E6DD6AB9`.
The updated launcher (help text only) SHA256:
`AF58350DA40A7B1C0885A33A59EB96CEE8D0E33696CD51D3DA87617203C8E62A`.
The unchanged manual launcher SourceOnly/CheckOnly resolved that exact client.
Saved launcher settings remain byte-identical. The preservation audit verified
1384 saved originals, ten scoped modified source/doc/build files, five new
recorder/test/analyser files, no missing originals and no staged files;
`git diff --check` passed. SDK revision remains
`b1b5cf5892918535619b2937bb927e46cb097ba1`.

User recording procedure: open the ordinary launcher, keep «Діагностика плавності
та ліфтів» checked, play normally for 5–10 minutes or longer, then close the
client normally and report completion. F8 is optional. The same run directory
under `%LOCALAPPDATA%/HLClientEngine/runs` receives `external-movement.json`;
the retained summary can be analysed without copying files or rerunning a game.

After the final production edits, the complete Release offline acceptance ran
3576 registered tests: **3511 passed, 65 explicitly skipped, 0 failed**, in
643.62 seconds. Optional installed-resource/device/capability cases retained
their existing skips. The ordinary no-stock close/verdict test passed in
122.98 seconds. Subsequent version/help/Null bootstrap, synthetic UDP transport,
functional-log checks, actual no-stock manual-wrapper fixtures and
SourceOnly/CheckOnly all passed; `FULL_RELEASE_EXIT=0`. The fresh evidence is
`release-offline-suite-final.log` in this follow-up's task record.

## Crossfire tunnel and outer UDP split follow-up (2026-10-12)

Attribution=user_report: while running through the Crossfire tunnel the user
experienced jerks followed by client exit. The identity-bound latest run is
`b911557a5d42e399fe3e9bf073793d35`, receiving entity 13, normal client SHA256
`F0C8B7794F26C1E800B739B7286AEED552D771922A3040955913B212E6DD6AB9`.
The recording survived: at 94.285..94.981 seconds the final recent window
contains 5 `player_allsolid` frames and 56 `solid_start` frames; the latter
identify the world and seed-position query. Prediction recovered before the
95.966-second fatal `unsupported_special_packet` exit (code 2), caused by an
exact-peer 1400-byte datagram. This is a deliberate protocol terminal, not a
native exception. The two observed failures have independent production fixes.

The read-only installed Crossfire structural probe established that standing
hull position (-160,-173.2109375,-1819.96875) terminates in solid at plane 446,
normal (1,0,0), distance -160; (-160.00390625, same y/z) is empty, while moving
the other way remains solid. The final seed at y=-6.7890625 has the same tie.
The parser/query's front-child-on-zero rule is preserved. Reference seed and
movement-start validation now share `resolve_movement_boundary_position`:
at most 26 neighboring binary32 positions, at most one ULP on each axis and
1/64 unit total distance, selecting the closest full-scene empty hull. The
initial blocker must be BSP world/brush; entity-box blockers and real embedding
retain failure. Canonical seed fields are untouched; `queried_origin` reports
the derived physical query. This is a narrowly bounded local-compatible
precision recovery, not a complete stock PM_CheckStuck implementation.

The existing live driver's opt-in public UDP split envelope assembles all parts
before ordinary codec/state admission. It is separate from reliable slot-0
fragmentation; strict historical callers retain the fail-closed profile. Bounds
and unconfirmed server-layout evidence are recorded in GOLDSRC_NETCHAN.md.
Normal external diagnostics additionally retain bounded numeric split totals.
No received payload, identity value, authentication bytes or native asset paths
are added to runtime logs.

Before edits, task record `manual-artifacts/task-records/e32-tunnel-split-20261012`
preserved and hash-verified 1389 source files, 36 Release files, three settings/
build-cache files and the latest 22-file LAN evidence set. The running launcher
is not selected for overwrite. Endpoint remains 192.168.0.104:27016 and saved
user video/audio/diagnostic settings remain user-owned.

Verification is recorded below. New game,
Steam/HLDS or managed/LAN gameplay sessions = 0; the one bounded read-only
server rules query received no response. Staging/commit/push = 0.

### Tunnel/split verification and ready normal client

The final VS2022/v143 Win32 Release, Debug and HL1-OFF core-only builds pass
with warnings as errors. Focused Release tests pass 11 cases / 1135 assertions,
including the preserved blocking-plane tie, a derived one-ULP clear seed, 200
successive wall-tangent commands, a rounded movement-start recovery, genuine
embedding rejection and a complete-scene entity-box veto. The canonical BSP
tie remains blocking: no old point-contents assertion or collision rule was
weakened. World-only adapters legitimately omit per-object hit metadata; their
validated model-zero-only profile supplies that proof. Other adapters must
retain an explicit BSP hit. Test failures during initial integration were
corrected and retained in the task record, not concealed or accepted as passes.

UDP fixtures exercise 1400-byte parts, reordered/identical duplicate parts,
no partial admission/ACK, one original owning payload with exact decoded bytes,
duplicate logical delivery suppression and an inner datagram larger than the
ordinary physical receive buffer. Conflicts, missing parts/fixed expiry,
two-transfer eviction, wrong endpoints, invalid indices/empty parts, nested
special packets and the 16-KiB logical cap preserve a viable ordinary channel.
These are independently authored offline fixtures; they do not establish an
uncaptured live server's complete split layout.

Final Debug movement/netchan/diagnostic selection passes 218 cases / 623166
assertions. Core-only CTest passes 44/44, including the architecture guard and
349 core API cases / 313050 assertions. The approved existing collision checker
successfully imports the installed Crossfire through its ordinary approved
source/parser/builder (7004 planes, 10377 clipnodes, structural hash
`8a61ea901c79eced131c8ce8b1f2bd096281cc67dc79da00b42988fe64733dfb`).
The separate sparse structural probe records the logged coordinates/plane tie;
it is not a game session or a replacement runtime collision implementation.

Ready normal Release `build/bin/Release/hlclient.exe` SHA256:
`7B2FA25A157E86D753CFE7A0B0282CF398889C7E3DDF424FD84534EED8351D2D`.
The running/unmodified launcher SHA256 remains
`AF58350DA40A7B1C0885A33A59EB96CEE8D0E33696CD51D3DA87617203C8E62A`.
SourceOnly/CheckOnly resolves precisely that normal client. No new option or
debug/demo executable is required. After the last production edit, the complete
Release offline suite passed **3514 cases, 65 explicit optional skips, 0 failed**
out of 3579 registrations, in 708.98 seconds. The ordinary no-stock normal-close
verdict passed in 112.31 seconds; subsequent version/help/Null, synthetic UDP,
functional-log, actual no-stock wrapper and SourceOnly/CheckOnly checks passed,
with `FULL_RELEASE_EXIT=0`. ASan runtime remains
historically unconfirmed; no infrastructure change or new ASan claim is made.

The final preservation audit verified all 1389 saved originals, 16 scoped
existing source/doc/test changes, no new/missing source files, unchanged saved
launcher settings and executable, no staged files and a passing `git diff
--check`. Branch/HEAD are unchanged, as is the clean SDK at
`b1b5cf5892918535619b2937bb927e46cb097ba1`. The complete logs, sparse map probe,
launch selection and final audit remain in the ignored task record above.

At the ready-image handoff, fresh build manual status was `not_run`. Open/use the ordinary launcher, retain
the enabled whole-session movement diagnostics, connect to 192.168.0.104:27016,
and run through the same tunnel. Normal close or a typed failure retains the
movement and protocol diagnostics, including outer-split numeric totals.

### Subsequent user sessions: uneven-ground audit (2026-10-12)

Attribution=user_report: the user reports remaining jerks while running over
uneven ground, and asks to inspect the two latest sessions. Both identity-bound
receipts use the ready `7B2FA25A...` client, Crossfire, receiving entity 13 and
192.168.0.104:27016. Run `65c77508bdcfcf7fe30f12ecde378e89` (20:13:06..20:14:37)
and `a3cbec58694ab7625599634400e60d9e` (20:15:38..20:15:55) both end with exit 0,
window_close, normal_close_completed and transport acknowledgement. No crash,
protocol or cleanup failure is observed. The first run receives four UDP split
parts and completes two packets, with zero rejection/duplicate/expiry/eviction;
the second receives none. This is positive observed compatibility for the
actual server's delivered split envelopes, not universal stock parity.

The longer 89.779-second recording contains 8086 controlled-alive frames, 14
fallback entries and 740.353 ms of observed inactive prediction intervals;
the shorter 16.223-second recording contains 897 controlled-alive frames, one
entry and 82.489 ms (these intervals also include fresh-life waiting).
Abnormal longer-run categories include movement_trace_failed (5 frames),
solid_start (10), large_unclassified_server_correction (22),
ground_flag_disagreement (7) and player_allsolid (26). Fresh-life waiting
accounts for 31 further controlled frames; the shorter run has six
solid_start frames and seven fresh-life-wait frames. Categories alone are
observations, not proof that every episode has the same cause.

Detailed examples: at 8.023 seconds in the short run, a server-box support with
entity 329/model slot 156 immediately precedes seed-position solid_start for
that same box on sloped world terrain. At 47.008 seconds in the longer run,
entity 347/model slot 156 similarly precedes solid_start, on a flat world plane.
Box bounds/owner and virtual model name are not retained, so the object class
and exact overlap depth cannot be claimed from these reports. The previous
BSP-only one-ULP repair intentionally does not bypass these boxes.

At 14.917 seconds in the short run, landing after a fall onto world plane 5518
(normal 0.08304548,0,0.99654573) corrects 9.044 world units, mostly vertical
(-8.8945). This is a landing transition, not evidence of a 9-unit error on every
running frame. At 60.996 seconds in the longer run, after changing support
between sloped planes 5518/5472, a 27.524-unit server correction triggers
fallback. Nearby world contact and other-player interaction cannot be
distinguished conclusively from the retained fields. A separate earlier
moving-brush-support episode involves entity 67 and support_clearance failure.

Frame pacing also contributes: controlled-alive intervals exceed 33.334 ms on
11 / 4 frames (long / short), maxima 41.560 / 42.407 ms, with none over 50 ms.
A retained short-run frame at 8.963 seconds takes 37.788 ms, CPU update 33.578,
parse/commit 8.465, prediction 6.780, Studio 7.385 and GPU 6.012 ms; nested CPU
stages must not be summed. Most detailed incidents late in the longer run were
not retained after the six automatic slots filled; whole-session aggregates
still cover them. No F8 marker is present. This audit changes no production
code, settings or server state and starts no new game session.

## Uneven-ground/contact correction follow-up (2026-10-12)

The user authorizes production fixes after the two-session audit above.
Task record: `manual-artifacts/task-records/e32-uneven-ground-20261012`.
Before changes, 1389 source files, 36 ready Release files, three configuration/
build-cache files and both 22-file user session evidence sets were copied and
SHA256-verified. Branch/HEAD, prior dirty edits and user settings are preserved;
the running launcher is not selected for overwrite. Source/readiness and
compatibility claims below are distinct from fresh manual listening/play proof.

Root cause A: slide contact clearance rejected a binary32 endpoint rounded
inside BSP solid, retained the pre-impact origin, and nevertheless consumed
the clipped collision velocity. The independently authored absolute-coordinate
sloped BSP fixture reproduces a 9.062-unit retained-altitude error (the user's
landing event measured 9.044). Before production fixes, this fixture fails
grounded/height assertions. Reference slide and ground categorization now use
one outward binary32 step along the witnessed contact normal only if its total
distance is <=1/64 unit, the complete scene reports empty/free, and a hull sweep
from the original free start is clear. Original hit identity, plane/fraction,
time consumption and velocity response are preserved. World point-content
rules, global epsilons, server trace and synthetic dry-walk behavior are not
rewritten. This is endpoint publication repair in the existing kernel.

Root cause B: generic entity origin/mins/maxs and receiving-client origin use
different public delta grids (pinned SDK network/delta.lst: entity bounds/grid
8, clientdata origin/grid128). A canonical coherent grounded seed can overlap
a reconstructed server box near its boundary; the previous adapter immediately
suspended prediction. A separate project-owned point-box fixture reproduces
that failure. The reference seed adapter now searches only the six faces of
the exact blocking server box, moves at most1/8 unit to the closest eligible
face, and requires full-scene empty/free occupancy, a reverse hull sweep that
hits that same box, and a valid downward support witness. Deep/airborne
penetration, other blocking boxes and invalid queries remain rejected. The
1/8 envelope is an explicit local-compatible limit, not a stock epsilon or
proof of every negotiated server quantization. Canonical wire fields remain
unchanged; queried_origin and diagnostic query endpoints describe the actual
derived query. The exact model/class/bounds of prior entities329/347 were not
recorded, so their individual overlap depths remain unproven; no model-slot or
classname exception was introduced.

Root cause C: Half-Life selected SOLID_BBOX2 only. The official pinned SDK
dlls/client.cpp preserves ent->v.solid in full-pack state and initializes
player baselines with SOLID_SLIDEBOX3. The existing server-box layer could
therefore omit server-described player blockers. Half-Life now explicitly
selects code3 through server_player_solid_code; only exact player-schema
observations with complete negotiated origin/mins/maxs are admitted. Receiving
entity is excluded; unavailable bounds produce a typed failure rather than
invented dimensions. Other games/core default to no player selection. Existing
owned-item filtering remains for generic boxes, not player ownership. This
fixes a known collision-coverage gap; it does not establish that every prior
unclassified27-unit correction was caused solely by another player.

Support handling: generated moving-brush boundary positions now reuse the
existing full-scene one-ULP recovery in all axes, not only a z nudge; the
contact-position vector follows the exact displacement. A reference-v2
post-command airborne server state stays airborne when the short local probe
finds nearby ground, instead of suspending on that disagreement; the next real
command performs ordinary ground categorization. Strict historical/default
ground-flag assertions retain their behavior. No carrier/ACK rule, command
msec/speed, ammo/health/damage, server file or renderer/mixer was changed.

Validation progress: the two primary red fixtures fail before fixes (3 failed
assertions /14). Final focused Release passes5 cases /2233 assertions, covering
240 uphill/downhill commands after the logged landing,200 commands through two
intersecting shallow slope directions, free hull publication, shallow/deep/
airborne/obstructed box seeds, strict-vs-reference airborne categorization,
explicit/off/nonplayer/self player-box policy, moved-box lifetime replacement,
coarse-boundary repair and movement that still cannot cross another player.
Neighboring Debug tests pass401 cases /446922 assertions. Core-only CTest
passes44/44 including the architecture guard and354 core API cases /315283
assertions. VS2022/v143 Win32 Release, Debug and core-only builds pass /WX.
After the last production edit, the complete Release offline suite passes
**3519 cases, 65 explicit optional skips, 0 failures** out of3584 registrations,
in745.32 seconds. The ordinary no-stock close/verdict case passes in124.44
seconds; subsequent help/version/Null, synthetic UDP, functional-log and actual
no-stock wrapper/SourceOnly/CheckOnly checks also pass (`FULL_RELEASE_EXIT=0`).
Historical ASan runtime remains unconfirmed; no new ASan runtime claim is made.

Ready normal Release SHA256 (unchanged launcher selection):
`04EC1BFAC221DE7E9D77BBBCBC1BB1E3C21CB41B9B486A826FB602F2B08B82A0`.
Fresh manual=`not_run`; no new agent game/stock client/HLDS/LAN sessions,
staging, commit or push. User can test the ordinary launcher on
192.168.0.104:27016 with whole-session movement diagnostics enabled, using the
same landing, uphill/downhill ground, slope join and close-player contacts.

The final source audit verifies all1389 saved originals,12 scoped existing
source/doc/build changes and one new project-owned test file; no originals are
missing. Saved launcher configuration and launcher executable remain unchanged;
manual SourceOnly/CheckOnly selects the ready04EC1BFA... normal client. Branch,
HEAD and the clean pinned SDK remain unchanged; `git diff --check` passes and
no files are staged. Full evidence remains in the ignored task record above.
The incident recorder/frame size and normal E3 audio/Studio/weapon paths were
not replaced. These changes fix reproduced contact/collision defects; they do
not constitute a measured live CPU/GPU frame-pacing improvement or universal
stock PM_Move/pusher parity. Fresh live verification of this image is pending.

## Gib-contact follow-up (2026-10-12)

New user report: jerks while running over gibs, attribution=user_report. The
latest run f2c9c5ac26645c9d8d1af3b172f95055 actually used04EC1BFA... on crossfire,
endpoint192.168.0.104:27016. It closed normally with exit0 and confirmed graceful
disconnect; it is not another crash. Over40.104 recorded seconds, the
controlled_alive cohort had2987 frames,9 fallback entries and330.746ms observed
inactive prediction. Only3 controlled frames exceeded33.334ms and none50ms.
At12.549740s a coherent server seed is rejected as solid_start by box297,
after local support on entity273/modelslot156. At14.923279s a separate raw
22.112-unit correction is unclassified. The old diagnostic does NOT record
the virtual model/bounds/classname of those boxes; slot156 is not an asset
identity and these logs alone do not prove that all corrections are gibs.

Scoped task record: `manual-artifacts/task-records/e32-gib-collision-20261012`.
1390 source files,36 ready Release files,3 configuration/cache files and22
latest-run evidence files were preserved and SHA256-verified before edits.
Prior dirty work, branch/HEAD, running launcher and saved settings are retained.

Confirmed source defect: the generic BBOX builder admits zero-relative-size
objects as player Minkowski obstacles. A point near the floor therefore gains
a full player-hull exclusion volume, becomes a spurious support or rejects a
canonical seed. Pinned official Valve SDK revision
b1b5cf5892918535619b2937bb927e46cb097ba1, `dlls/combat.cpp` CGib Spawn/
SpawnRandomGibs/SpawnStickyGibs and `common/const.h`, supplies the actual
zero-size gib and movetype profiles. These references are read-only behavior
evidence, not copied implementation or a claim about proprietary PM physent
internals. The production correction is explicitly local-compatible.

Half-Life selects6 exact pairs: models/hgibs.mdl, models/agibs.mdl and
models/germangibs.mdl with BOUNCE10; models/stickygib.mdl with BOUNCE10, TOSS6
or NONE0. Only nonplayer BBOX2 entities with complete finite origin/bounds,
all six relative bounds exactly zero, current advertised model name and
matching negotiated movetype are excluded from this prediction scene. Unknown
names, absent model/movetype, nonzero bounds, player-schema boxes and invalid
geometry do not gain an exemption. At most8 validated rules,128 real boxes
and the existing8192 input-entity limit remain bounded. No hard-coded model
slot, disk search, asset opening, server mutation, broad point-box bypass,
kernel epsilon change or deeper seed recovery is introduced. The visual gib
entity and canonical observation are not removed or rewritten; map/session
resource identity and same-frame box replacement remain the existing owners.

Verification and ready-build receipt are appended after the final production
edit and complete offline runs. Fresh manual result for this correction is
not_run until the user tests the new executable.

Red evidence before enabling the filter: Release `[e32-gibs]` has3 cases,
1 passed/2 failed,59 assertions/2 failed. Both failures specifically require
the selected point debris not to become a blocking box. No old assertions are
weakened. The new neutral fixture also compares160 ordinary movement commands
(both slope directions and jumps) against the same floor without debris, and
tests exact names/types, unavailable fields, player/nonzero/tiny bounds,
owner filtering, pool/entity reuse and invalid bounded policy. The actual HL
module test exercises all6 selected pairs with retained canonical entities.
An opt-in numeric `excluded_point_boxes` observation is added to the existing
bounded incident recorder, plus a final snapshot count; it is a policy outcome,
not visual proof or an inferred server classname. No per-frame console spam
or native asset path is logged.

Core-only final verification: VS2022/v143 Win32 ALL_BUILD passes;44/44 CTest
checks pass, including architecture guard and no-stock map/session analyzer.
All357 shared API cases pass317113 assertions without Half-Life linkage. The
incident record remains1656 bytes, retained capacity8387200 bytes (below the
unchanged8MiB limit), and whole-session aggregate capacity459960 bytes. A first
attempt using a wider optional count was correctly rejected by the existing
static memory guard; the final uint16 count uses alignment padding and an
explicit65535 unavailable sentinel, serialized as null. Values above the
8192 observed-entity bound are rejected by diagnostic sanitation. No guard,
retention duration, frame capacity or old assertion was relaxed.

Normal final Debug build passes /WX; selected movement/prediction/diagnostic/
game-module/Netchan/D4 regressions pass513 cases /522127 assertions. Final
focused Release passes72 cases /60369 assertions, with1 explicit optional
installed Satchel asset skip (no exact approved read-only root opt-in).
Actual normal launcher SourceOnly/CheckOnly selects this Release image:
`93358D219D925687F87FCE5BB4773D226AFBAC421FF87122027C008F8DCA6761`.
The unchanged launcher remains
`AF58350DA40A7B1C0885A33A59EB96CEE8D0E33696CD51D3DA87617203C8E62A`.
Final full Release offline acceptance was repeated after the last production
edit; its completed outcome follows, not inferred from these subsets.

Final acceptance:3588 registered tests,3523 passed,65 explicit optional skips,
0 failures,1022.64 seconds. The normal no-stock close/verdict test passes in
126.18 seconds. Subsequent version/help/Null, synthetic UDP, functional-log,
actual no-stock wrapper and SourceOnly/CheckOnly checks also pass;
`FULL_RELEASE_EXIT=0`. Historical ASan runtime remains unconfirmed, unchanged.
No installed-resource/device skip is described as a positive live result.

Source audit verifies all1390 saved originals and all36 rollback Release
copies. Exactly19 existing source/doc/test files changed, no new or missing
source files, no staging, branch/HEAD unchanged, pinned SDK clean. The ready
93358D21... client, unchanged launcher and saved launcher configuration remain
identity-bound; no Steam/game/server/cfg file was changed. New agent live game,
stock client, HLDS, LAN sessions, commit and push:0. Fresh manual=not_run.

Handoff: use the same normal Release launcher at192.168.0.104:27016 and run/
jump over visible human gibs, including on uneven ground. Actual gib visuals,
audio and server authority remain unchanged; real mines/player/brush collision
is still enabled. If a jerk remains, record the same diagnostic session/F8
near it; selected-point-box counts are bounded incident observations, not
proof that all other unclassified corrections or CPU/GPU pacing are solved.

## Automatic FPS-episode logging follow-up (2026-10-13)

The user asked what19 frames over33.3ms/max42.25ms mean and requested logging
FPS dips. These are individual observed frame intervals, not a sustained24FPS
period:1000/42.25 is about23.67FPS-equivalent for that interval. The last
1f3f4cd46084372ad223b566840e482e session used the preceding45B5B5DF... image
and its existing complete frame distributions. It does not retroactively
contain the new episode detail. No FPS optimization or prediction change is
part of this logging task.

The existing `MovementSessionRecorder` now adds `session_summary.frame_pacing`
(`hlclient.frame-pacing.v1`) in the same identity-bound `external-movement.json`.
No second profiler, log stream, audio/renderer path or hot-loop file I/O is
introduced. The ordinary launcher checkbox «Діагностика плавності та ліфтів»
already enables the collection; no F8, hidden flag or demo executable is needed.

Profile and limits:

- An eligible frame interval strictly greater than20ms is a slow frame
  (below50FPS-equivalent), not proof of falling from a requested FPS target.
  Adjacent slow frames form an episode. Exactly20ms is not a threshold crossing.
- Both endpoint contexts must be focused/captured/alive/control-active in the
  same life/network/map, with consecutive diagnostic frame numbers. Startup,
  focus/capture/death/life/generation transitions and missing intervals do not
  fabricate gameplay FPS episodes. Missing frames break continuity explicitly.
  Long real stalls, including intervals over250ms, remain recordable; the
  separate movement-fallback attribution cap is not an FPS suppression rule.
- All eligible slow-frame counts, interval sums and episode counts survive
  the whole session. At most64 episode details are retained, ranked by worst
  frame interval, then sum of slow intervals, then earliest ordinal. Omitted
  detail is counted, not hidden. Old automatic incident slots cannot suppress
  this summary. Ordinary timeline compaction does not erase retained episodes.
- Details retain begin/end local-clock stamps, first/last/worst frame numbers,
  generation identity, slow-frame count, wall-clock span, measured interval sum,
  maximum frame time, reciprocal FPS equivalents and typed end/recovery status.
  Encoding an active episode is non-mutating and does not claim observed
  recovery. A reciprocal of mean frame time is not an average of per-frame FPS
  values or a measured one-second FPS counter.
- CPU/Studio/render/present/network/prediction/collision/report stages come
  from the original worst frame, not independently chosen peaks. Delayed GPU
  timing updates only its matching original frame identity. Unavailable or
  expired GPU data stay null; duplicates cannot replace a measured sample.
  These timings may overlap and must not be summed into a bottleneck verdict.
  Numeric player/entity observations are not bot count or visibility proof.

The existing8MiB detailed-record limit and1656-byte frame layout are unchanged.
The aggregate allocation remains below its512KiB bound (479752bytes measured
offline), with no per-observation allocation; aggregate/combined encodings
remain bounded at2MiB/16MiB. Disabled diagnostics still allocate nothing.

`scripts/analyze_movement_session.ps1` now reports FPS equivalents and the ten
worst retained FPS episodes, including same-frame CPU/GPU measurements. JSON
output exposes the bounded episode list. It derives FPS from validated durations,
whitelists fields and validates policy/bounds/continuity/end status. Old reports
retain their original distributions and explicitly say episode detail was not
recorded. The analyzer launches nothing and writes no input. Project-owned
fixtures currently pass50 analyzer checks; C++ tests cover grouping, exact
threshold, original-frame GPU correlation, context/life/generation/missing
boundaries, long stalls,64-entry overflow, exhausted incident slots and
non-mutating partial encoding. No existing assertions were weakened.

Source/image/configuration/current-run evidence was preserved through the
existing mechanism in `manual-artifacts/task-records/e32-fps-drops-20261013`:
1391 source files,37 Release files,3 configuration files and22 current-run
files, all SHA-verified. Final build/test/image evidence is appended after
acceptance. Launcher/server/Steam/assets, graphics settings, frame cap, VSync,
prediction and all gameplay authority remain unchanged. New agent live game/
stock client/HLDS/LAN sessions=0; stage/commit/push=0; new image manual=not_run.
ASan runtime remains historically unconfirmed.

### FPS logging final verification and ready image

VS2022/v143 Win32 /WX Debug and Release builds pass. The final targeted suites
each pass83 cases /107506 assertions, with one explicit installed-resource
opt-in skip. All44 core-only CTest registrations pass;369 shared API cases
pass320106 assertions without HL1. All50 analyzer fixture checks pass. A
read-only reanalysis of the preceding user run derives103.2956FPS-equivalent
from controlled-play mean frame time and23.6712 from its single worst interval,
and explicitly marks the new episode detail as absent in that older image.

Full Release acceptance is not represented as an uninterrupted green run:

- First complete run:3605 registrations,65 optional skips,3534 passed,6 failed,
  1158.29s. Two cases timed out (Snark34.37s, normal-close/verdict180.32s), two
  processes exited0xC000012D, and two cases did not start (BAD_COMMAND). The
  installed Windows SDK `ntstatus.h` names0xC000012D `STATUS_COMMITMENT_LIMIT`.
  This identifies those process-exit codes, not the cause of the user's FPS.
- Identical code/image/limits repeat:3538 passed,65 skips,2 failed,929.01s.
  Manifest timed out60.49s; the synthetic PowerShell5.1 publication roundtrip
  failed its exact auxiliary-datagram check. The previously failed four
  snapshot/Snark launch cases pass0.04s each, Snark passes0.24s, and unchanged
  normal-close/verdict passes106.07s. All new FPS cases pass in both runs.
- Unchanged failed-subset CTest retry:2/2 pass,22.42s; manifest12.24s and
  PowerShell5.1 roundtrip7.96s. The earlier direct Snark retry also passes96
  assertions in0.1813s. Assertions, test deadlines and source were not weakened
  or changed to obtain these retries. All original/repeat logs remain separate;
  no single full-suite0-failure result is claimed. FPS/targeted/core validation
  is green; complete-run instability is an explicit qualification.

The existing post-CTest version/help/Null, synthetic UDP, functional-log,
summary, no-stock wrapper, analyzer and SourceOnly/CheckOnly steps are executed
separately because the non-green full runs stopped before those steps. Final
source/configuration/image audit remains in the task record. No ASan runtime
or real-device live FPS result/stock parity claim is introduced.

Ready ordinary client:
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`, SHA256
`CC4E64AF565F338CD863DB27CFA5B40471E46D8D8D903FF9441A53872889A178`.
Unchanged launcher SHA256:
`AF58350DA40A7B1C0885A33A59EB96CEE8D0E33696CD51D3DA87617203C8E62A`.
Normal launcher selection matches that exact client; saved diagnostics are
already enabled. For the next record, use the same ordinary launcher/server,
play normally and close normally. F8 is optional. The new episode data first
appears in a new run, not in the already-completed user session. Manual=not_run;
new agent live/stock/HLDS sessions, staging, commit and push remain0.
