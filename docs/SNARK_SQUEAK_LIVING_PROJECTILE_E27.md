# E27: Snark / Squeak living projectile

Result: **snark_squeak_living_projectile_integrated**, offline verified.
E27 manual=not_run; E26 manual=passed, attribution=user_report.
Date2026-10-08. Worktree `D:/DEV/CPP/HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD `00505f71ff2547a9a6b083e53edf69ec45d48213`.
Origin `https://github.com/Pvitaly91/hl-client-engine.git`.

## Pre-edit audit and preservation

Index empty; all pre-existing modified/untracked files preserved; no applicable
AGENTS.md. Other worktrees only inspected. No hlclient/HLDS/MSBuild/cl/CMake
process running at pre-edit check. Existing Win32/v143 VS2022 multi-config
build/core-only trees retained, no cleanup. Initial `git diff --check` passed.
1284 ordinary source files copied/hash-verified using the existing mechanism to
`manual-artifacts/task-records/e27-snark-20261008/source`, manifest beside it.
SDK gitlink and actual revision both `b1b5cf5892918535619b2937bb927e46cb097ba1`.
No submodule update, asset copying/download, checkout or Git mutation.

Pre-E27 generic inventory/HUD, Studio/Bounce history, wrapped yaw, remote
p-model merge, server sound/pitch/removal and E13/E14 existed. No Snark local
presentation existed. TE_BLOODSPRITE decoder existed but committed effect
staging/display were missing. New work extends those same G1 seams.

## Pinned reference and contract

Reviewed SDK: dlls/squeakgrenade.cpp, weapons.h, weapons.cpp, client.cpp,
util.cpp/util.h; cl_dll/ev_hldm.cpp, hl/hl_events.cpp, hl/hl_weapons.cpp,
hl/hl_baseentity.cpp; pm_shared/pm_defs.h and common/const.h. These are behavior
references, not copied implementation bodies or stock-binary parity evidence.
No ReHLDS/Xash/proprietary dumps.

ID15 weapon_snark: server registry ammo `Snarks` (no fixed ammo index), max15,
default give5, no clip(-1), flags24 LIMITINWORLD|EXHAUSTIBLE, slot4/position3.
v_squeak=viewmodel, p_squeak=remote held, w_sqknest=nest pickup,
w_squeak=living creature RUN sequence3. Slots/event indices are negotiated.
HUD/selection read canonical ownership/catalogue/AmmoX, not local arithmetic.

Normal path: exact newly submitted command -> GameClientHost ->
HalfLifePresentation/LocalWeaponPresentationController -> imported Studio pose.
Matching prediction entry freezes command origin/hull; canonical pose is used
only when prediction is unavailable, never a later camera position. HAL local
point trace (hull2) uses origin+18Z if ducked, forward20→64, self excluded,
!startsolid/!allsolid, fraction>.25 -> THROW5/body0. Event-only path uses
committed event origin/duck/angles. Blocked/free receipts deduplicate echoes;
event is an attempt, not ammo/world confirmation. Fresh AmmoX consumption
confirms only submitted actions. No local projectile, ammo mutation or event
squeak. Missing/invalid command pose safely skips with a reason counter.

Server differs: downward aim blends yaw/view-forward, duck18*(aimDown+1),
trace24→60/fraction>0; creates monster_snark at endpoint, velocity
blendedForward*200+playerVelocity. AI/Look512/chase/Bounce physics/health/bite/
damage/pop/15s lifetime are HLDS-owned. No client monster fuse or local pop.

Bounded visual JustThrown-equivalent (not a claimed network field): cadence.3s,
recovery1s; released attack buttons allow UP4 then imported idle. Last zero
ammo retires after recovery, not before THROW. R/RMB add no action. Local
death/focus/model/map cleanup never deletes server creatures. Generic ordinary
Studio interpolates committed frames/position/wrapped yaw and retains
framerate/animtime/controllers/blending; independent stock frame-clock parity
is not claimed. Current-list/model0/NODRAW/slot generation own removal. No
Snark corpse/ragdoll or invented grenade fireball. E13/E14 authority unchanged.

Audio stays canonical server pipeline with spatial origin, pitch, channel and
bounded voice/lifetime: squeek/sqk_hunt1/2/3.wav, sqk_deploy1.wav (bite),
sqk_die1.wav, sqk_blast1.wav, common/bodysplat.wav. EV_SnarkFire contains no
audio. Deploy/throw100/105, rising hunt/bite and bodysplat200 use transmitted
pitch; no timer/contact inference or local duplicates.

## Production details and approved asset findings

The new local profile is `include/hlclient/games/halflife/snark_profile.hpp`;
implementation is `src/games/halflife/snark_presentation.cpp`, routed by the
existing local controller/HL presentation/inventory. Same-index model rebinds
advance a fresh observation barrier; stale callbacks cannot attach to a new
resource revision. Eight owning pending attempts, eight receipts and sixteen
event tombstones bound work and replay. Distinct event-only callbacks are not
lost behind a single pending slot. Overflow is explicit unavailable, not a
world spawn. Receipts time out after1.25s; their timeout is not a monster fuse.
The pinned shared client weapon's no-hit trace stub sets JustThrown even when
the separate real EV trace rejects THROW. Thus a blocked submitted command can
still recover UP after1s/release, without success/ammo/world claims. A blocked
event-only callback does not invent that state. Fresh server UP/idle/DOWN keeps
its own occurrence identity and cannot be restarted by provisional recovery.
Pose correction alone never confirms an accepted throw. Canonical AmmoX is
the only consumption confirmation. HUD displays SNARKS without a magazine;
pickup notification never adds ammo a second time.

TE_BLOODSPRITE now preserves committed position, both signed sprite refs,
palette byte and full scale byte through replay's atomic commit. HAL accepts
palette195(yellow)/247(red), positive scale and slots1..1023; unsupported palette
or invalid components are typed skips. The existing transient pool presents
two sprites under one causal identity, without an extra explosion/audio event.
The normal application materializes only approved session sprite slots. A
missing component does not fabricate an asset or suppress the ready component.

Actual approved read-only import found ALPHA_TEST bloodspray/blood, not
INDEXALPHA. Covered texels are all grayscale; colored blue texels occur only
in transparent padding. An initial whole-image grayscale diagnostic counted
those transparent texels and was misleading. The extra intensity-recolor mode
introduced during that investigation was removed with scoped source edits:
it was unnecessary and could brighten filtered edges. No installed palette/
image was edited, no global color-key or importer change. The existing shader
modulates the verified covered grayscale texture by neutral HAL tint, preserving
mask, coverage, alpha, UV/filtering/depth and all other sprite passes. Yellow RGB(1,.85,0), red
RGB(1,.05,.05). Initial E27 scales .1/.05*wireByte, both10FPS/.6s, second downward32units/s were
explicit local-compatible presentation, not proprietary stock palette/RNG/
particle-motion parity. No blood event -> no blood, pop fireball or local sound.
That initial .6s lifetime was an upper bound, also capped by imported frame
count; the large-yellow profile is superseded by the visibility follow-up below.
Ordinary Studio frames interpolate only forward committed coordinates; a wrap
such as250→5 is held/stepped to the current value because that generic history
does not consume imported loop metadata. No local framerate/animtime advancement
or whole-cycle reconstruction during RX gaps is performed. Movement and wrapped
yaw remain interpolated; this is not stock-exact continuous Studio frame parity.

All four assets imported completely through `RuntimeReplayLocalAssets`:

| Virtual model | Bones | Bodyparts/submodels | Textures | Skins | Sequences | Sources |
|---|---:|---:|---:|---:|---:|---:|
| models/v_squeak.mdl |75|2/2|8|1|6|1|
| models/p_squeak.mdl |12|1/1|2|1|1|1|
| models/w_sqknest.mdl |15|1/1|1|1|2|2|
| models/w_squeak.mdl |15|1/1|2|1|4|2|

Each has zero attachments/controllers and one sequence group; the world models
require verified same-root w_sqknestT/w_squeakT texture companions, not external
animation groups. P-model's twelve bones are merged with the actual server
player pose by the existing generic attachment mechanism. Main-model source
movement/clipping bounds are zero; imported sequence geometry bounds are used,
not guessed player-sized creature bounds. RUN bounds approximately
[-7.685,-8.515,-.162]→[7.039,7.75,10.769]. Physics bbox[-4,-4,0]→[4,4,8] is server
evidence, never a substitute for the rendered model geometry.

Verified v_squeak sequence names / frames / FPS:

