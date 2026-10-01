# M4.7.3B1: narrow local weapon presentation

Reference revision: Valve SDK `b1b5cf5892918535619b2937bb927e46cb097ba1`.
This is independently implemented presentation policy, not Valve gameplay code,
client.dll ABI, or a generic event interpreter.

## Factual boundary and root cause

The two historical B runs and the user's manual report confirm server Glock
fire/reload and weapon selection. B run 2 observed clip 17 -> 14 -> 17,
reserve 68 -> 65, reload start/completion, and zero svc_weaponanim messages.
It did **not** verify local fire/reload/swing animation or recoil.

Pinned source routes reviewed in this continuation:

- dlls/glock.cpp: PrimaryAttack uses the 0.3 s GlockFire route, decrements clip
  before selecting the empty-shot event parameter. Direct shot SendWeaponAnim
  and canonical punch assignment are under OLD_WEAPONS. Modern shots use
  PLAYBACK_EVENT_FULL.
- cl_dll/ev_hldm.cpp: EV_FireGlock1 chooses sequence 3/4 from post-shot empty
  state, body 2, and V_PunchAxis(0,-2). EV_FireGlock2 also punches; secondary
  fire remains out of scope.
- dlls/crowbar.cpp: first swing dispatches FEV_NOTHOST. The client event
  EV_Crowbar cycles miss sequences 4,5,7 with body 1; hit outcome stays server
  side. Server miss interval is 0.5 s.
- dlls/weapons.cpp: SendWeaponAnim sets weaponanim, but CLIENT_WEAPONS +
  ENGINE_CANSKIP can suppress local svc_weaponanim. DefaultReload selects the
  empty/nonempty Glock sequence 5/6 with max clip 17 and delay 1.5 s.
- cl_dll/hl/hl_weapons.cpp: local SendWeaponAnim/HUD_WeaponsPostThink and
  g_runfuncs-gated correction provide client presentation in the reference.
- cl_dll/view.cpp: local event punch is added to presented view angles, then
  magnitude decays by the reference length-dependent recurrence.
- common/in_buttons.h and network/delta.lst establish primary/reload bits,
  weaponanim, punch and supported decrement-timer fields.

Our stock connection advertises cl_lw=1 (ClientConnectionSettings default).
The active profile is valve/boot_camp, public GoldSrc48 reconstruction,
steam-hlds-10210-no-mode-banner-v1, and the standard rooted v_9mmhandgun/
v_crowbar resources. The runtime decoder projects svc_weaponanim and
WeaponList/CurWeapon/AmmoX. Resource type event_script is catalogued, but
there is no svc_event/.sc dispatch-to-local-animation layer or loaded
client.dll. The old provider consequently retained idle/clientdata without
equivalent shot/swing/recoil presentation.

Compatibility conclusion: this client-side presentation gap is consistent
with the pinned modern prediction contract and the actual observed profile.
Zero svc_weaponanim alone is not network-loss proof and is not a B1 blocker.
Factual boundary: server_action_confirmed=true (historical B/user evidence);
server_animation_message_absent=true (historical B); client_local_event_layer
absent before B1, narrow equivalent presentation implemented in B1.
The map/game profile is the managed requested configuration; the historical
serverinfo_game field was unavailable, not an observed literal "valve".
The exact HLDS binary compile macros are **unavailable**, not inferred from
SDK source. No damage, bullet hit, or crowbar hit is inferred.

## Ownership and authority

One LocalWeaponPresentationController in the normal application loop receives
immutable newly submitted command identity, committed server observations,
and metadata from the already-imported sparse Studio resource binding.
It owns no SDL state, socket, renderer handle, movement state, or mutable
canonical state. Only animation/restart/time/status and local camera punch
are published. HUD continues to read canonical observations.

Profiles: public_hl1_glock_presentation_v1,
public_hl1_crowbar_presentation_v1, public_hl1_glock_visual_recoil_v1.
The exact standard virtual model name, generation, resource revision,
sequence count/fps/frame count and Studio body selection are checked.
Unsupported models fail closed; invalid poses retain previous valid visuals.

Precedence: fresh exact service event > changed current-record clientdata
state > coherent fresh weapon transition > eligible submitted command >
retained state. Matching confirmations retain restart/time. Sequence
correction restarts once; body-only correction keeps the action timeline.
Retained clientdata/records and replay/backup/retry do not create actions.
Bounded command/event identity caches reject conflicting duplicates before
partial publication. Stale generation/revision/time is rejected.

Glock uses post-shot sequence 3 or empty-shot 4, body 2. Reload uses 5 empty
or 6 nonempty, body 0, canonical reserve/clip and reload eligibility. It waits
for coherent server reload start/completion. Crowbar cycles 4,5,7, body 1;
timer transition confirms the action, not hit/damage. Confirmation deadlines
are bounded independently of actual Studio animation duration.

Actual sequence metadata controls animation completion. The existing Studio
evaluator continues to evaluate bones; nonloop actions return to the current
valid idle. A1 model-local transform, dedicated pass, depth policy, world
and screen-space HUD are unchanged.

