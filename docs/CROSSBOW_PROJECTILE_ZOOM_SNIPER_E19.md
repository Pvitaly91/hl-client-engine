# E19 — Crossbow projectile, zoom and sniper presentation

Task `HLC-E19-CROSSBOW-PROJECTILE-ZOOM-SNIPER`, checkout
`D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, starting HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. Existing uncommitted E11–E18
and unrelated files are preserved. The hash-verified pre-edit snapshot holds
1,225 ordinary source files in `manual-artifacts/task-records/e19-crossbow-20261004/source`;
its manifest and execution logs are evidence, not production dependencies.

E19 manual remains `not_run`. The request adds `E17 manual=passed;
attribution=user_report`, scoped to the user's Shotgun checks. It does not
rewrite the historical E17 `manual=not_run` report, erase its later viewmodel
clipping defect/fix, or certify stress, water or stock-binary parity. E18
manual remains `not_run`; no E19 executable hash is attributed to older runs.

## Boundary and executable path

| Ownership | Actual path and work |
| --- | --- |
| Half-Life roles/policy | `firearm_profile.hpp`, `crossbow_profile.hpp`, `local_weapon_presentation.cpp`, `presentation.cpp`: ID 6 plus exact WeaponList `weapon_crossbow`, validated `v_crossbow` roles, fire/reload eligibility, clip/reserve semantics, FOV20, input edge and recoil rules |
| Half-Life events/effects | `client_module.cpp`, `weapon_audio.cpp`, `weapon_visuals.cpp`, `remote_effects.cpp`, `temporary_effects.cpp`: assigned `events/crossbow1.sc`/`crossbow2.sc`, two launch cues, static sniper feedback and bounded neutral intents |
| Generic committed data | `runtime_replay_session.cpp`: owning optional signed integer event parameters, absent distinct from zero; no new opcode scan, retained RX pointer or canonical hash change |
| Generic wire | `runtime_control_decoder.cpp`: exact TE_BUBBLETRAIL 114 grammar, staged with the existing record transaction; TE_SPARKS 9 and TE_EXPLOSION 3 reuse strict framing |
| Generic presentation | `TemporaryEffectPresentation`, `RuntimeReplayLocalAssets::materialize_shells`: fixed transient Studio intents reuse approved named model bindings, pose/frame construction and existing renderer pass |
| Composition | normal `apps/hlclient/main.cpp` drains through GameClientHost, resolves static presentation hit, submits audio and materializes transient Studio models; no demo path or feature flag |

Dependency direction remains `hlclient -> host/GameClientAPI + HL module`,
`HL module -> API/shared mechanisms`, never `lower core -> concrete HL`.
Renderer/audio/collision do not recognize Crossbow names, IDs or gameplay rules.
Neutral model tokens are approved virtual names, not native locators/handles.

## Pinned reference and inspected models

Official local SDK revision `b1b5cf5892918535619b2937bb927e46cb097ba1`
was read without updating or copying implementation: `dlls/crossbow.cpp`,
`cl_dll/ev_hldm.cpp`, `cl_dll/hl/hl_events.cpp`, public constants/weapon
structures and `network/delta.lst`. The latter declares event iparam1/2 as
signed 16-bit integer fields; generic projection retains owning int32 values
only for exact schema-compatible decoded fields. Pinned playback sends
post-shot clip in iparam1 and remaining reserve in iparam2.

Read-only actual approved resource controls inspected all five models:

| Model | Metadata / SHA-256 |
| --- | --- |
| v_crossbow | 56 bones, 12 sequences, 2 bodyparts, 1 skin, 0 attachments, 0 events; `5A4E0AC15DE14446C132F521154CE6C8428EAA9563C2D5396915EA59E765CD51` |
| p_crossbow | 12 bones, 1 idle sequence, Line01 after Bip01 R Hand, 0 attachments; `F8CAA2844E03F0935512CCC793E48717FA1FB41B7A702F97387C0195ED94E94C` |
| crossbow_bolt | 1 bone, 1 frame/sequence, 0 attachments; `3FE32D119ADB328114287BD43149429B4409CA9140E8EAD6ADEC1479EF05DB30` |
| w_crossbow | 2 sequences; `B511C143948BA9923AF9C1529868AB32C69FBF471EE67E30A4C3DAE8BF26C898` |
| w_crossbow_clip | 1 sequence; `FC4512C1465576DF6C3AA9C9DAFAD0B0D1344222F86568615D0965A5121471A5` |

Actual v-model order: idle1/idle2/fidget1/fidget2/fire1/fire2/fire3/reload/
draw1/draw2/holster1/holster2. fire1 has 111 frames at 60 fps;
fire3 16 at 30; reload 136 at 30. Validated labels, timing and supported
bodies gate this profile; arbitrary models are not assigned numeric roles.
Post-shot clip >0 selects fire1 (4); clip=0 with reserve >0 selects fire3 (6);
clip=reserve=0 emits no new animation/restart, matching the inspected event.
Exact server deploy/holster roles retain loaded 8/10 versus empty 9/11;
they are not invented attack actions. Reload uses role 7/4.5 s with server
ammo. Existing state/service reconciliation and A1 camera-local rendering remain.

## Ordinary versus sniper authority

Unzoomed crossbow1 emits `xbow_fire1.wav` and event-mechanical
`xbow_reload1.wav` as independent automatic voices, normal -2 degree visual
pitch punch, no muzzle flash/light/brass/tracer and **no local flying bolt,
hitscan, stuck bolt, explosion or damage**. Underwater ordinary firing remains
eligible. The server owns its normal packet Studio bolt, velocity, contact,
damage and removal. Existing generic kinetic MOVETYPE_FLY presentation interpolates
committed origins only; disappearance, slot reuse and discontinuities break
continuity. No local projectile simulator or Crossbow renderer was added.
Final audit exposed a player-threshold leak: a valid 200-unit bolt interval
over 100 ms exceeded the player's 128-unit teleport guard. The existing
ordinary history now takes an explicit independent 512-unit displacement
guard (local-compatible recovery, not stock physics), under the unchanged
250 ms observation gap and at most 100 ms delay. Player policy stays 128.
It only brackets committed poses; one observation remains authoritative,
no extrapolation is added, and NOINTERP/model/sequence/movetype/absence/reuse
and larger jumps still break continuity. Owned production fixtures now
exercise 2000 units/s rather than the initial slow 80 units/s control.

Server `svc_sound` supplies ordinary body/world contact audio. Server TE_SPARKS
creates a neutral particle burst; multiplayer TE_EXPLOSION uses the existing
sprite/light/particle/audio path. The SDK's delayed MP explosion (.1 s,
damage 40/radius 128) is reference information, **not a client timer/rule**.
Server explosion NO_SOUND is respected; no double local explosion voice.

MP zoom is latest committed FOV20, not local input authority. It reuses E18
horizontal FOV/aspect, SetFOV/clientdata provenance, reload/holster/death/life/map
reset floors and pre-history RMB edge latch. Ordinary server health/ammo,
wire commands, history/replay, translation and physics are unchanged.
crossbow2 retains both launch cues, **no visual recoil or flying bolt**.
A bounded static-world query (8192 units, 250 ms freshness) suppresses decals,
material/ricochet/tracer effects. A supported opaque hit produces
`xbow_hit1.wav` at contact, sparks and a fixed presentation-only
`models/crossbow_bolt.mdl` at hit minus forward*10, fixed VectorAngles-derived
orientation and 5 s lifetime. Renderer roll/X, -pitch/Y, yaw/Z conversion is
explicit. Missing decal data cannot block this path.

The remote path uses assigned event names/owning parameters and emitter origin/
angles. `p_crossbow` reuses generic named-bone merge, not a made-up muzzle
attachment or forced player fire sequence. Remote events never recoil the
observer or mutate ammo/death/score. Duplicate/replayed records and late echoes
cannot repeat launch sounds or materialize a second stuck bolt. A normal
provisional shot may be refined by one confirmed crossbow2 echo without a
second launch sound or animation restart.

## Bounds, lifecycle and explicit limits

- Dynamic sniper body-hit presentation is `evidence_limited`: current static
  presentation trace cannot reliably classify player hitboxes. No proximity
  result, body sound, blood or damage is invented; server damage is independent.
  Static cosmetics do not certify a server hit and can disagree with dynamic occlusion.
- Static policy rejects liquid/sky/masked/moving-brush blockers. Dry opaque
  Crossfire is covered; underwater sniper stuck/sparks and water visuals are not
  upgraded to passed. TE_BUBBLES 113 and TE_BUBBLETRAIL 114 have exact consumption,
  transactional rejection and `visual_deferred`, never an arbitrary skip/resync.
- `xbow_fly1.wav`, `streak.spr`, `fvox/beep.wav` are precached but unused in the
  inspected firing/contact paths. No invented flight loop/trail/beep.
- Five bounded local effect WAV dependencies use the existing pinned HL
  namespace, verified approved open and asynchronous decode/preparation. No raw
  file I/O at contact/callback, server precache mutation or installed asset changes.
  Real WAV decode/nonzero PCM is not a human listening confirmation.
- Temporary models have 32 map-scoped slots; pending effects 32, hit identities
  64, remote actions 64, local seen-action lanes 32. The existing bounded particle,
  Studio and voice owners are reused. World effects survive viewmodel/weapon/local
  life changes; map/session teardown clears generations. No retained RX pointers.
- Sparks (8, .45 s, speeds 60–140, gravity 400, radius .65, warm emissive) and
  deterministic sample/pitch variation are local-compatible, not opaque engine
  RNG/temporary-entity parity. The sampled burst is not a server damage claim.
- Full app ASan runtime was historically unverified (exit 1 without output).
  Scoped core/API verification is reported separately, not used to claim app coverage.

## Verification and handoff

The saved pre-edit Release focused baseline passed 406 cases, skipped 11
explicit installed-resource controls, 271,904 assertions. New tests are ordinary
project-owned fixtures: controller/event parameters, loaded/empty/service roles,
reload/FOV/edge history, no false effects, production static trace, two-voice
and isolated hit PCM, server bolt interpolation/removal/reuse, generic p-model
merge, actual GL viewmodel/fixed-bolt expiry and immutable record transaction.
Independent read-only actual resources are optional, never a build dependency.
Initial fixture metadata, uint8 optional narrowing, clock anchoring, renderer
coordinate expectations and async delivery were corrected, not production
validation or historical assertions weakened.
An intermediate final run exposed the existing E9 process-test race: process
handles were signalled before asynchronous Windows Job accounting reached
zero. That test now awaits accounting separately for at most 5 s and retains
the exact-zero assertion. Production process/cleanup behavior was not changed.

Final acceptance (after the fast-kinetic production correction):

| Check | Result / evidence log under the E19 task-record directory |
| --- | --- |
| VS2022/Win32 Debug normal client + affected tests | build passed; focused 420 cases: 411 passed, 9 explicit opt-in skips, 281,995 assertions (`regression-debug-handoff.txt`) |
| VS2022/Win32 Release all targets | passed (`build-release-handoff.txt`), normal production executable, not a demo |
| Release E19, including actual approved read-only controls | all 25 cases passed, 639 assertions (`e19-release-handoff-assets.txt`); owned/actual GPU fixed-model orientation, camera-local viewmodel, expiry, p-model and isolated audio PCM |
| Complete audited Release offline CTest | 2,919 registered: 2,874 passed, 45 explicit skips, zero unexpected failures, 160.76 s (`release-offline-accepted-final.txt`) |
| Core-only HL OFF all targets / CTest | build passed; 41/41 passed, 23.49 s (`build-core-handoff.txt`, `core-only-handoff-ctest.txt`); core/API executable inside that run: 226 cases / 88,162 assertions |
| Actual no-HL graph / architecture / alternate module | 158 active solution projects, zero concrete HL source/header/link references; graph guard and its seven negative fixtures passed; project-owned TestGameClientModule uses the production host without fallback |
| Scoped ASan | `asan_build=passed` (core/API target); `asan_core_api=passed`: 56 cases / 3,748 assertions, including interpolation (`asan-core-api-handoff.txt`); `asan_application_runtime=not_verified`, historically exit 1/no output, not rerun |
| SDL real device capability | passed existing short 250 ms moderate output test: 1 case / 5 assertions (`audio-device-handoff.txt`); not human listening or gameplay acceptance |
| Unchanged launcher CheckOnly | reference + RemoteAudioPeer + NoTimeLimit, off, and damage-respawn-check passed; no runner/Steam/WFP/socket/game launches (`launcher-*-handoff.txt`) |

The full suite's skips are explicit unavailable symlink/native input capabilities,
installed/capture opt-ins and intentionally disabled WFP/live capabilities, not
failed E19 positive-path tests. Mandatory E19 fixtures need neither an installed
game nor an audio device. The optional actual control exercised five models and
five nonzero decoded WAVs through the approved owner. The complete audited
script also checks normal Null bootstrap, transport receipts, native/PowerShell
summary roundtrips and no-stock/fake-runner/manual-handoff contracts.

Ready normal Release executable: `build/bin/Release/hlclient.exe`, 3,703,296 bytes.
SHA-256: `612D66B6701B71D0B48345361483515D8A290339942A911BC02F2F5FD722E21D`.
The unchanged launcher selects that exact file; launcher SHA-256:
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF`.
No digest is retroactively assigned to an earlier user run. Starting HEAD and
branch are unchanged, the index is empty, the SDK checkout is clean, and the
pre-edit snapshot still has 1,225 verified files with no missing source or
snapshot hash mismatch. Six new ordinary project files hold E19 code/tests/doc;
the source has no dependency on manual-artifacts.