| Index/role | Label | Frames | FPS |
|---|---|---:|---:|
|0 idle|idle1|31|16|
|1 fidget|fidgetfit|71|16|
|2 fidget|fidgetnip|81|16|
|3 holster|down|21|16|
|4 deploy/recovery|up|51|30|
|5 throw|throw|14|24|

The HAL metadata gate verifies these semantic labels/body0 and finite imported
frame/FPS metadata. Production timing comes from the imported metadata; owned
fixtures deliberately use distinct timings. w_sqknest idle1 has16frames@30FPS;
w_squeak run3 has9frames@45FPS; p_squeak one refsequence has1frame@30FPS.
Exact service DOWN is supported; a canonical weapon/model switch supersedes
the old pose immediately, with no extra gameplay switch delay.

Seven approved WAVs decode to finite nonzero mono float PCM, no loops. blast
is22050Hz/.899s; bodysplat11025Hz/1.903s; die11025Hz/.278s; hunt1/2/3
11025Hz/.298/.147/.155s; deploy(bite)11025Hz/1.105s. Actual decoded peaks
.852–1.0 and isolated production-mixer peaks1.0 were observed. The existing
decoded API does not expose original WAV bitdepth; none is invented. Silent
normal SDL startup was observed ready with1920 generated frames/480 queued,
zero queue failures and no started voices. This is device startup evidence,
not audible Snark/game listening confirmation.

Owned fixtures cover clearance/crouch/downward/immutable pose, threshold.25,
event-before-command/batched events, blocked recovery, authoritative correction,
last-ammo retirement, rebind/focus/death/reset,32 local cases;11 world cases cover
mixed wire player/Snark/Hornet/RPG/grenade/Satchel/mine/beam/sprite transactions,
malformed rollback, independent histories, removal/reuse, remote p-model merge,
spatial pitch PCM and blood with no fireball. FPS30/60/144/240 use equal command
timestamps, not render-tick cadence. Optional actual-resource tests use the same
approved importer, scene, shader, mixer and backend, not alternate executables.
Final Debug/Release/full-suite evidence is recorded below.

## Build and verification commands

Normal VS2022/Win32(v143), existing multi-config build (no clean/reset):

```powershell
cmake --build build --config Debug --target hlclient_tests --parallel 1
cmake --build build --config Release --target hlclient_tests --parallel 1
pwsh -NoProfile -File scripts/build_source.ps1 -Configuration Release -Parallel 1 -TestOnly
```

Core-only tree is configured with `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`:

```powershell
cmake --build build-core-g1 --config Release --parallel 1
ctest --test-dir build-core-g1 -C Release --output-on-failure --parallel 1
```

Actual executions use the bundled VS2022 CMake path and serial MSBuild parameters
`/p:CL_MPCount=1 /p:BuildInParallel=false /verbosity:quiet`. Independent outputs
were never built twice concurrently. One attempted overlapping Debug build was
rejected; work continued serially after checking compiler processes had exited.
No foreign process was terminated and no other worktree modified.

New ordinary project files: this task record; `snark_profile.hpp`,
`snark_presentation.cpp`; `snark_test_fixture.hpp`, `snark_world_test_fixture.hpp`;
`test_snark_local.cpp`, `test_snark_world.cpp`, `test_snark_assets.cpp`.
Existing normal paths changed: controller/presentation/inventory, shared API
owning command pose, live TX notification/main handoff, neutral committed
temporary-effect data/replay staging, HAL temporary-effect policy/application
sprite materialization, root/test CMake and the existing TX integration test.
ARCHITECTURE and E26 records append contracts/acceptance. The renderer, its
instance header and frame signature source have no net E27 changes and match
the preserved pre-E27 source bytes. No server, launcher or installed asset changes.

## Final Release and sanitizer evidence

After the last production edit, full Release offline suite:
**3155 passed,54 skipped,0 failed /3209**,485.41s. The entire TestOnly wrapper
completed successfully, including normal version/help/Null bootstrap, offline
fake transport, native/PowerShell summary, no-stock launcher and SourceOnly
CheckOnly. Both historical flaky M4.4/evidence-pending checks passed this run;
no assertions, thresholds or timeouts were weakened.

Focused final E27 Release:43 passed/2 opt-in skips,11522 assertions. Separate
approved-resource opt-in:2/2 passed,1074 assertions. Actual blood GPU controls:
spray715 changed/yellow pixels, gray control574 pixels; blood91 changed pixels,
86 yellow pixels, gray control45. Alpha/coverage were identical; red intensity
was unchanged rather than brightened; background76085/76709 pixels untouched;
repeated draw and expiry restored baseline. CPU covered texels all gray, with
6958/235 covered and34002/2069 transparent chromatic texels respectively.
Synthetic owned GL, actual six viewmodel sequences, p/nest/living models and
isolated server PCM tests passed. Silence-only SDL startup is not listening proof.

Full-suite skips are explicit installed-resource/device/WFP opt-ins. Mandatory
owned CPU/GL/PCM regressions do not depend on game assets or listening devices.
New live/stock/HLDS/Steam/WFP sessions=0. Real Snark pursuit/bite/pop/kill and
subjective sound remain manual=not_run.

ASan normal application and **HL-free core API target** both compiled in the
existing Win32 `build-asan` RelWithDebInfo configuration. Both runtime smoke
attempts exit signed-1073741515 /`0xC0000135`, before useful output (zero-byte
logs). Runtime is blocked/not confirmed, not passed. No runtime DLL copying,
PATH workaround, self-elevation or ASan infrastructure repair was performed.

Final targeted Debug E13–E27:479 passed/16 explicit asset opt-in skips,203306
assertions/495 cases,0 failed. Normal Debug application and tests compiled.
Final game-OFF Release **entire core tree** compiled and42/42 tests passed,
42.19s, including HL-free same-host test module and architecture guard.
No Half-Life module linkage or fallback was required.

Default crossfire/reference/peer/unlimited `-CheckOnly` passed and selected the
normal Release path below; native peer contract reports process-launches=0.
Complete no-stock launcher fixture passed separately and again in full Release.
`git diff --check` passed. Original source snapshot1284 files:0 missing,20
changed by E27;8 new ordinary project files. Removed recolor-only hunks left
renderer/instance-header/signature source byte-identical to pre-E27 snapshot.
No unrelated files were deleted/overwritten; index remains empty and
branch/HEAD/SDK pin unchanged.

Ready executable: `D:/DEV/CPP/HLC-steamcfg-5e48b7c1/build/bin/Release/hlclient.exe`,
3905024 bytes, UTC2026-10-08T18:50:14. Prepared required helper binaries are in
the same Release directory; no hidden flag, alternate debug executable, asset
copying or changed launcher is required.

SHA-256:
`54AD66B747A9AB102BC12676520A9D71B65AE832C1E6BD09C71543C2B18BCE79`.

Manual next: check nest versus living model, local throw/UP/last retirement,
blocked/crouched/downward throws, both remote p-model/world views, independent
multiple Snarks, hunt/bounce/bite/pitch/blast/blood/no fake fireball, thrower
death and server-owned E13/E14 death/respawn/scoring. Crossfire weapon
availability is server/map-owned; no give/cheat/server configuration was added.
All16 E27 manual evidence categories above remain not_run; optional early kill,
water, exact yellow/pitch and owner-death continuation need separate observation.
Known boundaries: server-sampled rather than independent continuous Studio
frame clock; local-compatible blood RGB/motion/lifetime; observed removal/reuse
boundaries rather than unavailable server serial identity; synthetic PCM and
silent device startup rather than subjective listening. These are not universal
GoldSrc mod or proprietary stock-engine parity claims.

New live/stock/HLDS/Steam/WFP sessions=0. Staging/commit/push=none. No E28 work.

## Manual history and handoff

E26 manual=passed; attribution=user_report: new request explicitly confirms
19/19 required scenarios. Old E26 reports remain unchanged; optional conveyor/
moving-platform cases remain not_run without separate confirmation.

```text
snark_throw_manual=not_run
snark_rejection_manual=not_run
snark_pickup_manual=not_run
snark_world_model_manual=not_run
snark_animation_manual=not_run
snark_hunt_manual=not_run
snark_bounce_manual=not_run
snark_audio_manual=not_run
snark_bite_manual=not_run
snark_pop_manual=not_run
snark_blood_manual=not_run
snark_remote_manual=not_run
snark_multiple_manual=not_run
snark_last_weapon_manual=not_run
snark_kill_respawn_manual=not_run
snark_water_manual=not_run
```

Default crossfire/two clients/unlimited:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Map/server weapon availability stays server-owned. Existing gentest_mp external
all-weapons option is preserved, not a changed default. No E28. New live/HLDS/
stock/Steam/WFP sessions=0; staging/commit/push=none.

