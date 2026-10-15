# E15 — MP5 primary-fire presentation

## Automatic reload follow-up — 2026-10-14

`attribution=user_report`: an empty MP5 reloads without animation, while R works.
The submission path recognized only the R edge; repeated automatic reloads can
retain clientdata animation 3, so no new animation edge was observed. The existing
server-observed magazine-reload path now also accepts a fresh MP5 `in_reload`
transition, sequence 3/body 0, replacing the previous firing pose. Manual R
timing, server ammo, exact confirmation and marker deduplication are retained.
`[mp5-auto-reload]` covers repeated cycles, held/released attack, marker PCM,
duplicate confirmation and invalid/stale/dead/no-reserve observations.
Evidence: `manual-artifacts/task-records/crossfire-presentation-repairs-20261014`.
This follow-up's new executable remains `manual=not_run`.

Normal application integration; primary only. E16/M203 deferred.
E15 manual=not_run. New live client/HLDS/Steam/WFP/capture sessions=0.
Staging/commit/push=none.

Subsequent E16 implementation is recorded separately in
[MP5/M203 secondary](MP5_M203_SECONDARY_E16.md). The historical E15 scope,
manual status, baseline counts and tracer tuning below are preserved.

## Baseline and subsequent user evidence

Worktree `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. Existing dirty/untracked work
preserved. Pre-edit hash-verified snapshot: 1,197 source files; baseline
Release: 46 passed, 1 skipped, 22,114 assertions.
Evidence: `manual-artifacts/task-records/e15-mp5-20261002/`.

E14 subsequent acceptance “перевірив все ок”: manual=passed,
attribution=user_report. The historical E14 report's not_run is unchanged;
teamplay/reconnect manual=not_run. This does not imply MP5 acceptance.

## Reference and approved asset evidence

Official pinned SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`:
`dlls/mp5.cpp`, `dlls/weapons.h`, `cl_dll/ev_hldm.cpp`,
`cl_dll/hl/hl_events.cpp`, weapon timer handling. No SDK implementation copied
or submodule update; no claim of proprietary stock-binary parity.

Read-only actual models through RuntimeReplayLocalAssets approved importer:

| Asset | Actual imported findings |
| --- | --- |
| v_9mmAR | 25 bones, one unnamed attachment on bone 7/Bone09 at (-14,1,-0.5); 8 sequences, one bodypart/skin, 13 textures |
| p_9mmAR | 12 bones, no attachments; idle sequence; 310 emitted vertices on bone 11/Line02, 2 textures |
| shell | 2 bones, no attachments; shell sequence 101 frames/30 fps, 3 textures |

Viewmodel sequences: 0 longidle 41/8, 1 idle1 111/35, 2 grenade 34/30
(unused), 3 reload 47/30 (5004 markers 5/24), 4 deploy 12/12,
5/6/7 shoot 7/10 (5001 frame 0). Numbers are frames/fps. Profile validation
requires exact sequence labels/count and supported body 0/2. Mismatch is
unsupported, not Glock fallback. The separate opt-in approved-loader control
decoded hks1/hks2 with finite nonzero PCM; model mtimes unchanged. No assets
copied into the repository. `asset-final.txt`: 18 assertions, one passing case.

## Production ownership, bounds and limitations

Existing `hlclient_game_halflife` controller/audio/visual/event policy owns the
small firearm profile, MP5 semantics and cadence. GameClientAPI transports
owning sequence labels, opaque socket semantic keys and neutral line intents.
Asset host still selects server slots and evaluates the same merged Studio
pose. Existing WorldImpactPresentation traces/expires lines; renderer executes
numeric quads. No new weapon/audio/physics/prediction loop or concrete HL
dependency in lower targets. Core-only option remains
`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`; guard catches direct/alias/transitive/
generator/source/object/include violations.

Primary accepts existing submitted quantized commands at 0.1-second cadence;
not a render/network timer. Canonical readiness/timers/clip gate eligibility.
Clip-50 bounded provisional budget is presentation-only, never HUD ammo.
16 archived pending actions plus current action support batched fresh ACKs;
animation completion retains pending confirmation. Expiry, switch, life and
session resets bound ownership; exact replay creates no new action/effect/TX.
Reload uses sequence 3, 1.5-second local deadline and fresh server confirmation.
Deploy uses existing imported event/marker path, not an invented weapon loop.

Action hashing deterministically chooses shoot 5/6/7, hks1/2 and -2..2 visual
recoil: local-compatible, not SDK shared-RNG identity. Server nonzero punch
replaces it; command viewangles are untouched. Sounds use existing automatic
bounded voices, volume 1, pitch 94..109%; remote spatial attenuation 0.8.
There is no hks3 or second audio device.

Local muzzle uses imported attachment/5001 marker. Actual p_9mmAR has no
attachments: validated Line02 geometry/transform selects separate
local-compatible sockets, muzzle (-7.70,4.65,0.55), shell (0,4.25,-0.60),
basis forward (-1,0,0), right (0,0,-1), up (0,1,0), evaluated after merge.
Compatible explicit named sockets take precedence. Revision and opaque key
reject cached Glock/MP5 cross-binding. Known mismatches suppress flash/light/
shell; missing pose retains the existing diagnosed player-space fallback.
Local ejection reuses SDK-informed eye/basis offsets (20,-12,4), existing
shell.mdl pool, 2.5-second lifetime, point-sweep contacts/audio; not rigid bodies.

Tracer follows checked SDK post-increment `% 2`: shots 1,3,5… independent
per emitter and local MP5 profile. Focus/weapon switching does not reset local
cadence; life/map/session does. Remote occupant/visual-life boundary resets;
observed disappearance/re-arm may also reset under the existing E13 boundary,
not claimed as invisible-player stock parity. Frozen event fparams are used
without random replacement. Tracer begins at evaluated muzzle when available,
ends at static hit or ray endpoint: 0.06 seconds, width 0.65, 64 live lines,
128 recent identities. Depth test on/depth write off; state restored.
Tracer suppresses material audio/decal/particles; non-tracer uses E7/E11/E5.

Static presentation rays are normalized/bounded, not server hit/penetration/
player damage authority or exact proprietary spread. Moving brush blockers
retain existing unsupported policy. Player body animation, E13 corpses,
E14 ScoreInfo/DeathMsg and TE_BLOODSPRITE handling remain server-owned.
Offline GPU/PCM evidence is not human listening or live MP5 acceptance.

## Final verification

| Check | Final result | Log |
| --- | --- | --- |
| VS2022 / Win32 normal Release, helpers | Passed after last production edit | build-release-final.txt, build-release-test-final.txt |
| Focused Release E15 + owned Studio/GL camera test | 10 passed, 1 optional skip; 754 assertions | release-e15-final.txt |
| Affected Debug build and regressions | 255 passed, 2 opt-in skips; 183,161 assertions | build-debug-final.txt, debug-final.txt |
| Complete Release offline suite | 2,677 passed, 39 skipped, 0 failed of 2,716 results | release-offline-final.txt |
| Process-sensitive checks, serialized | 4/4 passed | release-serial-final.txt |
| Combined final Release | **2,681 passed, 39 skipped, 0 failed; 2,720 results** | preceding two logs |
| Approved installed MP5/shell metadata and hks PCM | 1 case, 18 assertions passed; read-only, no device | asset-final.txt |
| Core-only Release, actual HL module OFF | 41/41 CTest, guard included; core/API 195 cases, 7,669 assertions | build-core-final.txt, core-final.txt, core-api-final.txt |
| Existing ASan Debug core/API build/runtime | 29 cases, 1,635 assertions passed with official runtime PATH | build-asan-core.txt, asan-core-final.txt |
| Full ASan application runtime | not_run/not_verified; not inferred from core success | existing limitation retained |
| Launcher CheckOnly, reference/off and no-stock fixtures | Passed; actual runner/game not entered | launcher-checkonly.txt, launcher-fixtures.txt, fast-fixtures.txt |
| Source preservation | 1,197 snapshot files: 1,169 unchanged, 28 intended changed, zero missing; three new project files | source-preservation-final.json |
| git diff --check / staged diff | Clean / empty | final terminal verification |

Full-suite skips are explicit optional assets/capture/device/interactive SDL,
WFP/live-capability and reparse/link capability controls; no required E15
project-owned fixture was skipped. The approved installed control was run
separately, not enabled as a core/build dependency. Actual output-device
playback and human MP5 listening are not verified. Core-only's independent
TestGameClientModule and guard still use the same host/API, no HL fallback.
Source preservation change list excludes unrelated prior dirty files. New
project files: this document, firearm_profile.hpp and test_mp5_primary.cpp.

Final Release SHA-256 (3,597,312 bytes):

