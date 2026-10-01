# M4.7.3D1: pickups and inventory after G1

Implementation is in the existing G1 production path, not a second parser,
engine loop, socket or client.dll. New live/manual verification is not_run.
Result: halflife_pickups_inventory_integrated_offline_verified.
This is offline integration evidence, not new stock/live verification.

## Historical G1 manual termination

The exact candidate is research-copy-smoke/d2382267b69343eb9e96461a9d2ba169,
whose staged native summary binds server PID 30708, client PID 33896,
valve/boot_camp, port 27243, keyboard-mouse/reference/OpenGL and accepted
connection, schema, runtime and entered-game observations. All seven retained
files have local publication timestamp 2026-09-27 03:14:32. The D1 prechange
inventory preserves their SHA-256 values; they are not rewritten.

Keep these outcomes separate:

- Functionality passed, attribution=user_report: WASD/Shift/jump/duck/
  crouch-walk, smooth prediction, Glock/crowbar selection/fire/reload/
  animations, HUD and camera-local viewmodel. Recoil and combat damage/
  armor absorption were not independently confirmed.
- Application: exit 2, primary_error=runtime_record_failed, close=not_requested.
  There were 361 attempted runtime records and 360 commits; the last attempt
  failed while the user was playing. Scheduler underlying_error=none and
  gl_errors=0. This is not a scripted-achievement failure.
- Scripted motion: not_evaluated in keyboard mode. Prediction was active
  (626 local steps, 254 corrections, 76 replayed commands), but the summary
  verdict was live_prediction_active_accuracy_or_coverage_limited.
- Managed/native result: project-client-nonzero-exit, client-ready=false.
  These do not erase the user's observed functionality.
- Native owned_process_cleanup=exact and application cleanup=complete.
  Restoration remains unknown: only restoration_status=wrapper_pending
  exists; no transaction-bound final functional-smoke-wrapper.json was found.
  No unrelated run's restoration result is substituted.

The retained diagnostic excerpt contains no rejected message's parser enum,
wire opcode/cursor or context. Capture metadata reports no capture truncation,
but only the redacted bounded excerpt was published, not the original stdout.
The exact malformed/unsupported message or semantic field therefore cannot be
reconstructed offline from this run. No claimed root-cause fix or successful
restoration is fabricated. Runtime failure still produces nonzero.

The production manual completion branch already did not require prediction
coverage. D1 fixes the misleading healthy-manual primary_error precedence and
adds a shared tested application-outcome contract. Its typed terminal line
preserves first runtime replay/decoder code, opcode, absolute bit cursor,
record ordinal and transport sequence separately from feature coverage.
Missing diagnostic fields stay unavailable. No raw body/path/authentication
text is retained by this new contract. Native JSON carries application_outcome
and the existing wrapper retains it across restoration.

## Authority and pinned contracts

Reference: local Valve SDK b1b5cf5892918535619b2937bb927e46cb097ba1.
Reviewed actual registration/writers/readers: player.cpp LinkUserMessages,
GiveAmmo, SendAmmoUpdate and UpdateClientData; items.cpp ItemTouch,
Respawn/Materialize and battery MyTouch; healthkit.cpp MyTouch; weapons.cpp
AddToPlayer/AddPlayerItem/duplicate ammo; wpn_shared/hl_wpn_glock.cpp ammo
GiveAmmo route; cl_dll/ammo.cpp, ammohistory.cpp, health.cpp and battery.cpp.
The requested ammo_history.cpp spelling does not exist; ammohistory.cpp is
the actual file. VS2019 hl_cdll/hldll projects compile the listed HL routes.
Our CMake imports SDK headers/reference only, not SDK gameplay source.
Exact stock binary compile macros remain unavailable.

| Message/observation | Module meaning | Counter/ownership effect |
| --- | --- | --- |
| AmmoPickup: two bytes, ammo type/count | One feedback event; zero count ignored like reference history | None; exact count labels the event only |
| WeapPickup: one bounded nonzero weapon ID | Weapon feedback; unsupported IDs explicitly labeled | None; catalog/model IDs do not establish ownership |
| ItemPickup: one NUL-terminated inert token, at most 63 characters | Health-kit/battery name or explicit unsupported item label | None; no guessed HP/armor gain |
| AmmoX | Absolute reserve and exact per-slot message source | Server absolute value, never added to notification count |
| CurWeapon | Existing active weapon/clip semantics plus per-clip source | Independent from catalog, ownership and model index |
| Health/Battery | Existing absolute values with sources | Not derived from ItemPickup or Damage |
| clientdata owned_weapon_bits | Current canonical inventory | Selection uses only these bits and a safe catalog token |