## E27 pop visual follow-up (2026-10-08)

New user evidence, attribution=user_report: Snark disappears; its pop sound is
heard, but no visual effect is seen. `snark_pop_audio=heard`,
`snark_pop_visual=not_observed`. This is partial manual feedback, not full E27
acceptance. Historical build/manual=not_run entries above are preserved.

Closest identified run: b02990f0343f42f8b55f8bfab91c8e27, crossfire, HLDS15148,
client34144/peer15092, directory creation UTC20:04:40.0163251, publication about
20:05:59. Client completed, exit0, primary_error=none; owned cleanup exact,
scoped restoration exact, cleanup_error_count0. Current54AD66... Release matches
the selected executable path, but no historical client SHA is independently
published; no exact past-byte attribution is fabricated.

Published client log is16KiB from observed1,386,794 bytes/11,662 lines; peer is
also truncated. No retained record establishes receipt, palette, slots, scale or
rendering of TE_BLOODSPRITE at this pop. Aggregate successful audio does not
prove visual-event delivery. The raw in-memory pipe journal is not a replay
capture. Precise unknown boundary: server committed blood event -> host clock ->
HAL acceptance -> approved sprite materialization -> draw/depth visibility.

Read-only SDK/source audit: ordinary Snark death invokes blast + yellow blood
and bodysplat, not a grenade fireball; out-of-world disappearance is silent.
Blood is MSG_PVS opcode115, palette195, multiplayer scale16. `violence_ablood=0`
suppresses blood. Research config.cfg has that value, but actual dedicated
launch has no config exec/override; server.cfg/autoexec do not establish it.
Thus the file-at-rest value is a candidate, **not a proven runtime root cause**.
No static-world draw, cull-state or clock mismatch defect was found. The earlier
asset GPU fixture in empty space does not prove map-depth/ground visibility.

This follow-up fixes the **missing retained diagnostic evidence**, not a proven
Snark visual defect. No local disappearance/fuse-based pop, invented fireball,
resized/louder arbitrary fallback, server cvar override or configuration write.
The normal app always emits one bounded `[blood-effects-summary]` at completion,
and the publisher retains its newest strictly validated numeric row before
verbose output for both clients. It distinguishes committed host receipt,
clock drop, module duplicate/invalid/generation/palette/capacity/late reasons,
tag-isolated missing/unsupported/rejected sprite frames and render batches.
Materialized/draw counters are not claims of visible GPU pixels or PVS delivery.

Opaque diagnostic tags affect counters only; they never change asset access,
rendering, timing or gameplay. The functional HLDS argv also issues a value-less
`+violence_ablood` query after map/status, with no assignment. Only the server's
exact bounded0/1 console forms can become `[blood-server-setting]`; client/peer
lookalikes, arbitrary paths, trailing/private fields, malformed/reordered or
overflow counters are rejected. Unknown engine output spelling remains unknown.
SDK/Steam/server/cfg files remain untouched; new live/HLDS/stock/Steam/WFP
sessions0, staging/commit/push none. Visual root cause still requires the next
user-run evidence from the prepared diagnostic build; it is not marked fixed.

Follow-up offline verification completed2026-10-09(client Europe/Kiev):
Release49 passed/2 resource opt-in skips,12043 assertions; targeted Debug
E24–E27/diagnostics160 passed/5 skips,34861 assertions. Native strict publisher
fixtures passed Debug and Release, including per-field malformed values,
counter overflow, exact query argv, server-only setting provenance, priority,
role isolation and injection/privacy rejection. All six added cases are
ordinary project tests; existing assertions were not weakened.

After the final production edit, full Release suite **3161 passed/54 skipped/
0 failed out of3215**,508.56s. The entire TestOnly wrapper passed normal
version/help/Null, fake transport, native/PS summaries, no-stock launcher and
SourceOnly CheckOnly. Game-OFF full Release tree and core tests42/42 passed,
53.60s, architecture guard and alternate-module delivery included. Approved
Snark asset/GPU/PCM/silent-SDL opt-in repeated2/2,1074 assertions; this does not
prove event delivery in the reported live scene. ASan runtime remains the prior
unconfirmed0xC0000135 limitation; no infrastructure repair or new ASan runtime
success is claimed for this diagnostic follow-up.

New preserved snapshot:1292 ordinary sources under
`manual-artifacts/task-records/e27-pop-diagnostics-20261008/source`, copied and
hash-verified before edits;0 missing,13 changed, no unrelated file loss. No new
SDK/assets/config/launcher files. Index remains empty; branch/HEAD unchanged.
`git diff --check` and default reference/peer/unlimited CheckOnly passed.

Prepared diagnostic normal Release:
`D:/DEV/CPP/HLC-steamcfg-5e48b7c1/build/bin/Release/hlclient.exe`.
SHA-256 `22D0746F72AD21A038FC5D31139F1E078F577DE0B3E5CD7237291EA29935E13D`.
Required refreshed orchestrator is alongside it. Same manual command above;
repeat a Snark pop then close both clients and allow cleanup to finish. The
new retained summary/server setting is required to identify the still-unproven
visual root cause. New build manual=not_run; pop visual fix=not_verified.
New live launches0; staging/commit/push none.

## Pop visibility follow-up — 2026-10-09

New attribution=user_report: attack blood now visible; Snark pop visual
absent_or_barely_visible. This is not an all-E27 manual acceptance. Prior
manual=not_run records and E26 acceptance remain unchanged.

Correlated newest run `ace474a5544c4908a6f4061d65814046`, crossfire,
unlimited/reference/two clients: server35304, client2428, peer42212. Both clients
exited0; application completed/primary_error=none. Wrapper still reported
client-connection-rejected; this separate verdict is not a visual root cause.
Owned cleanup=exact, scoped restoration=scoped_exact, cleanup_error_count0.
The selected normal Release path is recorded, but the client's SHA was not
published, so the diagnostic-build SHA is not retroactively assigned to it.
Server query retained violence_ablood=1. Both clients reported committed
blood receipts14/forwarded14/accepted14, zero duplicate/clock/invalid/stale/
palette/capacity/delivery/resource rejection. Primary materialized1682,
draw_batches664; peer2278/901. These aggregate counters do not identify each
individual pop or prove visible pixels; they rule out a general disabled blood
path/resource rejection in this run.

Concrete production weakness: the verified blood.spr is9 frames16x16,
centered(-8,+8); bloodspray.spr is10 frames64x64, centered(-32,+32), both masked
ALPHA_TEST/view-parallel. At multiplayer wire scale16 the old drop half-height
was6.4units and its -32units/s motion sank its entire ground-origin footprint
below the floor by .2s (sooner with pitched billboarding). Snark Killed uses its
feet origin, unlike elevated bite trace hits. The generic alpha-blend smoke
fade dimmed both; the .6s owner cutoff prevented spray frames6–9. This is a
verified presentation defect, not a claim that the retained run proves every
pop event was delivered. No importer/palette/global-key/GPU shader change.

Only committed large yellow events (palette195, wire scale>=8; pinned Snark
death emits SP8/MP16) now select the local-compatible profile:

| Component | Scale | FPS / cap | Initial vertical speed / gravity | Fade |
|---|---|---|---|---|
| spray, transmitted slot1 | min(25.5, .15*wireScale) |10 /1s|80 /160units/s²|none|
| central blood, transmitted slot2 | .1*wireScale |6 /1.5s|0 /0|none|

The spray peaks20units above the actual event at .5s then falls. The central
mark remains at the transmitted origin; existing depth still clips the part
below the floor. Missing components skip independently; imported animation
length also limits lifetime. Small yellow bite and every red blood profile,
smoke/explosion defaults are unchanged. SDK verifies yellow blood/falling
spray and a1–2s second sprite, not these exact scales/FPS/kinematics: this remains
a two-sprite local-compatible approximation, not stock engine's plural-spray
RNG/physics. No grenade fireball, light, spark, added sound, disappearance/fuse
inference, AI, damage or server change. All audio remains server-owned.

Source snapshot1292 ordinary sources hash-verified before edits under
`manual-artifacts/task-records/e27-pop-visibility-20261009/source`.
New build manual=not_run; new live/stock/HLDS sessions0; staging/commit/push none.

Visibility follow-up verification, after the final production edit:

- VS2022/v143/Win32 normal Release and Debug app/tests/orchestrator built.
- Release Snark/temporary-effects:80 passed,4 opt-in skips,16065 assertions.
- Debug E24–E27/temporary-effects:188 passed,7 opt-in skips,38808 assertions.
- Full Release TestOnly suite:3163 passed,55 skipped,0 failed out of3218,
  609.29s. Complete wrapper version/help/Null/fake transport/native and PS
  summary/no-stock launcher/SourceOnly CheckOnly post-checks passed, exit0.