`65D6F808BADF41C1311F20EA22BD64A827668F30CEFD0FFD2606378EE75F0619`

Launcher remains unchanged (SHA-256
`7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`)
and resolves build/bin/Release/hlclient.exe; CheckOnly passed. Default
crossfire, reference/off, damage-respawn-check, duration/no-limit and peer
parameters were preserved.

Retained configuration commands (no clean/reset):

```powershell
cmake --build build --config Release --parallel 6
cmake --build build --config Debug --target hlclient hlclient_tests --parallel 6
cmake --build build-core-g1 --config Release --parallel 6
ctest --test-dir build-core-g1 -C Release --output-on-failure
ctest --test-dir build -C Release -j 6 -E 'hlclient_stock_runtime_orchestrator_startup_|Fake HLDS profile variants|Suspended launch keeps identity mismatch' --output-on-failure
ctest --test-dir build -C Release -j 1 -R 'hlclient_stock_runtime_orchestrator_startup_|Fake HLDS profile variants|Suspended launch keeps identity mismatch' --output-on-failure
```

## Required report index

1. Pre-E15 boundary: generic inventory/import, but Glock/crowbar-only presentation.
2. Server weapon identity: SDK/WeaponList ID 4.
3. Exact catalogue aliases weapon_mp5/weapon_9mmAR, existing owned selection.
4. Server-selected approved models/v_9mmAR.mdl, not hardcoded slot.
5. Server-selected models/p_9mmAR.mdl, existing merged attachment path.
6. Actual attachment findings in table, no assumed attachment 0 on p-model.
7. Validated sequence roles above, grenade excluded.
8. 0.1-second command cadence, 20 ms schedule, FPS-invariance test.
9. Local fire animation/body/audio/light/flash/shell/tracer integration.
10. Fresh reconciliation, bounded burst and late confirmation after animation.
11. Session-registered events/mp5.sc name binding, no fixed numeric ID.
12. mp52 secondary_deferred, no fake primary/grenade/session failure.
13. hks1/hks2 approved preload/PCM and existing mixer, no hks3.
14. Imported local muzzle/evaluated remote sockets; diagnosed fallback.
15. Existing bounded light owner/policy/execution.
16. Existing shell spawn/pool/collision/contact audio, separate MP5 sockets.
17. First/every-other tracer verified against pinned source.
18. Emitter/profile/life/map state ownership; no global player counter.
19. Neutral bounded depth-tested world-line primitive in normal GL.
20. Tracer impact suppression; non-tracer material/decal/particle eligibility.
21. Existing static BSP trace/clipping; no hit authority.
22. Existing E11 bounded dust/chips, non-tracer only.
23. Existing E7 material audio and generic bullet ricochet semantics.
24. Deterministic visual recoil -2..2, canonical punch precedence.
25. Server remote body animation untouched, no shot-driven pose restart.
26. Server-derived HUD clip/reserve; no provisional ammo display.
27. Explicit MP5 DeathMsg test: no score prediction, fresh ScoreInfo owns score.
28. Existing E13 death/respawn full regressions, no new live proof.
29. TE_BLOODSPRITE safe-consumption regressions included in full suite.
30. Changed-file mapping: firearm profile/controller/audio/visuals/remote and
   generic bullet helper; API metadata/visual requests; asset metadata/socket
   projection; app/main/impact dispatch; GL; CMake/tests/docs. Preservation
   evidence distinguishes these from prior dirty files.
31. Focused Debug: final results below.
32. Full final Release: after last production edit, results below.
33. Assertions/skips: explicit results below.
34. Real GL tracer pixels/depth/expiry and MP5 camera-local animation; Null suite.
35. Prediction/input regressions included, no simulation/wire rewrite.
36. Core-only build/tests with HL module excluded.
37. Actual manifest and architecture guard tested.
38. Bounded ASan core/API separate; full application runtime not verified.
39. CheckOnly/inert no-stock fixtures/preservation/diff results below.
40. Actual Release path: D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe.
41. New SHA below, not attributed to a past manual run.
42. Ready manual commands below.
43. E15 manual=not_run.
44. New live sessions/stock client/HLDS/Steam init/WFP/captures=0.
45. Staging/commit/push=none.

## Manual handoff

Subsequent user timing adjustment: MP5 local and remote tracer profile lifetime
is now **30 ms**, half the earlier 60 ms. Cadence, muzzle following, frozen hit
endpoint and flash/light durations are unchanged. Earlier 60 ms descriptions
above are historical implementation evidence, not the current MP5 profile.
Normal Release rebuilt; focused E15: 12 passed, one optional asset skip,
782 assertions, including present-at-29ms/expired-after-30ms. Full offline
suite was not repeated for this timing-only change; preceding counts belong
to the muzzle correction. Timing build SHA-256:
`7583513C0C0FA7EA89452AF5B65554D5E757D2769311C45CB92A48FEFAC834EC`.
Manual not_run; live launches and staging/commit/push remain zero/none.

### Subsequent muzzle/tracer correction — 2026-10-02

User evidence (`attribution=user_report`): moving MP5 tracer lagged behind the
weapon; first balcony pickup could show a tracer below the player and no flash.
This is not recorded as acceptance of the correction.

Production causes found: local lines stored the birth world-space start for
their entire 60 ms lifetime; main resolved a separate frame-zero attachment,
different from the visible animated pose; an RX pickup could change selected
model during session.update, but the new binding happened after command drain,
clearing its action/context. An unresolved attachment fell back to the frozen
command eye rather than the displayed muzzle. These are code-path findings,
not a claim of a captured exact manual-run diagnosis.

Correction: bind current post-RX model before submitted command processing.
Neutral local-muzzle-follow line intents use the current materialized pose and
render camera on every draw; trace/hit endpoint stays frozen, with no new trace,
audio, shot or TX on draw. Generation/model mismatch or unavailable muzzle
suppresses the local line instead of drawing an obsolete low fallback. Remote
world tracers stay frozen. Flash/light use current pose frame, not historical
marker frame zero; marker still triggers only one effect. Pool bounds/expiry,
cadence, server authority and normal launcher remain unchanged.

Snapshot: 1,200 source files, verified before edits. Follow-up evidence is under
`manual-artifacts/task-records/e15-muzzle-followup-20261002/`.
New fixtures check moving/elevated anchor, immutable endpoint, unready/mismatched
model suppression, expiry and first bound pickup flash/tracer without replay.
Follow-up manual=not_run, live sessions=0, staging/commit/push=none.
Final follow-up verification and new executable hash are appended below.

Follow-up final results after the last production edit:

- Release build passed (initial compiler DLL loader failure retained in
  build-release.txt; bounded retry with installed MSVC PATH passed).
- Focused Release: 42 passed, 3 opt-in skips, 5,729 assertions (focused.txt).
- Full Release: 2,679 passed, 39 skips, zero failures of 2,718 results;
  four serialized process checks passed. Combined: **2,683 passed / 39 skipped
  / zero failed**, 2,722 results (release-final.txt, serial-final.txt).
- Affected Debug build passed; 86 passed, 3 opt-in skips, 20,816 assertions
  (build-debug.txt, debug.txt).
- Core-only build and 41/41 checks passed, architecture guard included
  (core-build.txt, core-final.txt).
- Launcher CheckOnly passed; launcher SHA unchanged; git diff --check clean,
  staged diff empty. Snapshot: 1,192 unchanged, 8 intended changed, zero missing
  (preservation.json). ASan/device listening not rerun for this correction.
- New Release SHA-256:
  `C3E4402CC7D2D3397B9AAE900B7D29FC7F561175907AEEFC8AA48CFD6775E517`.

Use the unchanged manual command below; test first balcony pickup/fire and
strafe/walk/turn while firing. Follow-up manual remains not_run until the user
actually checks this binary. No new live sessions or Git writes were performed.

Unchanged crossfire default and Release path; no hidden E15 feature flag:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Both clients, user-controlled close:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Pick up/select MP5 (3/wheel), hold primary. Check shoot variants/hks/light/
flash/shells; first/every-other tracer, other shots static-wall impacts.
Observe both receivers, move/pitch/crouch/reload/switch/death/respawn and check
HUD/TAB/kill feed. No stale muzzle/replayed effects. Secondary is deferred,
not complete grenade presentation; E16 is a separate future task.

### Subsequent bullet-flight tracer correction — 2026-10-02

User request: replace the laser-like whole-path line with visible bullet flight.
This is a visual adjustment, not manual acceptance of this build. The earlier
muzzle-follow and timing reports above remain historical evidence.