Primary result: `crossbow_projectile_zoom_sniper_integrated`, offline verified
with the explicit dynamic-hit/water/local-compatible limits above.

Reuse existing directories; no clean/source restore or live launch:

```powershell
cmake --build build --config Debug --target hlclient_tests --parallel 2
cmake --build build --config Release --parallel 2
pwsh -NoProfile -File scripts/build_source.ps1 -TestOnly -Configuration Release -Parallel 4
cmake -S . -B build-core-g1 -G "Visual Studio 17 2022" -A Win32 -DHLCLIENT_BUILD_GAME_HALFLIFE=OFF
cmake --build build-core-g1 --config Release --parallel 2
ctest --test-dir build-core-g1 -C Release --output-on-failure --parallel 2
```

Normal unchanged launcher, default crossfire; user runs:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Check loaded/last/empty fire, both launch sounds, server moving bolt and contact/
MP explosion, remote p-model, RMB tap/hold/tap FOV20, zoomed static wall hit with
fixed bolt/sparks/no flash/no brass/no flying bolt, 5 s expiry, reload/holster/
death/respawn FOV reset, camera-local viewmodel and E13–E18 regressions.
New live/stock client/HLDS/Steam/WFP/capture sessions=0;
staging/commit/push=none; E19 manual=not_run until that test.

Manual evidence starts explicitly unset and only a new user report promotes it:

```text
crossbow_normal_fire_manual=not_run
crossbow_projectile_manual=not_run
crossbow_projectile_explosion_manual=not_run
crossbow_zoom20_manual=not_run
crossbow_sniper_fire_manual=not_run
crossbow_stuck_bolt_manual=not_run
crossbow_remote_manual=not_run
crossbow_reload_manual=not_run
crossbow_kill_respawn_manual=not_run
```

## Subsequent user confirmation (E20 request, 2026-10-05)

`manual=passed; attribution=user_report`: user reported “перевірив все працює”.
Scope: ordinary Crossbow/server bolt, FOV20/sniper, fixed bolt/expiry,
reload, remote presentation and client operation. The historical offline
handoff above remains unchanged. No new hash is assigned to that manual run;
water/bubble and dynamic-body-hit limitations are not promoted to passed.