- Game-OFF core-only Release build and42/42 tests passed,200.14s, including
  alternate-module/core API and architecture guard. No HL linkage/fallback.
- Explicit approved Snark assets/GPU/PCM/silent-SDL audit:3/3 passed,
  1267 assertions, no game launch. At eye64/distance128/age.4, actual assets
  produce11265 yellow changed pixels vs2518 old, horizontal span200 vs125,
  yellow signal3052325 vs406981. At.7 new7648 vs0; at1.2 central28 vs0.
  Ground-depth occlusion, exact baseline after expiry/reset, mask/tint and
  restored GL state pass. Owned10-/9-frame fixtures independently cover the
  same normal production host/owner/materializer/static-world OpenGL chain.
  This is GPU pixel evidence, not a live/user listening confirmation.
- Default zero-gravity smoke retains the original float arithmetic exactly;
  boundary7/8, red profiles, scale255 clamp, invalid gravity, max lifetime,
  duplicate/replay and reset bounds are covered. No older assertion weakened;
  expiry assertions now explicitly test the new1.5s policy at1.6s.
- The standard full-suite55 skips are optional installed-resource, WFP,
  focus/link/corpus capability gates, not passed checks. The three Snark
  resource tests were separately run with the exact approved root opt-in.
  ASan was not rebuilt/run in this follow-up; its prior runtime remains
  unconfirmed0xC0000135 with no output. No ASan infrastructure repair claimed.
- Actual gentest_mp/reference/peer/unlimited CheckOnly passed and reported
  the new Release SHA; unchanged reference/off/Scenario contracts and
  no-stock fixtures passed. Index empty, HEAD/branch unchanged, diff check
  passed. Snapshot reconciliation:0 missing,8 existing files changed,
  one added test file; all unrelated previous changes preserved.

Changed production path: `src/games/halflife/temporary_effects.cpp` selects
the profile through `include/hlclient/game_api/temporary_effects.hpp`;
`src/app/temporary_effect_presentation.cpp` validates/owns it and
`src/app/runtime_replay_local_assets.cpp` executes its bounded kinematics,
fade and expiry via approved sprites. No renderer/shader/audio/launcher edits.
New regression source `tests/test_snark_pop_visibility.cpp` is an ordinary
CMake-registered project file, not a manual/generated implementation.

