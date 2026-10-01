# D1: crossfire manual default (2026-09-27)

Result: `crossfire_manual_default_prepared_offline_verified`.

## New user evidence, not a rewritten historical report

Attribution: `user_report`. The user identifies the completed interactive run
`56902db1ebcb41f1b79c9bb5c232b63f`: server PID 32776, client PID 39552,
connection accepted, client ready/map entry observed, client/server exit 0,
prediction `live_local_prediction_and_reconciliation_verified`,
ServerInfo `maps/boot_camp.bsp`. Its transaction directory contains a native
`functional-smoke.staged.json` and redacted diagnostic logs, dated 2026-09-27
13:42 local time. These match the PID, input=keyboard-mouse, prediction=reference,
90-second profile and map reported by the user. Native owned cleanup is exact;
the application completed without a primary error. No final wrapper report or
transaction-bound restoration attestation is present: restoration remains
unknown, not inferred from exit 0. Run `769d55386fbe42a8857642ec125e2eea` is not
substituted for this transaction.

The user could not properly check a health kit (missing HP was needed) and did
not find a battery/armor. Health/armor pickup manual validation remains
`not_tested`, neither failed nor passed. This successful run does not establish
a fix for the older unrepresented rejected runtime record. Existing G1/D1/B1/C
reports, prior `manual validation=not_run` records, captures and hashes are
unchanged. This document is additive evidence only.

## One selected map, one session

`Start-HLClient-H2-Manual.ps1` now defaults to `-Map crossfire`. Explicit
`-Map boot_camp` and `-Map stalkyard` remain supported. Prediction reference/off,
keyboard-mouse default and Scenario manual/damage-respawn-check are unchanged.
One invocation still calls the same managed runner once, without a second map,
launcher or session. The banner includes the selected map. Paths are anchored
at PSScriptRoot, not the caller's current directory.

The three-value basename allowlist rejects native paths, traversal, BSP paths,
unknown names and console fragments before any runner/game launch. The launcher
checks `valve/maps/<selected>.bsp`, not a fixed boot_camp prerequisite. The runner
additionally checks containment, no reparse ancestors, default data stream and
no hard links using the existing safe-path mechanisms. Its functional immutable
projection compares the selected BSP against the approved source installation;
a missing/changed selected map gives a concrete error, never a fallback.
Map-independent preflight can still omit Map; explicit read-only functional
preflight accepts `-Map crossfire`. No preparation manifests are rewritten.

Actual production route:

| Boundary | Selected value / mechanism |
| --- | --- |
| Launcher -> capture_stock_runtime_state.ps1 | Game=valve, Map=crossfire, existing native PowerShell argv |
| Runner -> native orchestrator | --game valve --map crossfire, both environment gate and owned transaction |
| Native option parser -> owned HLDS | narrow_safe_token -> options.map -> +map crossfire |
| HLDS readiness | banner/local-info query compares actual map to options.map |
| Project client assets | ServerInfo's observed virtual map -> existing sandboxed resolver/importers/resource readiness |
| Diagnostics | native map=requested basename, serverinfo_map=observed value or null |
| New wrapper map_evidence | requested_map, expected_serverinfo_map, observed_serverinfo_map, comparison |

Expected ServerInfo is `maps/crossfire.bsp`; it is never used to fill an absent
observation. A mismatched observation stays mismatched in diagnostics. Existing
schema/protocol grammar, Steam AppID 70, protocol 48, ports 27243/27242, server
binary profile and all historical literal fixtures remain unchanged. The native
orchestrator needs no source or binary change for crossfire forwarding.

Scenario audit: Half-Life `DamageRespawnScript` uses authoritative life epochs,
one validated self-kill request, release/ordinary respawn press, a 0.4-second
forward pulse, server sample counts and owned Glock/crowbar bindings. There is
no boot_camp spawn coordinate, target position or geometry route. It remains
available, but its completion on crossfire has **not** been live verified.
No healthkit route/fall/damage is scripted. The other runner scripted input
checks use relative commands/observations, not a fixed boot_camp route; their
geometry-dependent coverage is not promised on crossfire. The separate historical
boot_camp/baseline canary and literal fake lifecycle fixtures keep their explicit
map contracts; they are not the interactive manual route.

## Actual local crossfire entities (read-only)

File: `D:\DEV\HLCLIENT-RESEARCH\Half-Life\valve\maps\crossfire.bsp`.
SHA-256: `6222243E0839022F3041E6B9E97776CE54D0CF87D4C3810E627B8B2815B555F5`.
Production BSP/entity parsers report version 30 and 251 entities.
The existing CPU `hlclient_bsp_compat_check` gained an opt-in
`--inspect-entities` metadata output; default output is unchanged. It reuses
GoldSrcBspParser, GoldSrcEntityDocumentParser, parse_entity_vector3 and
parse_brush_model_reference; no new BSP/entity parser, asset mutation or game
loop is introduced. Classnames are bounded inert identifiers; numeric output
is finite and locale-independent. Output is bounded by existing parser limits.

All positions below are BSP initial coordinates in (x,y,z), **not guaranteed
current server entity positions**, visibility or availability. No room names
or travel route is inferred. Counts do not prove live pickup behavior.