Actual MP5 local and remote profiles select **24-unit streaks at 12,000 world
units/second**, keeping the requested **30 ms maximum age** and existing
first/every-other shot cadence. The generic world-impact owner computes a
moving tail/front, clips the front to the frozen hit (or miss endpoint), and
stops drawing when the tail reaches it. Alpha fades within the original action
deadline. These are local-compatible visual values, not stock ballistic
constants. At low frame rates a 30 ms effect may occupy only one frame; it is
not guaranteed to reach a distant endpoint before the lifetime cap.

Local launch captures the first valid current evaluated muzzle once. It never
uses the old command-eye fallback; after launch the streak stays in the world
even if the camera moves or the weapon switches. Remote launch uses the known
world origin. Repeat render, duplicate submission and backward timestamps
cannot restart or rewind flight. Missing/mismatched local anchors suppress
launch; reset clears both lines and clock. Existing fixed pools and line pass
are reused, with no new projectile simulation, trace per frame, audio, command
or TX. Zero speed/length retain the stationary generic line mode. Invalid
motion pairs suppress only the tracer, not an otherwise valid hit.

Snapshot: 1,200 source files verified before edits, under
`manual-artifacts/task-records/e15-flight-followup-20261002/`.
Numerical fixtures cover launch/current muzzle, motion independent of gun,
24-unit length, fade, hit clipping, independent emitter clocks, expiry,
duplicate/backward-time handling, invalid profiles and reset. A separate
production GL fixture compares two time positions, bounded changed pixels,
wall occlusion and return to baseline after expiry. The small GL wall uses
scaled fixture speed/length; module regressions check the actual MP5 values.

New build manual=not_run; live launches=0; staging/commit/push=none.

Final verification after the last production edit:

- VS2022/Win32 Release and affected Debug app/tests built successfully.
- Focused Release: 64 passed, 4 opt-in asset skips, 6,469 assertions. The new
  moving GL pixel-centroid, occlusion and expiry test passed (not skipped).
- Debug: 108 passed, 4 opt-in asset skips, 21,556 assertions.
- Full Release offline run: 2,680 passed, 39 skips, **2 fixture timeouts** of
  2,721 tests. `hlclient_stock_runtime_restoration_guard` (30s bound) and
  `hlclient_stock_runtime_research_manifest` (60s bound) produced no error
  output before timeout while the separate Debug build was also active. These
  PowerShell temp-tree fixtures do not load the tracer/application. Contention
  is plausible but the empty output does not prove the exact timeout cause.
- After compilation/the suite finished, both fixtures passed serially with
  unchanged bounds (5.01s/7.85s). The four separately scheduled no-stock process
  fixtures also passed: serialized **6/6**. Unique coverage after retries and
  those four checks: **2,686 passed / 39 skipped / zero unresolved failures**,
  2,725 tests. This does not hide the initial non-green run; its full output is
  retained in `release-final.txt`, retry output in `serial-final.txt`.
- Core-only build and 41/41 tests passed, including the architecture guard.
- Launcher CheckOnly passed without starting runner/game/Steam/WFP/sockets;
  unchanged launcher hash
  `7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`.
- Snapshot comparison: 1,191 unchanged, 9 intended tracer/test/doc changes,
  zero missing source files. `git diff --check` clean, staged diff empty.
  ASan and device listening were not rerun for this visual-only correction;
  no ASan application-runtime acceptance is inferred from this build.
- Normal Release `build/bin/Release/hlclient.exe` SHA-256:
  `11FD30B2744D1C692DA9649CDD6F7277381AEA8F7A812FB106C75978726CE95A`.

Use the unchanged manual commands above. Check MP5 short flying streaks while
moving/turning, close-wall clipping, expiry and both receivers. Manual remains
not_run until the user checks this binary. No live sessions or Git writes.

### Subsequent flight visibility adjustment — completed 2026-10-03

User report (`attribution=user_report`): moving tracer is hard to see; request
to double its lifetime. MP5 local and remote maximum age is now **60 ms**,
up from 30 ms. Moving 24-unit streak, 12,000 units/second, fade, once-captured
muzzle, frozen endpoint and alternating cadence remain unchanged. Arrival of
the tail at a nearby hit still ends the streak sooner; the lifetime cap does
not make it linger at the wall or become a stationary laser.

Earlier 30 ms verification above describes its historical build. New local
and remote profile assertions check 60 ms; a moving-owner fixture checks
visibility after the old 30 ms deadline, travel/fade at 59 ms and expiry after
60 ms. Snapshot evidence: `manual-artifacts/task-records/e15-flight-60ms-20261002/`.
This is a narrow Half-Life profile timing change, not a new core mechanism.
New build manual=not_run; new live launches=0; staging/commit/push=none.

Normal Release rebuilt successfully; focused E15: **16 passed, 1 optional
approved-asset skip, 913 assertions**, including both launch modes at 59 ms
and expiry after 60 ms. Production OpenGL tracer checks passed. Launcher
CheckOnly passed; launcher SHA unchanged; `git diff --check` clean and staged
diff empty. Debug/full offline/core-only/ASan were not repeated for this
two-value profile adjustment; earlier counts above belong to earlier builds.
Release SHA-256:
`7124CB3B4C3E61B6A90C3D190536A4B4F8D66EDA67E1ABC2638C62D072EDB27D`.

Snapshot capture overlapped the edits: its per-file hash-verified copies mix
the before/after revisions of the six timing-related files, so it is **not**
claimed as an atomic pre-edit checkpoint. Audit confirms all 1,194 files
outside that explicit scope unchanged and none missing. Existing earlier
snapshots/history remain untouched. Use the unchanged manual command above;
new build manual=not_run until checked by the user.

### Subsequent pointed tracer and +30% lifetime — 2026-10-03

User request: another 30% lifetime plus a sharpened leading end. MP5 local
and remote profiles now select **78 ms** (60 x 1.3), with the existing
12,000-unit/second travel, 24-unit length and alternating cadence unchanged.
Near-wall tail arrival still ends the visual earlier than its maximum age.

The neutral API and RenderWorldLine add `head_taper_fraction`; default zero
preserves old rectangular callers, finite [0,1] is validated at owner/renderer.
MP5 selects 0.35: the body retains full width and the last 35% narrows linearly
to a point at `end`, without extending beyond the frozen collision endpoint.
The same existing quad/flash program performs the line-only coverage clip;
flash/particle shapes and blend/depth state restoration are unchanged. No
new resource, physics, wire, damage or gameplay authority is introduced.

Pre-edit snapshot: 1,200 source files hash-verified **before edits**, under
`manual-artifacts/task-records/e15-flight-tip-78ms-20261003/`. New owner fixtures
cover local/remote 77ms visibility, fade, exact 78ms expiry and invalid tips;
actual module tests check both selected profiles. Production GL A/B compares
column coverage at identical geometry/time: body preserved, head narrowed,
reversed direction moves the point to the opposite side, default rectangle
is restored, invalid profiles/behind-wall lines/cleanup return to baseline.

Current build manual=not_run; live game/stock/HLDS/Steam/WFP/captures=0;
staging/commit/push=none. Earlier 60ms counts/hash are historical, not this build.

Final results after the last production edit:

- VS2022/Win32 Release and affected Debug app/tests built successfully.
- Focused Release: 67 passed, 4 optional asset skips, 6,567 assertions. The
  actual GL pointed-head/unchanged-body/reversed-direction/default-state,
  invalid-profile, occlusion and cleanup checks passed (not skipped).
- Debug: 111 passed, 4 optional asset skips, 21,654 assertions.
- Final full Release offline suite: 2,685 passed, 39 capability/opt-in skips,
  zero failures of 2,724 results; separately scheduled no-stock fixtures 4/4
  passed. Combined **2,689 passed / 39 skipped / zero failed**, 2,728 results.
  All compilation finished before this full run; no timeout retry was needed.
- Core-only build and 41/41 tests passed, architecture guard included.
- Launcher CheckOnly passed without runner/game/Steam/WFP/sockets. Launcher
  SHA remains `7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`.
  Snapshot: 1,190 unchanged files, 10 intended edits, none missing;
  `git diff --check` clean and staged diff empty.
- ASan/device listening not rerun for this renderer change. No application
  ASan-runtime acceptance or user visual acceptance is inferred.
- Normal Release `build/bin/Release/hlclient.exe` SHA-256:
  `C046DFC145CAE69CE79A4E1A7A1C209B8D20B2CA1F4BDC7DCE71B57F814D98EC`.

Use the unchanged manual command above. Current build manual=not_run until
the user checks MP5 flight/head/visibility in their normal session.

### Tracer and bullet-impact glow / denser sparks — 2026-10-03

User-requested visual tuning (not a new gameplay milestone or a manual pass):
short light flashes for tracers and bullet sparks, denser sparks and a modest
increase in spread. The prior 78ms pointed-flight implementation is retained.
Pre-edit hash-verified source snapshot completed before edits under
`manual-artifacts/task-records/e15-tracer-spark-glow-20261003/` (1,200 files).

