# Optional server-assisted 50 HP start

Result: `optional_50hp_test_start_integrated_offline_verified`.
Live/manual validation of this profile: `not_run`. No Steam initialization,
HLDS/stock-client session, WFP activation, capture, ETW, staging, commit or push.
This is a treatment-condition helper, not damage/armor-absorption evidence.

Worktree: `D:\DEV\CPP\HLC-steamcfg-5e48b7c1`, branch
`codex/stock-runtime-campaign-5e48b7c1`, unchanged HEAD
`ea738a80e5a66a500c245f95b96eed32198e2d66`. The existing snapshot mechanism
preserved 152 dirty/untracked source files in
`manual-artifacts/task-records/optional-50hp-prechange/source`; inventory hashes
verified unchanged. G1/D1/D2/B1/C/H3/H4/A1 implementations and historical manual
reports were preserved, not restarted or rewritten.

## Mechanism and dependencies

The installed research `valve/liblist.gam` points at `dlls/hl.dll`, with no
configured health-setting addon. Inspection of the available HLSDK
`dlls/client.cpp` ClientCommand implementation found no supported sethealth or
hurtme command. Instead this uses **one** mechanism: a small project-owned
Metamod-P plugin, `test_server/start_health/plugin.cpp`. It uses the public
engine-owned `edict_t::v.health` member inside the server API callback, not
another process's memory, a private player layout, fabricated messages, or a
replacement game DLL. It never calls a damage/healing routine.