## Recoil timing policy

The modern event's -2 degree pitch impulse is set once per eligible submitted
shot. Its decay is an independent derivation of the pinned magnitude
recurrence on fixed 20 ms physical ticks, plus a fractional residual:
`max(0,22 * 0.99^ticks * (1 - residual/2) - 20)`.
This explicit versioned timing adaptation avoids render-FPS-dependent
integration. It does not claim bit-identical stock variable-render-step
rounding. A fresh finite nonzero canonical punch replaces local punch for
that action (no double application and no resurrection). Rejection clears
local punch. Mouse base angles, usercmd angles, canonical punch and movement
are unchanged.

First capture click still does not attack. Focus loss clears input upstream
and cancels pending provisional actions; confirmed visuals may finish.
Secondary attack is unsupported.

## Production verification

Explicit mode: scripted-fire-reload-presentation-check. It requests two
separate Glock shots, waits for each fresh clip decrement and an observation
window, requests reload, waits for canonical completion, then selects
crowbar and requests one swing followed by neutral tail. No blind action
retry. The scenario is 11 seconds, with an exclusively B1-scoped 90-second
total runtime boundary. Other modes retain their existing limits.
Known client m_flNextAttack and weapon next-primary decrement timers gate
visual eligibility; aged values are never published to canonical state.
The script checks coherent weapon/model source and waits the crowbar draw
window before its one swing request.