Half-Life profiles (`weapon_visuals.hpp`, local `weapon_visuals.cpp` and
`remote_effects.cpp`) select identical local/remote values:

- Moving tracer: 1.25-unit additive head glow; 48-unit light radius,
  intensity 0.35, warm tracer color. It follows the actual clipped front,
  fades with the existing line, and cannot precede a valid local muzzle
  launch or survive tail arrival/78ms expiry. Motion/cadence/tip are unchanged.
- Supported non-tracer bullet hit: **16** warm emissive particles instead of
  8 dust-colored particles. Speed 42–78 instead of 35–65 units/s; tangent
  spread scale 1.2 instead of 1.0. Radius 0.60 instead of 0.55; 280ms lifetime
  and gravity 180 unchanged. One 2-unit flash and 64-unit-radius/intensity
  0.55 light fades over the first 60ms at the accepted surface point.

These are explicit local-compatible visuals, not stock-exact constants.
Tracer shots still follow the existing alternating no-surface-effects policy;
this does not add extra hits, audio, wire traffic, ammo or damage authority.
Miss/startsolid/unsupported surfaces, duplicate bursts, missing decal/audio
resources and life/map/session resets retain the existing eligibility paths.

Neutral `TransientGlowProfile`, particle spread/emission mode and owning
scene light outputs carry no weapon names, assets or engine handles. The
normal application appends remote muzzle flashes before secondary effects,
so a saturated effect batch cannot consume their existing flash priority.
The primary `transient_world_light` is unchanged; a separate frame-owned
**8-light** batch adds contributions in the existing world/brush and Studio
shaders. First-person projection keeps its separate light and resets the
world batch. Shared upload filters invalid lights and resets count each draw.

The existing pool remains 32 bursts/128 recent causal identities, now at
most 16 velocities per burst. It saturates deterministic output at the
existing **256 particles** rather than growing GPU submission unboundedly;
`output_limited` records saturation. Flash output remains 32 and extra lights
8, with graceful omission at capacity. Rendering/culling does not restart
events; a monotonic pool clock prevents backwards-time flash resurrection.
The existing procedural billboard program renders soft additive sparks,
while default non-emissive particles retain their alpha-blended dust shape.

Lighting uses the existing squared-distance, unshadowed point-light model;
this is not shadow mapping, physically based bloom, dynamic bullet physics
or a complete material-dependent spark system. New assets/downloads=0;
manual=not_run, new live game/HLDS/Steam/WFP/capture sessions=0;
staging/commit/push=none. Verification and Release hash follow below.

Final verification after the last production edit (remote muzzle priority):

- VS2022/Win32 Release all targets and affected Debug app/tests built.
- Focused Release: 79 passed, 4 opt-in asset skips, 10,730 assertions;
  affected Debug: 123 passed, 4 skips, 25,817 assertions.
- Actual production GL tests passed, not skipped: isolated secondary light
  changes real wall pixels with flash/particle/line drawing disabled, retains
  the independent primary-lit region, filters invalid entries, and returns
  exactly to the primary-only then unlit framebuffer signatures. Soft
  emissive sparks give greater isolated pixel signal than alpha dust;
  tracer-head glow adds pixels beyond the line, opaque occlusion and cleanup
  restore baseline. Existing tip/flight/decal/world/Studio/HUD passes remain
  covered by the final offline suite. Pixel evidence is not a user eye test.
- Core-only build and 41/41 tests passed after the final source changes,
  including new neutral glow/spread/density tests and the architecture guard.
- Final full Release offline suite: **2,694 passed / 39 skipped / zero failed**
  of 2,733 results (160.75 s). Separately serialized no-stock/process fixtures
  passed 4/4 (12.49 s). Combined **2,698 passed / 39 skipped / zero failed**,
  2,737 results. All compilation finished before the full run; no retry of
  that full suite was required.
- The first focused run exposed two fixture setup errors: a 1,000-unit test
  origin retained the old 20-unit trace range, and an eviction fixture tried
  to rewind the newly monotonic particle clock. Corrected range and forward
  timestamps, preserving hit/oldest-eviction assertions; production was not
  changed to hide either failure. Final focused results above are after those
  test corrections, not the failed preliminary run.
- Launcher CheckOnly passed; it selects the normal Release below, with no
  new flags or resource copying. Script SHA remains
  `7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`.
- Source preservation: 1,179 snapshot files unchanged, 21 intended edits,
  none missing, plus the ordinary shared `opengl_effect_lights.hpp` header.
  `git diff --check` clean; staged diff empty. No other worktree or installation
  files were changed. No ASan/device listening rerun; prior application ASan
  runtime remains unverified (earlier exit 1 without output), not inferred
  from Debug/Release success. Capability/private-asset skips remain explicit.
- Normal `build/bin/Release/hlclient.exe` SHA-256:
  `E88D89AD187411D44300E9D8F279FFA01C0FA7F20758BA742648E69E455ABC6B`.

Use the unchanged normal manual command above (optionally the already
supported `-RemoteAudioPeer -NoTimeLimit`). Check moving head glow, brief
wall illumination at a non-tracer bullet hit, denser/wider warm sparks and
unchanged muzzle/decal/shell/camera behavior. Current build manual=not_run;
no new game/HLDS/stock/Steam/WFP/capture sessions or staging/commit/push.

### Double MP5 tracer width — 2026-10-03

User requested twice the thickness. Both Half-Life MP5 profiles now explicitly
select **1.30 world units instead of 0.65**, so the existing renderer's
`width * 0.5` side offsets double body coverage for local and remote shots.
The generic API default stays 0.65; other callers are not widened. Motion
12,000 units/s, length 24, lifetime 78ms, head taper 0.35, glow and spark
profiles/cadence/eligibility are unchanged. Actual module regressions assert
both selected widths and the unchanged generic default.

Pre-edit source snapshot completed and hash-verified (1,201 files) under
`manual-artifacts/task-records/e15-tracer-width-130-20261003/` before edits.
Only two Half-Life profile source files, their actual module tests and this
record change; no shared renderer/API/core source changes. No new assets,
live sessions or staging/commit/push. New build manual=not_run; earlier
glow/density counts and hash above describe the previous build.

Width-only verification: VS2022/Win32 affected Release and Debug targets built.
Focused Release 79 passed / 4 optional asset skips / 10,742 assertions;
Debug 123 passed / 4 skips / 25,829 assertions, including existing actual GL
flight/tip/glow/depth/cleanup tests. Launcher CheckOnly passed without live
execution; its SHA is unchanged. Preservation: 1,197 snapshot files unchanged,
4 intended edits, none missing; staged diff empty and `git diff --check` clean.
Full Release/core-only/ASan/device suites were not repeated for these two
Half-Life-only numeric assignments; prior full results are not claimed anew.
Normal Release SHA-256:
`847E6F68FE5C5D29875C8710C1BEBFB16D0BDC35AC5D2FA5673CF0ADA689B7D1`.
Manual command unchanged, new build manual=not_run; new live launches=0 and
staging/commit/push=none.

### First-person diameter and varied forward starts — 2026-10-03

User requested their first-person tracer to be twice thinner than other players'
tracers, and a random forward spawn distance whose minimum is the current muzzle
point. Selected local-compatible range: **0–16 world units forward**. This is
presentation variation, not changed hitscan/ballistics or a stock-exact constant.
Pre-edit source snapshot: 1,204 hash-verified files under
`manual-artifacts/task-records/e15-local-width-random-start-20261003/`.

Local `WeaponVisuals` now selects diameter **0.65**, while `RemoteWeaponEffects`
retains **1.30**. Both select one forward distance with the HL-owned
`mp5_tracer_start_offset(action)` helper: a bounded deterministic action hash
using generation, command, canonical record and emitter identity, not a global
RNG, frame time or render order. A replay/confirmation does not re-roll it.
Remote accepted-event serials are receiver-local: stable within each observer,
not a claim of identical variation across observers or stock-binary parity.

`LocalWorldImpactRequest::Tracer::start_offset_units` is an owning neutral scalar,
default zero, finite in [0,1024]. The existing world-impact owner stores it and
adds it to travelled distance only after capturing the actual first valid local
muzzle (or from the committed remote muzzle). No trace-eye offset/fallback or
per-frame accumulation. The collision endpoint and decal/impact results stay
unchanged. Close hits at/before the chosen start skip the line and its glow/light;
other hits clip as before. Invalid distances suppress the line only. Stationary
neutral callers also receive an explicit shortened-start line, not an ignored
parameter. The retained flat and normal 3D variants use exactly the same motion.

