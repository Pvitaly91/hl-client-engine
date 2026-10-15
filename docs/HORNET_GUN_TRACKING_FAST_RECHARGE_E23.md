# E23 — Hornet Gun tracking, fast fire and recharge

## Subsequent manual acceptance (2026-10-07)

`E23 manual=passed; attribution=user_report`. The E24 request supplies the new
user report **«Протестив все ок»**, accepting E23's Hornet Gun behavior including
TRACK/FAST, ammunition/recharge, projectile/trail and remote/lifecycle stability
at user-report scope. This is not a new automated measurement, isolated listening
test or hash-attested run. No executable SHA is attributed to that report.
The original preparation record below remains historical and unchanged.

## Original offline preparation record

Status: `hornetgun_tracking_fast_recharge_integrated_offline_verified`;
`manual=not_run`. Normal application Release prepared; no live proof claimed.
Authoritative worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, HEAD
`00505f71ff2547a9a6b083e53edf69ec45d48213`. The old remote checkpoint is not a
checkout/reset instruction. Existing E11–E22 and BSP-seam local changes remain.
Source snapshot: `manual-artifacts/task-records/e23-hornet-20261006/`, 1249
ordinary tracked/untracked files copied and SHA-256 verified before edits.
Pre-edit Release baseline: 75 cases, 72 passed, 3 optional-asset skips;
8320 assertions passed. Index empty; no other worktree modified.

## User evidence

New E22 `manual=passed; attribution=user_report`, from the explicit
“Протестовано” in this E23 request, covers the multiplayer manual scenario's
audio, local/remote beam, attachment, shared ammo, cleanup and stability.
It does not erase earlier E22 failures or `manual=not_run` reports and does not
assign a build hash to an unidentified manual run. ScreenShake visual output
remains deferred. The accepted Egon beam shape and recent BSP seam fix are kept.
E23 fire/track/fast/recharge/remote/trail/audio/hit-lifecycle/kill-respawn manual
fields are all `not_run`; only a later user report can promote them.

## Before E23

| Audit field | Initial support |
| --- | --- |
| hornetgun_catalogue_current | Generic bounded WeaponList/owned inventory selection worked; no named Hornet action profile. |
| hornets_ammo_current | Assigned primary ammo index and AmmoX counts already represented; no Hornet-specific no-clip HUD. |
| hornetgun_viewmodel_current | Generic import/server animation available; no validated local fire roles/cadence. |
| hornetgun_pmodel_current | E11 already merged the server-selected numeric weapon model slot, without a new renderer. |
| firehornet_event_current | No exact firehornet binding or launch sound presentation. |
| hornet_entity_current | Ordinary Studio server entities already supported; no local spawn needed. |
| hornet_interpolation_current | Existing generic FLY interpolation/angles and modelindex=0 visibility available. |
| hornet_trail_current | E20 generic TE_BEAMFOLLOW already present; between history samples its tip could lag a fast entity. |
| hornet_recharge_current | Server AmmoX updates already accepted, including increases; no client recharge timer. |

## Pinned reference, not copied engine code