The client separates server fire/reload, fire/reload/swing framebuffer proof,
camera recoil and canonical HUD evidence. Native status booleans are
true/false (the parser also accepts the client's 0/1); JSON booleans are
typed. New required keys and the staged weapon_prediction object apply only
to B1. The final wrapper retains it under native_summary.weapon_prediction.
The bounded client line also retains action/recoil counters and provenance.

Core CI uses an entirely project-authored multi-sequence Studio triangle
fixture through the shared pose evaluator and actual GL pass. Opt-in
HLCLIENT_LOCAL_GAME_ROOT checks installed stock viewmodels read-only; no
Valve assets or screenshots are committed.

## Manual verification (not run automatically)

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Check first-click capture without a shot; every confirmed Glock shot has
visible action/recoil; R consistently animates while canonical clip/reserve
updates; crowbar has an obvious swing; pitch ±80, HUD, smooth movement,
jump/duck remain correct. Hit/damage is not claimed.
manual_weapon_presentation_prediction_validation=not_run until user report.

Deferred: secondary fire, sounds, muzzle flash, shells, bullet/spread/trace,
hit/damage/decal visualization, generic .sc interpreter, client.dll ABI,
full weapon gameplay prediction, bob/sway.

## Initial verification and preflight blocker (before user manual launch)

Result: client_weapon_presentation_implemented_live_pending.
Implementation and offline/GL gates passed; live presentation is NOT claimed.

- Release MSVC Win32 build: hlclient, hlclient_tests,
  hlclient_stock_runtime_orchestrator passed.
- Deterministic B1: 14 test cases / 140 assertions.
- Prediction/movement/jump/duck, attack/reload physics invariance,
  capture/focus, reference command ownership/cadence, RX while TX, replay,
  explicit CLI/scenario and scoped timeout: 162 cases / 37662 assertions.
- Owned Studio actual OpenGL: 1 case / 236 assertions; no skips.
- Installed Glock/crowbar actual OpenGL: 1 case / 260 assertions; no skips.
  Includes action framebuffer changes, idle return, pitch 0/+80/-80,
  GL_NO_ERROR, valid world/HUD and bounded uploads.
- Native offline summary self-test and wrapper failure-retention test passed:
  missing/malformed booleans, partial failure, unrelated modes, [info] prefix
  and restoration-retained summary coverage. No stock process in these gates.
- git diff --check passed, HEAD unchanged, no staging/commit/push.

Evidence files under manual-artifacts/task-records:
b1-unit-tests.txt, b1-regression-tests.txt, b1-owned-opengl.txt,
b1-local-mdl-opengl.txt, b1-native-parser.txt, b1-wrapper-parser.txt,
b1-preflight.txt and M4.7.3B1-current.md.

The one managed runner invocation verified research inventory
prepared_projection_content_verified and the official Valve-signed Steam
API runtime. It then stopped at network_isolation_privilege_required:
PowerShell lacked Administrator elevation for WFP. Automatic elevation is
explicitly forbidden by the owned runner. No retry was made.

B1 actual live sessions=0/2, accepted runs=0, run IDs=unavailable.
Project app exit=not_run; runner exit=1.
Live server clip/reload/timer/punch/presentation observations=unavailable.
No production native summary or functional-smoke-wrapper.json was produced.
Owned processes, network operations and runner-created files=0.
Cleanup/restoration=not_applicable, not "exact live restoration": the
transaction did not begin. Final read-only process/UDP enumeration found no
game/owned runner processes and no owners on 27242/27243.

Historical evidence is not relabeled as a B1 acceptance run:
B IDs 7b3f7df916074252bfa683094375fe39 and
13c55c8f89f349839da9a9da0244bf98 remain separate (B budget exhausted 2/2).
The latter confirms three Glock shots, clip 17->14->17, reserve 68->65,
reload start/completion and selection, but not B1 presentation.
No historical artifacts, original client, primary Steam tree or Valve assets
were changed/committed; no capture/ETW/strict campaign was started.

### Continue the first actual managed run

Requires an Administrator PowerShell 7. This is the existing managed runner,
NOT the manual keyboard/mouse session, and retains WFP/Jobs/restoration,
valve/boot_camp, the Release client/research HLDS and 90-second limit.
Run only once when the elevated context is ready; retain its reported exact
run ID and final wrapper JSON. Do not launch a second session after success.
A second session needs an observed first-run blocker and verified fix.

```powershell
. "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -CheckOnly
$b1ManagedConfig = Get-H2ManualConfiguration -Root 'D:\DEV\CPP\HLC-steamcfg-5e48b7c1' -Mode reference -ResearchRoot 'D:\DEV\HLCLIENT-RESEARCH\Half-Life' -SteamRoot 'D:\Steam\steamapps'
$b1ManagedConfig.ProjectClientLiveInput = 'scripted-fire-reload-presentation-check'
& 'D:\DEV\CPP\HLC-steamcfg-5e48b7c1\scripts\capture_stock_runtime_state.ps1' @b1ManagedConfig
```

Manual presentation validation remains not_run until the user's report; its
separate command/checklist above is unchanged and was not executed.

### SHA-256 at final preflight

```text
build/bin/Release/hlclient.exe
0CCAF4EE1FEEB60E21AF2207B28CCF61406E02B21AC04DCBDFCF910B5F102628
build/bin/Release/hlclient_stock_runtime_orchestrator.exe
0AC4CBEF6E7C67DE6825E8594A3C5D4701C09E61F0E8509E981CB83629BB1D4E
build/bin/Release/hlclient_tests.exe
295E045CDAE6D693A343C2D438FEB7DF5A6EF04C1D34DD9CEAEB532E2E712D97
include/hlclient/app/local_weapon_presentation.hpp
374B8768B745BFC4F9EEFF01084195FFDB6E6A2CA0BE613938960874065C6B66
src/app/local_weapon_presentation.cpp
6A23E817656AFE52F0BF2477904BD5488F277535E194FCAE1509AE03FCBCD235
include/hlclient/goldsrc/weapon_presentation_script.hpp
2FE4FE4E1CFA7BBD08F3B8BE7559BC217F075B24A8E4DE18C67F6E7E2C23E0B0
apps/hlclient/main.cpp
ADB4E14F7BF489FC1A7EBCCADA945BC80C60EA3991BC0AC17001E2ACE2F0A498
apps/hlclient_stock_runtime_orchestrator/main.cpp
54E5D0F9EBBE30B233CE32A128D604EA817E0E04E449D48464D981DA9C2AA76D
scripts/capture_stock_runtime_state.ps1
08A90C025FB09232B32A8D5BF9DBD9F8EF1FB938AAB59879CB73BEEBC16A9388
tests/test_local_weapon_presentation.cpp
57F17D5DB61B991EDCB1659F4467AC756925F2F932B9287F678FCA2CFF4B499E
b1-prechange/inventory.json
7A2B9A953CD41515070E8001F90E5D6EF9194C46984EA375E173DB0AADA97436
```

The accumulated pre-B1 dirty/untracked source snapshot was hash-verified
before implementation and the inventory hash reconfirmed after preflight.
Research hlds.exe SHA256:
BE96B878E5F4BE6BDD4E1AD21603C570D8AB4238542F36A7F84C4AB573CCCE8C.
Research valve/dlls/hl.dll SHA256:
D932D274061EE05BEF89EA712F9B32B8F5F742AD0124D5F516C8CCA1E421CA24.
Exact binary FileVersion/build macros remain unavailable.

## Subsequent user manual launch and resource fix

The user then ran the manual keyboard-mouse launcher in an elevated shell.
Run 1fddd047260a4645a8021bc1d20a7601 passed Steam/connection/schema and
sendres ACK but timed out before resource list/runtime/assets/input.
Final wrapper result=local_server_ready_client_blocked; app exit=2;
owned_process_cleanup=exact; restoration_status=exact.
This is not a weapon presentation acceptance result.

The subsequent user-authorized offline fix adds rate-bounded empty transport
polling to the live compatibility profile, preserving strict behavior,
semantic request ownership, command cadence and all timeout bounds.
Build and regression/OpenGL gates passed; no further live session was run.
See [resource sign-on fix and current binary hashes](RESOURCE_SIGNON_IDLE_POLL_FIX.md).
Conservative current managed budget=1/2 consumed by the user's manual run;
accepted B1 presentation runs=0. Manual presentation validation remains
not_run, because the manual launch never reached runtime.