Speed 12,000, segment length 32, max age 78ms, 35% tip, color/fade, glow/light,
cadence, audio, shell/particles, wire/history/gameplay and lifecycle are unchanged.
The equal-size retained head glow can have the same halo envelope in both views;
the **body diameter**, not that halo, is halved. No new parameter/asset/live session.

New project-owned tests exercise actual local/remote module profiles, varied and
repeatable starts, exact diameter ratio, one-shot duplicate suppression, actual
muzzle vs trace eye, flight timestamps/backward time, reset, 0/8/16 offsets,
invalid/close hits, stationary semantics and both flat/3D paths. A production GL
pixel test isolates the solid body and checks its half-width silhouette and
cleanup/state restoration. Final build/test evidence and executable hash follow
after verification; previous stage counts/hashes remain historical.

Current changed build manual=not_run; new live game/stock/HLDS/Steam/WFP/capture
launches=0, staging/commit/push=none. Unchanged manual command:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

For direct own-vs-remote comparison, existing `-RemoteAudioPeer -NoTimeLimit`
options remain available. Verify MP5 at long enough range to see a complete
streak; close wall hits may end it early as before, including a chosen start
beyond that short visible path.

Final width/start verification (no production edits after the compiled change):

- VS2022/Win32 all Release targets, affected Debug client/tests and existing
  core-only Release tree built successfully, incrementally, without cleaning.
  One MSBuild at a time; process-local `/MP1` for all Release, `/MP2` for subsequent
  Debug/core and the one-file Release fixture rebuild. No competing builds.
- Focused Release **90 passed / 4 opt-in asset skips / 27,094 assertions**;
  focused Debug **134 passed / 4 opt-in asset skips / 42,181 assertions**.
  Added owner/motion/close-hit/lifecycle/profile checks and production GL passed.
  The isolated GL body column measured **6 pixels local / 14 remote** at the same
  pose and distance (raster rounding of exact 0.65/1.30 diameters), zero GL errors;
  broad redraw and cleanup restored the exact prior framebuffer signatures.
  This is pixel evidence, not a new user visual confirmation.
- The first focused run exposed a NEW test-fixture omission: no session time
  origin, so `GameClientHost` correctly classified remote events `missing_clock`.
  The fixture now establishes its normal clock and drains per-shot audio, with
  additional duplicate/one-shot assertions. Production fail-closed behavior and
  older expectations were not weakened; final focused results above are after
  that test-only correction.
- Core-only `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`: **41/41** passed (37.84s), including
  the actual architecture guard and core/API fixtures, without installed game
  assets or HL1 linkage.
- One final full Release offline run: **2,704 passed / 39 skipped / 1 failed**,
  2,744 results, 145.89s. The unchanged `Kill-on-close job owns only the exact
  launched fake process` failed at its system process-identity enumeration
  (`REQUIRE(matches)`), not at tracer/GL/module assertions. Exact underlying
  OS-query failure was not diagnosed by its existing log. An isolated serialized
  rerun passed, together with the separately serialized four existing fake/
  no-stock startup fixtures: **5/5** passed (8.61s). All distinct tests have a
  passing outcome or explicit skip after that recheck, but the original full run
  is NOT relabeled green; the unreproduced process-query failure remains noted.
- Launcher `-Prediction reference -CheckOnly` passed without runner, game, Steam,
  WFP or sockets. Launcher SHA unchanged:
  `7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`.
- Preservation audit: **1,194 unchanged / 10 expected changed / none missing**
  from 1,204 snapshot files. No renderer/flat draw edits in this follow-up.
  Staged diff empty, branch/HEAD unchanged, `git diff --check` passed.
- Actual normal Release `build/bin/Release/hlclient.exe` SHA-256:
  `F9DEBDF0A9FD9E4D57B9F1247FB85BD98A1D5062CBA0484EF2254BAEE0D380A8`.
- ASan runtime, device audio and user listening checks not rerun for this
  presentation change. Prior application ASan runtime remains unconfirmed;
  ordinary Debug/Release success does not change that status. New build manual=
  not_run; new live game/stock/HLDS/Steam/WFP/managed captures=0; no staging/commit/push.

### One-third longer MP5 streak — 2026-10-03

User requested a streak one third longer: **24 → 32 world units** (24 x 4/3)
in both local and remote Half-Life MP5 profiles. Width 1.30, travel speed
12,000 units/s, maximum age 78ms, pointed head, glow, spark profile and
alternating cadence remain unchanged. This length is the travelling visible
segment, not a longer lifetime or a muzzle-to-wall laser. The existing owner
still clips its leading end to the immutable endpoint and stops at tail
arrival. Actual module assertions now check 32 for both paths; independent
generic 24-unit motion fixtures remain unchanged.

Pre-edit source snapshot: 1,201 files hash-verified before edits under
`manual-artifacts/task-records/e15-tracer-length-32-20261003/`. Only two HL
profile source files, their tests and current architecture/task descriptions
change; no shared core/API/renderer changes or new assets. Earlier width/glow
results and hashes are historical. New build manual=not_run; new live
launches=0, staging/commit/push=none.

Length-only verification: VS2022/Win32 affected Release and Debug built.
Focused Release: 79 passed / 4 optional asset skips / 10,742 assertions;
Debug: 123 passed / 4 skips / 25,829 assertions, including existing actual GL
flight/tip/glow/depth/cleanup checks. Launcher CheckOnly passed without runner,
game/Steam/WFP/sockets; launcher SHA unchanged. Source audit: 1,196 unchanged
snapshot files, 5 intended edits, none missing; staged diff empty and
`git diff --check` clean. Full/core-only/ASan/device suites not repeated for
these two HL-only numeric assignments; earlier full results remain historical.
Normal Release SHA-256:
`72B88EECF9144E921C01B9BBC451A67D30D2242928553B75B7F407E6B67B29B3`.
Manual command unchanged; new build manual=not_run.

### Additive solid 3D tracer variant, normal MP5 default — 2026-10-03

User requested a real 3D alternative while retaining the current flat code
and all current effects/profile characteristics. `attribution=user_report`:
the prior tracer looked flat during firing; no new 3D manual pass is inferred.
Pre-edit source snapshot: 1,201 files hash-verified before any source edits,
under `manual-artifacts/task-records/e15-tracer-3d-default-20261003/`.

`WorldLineGeometry` is appended to the owning API tracer and scene line.
Its neutral default stays `flat_billboard`; the old quad geometry, shader,
pointed-head behavior and existing exact GL assertions remain in place.
Only the local and remote Half-Life MP5 profiles explicitly choose `solid_3d`
for normal application execution. No launcher parameter, feature flag,
replacement executable or asset is needed; no second effect owner is added.

All MP5 numeric values are retained: diameter 1.30, segment length 32,
speed 12,000 units/s, maximum age 78ms, warm RGBA, tip fraction 0.35,
head glow radius 1.25 and light radius/intensity 48/0.35. The existing owner
still captures the actual local muzzle once (remote committed origin), clips
to the same immutable endpoint, advances/fades/deduplicates/expires identically
and emits the same head flash/light. Sparks, muzzle/shell/audio, alternating
cadence, gameplay, wire/history and life/map/session boundaries are unchanged.

Shared `renderer/render_line_geometry.*` builds a camera-independent, owning,
allocation-free closed twelve-sided tube/cone, at most 144 vertices per line.
Radius equals half the selected width; the last 35% is an actual cone to the
endpoint. Taper zero is a capped cylinder, one a closed cone. Invalid geometry,
coordinates, color, width, taper or degenerate/oversized span returns no mesh,
not a flat fallback. The existing 64-line limit is unchanged. Pure geometry
tests are linked in both normal and core-only tests, without installed assets.

The added GL draw uses the same additive blend and depth test/no depth writes.
Outward CCW back-face culling prevents additive front/back double contribution;
blend factors, cull mode/front-face and depth-write state are restored. A mild
normal-derived emission falloff (0.65–1.0 of the existing tint) conveys a round
cross-section without changing its base color, timing or adding another halo.
The retained head glow/light supply the same surrounding effects. This is a
local-compatible presentation mesh, not a physical bullet or stock-exact effect.

Independent CPU tests measure diameter/shoulder/endpoints, normals/winding,
all axes, bounds, degeneracies and deterministic output. Owner A/B tests compare
flat/3D flight/fade/glow/expiry exactly. Production GL isolates geometry without
lights/flash: front- and tail-on views must be visible for 3D while the old flat
quad collapses; side/top/oblique views, tip reversal, wall depth, invalid inputs,
cleanup and exact flat restoration after mixed draws are also covered.

Verification and current executable hash follow below. Earlier full counts
and hashes are historical. New build manual=not_run; new live game/HLDS/stock/
Steam/WFP/capture sessions=0; staging/commit/push=none.