Prepared normal Release `build/bin/Release/hlclient.exe`:
SHA-256 `AEC5099B6D01795B6CAFF5427E26DCA85E8E22D609C1E63F3A9F1EF29660A8D8`.
Next user manual: let a Snark pop near the floor, observe the yellow spray and
remaining central blood mark; compare attack blood and close both clients
normally. New build manual=not_run until that actual user check.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\gentest_mp.bsp" -RemoteAudioPeer -NoTimeLimit
```

## Pop still absent: separate bite evidence from death evidence — 2026-10-09

New attribution=user_report: `snark_pop_visual=absent`, creatures simply
disappear. The previous visibility adjustment did not resolve the user's
observation. Its offline GPU results remain valid for the injected fixture,
but do not prove a live yellow death event was supplied. Pop visual closure
remains **unresolved**, not passed. Historical manual records are unchanged.
User clarification in this turn: Snarks disappear at the correct original-HL
moment, pop audio is audible; only the visual effect at death is absent. This
does not report a timer, disappearance-lifecycle or audio defect.

Read both new gentest_mp/reference/unlimited/two-client reports:

| Run | Server / primary / peer PID | Blood received / accepted / materialized / draw batches |
|---|---|---|
| `4fea72fd59774f39b447ea00c0a07ea2` |12724 /20628 /2428|13 /13 /1722 /643|
| `1d3539d84f6e49619ec432bc9e4b56c8` |32260 /22364 /38528|10 /10 /1264 /426|

Both report client and peer exit0, application completed/primary_error none,
server violence_ablood1, owned cleanup exact, scoped restoration scoped_exact,
zero cleanup errors. No clock/invalid/stale/palette/capacity/delivery/resource
rejection was reported by the primary blood summary. Peer retained windows do
not contain their terminal blood summary. The reports select the normal Release
path, but do not attest a client SHA; no retrospective SHA is assigned.

The decisive evidence gap: all13 or10 events could be red player bite blood.
The old aggregate neither records color/scale nor separates the yellow pop
profile. An independent source audit found no confirmed drop in decoder115,
transactional publication, clock conversion, queue drain, map/life ownership,
approved materialization or render ordering. This is an **unknown live event
boundary**, not proof of a renderer or server defect. No further visual tuning,
server configuration change, pop inferred from disappearance/sound/timer, or
grenade fireball has been added to conceal that uncertainty.

The diagnostic production change uses the existing four opaque counter groups:
HAL tag1=red247, tag2=yellow195 with scale>=8, tag3=smaller yellow195. Generic
owners/materializers still interpret none of these semantics. The old
`blood-effects-summary` sums1..3 and retains its original total meaning.
One additional bounded terminal `blood-profile-summary` separates accepted
red/small-yellow/large-yellow, and large-yellow owner stale/invalid/eviction,
materialized/absent/unsupported/expired/frame-rejected and rendered-batch counts.
The orchestrator retains its latest strictly validated numeric line for each
client ahead of the verbose excerpt. No paths, samples, handles or per-frame
spam; counters never change visuals, timing, resource permissions or gameplay.

Interpretation: accepted=0 plus zero aggregate rejection means this client did
not deliver a valid large-yellow event to its effect owner; positive accepted
with a typed resource failure identifies a different boundary. A rendered
batch still does not prove visible pixels or identify the originating creature:
TE_BLOODSPRITE has no entity/class identifier. The next user run is needed to
resolve that boundary. Do not label this diagnostic-only release a visual fix.

Pre-edit snapshot1293 ordinary project files copied and hash-verified under
`manual-artifacts/task-records/e27-pop-classification-20261009/source`.
Verification and Release identity follow below.

Review correction: a mixed red/yellow frame failure increments each opaque
profile counter. The legacy aggregate now counts the before/after union once
per materialization call, preserving its original rejected-batch unit rather
than reporting2–3 failures for one frame. No renderer behavior changed.
The initial full run was intentionally interrupted for this correction and
is not counted as final verification; only the restarted final run applies.

Final focused verification after the last production edit: normal Win32/v143
Release and Debug app/tests built; each E27/temporary-effects run81 passed,
4 explicit resource-gate skips,16234 assertions. Strict native summary
publication regression passed in both configurations. Core-only Release
Game-OFF build and42/42 tests passed,44.16s, including alternate-module API
and architecture guard (the later edit was only the HAL application's summary).
Approved read-only actual asset/GL/PCM checks separately passed2 cases,
1264 assertions; the gated silent SDL check was then explicitly run and passed
1 case/3 assertions, playback_ready,1920 silent frames/480 queued, no voices.
Owned and actual GPU tests still prove the injected yellow profile renders,
not that the user's server delivered it. ASan not rerun; prior application
runtime remains unconfirmed0xC0000135, not a new pass.

Only five files changed relative to the1293-file pre-edit snapshot: module
temporary_effects.cpp, application main.cpp, orchestrator main.cpp, existing
test_snark_world.cpp and this record. Zero missing sources, index empty,
HEAD/branch unchanged; no launcher/SDK/installed asset/server configuration
edits. New normal Release SHA-256:
`9D4E82119C8220B4F4A211E6C4EFD72EA9698854542B0AFF04516612E043E057`.
New build manual=not_run; visual issue=unresolved. New live/stock/HLDS sessions0;
staging/commit/push none.

Final restarted full Release TestOnly wrapper completed with exit0:
**3164 passed,55 skipped,0 failed out of3219**,501.46s CTest time. The complete
post-checks passed too: normal version/help/Null, fake transport, native and
PowerShell summary producers/consumers, no-stock launcher fixtures and
SourceOnly CheckOnly. Optional installed-resource/device/corpus/focus/WFP
gates are skips, not passes; actual Snark assets and silent SDL were separately
verified as above. The exact gentest_mp/reference/peer/unlimited CheckOnly
also passed and reported the final `9D4E8211...E043E057` executable SHA.
`git diff --check` passed. Final artifacts are in the classification task
directory: `release-focused-final.log`, `debug-focused-final.log`,
`release-full-offline-final.log`, `core-only.log`, `approved-assets.log`,
`silent-device.log`, `launcher-checkonly-final.log`. The earlier interrupted
full-run log is retained as historical incomplete evidence, not counted.

Manual handoff: use the unchanged gentest_mp command above, observe one actual
server pop, then close the clients and let restoration finish. Its new retained
`blood-profile-summary` is the missing evidence needed to continue the visual
fix; the present change only fixes diagnostic ambiguity, not the absent pop.

## Classified user run36147b9b: no yellow delivery; manual wrapper fix — 2026-10-09

User supplied the terminal report for exact run
`36147b9b517847bfa16adc5f6f19daa7`, gentest_mp/reference/RemoteAudioPeer/
NoTimeLimit. Its preflight explicitly reports client SHA-256
`9D4E82119C8220B4F4A211E6C4EFD72EA9698854542B0AFF04516612E043E057`;
unlike older reports, this attachment does attest the selected executable hash.
Server26236, primary12172, peer29740. Wrapper publication UTC
2026-10-08T23:05:58.1413477Z (2026-10-09 Europe/Kiev). This is user-run
evidence, not a new agent live session or a visual success confirmation.

Both retained client summaries say wire10/forwarded10/received10/accepted10,
red_accepted10, yellow_small_accepted0, yellow_large_accepted0. All clock,
validation, palette, stale, capacity and delivery rejection counters are zero.
Primary red materialized1248/draw391; peer1348/427. Large-yellow materialization,
resource failures and draw counters are all zero. Server query: violence_ablood1.
Both capture metadata records have no byte/line/capture failure; the retained
text is nevertheless a bounded redacted window, **not a raw packet journal**.

This resolves the earlier ambiguity: the ten accepted blood events are red,
not evidence of a Snark yellow pop. The missing visual is upstream of the
large-yellow presentation path in this run, not demonstrated small size, alpha,
resource absence or GPU rejection. The exact unestablished boundary remains
server emission / recipient delivery / pre-commit input. The logs cannot prove
that stock HLDS generated or sent that yellow message; nor may zero delivery
be described as proof of a server implementation defect. Pinned SDK Killed
calls UTIL_BloodDrips (yellow195, MP scale16, MSG_PVS); source reference does
not establish binary parity. No raw protocol bodies were retained to replay.
No new visual tuning, sound/removal/timer fallback, server/cvar/asset edit or
fake grenade explosion was made. **Snark pop visual remains unresolved.**

A separate concrete wrapper defect explains the red terminal exception:
all three processes exited0, application result completed/primary_error none,
native live_visual_verified true. Owned cleanup exact, restoration scoped_exact,
cleanup_error_count0. Native stage was live_visual_control_verified and prediction
coverage limited. The PowerShell stage assertion still required
live_local_prediction_and_reconciliation_verified solely because prediction was
reference, even in keyboard-mouse mode. That erroneous assertion turned the
successful native run into wrapper local_server_ready_client_blocked/exit1.
No client crash occurred. The historical report is retained unchanged.

Production fix: only the keyboard-mouse stage expectation now matches native
semantics: full prediction stage when independently attested, otherwise the
visual-control stage. It does not invent prediction coverage, accept an earlier
startup stage, override child failures or bypass cleanup/restoration. Scripted
prediction, jump/duck and damage/respawn remain strict. The no-stock launcher
regression executes the actual production AST expression plus assertion for
manual/scripted, reference/off and verified/limited/pending/unavailable coverage;
it also rejects incomplete startup and a scripted visual-only claim. Existing
nonzero-child/ambiguous-report fixtures remain intact. The saved pre-edit script
reproduced the exact wrong-stage expectation before the patch.

Scope: scripts/capture_stock_runtime_state.ps1, scripts/test_h2_manual_launch.ps1
and this record only. Their pre-edit copies were hash-verified under
manual-artifacts/task-records/e27-pop-run-36147b9b-20261009/source. No C++ or
executable change; the normal Release retains the attested9D4E...E043E057 hash.
Verification results for the wrapper correction are recorded below.

Verification after the last production-script edit:

- No-stock launcher regression passed, including32 new coverage/input matrix
  cases evaluating the actual production stage expression. The same assertion
  was also run read-only against run36147b9b's retained fields and passed without
  rewriting its limited prediction verdict or historical JSON.
- Complete Release `scripts/build_source.ps1 -Configuration Release -Parallel 1
  -TestOnly` exited0:3164 passed,55 explicit skips,0 failed/3219,532.22s CTest.
  All subsequent version/help/Null, fake transport, native/PowerShell summary,
  no-stock launcher and SourceOnly CheckOnly checks passed too.
- Existing core-only Win32 Game-OFF Release:42/42 passed,53.54s, including
  alternate-module/core API and architecture guard. No build was necessary:
  this turn changed no C++/headers/CMake/executable. Debug was not rerun or
  rebuilt; its previous result remains historical. Optional real-resource,
  device/focus/corpus/WFP gates were not enabled in this turn. ASan was not
  rerun; prior runtime remains unconfirmed0xC0000135.
- Exact gentest_mp/reference/peer/unlimited `-CheckOnly` passed and selected
  the unchanged normal Release hash above. `git diff --check` passed. Index
  empty; branch and HEAD00505f71ff2547a9a6b083e53edf69ec45d48213 unchanged.

Logs: `launcher-regression.log`, `release-offline-final.log`, `core-only.log`,
`checkonly.log` in this task's artifact directory. New wrapper manual=not_run.
New agent live sessions/HLDS/managed captures0; staging/commit/push none.
Do not ask for an identical replay solely to validate the pop: visuals have
not changed. Further closure needs evidence at the missing server-effect
delivery boundary, not another change to size/lifetime or a success claim
based only on synthetic yellow sprites.

### Effect-delivery diagnostic preparation and UAC boundary — 2026-10-09

The user explicitly authorized ONE controlled live session and confirmed they
could pick up/throw a Snark. This does not authorize repeated sessions, raw
packet/auth capture, or an inferred pop from removal, sound alone, or a timer.
The attempted elevated launcher returned Windows "operation was canceled by
the user" before its helper ran: no one-shot marker, run ID, HLDS or client
process was created. No new live evidence or visual-fix claim results from it.
The historical run36147b9b and earlier reports remain unchanged.

The new normal application diagnostic closes an observability gap, NOT the
as-yet-unproven visual cause:

- `RuntimeControlExactFixedBody` retains the count/color tail of the existing
  exactly framed11-byte `svc_particle` body. It remains inert; no new particle
  effect or game rule executes. Existing fixed-control state/hash behavior is
  unchanged. Truncated bodies still reject the record.
- `runtime_effect_diagnostics.hpp` projects only numeric service/type,
  palette/amount, sound slot/entity and flags. `LiveRuntimeStage` emits it only
  after successful atomic runtime-record application, via the existing trace
  callback. No text/control/auth message, native locator or raw service bytes
  are retained. `effect_diagnostics` defaults false; the normal app enables it
  with existing `--net-trace`, already supplied by the managed manual launcher.
- The normal application fingerprints approved manifest virtual sound names
  with FNV-1a64, once per512-bounded slot, so a received sound can be correlated
  offline with the SDK pop sample without persisting a native path/name.
- The existing orchestrator publishes `client-effects-numeric.log` and
  `peer-effects-numeric.log` beside its existing redacted reports. Each retains
  newest128 non-sound effect rows,64 sound rows and64 sample mappings, at most
  64KiB. The header reports observed row counts/retention limits; exceeding a
  limit is explicitly incomplete evidence. Existing16KiB outcome/cleanup
  excerpts retain their previous bounds and reject effect-marker injections.

Ordinary source files contain all required code/tests. The incidental
single-use elevation helper and pre-edit copies are in
`manual-artifacts/task-records/e27-effect-wire-diagnostic-20261009/`; neither is
needed for a clean build or ordinary launcher command. No renderer/profile
size/lifetime, gameplay authority, server asset/config, or launcher parameter
was changed to manufacture a visual result.

Focused Release checks:34 runtime-control tests /3190 assertions passed;
20 Snark/blood tests /12716 assertions passed,2 optional actual-resource tests
skipped without opt-in. Native functional-log self-test (including numeric
field bounds, malformed/private-marker rejection and retention overflow) and
the exact gentest_mp/reference/peer/180-second `-CheckOnly` passed.

Normal diagnostic Release SHA-256:
`9E85614AB3318400BA43BB9AE07ADD9E05995E3599BC82DA0D1B8D0D41ED8DB6`.
Manual=not_run; Snark pop visual result remains unresolved pending this new
effect-delivery evidence. New agent live sessions/HLDS/managed captures=0;
one elevation attempt canceled. Staging/commit/push=none.

Final verification after the last production edit:

- VS2022/Win32 Release normal application, orchestrator and test targets built.
- Full `scripts/build_source.ps1 -Configuration Release -Parallel 1 -TestOnly`
  completed with exit0:3165 passed,55 skipped,0 failed of3220 (CTest556.37s).
  Subsequent version/help/null-renderer, transport, native/PowerShell summary,
  no-stock launcher matrix and SourceOnly CheckOnly all passed. Log:
  `manual-artifacts/task-records/e27-effect-wire-diagnostic-20261009/release-offline-final.log`.
- Core-only `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` Win32 Release rebuilt and42/42
  tests passed (49.53s), including architecture and alternate-module API guard.
  Logs: `core-build.log`, `core-tests.log` in the same task directory.
- Debug was not rebuilt/rerun in this diagnostic preparation. Optional
  actual-resource/device/focus/private-corpus gates remain skips; no listening
  or new visual confirmation is claimed. ASan runtime was not rerun and
  remains unconfirmed (previous0xC0000135).
- Release hash above is unchanged; historical run36147b9b wrapper still hashes
  to766CF5EC4F8342A911B647A84DF0F74EF290874FAD75609B8668226CF317E852.

Next action from an already elevated user console (no hidden feature flag):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\gentest_mp.bsp" -RemoteAudioPeer -DurationSeconds 180
```