Numeric registration IDs/sizes and bounded body framing remain in the generic
decoder; the module dispatches by current-session registered name. Unknown
registered messages keep the existing explicit ignore policy. Unknown inert
ItemPickup tokens are displayed as unsupported, not executed/opened; malformed
or unsafe tokens reject the complete record. There is no byte scanning.

Notifications may precede or follow absolute counters/ownership in different
records. No simultaneous-arrival rule is imposed. A weapon message alone
cannot unlock selection; repeat messages never duplicate catalog entries.
Stock sources do not guarantee WeapPickup for every weapon route; absent
notifications are not fabricated from ownership changes or entity culling.
Ownership/selection still updates through clientdata. Unknown weapons may
receive safe selection requests, but have no Glock/crowbar firing fallback.

## Files, API and lifecycle

- games/halflife/server_messages.cpp stages owning pickups and absolute
  counter provenance without making weapon state fresh for feedback alone.
- games/halflife/client_module.cpp moves committed events into its existing
  presentation owner only after host/world validation.
- games/halflife/presentation.hpp/.cpp owns four feedback rows. Their five-second
  lifetime starts once at a finite monotonic presentation-clock call; repeated
  render/observe/reconciliation cannot restart or duplicate them. Expired rows
  cannot revive after clock rollback. Output uses existing bounded HUD
  rectangles/texts (at most two rectangles/five texts for this slice).
- games/halflife/inventory.cpp rejects dead and pre-death-source ownership.
  Fresh server new-life inventory replaces old bits; there is no UI cache.
- GameRecordState adds owning Pickup events, a source high-water and a
  64-events-per-record bound. Bodies/names remain stage-only immutable borrows.
  GameClientHost validates bounds and exact input-message sources. commit_record
  uses fixed rows/string moves and does not allocate.
- RuntimeWeaponHudObservation adds per-slot reserve/clip source metadata.
  Generic validation rejects invalid sources; existing canonical/value hashes
  intentionally remain unchanged, as with other provenance metadata.
- HudState adds owning feedback rows/count/revision for neutral telemetry.
  Main only wires these counts and application diagnostics; it adds no
  Half-Life parsing, pickup, inventory or visibility rules.

Death clears life-local rows; accepted respawn starts an empty history.
Post-death reassembled feedback whose life cannot be proved is suppressed.
Network/map generation reset and teardown also clear rows/high-water.
Life reset does not touch sockets, netchan/command sequences, protocol delta
histories or prepared/imported/GPU assets.

B1 fire/reload/swing profiles and action identities are unchanged. Pickup-only
messages never change clip/reserve, restart deploy or confirm reload. An
absolute reserve increase alone cannot confirm reload; the existing timer/
clip-decrement/coherent-reload rules remain authoritative for presentation.

World items use the existing generic full/delta reconstructed set, model
binding and EF_NODRAW bit (128). Hidden/absent items retain imported/GPU assets,
and server return reuses them. PVS, culling or omission in a delta neither
changes ownership nor creates feedback. No local item-respawn timer exists.
Renderer knows no healthkit/battery/ammo names.

Dependency direction remains application -> concrete Half-Life + host/API;
lower engine targets do not link/include the concrete module. Core-only
HLCLIENT_BUILD_GAME_HALFLIFE=OFF and the independent alternate module use the
same production host/API, without installed assets or a Half-Life fallback.
G1 normal/core-only build commands remain unchanged.

## Manual handoff, not run automatically

No pathfinding or pickups-check flag is added. The existing manual command
remains one invocation/one session, with reference/off and the separate
damage-respawn-check option preserved:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

Walk normally to available items without cheats or fixed spawn coordinates.
Observe Glock ammo feedback once, then the server reserve (not a double add);
weapon ownership enables existing digit/wheel selection when confirmed.
Battery feedback and server ARM value are independent; test a health kit only
with genuinely incomplete HP, otherwise report healthkit=not_tested.
At full capacity a refused touch creates no client-side gain/notification.
Repeat a pickup only if the server returns/allows it. A hidden/absent item
returns only when the server publishes it; no fixed respawn timing is promised.
Feedback expires in five seconds; existing fire/reload/crowbar, input/focus,
camera/prediction and death/respawn behavior should remain intact.

