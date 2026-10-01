# M4.7.3D2: Use interaction through G1

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`; branch
`codex/stock-runtime-campaign-5e48b7c1`; starting HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`. Existing dirty/untracked work
was preserved in the established source snapshot mechanism under
`manual-artifacts/task-records/use-d2-prechange` (150 source files plus
pre-edit offline baseline: 203 tests, 128950 assertions). No G1/D1 rewrite.

New D2 stock/live/manual verification: `not_run`. No Steam initialization,
HLDS/stock-client launch, WFP activation, capture or ETW; no staging/commit/push.
The additive pickup confirmation is in `MANUAL_USER_REPORT_D1_CROSSFIRE_20260927.md`
with `attribution=user_report`, not a new Use result.

## Production boundary

Physical SDL scancode E (not layout-dependent keycode) -> existing neutral
Use action -> `GameClientHost::movement_policy()` from `HalfLifeClientModule`
-> existing 20 ms scheduler/input adapter -> immutable wire command history
-> planner/encoder -> existing NetchanDriver -> server observations -> G1
Half-Life message/HUD composition -> existing scene/render primitives.

`src/games/halflife/movement_policy.cpp` admits Use and selects its physical
binding, capture/life scope, reference button profile and ground speed cap.
The shared input adapter maps the selected typed compatibility profile to
`IN_USE=1<<5=32`, explicitly, not by enum coincidence (neutral Use mask is 4).
Old v1/v2/v3 allowlists remain unchanged; v4 rejects other unknown buttons,
impulse and unsupported fields. Attack/reload/jump/duck are retained.
No arbitrary console commands, object packets, socket access, second input
loop or second predictor were added.

The host-owned neutral scoped-action gate clears Use on focus/capture loss
and death/new-life boundaries. Stale held input cannot resume it after
recapture; physical release or genuinely new press removes the barrier.
OS repeat does not create presses. Short taps use the existing pending latch,
consumed only after matching history insertion. Use remains in immutable
backups. Retry/replay/render never create local interaction effects.
RX remains active on updates with contextual command TX.

## Movement, reference and authority

Reviewed local Valve SDK revision
`b1b5cf5892918535619b2937bb927e46cb097ba1`: `common/in_buttons.h`,
`cl_dll/input.cpp` button state, `dlls/player.cpp::PlayerUse`,
`pm_shared/pm_shared.c::PM_CheckParamters`, `dlls/healthkit.cpp::CWallHealth::Use`
and actual armor charger `dlls/h_battery.cpp::CRecharge::Use`.
No third-party functions/tests were copied. SDK behavior does not establish
all stock binary compile macros or universal mod compatibility.

HL1 policy selects a simulation-local ground command maximum-speed cap of
1/3. The existing H4 kernel computes it once after initial ground categorization,
before jump/duck, then applies command vector limiting and existing duck
scaling. Ground+Use+jump keeps the cap for that starting-ground step; already
airborne Use does not reduce speed. Wire normal/Shift remains 400/120.
Server MoveVars and base maximum speed are immutable; replay recomputes the
same step, without cumulative scaling. Enabled configuration participates in
session identity; inactive configuration signatures/Use-off endpoints are
preserved. New explicit profiles are
`public_goldsrc48_jump_duck_weapon_use_prediction_v4` and
`reference_carrier_jump_duck_weapon_use_v4`; the existing movement-state
v2 hull/duck contract is retained.

HLDS alone chooses target, range/orientation, eligibility, one-shot/continuous
behavior and station energy/rate. No local health, armor, button/door state,
charger juice, interaction target or recharge timer is synthesized.
Existing atomic Health/Battery handlers publish absolute server values;
Use itself never creates ItemPickup/AmmoPickup feedback. Server entity
updates keep using the existing render path. No new svc grammar was needed.
Water/ladders, train/frozen/spectator contexts, non-walk modes and base velocity
remain truthful reference seed fallbacks. Moving-brush collision/platforms
and complete doors/trains support are outside D2; sound and armor absorption
are not implemented/claimed.

## Bounded diagnostics

The existing terminal `live_application_outcome` fixed allowlist is extended
from 41 to 55 inert fields. Native JSON/status and PowerShell retention use
the same appended fields; first typed decoder/module cause is unchanged.
No frame log or GPU readback was introduced. Manual completion still depends
on application health/cleanup, not scripted achievements or Use success.

- `use_press`, `use_release`: committed command rising/falling edges, not OS
  repeat or a claimed count of successful interactions.
- `use_generated`: new immutable commands carrying Use.
- `use_transmitted`: newly sent command identities carrying Use, excludes backups.
- `use_clear_transmitted`: sent Use-to-clear transitions (including capture/life
  cancellation); `use_sent` is only transport evidence, never server Use ACK.
- `use_health_before/after`, `use_armor_before/after`: available absolute
  server HUD observations around first sent Use and terminal observation window.
  Missing values stay unavailable; even a delta has unavailable causality.