Throw one Snark, wait for its audible natural pop, close primary A, and wait
for managed restoration. Inspect that exact run's numeric effect files and
outcome before changing rendering policy or requesting any further session.

### User diagnostic run a9ccb57e — 2026-10-09

User reported the requested test complete (`attribution=user_report`, message
"Готово"). This is not a new visual-success confirmation. Matched published
run `a9ccb57ef86349fdb708c447db4316f4` by the newly created directory,
transaction ID, gentest_mp map match, reference/two-client/timed180 parameters,
PID/creation times and diagnostic-orchestrator hash. Session started about
12:17:57 Europe/Kiev; final wrapper published12:19:39 (09:19:39UTC).
ServerPID22004, A38656, B6704. No agent-created live session was added.

Evidence, under `manual-artifacts/research-copy-smoke/<run>/`:

- `logs/peer-effects-numeric.log` is complete within all selected row limits:
  visuals19, sounds60, sample mappings7. The upstream peer metadata also has
  no byte/line-count/line-length truncation or capture failure.
- Peer record738, bit2736: `svc_sound`, sound slot54, entity45. Slot54's
  approved virtual-name fingerprint8107056524227144447 matches exactly
  `sound/squeek/sqk_blast1.wav` (FNV-1a64). This is a received Snark pop sound,
  not a guessed event from a disappearance or elapsed15 seconds.
- Peer visual rows comprise9 `TE_BLOODSPRITE` (all color247, scale3) and10
  `TE_WORLDDECAL`. There are zero yellow blood messages and zero `svc_particle`
  rows. Record738 itself contains red blood at bit2832, not yellow blood.
  Both terminal blood pipelines report9 red accepted, zero yellow accepted,
  no invalid/stale/palette/resource/clock failures for those accepted rows.
- Server query reports `violence_ablood=1`. At-rest research `liblist.gam`
  selects normal `dlls\hl.dll`; no health-helper profile is present there.
- A's `client-effects-numeric.log` is EMPTY, NOT evidence of zero events:
  upstream process logging reports line_count_truncated=true (14961 observed
  lines,1731417 bytes). The retaining process-log window evicted early effect
  rows before the new excerpt function ran. Peer capture is intact. The prior
  "verbose summaries cannot evict" guarantee only holds after the upstream
  capture window, so this remains a diagnostic retention limitation for A.

Conclusion: the pop signal reached B, but a yellow visual message did not
reach B's successfully committed decoder stream. The framing-only particle
hypothesis is NOT observed in this run. There is no proof of a failed yellow
GPU upload/draw here, and no justification for another blind scale/lifetime
change. This recipient-side evidence cannot distinguish no server emission
from server-side recipient/PVS suppression or earlier delivery loss. It is
not a server-side packet trace and does not establish stock-binary parity.

Separate termination defect:

- B closed normally: exit0, application completed, close=requested.
- A later exited2: runtime_record_failed / decoder_failed /
  runtime_control_failed / user_info_update_failed, opcode13, record2381,
  source_sequence2393, payload350 bytes, committed publication2380.
- Server diagnostic reports B disconnected. The existing runtime vacancy
  path only accepts zero private ID + empty userinfo + fixed suffix; other
  forms go through the strict sign-on parser. The retained report does not
  expose the precise nested userinfo error or actual offending body. Thus a
  particular rejected ID/string form is a hypothesis, not a proven root
  cause; do not replace exit2 with0 or globally relax the parser on this basis.
- Managed owned-process cleanup=exact, scoped restoration=scoped_exact,
  cleanup_error_count=0. No remaining client/HLDS/orchestrator process was
  observed after publication. Fast mode still does not attest full-tree
  restoration. This run's overall result remains client_blocked, not success.

Integrity (immutable files, not rewritten):

- wrapper JSON:0DF5C450D7054EF08DD6F22EA04DD85B7F6D0F0E17FB047B8F68FB1A9886423A
- peer numeric effects:F2E45AB08FEC1E8996C549988DAD34B06BD3D50169E3E78D17A9BCBC11F380F3
- A redacted diagnostic:4BBC88850986B5D15D2692B6E2F227C7E91CCD36DCFF3C4ACC35D62FDB6A1503
- B redacted diagnostic:C7D225B132B004D9AA9FF45ED380F19F30006D9122CC8C677A769582B5C8C1E4

Current executable hashes remain client9E85614A...ED8DB6 and orchestrator
FC91CB301ED2A6FF015BD6763D457943160A036D49B941A0E904B4A013B8FBDA.
The wrapper records the latter exact hash. It verifies client image identity
but does not persist the client's SHA; the current file hash is not by itself
an independent retrospective SHA attestation for this user run.

No C++/binary changes or rebuild/tests in this inspection; previous offline
verification remains historical. Documentation snapshot is in
`manual-artifacts/task-records/e27-user-run-a9ccb57e-20261009/`.
No staging/commit/push or new live launch. Visual issue remains unresolved.

Proposed next choice requires explicit E27 policy adjustment: allow a bounded
HL-only cosmetic yellow pop from an exact received server sqk_blast sound,
with source-entity/lifetime/generation validation, server event position,
approved blood assets and duplicate-TE suppression. This is a local-compatible
presentation fallback, not a fabricated damage/death event, disappearance
timer, grenade fireball or stock-exact TE reproduction. It has NOT been
implemented or implicitly authorized by the earlier one-session permission.

### Authorized server-sound yellow pop — 2026-10-09

The user now explicitly approved the proposed exact-sound fallback ("зроби").
This changes only E27 cosmetic policy; the preceding historical run/failure
and not-yet-authorized statements remain history, not current authorization.
The implementation does not infer a death from expiry/removal or a15s timer.

Normal production path:

`svc_sound -> successful atomic RuntimeReplaySession commit -> neutral
CommittedSoundObservation -> GameClientHost clock -> HalfLifeClientModule /
TemporaryEffects -> TemporaryEffectPresentation -> approved sprite slots -> GL`.

The existing server audio path is untouched and remains the only pop voice.
There is no extra sound request, audio-device dependency, local damage/AI,
server/cfg/precache edit, local projectile removal, fireball, light or decal.
The app binds exact advertised resource slots once per generation; these
bindings are metadata, not file-open permission. Rendering still uses the
existing approved resource importer/materializer, including graceful missing
asset handling. Normal launcher needs no new flag or copied assets.

HL-only acceptance requires exact `squeek/sqk_blast1.wav` advertised sound slot,
dynamic start on CHAN_ITEM3 (not stop/change/sentence), finite server origin and
receive time, matching generation and a previously observed ordinary non-player
entity with `models/w_squeak.mdl`. Nest/held/player models do not qualify.
NODRAW, zero render amount, frustum culling and focus do not revoke eligibility.
Observed model0/removal is allowed only in the SAME entity publication ordinal
as the sound, matching the pinned SDK's model-clear-before-sound ordering.
Removal alone creates nothing. A later sound after disappearance is rejected.
Observed non-Snark slot reuse invalidates the old receipt; reappearance starts
a new observed life. Exact record/cursor/source identity and one pop per observed
creature suppress replay and a repeated blast in another record.

The existing large-yellow blood profile is reused directly, not as a fabricated
wire TE: MP-sized scale16, two blood sprites, rising spray80units/s with160
gravity, spray cap1s, anchored drops cap1.5s, yellow(1,.85,0), normal depth and
masked alpha. This is explicitly local-compatible presentation, not proof of
stock-binary pop parity. No projectile lifetime timer was added; these are only
lifetimes of an effect already triggered by received sound.