Final 3D-variant verification after the last production edit:

- VS2022/Win32 Release all targets, affected Debug app/tests and the existing
  core-only Release tree built successfully. Initial concurrent Release
  compilation hit C1060 compiler/host memory pressure and a follow-on tlog
  sharing error; the same incremental build completed serially with process-local
  `/MP1`, without cleaning, source/expectation changes or persistent flag edits.
- Focused Release: **85 passed / 4 opt-in asset skips / 26,266 assertions**.
  Debug: **129 passed / 4 opt-in asset skips / 41,353 assertions**.
  All added production GL checks passed, not skipped: isolated solid end-on
  silhouettes at both ends, side/top/oblique views, tip/body/reversal, depth,
  invalid geometry and exact old flat framebuffer restoration after mixed draws.
  This is pixel evidence, not a user visual confirmation.
- One final full Release offline suite after all compilation finished:
  **2,700 passed / 39 capability/opt-in skips / zero failed**, 2,739 results,
  151.53s. Separately serialized existing no-stock/fake-process fixtures **4/4**
  passed (9.89s). Combined **2,704 passed / 39 skipped / zero failed**,
  2,743 results. No real HLDS/stock/Steam/WFP/managed session was launched.
- Core-only (`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`) **41/41** tests passed,
  architecture guard included. The independent geometry subset separately
  passed **15,269 assertions in 3 cases**, with no game/assets/device dependency.
- Launcher `-Prediction reference -CheckOnly` passed; exact Release selection
  and helper paths remain unchanged. Launcher SHA remains
  `7741E4A254410CA0404739E81577813EA51082E4EB06EE45C8F2E6D9578BE73D`.
- Source preservation: **1,188 unchanged / 13 intended edited / none missing**
  out of the pre-edit 1,201-file snapshot, plus three ordinary new files:
  mesh header/source and CPU tests. Retained flat draw, shader and geometry
  code were additionally compared byte-for-byte to the snapshot. Staged diff
  remains empty; `git diff --check` passed. Branch/HEAD remain unchanged.
- ASan/device-audio/listening checks not rerun for this geometry feature.
  The prior application ASan runtime is still unconfirmed (historical exit 1
  even for `--help`); ordinary builds/tests do not change that status.
- Normal `build/bin/Release/hlclient.exe` SHA-256:
  `861F515DC4E7CBA4A18252789E14796D6FDCD2A0F0133FCB370793D8FE0C097E`.

The volume is an emissive closed surface mesh, not ray-marched scattering,
bullet physics, material/shadow simulation or a new weapon milestone. As with
ordinary outward-culled convex geometry, a camera inside it has no special
interior/near-plane fill; normal muzzle launch is outside the eye. Existing
head billboard glow and point lighting retain their prior compatible limits.

Unchanged manual command (3D needs no extra option, for either client):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

The existing `-RemoteAudioPeer -NoTimeLimit` options still compose the two-client
manual test; both local and remote MP5 profiles now select 3D. Current build
manual=not_run until the user's actual check; new live launches=0 and
staging/commit/push=none.

Later same-day follow-up: the **First-person diameter and varied forward starts**
section above supersedes this historical 3D baseline's equal-diameter/current
hash statements. Current profiles are local **0.65**, remote **1.30**, with a
stable per-action **0–16-unit** forward start; other 3D/flat mechanisms and
flight/glow values are preserved. Current normal Release SHA-256 is
`F9DEBDF0A9FD9E4D57B9F1247FB85BD98A1D5062CBA0484EF2254BAEE0D380A8`.
See that follow-up's final verification for passing focused/core-only/GL checks
and the honestly retained full-run process-query failure followed by its passing
isolated recheck. Current manual=not_run; same launcher, no new flags/live sessions.

### Frozen 3D tracer inspection — 2026-10-03

User-requested opt-in diagnostic, not a new gameplay stage. The normal launcher
accepts `-DebugFreezeTracer` in the manual reference/off scenario, including
`-NoTimeLimit` and the existing optional peer mode. It forwards via the existing
runner and native orchestrator as `--debug-freeze-tracer`; default is false and
scripted/replay/non-OpenGL modes reject it. Native and wrapper argv profile
metadata both append `;tracer-geometry=frozen-debug` only when enabled.

The existing production world-impact owner retains **one** actual solid moving
tracer at its first valid muzzle/committed origin, with the current selected
forward offset, clipped 32-unit length, local/remote diameter, 35% cone tip and
original opacity. It stays fixed in world space without expiry until the next
valid tracer replaces it or map/network/session reset. No recapture on camera,
weapon change, repeated render/update, or action replay. Retention is owning and
bounded; submission serial ordering prevents wrapped pool slots/delayed old
anchors replacing a newer tracer. Missing/expired actual muzzle anchors, invalid
traces or offsets beyond a close hit do not fabricate a cone.

No permanent head-glow billboard/light is retained, to inspect the geometry
without its halo. Ordinary muzzle flashes, shells, sounds, impacts, ammo, cadence
and authority are unchanged. Flat/zero-motion neutral callers keep ordinary
expiry. **MP5 still emits a tracer on every second accepted shot**; firing Glock
does not gain MP5 tracers in this diagnostic. For inspection, aim into open space
or at a distant wall, then walk around the retained tracer. Starting another
valid tracer shot replaces it; reload and walking do not. No enlargement or
new shape is substituted for the current production mesh.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -DebugFreezeTracer -NoTimeLimit
```

Without `-DebugFreezeTracer` normal 78-ms flight/expiry is unchanged. The existing
untimed-session cleanup rules remain: close the client window(s), then wait for
managed cleanup/restoration before closing PowerShell. New diagnostic build
manual=not_run. New live sessions/Steam/HLDS/captures=0; staging/commit/push=none.
Pre-edit source snapshot: `e15-debug-frozen-tracer-20261003` (1204 hash-verified
ordinary source files). Verification results and executable hash follow after
the final builds/tests; historical reports above are not rewritten.

Frozen-tracer final verification (VS2022 / Win32):

- Normal Release application/helpers rebuilt successfully; affected Debug
  application/tests/orchestrator and existing core-only Release rebuilt serially.
  First Release compile exposed two mistakes only in new fixture assertions
  (no vector equality operator, implicit wide-to-narrow conversion); fixed using
  component equality and explicit conversion without changing production rules.
- New `[tracer-freeze]` regressions: **4/4 cases, 311 assertions**. Actual GL
  checks six viewpoints, visibility after 1000 seconds, repeated identical pixels,
  depth occlusion and reset back to baseline, without a retained glow/light.
  CPU checks include local/remote muzzle ownership, 86400-second retention,
  camera/weapon changes, dedup, bounded ring wrap/latest sample, close clipping,
  missing anchor/invalid collision, reset and unaffected flat/default expiry.
- Focused Release: **111 passed, 4 optional-asset skips**, 28210 assertions.
  Focused Debug including weapon/life/scoreboard regressions: **155 passed,
  4 optional-asset skips**, 43297 assertions. No assertion weakening.
- Final full Release offline suite after the last production edit: **2708 passed,
  39 capability/asset/device skips**, 0 failed / 2747 results, 135.49 seconds.
  Five existing fake-process/startup fixtures serialized separately: **5/5**,
  8.09 seconds. Combined **2713 passed, 39 skipped**, 0 failed / 2752 results.
  Skips are not live or device proof; opt-in assets/output remain untested here.
- `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`: **41/41**, including architecture guard,
  39.85 seconds. No HL1 object/header/link fallback in the core-only build.
- No-stock wrapper fixture and actual launcher `-CheckOnly` with frozen tracer,
  unlimited time and reference/off all passed. Native opt-in parser duplicate /
  scripted rejection passed, stock processes=0. Wrapper argv fixture roundtrip
  and native client-forwarding source audit passed; matching native/wrapper
  profile suffixes were inspected in source, not claimed as live metadata proof.
  Normal `hlclient.exe --help` exposes the flag. `git diff --check` clean.
- ASan runtime **not_run / previously unconfirmed**; this bounded diagnostic
  does not claim the historical ASan startup limitation repaired. New manual
  inspection **not_run**. No game, Steam, HLDS, WFP or managed capture launched.
- Snapshot audit: **1190 unchanged, 14 expected source/doc/test edits, 0 missing**
  of 1204 preserved files. Branch/HEAD unchanged, staged diff empty;
  staging/commit/push=none. Results/logs retained under the snapshot record.

Launcher resolves the rebuilt normal
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1\build\bin\Release\hlclient.exe`.
SHA-256:
`5E26A2AFBA366340F909F9C5D8D70CAB3D8CB2769D81EA433116F4BD236CF0D2`.
This new hash supersedes the previous width/start build only for this diagnostic
follow-up; it is not attributed to any previous user's manual run.