Official Valve SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`:
`dlls/hornetgun.cpp`, `dlls/hornet.cpp`, `dlls/hornet.h`, `dlls/weapons.h`,
`cl_dll/ev_hldm.cpp`, `cl_dll/hl/hl_events.cpp`, `common/const.h`.
No implementation bodies copied, submodule updated, assets downloaded or
installed files changed. The following server values are reference facts,
not client physics/AI constants:

- Weapon ID 11 / `weapon_hornetgun`, ammo label `Hornets`, max 8, no clip;
  multiplayer pickup fills ammo on the server. Slot 3/position 3 is group 4.
- `IsUseable()` stays true; NOAUTOSWITCHEMPTY/NOAUTORELOAD are intentional.
  The SDK method named `Reload()` is automatic biological regeneration, not R.
- Recharge catches up while its private clock is behind server time: 0.3 s
  per Hornet for maxClients>1, otherwise 0.5 s. Empty holster's legacy server
  grant of one is not duplicated by the client. Switching away/returning simply
  adopts current server ammo; no fake local fill or permanently predicted reserve.
- Primary TRACK=0 has a 0.25 s next-primary delay; the last round adds one
  recharge interval (0.55 s MP / 0.75 s SP total). Secondary FAST=1 sets both
  attack delays to 0.1 s, plus 0.5 s on the last round (0.6 s total).
- Both event modes use `HGUN_SHOOT` sequence 5/body 1, the same three
  `agrunt/ag_fire1.wav`–`ag_fire3.wav` samples, volume 1, pitch 100 and normal
  spatial attenuation for remote emitters. Local punch is positive pitch 0–2
  degrees, chosen deterministically per action here, not stock mutable RNG parity.
- Server TRACK starts near the gun at 300 units/s, starts tracking after
  about 0.1 s, owns the search/visibility/512-unit targeting and heading. Server
  red/orange selection is 2/5 versus 3/5, nominal speeds 600/800, MP attack
  lifetime 3.5 s. FAST starts at 1200 units/s, uses server eight-offset phases
  and StartDart retirement after 4 s. None of those trajectories/timers is
  simulated or chosen in this client.
- Server TE_BEAMFOLLOW supplies entity and sprite slots, life 10 tenths,
  width 2 tenths, brightness 128 and red 179/39/14 or orange 255/128/0.
  Expected sprite is `sprites/laserbeam.spr`; actual message values win.
- Server tracking may emit `hornet/ag_buzz1.wav`–`ag_buzz3.wav` on strong
  turns; target hits may emit `hornet/ag_hornethit1.wav`–`ag_hornethit3.wav`.
  Disappearance alone creates no sound. On hit, server modelindex becomes 0
  while the entity remains for one second in the pinned code so audio can finish.
  This differs from the old source comment saying 0.1 s; no client removal timer
  is added. The SP-only puff is not added as a multiplayer feature.

## Production ownership

`hornet_profile.hpp` validates exact original v_hgun roles before applying
sequence numbers and owns the launch allowlist/mode/recoil policy.
`LocalWeaponPresentationController` uses existing submitted-command identities,
server eligibility and model/life reset. `HalfLifePresentation` publishes the
ordinary viewmodel intent and `Hornet Gun / HORNETS n` HUD, never `clip=-1`.
The numeric ammo slot is assigned by WeaponList, not hardcoded; the label is
the HL profile's semantic label, not a claimed network ammo-name field.
R has no reload presentation, zero ammo remains selectable, and only server
observations raise the displayed reserve. Invalid >8 input is unavailable for
presentation/eligibility, not silently clamped into invented valid ammo.

The local controller keeps 32 bounded action receipts. A provisional shot
reserves presentation eligibility for at most 0.75 s, never subtracting from
HUD/server ammo; consumed echo tombstones remain for another 0.75 s. Exact
per-ammo AmmoX provenance reconciles an event-confirmed shot even when recharge
and consumption coalesce into an unchanged absolute count. Retained ammo in
an unrelated newer record cannot release another unsampled shot. Focus loss
clears pending action/reservations but keeps echo tombstones and cooldowns;
true model/weapon/life/session boundaries clear the local receipt scope.
Committed-only events do not infer last-round timing from post-shot reserve;
observed server weapon timers continue to gate subsequent predicted actions.

`RemoteWeaponEffects` binds exact session `events/firehornet.sc`, accepts only
explicit TRACK/FAST values and emits launch audio without changing remote body
animation. Ordinary E11 p-model merge uses the current server weapon model slot.
One fire event alone creates zero projectiles, trails, hitscans, bullet decals,
muzzle flashes, firearm lights, shells or explosions. E13/E14 consume server
damage/DeathMsg/ScoreInfo normally; neither firing nor disappearance fabricates kills.

Shared runtime entities/interpolation/Studio rendering and `EntityFollowPresentation`
present every server Hornet independently. Trail history is bounded and scoped
to network/map/entity lifetime, not the firing player's current life. Model 0
hides immediately; historical trail may decay; one-shot server hit audio may
finish. Slot reuse following an observed removal, model-zero or changed-model
lifetime boundary cannot attach old effects to the new entity. An unseen
same-model replacement between publications cannot be inferred from the public
observations; no guessed spawn identity is added. The transient
tip fix keeps the same 20-ms retained history and fixed segment limit and is
independent of Hornet names, AI or weapon mode. RPG profile smoke remains unchanged.

`WeaponAudio` and remote preparation request the three exact launch dependencies
before ordinary firing through existing approved async assets and the shared
mixer. The new neutral module-profile provenance permits safe flat virtual tokens
from a compiled allowlist without adding HL sample names to generic audio code.
Server buzz/hit use existing precache slots and svc_sound, not this local allowlist.
No file I/O in audio callbacks, new device, raw socket send or external fallback.

## Verification and handoff

### Approved read-only asset findings

Imports use the existing approved-root projection/opener, not direct gameplay
file access. All four MDLs have one bodypart, one skin and zero attachments;
server spawn offsets are not guessed from a nonexistent muzzle socket.

| Resource | Bones | Sequences (frames @ fps) | Imported source vertex bounds |
| --- | ---: | --- | --- |
| v_hgun | 21 | idle1 31@16; fidgetSway 41@16; fidgetShake 36@16; down 19@16; up 31@30; Shoot 11@24 | -10.44,-7.53,-8.81 to 6.67,5.05,9.01 |
| p_hgun | 12 | idle 1@30 | -3.85,-3.99,-9.51 to 4.36,4.58,19.39 |
| w_hgun | 6 | stayput 2@1 | -24.75,-9.86,-10.12 to 14.04,16.33,10.26 |
| hornet | 3 | idle 4@30 | -7.09,-1.9,-0.59 to 2.88,2.28,0.61 |

All nine approved WAVs decoded as mono 22050 Hz. Sample counts:
ag_fire1/2/3 = 9952/8234/8532; ag_buzz1/2/3 = 6120/4226/3448;
ag_hornethit1/2/3 = 8618/5943/5701. Each individual decoded sample and
isolated production mixer output was finite and nonzero, so launch audio
cannot mask an absent buzz/hit in this evidence. No native asset path is emitted
by the runtime control. Original v_hgun Shoot/body1 produced GPU pixels at
three fixed ages. Expanded p-model/projectile/trail verification follows below.

Debug E13–E22 regression selection: 309 cases, 299 passed, 10 optional-asset
skips, 167452 assertions passed. The symbolic tags g1/b1/h3/h4 are not registered;
the actual weapon-presentation/prediction/player-walk/game-api tags are checked
separately rather than claiming nonexistent selections ran.
That actual Debug selection passed 244/245 cases (one optional local-asset
skip), 179826 assertions. An unregistered world-seams selector selected nothing;
the existing BSP seam regression remains in the final full Release suite.

The existing SDL actual-device probe passed 1 case / 5 assertions (backend
opened, voice started, callback consumed frames). This is output-path evidence,
not human listening or E23 manual acceptance. ASan Debug application and core
targets compiled successfully; both runtime probes still exited before main
with `-1073741515 / 0xC0000135`, zero output bytes. ASan runtime remains unverified;
no unrelated sanitizer infrastructure repair was attempted.

Core-only existing `build-core-g1`, `HLCLIENT_BUILD_GAME_HALFLIFE=OFF`, VS2022
Win32 Release: updated core/API target built, 41/41 registered checks passed.
The core/API executable passed 244 cases / 125333 assertions; architecture
guard passed, including its bad direct/transitive/alias dependency fixtures.

Final Release CTest after the last production edit: **3047 registered checks,
2999 passed, 48 skipped, 0 failed**, 532.43 s. The mandatory run had no installed
game root; original-asset controls ran separately above. Skips are explicit
optional assets/private fixtures, unavailable link/reparse/UNC/input-capture
capabilities, or deliberately disabled WFP/provider activation. They are not
counted as successful tests. The actual SDL output test and BSP boundary-seam
OpenGL regression passed. Successful Catch invocations in LastTest.log total
1,175,082 assertions / 3124 case executions; this includes the separately
registered core/API executable, so it is not a count of unique source test cases.
Non-Catch process/script assertions are not folded into that number.
The final `scripts/build_source.ps1 -Configuration Release -Parallel 1 -TestOnly`
completed with exit 0 after CTest: application version/help/Null, transport
self-test, native functional-log observations, G/H1 summary roundtrips, no-stock
launcher fixtures and source-only CheckOnly all passed. Reference/off,
damage-respawn scenario, duration/no-time-limit and external-map argument
contracts remain covered; no actual managed session was launched.

Final focused E23 Debug and Release: **28/28 cases, 2023 assertions, no skips**
in each configuration, with the approved read-only asset opt-in. The actual
p_hgun merge matched 11/12 named child bones (unmatched child bone retains its
own valid local hierarchy); merged matrices are finite, and body-only/full GPU
comparison proves a weapon contribution. Original hornet.mdl and laserbeam.spr
also produce isolated GPU pixels, retain upload identity on repeat rendering,
and the detached trail expires back to the empty baseline. Synthetic mandatory
controls separately cover red/orange tint, curved movement, near-plane clipping,
missing model/sprite independence and 2/8/32 mixed server projectile records.
The first Debug attempt had one **fixture** error: distinct snapshots reused a
record identity. Correcting the fixture's identity/ordinal/time fixed it without
weakening renderer validation or pose assertions.

Changed production files: `include/hlclient/game_api/{audio,presentation}.hpp`,
`include/hlclient/games/halflife/{hornet_profile,local_weapon_presentation}.hpp`,
`src/games/halflife/{local_weapon_presentation,presentation,remote_effects,weapon_audio}.cpp`,
`src/goldsrc/local_audio.cpp`, `src/app/entity_follow_presentation.cpp`.
Tests: four `tests/test_hornet_*.cpp`, `tests/hornet_test_fixture.hpp`, and
`tests/CMakeLists.txt`. Architecture/E22 evidence and this task record updated.
No new renderer, audio device, physics, network grammar or gameplay authority.

Normal build: `cmake --build build --config Release --parallel 2` (existing
VS2022/Win32 preset). Core build: `cmake --build build-core-g1 --config Release
--target hlclient_core_api_tests --parallel 2`; test with `ctest --test-dir
build-core-g1 -C Release --output-on-failure`. The latter cache has the concrete
game disabled; its actual core API vcxproj contains no Half-Life module reference.

Unchanged launcher `-Prediction reference -RemoteAudioPeer -NoTimeLimit -CheckOnly`
passed: zero process launches, no runner/Steam API/WFP/sockets. Script SHA-256
`AAE46322C7D44A8CBDB6080F464664F207FFE2E99CE9C7D28E4E32A37E8B6DBF` unchanged.
Normal executable: `build/bin/Release/hlclient.exe`, SHA-256
`5B09C1E540AE95E4CA7CC61D9363BA9963C9F63E8750BE000C03EED327DF5CAD`.
Preservation audit: none of the 1249 snapshotted files is missing; only the
12 declared pre-existing E23 integration/docs/build-list files differ, with
7 ordinary new E23 header/test/doc files. Existing unrelated source content
is unchanged. SDK pin remains `b1b5cf5892918535619b2937bb927e46cb097ba1`.
`git diff --check` passed; index remains empty. Existing LF/CRLF warnings
were not 'fixed' by rewriting unrelated launcher/scripts.
E23 manual remains `not_run`; new live/HLDS/stock/Steam/WFP/capture sessions=0;
staging/commit/push=none.

Manual command, unchanged two-client Crossfire workflow:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -RemoteAudioPeer -NoTimeLimit
```

Check Hivehand v/p models, server Hornets 0..8 with no clip/R reload, slower
LMB tracking versus rapid straight RMB, shared launch sound/animation, actual
server projectile/trail, hit disappearance/audio, two independent players and
death/respawn while old projectiles remain. After spending ammo, wait and also
switch away/return; only the server refills. Check Egon/Gauss/RPG/Crossbow afterward.
Exact stock RNG, private recharge-state transfer and client homing are not claimed.
Hand Grenade and other later milestones are outside E23.