Cross-format duplicate handling: actual large yellow TE and validated sound
match one-to-one within2 world units and250ms, in either delivery order, even
across frame drains. Small/red blood and unrelated positions remain separate.
This tolerance is cosmetic correlation, NOT entity/death authority: TE carries
no entity ID. Two independent colocated Snark sounds still receive two effects.
Ambiguous simultaneous unrelated large yellow blood at precisely the same
location/time cannot be perfectly distinguished; no stock causal ID exists.

Bounds:128 observed creature receipts,64 correlation receipts, existing256
provenance receipts and32 undelivered effects; existing renderer pool unchanged.
Delivery older than250ms is discarded rather than replayed after readiness.
Session/map reset clears all new receipts/bindings; local viewmodel/focus changes
do not clear committed world cosmetics. Unobserved source entities fail closed.
Wire lacks a private entity allocation generation: undetectable same-model slot
reuse without any observed boundary is not claimed to be distinguishable.

Opt-in terminal `sound-visual-summary` separates received, accepted, source
rejected, duplicate, TE-correlated, missing-resource, invalid and capacity.
Accepted is not a claim of pixels or user confirmation; existing large-yellow
materialized/draw/stale/absent counters remain separate.

Pre-edit hash-checked file copies are under
`manual-artifacts/task-records/e27-server-sound-pop-20261009/source/`.
Required source/tests remain ordinary project files, not artifacts.
New build manual=not_run; no new live session, staging, commit or push.
The separate prior userinfo/disconnect failure is not changed by this cosmetic
fix and remains at its previously documented unproven nested-error boundary.

Verification after the final production edit:

- VS2022 / Win32 Release normal application + tests + orchestrator built.
  First test compilation found a test-only AssetVector3 equality mistake;
  component assertions fixed it without changing production or weakening tests.
- New `[sound-pop]`:8/8,332 assertions. Covers exact mapping/flags/channel,
  unknown/player/nest/reused/hidden sources, model0/removal-same-record,
  repeated sound, generation/reset, no audio queue, malformed-transaction
  rollback, clock conversion, bounded capacity, missing binding, stale delivery,
  TE/sound both orders with/without intermediate drain, independent colocated
  creatures and production OpenGL pixel/depth/expiry/state restoration.
- Debug normal application + tests + orchestrator built; focused sound-pop,
  Snark-world, runtime-replay and game-API:173 passed,1 skipped,29554 assertions.
  Skip is the separate opt-in installed player-model test without its root.
- Full final Release `scripts/build_source.ps1 -Configuration Release -Parallel
  1 -TestOnly`: exit0. CTest3228 registered,3173 passed,55 skipped,0failed;
  521.36s. Existing opt-in assets/device/WFP/corpus/symlink/SDL capability skips
  remain skips, not successful live coverage. Wrapper version/help/Null,
  fake transport, native/PowerShell summaries, no-stock launcher fixtures and
  SourceOnly CheckOnly also finished with exit0. No submodule update.
- Core-only `HLCLIENT_BUILD_GAME_HALFLIFE=OFF` Release rebuilt:42/42 CTest,
  20.34s; alternate core API276 cases/141382 assertions, including the new
  neutral sound seam; architecture guard passed. No HL1 fallback/linkage.
- Additional read-only exact approved research-root GL test:1/1,376 assertions,
  both received-TE and sound-triggered paths. At distance128/eye64/groundZ0,
  actual blood sprites produce11265 yellow pixels at0.4s,7648 at0.7s,28 at1.2s;
  old-profile controls2518/0/0. Buried effects are occluded, expiry returns to
  baseline and render state is restored. This is offline pixel evidence, not
  a fresh live/subjective user confirmation. Owned fixtures remain mandatory.
- Exact gentest_mp/reference/two-client/180s launcher CheckOnly passed; reports
  the Release SHA below and process-launches=0. Launcher source unchanged.
- `git diff --check` passed; index remains empty; branch/HEAD unchanged.
  ASan runtime not rerun, historical unconfirmed status unchanged.

Normal Release `build/bin/Release/hlclient.exe` SHA-256:
`397BB8EB4F0A941A6E7FC7F6E2BA0310B90568C133ABEEF66D6753D12D32E9F7`.
Debug SHA-256:
`86DB78DE6175B2FC1FC343BE87F02C045434F225AD9578ED736E7883EEE657C0`.
Orchestrator remains
`FC91CB301ED2A6FF015BD6763D457943160A036D49B941A0E904B4A013B8FBDA`.
Build/test/CheckOnly logs reside beside the pre-edit snapshots in
`manual-artifacts/task-records/e27-server-sound-pop-20261009/`.