### Rounded 3D tracer rear — 2026-10-03

`attribution=user_report`: the frozen inspection screenshot shows a conspicuous
flat rear cut, also noticeable during shots. This is not a launched-image hash
confirmation. The previous mesh emitted a flat triangle fan at `line.start`,
with identical backward-facing normals; it was an actual cap, not a texture or
debug overlay. The existing shared `solid_line_mesh` now replaces that fan with
a four-ring smooth hemisphere and radial-gradient normals tangent to the body.

Rounding is **inside** the existing start/end interval: its axial extent is
`min(radius, body_length / 2)`, and a very short clipped body uses a compressed
half-ellipsoid. Diameter (.65 local / 1.30 remote), random forward start,
32-unit segment envelope, front shoulder/35% pointed tip, cadence, speed/fade,
glow/light/audio and authority remain unchanged. Full-taper standalone cones
(not the MP5 profile) have no body space and retain the previous closed base.
The original flat variant is untouched. Mesh limit becomes 360 vertices / 120
triangles; the existing GL path uploads exactly the emitted vertices, preserving
the same blend/cull/depth-state restoration and bounded line pool.

Applies by default to normal 3D shots and the existing `-DebugFreezeTracer` mode.
No new parameter, asset, profile permission or alternate renderer is needed.
New build manual=not_run. Source snapshot: `e15-rounded-tracer-tail-20261003`,
1204 files verified before edits. New live sessions/Steam/HLDS/captures=0;
staging/commit/push=none. Final verification/hash follow after the final build.

Rounded-rear verification (VS2022 / Win32):

- Release application/helpers, affected Debug application/tests and existing
  core-only Release built successfully, one build at a time. Initial compile
  rejected chained `&&` in the new Catch2 assertion; parenthesizing that boolean
  expression fixed test syntax only, without weakening its no-flat-cut check.
- Shape/frozen/3D regressions: **38/38**, **50582 assertions**. Independent
  measurements verify sphere/ellipsoid shape, finite unit outward normals,
  unchanged start/end/diameter/head shoulder, no planar rear triangles, and
  every welded edge owned by exactly two faces. Both .65 and 1.30 widths also
  pass at a .01-unit clipped length; zero/full taper and invalid input remain
  explicitly covered. Expected vertex counts change only for the requested
  shape, not wire/gameplay/scheduler/camera expectations.
- Actual production owner/GL rounded-rear test: **37 assertions / 1 case**.
  At 640x480, changed-pixel heights at rear / mid-cap / full body are **12 / 26 /
  28** for local .65 and **22 / 50 / 54** for remote 1.30. Thus the rear starts
  narrow and grows smoothly, rather than appearing at full width. No retained
  head light/glow masks this test; it remains visible after debug time=1000,
  resets to exact baseline, and existing six-view/depth/mixed-state checks pass.
  These are GPU pixel measurements, not a user's new manual confirmation.
- Focused Debug weapon/scene/life/geometry regressions: **183 passed, 4 optional
  read-only asset skips**, **78053 assertions**. Final Release offline suite after
  the last production edit: **2710 passed, 39 capability/asset/device skips**,
  0 failed / 2749 results, 126.22 seconds. Five fake-process/startup fixtures
  separately serialized: **5/5**, 9.32 seconds. Combined **2715 passed, 39 skipped**,
  0 failed / 2754 results. Skips remain skips, not live/device/asset proof.
- `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`: **41/41**, including core/API mesh tests and
  architecture guard, 39.18 seconds. Actual launcher `-CheckOnly` with reference /
  off + frozen tracer + unlimited time passed; launcher hash is unchanged.
  `git diff --check` clean, staged diff empty, branch/HEAD unchanged.
- Preservation snapshot: **1198 unchanged, 6 intended source/test/doc edits,
  0 missing** of 1204 files. No game API, HL profiles, owner flight/retention,
  renderer shader/state, launcher, assets or external worktrees edited.
- ASan runtime not_run / previously unconfirmed; no unrelated infrastructure
  repair or claim of live proof. New build manual=not_run, live launches=0,
  staging/commit/push=none.

Current normal `build\bin\Release\hlclient.exe` SHA-256:
`B3E95A0FC7B71EB70ACB103C08197AB87EB988C70879B53259C15B542A42BC54`.
The same manual command above can inspect the new rounded rear; without
`-DebugFreezeTracer` the rounding also applies to ordinary moving 3D tracers.
This hash is not attributed to the user's earlier flat-cut screenshot.

### Reversed 3D shape orientation — 2026-10-03

User-requested shape reversal: the rounded end now leads at the owning line's
`end`, and the 35% pointed cone trails at its `start`. Only the shared mesh's
private generation axis is reversed; the input line, world flight direction,
random muzzle start, clipping/hit, local/remote width, length, speed, lifetime,
fade, front glow/light and effect identity remain unchanged. Outward CCW winding
and normals follow that private axis, not a negative-scale reflection that
would break culling. No new flag or mutable owner/game API state is introduced.
Historical `head_taper_fraction` remains the pointed-portion length scalar;
flat billboard shape/shader/behavior stay as before.

Normal application 3D and `-DebugFreezeTracer` both use this orientation. The
previous rounded-rear report/hash remains historical; it is not rewritten as
the new shape. Pre-edit snapshot: `e15-reversed-tracer-shape-20261003`, 1204 files.
New build manual=not_run; new live game/Steam/HLDS/capture sessions=0;
staging/commit/push=none. Final checks and new Release hash follow below.

Reversed-shape verification (VS2022 / Win32):

- Release application/helpers, affected Debug application/tests, and existing
  core-only Release all built successfully, one build at a time.
- Shape/frozen/3D regressions: **39/39**, **50842 assertions**. A new independent
  seven-direction oracle finds the pointed rear at the original `start` and
  rounded leading pole at the original `end`, with outward normals and intact
  caller fields. Closed/watertight geometry, short-cap sphere/ellipsoid checks,
  unchanged diameter/bounds, zero/full taper and invalid input remain covered.
- Actual production owner/OpenGL leading-cap test: **1 case / 37 assertions**.
  Leading / mid-cap / full-body pixel heights are **12 / 26 / 28** for local .65
  and **22 / 50 / 54** for remote 1.30. Frozen time=1000 still retains the shape;
  reset returns to exact baseline. Six-view, pointed-rear, depth rejection and
  mixed flat/solid state checks pass. These are GPU pixel tests, not manual proof.
- Focused Debug: **184 passed, 4 optional read-only asset skips**, **78313
  assertions**. Final Release offline suite after the last production edit:
  **2711 passed, 39 capability/asset/device skips**, 0 failed / 2750 results,
  146.47 seconds. Five fake-process/startup fixtures separately serialized:
  **5/5**, 14.35 seconds. Combined **2716 passed, 39 skipped**, 0 failed / 2755
  results. No stock or live sessions were used by these fake fixtures.
- `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`: **41/41**, including the same mesh/core/API
  tests and architecture guard, 34.71 seconds. Reference/off launcher
  `-DebugFreezeTracer -NoTimeLimit -CheckOnly` both pass and select the normal
  Release executable. The launcher is byte-for-byte unchanged.
- Snapshot preservation: **1198 unchanged, 6 intended source/test/doc edits,
  0 missing** of 1204 files. `git diff --check` clean, staged diff empty,
  branch/HEAD unchanged. Owner/game profiles, shaders/state, assets and other
  worktrees untouched.
- ASan runtime not_run / previously unconfirmed. New build manual=not_run;
  live game/Steam/HLDS/captures=0; staging/commit/push=none.

New normal `build\bin\Release\hlclient.exe` SHA-256:
`77282BD5AED2EE0F946387B9445F42DEBD06159808AA32DD42BEC993F955324F`.
No new flag is needed: ordinary moving 3D tracers and the existing frozen
inspection command above both have a rounded front and pointed rear. This hash
is not attributed to any previous user screenshot or manual session.

### Longer tracer with early rear taper — 2026-10-03

User requested 50% more length and rear sharpening beginning 10% from the
leading rounded end. Both normal Half-Life MP5 profile sites now select **48
world units** (32 x 1.5) and a **90% rear cone**, leaving the rounded cap/body
within the first **4.8 units / 10%**. A clipped shorter segment keeps the same
10%/90% ratio. The generic mesh mechanism already supports this proportion;
it is not replaced or coupled to Half-Life. The retained flat variant and all
other caller profiles remain unchanged.