Metamod-P is explicitly a Half-Life 1 engine/game DLL plugin manager, not
Metamod:Source. The official [Metamod-P usage contract](https://metamod-p.sourceforge.net/doc/html/metamod.html)
documents the liblist loader, `+localinfo mm_configfile`, explicit original
`gamedll`, and plugin list. Pinned official dependency:

- [Metamod-P v1.21p109 release](https://github.com/Bots-United/metamod-p/releases/tag/v1.21p109),
  Win32 asset `metamod-p-v1.21p109-win32.tar.xz`.
- Release asset SHA-256 (published by GitHub release asset metadata and checked):
  `564838D9A42032EE3BC44EEB2616896E0789C3CE8D7A033D81F3F795B67E31ED`.
- Extracted official x86 `metamod.dll` SHA-256:
  `16B849F1CBF1503266A1B89D9B4ED38807091FABE724A8607D544338D155A005`.
- API/HLSDK headers from the same official source commit
  `dc4f6d8d6271658268a19e22cbc88706f9c1c78e`; source archive SHA-256 pin
  `10D5FCF73ED946D8A4B036214122B265A02A174233F71B57287C31D431A41C6C`.
- Project helper Release SHA-256:
  `5C0492FB998D8A20016B53AE8CAD855FE7BF17F2AE918208112959A907D481BE`.

`scripts/prepare_test_start_health.ps1` reproducibly downloads/verifies the
official asset and configures only the standalone addon build using the pinned
API source archive. It builds/tests Win32 Debug/Release with VS2022. The helper
uses static CRT; PE dependency inspection shows only `KERNEL32.dll`.
The component receipt checks current project source hashes and helper bytes,
rejecting missing, modified or stale outputs before launching. The license and
HL Engine/MOD exception are in `test_server/start_health/LICENSE.txt`.
No AMX Mod X, installed Half-Life SDK, full game rebuild, or new client runtime
dependency is used. Source code is ordinary checkpointable project files;
downloaded/build products remain reproducible build artifacts.

For a clean-source reproduction, from the repository root:

```powershell
pwsh -NoProfile -File scripts/prepare_test_start_health.ps1
```

Already prepared in this worktree. This script never installs to research or
Steam and is not a second launcher.

## Scope, identity and restoration

`-TestStartHealth 50` is supported only for manual keyboard-mouse, reference,
crossfire through the existing owned managed HLDS transaction. Other health
values and incompatible map/prediction/scripted scenarios fail before process
launch. Normal crossfire/default/explicit maps/reference/off and the scripted
damage-respawn-check path remain available **without** this parameter.

The wrapper validates the original research projection/integrity and obtains
the complete existing restoration guard first. Only this optional profile then
adds `valve/addons/hlclient_test50/<run-id>/` with two verified DLLs, explicit
Metamod config/plugin list and empty exec config, and temporarily changes only
the liblist gamedll configuration. The original `hlds.exe`, `hl.dll`,
`client.dll`, `crossfire.bsp`, and main Steam installation are not modified.
Nothing is added to the global mutable allowlist. Native host passes the exact
run ID/profile via bounded `+localinfo` arguments before `+map`.

The local project client gets `HLC50_<first-24-hex-of-run-id>` as a bounded
30-character session name. The plugin requires the complete 32-hex server
run token, profile, crossfire/deathmatch, original accepted loopback connection,
that exact name, engine edict/serial identity, unique connected target, alive
real player, non-godmode/damageable status and server health/max_health=100.
Slots are checked, never assumed to be 1. Ambiguity/rejection cannot choose a
random player. `ClientPutInServer` post runs after the original game's first
`CBasePlayer::Spawn`; it changes only health to 50 and consumes the one-shot.
There is no frame, think, spawn or timer health hook. Duplicate callback,
healing, later respawn or reconnect cannot rearm it; maximum remains 100.
Armor, suit, weapons/ammo, damage rules and healing rates are untouched.

After owned process cleanup the existing exact restoration guard restores
liblist bytes/metadata and removes all new run-scoped addon files/directories.
Preparation failure before orchestrator invocation uses the same guard to
restore the file-only transaction. If owned process cleanup cannot be attested,
the existing fail-closed retained-backup/recovery policy remains in force;
restoration is never falsely labelled exact.

## Ordinary observation path and handoff

The unmodified game publishes clientdata/Health through the existing transport,
decoder, G1 HalfLifeClientModule, canonical state and HUD. Nothing locally
sets HP, max HP or HUD values. The application test-only readiness gate accepts
50 only from a fresh committed receiving-client clientdata source; retained,
missing and duplicate sources cannot satisfy it. It is read-only and absent
from normal gameplay policy. While pending the same SDL/RX/TX/render loop
continues; command cadence remains unchanged, motion/action input stays zero
and mouse capture waits for readiness. No automatic movement/Use occurs.
After readiness ordinary first-click/manual control applies. The 15-second
non-sleeping readiness deadline produces a concrete test failure if fresh 50
is unavailable (including a server that still reports 100).

Wrapper results separately report `requested_start_health`,
`server_setup_applied`, `client_observed_health`, `max_health`, and
`max_health_source`. Max health is server-entvars evidence from the verified
helper, **not** a nonexistent wire max-health field. Confirmation requires
both the exact run-bound single server application record and the client's
fresh 50 observation; argument delivery/helper success alone cannot pass.
Missing/duplicate/foreign-run evidence fails closed. The fixed inert helper
grammar survives native diagnostic redaction; no private IDs/auth are retained.
Optional native/wrapper evidence eligibility is false and the wrapper names
`test-server-assisted`, never a fully unmodified stock server profile.
Real runtime failure and cleanup/restoration failures retain precedence.

## Offline verification and handoff

- Helper Debug/Release: 153 fake-engine checks each, actually loading the
  built DLL/exports/API, slot 7, wrong/rejected/ambiguous/non-loopback/dead/free/
  fake/missing/serial-mismatched targets, duplicates, healing, max health,
  ordinary respawn/reconnect and missing/retained client readiness inputs.
- Affected client/orchestrator/tests Debug and Release built incrementally.
  Focused CLI/manual/G1/Use regressions: 46 tests, 2250 assertions per config.
- Native Debug/Release socket-free contract mode tests actual option parser,
  shared server/client argv builders, incompatible scenarios and redaction.
- Launcher: 12 real no-stock PowerShell fake-runner cases, including 50 HP;
  default/explicit maps/reference/off/damage-respawn contracts preserved.
- Independent PowerShell evidence/missing/foreign/duplicate/100-failure and
  rejected-preparation fixtures passed.
- Actual restoration guard with the new profile in a private fake tree:
  exact addon removal/liblist restoration and existing substitution/link,
  external sentinel and publication guards passed.
- Existing core-only (`HLCLIENT_BUILD_GAME_HALFLIFE=OFF`) build/test target:
  93 tests, 1318 assertions; core/API and architecture CTest gates 2/2.
  Metamod/helper are a standalone build, not linked/included by lower targets.
- Actual `-CheckOnly -TestStartHealth 50` from `C:\WINDOWS\system32` passed
  without HLDS/Steam/WFP. Normal off/boot_camp CheckOnly also passed.
- No blanket full CTest, clean build, ASan or actual GL session was needed for
  this bounded harness/config change. No new live/manual success is claimed.

Release `build/bin/Release/hlclient.exe` SHA-256:
`115AD21CB37AEB93C0364146DCD8C0C8F61BC11EA7C0DF63D711084403C69B22`.

Prepared manual command (one session, user launches separately):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference -TestStartHealth 50
```

Live addon loading/healing remains unverified until that user-run session.
The prior run `e8c0b04dbee54005ac51d4418f11ad82` failed at the Steam API
initialization boundary before connect; this optional profile does not fix or
relabel that separate startup failure. Its exact lower cause remains unresolved.