For the next user-controlled test, same command (not run by this task):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\gentest_mp.bsp" -RemoteAudioPeer -DurationSeconds 180
```

Throw one Snark and wait for its natural audible pop. Verify the yellow sprite
burst on both clients. Close primary A and wait for managed cleanup; this does
not claim the separate B-disconnect parser issue fixed. New build manual=not_run,
new live/HLDS/stock/managed captures=0, staging/commit/push=none.

### Snark original blood-pop correction (E27.1) — 2026-10-09

Current yellow pop rejected by the user:
`snark_pop_visual_manual=failed`, `attribution=user_report`,
`reason=does_not_resemble_original_half_life`.
This is rejection of the visual profile only. E26 user acceptance and existing
throw/inventory/server-creature/audio/gameplay evidence remain intact.
New corrected build: `snark_pop_visual_manual=not_run`.

Result: `snark_pop_original_assets_integrated`, offline verified.
Stock visual comparison is separately `not_run_no_stock_reference`; this is
not a stock-exact renderer claim. Separate B-disconnect parsing defect unchanged.

#### Established cause and original reference

Both existing paths already used the original `bloodspray.spr` and `blood.spr`.
No procedural explosion replaced them. Source/render inspection established
the incorrect **profile/composition** in `TemporaryEffects::large_yellow_pop`:
manually chosen tint(1,.85,0), MP spray scale2.4/153.6-unit quad, rising80 with
gravity160, and ONE anchored25.6-unit drop quad animated at6FPS up to1.5s.
The spray already had a presentation clock; snapshot-stalled animation was not
the cause. Actual production GL A/B reproduces that old profile with the same
approved assets, camera, floor, FOV, light and timestamps.

Pinned SDK remains `b1b5cf5892918535619b2937bb927e46cb097ba1`:
`squeakgrenade.cpp:175–179` emits the blast then BloodDrips(yellow,80);
`util.h:168` supplies195; `util.cpp:1166–1195` gates blood settings/PVS,
duplicates amount in multiplayer, writes spray/drop indices, host_basepal
color index and clamp(amount/10,3,16). MP80 becomes source amount160 and wire
scale16. These are NOT a world radius or a count of drops.
`weapons.cpp:401–402` binds first bloodspray/second blood; `r_efx.h:122`
declares R_BloodSprite with float size but does not implement it. The SDK
temporary sprite update uses elapsed time*framerate. Blood-specific size
conversion, FPS, RNG, drop count, velocities/contact and lifetime remain
internal renderer details; no implementation bodies or Xash code were copied.

Read-only verified original assets:

| Resource | Source facts |
| --- | --- |
| gfx/palette.lmp |768 RGB bytes; index195=(203,183,15), index247=(103,0,0) |
| sprites/bloodspray.spr |10 single64x64 frames, origin(-32,+32), VP_PARALLEL, ALPHA_TEST, sync=random |
| sprites/blood.spr |9 single16x16 frames, origin(-8,+8), same orientation/format/sync; varied droplet silhouettes |

The embedded sprite palette describes grayscale covered texels and blue index255
padding. It is NOT host_basepal. Index255 padding has alpha0; covered texels
have alpha255. New large yellow pop uses verified host palette195 multiplied
by original sprite texel intensity and coverage. Existing straight-alpha shader,
SRC_ALPHA/ONE_MINUS_SRC_ALPHA, alpha-test coverage/filtering and world depth are
retained. No additive/glow material or light illuminates the background. Exact
stock gamma/brightness mapping is not established by this source audit.
Legacy small yellow/red/player-blood profiles remain unchanged; the palette-
verified RGB claim applies specifically to corrected large-yellow pop.

Original-file hashes before/after audit agree:
palette036B7EA3C8C03BE214BB7D22B8D63AC27F63D547E42CA677FEF344DDC780961E;
spray86769469CF458DF8114A7239A9C1B5018ECC8CC2E443D679EB43BAD5B70EFB89;
drops42CABFBAC848496FDE138163BC36DE3E71B7E7B4E51926F6F4F9A2FF685F19C8.

#### Current production composition and boundaries

Real TE preserves both received indices/color/scale. Validated sound keeps
distinct `Source::server_sound` provenance, reported as
`source=inferred_from_validated_snark_sound`; MP palette195/scale16/roles are
inferred from the pinned profile, not misreported as received TE fields.
Both call ONE shared blood profile and the same approved materializer/renderer.

- Spray is anchored at the server point, original frame offsets/shape retained,
  scale=wireSize*.1 (MP16=>1.6, full64px quad102.4units),20FPS, nonlooping.5s,
  alpha1, no smoke fade, no artificial rising motion.
- 16 separate imported blood.spr drops, fixed randomly selected source silhouette,
  scale.5/full quad8units, independent seeded XYZ velocities in
  [-64,-64,96]..[64,64,192], gravity800, lifetimes0.6–0.9s. Density16 is a separate
  profile constant, NOT derived from amount80 or wire size; scale12 tests prove
  this distinction. Seeded expansion occurs once, not once per frame.

Those numeric size conversion/FPS/density/kinematics/lifetimes and fixed-frame
drop policy are **local-compatible approximations**, not measured stock
constants. The source geometry makes their world sizes explicit and the
temporal comparison is reviewable; public SDK alone cannot certify the stock
mapping. There is no reference-backed contact response, so none was invented.
Drops can pass below the floor and are naturally depth-occluded; their bounded
visual lifetime then expires. At a literal ground origin the sprite plane can
intersect the floor; source offsets/depth are correct, and no speculative
render offset or disabled depth test hides that reference uncertainty.

Existing owner expanded with a neutral compact sprite burst:16 per event,
128 drop slots alongside32 ordinary sprites,160 output frames. Source origin,
velocity, fixed-frame choice and elapsed age reach the existing sprite
materializer. No new particle framework/physics engine/Snark renderer exists.
Main heap-owns the enlarged owner for Win32 stack headroom. GameClientAPI owns
neutral palette metadata/requests and presentation policy; HAL owns names,
index195 interpretation, eligibility and the profile. Renderer has no Snark name.

The palette dependency is prepared once in the startup worker: a compiled module
gfx leaf token, exact selected-world root, stable locator identity/size,
existing LocalAssetSourceOpener and exact768-byte validation. No arbitrary
native open, alternate installation, per-contact IO or copied game asset.
Typed status ready/absent/not_authorized/open_failed/decode_failed remains
distinct. Missing palette skips large pop without a bright guessed fallback
or later replay. Generation reset clears new state; main rebinds owning approved
metadata/policy with the generation's resource slots. Normal player/viewmodel
life changes do not retire already committed world effects. Existing one-map
application asset preparation is not claimed to implement general map reload.

The existing value-less server violence_ablood query is now consumed before
launching clients. A fresh complete owned-server console snapshot supplies
optional0/1; strictly validated enabled/disabled evidence reaches both clients
through the ordinary argument plan and GameClientAPI. Explicit false suppresses
the optional sound fallback. Unknown remains unknown; config.cfg at rest is
not runtime evidence. Real TE retains its current server authority. No setting,
server/cfg/DLL/precache file or launcher default was changed.

Existing entity/generation/source dedup retained. Cross-source TE correlation
additionally requires the advertised blood-sprite pair and scale8/16; wrong
pair, scale12, red/small blood, other position/time remain independent. Tightly
bounded one-to-one2units/250ms correlation does not restart the existing effect.
Two distinct colocated Snarks still produce two effects. A completely identical
nearby large-yellow TE remains ambiguous because no TE emitter ID is available.

#### Evidence, verification and delivery

Generated visual artifacts (not installed assets or stock screenshots):

- [Original frames/intensity/coverage/palette contact sheet](../manual-artifacts/task-records/e271-original-blood-pop-20261009/visuals/blood-original-contact-sheet.png).
- [Production GL before/after time series](../manual-artifacts/task-records/e271-original-blood-pop-20261009/visuals/blood-pop-before-after.png).
- [Isolated spray/drops/composition](../manual-artifacts/task-records/e271-original-blood-pop-20261009/visuals/blood-pop-components.png).

No approved stock Snark screenshots/demo/capture were found. These images compare
our previous/corrected profiles, not GoldSrc stock renderer output. Repeatable
artifacts use the ordinary tests and `scripts/render_snark_blood_comparison.py`.
Source-manifest snapshot1278 ordinary files was hash-verified before edits
under `manual-artifacts/task-records/e271-original-blood-pop-20261009/source/`.

After the final production edit, VS2022/Win32:

- Release focused E27/E27.1/sprite-burst:69 passed,4 optional asset skips,
  10906 assertions. New mandatory fixtures use a deliberately different palette
  (160,130,20) to catch hardcoded RGB. They exercise scale-vs-density, CPU/GL
  source roles/frames, varied independent drops,30/60/144/240FPS, bounds, expiry,
  reset, missing components/palette, source-labelled TE/fallback equivalence,
  all dedup orders/repeated updates and known disabled-blood policy.
- Actual approved palette/sprite/importer/GL opt-in:3/3,110603 assertions.
  Spray and drops separately produce pixels; composition differs from each
  isolated pass. Mask holes remain background, near/oblique ground cameras at
 32/64/128units stay bounded, buried effects are occluded, expiry returns to
  baseline and GL blend/depth state is restored. No subjective/stock acceptance
  is inferred from these checks. Original files' hashes are unchanged.
- Debug normal app/tests/runner built:218 passed,6 optional installed-resource
  skips of224 cases,28705 assertions, including replay/GameAPI/audio/dedup.
  Native diagnostic-policy/peer self-test exit0, stock-processes-started=0.
- Full final Release offline wrapper exit0:3239 CTest registered,3183 passed,
 56 capability/opt-in/corpus/symlink/SDL/device/WFP skips,0 failures,471.50s.
  Version/help/Null, transport/summary/strict runner/no-stock launcher fixtures
  and SourceOnly CheckOnly also passed. No submodule update.
- Final core-only Release rebuilt with HLCLIENT_BUILD_GAME_HALFLIFE=OFF:
 42/42 CTest,21.28s; core API277 cases/141395 assertions. Alternate module uses
  palette/policy/source API without an HL fallback; architecture guard passes.
- ASan Debug normal app build passed after final edit; `--help` exited
  -1073741515 /0xC0000135 without output. ASan runtime remains failed/unconfirmed;
  it was not repaired or counted as a successful runtime test.
- gentest_mp/reference/two-client/180s CheckOnly passed, process-launches=0,
  exact Release SHA below. Default map remains crossfire. No map edits.
- git diff --check passed. Empty staging, unchanged branch/HEAD, no missing
  source files or unrelated changes removed. Build outputs were not cleaned.

One test-only wchar-to-char narrowing warning was fixed with explicit checked
ASCII conversion; warnings-as-errors remain enabled. All final checks above
use the rebuilt source, not a pre-fix run. Detailed logs accompany the snapshot.

Release: `D:/DEV/CPP/HLC-steamcfg-5e48b7c1/build/bin/Release/hlclient.exe`
SHA-256 `07BCCCB4A3E55607C3F0E5AD335E2FD461795EA6951954EA922CD22451E8B4C3`.
Debug SHA-256 `23FB21F2A1C834CD6128E7667516BE05F79B0141D22D44334FB58F88D38D3497`.
Runner SHA-256 `9342B1A481F0CFFED036E8F76D5FE1590139BCFC2D313D1375E6B77794BADE28`.

Next user-controlled manual test (checked, not launched here):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -ExternalMapBsp "D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\gentest_mp.bsp" -RemoteAudioPeer -DurationSeconds 180
```

Throw one Snark, observe its natural pop and compare spray/drop shape, palette,
size and timing with original HL. Close primary A and await cleanup.
`snark_pop_visual_manual=not_run`; `stock_visual_comparison=not_run_no_reference`;
new live/Steam/HLDS/stock/managed captures=0; staging/commit/push=none.

### E27.1 user acceptance carried into E28 — 2026-10-09

User reported "Протестував все ок" for the accepted E27.1 Release
`07BCCCB4A3E55607C3F0E5AD335E2FD461795EA6951954EA922CD22451E8B4C3`.
`manual=passed`, `attribution=user_report`,
`scope=Snark blood-pop correction manual scenario`.
The file's actual SHA matched this at E28 preflight. Historical rejection of
the preceding visual profile remains above. This acceptance does not establish
stock-exact size/FPS/drop counts/dynamics/sockets, E18/water/platform/reconnect
coverage or peer-disconnect correctness. E28's subsequent build is manual=not_run.
