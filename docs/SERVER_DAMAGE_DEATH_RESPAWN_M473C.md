# M4.7.3C: server damage, local death and same-session respawn

Current result: `damage_death_respawn_implemented_live_pending`.
This slice implements production behavior in the normal project client.
It does not load stock client.dll. Live launches in this implementation
pass: 0; separate C allowance: 0/2. No commit or push.

## Compatibility and authority

The pinned [Valve SDK](https://github.com/ValveSoftware/halflife/tree/b1b5cf5892918535619b2937bb927e46cb097ba1)
defines the HLDM contract. Reviewed: player.cpp LinkUserMessages,
UpdateClientData, Killed, PlayerDeathThink, Spawn, StartDeathCam;
client.cpp ClientKill/receiving-client projection; multiplay_gamerules.cpp
DeathNotice/respawn rules; health.cpp, death.cpp, hud.cpp/hud_msg.cpp,
const.h, in_buttons.h and network/delta.lst.

Damage is 12 bytes: saved armor, damage amount, all uint32 type bits and
three signed-short coordinates scaled by 1/8. DeathMsg is killer/victim
entity indexes followed by terminated weapon text, not the CS layout.
The receiving-player entity is ServerInfo's zero-based slot plus one.
World killer 0 and unknown weapon text are inert metadata.
Messages use the current registered name/size catalogue, never fixed IDs.
Health/Battery remain canonical absolute values; Damage is not subtracted.

SDK receiving-client projection does not explicitly set cd->deadflag.
Consequently a valid nonzero deadflag (1..4) with receiving view context,
or a local DeathMsg corroborated by zero-HP receiving state, establishes
death. HP zero alone, missing viewmodel or an unusable seed do not.
DeathMsg may precede or follow clientdata. ResetHUD is a presentation
reset, not a life boundary; InitHUD is not required every spawn.
Fresh positive HP and a valid receiving view context establish new life
only after the accepted death boundary. Reassembled completion metadata
cannot establish the new-life boundary. A local epoch is not a wire field,
network generation or server execution ACK.

The narrow explicit script request is the fixed typed
`clc_stringcmd "kill"` payload, once per check. The exact supplemental
[ReHLDS sv_user.cpp](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/sv_user.cpp)
and [host_cmd.cpp](https://github.com/rehlds/ReHLDS/blob/6266cd23faee4a6e9cf3974f9605b2cadd86f0a4/rehlds/engine/host_cmd.cpp)
were reviewed: validated client string command -> registered kill handler
-> game ClientKill callback. No engine implementation/tests were copied.
There is no arbitrary console-command API, RCON, cheats or manual kill key.
Self-kill does not verify nonlethal damage or armor absorption.

## Production ownership and retained state

RuntimeReplaySession's existing dispatcher decodes owning bounded event
values with exact record identity/cursors and stages the entire record.
The renderer-neutral local-player lifecycle owner runs before atomic
publication. Malformed suffixes publish no HUD/lifecycle effects.
Its bounded event high-water prevents duplicate delivery; identical bytes
in distinct legal records remain distinct events. Damage feedback is a
non-directional 600 ms HUD label, not a physics timer or hit marker.
Unknown values are unavailable, not confirmed zero.

Death suspends alive prediction and clears prediction history, correction
residuals, H3 presentation pairs, H4 transition eligibility, button latch,
B1 provisional/confirmation/animation/recoil state and viewmodel bindings.
New life clears old transient feedback. First-person weapons stay hidden
until current authoritative binding exists. A presentation release barrier
consumes neutral input after respawn before permitting a new provisional
attack; committed wire commands are never rewritten.

The same socket, NetchanDriver, Steam lifetime, network reliability and
command sequence identities continue. Schemas, baselines, entity/clientdata
delta bases, wire command history, model namespace/imported assets and GPU
cache are retained. No reset_generation is invoked for death/respawn.
RX/TX and the 20 ms scheduler continue while dead, including ordinary
release and Space/LMB usercmds. Server readiness is not inferred from time.
Forced new-life state is accepted too.

New prediction needs current collision context, a strictly post-boundary
exact carrier, and a neutral command when no new-life oldbuttons history
exists. Old carrier ACKs cannot seed new life. Replay contains only eligible
post-boundary actions. The server-derived spawn origin replaces corpse
presentation immediately; it is not interpolated across the map.
Dead view uses exact server origin/view_offset, including zero offset, and
ignores stale alive prediction. Existing Setangle source is applied once.
This is not a claim of stock-exact full spectator/deathcam behavior.

## Process and bounded evidence contract

Manual idle/dead sessions can exit normally without scripted motion coverage.
Actual runtime errors retain priority over feature coverage. Application,
feature and managed cleanup/restoration results remain separate.
C-only diagnostics flow client -> native parser -> wrapper JSON and survive
bounded excerpt selection with optional [info] prefix and 1/true booleans.
Old modes do not acquire mandatory C keys.

Evidence includes generation/epoch, deaths/deadflag, damage counts and
before/after Health/Battery values with their source, input submitted,
server alive, retained driver, fresh samples/commands/rendered frames,
pre/post model/weapon/current HUD, prediction state/reason/anchor/depth/end,
replayed command count and seed status. Source tokens encode
record identity-ordinal-transport sequence-start bit-end bit-reassembled.
Before/after values are committed record snapshots, not server execution
ACKs or a claim of armor calculation. Missing evidence stays unavailable.
Pre-life HUD values are the last presented canonical HUD snapshot before
the observed death transition, with their exact Health/Battery/clientdata
source; they are not invented when no preceding HUD frame exists.

The scripted mode has bounded initial/death/release-press/movement/Glock/
crowbar/neutral phases. It emits one kill request, ordinary neutral/button
pulses, and catalogue-validated owned weapon requests. Server alive and
fresh bindings are required; a held respawn click cannot create provisional
fire while dead. No blocking GPU probes are added to the command loop.
Full cycle verification additionally requires fresh post-respawn samples,
submitted commands, rendered frames and restored active prediction.

No reproducible normal hazard route is claimed here:
`damage_live=not_observed`, armor absorption unverified. A subsequent real
successful check can report `live_death_respawn_verified_damage_pending`.
Fake peers, CheckOnly and self-kill never prove combat damage.

## Desktop handoff

Use the existing Administrator PowerShell 7 workflow; no automatic
elevation. Jobs/WFP/isolation/cleanup/restoration are unchanged. One
invocation is one session, no comparison or automatic retry.
Settings remain valve/boot_camp, ports 27242/27243, existing research HLDS,
current Release client, reference prediction and OpenGL; active bound 90 s.
Preparation and cleanup remain separate phases.

Explicit C check (one next invocation):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -Scenario damage-respawn-check
```

Unchanged manual command (a separate alternative, not an automatic second run):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

`-CheckOnly` validates both scenarios without runner/stock/WFP/sockets.
After a failed actual C session, analyze its exact summary before considering
the one remaining retry. Repeating this prompt does not renew the allowance.

Manual C checklist is not_run: server damage/HUD if naturally available ->
local death -> release all buttons -> Space or captured LMB -> server respawn
-> smooth movement/jump/duck -> weapon/animation/HUD without old-life state.
No need to search for a hazard to run the explicit death/respawn check.

Preserved attribution=user_report: smooth WASD/Shift/mouse/jump/duck/crouch-walk,
Glock/crowbar selection and HUD, camera-space viewmodel at extreme pitch,
Glock fire/reload animations and crowbar swing. Visible camera recoil was
not separately confirmed. Crowbar swing is not a hit/damage proof.

## Verification record

Final normal Debug/Release and ASan Debug affected client/tests/orchestrator/
checker targets built successfully. This is not a live proof.

- C focused normal Debug and Release: 13 cases, 591 assertions each, all passed.
- Relevant ASan Debug: 52 passed, 1 capability skip; 1,874 assertions passed.
- B transport/resource, B1 weapon, H4 movement/prediction, A1 first-person and
  live-visual regressions: 430 passed, 2 capability skips; 150,664 assertions.
- Owned OpenGL viewmodel: 1 case/236 assertions; opt-in read-only installed
  first-person MDL capability: 1 case/260 assertions; both passed.
- One final broad offline Catch suite: 2,073 passed, 21 capability skips;
  386,374 assertions. Exclusions:
  `~[udp]~[network]~[isolation]~[orchestrator]~[steam]~[live]~[loss]~[security]`.
  This is not an unqualified all-tests/stock-session claim.
- Eight selected offline application replay controls passed; literal
  canonical hash remains 18197719904158225405.
- Existing private functional capture: 323/323 records, zero errors,
  canonical hash remains 7014210005320501317; corpus hash
  614a07db0d29c13cc7a121c636deeda9e75e12bb75956dcb3ac609896c78137c.
- Native functional-log and PowerShell failure-retention/C contract self-tests,
  no-stock publication roundtrip and launcher CheckOnly from system32 passed.
  CheckOnly validates both manual and C scenarios.

Current Release hlclient.exe SHA-256:
`9404BD086683818B838670524B9E8764D2E306E5A67C0869DBC6BEBCF9461C61`.
Detailed reports are retained under ignored manual-artifacts/task-records;
the current C record is M4.7.3C-current.md. Initial mixed-layout intermediate
build failures are retained and not counted as final results. Coherent
recompilation used only a CMake-cache compiler marker, with no cleanup.
Historical artifacts and budgets are not relabeled.
The 77-file prechange snapshot was hash-verified with zero integrity failures;
49 prior source files remain byte-identical, while C extends 28 existing files.
HEAD/branch are unchanged; staged diff is empty; git diff --check passed.
No C stock/client/HLDS session ran; C budget remains 0/2.