Local/remote width .65/1.30, stable random start 0..16, speed 12000, lifetime
.078 seconds, color/fade, glow/light, cadence, hit clipping, authority and
frozen debug ownership are unchanged. No new flag or asset is required.
Pre-edit snapshot: `e15-long-taper-tracer-20261003`, 1204 verified files. Offline
baseline `[e15],[geometry]`: 70 passed, 1 optional asset skip, 56400 assertions.
New build manual=not_run; live/Steam/HLDS/captures=0; staging/commit/push=none.
Final verification and new normal Release hash follow below.

Longer/early-taper verification (VS2022 / Win32):

- Normal Release application/helpers, affected Debug application/tests and
  existing core-only Release build passed. Production changes are only the two
  Half-Life profile sites (length 48, taper .90); generic owner/renderer/API,
  resources, launcher and other worktrees are untouched.
- Focused final Release `[e15],[geometry]`: **71 passed, 1 optional asset skip**,
  **60154 assertions**, including production local/remote module composition,
  frozen owner and actual-context OpenGL regressions. Independent mesh slices
  at 15%, 55% and 90% verify progressive rear narrowing after the measured
  4.8-unit shoulder, both diameters, unit outward normals and the round front.
  The new test has **3754 assertions**. Its initial shoulder vertex expectation
  was corrected from 48 to 60: each wedge has three body plus two cone shoulder
  vertices; shape/location/normal/slice assertions were not weakened.
- Focused Debug: **185 passed, 4 optional asset skips**, **82067 assertions**.
  `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`: **41/41**, including new generic geometry
  coverage and the architecture guard, 46.42 seconds.
- An initial full run exposed a stale separately linked core/API test binary
  retaining that initial vertex expectation, and an unrelated log-cancellation
  fixture timed out under parallel load. The normal core/API executable was
  rebuilt. Log cancellation passed in isolation with unchanged assertions and
  deadlines; the timeout's underlying cause is not established. No log/process
  production code changed. All six process/timing-sensitive checks were then
  serialized, not skipped.
- Final complete Release offline suite with current test executables:
  **2711 passed, 39 capability/asset/device skips**, 0 failed / 2750 results,
  180.81 seconds; serialized fixtures **6/6**, 13.33 seconds. Combined **2717
  passed, 39 skipped**, 0 failed / 2756 results. Skips are not live/device proof.
- Launcher reference + frozen/no-limit and ordinary off `-CheckOnly` pass;
  launcher bytes unchanged and normal Release path retained. Source snapshot:
  **1197 unchanged, 7 intended source/test/doc edits, 0 missing** of 1204 files.
  `git diff --check` clean, staged diff empty, branch/HEAD unchanged.
- ASan runtime not_run / previously unconfirmed. New build manual=not_run;
  new live game/Steam/HLDS/captures=0; staging/commit/push=none.

New `build\bin\Release\hlclient.exe` SHA-256:
`4EC0CA9CB87E70A2EE5C92AF15D06BD6CD8F39B56C7493AFCB5BC1F0198132CF`.
Existing ordinary and `-DebugFreezeTracer` manual commands need no new parameter.

### Rounded 20%-diameter rear — 2026-10-03

User requested the rear taper to stop at 20% of body diameter, then round that
narrow end, instead of an acute cone. Both Half-Life MP5 profiles now select
owning `taper_end_diameter_fraction=.20` through the existing GameClientAPI,
validated WorldImpactPresentation and RenderWorldLine. The generic mesh emits
a frustum plus a four-ring rear hemisphere; cap length is min(rear radius,
half taper length), so clipped lines use a half-ellipsoid. Both caps stay inside
the original endpoints. The 20% is the diameter of the rear cap's equator,
not a flat terminal disc. No sharp cone surface reaches its pole.

Neutral default zero retains prior sharp solid geometry; flat/zero-taper
callers retain their original shape. The renderer receives no MP5 names or
sample/asset policy. Fixed mesh capacity increases to 648 vertices / 216
triangles; the existing GL path uploads actual emitted vertices, with unchanged
shader/state restoration. Local/remote 20% rear diameters are .13 / .26 units.

48-unit length, 10% taper shoulder, rounded front, widths .65/1.30, stable
start offsets, speed/lifetime, fade/light/glow, cadence, clipping, authority
and debug retention remain unchanged. Snapshot: `e15-rounded-20pct-rear-20261003`,
1204 verified sources. Baseline `[e15],[geometry]`: 71 passed, 1 optional skip,
60154 assertions. New build manual=not_run; live/Steam/HLDS/captures=0;
staging/commit/push=none. Final verification and Release hash follow below.

Rounded-narrow-rear verification (VS2022 / Win32):

- Normal Release application/helpers/all current test binaries, affected Debug
  application/tests and existing Half-Life-disabled core-only Release built.
  The first test compile encountered the Win32 `near` macro; renaming that test
  local to `rear_pixels` fixed syntax without changing production/assertions.
- New CPU + actual-context GL tests: **2/2**, **24517 assertions**. CPU geometry
  measures the .13/.26 rear equators, rounded hemisphere/short half-ellipsoid,
  unchanged 10% shoulder, frustum radius, finite outward unit normals, CCW
  triangles and every welded edge shared twice, for widths .65/1.30 and lengths
  48/.01. There is no flat rear disc and no extension beyond owning endpoints.
  Neutral omitted-diameter sharp-cone and original flat regressions stay intact.
- Actual production owner/GL cap A/B: **49 assertions / 1 case**, 640x480.
  Changed-pixel heights at 15% / 60% / equator of the cap are **4 / 10 / 10**
  local and **10 / 20 / 22** remote. Narrow round cap is wider than the old acute
  cone there, grows rather than a planar cut, persists at frozen time=1000,
  repeats exactly, is hidden behind a wall and resets to exact baseline.
  Redundant verbose RGBA pretty-print was replaced with the bounded measurement
  summary. These are GPU measurements, not human manual acceptance.
- Focused Release `[e15],[geometry]`: **73 passed, 1 optional asset skip**, **84697
  assertions**. Focused Debug: **187 passed, 4 optional skips**, **106610
  assertions**, retaining weapon/audio/light/camera/life regressions.
- Initial full parallel run failed only the existing PowerShell 5.1 fake-peer
  publication fixture: packet-limit marker absent. It passed in isolation with
  unchanged assertions/deadlines; underlying cause is not established, and no
  launcher/process/network code was changed. Both publication fixtures joined
  the existing process-sensitive serialized group; they were not skipped.
- Final full Release offline coverage after the last production edit: **2711
  passed, 39 capability/asset/device skips**, 0 failed / 2750 results, 109.69
  seconds; serialized group **8/8**, 24.62 seconds. Combined **2719 passed, 39
  skipped**, 0 failed / 2758 results. Skips are not device/live/asset proof.
- `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`: **41/41**, same neutral API/mesh tests and
  architecture guard, 39.42 seconds. Reference + frozen/no-limit and ordinary
  off launcher `-CheckOnly` passed; launcher hash is unchanged and selects the
  normal updated Release executable. Main passes the owning module impact at
  the existing submit/append-lines boundary; no alternate application path.
- Snapshot preservation: **1192 unchanged, 12 intended source/test/doc edits,
  0 missing** of 1204 files. `git diff --check` clean, staged diff empty,
  branch/HEAD unchanged. No assets, shader, transport, gameplay authority,
  launcher or other worktree edits.
- ASan runtime not_run / previously unconfirmed; new build manual=not_run.
  Live game/Steam/HLDS/captures=0; staging/commit/push=none.

New normal `build\bin\Release\hlclient.exe` SHA-256:
`34161C6A5C4E29DACEA3324A2204627C171D7578AEF28004E9D22B371B0F5038`.
The same normal and `-DebugFreezeTracer` commands apply; no new flag is needed.

### Requested tracer lifetime increase — 2026-10-06

Local and remote MP5 profiles now use 0.1014 s instead of 0.078 s (+30%).
Travel speed, immutable hit clipping, 48-unit length, local/remote diameters,
rounded geometry, start offsets and glow remain unchanged. This is the maximum
causal lifetime: reaching the endpoint still retires a moving streak earlier.
The new build needs manual validation. Shared verification/Release identity
for this request is recorded in the E20 flight-presentation follow-up below.

## 2026-10-06 shot-audio cache correction

`attribution=user_report`: MP5 primary/secondary and .357 shots are silent in
both clients. The 64-name approved local-audio quota was smaller than the current
74-name client preparation set. Resource admission now permits 128 names while
preserving the other cache/voice/resource bounds. Primary hks1/hks2 selection,
gain/pitch, cadence and visuals are unchanged. Local and observer isolated PCM
regressions pass after the correction. See the current source, verification,
Release identity and pending manual handoff in
[the E2 shared correction record](LOCAL_WEAPON_AUDIO_E2.md).