| Pickup | Count | Initial coordinates |
| --- | ---: | --- |
| item_healthkit | 6 | (-688,960,-1680), (-688,992,-1680); (624,1488,-1664), (624,1456,-1664), (624,1440,-1664), (624,1424,-1664) |
| item_battery | 11 | (848,1312,-1520), (816,1312,-1520); (472,280,-1520), (472,312,-1520); (-712,280,-1520), (-680,280,-1520); (624,1264,-1664), (624,1280,-1664), (624,1296,-1664), (624,1312,-1664), (624,1328,-1664) |

Wall stations lack origin keys. These are validated BSP brush bounds, not
invented point origins or server positions:

| Station | Entity/model | Initial bounds minimum -> maximum |
| --- | --- | --- |
| func_healthcharger (6 total) | 3/*1 | (-144,-2112,-1728) -> (-112,-2104,-1680) |
| func_healthcharger | 4/*2 | (-64,-2112,-1728) -> (-32,-2104,-1680) |
| func_healthcharger | 5/*3 | (32,-2112,-1728) -> (64,-2104,-1680) |
| func_healthcharger | 6/*4 | (112,-2112,-1728) -> (144,-2104,-1680) |
| func_healthcharger | 135/*40 | (448,384,-1504) -> (456,416,-1456) |
| func_healthcharger | 136/*41 | (-848,256,-1504) -> (-816,264,-1456) |
| func_recharge (2 total) | 105/*38 | (272,1200,-1824) -> (280,1232,-1776) |
| func_recharge | 106/*39 | (272,432,-1824) -> (280,464,-1776) |

These stations require Use, unlike touch pickups. SDL recognizes physical E,
but the current Half-Life live button mask/reference policy excludes Use; the
manual client therefore cannot exercise these stations via Use/E. No Use
system is added in this task and a station is not counted as a battery pickup.

| Actual ammunition classname | Count | One verified initial coordinate |
| --- | ---: | --- |
| ammo_357 | 4 | (-624,1512,-1680) |
| ammo_9mmAR | 3 | (832,720,-1840) |
| ammo_ARgrenades | 4 | (-72,1120,-1520) |
| ammo_buckshot | 9 | (616,-2576,-1784) |
| ammo_crossbow | 18 | (600,-1736,-1744) |
| ammo_gaussclip | 14 | (616,-2624,-1784) |
| ammo_rpgclip | 19 | (616,-2648,-1784) |

Touch pickups can exercise D1's existing server-notification feedback and
absolute HP/armor/ammo/HUD publication when the server permits the pickup;
unsupported weapons do not gain new firing/presentation support here.
AmmoPickup is feedback only; AmmoX is absolute, never a second additive gain.
Hidden/returned items remain controlled by server snapshots, not proximity,
PVS/culling or a local respawn timer.

Health kit eligibility: alive and server HP below its supported maximum;
full HP is `not_eligible/full_health`, not a pickup error. Obtain missing HP
only through ordinary server-confirmed gameplay; no safe-fall guarantee is
made. If missing HP cannot be obtained, retain `not_tested`.
Battery eligibility: alive, required suit, armor below the supported server
maximum. Missing HP is not required. Verify server armor separately from
feedback; pickup armor does not prove combat damage absorption. Nothing changes
HP/armor locally, server DLLs, HUD authority, cheats/RCON or impulse 101.

## Targeted verification and handoff

No full engine suite, clean build, new core-only rebuild or unrelated rebuild
was needed: engine/game policy/API and CMake dependency graph are unchanged.
Only the existing affected CPU inspector was rebuilt in Debug/Release. The
ready Release hlclient.exe remains byte-identical:
`10A124BDDF88D762BFF358C4703389CC8A190AA3161AF3814A1C921035C0B6FD`.

Passed gates:

- Existing no-stock launcher regression: 11 fake process cases, default and
  explicit maps, actual argv including space paths, reference/off, Scenario,
  exit/report/first-cause forwarding and foreign-report rejection.
- All three maps x both prediction modes x both Scenarios validated against
  runner parameter metadata. Six invalid/path/injection map inputs rejected.
  Missing crossfire cannot be satisfied by existing boot_camp. Requested,
  matched, absent and mismatched observed map evidence stay distinct.
- Actual required-files CheckOnly from System32 defaults to crossfire;
  project-root explicit boot_camp/off/damage-respawn-check also passes.
- Existing functional projection self-test plus selected-crossfire fixture;
  actual read-only -ValidateFunctionalResearchRoot -Map crossfire reports
  prepared_projection_content_verified, files-written=0, stock launches=0.
- Existing entity parser/transform regressions: 8 cases, 141 assertions.
  Project-owned CPU inspector regression: exact origin/brush bounds, unchanged
  default output, repeat determinism, duplicate flag/path rejection, Debug/Release.
- Actual local BSP inspection repeated with Debug/Release; no asset writes.
- git diff --check and preservation checks; index/branch/HEAD unchanged.

Native argument construction/readiness/observed-map separation was source
audited; no native WFP/environment/live mode was invoked to test forwarding.
Fake process and asset inspection tests are not stock/live evidence.
Logs/snapshot are `crossfire-*.txt` and
`manual-artifacts/task-records/crossfire-prechange`. Required changes are normal
project files, not code hidden in artifacts. No old build/history was deleted.

Run in an administrative PowerShell 7 console, unchanged single command:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File "D:\DEV\CPP\HLC-steamcfg-5e48b7c1\Start-HLClient-H2-Manual.ps1" -Prediction reference
```

New live/HLDS/stock-client/Steam initialization/WFP launches=0.
Health/armor pickups on crossfire remain `not_tested`; historical runtime root
cause remains unresolved. Staging/commit/push=none.