- `use_server_effect=not_observed`, `use_reason=unavailable` when no delta is
  observed; otherwise `observed_delta_cause_unavailable`, not inferred healing.
- `use_prediction_state`, `use_prediction_reason`: existing real prediction
  state/fallback; these never replace the primary runtime failure.

A scalar transport count is read during manual observation sampling, avoiding
per-frame copies of bounded history/server sample vectors. Game semantics and
HP/armor window interpretation remain in `games/halflife/action_evidence.cpp`.

## Manual test, not yet performed for D2

The existing launcher keeps crossfire default, explicit boot_camp/stalkyard,
reference/off, keyboard/mouse and Scenario damage-respawn-check. One invocation,
one managed session. New E/hold-E help is added without mandatory parameters.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Approach a station, aim at it, hold E and check server HP/armor growth, then
release E and check cessation after delivered release and in-flight updates.
Health testing requires missing HP and server eligibility; full HP is not a
failed healing test. Armor testing needs missing armor and server conditions,
not missing HP. No cheats, RCON, teleport, stock-file edits or automatic attack
button activation. If eligibility is not encountered, record `not_tested`.

Reuse the prior actual crossfire inspection in
`CROSSFIRE_MANUAL_TEST_DEFAULT_D1.md`: BSP SHA-256
`6222243E0839022F3041E6B9E97776CE54D0CF87D4C3810E627B8B2815B555F5`,
6 health and 2 armor stations. Initial brush bounds (not current availability):

| Class | Entity/model | Minimum -> maximum |
| --- | --- | --- |
| func_healthcharger | 3/*1 | (-144,-2112,-1728) -> (-112,-2104,-1680) |
| func_healthcharger | 4/*2 | (-64,-2112,-1728) -> (-32,-2104,-1680) |
| func_healthcharger | 5/*3 | (32,-2112,-1728) -> (64,-2104,-1680) |
| func_healthcharger | 6/*4 | (112,-2112,-1728) -> (144,-2104,-1680) |
| func_healthcharger | 135/*40 | (448,384,-1504) -> (456,416,-1456) |
| func_healthcharger | 136/*41 | (-848,256,-1504) -> (-816,264,-1456) |
| func_recharge | 105/*38 | (272,1200,-1824) -> (280,1232,-1776) |
| func_recharge | 106/*39 | (272,432,-1824) -> (280,464,-1776) |

Buttons receive the same standard Use input. Not every door is E-activated;
actual map/server behavior decides. No universal support claim is made.

## Verification status

Primary result: `halflife_use_interaction_integrated_offline_verified`.

| Gate | Result |
| --- | --- |
| Affected normal Debug/Release and existing ASan Debug builds | Passed, incremental; no clean builds |
| Focused D2 production paths | 6 passed, 759 assertions |
| Debug, Release, ASan focused G1/input/Use/transmission/H3/H4/B1/C/D1/diagnostic regressions | Each: 251 passed, 131677 assertions |
| Debug/Release/ASan pure core/API tests | Each: 93 passed, 1318 assertions |
| Existing core-only build, concrete HL1 OFF | Engine libraries built; 93 tests/1318 assertions passed |
| Alternate-module/architecture guard | 2/2 CTest gates; zero concrete HL1 references in actual core projects and compiler read logs |
| Explicit Release actual-context GL B1/A1/HUD controls | 6 passed, 1152 assertions; zero GL skips |
| Network-free Release Null/application replay process checks | 8/8 passed, canonical expectations unchanged |
| Three owning decoder/module failures + Use diagnostics through native/fake-child/PowerShell | All 55 fields retained, primary cause/exit 2 preserved, fake-child cleanup and self-test restoration exact |
| Existing guarded no-stock launcher fixtures | 11 child cases; default/explicit maps, reference/off, Scenario, CheckOnly, stdout/fallback and exit codes passed |
| Actual launcher CheckOnly | Passed; no runner/game/Steam/WFP/socket |
| Historical artifacts | All 7 retained historical run hashes unchanged |
| Source whitespace/index/HEAD | Passed; empty staging, unchanged branch/HEAD |

All Catch runs use seed 2531315663. No blanket/full suite was run; only the
listed focused tests and explicit offline CTest gates. Proprietary local
asset/capture controls and live charger eligibility are not_run. Initial
compile/test-fixture diagnostic failures are retained in separate local logs;
no production behavior expectations were changed to obtain a pass.

The normal `build/bin/Release/hlclient.exe` and required manual helpers are
ready. Release SHA-256:
`018262F824C7C7CA14A43D711630A879F443A226B484FF12050BE7BFAB7CDDA0`.
Detailed local build/test/roundtrip logs remain in
`manual-artifacts/task-records/use-d2-prechange`; required code and docs are
ordinary project files, not hidden in that artifact directory.

D2 live/manual remains **not_run**. Historical root cause is not retroactively
declared fixed by offline integration. New live launches=0;
staging/commit/push=none; no other worktree or stock-game file was changed.