Terminal application_outcome includes bounded inventory_notifications and
feedback_rows, independently of prediction/scripted coverage. It is supporting
telemetry, not a live pickup/respawn/armor-absorption proof.

## Verification

Prechange Release SHA-256:
2821F1BA2D6E2136A8458B2163A573871493ED977E920FB5F638C4C8641631D0.
Offline baseline: 202 cases passed, one optional-local-assets skip,
38,111 assertions, seed 2532982175. Source snapshot: 134 files hash-verified.
Existing build trees were reused without cleanup. Affected normal Debug and
Release, existing ASan Debug and HL1-disabled core-only Release builds passed.
Release hlclient.exe and capture/check/orchestrator/fake-process helpers are
ready for the unchanged launcher. No private SDK assets are a core/test build
dependency. Initial compile/fixture failures were corrected; their logs remain
separate from the passing final gates.

| Gate | Actual result |
| --- | --- |
| New pickups/manual-result regressions, Release | 10 passed, 318 assertions |
| Focused G1/B1/C/H3/H4/input/replay, Debug and Release | Each 212 passed + 1 optional-assets skip, 38,429 assertions |
| Same focused set, ASan Debug | 212 passed + 1 optional-assets skip, 38,429 assertions |
| Core/API alternate-module tests, normal Debug/Release, ASan Debug and core-only Release | Each 93 passed, 1,318 assertions; same host, no HL1 fallback |
| Architecture guard, core-only | Passed, including direct/alias/transitive/generator/source/object/include violation fixtures |
| Actual core-only projects/source lists and compiler/link inputs | 164 project files inspected; no concrete HL1 project, source, link or core/API compiler read |
| Actual OpenGL HUD/world/B1/A1 controls, Release | 5 passed, 390 assertions, no GL skips/errors |
| Optional local MDL viewmodel GL control, read-only | 1 passed, 260 assertions |
| Null/process-level application replay checks | 8/8 passed |
| Existing local capture, read-only Null replay | 323/323 applied, 0 errors, canonical hash 7014210005320501317 unchanged |
| Native typed-summary parser, PowerShell failure retention and no-stock fake-process publication roundtrip | Passed; stock launch absent |
| Launcher reference -CheckOnly from System32 | Passed; no runner/game/Steam/WFP/socket started |
| One final broad Release offline suite | 2,097 passed + 21 capability/opt-in skips, 386,989 assertions |
| git diff --check, additional new-file whitespace checks | Passed |

All deterministic test runs use seed 2532982175. The single broad suite excludes
udp/network/isolation/orchestrator/steam/live/loss/security labels; it is not
blanket CTest or a live campaign. Its skips cover optional installed assets,
unavailable Windows symlink/custom-reparse/second-volume capabilities and
hidden-SDL focus/relative-mouse capabilities. A warning-only symlink assertion
also remains skipped; these capabilities are not reported as passed. Actual GL
tests ran; fake-process checks are not stock/live evidence.

The primary fixtures are independent project-owned data. Extra local MDL and
capture controls are read-only supplements, not reproducibility requirements.
Logs and the prechange snapshot are under manual-artifacts/task-records/d1-*;
production source, public headers and tests are ordinary project files.

Prechange snapshot: all 134 copied source files reverified, and all seven files
of the exact historical G1 run unchanged. G1/B1/C/H3/H4 contracts and manual
launcher remain byte-identical to their D1 prechange inventory entries.
Snapshot inventory SHA-256:
0CBAF645AD2EA01A70CB3052796D5C4B10CCC6D25EFEA1EDC5EF8B13F2608794.
Branch/HEAD unchanged; staging remains empty.

Final build/bin/Release/hlclient.exe SHA-256:
20610194D4EEC2C6897BD0FE592D03329CBB3D6ACBD603330BE180BA683E718E.

Remaining limits: exact historical rejected parser/body unavailable; old final
restoration unknown; new pickups/item-return/death/respawn manual not_run;
healthkit needs genuinely incomplete HP; battery pickup does not prove combat
armor absorption; no additional weapon firing/prediction, sounds or CS support.

New live/managed/HLDS/Steam/WFP launches=0; commit/push/staging=none.
Historical artifacts and prior G1/B1/C reports remain unchanged.
